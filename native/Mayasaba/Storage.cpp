#include "Storage.h"
#include "ProjectRoot.h"
#include "NativeIds.h"
#include "ProtectedDraft.h"
#include "Sha256Hex.h"
#include <filesystem>
#include <utility>
#include <sstream>
#include <iomanip>
#include <unordered_set>
#include <array>
#include <optional>
#include <string_view>
#include <thread>
#include <chrono>
#include <algorithm>
#include <memory>
#include <cctype>
#ifdef _WIN32
#include <shlobj.h>
#include <bcrypt.h>
#endif

namespace mayasaba
{
    namespace
    {
        // One translation for every path that can fail because a statement was denied.
        // SQLite reports an authorizer denial as SQLITE_AUTH with the bare message
        // "not authorized"; admission and the authorizer backstop must be
        // indistinguishable to a caller, so prepare, step and exec all surface the
        // same quarantine explanation.
        inline std::string StatementFailureMessage(sqlite3* db, int rc, char const* sqliteExecMessage = nullptr)
        {
            if (rc == SQLITE_AUTH)
            {
                return "The store is quarantined after an unconfirmed transaction rollback, so this change was "
                       "refused. Reopen the project store and reconcile the unresolved submission before retrying.";
            }
            return sqliteExecMessage ? sqliteExecMessage : sqlite3_errmsg(db);
        }
        struct Statement
        {
            sqlite3_stmt* value{};
            Statement(sqlite3* db, char const* sql)
            {
                auto const rc = sqlite3_prepare_v2(db, sql, -1, &value, nullptr);
                if (rc != SQLITE_OK)
                    throw std::runtime_error(StatementFailureMessage(db, rc));
            }
            ~Statement() { if (value) sqlite3_finalize(value); }
            Statement(Statement const&) = delete;
            Statement& operator=(Statement const&) = delete;
        };

#ifdef _WIN32
        inline std::string WideToUtf8(std::wstring const& w)
        {
            if (w.empty()) return {};
            int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
            std::string s(size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), size, nullptr, nullptr);
            return s;
        }
        inline std::wstring Utf8ToWide(std::string const& s)
        {
            if (s.empty()) return {};
            int size = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(size, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), size);
            return w;
        }

        struct FileHashResult
        {
            bool success{false};
            uint64_t size{0};
            std::string sha256;
            std::string error;
        };

        inline FileHashResult ComputeSha256FromHandle(HANDLE hFile)
        {
            FileHashResult res;
            if (hFile == INVALID_HANDLE_VALUE)
            {
                res.error = "Invalid handle";
                return res;
            }

            LARGE_INTEGER size{};
            if (!GetFileSizeEx(hFile, &size))
            {
                res.error = "Cannot query file size";
                return res;
            }
            res.size = static_cast<uint64_t>(size.QuadPart);

            LARGE_INTEGER zero{};
            SetFilePointerEx(hFile, zero, nullptr, FILE_BEGIN);

            BCRYPT_ALG_HANDLE hAlg = nullptr;
            NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
            if (!BCRYPT_SUCCESS(status))
            {
                res.error = "BCryptOpenAlgorithmProvider failed";
                return res;
            }

            DWORD hashObjSize = 0, dataLen = 0;
            status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&hashObjSize, sizeof(DWORD), &dataLen, 0);
            if (!BCRYPT_SUCCESS(status) || hashObjSize == 0)
            {
                BCryptCloseAlgorithmProvider(hAlg, 0);
                res.error = "BCryptGetProperty failed";
                return res;
            }

            std::vector<BYTE> hashObj(hashObjSize);
            BCRYPT_HASH_HANDLE hHash = nullptr;
            status = BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0);
            if (!BCRYPT_SUCCESS(status))
            {
                BCryptCloseAlgorithmProvider(hAlg, 0);
                res.error = "BCryptCreateHash failed";
                return res;
            }

            constexpr DWORD BUFFER_SIZE = 64 * 1024;
            std::vector<BYTE> buffer(BUFFER_SIZE);
            DWORD bytesRead = 0;
            uint64_t totalRead = 0;
            bool readError = false;

            while (true)
            {
                BOOL ok = ReadFile(hFile, buffer.data(), BUFFER_SIZE, &bytesRead, nullptr);
                if (!ok)
                {
                    readError = true;
                    res.error = "ReadFile returned error " + std::to_string(GetLastError());
                    break;
                }
                if (bytesRead == 0)
                {
                    break;
                }
                totalRead += bytesRead;
                status = BCryptHashData(hHash, buffer.data(), bytesRead, 0);
                if (!BCRYPT_SUCCESS(status))
                {
                    readError = true;
                    res.error = "BCryptHashData failed";
                    break;
                }
            }

            if (readError || totalRead != res.size)
            {
                if (!readError) res.error = "File size mismatch during read";
                BCryptDestroyHash(hHash);
                BCryptCloseAlgorithmProvider(hAlg, 0);
                return res;
            }

            BYTE hash[32];
            status = BCryptFinishHash(hHash, hash, 32, 0);
            BCryptDestroyHash(hHash);
            BCryptCloseAlgorithmProvider(hAlg, 0);

            if (!BCRYPT_SUCCESS(status))
            {
                res.error = "BCryptFinishHash failed";
                return res;
            }

            std::ostringstream ss;
            for (int i = 0; i < 32; ++i)
            {
                ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
            }
            res.sha256 = ss.str();
            res.success = true;
            return res;
        }

        inline FileHashResult ComputeFileSha256Safe(std::wstring const& path)
        {
            FileHashResult res;
            if (path.empty())
            {
                res.error = "Path is empty";
                return res;
            }

            HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE)
            {
                hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, nullptr);
            }
            if (hFile == INVALID_HANDLE_VALUE)
            {
                res.error = "Cannot open file";
                return res;
            }

            res = ComputeSha256FromHandle(hFile);
            CloseHandle(hFile);
            return res;
        }

        inline std::string ComputeFileSha256(std::wstring const& path, uint64_t* outSize = nullptr)
        {
            auto res = ComputeFileSha256Safe(path);
            if (!res.success) return {};
            if (outSize) *outSize = res.size;
            return res.sha256;
        }

        inline std::wstring GetCanonicalPathFromHandle(HANDLE hFile)
        {
            std::vector<wchar_t> buffer(MAX_PATH);
            DWORD len = GetFinalPathNameByHandleW(hFile, buffer.data(), static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (len >= buffer.size())
            {
                buffer.resize(len + 1);
                len = GetFinalPathNameByHandleW(hFile, buffer.data(), static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            }
            if (len == 0) return {};
            std::wstring path(buffer.data(), len);
            if (path.rfind(L"\\\\?\\", 0) == 0) path = path.substr(4);
            return path;
        }

        inline std::wstring GetCanonicalDirectoryPath(std::wstring const& dirPath)
        {
            HANDLE h = CreateFileW(dirPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (h == INVALID_HANDLE_VALUE) return {};
            auto res = GetCanonicalPathFromHandle(h);
            CloseHandle(h);
            return res;
        }

        // Returns the lowercase hex SHA-256 of `input`, or std::nullopt when any BCrypt
        // operation fails. Callers must treat nullopt as "no integrity signature available"
        // and fail closed; a partially-initialised digest must never be emitted.
        // The digest is the full 32 bytes (64 hex characters) per the registry's
        // Sha256 definition (^[0-9a-f]{64}$).
        inline std::optional<std::string> ComputeStringHashHex(std::string_view input)
        {
            if (input.size() > static_cast<size_t>(ULONG_MAX)) return std::nullopt;

            BCRYPT_ALG_HANDLE hAlg = nullptr;
            NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
            if (!BCRYPT_SUCCESS(status)) return std::nullopt;

            struct AlgCloser
            {
                BCRYPT_ALG_HANDLE handle;
                ~AlgCloser() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
            } algCloser{ hAlg };

            DWORD hashObjSize = 0, dataLen = 0;
            status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PBYTE>(&hashObjSize), sizeof(DWORD), &dataLen, 0);
            if (!BCRYPT_SUCCESS(status) || hashObjSize == 0) return std::nullopt;

            std::vector<BYTE> hashObj(hashObjSize);

            BCRYPT_HASH_HANDLE hHash = nullptr;
            status = BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0);
            if (!BCRYPT_SUCCESS(status)) return std::nullopt;

            struct HashCloser
            {
                BCRYPT_HASH_HANDLE handle;
                ~HashCloser() { if (handle) BCryptDestroyHash(handle); }
            } hashCloser{ hHash };

            status = BCryptHashData(hHash,
                reinterpret_cast<PBYTE>(const_cast<char*>(input.data())),
                static_cast<ULONG>(input.size()), 0);
            if (!BCRYPT_SUCCESS(status)) return std::nullopt;

            std::array<BYTE, 32> digest{};
            status = BCryptFinishHash(hHash, digest.data(), static_cast<ULONG>(digest.size()), 0);
            if (!BCRYPT_SUCCESS(status)) return std::nullopt;

            static char const* kHex = "0123456789abcdef";
            std::string hex;
            hex.reserve(digest.size() * 2);
            for (BYTE byte : digest)
            {
                hex.push_back(kHex[byte >> 4]);
                hex.push_back(kHex[byte & 0x0F]);
            }
            return hex;
        }

        inline bool IsEnglishSafeText(std::string_view text)
        {
            if (text.empty()) return false;

            // 1. Byte and character check: all characters must be standard whitespace,
            // printable ASCII (0x20..0x7E), or approved multi-byte English typography
            size_t i = 0;
            while (i < text.size())
            {
                unsigned char c = static_cast<unsigned char>(text[i]);
                if (c == '\t' || c == '\n' || c == '\r')
                {
                    i++;
                    continue;
                }
                if (c >= 0x20 && c <= 0x7E)
                {
                    i++;
                    continue;
                }
                // Check allowed multi-byte UTF-8 English typography: smart quotes, dashes, ellipsis, nbsp
                if (c == 0xC2 && (i + 1 < text.size()) && static_cast<unsigned char>(text[i + 1]) == 0xA0)
                {
                    i += 2;
                    continue;
                }
                if (c == 0xE2 && (i + 2 < text.size()))
                {
                    unsigned char c2 = static_cast<unsigned char>(text[i + 1]);
                    unsigned char c3 = static_cast<unsigned char>(text[i + 2]);
                    if (c2 == 0x80)
                    {
                        if (c3 >= 0x90 && c3 <= 0xBF)
                        {
                            i += 3;
                            continue;
                        }
                    }
                }
                return false;
            }

            // 2. Tokenize text into whitespace-delimited tokens
            std::vector<std::string> tokens;
            std::string curTok;
            for (char ch : text)
            {
                if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
                {
                    if (!curTok.empty())
                    {
                        tokens.push_back(std::move(curTok));
                        curTok.clear();
                    }
                }
                else
                {
                    curTok.push_back(ch);
                }
            }
            if (!curTok.empty())
            {
                tokens.push_back(std::move(curTok));
            }

            if (tokens.empty()) return false;

            // 3. Conservative positive validation of source prose against an English lexicon.
            //
            // This check does not prove that arbitrary prose is English, and no caller may
            // present a pass as such a proof. It is the positive half of a fail-closed
            // filter: when language eligibility cannot be positively established, the
            // content preview is withheld and only metadata is shown. Presence of
            // punctuation, digits or underscores grants no permission, and one
            // unrecognized token is enough to withhold the preview.
            //
            // The vocabulary is deliberately broad with everyday English, because a filter
            // that rejects ordinary prose withholds content the user can lawfully read. It
            // stays strict about non-Latin scripts, mixed-script tokens and words it does
            // not know. The previous list omitted high-frequency function words such as
            // "of", "not", "one", "no" and "like", so ordinary English was refused.
            static constexpr std::string_view englishVocabulary =
                // Articles, determiners, pronouns, quantifiers
                "a an the this that these those i you he she it we they me him her us them my your his its "
                "our their mine yours hers ours theirs myself yourself himself herself itself ourselves "
                "themselves who whom whose which what when where why how whoever whatever whichever "
                "whether either neither each every all any some none both few many much more most less "
                "least several other another such same own only one two three four five six seven eight "
                "nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen "
                "twenty thirty forty fifty sixty seventy eighty ninety hundred thousand million billion "
                "trillion dozen quarter half zero first second third fourth fifth sixth seventh eighth "
                "ninth tenth last next "
                // Prepositions and conjunctions
                "of in on at to for with from by about into through during before after above below "
                "between under over up down out off across against along among around behind beyond near "
                "since until within without upon onto toward towards per via than as and or but nor so if "
                "because although though while unless whereas whilst then thus hence therefore however "
                "moreover furthermore nevertheless nonetheless instead otherwise meanwhile "
                // Auxiliaries and modals
                "be is am are was were been being have has had having do does did done doing will would "
                "shall should can could may might must ought need dare used "
                // Common verbs, including irregular past forms
                "accept achieve acknowledge acquire act adapt add address adjust admit adopt advance advise "
                "affect agree aim align allocate allow alter analyze announce answer anticipate appear apply "
                "appreciate approach approve argue arrange arrive assess assign assist assume assure attach "
                "attack attempt attend attract avoid await back balance base bear beat become begin begun "
                "behave belong benefit block borrow bother break broke broken bring brought broaden build "
                "built burn buy bought calculate call came capture care carry catch caught cause challenge "
                "change charge check choose chose chosen cite claim clarify clean clear close collect "
                "combine come comment commit communicate compare compete compile complete compose compute "
                "conclude conduct confirm connect consider consist constitute construct consult consume "
                "contain continue contribute control convert convince correct cost count cover create "
                "critique cross cut deal decide declare decline decrease define delay deliver demand "
                "demonstrate deny depend describe design desire destroy detect determine develop differ "
                "direct disagree discover discuss dismiss display distribute divide document double doubt "
                "draft draw drew drawn drive drove driven drop earn edit effect eliminate employ enable "
                "encourage end enforce ensure enter establish estimate evaluate examine exceed exchange "
                "exclude execute exhibit exist expand expect explain explore express extend face fail fall "
                "fell fallen favor fear feature feel felt fetch fight figure file fill filter find finish "
                "fit fix fly flew flown focus follow force forecast forget forgot forgotten form forward "
                "frame fulfill fund gain gather generate get give gave given go went gone govern grant grow "
                "grew grown guarantee handle hang happen harm hate head hear heard help hide highlight "
                "hire hold held honor hope host identify ignore illustrate imagine implement imply import "
                "impose improve include incorporate increase indicate influence inform inherit initiate "
                "injure insert insist inspect install intend interact interpret introduce invest "
                "investigate invite involve isolate issue join judge justify keep kept kick know knew known "
                "label land last launch lead led learn leave left lend let level lie lift light like limit "
                "link list listen live load locate lock look lose lost love lower maintain make made manage "
                "map mark match matter mean meant measure meet met mention merge migrate mind minimize miss "
                "mitigate mix modify monitor mount move multiply name navigate need negotiate note notice "
                "number observe obtain occur offer open operate oppose optimize order organize output "
                "overcome override overwrite own pack pair parse pass pause pay paid perform permit "
                "persist pick place plan play please point poll prepare present preserve prevent print "
                "prioritize proceed process produce prohibit promise promote prompt propose protect prove "
                "provide publish pull purchase push put qualify query question quote raise ran range rate "
                "reach react read realize reason rebuild receive recognize recommend reconcile record "
                "recover reduce refer reflect refuse regard register reject relate release rely remain "
                "remember remind remove render repeat replace reply report represent request require "
                "rescue reserve reset resolve respond restore restrict result retain retrieve return "
                "reveal reverse review revise reward rewrite ride rise risk roll round rule run satisfy "
                "save say said scan schedule score search secure see saw seen seek seem select sell send "
                "sent separate serve set settle share shift ship shorten show showed shown shrink shut "
                "sign signal simplify sit sat skip sleep slept slow solve sort sound specify spend spent "
                "split spread stand stood start state stay step stick stop store stream strengthen strike "
                "strip study submit succeed suffer suggest summarize supply support suppose suppress "
                "surface survive suspend switch take took taken talk target teach taught tell tend "
                "terminate test thank think thought throw threw thrown tie track train transfer transform "
                "treat trigger trust try turn type understand understood undertake unify update upgrade "
                "upload use used utilize validate value vary verify view visit vote wait wake walk want "
                "warn watch wear wore worn weigh welcome win won withdraw wish work worry write wrote "
                "written "
                // Common nouns
                "ability access account accuracy action activity address age agency agent agreement aim "
                "air algorithm alternative amount analysis answer application approach area argument art "
                "article aspect assessment asset assumption attention attitude authority availability "
                "average awareness background backup balance barrier base basis batch behavior benefit "
                "body book boundary branch budget bug build business byte card case category cause cell "
                "center century chain chance change channel chapter character charge chart check child "
                "choice circumstance city claim class client code collection column combination comment "
                "commission committee community company comparison compatibility complexity component "
                "composition concept concern conclusion condition configuration conflict consequence "
                "consideration consistency constraint construction contact content context control "
                "conversation copy core corner cost council count country couple course court coverage "
                "criteria criterion culture currency current customer cut damage data database date day "
                "deal death debt decision declaration decline default defect definition degree delay "
                "delivery demand department dependency description design desire detail detail detection "
                "development device difference difficulty dimension direction directory discipline "
                "discovery discussion disk display distinction distribution document domain door draft "
                "drive duty edge effect effort element emergency employee end energy engine entry "
                "environment epoch error essay estimate evaluation event evidence example exception "
                "exchange execution exercise existence expansion expectation experience experiment "
                "expert explanation exposure expression extension extent facility fact factor failure "
                "family feature fee field figure file filename filter finding firm flag floor flow focus "
                "folder force form format foundation frame framework freedom function funding game gap "
                "general goal good government grade grant group growth guarantee guard guidance guideline "
                "habit hand handle harm hash head health height help history hold home hope hour house "
                "human hypothesis idea identity image impact implementation importance impression "
                "improvement incident inclusion increase independence index individual industry "
                "information infrastructure initial input inquiry inside insight instance institution "
                "instruction instrument integration integrity intent intention interest interface "
                "interpretation interval intervention inventory investigation investment issue item job "
                "journal judgment jurisdiction key kind knowledge label lack language law layer leader "
                "learning lease length level library license life limit line link list literature load "
                "loan local location log logic loss lot maintenance management manager manner map margin "
                "market master match material matrix matter meaning measure mechanism media medium "
                "member memory mention message method metric middle minimum minute mission mistake mode "
                "model modification module moment money month move movement name nation nature need "
                "network news node noise norm note notice notion number object objective obligation "
                "observation obstacle occasion offer office operation opinion opportunity option order "
                "organization origin outcome output overview owner page paper paragraph parameter "
                "parent part participant partner party passage path pattern pause payment peak peer "
                "percentage performance period permission person perspective phase phone phrase place "
                "plan platform point policy position possibility potential power practice precedent "
                "precision preference premise preparation presence pressure price principle priority "
                "procedure process product production profile program progress project promise property "
                "proposal proposition protection protocol provision publication purpose quality quantity "
                "question quota race range rate ratio reach reaction reader reason receipt record "
                "recovery reference region register relation relationship release relevance reliability "
                "remainder remark removal repair repeat replacement report representation request "
                "requirement rescue research reserve resolution resource respect response "
                "responsibility rest result retention review revision reward risk role room root route "
                "rule run safety sample satisfaction scale schedule scope score screen search section "
                "security segment selection sense sequence series service session set setting severity "
                "shape share shift side sign signal signature size situation skill slice source space "
                "specification speed stability staff stage stake standard start state statement status "
                "step stock storage story strategy stream strength stress structure study style subject "
                "submission subset substance success suggestion summary supply support surface survey "
                "switch symbol system table tag target task team technique technology term test text "
                "theme theory thing thread threat threshold time timestamp title token tool topic total "
                "trace track trade traffic training transaction transfer transformation transition "
                "treatment trend trial trigger trust truth turn type unit usage user utility validation "
                "validity value variable variant vector version view violation vision visit voice "
                "volume wait wall want warning way weakness wealth web week weight window wish word "
                "work worker workflow workspace world worry worth writing year zone "
                // Adjectives
                "able absent absolute abstract acceptable accessible accurate active actual additional "
                "adequate adjacent administrative advanced adverse affected aggressive alert alive "
                "alone alternate alternative amazing ambiguous ancient angry annual anonymous apparent "
                "applicable appropriate approximate arbitrary architectural artificial assistant "
                "associated atomic attractive audio automatic available average aware awful bad "
                "balanced bare basic beautiful beneficial best better big binary blank blind blocked "
                "blue bold brief bright broad broken brown busy calm capable capital careful casual "
                "central certain cheap chief chronic circular civic civil classic clean clear clever "
                "clinical closed coarse cognitive cold collaborative collective commercial common "
                "compact comparable compatible competitive complete complex compliant comprehensive "
                "compulsory concrete conditional confident confidential consecutive conscious "
                "considerable consistent constant constitutional contemporary content contiguous "
                "continuous conventional convenient cool cooperative core corporate correct costly "
                "critical crucial cultural cumulative current custom daily dark dead deaf dear decent "
                "deep default defensive definite deliberate delicate dense dependent deployed detailed "
                "different difficult digital direct dirty disabled distant distinct distinguished "
                "diverse domestic dominant double doubtful dry dual due dull dynamic eager early "
                "eastern easy economic educational effective efficient eight either elderly electric "
                "electronic elegant eligible embedded emotional empty enabled enormous entire equal "
                "equivalent essential eternal ethical even eventual evident exact excellent "
                "exceptional excessive exclusive exotic expected expensive experimental explicit "
                "external extra extreme fair faithful familiar famous fast fatal favorable federal "
                "fellow few fierce final financial fine finite firm fiscal fit five fixed flat "
                "flexible fluent following foreign formal former forth forward four fragile free "
                "frequent fresh friendly full functional fundamental funny future general generic "
                "gentle genuine given glad global golden good gradual grand grateful great green grey "
                "gross guilty half handy happy hard harmful harsh healthy heavy helpful hidden high "
                "historical hollow holy honest horizontal hostile hot huge human humble hundred ideal "
                "identical idle ill illegal immediate immense immune imperative imperial implicit "
                "important impossible impressive inadequate inclined independent indirect individual "
                "industrial inevitable infinite informal inherent initial inner innocent innovative "
                "intact integral intelligent intense interactive interested interesting internal "
                "international intimate invalid invisible involved inward iron irregular isolated "
                "joint just keen key kind known large last late later latter lazy leading least left "
                "legal legislative lengthy less level liable liberal light likely limited linear "
                "liquid literary little live lively local logical lonely long loose lost loud low "
                "loyal lucky major male manual marginal marine massive material mathematical mature "
                "maximum mean medical medium mental mere middle mild military minimal minimum minor "
                "minute mixed mobile moderate modern modest moral multiple mutual narrow national "
                "native natural naval near necessary negative nervous neutral new next nice noble "
                "noisy nominal normal northern notable novel numerous objective obvious occasional "
                "odd official old only open opposite optimal ordinary organic original other outer "
                "outgoing outside overall overseas own painful parallel partial particular passive "
                "past patient peaceful peculiar perfect permanent persistent personal physical "
                "plain pleasant pleased plus polite poor popular portable positive possible "
                "potential powerful practical precious precise predictable pregnant preliminary "
                "premium prepared present presidential pretty previous primary prime principal "
                "prior private probable productive professional profound progressive prominent "
                "prompt proper proportional protective proud provincial public pure qualified quick "
                "quiet random rapid rare raw ready real realistic reasonable recent recursive red "
                "reduced redundant regional regular relative relevant reliable religious reluctant "
                "remaining remarkable remote repeated representative residential resilient "
                "resistant responsible retail rich right rigid ripe rival robust romantic rough "
                "round routine royal rural sacred sad safe same satisfied scarce scientific "
                "secondary secret secure select senior sensible sensitive separate serious seven "
                "severe shallow sharp sheer short shy sick significant silent silly similar simple "
                "single skilled slight slim slow small smart smooth social soft solar sole solid "
                "sophisticated sound sour southern spare spatial special specific spectacular "
                "spiritual splendid stable standard static statistical steady steep stiff still "
                "straight strange strategic strict strong structural stupid subsequent substantial "
                "subtle successful successive sudden sufficient suitable summary super superb "
                "superior sure surplus surprising suspicious sweet swift symbolic tactical tall "
                "technical temporary tender tense terminal terrible tertiary thick thin third "
                "thorough three tight tiny tired tolerant top total tough toxic traditional tragic "
                "transparent tremendous tropical true typical ugly ultimate unable uncertain under "
                "unique united universal unknown unusual upper upset urban urgent useful usual "
                "vague valid valuable variable various vast verbal vertical viable vibrant vicious "
                "visible visual vital vivid vocal voluntary vulnerable warm weak wealthy weekly "
                "weird welcome western wet white whole wide wild willing wise wonderful wooden "
                "worried worse worst worthy wrong yellow young "
                // Adverbs and connectives
                "abroad absolutely accordingly accurately actually additionally admittedly "
                "afterwards again ago ahead almost alone along already also alternatively always "
                "annually apparently approximately around aside automatically away backward badly "
                "barely basically before behind below beneath beside besides better between beyond "
                "briefly broadly carefully certainly cheaply clearly closely commonly completely "
                "consequently considerably consistently constantly correctly currently daily "
                "deeply definitely deliberately directly downward dramatically earlier easily "
                "effectively efficiently elsewhere entirely equally especially essentially "
                "eventually ever exactly exceedingly excessively exclusively explicitly extremely "
                "fairly far fast finally firmly first forever fortunately forward frequently fully "
                "furthermore generally gently gradually greatly hardly heavily hence highly "
                "honestly hopefully immediately increasingly indeed independently inevitably "
                "initially instead inward largely last lately later least less likely likewise "
                "literally little locally long mainly merely mildly mostly much namely naturally "
                "nearly necessarily neither never nevertheless newly next no nobody none "
                "nonetheless nor normally not notably nothing now nowhere obviously occasionally "
                "often once only openly otherwise out outside over overall particularly partly "
                "perhaps permanently personally plainly please poorly possibly potentially "
                "practically precisely preferably presumably previously primarily probably "
                "properly quickly quite rarely rather readily really recently regularly "
                "relatively remarkably repeatedly respectively rightly roughly routinely sadly "
                "scarcely second seldom seriously severely sharply shortly significantly "
                "similarly simply since slightly slowly so sometime sometimes soon specifically "
                "steadily still straight strongly subsequently substantially successfully "
                "suddenly sufficiently surely then thereafter therefore thoroughly though thus "
                "today together tomorrow tonight too totally traditionally truly twice typically "
                "ultimately under unfortunately uniformly uniquely unlikely until unusually up "
                "upon usually utterly very virtually voluntarily well whatever when whenever "
                "where whereas wherever whether while wholly widely willingly within without "
                "worse yes yesterday yet "
                // Domain, software, engineering and testing vocabulary
                "api array assertion attribute authentication authorization bitmap breakpoint "
                "buffer cache callback certificate channel checkpoint checksum cli codec cluster "
                "commit compiler compression concatenation connection console constant container "
                "contract controller cookie corruption counter cpu credential cursor daemon "
                "dataset debug decimal decoder deploy deployment descriptor desktop destination "
                "diagnostic dictionary diff digest disable dispatch download driver dump "
                "duplicate edition element email encode encryption endpoint enum escape "
                "evidence executable experiment export factory fallback fetch firmware fragment "
                "gateway generation generator graph grid hardware header heap hierarchy host html "
                "http icon identifier image import inference install instruction integer "
                "interrupt iteration json kernel latency launch layer lease ledger lifecycle "
                "load lock log login loop machine macro manifest markup memory menu message "
                "metadata migration mode module monitor mutex namespace native network "
                "notification null offset operation operator option output package packet "
                "panel parameter parse parser partition password patch path pattern payload peer "
                "permission persistence pipeline pixel platform plugin pointer policy port "
                "portal preference prefix preview process processor profile prompt property "
                "provider proxy publish queue quota race record recovery redirect renderer "
                "reply repository request requirement resource response result retry revision "
                "rollback route runtime schema script search secret sector server service "
                "session setting sha shadow shell signature slice snapshot socket source "
                "spec sql sqlite stack staging statement status storage stream string "
                "submission subset suffix suite summary switch symbol syntax table tag target "
                "task template tenant test testing thread threshold timestamp token tool trace traffic "
                "transaction transfer transform transport tree trigger tuple type unit update "
                "upload utility validation value variable variant vector version view virtual "
                "volume warning web widget window worker workflow workspace wrapper write "
                "mayasaba english prose markdown spec hello drag drop details detail rule "
                "rules task tasks action actions directory results found unresolved "
                "restriction recheck remove reopen eligible sha guid uuid composite "
                "attribute handle containment canonical managed manage management isolate "
                "isolated isolation verify verified modify modified modification limits "
                "limit requests authorize authorized authorization within second seconds "
                "race races criteria matrix invariant invariants "
                // Everyday words carried over from the original narrower list, plus the
                // inflections and domain nouns that ordinary technical prose uses. These
                // are listed explicitly rather than left to the suffix rules so a change
                // to the stemmer cannot silently narrow what the user is allowed to read.
                "acceptance added agreed allowed appeared ask asked attachment attachments began "
                "believe believed building bytes called car carried caused changed chat committed "
                "considered continued covered created decided developed die died drafts education "
                "explained eye false files followed foot friend got gotten ground happened heart "
                "here hit hoped included info inspection learned listened lived looked morning "
                "moved night offered opened others passed pending people percent played pointed "
                "problem produced provided pulled raised reached received remained remembered "
                "reported required returned road seemed served site speak spoke spoken staged "
                "stayed stopped street suggested supported there told town tried verification "
                "verifies verified verifying waited walked wanted war watched water worked "
                "orchestrator controller agent agents lease leases barrier epoch council "
                "requirement requirements decision decisions review reviews peer peers "
                "reply replies response responses";

            static std::unordered_set<std::string_view> const englishLexicon = []()
            {
                std::unordered_set<std::string_view> set;
                std::size_t index = 0;
                while (index < englishVocabulary.size())
                {
                    while (index < englishVocabulary.size() && englishVocabulary[index] == ' ') ++index;
                    std::size_t const start = index;
                    while (index < englishVocabulary.size() && englishVocabulary[index] != ' ') ++index;
                    if (index > start) set.insert(englishVocabulary.substr(start, index - start));
                }
                return set;
            }();

            // Productive English bound prefixes. They carry no lexical meaning alone, so a
            // hyphenated compound may use one as a part ("re-verified", "non-English",
            // "self-hosted") without the prefix itself being a dictionary word. This is
            // only a licence for the prefix part; the remaining parts must still be
            // recognized words, so an unrecognized stem still fails the whole token.
            static constexpr std::string_view boundPrefixes =
                "re non pre post sub super anti auto co counter de dis ex in inter intra "
                "meta micro mini multi over semi under un up out cross well self half "
                "full low high long short mid near off on one two three all any ever never "
                "by side end front back top bottom left right hard soft fast slow";
            auto isBoundPrefix = [](std::string_view part) -> bool
            {
                std::size_t index = 0;
                while (index < boundPrefixes.size())
                {
                    while (index < boundPrefixes.size() && boundPrefixes[index] == ' ') ++index;
                    std::size_t const start = index;
                    while (index < boundPrefixes.size() && boundPrefixes[index] != ' ') ++index;
                    if (index > start && part == boundPrefixes.substr(start, index - start)) return true;
                }
                return false;
            };

            auto isRecognizedEnglishWord = [&](std::string_view w) -> bool {

                if (englishLexicon.find(w) != englishLexicon.end()) return true;

                // Standard morphological stems:
                // -s (plural / 3rd person)
                if (w.size() > 3 && w.ends_with("s"))
                {
                    if (englishLexicon.find(w.substr(0, w.size() - 1)) != englishLexicon.end()) return true;
                }
                // -es
                if (w.size() > 4 && w.ends_with("es"))
                {
                    if (englishLexicon.find(w.substr(0, w.size() - 2)) != englishLexicon.end()) return true;
                }
                // -ied -> -y (e.g. verified -> verify, modified -> modify)
                if (w.size() > 4 && w.ends_with("ied"))
                {
                    std::string withY = std::string(w.substr(0, w.size() - 3)) + "y";
                    if (englishLexicon.find(withY) != englishLexicon.end()) return true;
                }
                // -ies -> -y (plural / 3rd person of a consonant+y stem: copies -> copy,
                // carries -> carry, verifies -> verify). Only -ied was handled before, so
                // ordinary plurals were refused.
                if (w.size() > 4 && w.ends_with("ies"))
                {
                    std::string withY = std::string(w.substr(0, w.size() - 3)) + "y";
                    if (englishLexicon.find(withY) != englishLexicon.end()) return true;
                }
                // Doubled final consonant before -ed / -ing (stopped -> stop, running -> run).
                if (w.size() > 4 && (w.ends_with("ed") || w.ends_with("ing")))
                {
                    std::size_t const cut = w.ends_with("ed") ? 2 : 3;
                    std::string_view const stem = w.substr(0, w.size() - cut);
                    if (stem.size() > 2 && stem[stem.size() - 1] == stem[stem.size() - 2])
                    {
                        if (englishLexicon.find(stem.substr(0, stem.size() - 1)) != englishLexicon.end()) return true;
                    }
                }
                // -ed
                if (w.size() > 4 && w.ends_with("ed"))
                {
                    if (englishLexicon.find(w.substr(0, w.size() - 2)) != englishLexicon.end()) return true;
                    if (englishLexicon.find(w.substr(0, w.size() - 1)) != englishLexicon.end()) return true;
                }
                // -ing
                if (w.size() > 5 && w.ends_with("ing"))
                {
                    if (englishLexicon.find(w.substr(0, w.size() - 3)) != englishLexicon.end()) return true;
                    std::string withE = std::string(w.substr(0, w.size() - 3)) + "e";
                    if (englishLexicon.find(withE) != englishLexicon.end()) return true;
                }
                // -ly
                if (w.size() > 4 && w.ends_with("ly"))
                {
                    if (englishLexicon.find(w.substr(0, w.size() - 2)) != englishLexicon.end()) return true;
                }
                return false;
            };

            size_t verifiedEnglishCount = 0;

            for (auto const& token : tokens)
            {
                // Strip surrounding punctuation (e.g. quotes, brackets, semicolons, commas, periods)
                size_t start = 0;
                while (start < token.size() && (ispunct(static_cast<unsigned char>(token[start])) || isspace(static_cast<unsigned char>(token[start]))))
                {
                    start++;
                }
                size_t end = token.size();
                while (end > start && (ispunct(static_cast<unsigned char>(token[end - 1])) || isspace(static_cast<unsigned char>(token[end - 1]))))
                {
                    end--;
                }
                if (start >= end)
                {
                    // Pure punctuation token (e.g. "---", "###", "::"), permitted syntax
                    continue;
                }

                std::string core = token.substr(start, end - start);

                // Check if purely numeric literal (e.g. "123", "3.14")
                bool allDigitsOrDecimal = true;
                bool hasDigits = false;
                for (char ch : core)
                {
                    if (isdigit(static_cast<unsigned char>(ch)))
                    {
                        hasDigits = true;
                    }
                    else if (ch == '.')
                    {
                        // decimal separator
                    }
                    else
                    {
                        allDigitsOrDecimal = false;
                        break;
                    }
                }
                if (allDigitsOrDecimal && hasDigits)
                {
                    // Pure numeric literal, permitted
                    continue;
                }

                // If token contains letters mixed with digits, underscores, or punctuation
                // (e.g. "hola1", "hola_mundo", "test_1"), it cannot bypass language verification
                // based solely on token shape.
                std::string word;
                word.reserve(core.size());
                for (char ch : core)
                {
                    word.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                }

                // A hyphenated compound ("re-verified", "fail-closed", "read-only") is
                // ordinary English prose, and the whole token is not itself a dictionary
                // entry. Each part must pass the same check on its own, so a compound of
                // recognized words is accepted while a compound carrying an unrecognized
                // part still fails closed. Without this, hyphenated prose was rejected.
                // A bound prefix ("re-", "non-", "pre-", "self-", ...) is not a word, so
                // those are permitted as parts; every other part must still be recognized,
                // which is why "hola-mundo" remains refused.
                if (word.find('-') != std::string::npos)
                {
                    std::size_t partStart = 0;
                    while (partStart <= word.size())
                    {
                        std::size_t const dash = word.find('-', partStart);
                        std::size_t const partEnd = (dash == std::string::npos) ? word.size() : dash;
                        std::string_view const part(word.data() + partStart, partEnd - partStart);
                        if (!part.empty() && !isRecognizedEnglishWord(part) && !isBoundPrefix(part))
                        {
                            return false;
                        }
                        if (dash == std::string::npos) break;
                        partStart = dash + 1;
                    }
                    verifiedEnglishCount++;
                    continue;
                }

                // Strip common English apostrophe suffixes (e.g. "user's", "it's", "don't").
                // Contractions are checked before the base word so "can't" reduces to "can"
                // rather than to the non-word "ca", and "won't"/"shan't" are handled by the
                // irregular map below because their stems are not the literal prefix.
                if (word.size() > 2 && word.ends_with("'s")) word.resize(word.size() - 2);
                else if (word == "can't") word = "can";
                else if (word == "won't") word = "will";
                else if (word == "shan't") word = "shall";
                else if (word.size() > 2 && word.ends_with("n't")) word.resize(word.size() - 3);
                else if (word.size() > 3 && word.ends_with("'ve")) word.resize(word.size() - 3);
                else if (word.size() > 3 && word.ends_with("'ll")) word.resize(word.size() - 3);
                else if (word.size() > 3 && word.ends_with("'re")) word.resize(word.size() - 3);
                else if (word.size() > 3 && word.ends_with("'d")) word.resize(word.size() - 2);

                // Word must consist entirely of alphabetic ASCII letters ('a' through 'z')
                bool allLetters = true;
                for (char ch : word)
                {
                    if (ch < 'a' || ch > 'z')
                    {
                        allLetters = false;
                        break;
                    }
                }
                if (!allLetters) return false;

                // EVERY prose word must be recognized by the English lexicon or standard morphological stem.
                // If any prose word cannot be positively verified, fail closed (withhold content preview).
                if (!isRecognizedEnglishWord(word))
                {
                    return false;
                }

                verifiedEnglishCount++;
            }

            // Must contain at least one recognized English word
            return verifiedEnglishCount > 0;
        }
#endif
    }

    void Storage::Check(int rc, sqlite3* db)
    {
        if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW)
        {
            throw std::runtime_error(StatementFailureMessage(db, rc));
        }
    }
    void Storage::Exec(char const* sql)
    {
        if(std::string_view(sql).starts_with("BEGIN IMMEDIATE"))
        {
            AdmitWrite_locked();
        }
        char* error = nullptr;
        auto const rc = sqlite3_exec(db_, sql, nullptr, nullptr, &error);
        if (rc != SQLITE_OK)
        {
            std::string message = StatementFailureMessage(db_, rc, error);
            if (error) sqlite3_free(error);
            throw std::runtime_error(message);
        }
    }
    // Single admission point for every mutation.
    //
    // Quarantine used to be checked only where a statement happened to begin with
    // "BEGIN IMMEDIATE", so a direct prepared write - SaveDraftAttachment is one - reached
    // SQLite on a connection whose transaction outcome was unknown. Admission is now a
    // separate step that every mutation calls, and it fails closed before any statement is
    // prepared. The authorizer installed at open is the backstop for any future entry point
    // that forgets to call this: it denies writes on a quarantined connection at the SQLite
    // layer, where no call path can bypass it.
    void Storage::AdmitWrite_locked()
    {
        if (poisoned_)
        {
            throw std::runtime_error(
                "The store is quarantined after an unconfirmed transaction rollback, so no change can be "
                "recorded. Reopen the project store and reconcile the unresolved submission before retrying.");
        }
        CheckWriteCapacity();
    }

    int Storage::WriteAdmissionAuthorizer(void* userData, int action, char const*, char const*,
                                          char const*, char const*)
    {
        auto* self = static_cast<Storage*>(userData);
        if (!self || !self->poisoned_) return SQLITE_OK;

        switch (action)
        {
        case SQLITE_INSERT:
        case SQLITE_UPDATE:
        case SQLITE_DELETE:
        case SQLITE_CREATE_TABLE:
        case SQLITE_CREATE_TEMP_TABLE:
        case SQLITE_CREATE_INDEX:
        case SQLITE_CREATE_TRIGGER:
        case SQLITE_CREATE_VIEW:
        case SQLITE_DROP_TABLE:
        case SQLITE_DROP_TEMP_TABLE:
        case SQLITE_DROP_INDEX:
        case SQLITE_DROP_TRIGGER:
        case SQLITE_DROP_VIEW:
        case SQLITE_ALTER_TABLE:
        case SQLITE_REINDEX:
        case SQLITE_ANALYZE:
        case SQLITE_ATTACH:
        case SQLITE_DETACH:
            // Reads stay available: the Action Center and inspection must still be able to
            // describe the unresolved state that the quarantine exists to protect.
            return SQLITE_DENY;
        default:
            return SQLITE_OK;
        }
    }

    // Establishes the physical transaction state after a failed mutation instead of
    // assuming the rollback worked. A connection still inside a transaction cannot be
    // trusted, so it is quarantined rather than presented as usable.
    void Storage::RollbackAfterFailure_locked() noexcept
    {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        if (sqlite3_get_autocommit(db_) == 0)
        {
            poisoned_ = true;
        }
    }

    // An attachment identity is owned by the project that first recorded it. Without this
    // check an upsert naming another project would silently rewrite the owner's row: the
    // ON CONFLICT clause matched on attachment_id alone, and the pending-manifest guards
    // used the SUPPLIED project, so they never looked at the row actually being updated.
    void Storage::RefuseForeignAttachmentIdentity_locked(std::string const& projectId, std::string const& attachmentId)
    {
        {
            Statement q(db_, "SELECT project_id FROM draft_attachments WHERE attachment_id=?1;");
            Bind(q.value, 1, attachmentId);
            if (sqlite3_step(q.value) == SQLITE_ROW)
            {
                std::string const owner = Text(q.value, 0);
                if (owner != projectId)
                {
                    throw std::runtime_error(
                        "That attachment identity belongs to a different project, so it cannot be written "
                        "through this project. Nothing was changed.");
                }
                return;
            }
        }
        {
            // A committed attachment owns its identity too; reusing it as a draft elsewhere
            // would make inspection ambiguous about which record the identity names.
            Statement q(db_, "SELECT project_id FROM attachments WHERE attachment_id=?1;");
            Bind(q.value, 1, attachmentId);
            if (sqlite3_step(q.value) == SQLITE_ROW && Text(q.value, 0) != projectId)
            {
                throw std::runtime_error(
                    "That attachment identity belongs to a committed attachment of a different project, so "
                    "it cannot be written through this project. Nothing was changed.");
            }
        }
    }

    void Storage::CheckWriteCapacity()
    {
        auto space=std::filesystem::space(file_.parent_path());
        if(space.available < 5ull*1024*1024*1024)
            throw std::runtime_error("Local storage free-space reserve is below 5 GiB; protected data was retained");
        uintmax_t size=0;
        for(auto const& suffix:{L"",L"-wal",L"-shm"})
        {auto path=file_.wstring()+suffix;std::error_code ec;auto bytes=std::filesystem::file_size(path,ec);if(!ec)size+=bytes;}
        if(size>=10ull*1024*1024*1024)
            throw std::runtime_error("Local project store reached its 10 GiB ceiling; protected data was retained");
    }
    void Storage::Bind(sqlite3_stmt* statement, int index, std::string const& value)
    {
        Check(sqlite3_bind_text(statement, index, value.c_str(),
            static_cast<int>(value.size()), SQLITE_TRANSIENT), sqlite3_db_handle(statement));
    }
    std::string Storage::Text(sqlite3_stmt* statement, int column)
    {
        auto const* ptr = sqlite3_column_text(statement, column);
        auto length = sqlite3_column_bytes(statement, column);
        if (!ptr || length <= 0) return {};
        return {reinterpret_cast<char const*>(ptr), static_cast<size_t>(length)};
    }

    std::filesystem::path Storage::DefaultFile()
    {
#ifdef _WIN32
        PWSTR raw = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw)))
            throw std::runtime_error("Could not locate local app data");
        auto production = std::filesystem::path(raw) / L"Mayasaba" / L"mayasaba.db";
        CoTaskMemFree(raw);
        SetLastError(ERROR_SUCCESS);
        wchar_t buffer[MAX_PATH]{};
        DWORD count = GetEnvironmentVariableW(L"MAYASABA_STORE_PATH", buffer, MAX_PATH);
        if (!count && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return production;
        if (!count || count >= MAX_PATH)
            throw std::runtime_error("Invalid test-store override; production fallback is forbidden");
        std::filesystem::path candidate(buffer);
        if (!candidate.is_absolute()) throw std::runtime_error("Test-store path must be absolute");
        auto canonical = std::filesystem::weakly_canonical(candidate).wstring();
        auto protectedPath = std::filesystem::weakly_canonical(production).wstring();
        std::error_code ec;
        bool sameFile = std::filesystem::equivalent(candidate, production, ec);
        if (_wcsicmp(canonical.c_str(), protectedPath.c_str()) == 0 || sameFile)
            throw std::runtime_error("Test-store override cannot target the production database");
        return candidate;
#else
        return std::filesystem::temp_directory_path() / "mayasaba" / "mayasaba.db";
#endif
    }
    Storage::Storage(std::filesystem::path const& file) : file_(file)
    {
        try
        {
            std::filesystem::create_directories(file.parent_path());
            CheckWriteCapacity();
#ifdef _WIN32
            // Lock the actual file identity, independent of environment, case,
            // hard links or path aliases. Hold it against rename for this lifetime.
            identityHandle_ = CreateFileW(file.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (identityHandle_ == INVALID_HANDLE_VALUE)
                throw std::runtime_error("Cannot hold database identity");
            FILE_ID_INFO identity{};
            if (!GetFileInformationByHandleEx(identityHandle_, FileIdInfo, &identity, sizeof(identity)))
                throw std::runtime_error("Cannot verify database file identity");
            std::wostringstream name;
            name << L"Local\\Mayasaba.AppStore.FileId." << std::hex << identity.VolumeSerialNumber << L".";
            for (auto byte : identity.FileId.Identifier)
                name << std::setw(2) << std::setfill(L'0') << static_cast<unsigned>(byte);
            instanceMutex_ = CreateMutexW(nullptr, FALSE, name.str().c_str());
            if (!instanceMutex_ || GetLastError() == ERROR_ALREADY_EXISTS)
                throw std::runtime_error("Another Mayasaba instance is already using its store");
            // Cooperate with older application versions for the production store.
            PWSTR raw = nullptr;
            if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw)))
                throw std::runtime_error("Cannot identify protected application store");
            auto production = std::filesystem::path(raw) / L"Mayasaba" / L"mayasaba.db";
            CoTaskMemFree(raw);
            std::error_code ec;
            if (std::filesystem::equivalent(file, production, ec))
            {
                legacyMutex_ = CreateMutexW(nullptr, FALSE, L"Local\\Mayasaba.AppStore.SingleWriter.v1");
                if (!legacyMutex_ || GetLastError() == ERROR_ALREADY_EXISTS)
                    throw std::runtime_error("Another Mayasaba instance is already using its store");
            }
            int status = sqlite3_open16(file.c_str(), &db_);
#else
            int status = sqlite3_open(file.string().c_str(), &db_);
#endif
            if (status != SQLITE_OK) throw std::runtime_error(db_ ? sqlite3_errmsg(db_) : "SQLite open failed");
            // Backstop for quarantine: once the connection is quarantined this denies every
            // write at the SQLite layer, so a mutation entry point that forgets to call
            // AdmitWrite_locked still fails closed instead of recording a change.
            sqlite3_set_authorizer(db_, &Storage::WriteAdmissionAuthorizer, this);
            sqlite3_busy_timeout(db_, 5000);
            int schemaVersion{};
            {
                Statement version(db_, "PRAGMA user_version;");
                Check(sqlite3_step(version.value), db_);
                schemaVersion = sqlite3_column_int(version.value, 0);
                if (schemaVersion > 4) throw std::runtime_error("Store schema is newer than this application; no migration was attempted");
            }
            Exec("PRAGMA foreign_keys=ON;");
            Exec("PRAGMA journal_mode=WAL;");
            Exec("PRAGMA synchronous=FULL;");
            // Refuse unverified fallback; a future B3 implementation must also record observed settings.
            {
                Statement mode(db_, "PRAGMA journal_mode;");
                if (sqlite3_step(mode.value) != SQLITE_ROW || Text(mode.value, 0) != "wal")
                    throw std::runtime_error("WAL not available: store writes refused");
            }
            {
                Statement sync(db_, "PRAGMA synchronous;");
                if (sqlite3_step(sync.value) != SQLITE_ROW || sqlite3_column_int(sync.value, 0) != 2)
                    throw std::runtime_error("FULL synchronous mode not observed");
            }
            // Migration: ensure last_opened_at and filesystem_identity exist on projects, and prebinding_draft exists on ui_state
            bool hasLastOpened = false;
            bool hasFilesystemIdentity = false;
            bool hasDomain = false;
            bool hasProjectsTable = false;
            {
                Statement info(db_, "PRAGMA table_info(projects);");
                while (sqlite3_step(info.value) == SQLITE_ROW)
                {
                    std::string colName = Text(info.value, 1);
                    hasProjectsTable = true;
                    if (colName == "last_opened_at") hasLastOpened = true;
                    if (colName == "filesystem_identity") hasFilesystemIdentity = true;
                    if (colName == "domain_id") hasDomain = true;
                }
            }
            bool hasPrebind = false;
            {
                Statement info(db_, "PRAGMA table_info(ui_state);");
                while (sqlite3_step(info.value) == SQLITE_ROW)
                {
                    if (Text(info.value, 1) == "prebinding_draft") { hasPrebind = true; break; }
                }
            }

            // If migration is needed, perform consistent verified pre-migration online backup first
            if (hasProjectsTable && schemaVersion < 3)
            {
                auto backupPath = file.wstring() + L".pre_migration.bak";
                if (std::filesystem::exists(backupPath)) backupPath += L".v2." + Utf8ToWide(NewUuid());
                sqlite3* pDest = nullptr;
#ifdef _WIN32
                int openRc = sqlite3_open16(backupPath.c_str(), &pDest);
#else
                int openRc = sqlite3_open(std::string(backupPath.begin(), backupPath.end()).c_str(), &pDest);
#endif
                if (openRc != SQLITE_OK || !pDest)
                {
                    if (pDest) sqlite3_close(pDest);
                    throw std::runtime_error("Pre-migration backup admission failed: could not open backup destination");
                }
                sqlite3_backup* pBackup = sqlite3_backup_init(pDest, "main", db_, "main");
                if (!pBackup)
                {
                    sqlite3_close(pDest);
                    throw std::runtime_error("Pre-migration backup admission failed: sqlite3_backup_init returned null");
                }
                int stepRc = sqlite3_backup_step(pBackup, -1);
                int finishRc = sqlite3_backup_finish(pBackup);
                if (stepRc != SQLITE_DONE || finishRc != SQLITE_OK)
                {
                    sqlite3_close(pDest);
                    throw std::runtime_error("Pre-migration backup admission failed: backup step/finish did not complete successfully");
                }
                // Verify backup database integrity before admitting migration
                {
                    Statement check(pDest, "PRAGMA integrity_check;");
                    int checkRc = sqlite3_step(check.value);
                    if (checkRc != SQLITE_ROW || Text(check.value, 0) != "ok")
                    {
                        sqlite3_close(pDest);
                        throw std::runtime_error("Pre-migration backup admission failed: backup integrity check returned error");
                    }
                }
                sqlite3_close(pDest);
            }
            Exec("BEGIN IMMEDIATE;");
            Exec("CREATE TABLE IF NOT EXISTS projects(id TEXT PRIMARY KEY, display_name TEXT NOT NULL, canonical_path TEXT NOT NULL,"
                 "draft TEXT NOT NULL DEFAULT '', closed INTEGER NOT NULL DEFAULT 0, version INTEGER NOT NULL DEFAULT 0);");
            Exec("CREATE TABLE IF NOT EXISTS contributions(row_id INTEGER PRIMARY KEY AUTOINCREMENT, project_id TEXT NOT NULL REFERENCES projects(id),"
                 "submission_id TEXT NOT NULL UNIQUE, body TEXT NOT NULL, created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));");
            Exec("CREATE INDEX IF NOT EXISTS contribution_project ON contributions(project_id,row_id);");
            Exec("CREATE TABLE IF NOT EXISTS pending_submissions(project_id TEXT PRIMARY KEY REFERENCES projects(id),submission_id TEXT UNIQUE NOT NULL,body TEXT NOT NULL,attachment_manifest TEXT NOT NULL DEFAULT '');");
            Exec("CREATE TABLE IF NOT EXISTS ui_state(singleton INTEGER PRIMARY KEY CHECK(singleton=1), last_project TEXT NOT NULL);");
            if (!hasLastOpened)
            {
                Exec("ALTER TABLE projects ADD COLUMN last_opened_at TEXT;");
            }
            if (!hasFilesystemIdentity)
            {
                Exec("ALTER TABLE projects ADD COLUMN filesystem_identity TEXT;");
                Exec("UPDATE projects SET filesystem_identity = id WHERE filesystem_identity IS NULL OR filesystem_identity = '';");
            }
            if (!hasPrebind)
            {
                Exec("ALTER TABLE ui_state ADD COLUMN prebinding_draft TEXT NOT NULL DEFAULT '';");
            }
            {
                bool hasPendingAttachmentManifest = false;
                Statement info(db_, "PRAGMA table_info(pending_submissions);");
                while (sqlite3_step(info.value) == SQLITE_ROW)
                {
                    if (Text(info.value, 1) == "attachment_manifest") { hasPendingAttachmentManifest = true; break; }
                }
                if (!hasPendingAttachmentManifest)
                {
                    Exec("ALTER TABLE pending_submissions ADD COLUMN attachment_manifest TEXT NOT NULL DEFAULT '';");
                }
            }
            if (!hasDomain)
            {
                Exec("ALTER TABLE projects ADD COLUMN domain_id TEXT;");
                Statement rows(db_, "SELECT id FROM projects;");
                std::vector<std::string> keys;
                int rc{};
                while ((rc = sqlite3_step(rows.value)) == SQLITE_ROW) keys.push_back(Text(rows.value, 0));
                Check(rc, db_);
                for (auto const& key : keys)
                {
                    Statement mapping(db_, "UPDATE projects SET domain_id=?2 WHERE id=?1;");
                    Bind(mapping.value, 1, key); Bind(mapping.value, 2, IsUuid(key) ? key : NewUuid());
                    Check(sqlite3_step(mapping.value), db_);
                }
            }
            Exec("CREATE UNIQUE INDEX IF NOT EXISTS project_domain ON projects(domain_id);");
            Exec("CREATE UNIQUE INDEX IF NOT EXISTS project_filesystem ON projects(filesystem_identity);");
            Exec("CREATE TABLE IF NOT EXISTS selection_receipts(operation_id TEXT PRIMARY KEY, request_bytes TEXT NOT NULL, project_id TEXT NOT NULL REFERENCES projects(id), transferred INTEGER NOT NULL, committed_at TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%fZ','now')));");
            Exec("CREATE TABLE IF NOT EXISTS selection_events(operation_id TEXT PRIMARY KEY REFERENCES selection_receipts(operation_id), owner TEXT NOT NULL CHECK(owner='PROJECT_SERVICE'), event_type TEXT NOT NULL CHECK(event_type='ProjectSelected'), project_id TEXT NOT NULL REFERENCES projects(id));");

            Exec("CREATE TABLE IF NOT EXISTS directory_operations("
                 "operation_id TEXT PRIMARY KEY, parent_path TEXT NOT NULL, folder_name TEXT NOT NULL,"
                 "target_path TEXT NOT NULL, state TEXT NOT NULL, observed_identity TEXT,"
                 "error_message TEXT, created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
                 "updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));");

            Exec("CREATE TABLE IF NOT EXISTS draft_attachments("
                 "attachment_id TEXT PRIMARY KEY, project_id TEXT NOT NULL REFERENCES projects(id),"
                 "file_name TEXT NOT NULL, file_size INTEGER NOT NULL, sha256 TEXT NOT NULL,"
                 "detected_type TEXT NOT NULL, staged_path TEXT NOT NULL, source_path TEXT NOT NULL,"
                 "status TEXT NOT NULL, error_message TEXT NOT NULL DEFAULT '',"
                 "version INTEGER NOT NULL DEFAULT 1,"
                 "created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));");
            Exec("CREATE INDEX IF NOT EXISTS draft_attachment_project ON draft_attachments(project_id);");

            Exec("CREATE TABLE IF NOT EXISTS attachments("
                 "attachment_id TEXT PRIMARY KEY, project_id TEXT NOT NULL REFERENCES projects(id),"
                 "submission_id TEXT NOT NULL REFERENCES contributions(submission_id),"
                 "file_name TEXT NOT NULL, file_size INTEGER NOT NULL, sha256 TEXT NOT NULL,"
                 "detected_type TEXT NOT NULL, storage_path TEXT NOT NULL, source_path TEXT NOT NULL,"
                 "status TEXT NOT NULL, error_message TEXT NOT NULL DEFAULT '',"
                 "version INTEGER NOT NULL DEFAULT 1,"
                 "created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));");
            Exec("CREATE INDEX IF NOT EXISTS attachment_submission ON attachments(submission_id);");
            Exec("CREATE INDEX IF NOT EXISTS attachment_project ON attachments(project_id);");

            {
                bool hasDraftVersion = false;
                Statement info(db_, "PRAGMA table_info(draft_attachments);");
                while (sqlite3_step(info.value) == SQLITE_ROW)
                {
                    if (Text(info.value, 1) == "version") { hasDraftVersion = true; break; }
                }
                if (!hasDraftVersion)
                {
                    Exec("ALTER TABLE draft_attachments ADD COLUMN version INTEGER NOT NULL DEFAULT 1;");
                }
            }
            {
                bool hasAttVersion = false;
                Statement info(db_, "PRAGMA table_info(attachments);");
                while (sqlite3_step(info.value) == SQLITE_ROW)
                {
                    if (Text(info.value, 1) == "version") { hasAttVersion = true; break; }
                }
                if (!hasAttVersion)
                {
                    Exec("ALTER TABLE attachments ADD COLUMN version INTEGER NOT NULL DEFAULT 1;");
                }
            }
            if(schemaVersion<3)
            {
                auto protectColumn=[&](char const* select,char const* update)
                {
                    std::vector<std::pair<std::string,std::string>> rows;
                    {Statement q(db_,select);int rc{};while((rc=sqlite3_step(q.value))==SQLITE_ROW)
                        rows.emplace_back(Text(q.value,0),Text(q.value,1));Check(rc,db_);}
                    for(auto const& row:rows){Statement q(db_,update);Bind(q.value,1,row.first);
                        Bind(q.value,2,ProtectDraft(ReadDraft(row.second)));Check(sqlite3_step(q.value),db_);}
                };
                protectColumn("SELECT id,draft FROM projects;","UPDATE projects SET draft=?2 WHERE id=?1;");
                protectColumn("SELECT singleton,prebinding_draft FROM ui_state;","UPDATE ui_state SET prebinding_draft=?2 WHERE singleton=?1;");
                protectColumn("SELECT project_id,body FROM pending_submissions;","UPDATE pending_submissions SET body=?2 WHERE project_id=?1;");
                protectColumn("SELECT operation_id,request_bytes FROM selection_receipts;","UPDATE selection_receipts SET request_bytes=?2 WHERE operation_id=?1;");
            }

            // ------------------------------------------------------------------
            // Gate I1: durable delivery.
            //
            // The outbox, inbox and receipt tables are created here, inside the
            // same migration transaction as everything else, so a store either
            // has all of them or none. They are created rather than migrated from
            // an earlier shape because nothing wrote them before this version: an
            // earlier build had no outbox at all.
            //
            // `request_bytes` on command_receipts stores canonical bytes, for the
            // same reason selection_receipts does: idempotency compares bytes, so
            // a re-spelling of the same command must compare equal and a different
            // command must not.
            // ------------------------------------------------------------------
            Exec("CREATE TABLE IF NOT EXISTS outbox_envelopes("
                 "envelope_id TEXT PRIMARY KEY,"
                 "original_envelope_id TEXT,"
                 "message_class TEXT NOT NULL,"
                 "sender TEXT NOT NULL,"
                 "recipients TEXT NOT NULL,"
                 "project_id TEXT NOT NULL,"
                 "approved_epoch TEXT NOT NULL,"
                 "operation_id TEXT NOT NULL,"
                 "effect_id TEXT NOT NULL,"
                 "ordering_scope TEXT NOT NULL,"
                 "sequence INTEGER NOT NULL,"
                 "correlation_id TEXT NOT NULL,"
                 "payload_type TEXT NOT NULL,"
                 "payload_digest TEXT NOT NULL,"
                 "payload TEXT NOT NULL DEFAULT '',"
                 "managed_artifact_id TEXT NOT NULL DEFAULT '',"
                 "created_at_utc TEXT NOT NULL,"
                 "ack_required INTEGER NOT NULL DEFAULT 1,"
                 // Delivery bookkeeping belongs to MCF-v2, but a row cannot be
                 // said to be durably queued without recording that it has not
                 // yet been sent. 0 means queued; only a receiver acknowledgement
                 // moves it.
                 "dispatched INTEGER NOT NULL DEFAULT 0,"
                 "UNIQUE(ordering_scope, sequence));");
            Exec("CREATE INDEX IF NOT EXISTS outbox_project ON outbox_envelopes(project_id, sequence);");

            Exec("CREATE TABLE IF NOT EXISTS inbox_envelopes("
                 "envelope_id TEXT PRIMARY KEY,"
                 "message_class TEXT NOT NULL,"
                 "sender TEXT NOT NULL,"
                 "project_id TEXT NOT NULL,"
                 "approved_epoch TEXT NOT NULL,"
                 "ordering_scope TEXT NOT NULL,"
                 "sequence INTEGER NOT NULL,"
                 "payload_digest TEXT NOT NULL,"
                 "payload TEXT NOT NULL DEFAULT '',"
                 "received_at_utc TEXT NOT NULL,"
                 // Admitted, then applied. An envelope is admitted exactly once;
                 // the two flags are what let a crash between them be reconciled
                 // instead of re-delivered.
                 "admitted INTEGER NOT NULL DEFAULT 1,"
                 "applied INTEGER NOT NULL DEFAULT 0,"
                 "UNIQUE(ordering_scope, sequence));");

            Exec("CREATE TABLE IF NOT EXISTS command_receipts("
                 "receipt_id TEXT PRIMARY KEY,"
                 "command_id TEXT NOT NULL,"
                 "operation_id TEXT NOT NULL,"
                 // The idempotency identity is the operation id plus the exact
                 // canonical bytes, so the same operation with different input is
                 // detectable rather than silently deduplicated.
                 "request_bytes TEXT NOT NULL,"
                 "request_digest TEXT NOT NULL,"
                 "result_kind TEXT NOT NULL,"
                 "owner TEXT NOT NULL,"
                 "result_record_id TEXT NOT NULL DEFAULT '',"
                 "result_record_version TEXT NOT NULL DEFAULT '',"
                 "event_ids TEXT NOT NULL DEFAULT '',"
                 "error_code TEXT NOT NULL DEFAULT '',"
                 "error_message TEXT NOT NULL DEFAULT '',"
                 "retry_kind TEXT NOT NULL DEFAULT 'NEVER',"
                 "source_owner TEXT NOT NULL DEFAULT '',"
                 "original_receipt_id TEXT NOT NULL DEFAULT '',"
                 "original_receipt_digest TEXT NOT NULL DEFAULT '',"
                 "physical_outcome TEXT NOT NULL,"
                 "recorded_at_utc TEXT NOT NULL,"
                 // No UNIQUE(operation_id) here, deliberately. This table is an
                 // append-only attempt log, not a one-row-per-operation table:
                 // a duplicate or a conflicting re-presentation is itself a
                 // recorded fact and must not overwrite the original. The
                 // *earliest* row for an operation is the authoritative one,
                 // because CommitCommand only writes an outcome-establishing row
                 // when no prior row exists; every later row is a DUPLICATE or a
                 // conflict refusal and says so in its result_kind.
                 "UNIQUE(receipt_id));");
            Exec("CREATE INDEX IF NOT EXISTS receipt_operation ON command_receipts(operation_id);");

            Exec("PRAGMA user_version=4;");
            Exec("COMMIT;");
            ReconcileCrashOperations();
        }
        catch (...)
        {
            if (db_) sqlite3_close(db_);
            db_ = nullptr;
#ifdef _WIN32
            if (instanceMutex_) { CloseHandle(instanceMutex_); instanceMutex_ = nullptr; }
            if (legacyMutex_) CloseHandle(legacyMutex_);
            if (identityHandle_ != INVALID_HANDLE_VALUE) CloseHandle(identityHandle_);
#endif
            throw;
        }
    }
    Storage::~Storage()
    {
        if (db_) sqlite3_close(db_);
#ifdef _WIN32
        if (instanceMutex_) CloseHandle(instanceMutex_);
        if (legacyMutex_) CloseHandle(legacyMutex_);
        if (identityHandle_ != INVALID_HANDLE_VALUE) CloseHandle(identityHandle_);
#endif
    }
    std::string Storage::BindProject(std::string const& id, std::string const& name, std::string const& path, std::string const& filesystemIdentity)
    {
        std::lock_guard lock(mutex_);
        Exec("BEGIN IMMEDIATE;");
        try
        {
            std::string actualId = id;
            std::string fsId = filesystemIdentity.empty() ? id : filesystemIdentity;

            // If a project already exists with this filesystem_identity, preserve its domain id!
            {
                Statement find(db_, "SELECT id FROM projects WHERE filesystem_identity = ?1;");
                Bind(find.value, 1, fsId);
                if (sqlite3_step(find.value) == SQLITE_ROW)
                {
                    actualId = Text(find.value, 0);
                }
            }

            Statement p(db_, "INSERT INTO projects(id, display_name, canonical_path, last_opened_at, filesystem_identity, domain_id) "
                             "VALUES(?1, ?2, ?3, strftime('%Y-%m-%dT%H:%M:%fZ','now'), ?4, ?5) "
                             "ON CONFLICT(id) DO UPDATE SET display_name=excluded.display_name,"
                             "canonical_path=excluded.canonical_path,"
                             "last_opened_at=strftime('%Y-%m-%dT%H:%M:%fZ','now'),"
                             "filesystem_identity=excluded.filesystem_identity;");
            Bind(p.value, 1, actualId);
            Bind(p.value, 2, name);
            Bind(p.value, 3, path);
            Bind(p.value, 4, fsId);
            Bind(p.value, 5, IsUuid(actualId) ? actualId : NewUuid());
            Check(sqlite3_step(p.value), db_);

            Statement u(db_, "INSERT INTO ui_state(singleton, last_project) VALUES(1, ?1)"
                             "ON CONFLICT(singleton) DO UPDATE SET last_project=excluded.last_project;");
            Bind(u.value, 1, actualId);
            Check(sqlite3_step(u.value), db_);

            Exec("COMMIT;");
            return actualId;
        }
        catch (...)
        {
            RollbackAfterFailure_locked();
            throw;
        }
    }
    std::optional<ProjectRecord> Storage::Project(std::string const& id)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT id, display_name, canonical_path, draft, closed, version, coalesce(last_opened_at,''), coalesce(filesystem_identity,''), domain_id FROM projects WHERE id=?1;");
        Bind(q.value, 1, id);
        int rc = sqlite3_step(q.value);
        if (rc == SQLITE_DONE) return std::nullopt;
        Check(rc, db_);
        return ProjectRecord{Text(q.value, 0), Text(q.value, 1), Text(q.value, 2), ReadDraft(Text(q.value, 3)),
            sqlite3_column_int(q.value, 4) != 0, sqlite3_column_int(q.value, 5), Text(q.value, 6), Text(q.value, 7), Text(q.value, 8)};
    }
    std::optional<ProjectRecord> Storage::ProjectByFilesystemIdentity(std::string const& fsId)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT id, display_name, canonical_path, draft, closed, version, coalesce(last_opened_at,''), coalesce(filesystem_identity,''), domain_id FROM projects WHERE filesystem_identity=?1 OR id=?1;");
        Bind(q.value, 1, fsId);
        int rc = sqlite3_step(q.value);
        if (rc == SQLITE_DONE) return std::nullopt;
        Check(rc, db_);
        return ProjectRecord{Text(q.value, 0), Text(q.value, 1), Text(q.value, 2), ReadDraft(Text(q.value, 3)),
            sqlite3_column_int(q.value, 4) != 0, sqlite3_column_int(q.value, 5), Text(q.value, 6), Text(q.value, 7), Text(q.value, 8)};
    }
    bool Storage::HasAnyProjects()
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT count(*) FROM projects;");
        int rc = sqlite3_step(q.value);
        Check(rc, db_);
        return sqlite3_column_int(q.value, 0) > 0;
    }
    std::vector<ProjectRecord> Storage::RecentProjects(size_t limit)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT id, display_name, canonical_path, draft, closed, version, coalesce(last_opened_at, ''), coalesce(filesystem_identity, ''), domain_id "
                         "FROM projects "
                         "ORDER BY (CASE WHEN last_opened_at IS NOT NULL AND last_opened_at != '' THEN 0 ELSE 1 END), "
                         "last_opened_at DESC, id ASC LIMIT ?1;");
        sqlite3_bind_int(q.value, 1, static_cast<int>(limit));
        std::vector<ProjectRecord> result;
        while (sqlite3_step(q.value) == SQLITE_ROW)
        {
            result.push_back({
                Text(q.value, 0),
                Text(q.value, 1),
                Text(q.value, 2),
                ReadDraft(Text(q.value, 3)),
                sqlite3_column_int(q.value, 4) != 0,
                sqlite3_column_int(q.value, 5),
                Text(q.value, 6),
                Text(q.value, 7), Text(q.value, 8)
            });
        }
        return result;
    }
    namespace
    {
        std::string EscapeLike(std::string const& s)
        {
            std::string escaped;
            escaped.reserve(s.size() * 2);
            for (char c : s)
            {
                if (c == '\\' || c == '%' || c == '_')
                {
                    escaped.push_back('\\');
                }
                escaped.push_back(c);
            }
            return escaped;
        }
    }

    CrossProjectRecentProjectsResult Storage::QueryRecentProjects(CrossProjectRecentProjectsQuery const& query)
    {
        std::lock_guard lock(mutex_);
        CrossProjectRecentProjectsResult result;
        try
        {
            if (query.pageSize < 1 || query.pageSize > 100 || query.offset > 1000000 || query.filterText.size() > 500)
                throw std::runtime_error("Recent-project query exceeds its bounds");

            std::string const pattern = "%" + EscapeLike(query.filterText) + "%";
            int const status = static_cast<int>(query.statusFilter);

            // 1. Total matching count across all pages
            Statement cnt(db_, "SELECT count(*) FROM projects "
                               "WHERE (?1 = '' OR (display_name LIKE ?2 ESCAPE '\\' OR canonical_path LIKE ?2 ESCAPE '\\')) "
                               "  AND (?3 = 0 OR (?3 = 1 AND closed = 0) OR (?3 = 2 AND closed != 0));");
            Bind(cnt.value, 1, query.filterText);
            Bind(cnt.value, 2, pattern);
            sqlite3_bind_int(cnt.value, 3, status);
            int rc = sqlite3_step(cnt.value);
            Check(rc, db_);
            result.totalMatching = static_cast<size_t>(sqlite3_column_int64(cnt.value, 0));

            // 2. Data query with deterministic ordering and bounded pagination
            Statement q(db_, "SELECT id, display_name, canonical_path, draft, closed, version, "
                             "coalesce(last_opened_at, ''), coalesce(filesystem_identity, ''), domain_id "
                             "FROM projects "
                             "WHERE (?1 = '' OR (display_name LIKE ?2 ESCAPE '\\' OR canonical_path LIKE ?2 ESCAPE '\\')) "
                             "  AND (?3 = 0 OR (?3 = 1 AND closed = 0) OR (?3 = 2 AND closed != 0)) "
                             "ORDER BY (CASE WHEN last_opened_at IS NOT NULL AND last_opened_at != '' THEN 0 ELSE 1 END), "
                             "last_opened_at DESC, id ASC LIMIT ?4 OFFSET ?5;");
            Bind(q.value, 1, query.filterText);
            Bind(q.value, 2, pattern);
            sqlite3_bind_int(q.value, 3, status);
            sqlite3_bind_int(q.value, 4, static_cast<int>(query.pageSize + 1));
            sqlite3_bind_int(q.value, 5, static_cast<int>(query.offset));

            while ((rc = sqlite3_step(q.value)) == SQLITE_ROW)
            {
                result.items.push_back({
                    Text(q.value, 0),
                    Text(q.value, 1),
                    Text(q.value, 2),
                    ReadDraft(Text(q.value, 3)),
                    sqlite3_column_int(q.value, 4) != 0,
                    sqlite3_column_int(q.value, 5),
                    Text(q.value, 6),
                    Text(q.value, 7), Text(q.value, 8)
                });
            }
            Check(rc, db_);
            result.hasMore = result.items.size() > query.pageSize;
            if (result.hasMore) result.items.pop_back();
            result.isComplete = true;
        }
        catch (std::exception const& e)
        {
            result.isComplete = false;
            result.errorMessage = e.what();
        }
        return result;
    }
    std::optional<ProjectRecord> Storage::ProjectByDomainId(std::string const& id)
    {
        std::string key;
        {
            std::lock_guard lock(mutex_);
            Statement q(db_, "SELECT id FROM projects WHERE domain_id=?1;");
            Bind(q.value, 1, id);
            int rc = sqlite3_step(q.value); Check(rc, db_);
            if (rc == SQLITE_DONE) return std::nullopt;
            key = Text(q.value, 0);
        }
        return Project(key);
    }

    SelectionReceipt Storage::CommitSelection(SelectionCommit const& c)
    {
        std::lock_guard lock(mutex_);
        if (!IsUuid(c.operationId) || !IsUuid(c.proposedId) || c.requestBytes.size() > 262144)
            throw std::runtime_error("Invalid selection identity or payload bound");
        auto read = [&](std::string const& key) -> ProjectRecord
        {
            Statement q(db_, "SELECT id,display_name,canonical_path,draft,closed,version,coalesce(last_opened_at,''),filesystem_identity,domain_id FROM projects WHERE id=?1;");
            Bind(q.value, 1, key);
            if (sqlite3_step(q.value) != SQLITE_ROW) throw std::runtime_error("Project record unavailable");
            return {Text(q.value,0),Text(q.value,1),Text(q.value,2),ReadDraft(Text(q.value,3)),sqlite3_column_int(q.value,4)!=0,
                sqlite3_column_int(q.value,5),Text(q.value,6),Text(q.value,7),Text(q.value,8)};
        };
        Exec("BEGIN IMMEDIATE;");
        try
        {
            {
                Statement receipt(db_, "SELECT request_bytes,project_id,transferred FROM selection_receipts WHERE operation_id=?1;");
                Bind(receipt.value,1,c.operationId);
                int rc = sqlite3_step(receipt.value); Check(rc,db_);
                if (rc == SQLITE_ROW)
                {
                    if (ReadDraft(Text(receipt.value,0)) != c.requestBytes) throw std::runtime_error("Selection identity reused with changed input");
                    SelectionReceipt result; result.project = read(Text(receipt.value,1));
                    if(result.project.filesystemIdentity!=c.filesystemIdentity)
                        throw std::runtime_error("Selection receipt refers to a different folder identity");
                    result.duplicate = true; result.transferred = sqlite3_column_int(receipt.value,2)!=0;
                    Exec("COMMIT;"); return result;
                }
            }
            std::string key;
            {
                Statement find(db_, "SELECT id FROM projects WHERE filesystem_identity=?1;");
                Bind(find.value,1,c.filesystemIdentity);
                int rc = sqlite3_step(find.value); Check(rc,db_);
                if (rc == SQLITE_ROW) key=Text(find.value,0);
            }
            if (!c.expectedProject.empty() && key != c.expectedProject)
                throw std::runtime_error("Folder identity differs; original project history is preserved. Use Locate folder");
            ProjectRecord project;
            if (!key.empty())
            {
                project=read(key);
                if (c.expectedVersion >= 0 && project.version != c.expectedVersion)
                    throw std::runtime_error("Project changed during selection; select it again");
            }
            else { key=c.proposedId; project={key,c.name,c.path,"",false,0,"",c.filesystemIdentity,c.proposedId}; }
            std::string prebind;
            {
                Statement q(db_, "SELECT prebinding_draft FROM ui_state WHERE singleton=1;");
                int rc=sqlite3_step(q.value); Check(rc,db_);
                if(rc==SQLITE_ROW) prebind=ReadDraft(Text(q.value,0));
            }
            if (savedDraftRevision_ != requestedDraftRevision_.load()) throw std::runtime_error("Selector draft save is not confirmed");
            bool pending=false;
            {
                Statement q(db_, "SELECT 1 FROM pending_submissions WHERE project_id=?1;"); Bind(q.value,1,key);
                int rc=sqlite3_step(q.value); Check(rc,db_); pending=rc==SQLITE_ROW;
            }
            SelectionReceipt result; result.project=project; result.pending=pending; result.selectorDraft=prebind;
            if (!project.closed && !prebind.empty() && (pending || !project.draft.empty()) && c.draftChoice==DraftChoice::Automatic)
            { result.collision=true; Exec("ROLLBACK;"); return result; }
            if (c.draftChoice==DraftChoice::UseSelector &&
                (project.closed || pending || prebind!=c.expectedSelectorDraft || project.draft!=c.expectedTargetDraft))
                throw std::runtime_error("Drafts changed or target is locked; both drafts were preserved");
            bool transfer=!project.closed && !prebind.empty() && c.draftChoice!=DraftChoice::KeepProject && !pending;
            {
                Statement q(db_, "INSERT INTO projects(id,display_name,canonical_path,draft,closed,version,last_opened_at,filesystem_identity,domain_id) VALUES(?1,?2,?3,?4,0,0,strftime('%Y-%m-%dT%H:%M:%fZ','now'),?5,?6) ON CONFLICT(id) DO UPDATE SET display_name=excluded.display_name,canonical_path=excluded.canonical_path,draft=excluded.draft,version=projects.version+1,last_opened_at=excluded.last_opened_at;");
                Bind(q.value,1,key);Bind(q.value,2,c.name);Bind(q.value,3,c.path);
                Bind(q.value,4,ProtectDraft(transfer?prebind:project.draft));Bind(q.value,5,c.filesystemIdentity);Bind(q.value,6,project.domainId);
                Check(sqlite3_step(q.value),db_);
            }
            {
                Statement q(db_, "INSERT INTO ui_state(singleton,last_project,prebinding_draft) VALUES(1,?1,?2) ON CONFLICT(singleton) DO UPDATE SET last_project=excluded.last_project,prebinding_draft=excluded.prebinding_draft;");
                result.selectorDraft=transfer?(c.draftChoice==DraftChoice::UseSelector?project.draft:""):prebind;
                Bind(q.value,1,key);Bind(q.value,2,ProtectDraft(result.selectorDraft));Check(sqlite3_step(q.value),db_);
            }
            {
                Statement q(db_, "INSERT INTO selection_receipts(operation_id,request_bytes,project_id,transferred) VALUES(?1,?2,?3,?4);");
                Bind(q.value,1,c.operationId);Bind(q.value,2,ProtectDraft(c.requestBytes));Bind(q.value,3,key);sqlite3_bind_int(q.value,4,transfer?1:0);Check(sqlite3_step(q.value),db_);
                Statement e(db_, "INSERT INTO selection_events(operation_id,owner,event_type,project_id) VALUES(?1,'PROJECT_SERVICE','ProjectSelected',?2);");
                Bind(e.value,1,c.operationId);Bind(e.value,2,key);Check(sqlite3_step(e.value),db_);
            }
            if (!c.creationOperation.empty())
            {
                Statement q(db_, "UPDATE directory_operations SET state='BOUND',error_message='',updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE operation_id=?1 AND observed_identity=?2 AND state IN ('CREATED','BOUND');");
                Bind(q.value,1,c.creationOperation);Bind(q.value,2,c.filesystemIdentity);Check(sqlite3_step(q.value),db_);
                if(sqlite3_changes(db_)!=1) throw std::runtime_error("Creation observation no longer matches binding");
            }
            result.project=read(key); result.transferred=transfer;
            Exec("COMMIT;");
            return result;
        }
        catch (...) { sqlite3_exec(db_,"ROLLBACK;",nullptr,nullptr,nullptr); throw; }
    }

    // ----------------------------------------------------------------------
    // Gate I1: durable delivery
    // ----------------------------------------------------------------------

    namespace
    {
        // Joins with a separator SQLite can split on. Recipients and event ids
        // are both `^[A-Za-z0-9._:-]+$`-safe in practice but this is not relied
        // on: a value containing the separator is refused at insert time by
        // delivery::Validate for recipients, and event ids are Uuids.
        std::string JoinValues(std::vector<std::string> const& values)
        {
            std::string joined;
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (i) joined.push_back('\n');
                joined.append(values[i]);
            }
            return joined;
        }

        std::vector<std::string> SplitValues(std::string const& joined)
        {
            std::vector<std::string> values;
            if (joined.empty()) return values;
            std::size_t start = 0;
            for (;;)
            {
                auto const at = joined.find('\n', start);
                if (at == std::string::npos) { values.push_back(joined.substr(start)); break; }
                values.push_back(joined.substr(start, at - start));
                start = at + 1;
            }
            return values;
        }
    }

    delivery::CommandReceipt Storage::CommitCommand(CommandCommit const& command)
    {
        using namespace mayasaba::delivery;

        std::lock_guard lock(mutex_);

        // Admission before any statement is prepared, per AGENTS.md §11. A
        // quarantined or over-capacity store refuses here rather than midway
        // through the transaction.
        AdmitWrite_locked();

        auto nowReceipt = [&](ResultKind kind) -> CommandReceipt
        {
            CommandReceipt receipt;
            receipt.receiptId = NewUuid();
            receipt.commandId = command.commandId;
            receipt.operationId = command.operationId;
            receipt.resultKind = kind;
            receipt.owner = command.owner;
            receipt.recordedAtUtc = command.recordedAtUtc;
            receipt.physicalOutcome = command.physicalOutcome;
            return receipt;
        };

        auto insertReceipt = [&](CommandReceipt const& receipt, std::string const& requestDigest)
        {
            Statement q(db_, "INSERT INTO command_receipts(receipt_id,command_id,operation_id,request_bytes,request_digest,"
                             "result_kind,owner,result_record_id,result_record_version,event_ids,error_code,error_message,"
                             "retry_kind,source_owner,original_receipt_id,original_receipt_digest,physical_outcome,recorded_at_utc) "
                             "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18);");
            Bind(q.value, 1, receipt.receiptId);
            Bind(q.value, 2, receipt.commandId);
            Bind(q.value, 3, receipt.operationId);
            Bind(q.value, 4, ProtectDraft(command.canonicalRequestBytes));
            Bind(q.value, 5, requestDigest);
            Bind(q.value, 6, ResultKindText(receipt.resultKind));
            Bind(q.value, 7, receipt.owner);
            Bind(q.value, 8, receipt.resultRecordId);
            Bind(q.value, 9, receipt.resultRecordVersion);
            Bind(q.value, 10, JoinValues(receipt.eventIds));
            Bind(q.value, 11, receipt.errorCode);
            Bind(q.value, 12, receipt.errorMessage);
            Bind(q.value, 13, RetryKindText(receipt.retryKind));
            Bind(q.value, 14, receipt.sourceOwner);
            Bind(q.value, 15, receipt.originalReceiptId);
            Bind(q.value, 16, receipt.originalReceiptDigest);
            Bind(q.value, 17, receipt.physicalOutcome);
            Bind(q.value, 18, receipt.recordedAtUtc);
            Check(sqlite3_step(q.value), db_);
        };

        // The digest is computed before the transaction opens so a hash failure
        // cannot leave a half-written command. An unavailable digest fails closed:
        // without it the idempotency comparison has no basis.
        auto const requestDigest = Sha256Hex(command.canonicalRequestBytes);
        if (!requestDigest.has_value())
            throw std::runtime_error("Command identity digest unavailable; the command was not recorded");

        Exec("BEGIN IMMEDIATE;");
        try
        {
            // C09 step 4. The comparison is on canonical bytes, never on a
            // parsed value, so a re-spelling is a duplicate and a different
            // command under the same id is a conflict.
            {
                Statement prior(db_, "SELECT receipt_id,request_bytes,result_kind,owner,result_record_id,"
                                     "result_record_version,event_ids,error_code,error_message,retry_kind,"
                                     "source_owner,original_receipt_id,original_receipt_digest,physical_outcome,"
                                     "recorded_at_utc FROM command_receipts WHERE operation_id=?1 "
                                     "ORDER BY rowid ASC LIMIT 1;");
                Bind(prior.value, 1, command.operationId);
                int const rc = sqlite3_step(prior.value);
                Check(rc, db_);
                if (rc == SQLITE_ROW)
                {
                    std::string const priorBytes = ReadDraft(Text(prior.value, 1));
                    auto const decision = DecideIdempotency(priorBytes, command.canonicalRequestBytes);

                    if (decision == IdempotencyDecision::Conflict)
                    {
                        // Different bytes under one identity. Refused, and the
                        // original receipt is left exactly as it was.
                        auto receipt = nowReceipt(ResultKind::Refused);
                        receipt.errorCode = "IDEMPOTENCY_CONFLICT";
                        receipt.errorMessage = "This operation identity was already used with different input; "
                                               "the original receipt is unchanged.";
                        receipt.retryKind = RetryKind::AfterUserAction;
                        receipt.sourceOwner = command.owner;
                        // A refusal is a known outcome: nothing was applied.
                        receipt.physicalOutcome = "NOT_STARTED";
                        insertReceipt(receipt, *requestDigest);
                        Exec("COMMIT;");
                        return receipt;
                    }

                    // A duplicate returns a linkage receipt naming the original,
                    // per C09 step 4. The original is NOT rewritten: a reader
                    // that followed the link must find what it read the first
                    // time.
                    auto receipt = nowReceipt(ResultKind::Duplicate);
                    receipt.originalReceiptId = Text(prior.value, 0);
                    receipt.originalReceiptDigest = priorBytes.empty()
                        ? std::string{}
                        : Sha256Hex(priorBytes).value_or(std::string{});
                    receipt.retryKind = RetryKind::Never;
                    // The duplicate's physical outcome is the original's, because
                    // that is the effect the caller is asking about.
                    receipt.physicalOutcome = Text(prior.value, 13);
                    insertReceipt(receipt, *requestDigest);
                    Exec("COMMIT;");
                    return receipt;
                }
            }

            // The owner's own write. A throw here rolls back everything,
            // including any envelope that had already been written.
            std::string resultRecordId;
            std::string resultRecordVersion;
            try
            {
                if (command.applyMutation)
                    command.applyMutation(resultRecordId, resultRecordVersion);
            }
            catch (std::exception const& e)
            {
                auto receipt = nowReceipt(ResultKind::Refused);
                receipt.errorCode = "PRECONDITION_FAILED";
                receipt.errorMessage = e.what();
                receipt.retryKind = RetryKind::AfterUserAction;
                receipt.sourceOwner = command.owner;
                receipt.physicalOutcome = "NOT_STARTED";
                insertReceipt(receipt, *requestDigest);
                Exec("COMMIT;");
                return receipt;
            }

            // C09 step 7: causal envelopes in the same transaction. Validated
            // here rather than at the caller so a malformed envelope cannot be
            // recorded beside a committed state change.
            for (auto const& envelope : command.envelopes)
            {
                auto const error = Validate(envelope);
                if (error != EnvelopeError::None)
                    throw std::runtime_error(std::string("Outbox envelope refused: ") + EnvelopeErrorText(error));

                Statement q(db_, "INSERT INTO outbox_envelopes(envelope_id,original_envelope_id,message_class,sender,"
                                 "recipients,project_id,approved_epoch,operation_id,effect_id,ordering_scope,sequence,"
                                 "correlation_id,payload_type,payload_digest,payload,managed_artifact_id,created_at_utc,"
                                 "ack_required) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18);");
                Bind(q.value, 1, envelope.envelopeId);
                Bind(q.value, 2, envelope.originalEnvelopeId);
                Bind(q.value, 3, envelope.messageClass);
                Bind(q.value, 4, envelope.sender);
                Bind(q.value, 5, JoinValues(envelope.recipients));
                Bind(q.value, 6, envelope.projectId);
                Bind(q.value, 7, envelope.approvedEpoch);
                Bind(q.value, 8, envelope.operationId);
                Bind(q.value, 9, envelope.effectId);
                Bind(q.value, 10, envelope.orderingScope);
                sqlite3_bind_int64(q.value, 11, static_cast<sqlite3_int64>(envelope.sequence));
                Bind(q.value, 12, envelope.correlationId);
                Bind(q.value, 13, envelope.payloadType);
                Bind(q.value, 14, envelope.payloadDigest);
                Bind(q.value, 15, envelope.payload);
                Bind(q.value, 16, envelope.managedArtifactId);
                Bind(q.value, 17, envelope.createdAtUtc);
                sqlite3_bind_int(q.value, 18, envelope.ackRequired ? 1 : 0);
                Check(sqlite3_step(q.value), db_);
            }

            auto receipt = nowReceipt(ResultKind::Committed);
            receipt.resultRecordId = resultRecordId;
            receipt.resultRecordVersion = resultRecordVersion;
            receipt.eventIds = command.eventIds;
            receipt.retryKind = RetryKind::Never;

            // Validate the receipt before persisting it: a COMMITTED receipt
            // without a record identity would be schema-invalid, and recording
            // one would put an unreadable row in the audit trail.
            auto const receiptError = Validate(receipt);
            if (receiptError != ReceiptError::None)
                throw std::runtime_error(std::string("Command receipt refused: ") + ReceiptErrorText(receiptError));

            insertReceipt(receipt, *requestDigest);
            Exec("COMMIT;");
            return receipt;
        }
        catch (...)
        {
            RollbackAfterFailure_locked();
            throw;
        }
    }

    bool Storage::AdmitInboxEnvelope(delivery::OutboxEnvelope const& envelope)
    {
        using namespace mayasaba::delivery;

        std::lock_guard lock(mutex_);
        AdmitWrite_locked();

        auto const error = Validate(envelope);
        if (error != EnvelopeError::None)
            throw std::runtime_error(std::string("Inbox envelope refused: ") + EnvelopeErrorText(error));

        Exec("BEGIN IMMEDIATE;");
        try
        {
            // Admission is durable before any receiver effect, which is what makes
            // at-least-once delivery safe to apply. A second admission of the same
            // id is a no-op: the sender is entitled to retry, and refusing the
            // retry would turn a lost ACK into a lost message.
            {
                Statement existing(db_, "SELECT 1 FROM inbox_envelopes WHERE envelope_id=?1;");
                Bind(existing.value, 1, envelope.envelopeId);
                int const rc = sqlite3_step(existing.value);
                Check(rc, db_);
                if (rc == SQLITE_ROW) { Exec("COMMIT;"); return false; }
            }

            Statement q(db_, "INSERT INTO inbox_envelopes(envelope_id,message_class,sender,project_id,approved_epoch,"
                             "ordering_scope,sequence,payload_digest,payload,received_at_utc,admitted,applied) "
                             "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,1,0);");
            Bind(q.value, 1, envelope.envelopeId);
            Bind(q.value, 2, envelope.messageClass);
            Bind(q.value, 3, envelope.sender);
            Bind(q.value, 4, envelope.projectId);
            Bind(q.value, 5, envelope.approvedEpoch);
            Bind(q.value, 6, envelope.orderingScope);
            sqlite3_bind_int64(q.value, 7, static_cast<sqlite3_int64>(envelope.sequence));
            Bind(q.value, 8, envelope.payloadDigest);
            Bind(q.value, 9, envelope.payload);
            Bind(q.value, 10, envelope.createdAtUtc);
            Check(sqlite3_step(q.value), db_);

            Exec("COMMIT;");
            return true;
        }
        catch (...)
        {
            RollbackAfterFailure_locked();
            throw;
        }
    }

    std::optional<delivery::CommandReceipt> Storage::ReceiptForOperation(std::string const& operationId)
    {
        std::lock_guard lock(mutex_);

        Statement q(db_, "SELECT receipt_id,command_id,operation_id,result_kind,owner,result_record_id,"
                         "result_record_version,event_ids,error_code,error_message,retry_kind,source_owner,"
                         "original_receipt_id,original_receipt_digest,physical_outcome,recorded_at_utc "
                         "FROM command_receipts WHERE operation_id=?1 ORDER BY rowid ASC LIMIT 1;");
        Bind(q.value, 1, operationId);
        int const rc = sqlite3_step(q.value);
        if (rc == SQLITE_DONE) return std::nullopt;
        Check(rc, db_);

        delivery::CommandReceipt receipt;
        receipt.receiptId = Text(q.value, 0);
        receipt.commandId = Text(q.value, 1);
        receipt.operationId = Text(q.value, 2);
        receipt.resultKind = delivery::ParseResultKind(Text(q.value, 3)).value_or(delivery::ResultKind::Unknown);
        if (!delivery::ParseResultKind(Text(q.value, 3)).has_value()) receipt.resultKindUnreadable = true;
        receipt.owner = Text(q.value, 4);
        receipt.resultRecordId = Text(q.value, 5);
        receipt.resultRecordVersion = Text(q.value, 6);
        receipt.eventIds = SplitValues(Text(q.value, 7));
        receipt.errorCode = Text(q.value, 8);
        receipt.errorMessage = Text(q.value, 9);
        // Retry kind is parsed back from its registry spelling rather than
        // defaulted. A row this build cannot read leaves the policy unknown, and
        // the flag says so, because defaulting to NEVER would present an
        // unreadable policy as the strictest one and quietly forbid a retry the
        // recorded receipt might have allowed.
        if (auto const parsed = delivery::ParseRetryKind(Text(q.value, 10)); parsed.has_value())
            receipt.retryKind = *parsed;
        else
            receipt.retryKindUnreadable = true;
        receipt.sourceOwner = Text(q.value, 11);
        receipt.originalReceiptId = Text(q.value, 12);
        receipt.originalReceiptDigest = Text(q.value, 13);
        receipt.physicalOutcome = Text(q.value, 14);
        receipt.recordedAtUtc = Text(q.value, 15);
        return receipt;
    }

    std::vector<delivery::OutboxEnvelope> Storage::QueuedOutbox(std::string const& projectId, std::size_t limit)
    {
        std::lock_guard lock(mutex_);

        std::vector<delivery::OutboxEnvelope> queued;
        Statement q(db_, "SELECT envelope_id,original_envelope_id,message_class,sender,recipients,project_id,"
                         "approved_epoch,operation_id,effect_id,ordering_scope,sequence,correlation_id,payload_type,"
                         "payload_digest,payload,managed_artifact_id,created_at_utc,ack_required "
                         "FROM outbox_envelopes WHERE project_id=?1 AND dispatched=0 ORDER BY sequence LIMIT ?2;");
        Bind(q.value, 1, projectId);
        sqlite3_bind_int64(q.value, 2, static_cast<sqlite3_int64>(limit));
        int rc{};
        while ((rc = sqlite3_step(q.value)) == SQLITE_ROW)
        {
            delivery::OutboxEnvelope envelope;
            envelope.envelopeId = Text(q.value, 0);
            envelope.originalEnvelopeId = Text(q.value, 1);
            envelope.messageClass = Text(q.value, 2);
            envelope.sender = Text(q.value, 3);
            envelope.recipients = SplitValues(Text(q.value, 4));
            envelope.projectId = Text(q.value, 5);
            envelope.approvedEpoch = Text(q.value, 6);
            envelope.operationId = Text(q.value, 7);
            envelope.effectId = Text(q.value, 8);
            envelope.orderingScope = Text(q.value, 9);
            envelope.sequence = static_cast<std::uint64_t>(sqlite3_column_int64(q.value, 10));
            envelope.correlationId = Text(q.value, 11);
            envelope.payloadType = Text(q.value, 12);
            envelope.payloadDigest = Text(q.value, 13);
            envelope.payload = Text(q.value, 14);
            envelope.managedArtifactId = Text(q.value, 15);
            envelope.createdAtUtc = Text(q.value, 16);
            envelope.ackRequired = sqlite3_column_int(q.value, 17) != 0;
            queued.push_back(std::move(envelope));
        }
        Check(rc, db_);
        return queued;
    }

    bool Storage::MarkEnvelopeDispatched(std::string const& envelopeId)
    {
        std::lock_guard lock(mutex_);
        AdmitWrite_locked();

        Statement q(db_, "UPDATE outbox_envelopes SET dispatched=1 WHERE envelope_id=?1 AND dispatched=0;");
        Bind(q.value, 1, envelopeId);
        Check(sqlite3_step(q.value), db_);
        return sqlite3_changes(db_) == 1;
    }

    std::uint64_t Storage::LastDeliveredSequence(std::string const& orderingScope)
    {
        std::lock_guard lock(mutex_);

        Statement q(db_, "SELECT coalesce(max(sequence),0) FROM outbox_envelopes WHERE ordering_scope=?1 AND dispatched=1;");
        Bind(q.value, 1, orderingScope);
        int const rc = sqlite3_step(q.value);
        if (rc != SQLITE_ROW) { Check(rc, db_); return 0; }
        return static_cast<std::uint64_t>(sqlite3_column_int64(q.value, 0));
    }

    void Storage::TouchProjectOpened(std::string const& id)
    {        std::lock_guard lock(mutex_);
        Exec("BEGIN IMMEDIATE;");
        try
        {
            Statement q(db_, "UPDATE projects SET last_opened_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=?1;");
            Bind(q.value, 1, id);
            Check(sqlite3_step(q.value), db_);
            Statement u(db_, "INSERT INTO ui_state(singleton, last_project) VALUES(1, ?1) "
                             "ON CONFLICT(singleton) DO UPDATE SET last_project=excluded.last_project;");
            Bind(u.value, 1, id);
            Check(sqlite3_step(u.value), db_);
            Exec("COMMIT;");
        }
        catch (...)
        {
            RollbackAfterFailure_locked();
            throw;
        }
    }
    void Storage::RelocateProject(std::string const& id, std::string const& newCanonicalPath, std::string const& newDisplayName)
    {
        std::lock_guard lock(mutex_);
        AdmitWrite_locked();
        Statement q(db_, "UPDATE projects SET canonical_path=?2, display_name=?3 WHERE id=?1;");
        Bind(q.value, 1, id);
        Bind(q.value, 2, newCanonicalPath);
        Bind(q.value, 3, newDisplayName);
        Check(sqlite3_step(q.value), db_);
    }
    void Storage::SavePrebindDraft(std::string const& body)
    {
        SavePrebindDraft(body, ReservePrebindRevision());
    }
    bool Storage::SavePrebindDraft(std::string const& body, uint64_t revision)
    {
        std::lock_guard lock(mutex_);
        AdmitWrite_locked();
        if (revision != requestedDraftRevision_.load()) return false;
        Statement q(db_, "INSERT INTO ui_state(singleton, last_project, prebinding_draft) VALUES(1, '', ?1) "
                         "ON CONFLICT(singleton) DO UPDATE SET prebinding_draft=excluded.prebinding_draft;");
        Bind(q.value, 1, ProtectDraft(body));
        Check(sqlite3_step(q.value), db_);
        savedDraftRevision_ = revision;
        return true;
    }
    std::string Storage::GetPrebindDraft()
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT coalesce(prebinding_draft, '') FROM ui_state WHERE singleton=1;");
        int rc = sqlite3_step(q.value);
        if (rc == SQLITE_DONE) return {};
        Check(rc, db_);
        return ReadDraft(Text(q.value, 0));
    }
    void Storage::ClearPrebindDraft()
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "UPDATE ui_state SET prebinding_draft='' WHERE singleton=1;");
        Check(sqlite3_step(q.value), db_);
    }
    bool Storage::TransferPrebindDraftToProject(std::string const& projectId, bool overwrite)
    {
        std::lock_guard lock(mutex_);
        if (savedDraftRevision_ != requestedDraftRevision_.load()) return false;
        std::string prebind;
        {
            Statement q(db_, "SELECT coalesce(prebinding_draft, '') FROM ui_state WHERE singleton=1;");
            if (sqlite3_step(q.value) == SQLITE_ROW) prebind = ReadDraft(Text(q.value, 0));
        }
        if (prebind.empty()) return true;

        Exec("BEGIN IMMEDIATE;");
        try
        {
            Statement p(db_, "SELECT draft, closed, version FROM projects WHERE id=?1;");
            Bind(p.value, 1, projectId);
            if (sqlite3_step(p.value) != SQLITE_ROW)
            {
                Exec("ROLLBACK;");
                return false;
            }
            bool closed = sqlite3_column_int(p.value, 1) != 0;
            if (closed)
            {
                // Never write draft into closed project!
                Exec("ROLLBACK;");
                return false;
            }
            int currentVersion = sqlite3_column_int(p.value, 2);
            std::string targetDraft = ReadDraft(Text(p.value, 0));

            // Check if project has a pending submission
            {
                Statement pend(db_, "SELECT count(*) FROM pending_submissions WHERE project_id=?1;");
                Bind(pend.value, 1, projectId);
                if (sqlite3_step(pend.value) == SQLITE_ROW && sqlite3_column_int(pend.value, 0) > 0)
                {
                    // Locked due to unresolved pending submission
                    Exec("ROLLBACK;");
                    return false;
                }
            }

            if (!targetDraft.empty() && !overwrite)
            {
                // Collision: target already has a draft and overwrite is false
                Exec("ROLLBACK;");
                return false;
            }

            // Atomic version-checked update
            Statement upd(db_, "UPDATE projects SET draft=?2, version=version+1 WHERE id=?1 AND version=?3;");
            Bind(upd.value, 1, projectId);
            Bind(upd.value, 2, ProtectDraft(prebind));
            sqlite3_bind_int(upd.value, 3, currentVersion);
            Check(sqlite3_step(upd.value), db_);
            if (sqlite3_changes(db_) != 1)
            {
                // Version mismatch or concurrent edit
                Exec("ROLLBACK;");
                return false;
            }

            // Only clear prebinding draft AFTER target draft update succeeds
            Statement clr(db_, "UPDATE ui_state SET prebinding_draft='' WHERE singleton=1;");
            Check(sqlite3_step(clr.value), db_);
            Exec("COMMIT;");
            auto expected = savedDraftRevision_;
            if (requestedDraftRevision_.compare_exchange_strong(expected, expected + 1))
                ++savedDraftRevision_;
            return true;
        }
        catch (...)
        {
            RollbackAfterFailure_locked();
            throw;
        }
    }

    void Storage::RecordDirectoryIntent(std::string const& opId, std::string const& parentPath, std::string const& folderName, std::string const& targetPath)
    {
        std::lock_guard lock(mutex_);
        AdmitWrite_locked();
        Statement q(db_, "INSERT INTO directory_operations(operation_id, parent_path, folder_name, target_path, state) "
                         "VALUES(?1, ?2, ?3, ?4, 'INTENDED');");
        Bind(q.value, 1, opId);
        Bind(q.value, 2, parentPath);
        Bind(q.value, 3, folderName);
        Bind(q.value, 4, targetPath);
        Check(sqlite3_step(q.value), db_);
    }

    void Storage::RecordDirectoryCreated(std::string const& opId, std::string const& observedIdentity)
    {
        std::lock_guard lock(mutex_);
        AdmitWrite_locked();
        Statement q(db_, "UPDATE directory_operations SET state='CREATED', observed_identity=?2, "
                         "updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE operation_id=?1;");
        Bind(q.value, 1, opId);
        Bind(q.value, 2, observedIdentity);
        Check(sqlite3_step(q.value), db_);
    }

    void Storage::RecordDirectoryBound(std::string const& opId)
    {
        std::lock_guard lock(mutex_);
        AdmitWrite_locked();
        Statement q(db_, "UPDATE directory_operations SET state='BOUND', "
                         "updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE operation_id=?1;");
        Bind(q.value, 1, opId);
        Check(sqlite3_step(q.value), db_);
    }

    void Storage::RecordDirectoryFailed(std::string const& opId, std::string const& errorMessage)
    {
        std::lock_guard lock(mutex_);
        AdmitWrite_locked();
        Statement q(db_, "UPDATE directory_operations SET state='FAILED', error_message=?2, "
                         "updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE operation_id=?1;");
        Bind(q.value, 1, opId);
        Bind(q.value, 2, errorMessage);
        Check(sqlite3_step(q.value), db_);
    }

    std::optional<DirectoryOperationRecord> Storage::GetDirectoryOperation(std::string const& opId)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT operation_id, parent_path, folder_name, target_path, state, "
                         "coalesce(observed_identity,''), coalesce(error_message,''), created_at, updated_at "
                         "FROM directory_operations WHERE operation_id=?1;");
        Bind(q.value, 1, opId);
        if (sqlite3_step(q.value) != SQLITE_ROW) return std::nullopt;
        return DirectoryOperationRecord{
            Text(q.value, 0), Text(q.value, 1), Text(q.value, 2), ReadDraft(Text(q.value, 3)),
            Text(q.value, 4), Text(q.value, 5), Text(q.value, 6), Text(q.value, 7), Text(q.value, 8)
        };
    }

    std::vector<DirectoryOperationRecord> Storage::UnreconciledDirectoryOperations()
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT operation_id, parent_path, folder_name, target_path, state, "
                         "coalesce(observed_identity,''), coalesce(error_message,''), created_at, updated_at "
                         "FROM directory_operations WHERE state NOT IN ('BOUND', 'FAILED') ORDER BY created_at ASC;");
        std::vector<DirectoryOperationRecord> result;
        while (sqlite3_step(q.value) == SQLITE_ROW)
        {
            result.push_back({
                Text(q.value, 0), Text(q.value, 1), Text(q.value, 2), ReadDraft(Text(q.value, 3)),
                Text(q.value, 4), Text(q.value, 5), Text(q.value, 6), Text(q.value, 7), Text(q.value, 8)
            });
        }
        return result;
    }

    void Storage::RecordDirectoryUncertain(std::string const& opId, std::string const& errorMessage)
    {
        std::lock_guard lock(mutex_);
        AdmitWrite_locked();
        Statement q(db_, "UPDATE directory_operations SET error_message=?2, updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE operation_id=?1 AND state NOT IN ('BOUND','FAILED');");
        Bind(q.value, 1, opId); Bind(q.value, 2, errorMessage);
        Check(sqlite3_step(q.value), db_);
    }
    void Storage::ReconcileCrashOperations()
    {
        // Storage does not originate project binding authority. Keep observations
        // for an explicit Project-service recovery; restart must not adopt roots.
        for (auto const& op : UnreconciledDirectoryOperations())
            RecordDirectoryUncertain(op.operationId, op.state == "INTENDED" ?
                "PHYSICAL_OUTCOME_UNKNOWN" : "CREATED_DIRECTORY_REQUIRES_EXPLICIT_SELECTION");
    }
    std::optional<std::string> Storage::LastBoundProject()
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT last_project FROM ui_state WHERE singleton=1;");
        int rc=sqlite3_step(q.value);
        if (rc==SQLITE_DONE) return std::nullopt;
        Check(rc,db_);
        return Text(q.value,0);
    }
    void Storage::SaveDraft(std::string const& id, std::string const& body)
    {
        if (!TrySaveDraft(id, body)) throw std::runtime_error("Draft project not found");
    }
    bool Storage::TrySaveDraft(std::string const& id, std::string const& body)
    {
        std::lock_guard lock(mutex_);
        AdmitWrite_locked();
        Statement q(db_, "UPDATE projects SET draft=?2 WHERE id=?1;");
        Bind(q.value,1,id); Bind(q.value,2,ProtectDraft(body));
        Check(sqlite3_step(q.value),db_);
        // Zero changed rows means the id matches no project. That is a fact about
        // the binding, not about the attempt, so it is reported rather than thrown:
        // a caller that can unbind and continue needs to distinguish it from a store
        // refusal it should simply retry. Every other failure still throws through
        // Check/AdmitWrite_locked, so a refusal can never look like a save.
        return sqlite3_changes(db_)==1;
    }
    std::optional<PendingSubmission> Storage::Pending(std::string const& id)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT submission_id,body,attachment_manifest FROM pending_submissions WHERE project_id=?1;");
        Bind(q.value,1,id);
        int rc=sqlite3_step(q.value);
        if (rc==SQLITE_DONE) return std::nullopt;
        Check(rc,db_);
        return PendingSubmission{Text(q.value,0),ReadDraft(Text(q.value,1)),Text(q.value,2)};
    }
    std::string Storage::StageSubmission(std::string const& id, std::string const& proposed, std::string const& body)
    {
        if (id.empty() || proposed.empty()) throw std::runtime_error("Invalid submission");
        std::lock_guard lock(mutex_);
        Exec("BEGIN IMMEDIATE;");
        try
        {
            // Query EVERY draft attachment of the project, not only the READY ones.
            // Counting only READY rows let a lower-level caller stage a submission whose
            // manifest silently omitted a rejected, timed-out or source-changed sibling:
            // the ready count matched the supplied count and the omission was invisible.
            // A draft is one payload; if any part of it is unresolved, none of it may be
            // admitted as if it were complete.
            std::vector<AttachmentRecord> drafts;
            std::string unresolvedStatus;
            std::string unresolvedId;
            {
                Statement qDrafts(db_, "SELECT attachment_id, file_size, sha256, status FROM draft_attachments WHERE project_id=?1 ORDER BY attachment_id;");
                Bind(qDrafts.value, 1, id);
                int rc{};
                while ((rc = sqlite3_step(qDrafts.value)) == SQLITE_ROW)
                {
                    std::string const status = Text(qDrafts.value, 3);
                    if (status != "READY" && unresolvedStatus.empty())
                    {
                        unresolvedStatus = status;
                        unresolvedId = Text(qDrafts.value, 0);
                    }
                    AttachmentRecord r;
                    r.id = Text(qDrafts.value, 0);
                    r.fileSize = static_cast<uint64_t>(sqlite3_column_int64(qDrafts.value, 1));
                    r.sha256 = Text(qDrafts.value, 2);
                    drafts.push_back(std::move(r));
                }
                Check(rc, db_);
            }

            if (!unresolvedStatus.empty())
            {
                throw std::runtime_error(
                    "An attachment in this draft is not ready (" + unresolvedStatus + "). The submission was "
                    "refused so the incomplete set is not silently omitted; remove or re-attach the affected "
                    "attachment and try again.");
            }

            if (drafts.size() > 50)
            {
                throw std::runtime_error("Attachment count exceeds maximum 50 files limit");
            }

            if (body.empty() && drafts.empty())
            {
                throw std::runtime_error("Submission cannot be empty: must provide text or attachments");
            }

            {
                Statement project(db_, "SELECT closed FROM projects WHERE id=?1;");
                Bind(project.value,1,id);
                int rc=sqlite3_step(project.value);
                if (rc==SQLITE_DONE) throw std::runtime_error("Missing project");
                Check(rc,db_);
                if (sqlite3_column_int(project.value,0))
                    throw std::runtime_error("Closed project");
            }

            std::string const currentManifest = ComputeAttachmentManifest(drafts);
            std::string winner=proposed;
            {
                Statement q(db_, "SELECT submission_id,body,attachment_manifest FROM pending_submissions WHERE project_id=?1;");
                Bind(q.value,1,id);
                if(sqlite3_step(q.value)==SQLITE_ROW)
                {
                    if(ReadDraft(Text(q.value,1))!=body)
                        throw std::runtime_error("Different source text while prior submission is unresolved");
                    if(Text(q.value,2)!=currentManifest)
                        throw std::runtime_error("Different attachment manifest while prior submission is unresolved");
                    winner=Text(q.value,0);
                }
                else
                {
                    Statement add(db_, "INSERT INTO pending_submissions(project_id,submission_id,body,attachment_manifest) VALUES(?1,?2,?3,?4);");
                    Bind(add.value,1,id);Bind(add.value,2,proposed);Bind(add.value,3,ProtectDraft(body));Bind(add.value,4,currentManifest);
                    Check(sqlite3_step(add.value),db_);
                    Statement draft(db_, "UPDATE projects SET draft=?2 WHERE id=?1;");
                    Bind(draft.value,1,id);Bind(draft.value,2,ProtectDraft(body));
                    Check(sqlite3_step(draft.value),db_);
                }
            }
            Exec("COMMIT;");
            return winner;
        }
        catch(...)
        {
            sqlite3_exec(db_,"ROLLBACK;",nullptr,nullptr,nullptr);
            throw;
        }
    }
    bool Storage::SetClosure(std::string const& id, int version, bool closed)
    {
        std::lock_guard lock(mutex_);
        AdmitWrite_locked();
        Statement q(db_, "UPDATE projects SET closed=?3,version=version+1 WHERE id=?1 AND version=?2;");
        Bind(q.value,1,id);
        Check(sqlite3_bind_int(q.value,2,version),db_);
        Check(sqlite3_bind_int(q.value,3,closed?1:0),db_);
        Check(sqlite3_step(q.value),db_);
        return sqlite3_changes(db_)==1;
    }
    SaveDisposition Storage::SaveContribution(std::string const& id, std::string const& submission, std::string const& body)
    {
        return SaveContribution(id, submission, body, {});
    }

    SaveDisposition Storage::SaveContribution(std::string const& id, std::string const& submission, std::string const& body, std::vector<AttachmentRecord> const& attachments)
    {
        SaveContributionDetail ignored;
        return SaveContribution(id, submission, body, attachments, ignored);
    }

    SaveDisposition Storage::SaveContribution(std::string const& id, std::string const& submission, std::string const& body, std::vector<AttachmentRecord> const& attachments, SaveContributionDetail& detail)
    {
        detail = SaveContributionDetail{};
        auto reject = [&detail](SaveDisposition d, std::string reason) {
            detail.reasonEn = std::move(reason);
            return d;
        };

        if (submission.empty() || (body.empty() && attachments.empty()))
            return reject(SaveDisposition::Conflict, "Submission identity or payload is empty.");
        // P0: Enforce 50-file limit inside Storage boundary
        if (attachments.size() > 50)
            return reject(SaveDisposition::Conflict, "The submission exceeds the 50-attachment limit.");

        // Phase 1: Pre-transaction validation, exclusive write-protection locking, and cryptographic hashing
        // Staged files are locked with FILE_SHARE_READ so that no concurrent process can modify, truncate, or overwrite them.
#ifdef _WIN32
        struct ProtectedFileLock
        {
            HANDLE handle{ INVALID_HANDLE_VALUE };
            std::wstring path{};
            bool sealed{ false };

            ~ProtectedFileLock()
            {
                if (handle != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(handle);
                }
            }
        };

        std::vector<std::unique_ptr<ProtectedFileLock>> openLocks;
        openLocks.reserve(attachments.size());
#endif
        uint64_t aggregateBytes = 0;
        for (auto const& att : attachments)
        {
            if ((!att.projectId.empty() && att.projectId != id) ||
                (!att.submissionId.empty() && att.submissionId != submission))
            {
                return reject(SaveDisposition::Conflict, "An attachment does not belong to this project and submission.");
            }
            if (att.fileSize == 0 || att.fileSize > 100ULL * 1024ULL * 1024ULL) // 100 MiB limit
            {
                return reject(SaveDisposition::Conflict, "An attachment is empty or exceeds the 100 MiB per-file limit.");
            }
            if (att.sha256.size() != 64)
            {
                return reject(SaveDisposition::Conflict, "An attachment is missing a complete SHA-256 integrity value.");
            }
            aggregateBytes += att.fileSize;
            if (aggregateBytes > 500ULL * 1024ULL * 1024ULL) // 500 MiB aggregate limit
            {
                return reject(SaveDisposition::Conflict, "The attachments exceed the 500 MiB aggregate limit.");
            }

#ifdef _WIN32
            // Verified-byte admission lock: open with exclusive write protection (FILE_SHARE_READ only).
            // This prevents any concurrent process or thread from modifying, truncating, deleting, or renaming the file.
            std::wstring stagedW = Utf8ToWide(att.storagePath);
            std::filesystem::path stagedP(stagedW);
            std::error_code ec;
            if (!std::filesystem::exists(stagedP, ec) || std::filesystem::is_directory(stagedP, ec))
            {
                return reject(SaveDisposition::Conflict,
                    "A staged attachment is missing or is not a regular file; nothing was saved.");
            }

            HANDLE h = CreateFileW(stagedW.c_str(),
                GENERIC_READ | FILE_WRITE_ATTRIBUTES,
                FILE_SHARE_READ,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (h == INVALID_HANDLE_VALUE)
            {
                // If it is already read-only (e.g. during an identical replay check), open with FILE_ATTRIBUTE_READONLY
                h = CreateFileW(stagedW.c_str(),
                    GENERIC_READ | FILE_WRITE_ATTRIBUTES,
                    FILE_SHARE_READ,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_READONLY,
                    nullptr);
            }
            if (h == INVALID_HANDLE_VALUE)
            {
                return reject(SaveDisposition::Conflict,
                    "A staged attachment could not be opened for verified admission; nothing was saved.");
            }

            LARGE_INTEGER sizeLi{};
            if (!GetFileSizeEx(h, &sizeLi) || static_cast<uint64_t>(sizeLi.QuadPart) != att.fileSize)
            {
                CloseHandle(h);
                return reject(SaveDisposition::Conflict,
                    "A staged attachment no longer matches its recorded size; nothing was saved.");
            }

            // Read directly through the protected handle to compute SHA-256
            BCRYPT_ALG_HANDLE hAlg = nullptr;
            NTSTATUS bStatus = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
            if (!BCRYPT_SUCCESS(bStatus))
            {
                CloseHandle(h);
                return reject(SaveDisposition::Conflict,
                    "The SHA-256 provider is unavailable, so attachment integrity could not be verified; nothing was saved.");
            }

            DWORD hashObjSize = 0, dataLen = 0;
            bStatus = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&hashObjSize, sizeof(DWORD), &dataLen, 0);
            if (!BCRYPT_SUCCESS(bStatus))
            {
                BCryptCloseAlgorithmProvider(hAlg, 0);
                CloseHandle(h);
                return reject(SaveDisposition::Conflict,
                    "The SHA-256 provider could not report its state, so attachment integrity could not be verified; nothing was saved.");
            }

            std::vector<BYTE> hashObj(hashObjSize);
            BCRYPT_HASH_HANDLE hHash = nullptr;
            bStatus = BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0);
            if (!BCRYPT_SUCCESS(bStatus))
            {
                BCryptCloseAlgorithmProvider(hAlg, 0);
                CloseHandle(h);
                return reject(SaveDisposition::Conflict,
                    "A SHA-256 operation could not be started, so attachment integrity could not be verified; nothing was saved.");
            }

            constexpr DWORD BUFFER_SIZE = 64 * 1024;
            std::vector<BYTE> buffer(BUFFER_SIZE);
            DWORD bytesRead = 0;
            uint64_t actualBytes = 0;
            SetFilePointer(h, 0, nullptr, FILE_BEGIN);
            // A failed read is NOT end-of-file. The previous loop
            // (ReadFile(...) && bytesRead > 0) treated a read error as a clean EOF and
            // ignored the BCryptHashData result, so a truncated or partly unreadable
            // attachment could still produce a digest that was compared as if complete.
            for (;;)
            {
                if (!ReadFile(h, buffer.data(), BUFFER_SIZE, &bytesRead, nullptr))
                {
                    BCryptDestroyHash(hHash);
                    BCryptCloseAlgorithmProvider(hAlg, 0);
                    CloseHandle(h);
                    return reject(SaveDisposition::Conflict,
                        "A staged attachment could not be read in full, so its integrity is unverified; nothing was saved.");
                }
                if (bytesRead == 0) break;

                actualBytes += bytesRead;
                bStatus = BCryptHashData(hHash, buffer.data(), bytesRead, 0);
                if (!BCRYPT_SUCCESS(bStatus))
                {
                    BCryptDestroyHash(hHash);
                    BCryptCloseAlgorithmProvider(hAlg, 0);
                    CloseHandle(h);
                    return reject(SaveDisposition::Conflict,
                        "A SHA-256 operation failed while verifying a staged attachment; nothing was saved.");
                }
            }

            if (actualBytes != att.fileSize)
            {
                BCryptDestroyHash(hHash);
                BCryptCloseAlgorithmProvider(hAlg, 0);
                CloseHandle(h);
                return reject(SaveDisposition::Conflict,
                    "A staged attachment is not the length it was recorded with, so its integrity is unverified; nothing was saved.");
            }

            BYTE hashBytes[32];
            bStatus = BCryptFinishHash(hHash, hashBytes, 32, 0);
            BCryptDestroyHash(hHash);
            BCryptCloseAlgorithmProvider(hAlg, 0);
            if (!BCRYPT_SUCCESS(bStatus))
            {
                CloseHandle(h);
                return reject(SaveDisposition::Conflict,
                    "The SHA-256 digest of a staged attachment could not be finalized, so integrity is unverified; nothing was saved.");
            }

            std::ostringstream ss;
            for (int i = 0; i < 32; ++i)
            {
                ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hashBytes[i]);
            }
            std::string computedHash = ss.str();
            if (computedHash != att.sha256)
            {
                CloseHandle(h);
                return reject(SaveDisposition::Conflict,
                    "A staged attachment no longer matches its recorded SHA-256 digest; nothing was saved.");
            }

            auto lockPtr = std::make_unique<ProtectedFileLock>();
            lockPtr->handle = h;
            lockPtr->path = stagedW;
            openLocks.push_back(std::move(lockPtr));
#endif
        }

        std::string const currentManifest = ComputeAttachmentManifest(attachments);

        // Phase 2: Short, lightweight SQLite write transaction
        std::lock_guard lock(mutex_);
        bool began=false;
        bool duplicate=false;

        // Rolls back and reports the PROVEN physical transaction state. A ROLLBACK that
        // reports failure does not by itself prove a transaction is still open, so the
        // connection's own autocommit state decides. Only a connection that is still inside
        // a transaction is quarantined; that case can no longer be used safely and every
        // later transaction attempt fails closed with an explicit reason.
        // Restores the attributes of files this operation sealed. Called only when the
        // transaction outcome is proven to be "not committed": on an unknown commit the
        // sealed state may be the only remaining evidence that these bytes were bound to a
        // contribution, so it is preserved and the outcome is reconciled instead.
        auto compensateSealedFiles = [&]() -> bool {
            bool allRestored = true;
#ifdef _WIN32
            for (auto& lockPtr : openLocks)
            {
                if (!lockPtr->sealed) continue;
                // The return code is checked: an attribute restore that silently failed
                // would leave a file the reader can no longer remove or re-attach, and the
                // caller must be told that rather than assume a clean rollback.
                if (SetFileAttributesW(lockPtr->path.c_str(), FILE_ATTRIBUTE_NORMAL))
                {
                    lockPtr->sealed = false;
                }
                else
                {
                    allRestored = false;
                }
            }
#endif
            return allRestored;
        };

        auto rollbackAndReject = [&](SaveDisposition disposition, std::string reason) -> SaveDisposition {
            detail.reasonEn = std::move(reason);
            char* message = nullptr;
            int rc = SQLITE_OK;
            if (forceRollbackFailureForTesting_)
            {
                // Inject the observable failure mode: the rollback reports an error while the
                // connection is still inside the transaction. SQLITE_ERROR is 1 and is not
                // declared by every SDK shim, so the value is named locally.
                constexpr int kInjectedRollbackError = 1;
                forceRollbackFailureForTesting_ = false;
                rc = kInjectedRollbackError;
            }
            else
            {
                rc = sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &message);
            }
            if (rc != SQLITE_OK)
            {
                std::string failure = message ? message : sqlite3_errmsg(db_);
                if (message) sqlite3_free(message);

                if (sqlite3_get_autocommit(db_) != 0)
                {
                    // No transaction remains open; the rollback is effective even though
                    // SQLite reported an error (for example "no transaction is active").
                    detail.rollbackConfirmed = true;
                    detail.connectionUsable = true;
                }
                else
                {
                    detail.rollbackConfirmed = false;
                    detail.connectionUsable = false;
                    poisoned_ = true;
                    detail.reasonEn += " The transaction rollback could not be confirmed (" + failure +
                        "); the physical outcome is UNKNOWN. The store is quarantined until it is "
                        "reopened and the submission outcome is reconciled.";
                    return SaveDisposition::Unknown;
                }
            }
            // The rollback is proven, so no file this operation sealed is bound to a
            // contribution any more: restore it. Before this compensation existed, a
            // failure on the second attachment left the first one read-only for good, which
            // also made it unremovable through the ordinary path.
            if (!compensateSealedFiles())
            {
                detail.reasonEn += " One or more already-sealed attachments could not have their "
                    "read-only attribute restored; they may still be read-only on disk.";
            }
            return disposition;
        };

        try
        {
            Exec("BEGIN IMMEDIATE;"); began=true;
            {
                Statement p(db_, "SELECT closed FROM projects WHERE id=?1;");
                Bind(p.value,1,id);
                if (sqlite3_step(p.value)!=SQLITE_ROW || sqlite3_column_int(p.value,0)!=0)
                {
                    return rollbackAndReject(SaveDisposition::RejectedClosed,
                        "The project is closed; an explicit Reopen Project is required before saving.");
                }
            }

            // Check if contribution is already committed (replay / duplicate check)
            bool alreadyCommitted = false;
            std::string storedProject;
            std::string storedBody;
            {
                Statement stored(db_, "SELECT project_id,body FROM contributions WHERE submission_id=?1;");
                Bind(stored.value,1,submission);
                if (sqlite3_step(stored.value)==SQLITE_ROW)
                {
                    alreadyCommitted = true;
                    storedProject = Text(stored.value,0);
                    storedBody = Text(stored.value,1);
                }
            }

            // Check pending submission
            {
                Statement pending(db_, "SELECT submission_id,body,attachment_manifest FROM pending_submissions WHERE project_id=?1;");
                Bind(pending.value,1,id);
                if(sqlite3_step(pending.value)==SQLITE_ROW)
                {
                    if(Text(pending.value,0)!=submission ||
                       ReadDraft(Text(pending.value,1))!=body ||
                       Text(pending.value,2)!=currentManifest)
                    {
                        return rollbackAndReject(SaveDisposition::Conflict,
                            "Another submission is pending for this project, or this submission's text or attachments changed. "
                            "The pending submission was left untouched.");
                    }
                }
                else
                {
                    // Pending retired only by a successful commit; identical replay is allowed.
                    if (!alreadyCommitted)
                    {
                        return rollbackAndReject(SaveDisposition::Conflict,
                            "This submission was never staged for this project and cannot be saved. Recreate the message and try again.");
                    }
                    if (storedProject != id)
                    {
                        return rollbackAndReject(SaveDisposition::Conflict,
                            "This submission identity already belongs to a different project; it was not reused.");
                    }
                    if (storedBody != body)
                    {
                        return rollbackAndReject(SaveDisposition::Conflict,
                            "This submission is already saved with different message text; it was not treated as a replay.");
                    }
                }
            }

            if (alreadyCommitted)
            {
                if (storedProject!=id || storedBody!=body)
                {
                    return rollbackAndReject(SaveDisposition::Conflict,
                        "This submission identity already belongs to a different project or message text; it was not overwritten.");
                }

                // Bind submission identity to the complete attachment set
                Statement commAtt(db_, "SELECT attachment_id, file_size, sha256 FROM attachments WHERE submission_id=?1 ORDER BY attachment_id;");
                Bind(commAtt.value, 1, submission);
                std::vector<AttachmentRecord> existingAtts;
                int arc{};
                while ((arc = sqlite3_step(commAtt.value)) == SQLITE_ROW)
                {
                    AttachmentRecord ar;
                    ar.id = Text(commAtt.value, 0);
                    ar.fileSize = static_cast<uint64_t>(sqlite3_column_int64(commAtt.value, 1));
                    ar.sha256 = Text(commAtt.value, 2);
                    existingAtts.push_back(std::move(ar));
                }
                Check(arc, db_);
                std::string existingManifest = ComputeAttachmentManifest(existingAtts);
                if (existingManifest != currentManifest)
                {
                    // Reject changed payloads instead of reporting them as duplicates
                    return rollbackAndReject(SaveDisposition::Conflict,
                        "This submission is already saved with a different attachment set; it was not treated as a replay.");
                }
                duplicate=true;
            }
            else
            {
                // Validate exact staging identity, ownership, draft status, and manifest completeness.
                // The completeness test counts EVERY draft row, not only the READY ones. A
                // READY-only count is satisfied by supplying the ready subset while a
                // rejected sibling is quietly left behind, which would commit a payload the
                // reader never approved.
                {
                    Statement draftTotals(db_, "SELECT coalesce(count(*),0), coalesce(sum(CASE WHEN status='READY' THEN 1 ELSE 0 END),0) FROM draft_attachments WHERE project_id=?1;");
                    Bind(draftTotals.value, 1, id);
                    if (sqlite3_step(draftTotals.value) != SQLITE_ROW)
                    {
                        return rollbackAndReject(SaveDisposition::Conflict,
                            "The draft attachment set could not be read; the message was not submitted.");
                    }
                    int const totalDrafts = sqlite3_column_int(draftTotals.value, 0);
                    int const readyDrafts = sqlite3_column_int(draftTotals.value, 1);
                    if (totalDrafts != readyDrafts)
                    {
                        return rollbackAndReject(SaveDisposition::Conflict,
                            "One or more draft attachments are not ready, so the draft is incomplete. "
                            "The message was not submitted and the original draft is preserved; remove or "
                            "re-attach the affected attachment and try again.");
                    }
                    if (readyDrafts != static_cast<int>(attachments.size()))
                    {
                        return rollbackAndReject(SaveDisposition::Conflict,
                            "The draft attachment set changed before it could be saved; the message was not submitted.");
                    }
                }

                for (auto const& att : attachments)
                {
                    Statement checkDraft(db_, "SELECT file_size, sha256, staged_path, status FROM draft_attachments WHERE project_id=?1 AND attachment_id=?2;");
                    Bind(checkDraft.value, 1, id);
                    Bind(checkDraft.value, 2, att.id);
                    if (sqlite3_step(checkDraft.value) != SQLITE_ROW)
                    {
                        return rollbackAndReject(SaveDisposition::Conflict,
                            "A submitted attachment is no longer a draft of this project; the message was not submitted.");
                    }
                    if (Text(checkDraft.value, 3) != "READY" ||
                        static_cast<uint64_t>(sqlite3_column_int64(checkDraft.value, 0)) != att.fileSize ||
                        Text(checkDraft.value, 1) != att.sha256 ||
                        Text(checkDraft.value, 2) != att.storagePath)
                    {
                        return rollbackAndReject(SaveDisposition::Conflict,
                            "A submitted attachment is not ready or no longer matches its verified size, digest or staging path; the message was not submitted.");
                    }
                }

                Statement p(db_, "INSERT INTO contributions(project_id,submission_id,body) VALUES(?1,?2,?3);");
                Bind(p.value,1,id); Bind(p.value,2,submission); Bind(p.value,3,body);
                Check(sqlite3_step(p.value),db_);

                // Atomically link verified attachments to this submission
                for (auto const& att : attachments)
                {
                    Statement ins(db_, "INSERT INTO attachments(attachment_id,project_id,submission_id,file_name,file_size,sha256,detected_type,storage_path,source_path,status,error_message,version) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12);");
                    Bind(ins.value,1,att.id);
                    Bind(ins.value,2,id);
                    Bind(ins.value,3,submission);
                    Bind(ins.value,4,att.fileName);
                    sqlite3_bind_int64(ins.value,5,static_cast<sqlite3_int64>(att.fileSize));
                    Bind(ins.value,6,att.sha256);
                    Bind(ins.value,7,att.detectedType);
                    Bind(ins.value,8,att.storagePath);
                    Bind(ins.value,9,att.sourcePath);
                    Bind(ins.value,10,"COMMITTED");
                    Bind(ins.value,11,att.errorMessage);
                    sqlite3_bind_int64(ins.value,12,static_cast<sqlite3_int64>(att.version > 0 ? att.version : 1));
                    Check(sqlite3_step(ins.value),db_);
                }
                // Verified-byte finalization gate: seal committed files and verify attribute before transaction commit
#ifdef _WIN32
                std::size_t sealIndex = 0;
                for (auto& lockPtr : openLocks)
                {
                    // Injected failure: skip the seal for one attachment so the verification
                    // below fails, exercising the compensation path deterministically.
                    bool const injectSealFailure = (sealIndex == sealFailureIndexForTesting_);
                    ++sealIndex;

                    FILE_BASIC_INFO basicInfo{};
                    if (!injectSealFailure &&
                        GetFileInformationByHandleEx(lockPtr->handle, FileBasicInfo, &basicInfo, sizeof(basicInfo)))
                    {
                        basicInfo.FileAttributes |= FILE_ATTRIBUTE_READONLY;
                        SetFileInformationByHandle(lockPtr->handle, FileBasicInfo, &basicInfo, sizeof(basicInfo));
                    }
                    if (!injectSealFailure)
                    {
                        SetFileAttributesW(lockPtr->path.c_str(), FILE_ATTRIBUTE_READONLY);
                    }
                    DWORD attr = GetFileAttributesW(lockPtr->path.c_str());
                    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_READONLY))
                    {
                        return rollbackAndReject(SaveDisposition::Conflict,
                            "A verified attachment could not be sealed read-only before commit; the message was not submitted.");
                    }
                    lockPtr->sealed = true;
                }
#endif
            }

            // Draft and draft-attachment cleanup belongs to the ORIGINAL submission only.
            // On an identical replay the contribution is already committed and any newer
            // draft or newer draft attachments written since that commit must survive
            // unchanged; clearing them would silently destroy unsent user work.
            if (!duplicate)
            {
                {
                    Statement p(db_, "UPDATE projects SET draft='' WHERE id=?1;");
                    Bind(p.value,1,id);Check(sqlite3_step(p.value),db_);
                }
                {
                    Statement clrDraft(db_, "DELETE FROM draft_attachments WHERE project_id=?1;");
                    Bind(clrDraft.value,1,id);Check(sqlite3_step(clrDraft.value),db_);
                }
            }
            // The pending row is scoped to this exact submission, so retiring it is safe on
            // both paths: an unrelated newer pending submission cannot be reached here
            // (it is refused as a Conflict above), and this row has already been verified
            // to belong to the submission being saved or replayed.
            {
                Statement cleared(db_, "DELETE FROM pending_submissions WHERE project_id=?1 AND submission_id=?2;");
                Bind(cleared.value,1,id);Bind(cleared.value,2,submission);Check(sqlite3_step(cleared.value),db_);
            }
            Exec("COMMIT;");
            detail.rollbackConfirmed = true;
            detail.connectionUsable = true;
            detail.reasonEn = duplicate ? "This submission was already saved; its existing outcome is returned."
                                        : "The contribution was committed.";
        }
        catch (std::exception const& e)
        {
            // Compensation happens AFTER the transaction state is established, not before.
            // Unsealing first would turn a file back into a writable object even when the
            // commit outcome is unknown and the bytes may already be bound to a contribution.
            // Preserve the reason instead of returning a bare Unknown, and establish the
            // actual transaction state. A failure to roll back is NOT reported as a definite
            // rollback: if a transaction is still open the connection is quarantined and the
            // outcome stays UNKNOWN so the caller reconciles instead of retrying blindly.
            std::string reason = e.what();
            detail.reasonEn = reason;

            bool const wasInTransaction = (sqlite3_get_autocommit(db_) == 0);
            if (!began && !wasInTransaction)
            {
                // The failure happened before the transaction started; nothing was written
                // and nothing was sealed.
                detail.rollbackConfirmed = true;
                detail.connectionUsable = !poisoned_;
                detail.reasonEn = "The submission could not be prepared for saving: " + reason +
                    ". Nothing was written.";
                return SaveDisposition::Unknown;
            }

            char* message = nullptr;
            int const rc = sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &message);
            if (rc != SQLITE_OK)
            {
                std::string rollbackFailure = message ? message : sqlite3_errmsg(db_);
                if (message) sqlite3_free(message);
                reason += " (rollback failed: " + rollbackFailure + ")";
            }

            bool const stillInTransaction = (sqlite3_get_autocommit(db_) == 0);
            detail.rollbackConfirmed = !stillInTransaction;
            detail.connectionUsable = !stillInTransaction;
            if (stillInTransaction)
            {
                poisoned_ = true;
                detail.reasonEn = "The submission outcome is UNKNOWN: " + reason +
                    ". The transaction could not be rolled back, so the store is quarantined; "
                    "the submission must be reconciled before it is retried.";
            }
            else
            {
                // Proven not committed, so the sealed files are bound to nothing.
                detail.reasonEn = "The submission was not saved: " + reason +
                    ". The transaction was rolled back and the store is usable; the message can be retried safely.";
                if (!compensateSealedFiles())
                {
                    detail.reasonEn += " One or more already-sealed attachments could not have their "
                        "read-only attribute restored; they may still be read-only on disk.";
                }
            }
            return SaveDisposition::Unknown;
        }

        return duplicate ? SaveDisposition::Duplicate : SaveDisposition::Saved;
    }

    std::vector<Contribution> Storage::Contributions(std::string const& id)
    {
        std::lock_guard lock(mutex_);
        Statement p(db_, "SELECT submission_id,body,created_at FROM contributions WHERE project_id=?1 ORDER BY row_id;");
        Bind(p.value,1,id);
        std::vector<Contribution> result;
        int rc=SQLITE_OK;
        while ((rc=sqlite3_step(p.value))==SQLITE_ROW)
        {
            Contribution c{Text(p.value,0),Text(p.value,1),Text(p.value,2)};
            Statement att(db_, "SELECT attachment_id,project_id,submission_id,file_name,file_size,sha256,detected_type,storage_path,source_path,status,error_message,version,created_at FROM attachments WHERE submission_id=?1 ORDER BY rowid;");
            Bind(att.value,1,c.submissionId);
            int attRc=SQLITE_OK;
            while ((attRc=sqlite3_step(att.value))==SQLITE_ROW)
            {
                c.attachments.push_back({
                    Text(att.value,0),
                    Text(att.value,1),
                    Text(att.value,2),
                    Text(att.value,3),
                    static_cast<uint64_t>(sqlite3_column_int64(att.value,4)),
                    Text(att.value,5),
                    Text(att.value,6),
                    Text(att.value,7),
                    Text(att.value,8),
                    Text(att.value,9),
                    Text(att.value,10),
                    static_cast<uint64_t>(sqlite3_column_int64(att.value,11)),
                    Text(att.value,12)
                });
            }
            result.push_back(std::move(c));
        }
        Check(rc,db_);
        return result;
    }

    MessageSearchResult Storage::SearchUserMessages(MessageSearchQuery const& query)
    {
        MessageSearchResult result;
        if (query.projectId.empty())
        {
            return result;
        }

        std::string queryText = query.queryText;
        if (queryText.size() > 1000)
        {
            queryText = queryText.substr(0, 1000);
        }

        if (queryText.empty())
        {
            result.isComplete = true;
            result.totalMatches = 0;
            result.hasMore = false;
            return result;
        }

        size_t limit = query.limit;
        if (limit == 0) limit = 20;
        if (limit > 100) limit = 100;
        size_t offset = (query.offset > 1000000) ? 1000000 : query.offset;

        std::lock_guard lock(mutex_);
        try
        {
            std::string pattern = "%" + EscapeLike(queryText) + "%";

            // Count total matches
            {
                Statement countStmt(db_, "SELECT count(*) FROM contributions WHERE project_id = ?1 AND body LIKE ?2 ESCAPE '\\';");
                Bind(countStmt.value, 1, query.projectId);
                Bind(countStmt.value, 2, pattern);
                int rc = sqlite3_step(countStmt.value);
                if (rc == SQLITE_ROW)
                {
                    result.totalMatches = static_cast<size_t>(sqlite3_column_int64(countStmt.value, 0));
                }
            }

            // Fetch matching page
            {
                Statement dataStmt(db_, "SELECT submission_id, body, created_at FROM contributions WHERE project_id = ?1 AND body LIKE ?2 ESCAPE '\\' ORDER BY row_id ASC LIMIT ?3 OFFSET ?4;");
                Bind(dataStmt.value, 1, query.projectId);
                Bind(dataStmt.value, 2, pattern);
                sqlite3_bind_int64(dataStmt.value, 3, static_cast<sqlite3_int64>(limit));
                sqlite3_bind_int64(dataStmt.value, 4, static_cast<sqlite3_int64>(offset));

                int rc = SQLITE_OK;
                while ((rc = sqlite3_step(dataStmt.value)) == SQLITE_ROW)
                {
                    result.matches.push_back(MessageMatch{
                        Text(dataStmt.value, 0),
                        Text(dataStmt.value, 1),
                        Text(dataStmt.value, 2)
                    });
                }
                Check(rc, db_);
            }

            result.hasMore = (offset + result.matches.size() < result.totalMatches);
            result.isComplete = true;
        }
        catch (std::exception const& e)
        {
            result.isComplete = false;
            result.errorMessage = e.what();
        }
        return result;
    }

    void Storage::RefusePendingManifestMutation_locked(
        std::string const& projectId,
        std::string const& attachmentId,
        std::string const& newStatus,
        std::string const& newSha256,
        std::uint64_t newSize,
        std::string const& newPath)
    {
        Statement pending(db_, "SELECT submission_id FROM pending_submissions WHERE project_id=?1;");
        Bind(pending.value, 1, projectId);
        if (sqlite3_step(pending.value) != SQLITE_ROW) return; // nothing is locked

        Statement current(db_, "SELECT status, sha256, file_size, staged_path FROM draft_attachments WHERE project_id=?1 AND attachment_id=?2;");
        Bind(current.value, 1, projectId);
        Bind(current.value, 2, attachmentId);
        if (sqlite3_step(current.value) != SQLITE_ROW)
        {
            // A brand-new row. A non-READY row is not part of the locked manifest, so
            // recording a failed attachment is still allowed while a submission is
            // pending - the Action Center is expected to show both obligation kinds.
            // A READY row would widen the payload the reader is being asked to reconcile,
            // and is refused.
            if (newStatus != "READY") return;
            throw std::runtime_error(
                "An unresolved submission is locked for this project, so a new attachment cannot be added to "
                "its draft. Reconcile the pending submission first; nothing was changed.");
        }

        std::string const oldStatus = Text(current.value, 0);
        std::string const oldSha = Text(current.value, 1);
        auto const oldSize = static_cast<std::uint64_t>(sqlite3_column_int64(current.value, 2));
        std::string const oldPath = Text(current.value, 3);

        // Rows outside the locked manifest are not constrained by it.
        if (oldStatus != "READY") return;

        bool const identityUnchanged = (newSha256 == oldSha) && (newSize == oldSize) && (newPath == oldPath);
        // A no-op rewrite, or a truthful degradation of a READY row, is permitted: the
        // manifest keeps meaning what it meant, and the pending submission is still
        // refused at commit time because the row is no longer READY.
        if (identityUnchanged) return;

        throw std::runtime_error(
            "An unresolved submission is locked for this project; this attachment is part of the payload it "
            "was admitted with, so its identity cannot be replaced (" + oldStatus + " -> " + newStatus +
            "). Nothing was changed.");
    }

    void Storage::RefusePendingManifestDeletion_locked(std::string const& projectId, std::string const& attachmentId)
    {
        Statement pending(db_, "SELECT submission_id FROM pending_submissions WHERE project_id=?1;");
        Bind(pending.value, 1, projectId);
        if (sqlite3_step(pending.value) != SQLITE_ROW) return;

        Statement current(db_, "SELECT status FROM draft_attachments WHERE project_id=?1 AND attachment_id=?2;");
        Bind(current.value, 1, projectId);
        Bind(current.value, 2, attachmentId);
        if (sqlite3_step(current.value) != SQLITE_ROW) return;

        if (Text(current.value, 0) != "READY") return;

        throw std::runtime_error(
            "An unresolved submission is locked for this project and this attachment is part of its payload, "
            "so it cannot be removed. Reconcile the pending submission first; nothing was changed.");
    }

    void Storage::SaveDraftAttachment(std::string const& projectId, AttachmentRecord const& att)
    {
        std::lock_guard lock(mutex_);
        AdmitWrite_locked();

        // The ownership check, the pending-manifest check and the upsert are one
        // transactional operation. Previously this path wrote with no transaction at all,
        // and the upsert matched on attachment_id alone, so a record naming another project
        // silently rewrote that project's row while the caller was told it succeeded.
        Exec("BEGIN IMMEDIATE;");
        try
        {
            RefuseForeignAttachmentIdentity_locked(projectId, att.id);
            RefusePendingManifestMutation_locked(projectId, att.id, att.status, att.sha256, att.fileSize, att.storagePath);
            Statement q(db_, "INSERT INTO draft_attachments(attachment_id,project_id,file_name,file_size,sha256,detected_type,staged_path,source_path,status,error_message,version) "
                             "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,1) "
                             "ON CONFLICT(attachment_id) DO UPDATE SET file_size=excluded.file_size,sha256=excluded.sha256,staged_path=excluded.staged_path,status=excluded.status,error_message=excluded.error_message,version=draft_attachments.version+1;");
            Bind(q.value,1,att.id);
            Bind(q.value,2,projectId);
            Bind(q.value,3,att.fileName);
            sqlite3_bind_int64(q.value,4,static_cast<sqlite3_int64>(att.fileSize));
            Bind(q.value,5,att.sha256);
            Bind(q.value,6,att.detectedType);
            Bind(q.value,7,att.storagePath);
            Bind(q.value,8,att.sourcePath);
            Bind(q.value,9,att.status);
            Bind(q.value,10,att.errorMessage);
            Check(sqlite3_step(q.value),db_);
            Exec("COMMIT;");
        }
        catch (...)
        {
            RollbackAfterFailure_locked();
            throw;
        }
    }

    void Storage::SaveDraftAttachments(std::string const& projectId, std::vector<AttachmentRecord> const& atts)
    {
        if (atts.empty()) return;

        // Every caller of this operation runs on the controlled background lane, so this
        // waits on the store mutex instead of polling it for a fixed 100 ms budget. The
        // previous bounded polling did not bound SQLite contention: it could fail early
        // under a transient C++ lock and, worse, let a caller proceed to BEGIN IMMEDIATE
        // while another connection held the write lock, where the real wait happens under
        // SQLite's own busy timeout. Blocking here, off the interface thread, is the honest
        // behaviour: the caller is told the truth by completing or by a real error.
        std::lock_guard<std::mutex> lock(mutex_);
        AdmitWrite_locked();
        Exec("BEGIN IMMEDIATE;");
        try
        {
            size_t idx = 0;
            for (auto const& att : atts)
            {
                // Every write in the batch is checked against the locked manifest inside
                // this transaction, so a partially-applied widening is impossible. The same
                // transaction also refuses an identity another project owns, so a rejected
                // batch leaves no partial update behind.
                RefuseForeignAttachmentIdentity_locked(projectId, att.id);
                RefusePendingManifestMutation_locked(projectId, att.id, att.status, att.sha256, att.fileSize, att.storagePath);
                if (saveDraftAttachmentsItemHook_)
                {
                    saveDraftAttachmentsItemHook_(idx, att.id);
                }
                Statement q(db_, "INSERT INTO draft_attachments(attachment_id,project_id,file_name,file_size,sha256,detected_type,staged_path,source_path,status,error_message,version) "
                                 "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,1) "
                                 "ON CONFLICT(attachment_id) DO UPDATE SET file_size=excluded.file_size,sha256=excluded.sha256,staged_path=excluded.staged_path,status=excluded.status,error_message=excluded.error_message,version=draft_attachments.version+1;");
                Bind(q.value,1,att.id);
                Bind(q.value,2,projectId);
                Bind(q.value,3,att.fileName);
                sqlite3_bind_int64(q.value,4,static_cast<sqlite3_int64>(att.fileSize));
                Bind(q.value,5,att.sha256);
                Bind(q.value,6,att.detectedType);
                Bind(q.value,7,att.storagePath);
                Bind(q.value,8,att.sourcePath);
                Bind(q.value,9,att.status);
                Bind(q.value,10,att.errorMessage);
                Check(sqlite3_step(q.value),db_);
                idx++;
            }
            Exec("COMMIT;");
        }
        catch (...)
        {
            // A rollback that was assumed rather than proven is how a connection ends up
            // presented as usable while still inside a transaction. The state is
            // established here, and an unresolved one quarantines the store.
            RollbackAfterFailure_locked();
            throw;
        }
    }

    std::optional<std::string> Storage::RemoveDraftAttachment(std::string const& projectId, std::string const& attachmentId)
    {
        std::lock_guard lock(mutex_);

        // Authorization and removal are one step. The previous shape let a caller read the
        // record, delete its bytes, and only then ask Storage whether that was allowed - so
        // a refusal arrived after the damage. Here the decision and the DELETE share a
        // serialized section, and the caller receives the path only if the row is gone.
        Exec("BEGIN IMMEDIATE;");
        try
        {
            RefusePendingManifestDeletion_locked(projectId, attachmentId);

            std::optional<std::string> staged;
            {
                Statement q(db_, "SELECT staged_path FROM draft_attachments WHERE project_id=?1 AND attachment_id=?2;");
                Bind(q.value, 1, projectId);
                Bind(q.value, 2, attachmentId);
                int const rc = sqlite3_step(q.value);
                if (rc == SQLITE_ROW) staged = Text(q.value, 0);
                else Check(rc, db_);
            }

            if (staged)
            {
                Statement q(db_, "DELETE FROM draft_attachments WHERE project_id=?1 AND attachment_id=?2;");
                Bind(q.value, 1, projectId);
                Bind(q.value, 2, attachmentId);
                Check(sqlite3_step(q.value), db_);
            }

            Exec("COMMIT;");
            return staged;
        }
        catch (...)
        {
            RollbackAfterFailure_locked();
            throw;
        }
    }

    std::vector<std::string> Storage::ClearDraftAttachments(std::string const& projectId)
    {
        std::lock_guard lock(mutex_);

        Exec("BEGIN IMMEDIATE;");
        try
        {
            {
                // Clearing the set would delete rows a locked submission's manifest covers.
                Statement pending(db_, "SELECT count(*) FROM pending_submissions WHERE project_id=?1;");
                Bind(pending.value, 1, projectId);
                if (sqlite3_step(pending.value) == SQLITE_ROW && sqlite3_column_int(pending.value, 0) > 0)
                {
                    Statement ready(db_, "SELECT count(*) FROM draft_attachments WHERE project_id=?1 AND status='READY';");
                    Bind(ready.value, 1, projectId);
                    if (sqlite3_step(ready.value) == SQLITE_ROW && sqlite3_column_int(ready.value, 0) > 0)
                    {
                        throw std::runtime_error(
                            "An unresolved submission is locked for this project and its attachments are part of its "
                            "payload, so the draft set cannot be cleared. Reconcile the pending submission first; "
                            "nothing was changed.");
                    }
                }
            }

            std::vector<std::string> staged;
            {
                Statement q(db_, "SELECT staged_path FROM draft_attachments WHERE project_id=?1;");
                Bind(q.value, 1, projectId);
                int rc = SQLITE_OK;
                while ((rc = sqlite3_step(q.value)) == SQLITE_ROW)
                {
                    std::string path = Text(q.value, 0);
                    if (!path.empty()) staged.push_back(std::move(path));
                }
                Check(rc, db_);
            }

            Statement q(db_, "DELETE FROM draft_attachments WHERE project_id=?1;");
            Bind(q.value, 1, projectId);
            Check(sqlite3_step(q.value), db_);

            Exec("COMMIT;");
            return staged;
        }
        catch (...)
        {
            RollbackAfterFailure_locked();
            throw;
        }
    }

    std::vector<AttachmentRecord> Storage::GetDraftAttachments(std::string const& projectId)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT attachment_id,project_id,'',file_name,file_size,sha256,detected_type,staged_path,source_path,status,error_message,version,created_at FROM draft_attachments WHERE project_id=?1 ORDER BY rowid;");
        Bind(q.value,1,projectId);
        std::vector<AttachmentRecord> result;
        int rc=SQLITE_OK;
        while ((rc=sqlite3_step(q.value))==SQLITE_ROW)
        {
            result.push_back({
                Text(q.value,0),
                Text(q.value,1),
                Text(q.value,2),
                Text(q.value,3),
                static_cast<uint64_t>(sqlite3_column_int64(q.value,4)),
                Text(q.value,5),
                Text(q.value,6),
                Text(q.value,7),
                Text(q.value,8),
                Text(q.value,9),
                Text(q.value,10),
                static_cast<uint64_t>(sqlite3_column_int64(q.value,11)),
                Text(q.value,12)
            });
        }
        Check(rc,db_);
        return result;
    }

    std::vector<AttachmentRecord> Storage::GetAttachmentsForContribution(std::string const& submissionId)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT attachment_id,project_id,submission_id,file_name,file_size,sha256,detected_type,storage_path,source_path,status,error_message,version,created_at FROM attachments WHERE submission_id=?1 ORDER BY rowid;");
        Bind(q.value,1,submissionId);
        std::vector<AttachmentRecord> result;
        int rc=SQLITE_OK;
        while ((rc=sqlite3_step(q.value))==SQLITE_ROW)
        {
            result.push_back({
                Text(q.value,0),
                Text(q.value,1),
                Text(q.value,2),
                Text(q.value,3),
                static_cast<uint64_t>(sqlite3_column_int64(q.value,4)),
                Text(q.value,5),
                Text(q.value,6),
                Text(q.value,7),
                Text(q.value,8),
                Text(q.value,9),
                Text(q.value,10),
                static_cast<uint64_t>(sqlite3_column_int64(q.value,11)),
                Text(q.value,12)
            });
        }
        Check(rc,db_);
        return result;
    }

    std::vector<AttachmentRecord> Storage::GetCommittedAttachmentsForProject(std::string const& projectId)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT attachment_id,project_id,submission_id,file_name,file_size,sha256,detected_type,storage_path,source_path,status,error_message,version,created_at FROM attachments WHERE project_id=?1 ORDER BY rowid;");
        Bind(q.value,1,projectId);
        std::vector<AttachmentRecord> result;
        int rc=SQLITE_OK;
        while ((rc=sqlite3_step(q.value))==SQLITE_ROW)
        {
            result.push_back({
                Text(q.value,0),
                Text(q.value,1),
                Text(q.value,2),
                Text(q.value,3),
                static_cast<uint64_t>(sqlite3_column_int64(q.value,4)),
                Text(q.value,5),
                Text(q.value,6),
                Text(q.value,7),
                Text(q.value,8),
                Text(q.value,9),
                Text(q.value,10),
                static_cast<uint64_t>(sqlite3_column_int64(q.value,11)),
                Text(q.value,12)
            });
        }
        Check(rc,db_);
        return result;
    }

    std::optional<std::string> Storage::ComputeSha256HexForTesting(std::string_view input)
    {
        return ComputeStringHashHex(input);
    }

    std::string Storage::ComputeAttachmentManifest(std::vector<AttachmentRecord> const& attachments)
    {
        if (attachments.empty()) return {};
        auto sorted = attachments;
        std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) {
            return a.id < b.id;
        });
        std::ostringstream ss;
        for (auto const& att : sorted)
        {
            ss << att.id << ":" << att.fileSize << ":" << att.sha256 << ";";
        }
        return ss.str();
        }

    ActionCenterResult Storage::QueryActionCenter(ActionCenterQuery const& query)
    {
        std::lock_guard lock(mutex_);
        ActionCenterResult result;
        try
        {
            if (query.pageSize < 1 || query.pageSize > 200)
                throw std::runtime_error("Action Center query exceeds page bounds");

            if (query.projectId.empty())
                throw std::runtime_error("Project ID cannot be empty");

            // Execute all queries within a single consistent SQLite read transaction/snapshot
            struct ReadTxGuard
            {
                sqlite3* db;
                bool* quarantined;
                bool forceCommitFailure;
                bool active{true};

                ReadTxGuard(sqlite3* d, bool* quarantinedFlag, bool forceFailure)
                    : db(d), quarantined(quarantinedFlag), forceCommitFailure(forceFailure)
                {
                    char* err = nullptr;
                    int rc = sqlite3_exec(db, "BEGIN DEFERRED;", nullptr, nullptr, &err);
                    if (rc != SQLITE_OK)
                    {
                        std::string msg = err ? err : "Failed to begin read transaction";
                        sqlite3_free(err);
                        throw std::runtime_error(msg);
                    }
                }
                ~ReadTxGuard()
                {
                    if (active && db)
                    {
                        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
                    }
                }
                void Commit()
                {
                    if (!active || !db) return;

                    if (forceCommitFailure)
                    {
                        // Injected: report a failed commit WITHOUT completing it. The
                        // transaction is deliberately left open, which is exactly the state
                        // the previous bookkeeping forgot about: it cleared `active` before
                        // establishing whether SQLite had finished, so the destructor
                        // declined to clean up and the transaction leaked into the next
                        // operation on this connection.
                        active = true; // guard still owns it; the destructor cleans up
                        throw std::runtime_error("Injected read-transaction commit failure");
                    }

                    char* err = nullptr;
                    int rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &err);
                    if (rc == SQLITE_OK)
                    {
                        active = false;
                        return;
                    }

                    std::string msg = err ? err : sqlite3_errmsg(db);
                    if (err) sqlite3_free(err);

                    // Ownership of the transaction is only relinquished once it is PROVEN
                    // to have ended. If a transaction is still open, clean it up here; if
                    // that also fails the connection can no longer be trusted and the store
                    // is quarantined, matching the write path's policy.
                    if (sqlite3_get_autocommit(db) == 0)
                    {
                        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
                        if (sqlite3_get_autocommit(db) == 0)
                        {
                            if (quarantined) *quarantined = true;
                            msg += " The read transaction could not be closed, so the store is quarantined "
                                   "until it is reopened.";
                        }
                        else
                        {
                            active = false;
                        }
                    }
                    else
                    {
                        active = false;
                    }
                    throw std::runtime_error(msg);
                }
            } tx(db_, &poisoned_, forceActionCenterCommitFailureForTesting_);

            std::string consistencyDigestInput;
            consistencyDigestInput.reserve(512);

            int currentProjectVersion = 0;
            int currentProjectClosed = 0;
            {
                Statement qVer(db_, "SELECT version, closed FROM projects WHERE id=?1;");
                Bind(qVer.value, 1, query.projectId);
                int rcVer = sqlite3_step(qVer.value);
                if (rcVer != SQLITE_ROW)
                {
                    if (rcVer == SQLITE_DONE)
                        throw std::runtime_error("Project not found");
                    Check(rcVer, db_);
                }
                currentProjectVersion = sqlite3_column_int(qVer.value, 0);
                currentProjectClosed = sqlite3_column_int(qVer.value, 1);
            }
            consistencyDigestInput += "proj:" + std::to_string(currentProjectVersion) + ":" + std::to_string(currentProjectClosed) + ";";

            {
                Statement qSubs(db_, "SELECT submission_id FROM pending_submissions WHERE project_id=?1 ORDER BY submission_id;");
                Bind(qSubs.value, 1, query.projectId);
                int rcSubs = SQLITE_OK;
                while ((rcSubs = sqlite3_step(qSubs.value)) == SQLITE_ROW)
                {
                    consistencyDigestInput += "sub:" + Text(qSubs.value, 0) + ";";
                }
                if (rcSubs != SQLITE_DONE)
                {
                    Check(rcSubs, db_);
                }
            }

            {
                Statement qAtts(db_, "SELECT attachment_id, version, status FROM draft_attachments WHERE project_id=?1 AND (status='REJECTED' OR status='SOURCE_CHANGED' OR status='TIMED_OUT') ORDER BY rowid;");
                Bind(qAtts.value, 1, query.projectId);
                int rcAtts = SQLITE_OK;
                while ((rcAtts = sqlite3_step(qAtts.value)) == SQLITE_ROW)
                {
                    consistencyDigestInput += "att:" + Text(qAtts.value, 0) + ":" + std::to_string(sqlite3_column_int64(qAtts.value, 1)) + ":" + Text(qAtts.value, 2) + ";";
                }
                if (rcAtts != SQLITE_DONE)
                {
                    Check(rcAtts, db_);
                }
            }

            std::optional<std::string> consistencySignature;
            if (!forceHashFailureForTesting_)
            {
                consistencySignature = ComputeStringHashHex(consistencyDigestInput);
            }
            if (!consistencySignature.has_value())
            {
                // Fail the query closed rather than emit a token whose integrity signature
                // could not be computed. No page is returned and no continuation token is
                // issued, so a caller cannot resume from unverified snapshot state.
                throw std::runtime_error(
                    "Action Center snapshot signature unavailable: SHA-256 computation failed; "
                    "no page or continuation token was issued. Recheck after storage is healthy.");
            }

            size_t offset = 0;
            if (!query.continuationToken.empty())
            {
                std::string const& token = query.continuationToken;
                std::vector<std::string> parts;
                size_t start = 0;
                while (start < token.size())
                {
                    size_t pos = token.find(':', start);
                    if (pos == std::string::npos) { parts.push_back(token.substr(start)); break; }
                    parts.push_back(token.substr(start, pos - start));
                    start = pos + 1;
                }
                if (parts.size() != 5 || parts[0] != "ac" || (parts[1] != "v2" && parts[1] != "v1"))
                {
                    throw std::runtime_error("Invalid continuation token: format invalid");
                }
                if (parts[2] != query.projectId)
                {
                    throw std::runtime_error("Invalid continuation token: project mismatch");
                }
                if (parts[1] == "v2")
                {
                    if (parts[3] != *consistencySignature)
                    {
                        throw std::runtime_error("Continuation token expired: participating record set mutated between pages");
                    }
                }
                else
                {
                    if (parts[3] != std::to_string(currentProjectVersion))
                    {
                        throw std::runtime_error("Continuation token expired: project state mutated");
                    }
                }
                try {
                    offset = std::stoull(parts[4]);
                } catch (...) {
                    throw std::runtime_error("Invalid continuation token: offset malformed");
                }
            }

            // Testing hook for external writer concurrency testing if registered
            if (queryActionCenterSnapshotHook_)
            {
                queryActionCenterSnapshotHook_();
            }

            char const* unifiedSql =
                "WITH candidates AS ("
                "  SELECT 1 AS source_rank, rowid AS sub_rank, 'unresolved-sub-' || submission_id AS item_id, "
                "         'MESSAGE_CONTRIBUTION_SERVICE' AS source_owner, submission_id AS source_record_id, "
                "         '1' AS source_record_version, 'UNKNOWN_EFFECT' AS state, "
                "         'Unresolved submission awaiting reconciliation' AS title_en, "
                "         'A Send outcome could not be confirmed. The draft and submission identity are retained; use the existing reconciliation path before resending. No duplicate contribution will be created.' AS explanation_en, "
                "         'pending_submissions.' || submission_id AS source_reference, 'Recheck outcome' AS available_action, 0 AS kind_int "
                "  FROM pending_submissions WHERE project_id=?1 "
                "  UNION ALL "
                "  SELECT 2 AS source_rank, rowid AS sub_rank, 'failed-att-' || attachment_id AS item_id, "
                "         'SQLITE_STORAGE' AS source_owner, attachment_id AS source_record_id, "
                "         CAST(version AS TEXT) AS source_record_version, 'COMPLETE' AS state, "
                "         'Attachment requires attention: ' || file_name AS title_en, "
                "         CASE WHEN error_message IS NOT NULL AND error_message != '' "
                "              THEN 'Attachment preparation failed or the source file changed. Remove or re-attach it before sending; nothing is sent automatically. Detail: ' || error_message "
                "              ELSE 'Attachment preparation failed or the source file changed. Remove or re-attach it before sending; nothing is sent automatically.' END AS explanation_en, "
                "         'draft_attachments.' || attachment_id AS source_reference, 'Remove or re-attach' AS available_action, 1 AS kind_int "
                "  FROM draft_attachments WHERE project_id=?1 AND (status='REJECTED' OR status='SOURCE_CHANGED' OR status='TIMED_OUT') "
                "  UNION ALL "
                "  SELECT 3 AS source_rank, rowid AS sub_rank, 'closed-project-' || id AS item_id, "
                "         'PROJECT_SERVICE' AS source_owner, id AS source_record_id, "
                "         CAST(version AS TEXT) AS source_record_version, 'COMPLETE' AS state, "
                "         'Project is closed' AS title_en, "
                "         'Work is blocked until an explicit, authorized Reopen Project action is taken. History remains readable; Send and Start remain gated.' AS explanation_en, "
                "         'projects.' || id AS source_reference, 'Reopen Project' AS available_action, 2 AS kind_int "
                "  FROM projects WHERE id=?1 AND closed != 0 "
                ") "
                "SELECT item_id, source_owner, source_record_id, source_record_version, state, title_en, explanation_en, source_reference, available_action, kind_int "
                "FROM candidates ORDER BY source_rank, sub_rank LIMIT ?2 OFFSET ?3;";

            Statement q(db_, unifiedSql);
            Bind(q.value, 1, query.projectId);
            sqlite3_bind_int64(q.value, 2, static_cast<sqlite3_int64>(query.pageSize + 1));
            sqlite3_bind_int64(q.value, 3, static_cast<sqlite3_int64>(offset));

            int rc = SQLITE_OK;
            while ((rc = sqlite3_step(q.value)) == SQLITE_ROW)
            {
                ActionCenterItem item;
                item.itemId = Text(q.value, 0);
                item.projectId = query.projectId;
                item.sourceOwner = Text(q.value, 1);
                item.sourceRecordId = Text(q.value, 2);
                item.sourceRecordVersion = Text(q.value, 3);
                item.state = Text(q.value, 4);
                item.titleEn = Text(q.value, 5);
                item.explanationEn = Text(q.value, 6);
                item.sourceReference = Text(q.value, 7);
                item.availableAction = Text(q.value, 8);
                int kindInt = sqlite3_column_int(q.value, 9);
                switch (kindInt)
                {
                    case 0: item.kind = ActionCenterKind::UnresolvedSubmission; break;
                    case 1: item.kind = ActionCenterKind::FailedAttachment; break;
                    case 2: item.kind = ActionCenterKind::ClosedProjectRestriction; break;
                    default: item.kind = ActionCenterKind::StorageFailure; break;
                }
                result.items.push_back(std::move(item));
            }
            if (rc != SQLITE_DONE)
            {
                Check(rc, db_);
            }

            tx.Commit();

            if (result.items.size() > query.pageSize)
            {
                result.items.pop_back();
                result.isComplete = false;
                result.continuationToken = "ac:v2:" + query.projectId + ":" +
                    *consistencySignature + ":" + std::to_string(offset + query.pageSize);
            }
            else
            {
                result.isComplete = true;
                result.continuationToken = "";
            }

            for (auto const& itm : result.items)
            {
                result.ownerRecordVersions.push_back(itm.sourceOwner + ":" + itm.sourceRecordId + ":" + itm.sourceRecordVersion);
            }
        }
        catch (std::exception const& e)
        {
            result.isComplete = false;
            result.errorMessage = e.what();
            result.ownerUnavailable = false;
        }
        return result;
    }

    AttachmentInspectionResult Storage::InspectAttachment(AttachmentInspectionQuery const& query)
    {
        AttachmentInspectionResult result;

        struct RecordSnapshot
        {
            bool found{false};
            std::string attachmentId;
            std::string projectId;
            std::string sourceRecordId;
            std::string sourceRecordVersion;
            std::string fileName;
            uint64_t fileSize{0};
            std::string sha256;
            std::string detectedType;
            std::string storagePath;
            std::string sourcePath;
            std::string status;
            std::string errorMessage;
            bool isCommitted{false};
        } snap;

        // Step 1: Query database records under storage mutex lock
        {
            std::lock_guard lock(mutex_);
            try
            {
                // Look in draft attachments first
                Statement q(db_, "SELECT attachment_id,project_id,file_name,file_size,sha256,detected_type,staged_path,source_path,status,error_message,version "
                                 "FROM draft_attachments WHERE attachment_id=?1 AND project_id=?2;");
                Bind(q.value, 1, query.attachmentId);
                Bind(q.value, 2, query.projectId);
                int rc = sqlite3_step(q.value);
                if (rc == SQLITE_ROW)
                {
                    snap.found = true;
                    snap.attachmentId = Text(q.value, 0);
                    snap.projectId = Text(q.value, 1);
                    snap.sourceRecordId = query.attachmentId;
                    snap.sourceRecordVersion = std::to_string(sqlite3_column_int64(q.value, 10));
                    snap.fileName = Text(q.value, 2);
                    snap.fileSize = static_cast<uint64_t>(sqlite3_column_int64(q.value, 3));
                    snap.sha256 = Text(q.value, 4);
                    snap.detectedType = Text(q.value, 5);
                    snap.storagePath = Text(q.value, 6);
                    snap.sourcePath = Text(q.value, 7);
                    snap.status = Text(q.value, 8);
                    snap.errorMessage = Text(q.value, 9);
                    snap.isCommitted = false;
                }
                else
                {
                    // Look in committed attachments
                    Statement q2(db_, "SELECT attachment_id,project_id,submission_id,file_name,file_size,sha256,detected_type,storage_path,source_path,status,error_message,version "
                                      "FROM attachments WHERE attachment_id=?1 AND project_id=?2;");
                    Bind(q2.value, 1, query.attachmentId);
                    Bind(q2.value, 2, query.projectId);
                    rc = sqlite3_step(q2.value);
                    if (rc == SQLITE_ROW)
                    {
                        snap.found = true;
                        snap.attachmentId = Text(q2.value, 0);
                        snap.projectId = Text(q2.value, 1);
                        snap.sourceRecordId = query.attachmentId;
                        snap.sourceRecordVersion = std::to_string(sqlite3_column_int64(q2.value, 11));
                        snap.fileName = Text(q2.value, 3);
                        snap.fileSize = static_cast<uint64_t>(sqlite3_column_int64(q2.value, 4));
                        snap.sha256 = Text(q2.value, 5);
                        snap.detectedType = Text(q2.value, 6);
                        snap.storagePath = Text(q2.value, 7);
                        snap.sourcePath = Text(q2.value, 8);
                        snap.status = Text(q2.value, 9);
                        snap.errorMessage = Text(q2.value, 10);
                        snap.isCommitted = true;
                    }
                }
                Check(rc, db_);
            }
            catch (std::exception const& e)
            {
                result.found = false;
                result.errorMessage = e.what();
                return result;
            }
        } // Storage mutex lock released! File I/O and hashing happen completely outside lock!

        if (!snap.found)
        {
            result.found = false;
            return result;
        }

        result.found = true;
        result.inspection.attachmentId = snap.attachmentId;
        result.inspection.projectId = snap.projectId;
        result.inspection.sourceRecordId = snap.sourceRecordId;
        result.inspection.sourceRecordVersion = snap.sourceRecordVersion;
        result.inspection.fileName = snap.fileName;
        result.inspection.fileSize = snap.fileSize;
        result.inspection.sha256 = snap.sha256;
        result.inspection.detectedType = snap.detectedType;
        result.inspection.storagePath = snap.storagePath;
        result.inspection.status = snap.status;
        result.inspection.titleEn = "Attachment: " + snap.fileName;
        result.inspection.explanationEn = snap.isCommitted ?
            "Read-only inspection of managed attachment bytes. Original source path is not reopened for reading; only the verified managed copy is exposed." :
            "Read-only inspection of staged draft attachment bytes. Original source path is not reopened for reading; only the verified managed copy is exposed.";

        // Step 2: Establish managed-path identity outside storage mutex through open handle
        // DO NOT mutate the filesystem: do not create directories if absent.
        auto managedRootDir = ManagedAttachmentRoot();
        std::error_code ec;
        bool managedRootExists = std::filesystem::exists(managedRootDir, ec) && !ec;
        std::wstring canonicalManagedRoot;
        if (managedRootExists)
        {
            canonicalManagedRoot = GetCanonicalDirectoryPath(managedRootDir.wstring());
            if (!canonicalManagedRoot.empty() && canonicalManagedRoot.back() != L'\\')
            {
                canonicalManagedRoot += L'\\';
            }
        }

        bool isManaged = false;
        bool integrityVerified = false;
        std::string previewContent;
        bool previewEligible = false;
        std::string previewUnavailableReasonEn;

        std::wstring stagedW = Utf8ToWide(snap.storagePath);
        std::wstring sourceW = Utf8ToWide(snap.sourcePath);
        bool const aliasesSource = (!snap.storagePath.empty() && !snap.sourcePath.empty() &&
                                    (snap.storagePath == snap.sourcePath || _wcsicmp(stagedW.c_str(), sourceW.c_str()) == 0));

        if (!managedRootExists || canonicalManagedRoot.empty())
        {
            previewUnavailableReasonEn = "Managed attachment storage directory is unavailable.";
        }
        else if (snap.storagePath.empty())
        {
            previewUnavailableReasonEn = "Managed copy path is empty.";
        }
        else if (aliasesSource)
        {
            previewUnavailableReasonEn = "Storage path aliases source path; unisolated files cannot be previewed.";
        }
        else if (snap.storagePath.find("..") != std::string::npos)
        {
            previewUnavailableReasonEn = "Storage path contains directory traversal.";
        }
        else
        {
            HANDLE hFile = CreateFileW(stagedW.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE && snap.isCommitted)
            {
                hFile = CreateFileW(stagedW.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, nullptr);
            }
            if (hFile != INVALID_HANDLE_VALUE)
            {
                BY_HANDLE_FILE_INFORMATION info{};
                if (GetFileInformationByHandle(hFile, &info) &&
                    !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                    !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    if (!snap.isCommitted || (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY))
                    {
                        std::wstring canonicalFile = GetCanonicalPathFromHandle(hFile);
                        if (canonicalFile.size() >= canonicalManagedRoot.size() &&
                            _wcsnicmp(canonicalFile.c_str(), canonicalManagedRoot.c_str(), canonicalManagedRoot.size()) == 0)
                        {
                            // Managed file established via open handle containment inside canonicalManagedRoot
                            // without reopening original source file!
                            isManaged = true;

                            // Step 3: Compute digest directly from verified open handle (prevents file replacement during inspection)
                            auto hashRes = ComputeSha256FromHandle(hFile);
                            if (hashRes.success && hashRes.size == snap.fileSize &&
                                hashRes.sha256 == snap.sha256 && !snap.sha256.empty())
                            {
                                integrityVerified = true;
                            }
                            else
                            {
                                integrityVerified = false;
                            }

                            // Step 4: Preview evaluation with 10 MiB ceiling, status, renderer, and English-safety check
                            if (snap.fileSize > 10ULL * 1024ULL * 1024ULL) // 10 MiB ceiling
                            {
                                previewEligible = false;
                                previewUnavailableReasonEn = "File size (" + std::to_string(snap.fileSize) +
                                    " bytes) exceeds the 10 MiB content preview ceiling; metadata inspection only.";
                            }
                            else if (!integrityVerified)
                            {
                                previewEligible = false;
                                previewUnavailableReasonEn = "Attachment bytes failed integrity verification: size or SHA-256 digest mismatch.";
                            }
                            else if (snap.status != "READY" && snap.status != "COMMITTED")
                            {
                                previewEligible = false;
                                previewUnavailableReasonEn = "Attachment is in non-ready status (" + snap.status + ") and cannot be previewed.";
                            }
                            else if (snap.detectedType != "text/plain" && snap.detectedType != "text/markdown")
                            {
                                previewEligible = false;
                                previewUnavailableReasonEn = "No approved preview renderer for content type " +
                                    (snap.detectedType.empty() ? "(unspecified)" : snap.detectedType);
                            }
                            else
                            {
                                // Read file content from the verified open handle for preview
                                LARGE_INTEGER zeroOffset{};
                                SetFilePointerEx(hFile, zeroOffset, nullptr, FILE_BEGIN);
                                std::string contentBytes(static_cast<size_t>(snap.fileSize), '\0');
                                DWORD bytesRead = 0;
                                if (ReadFile(hFile, contentBytes.data(), static_cast<DWORD>(snap.fileSize), &bytesRead, nullptr) &&
                                    bytesRead == snap.fileSize)
                                {
                                    if (IsEnglishSafeText(contentBytes))
                                    {
                                        previewEligible = true;
                                        previewContent = std::move(contentBytes);
                                        previewUnavailableReasonEn = "";
                                    }
                                    else
                                    {
                                        previewEligible = false;
                                        previewUnavailableReasonEn = "Attachment contains non-English characters or unverifiable prose; English-only preview rule applies. Metadata inspection is preserved.";
                                    }
                                }
                                else
                                {
                                    previewEligible = false;
                                    previewUnavailableReasonEn = "Failed to read attachment bytes for content preview.";
                                }
                            }
                        }
                        else
                        {
                            previewUnavailableReasonEn = "Attachment is located outside approved managed storage boundary.";
                        }
                    }
                    else
                    {
                        previewUnavailableReasonEn = "Committed attachment is missing required read-only attribute.";
                    }
                }
                else
                {
                    previewUnavailableReasonEn = "Attachment file is invalid or a reparse point / directory.";
                }
                CloseHandle(hFile);
            }
            else
            {
                previewUnavailableReasonEn = "Cannot open managed copy on disk.";
            }
        }

        // Step 5: Revalidate record under storage mutex lock after file work completes
        // Ensures that if the attachment was removed, replaced, or changed while inspection was in flight,
        // stale content is withheld.
        if (inspectionFileHashedHook_)
        {
            inspectionFileHashedHook_();
        }

        {
            std::lock_guard lock(mutex_);
            try
            {
                bool stillValid = false;
                if (!snap.isCommitted)
                {
                    Statement q(db_, "SELECT version, file_size, sha256, staged_path, status FROM draft_attachments WHERE attachment_id=?1 AND project_id=?2;");
                    Bind(q.value, 1, query.attachmentId);
                    Bind(q.value, 2, query.projectId);
                    if (sqlite3_step(q.value) == SQLITE_ROW)
                    {
                        std::string curVer = std::to_string(sqlite3_column_int64(q.value, 0));
                        uint64_t curSize = static_cast<uint64_t>(sqlite3_column_int64(q.value, 1));
                        std::string curSha = Text(q.value, 2);
                        std::string curPath = Text(q.value, 3);
                        std::string curStatus = Text(q.value, 4);

                        if (curVer == snap.sourceRecordVersion &&
                            curSize == snap.fileSize &&
                            curSha == snap.sha256 &&
                            curPath == snap.storagePath &&
                            curStatus == snap.status)
                        {
                            stillValid = true;
                        }
                    }
                }
                else
                {
                    Statement q2(db_, "SELECT version, file_size, sha256, storage_path, status FROM attachments WHERE attachment_id=?1 AND project_id=?2;");
                    Bind(q2.value, 1, query.attachmentId);
                    Bind(q2.value, 2, query.projectId);
                    if (sqlite3_step(q2.value) == SQLITE_ROW)
                    {
                        std::string curVer = std::to_string(sqlite3_column_int64(q2.value, 0));
                        uint64_t curSize = static_cast<uint64_t>(sqlite3_column_int64(q2.value, 1));
                        std::string curSha = Text(q2.value, 2);
                        std::string curPath = Text(q2.value, 3);
                        std::string curStatus = Text(q2.value, 4);

                        if (curVer == snap.sourceRecordVersion &&
                            curSize == snap.fileSize &&
                            curSha == snap.sha256 &&
                            curPath == snap.storagePath &&
                            curStatus == snap.status)
                        {
                            stillValid = true;
                        }
                    }
                }

                if (!stillValid)
                {
                    result.found = false;
                    result.inspection = AttachmentInspection{};
                    result.errorMessage = "Attachment record was removed, replaced, or changed during inspection; stale inspection withheld.";
                    return result;
                }
            }
            catch (std::exception const& e)
            {
                result.found = false;
                result.inspection = AttachmentInspection{};
                result.errorMessage = e.what();
                return result;
            }
        }

        result.inspection.isManaged = isManaged;
        result.inspection.integrityVerified = integrityVerified;
        result.inspection.previewEligible = previewEligible;
        result.inspection.previewUnavailableReasonEn = previewUnavailableReasonEn;
        result.inspection.previewContent = std::move(previewContent);

        return result;
    }
}
