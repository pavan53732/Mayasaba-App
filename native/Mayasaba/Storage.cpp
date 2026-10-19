#include "Storage.h"
#include "ProjectRoot.h"
#include "NativeIds.h"
#include "ProtectedDraft.h"
#include <filesystem>
#include <utility>
#include <sstream>
#include <iomanip>
#include <unordered_set>
#ifdef _WIN32
#include <shlobj.h>
#include <bcrypt.h>
#endif

namespace mayasaba
{
    namespace
    {
        struct Statement
        {
            sqlite3_stmt* value{};
            Statement(sqlite3* db, char const* sql)
            {
                if (sqlite3_prepare_v2(db, sql, -1, &value, nullptr) != SQLITE_OK)
                    throw std::runtime_error(sqlite3_errmsg(db));
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

        inline std::string ComputeStringHashHex(std::string_view input)
        {
            BCRYPT_ALG_HANDLE hAlg = nullptr;
            NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
            if (!BCRYPT_SUCCESS(status)) return {};

            DWORD hashObjSize = 0, dataLen = 0;
            BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PBYTE>(&hashObjSize), sizeof(DWORD), &dataLen, 0);
            std::vector<BYTE> hashObj(hashObjSize);

            BCRYPT_HASH_HANDLE hHash = nullptr;
            status = BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0);
            if (!BCRYPT_SUCCESS(status))
            {
                BCryptCloseAlgorithmProvider(hAlg, 0);
                return {};
            }

            BCryptHashData(hHash, reinterpret_cast<PBYTE>(const_cast<char*>(input.data())), static_cast<ULONG>(input.size()), 0);

            BYTE hash[32];
            BCryptFinishHash(hHash, hash, 32, 0);
            BCryptDestroyHash(hHash);
            BCryptCloseAlgorithmProvider(hAlg, 0);

            std::ostringstream ss;
            for (int j = 0; j < 16; ++j)
            {
                ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[j]);
            }
            return ss.str();
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

            // 2. Tokenize into lowercase alphabetic words
            std::vector<std::string> words;
            std::string current;
            for (char ch : text)
            {
                if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))
                {
                    current.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                }
                else
                {
                    if (!current.empty())
                    {
                        words.push_back(std::move(current));
                        current.clear();
                    }
                }
            }
            if (!current.empty())
            {
                words.push_back(std::move(current));
            }

            if (words.empty()) return false;

            // 3. Reject non-English marker words (e.g. Spanish, French, German, Italian, Portuguese)
            static const std::unordered_set<std::string_view> nonEnglishMarkers = {
                "hola", "mundo", "gracias", "por", "favor", "buenos", "dias", "amigo", "este", "todo", "bien", "como", "pero", "para", "los", "las", "una", "unos", "unas", "del",
                "bonjour", "monde", "merci", "avec", "pour", "dans", "sur", "cette", "tout",
                "hallo", "welt", "danke", "bitte", "und", "oder", "nicht", "ein", "eine", "fuer",
                "ciao", "mondo", "grazie", "per", "favore", "molto", "questo",
                "ola", "obrigado", "voce"
            };

            for (auto const& w : words)
            {
                if (nonEnglishMarkers.find(w) != nonEnglishMarkers.end())
                {
                    return false;
                }
            }

            // 4. Require positive English vocabulary establishment
            static const std::unordered_set<std::string_view> englishCore = {
                "the", "be", "to", "of", "and", "a", "in", "that", "have", "i", "it", "for", "not", "on", "with",
                "he", "as", "you", "do", "at", "this", "but", "his", "by", "from", "they", "we", "say", "her",
                "she", "or", "an", "will", "my", "one", "all", "would", "there", "their", "what", "so", "up",
                "out", "if", "about", "who", "get", "which", "go", "me", "when", "make", "can", "like", "time",
                "no", "just", "him", "know", "take", "people", "into", "year", "your", "good", "some", "could",
                "them", "see", "other", "than", "then", "now", "look", "only", "come", "its", "over", "think",
                "also", "back", "after", "use", "two", "how", "our", "work", "first", "well", "way", "even",
                "new", "want", "because", "any", "these", "give", "day", "most", "us", "is", "are", "was",
                "were", "been", "has", "had", "mayasaba", "verification", "inspection", "attachment", "payload",
                "acceptance", "native", "file", "spec", "project", "code", "system", "test", "error", "data",
                "staged", "source", "markdown", "text", "version", "valid", "testing", "content", "sample",
                "authorized", "committed", "draft", "document", "specification", "english", "real", "drag", "drop",
                "hello", "world", "ready", "user", "run", "start", "stop", "pause", "read", "write", "open", "close",
                "name", "path", "root", "entry", "output", "input", "line", "item", "list", "check", "pass", "fail",
                "note", "title", "info", "true", "false", "status", "hash", "digest", "size", "byte", "bytes", "format",
                "string", "type", "rule", "rules", "task", "tasks", "action", "detail", "details", "directory", "summary",
                "result", "results", "found", "unresolved", "closed", "restriction", "recheck", "remove", "reopen",
                "policy", "preview", "eligible", "reason", "sha", "state", "epoch", "requirement", "decision", "council",
                "review", "peer", "response", "reply", "drafts", "files", "here", "where", "more"
            };

            static const std::unordered_set<std::string_view> englishStructural = {
                "the", "and", "to", "of", "a", "in", "is", "it", "for", "on", "are", "with", "be", "this",
                "have", "from", "or", "not", "we", "can", "if", "will", "its", "all", "an", "as", "by", "that"
            };

            size_t englishWordCount = 0;
            bool hasStructural = false;
            for (auto const& w : words)
            {
                if (englishCore.find(w) != englishCore.end())
                {
                    englishWordCount++;
                }
                if (englishStructural.find(w) != englishStructural.end())
                {
                    hasStructural = true;
                }
            }

            if (words.size() == 1)
            {
                return englishCore.find(words[0]) != englishCore.end();
            }

            // For multi-word text: must establish English eligibility through English words
            // If it has non-English markers, it was already rejected above.
            // Positive establishment: requires either an English structural word and at least 2 English words,
            // or at least 40% of words recognized in englishCore.
            if (hasStructural)
            {
                if (englishWordCount >= 2 || (englishWordCount * 3 >= words.size()))
                {
                    return true;
                }
            }
            else
            {
                if (englishWordCount >= 2 && (englishWordCount * 2 >= words.size()))
                {
                    return true;
                }
            }

            return false;
        }
#endif
    }

    void Storage::Check(int rc, sqlite3* db)
    {
        if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    void Storage::Exec(char const* sql)
    {
        if(std::string_view(sql).starts_with("BEGIN IMMEDIATE")) CheckWriteCapacity();
        char* error = nullptr;
        auto const rc = sqlite3_exec(db_, sql, nullptr, nullptr, &error);
        if (rc != SQLITE_OK)
        {
            std::string message = error ? error : sqlite3_errmsg(db_);
            if (error) sqlite3_free(error);
            throw std::runtime_error(message);
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
            sqlite3_busy_timeout(db_, 5000);
            int schemaVersion{};
            {
                Statement version(db_, "PRAGMA user_version;");
                Check(sqlite3_step(version.value), db_);
                schemaVersion = sqlite3_column_int(version.value, 0);
                if (schemaVersion > 3) throw std::runtime_error("Store schema is newer than this application; no migration was attempted");
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
            Exec("PRAGMA user_version=3;");
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
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
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

    void Storage::TouchProjectOpened(std::string const& id)
    {
        std::lock_guard lock(mutex_);
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
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            throw;
        }
    }
    void Storage::RelocateProject(std::string const& id, std::string const& newCanonicalPath, std::string const& newDisplayName)
    {
        std::lock_guard lock(mutex_);
        CheckWriteCapacity();
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
        CheckWriteCapacity();
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
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            throw;
        }
    }

    void Storage::RecordDirectoryIntent(std::string const& opId, std::string const& parentPath, std::string const& folderName, std::string const& targetPath)
    {
        std::lock_guard lock(mutex_);
        CheckWriteCapacity();
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
        CheckWriteCapacity();
        Statement q(db_, "UPDATE directory_operations SET state='CREATED', observed_identity=?2, "
                         "updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE operation_id=?1;");
        Bind(q.value, 1, opId);
        Bind(q.value, 2, observedIdentity);
        Check(sqlite3_step(q.value), db_);
    }

    void Storage::RecordDirectoryBound(std::string const& opId)
    {
        std::lock_guard lock(mutex_);
        CheckWriteCapacity();
        Statement q(db_, "UPDATE directory_operations SET state='BOUND', "
                         "updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE operation_id=?1;");
        Bind(q.value, 1, opId);
        Check(sqlite3_step(q.value), db_);
    }

    void Storage::RecordDirectoryFailed(std::string const& opId, std::string const& errorMessage)
    {
        std::lock_guard lock(mutex_);
        CheckWriteCapacity();
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
        CheckWriteCapacity();
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
        std::lock_guard lock(mutex_);
        CheckWriteCapacity();
        Statement q(db_, "UPDATE projects SET draft=?2 WHERE id=?1;");
        Bind(q.value,1,id); Bind(q.value,2,ProtectDraft(body));
        Check(sqlite3_step(q.value),db_);
        if (sqlite3_changes(db_)!=1) throw std::runtime_error("Draft project not found");
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
            // Query ready draft attachments to bind manifest
            std::vector<AttachmentRecord> drafts;
            {
                Statement qDrafts(db_, "SELECT attachment_id, file_size, sha256 FROM draft_attachments WHERE project_id=?1 AND status='READY' ORDER BY attachment_id;");
                Bind(qDrafts.value, 1, id);
                int rc{};
                while ((rc = sqlite3_step(qDrafts.value)) == SQLITE_ROW)
                {
                    AttachmentRecord r;
                    r.id = Text(qDrafts.value, 0);
                    r.fileSize = static_cast<uint64_t>(sqlite3_column_int64(qDrafts.value, 1));
                    r.sha256 = Text(qDrafts.value, 2);
                    drafts.push_back(std::move(r));
                }
                Check(rc, db_);
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
        CheckWriteCapacity();
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
        if (submission.empty() || (body.empty() && attachments.empty())) return SaveDisposition::Conflict;
        // P0: Enforce 50-file limit inside Storage boundary
        if (attachments.size() > 50) return SaveDisposition::Conflict;

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
                return SaveDisposition::Conflict;
            }
            if (att.fileSize == 0 || att.fileSize > 100ULL * 1024ULL * 1024ULL) // 100 MiB limit
            {
                return SaveDisposition::Conflict;
            }
            if (att.sha256.size() != 64)
            {
                return SaveDisposition::Conflict;
            }
            aggregateBytes += att.fileSize;
            if (aggregateBytes > 500ULL * 1024ULL * 1024ULL) // 500 MiB aggregate limit
            {
                return SaveDisposition::Conflict;
            }

#ifdef _WIN32
            // Verified-byte admission lock: open with exclusive write protection (FILE_SHARE_READ only).
            // This prevents any concurrent process or thread from modifying, truncating, deleting, or renaming the file.
            std::wstring stagedW = Utf8ToWide(att.storagePath);
            std::filesystem::path stagedP(stagedW);
            std::error_code ec;
            if (!std::filesystem::exists(stagedP, ec) || std::filesystem::is_directory(stagedP, ec))
            {
                return SaveDisposition::Conflict;
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
                return SaveDisposition::Conflict;
            }

            LARGE_INTEGER sizeLi{};
            if (!GetFileSizeEx(h, &sizeLi) || static_cast<uint64_t>(sizeLi.QuadPart) != att.fileSize)
            {
                CloseHandle(h);
                return SaveDisposition::Conflict;
            }

            // Read directly through the protected handle to compute SHA-256
            BCRYPT_ALG_HANDLE hAlg = nullptr;
            NTSTATUS bStatus = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
            if (!BCRYPT_SUCCESS(bStatus))
            {
                CloseHandle(h);
                return SaveDisposition::Conflict;
            }

            DWORD hashObjSize = 0, dataLen = 0;
            bStatus = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&hashObjSize, sizeof(DWORD), &dataLen, 0);
            if (!BCRYPT_SUCCESS(bStatus))
            {
                BCryptCloseAlgorithmProvider(hAlg, 0);
                CloseHandle(h);
                return SaveDisposition::Conflict;
            }

            std::vector<BYTE> hashObj(hashObjSize);
            BCRYPT_HASH_HANDLE hHash = nullptr;
            bStatus = BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0);
            if (!BCRYPT_SUCCESS(bStatus))
            {
                BCryptCloseAlgorithmProvider(hAlg, 0);
                CloseHandle(h);
                return SaveDisposition::Conflict;
            }

            constexpr DWORD BUFFER_SIZE = 64 * 1024;
            std::vector<BYTE> buffer(BUFFER_SIZE);
            DWORD bytesRead = 0;
            SetFilePointer(h, 0, nullptr, FILE_BEGIN);
            while (ReadFile(h, buffer.data(), BUFFER_SIZE, &bytesRead, nullptr) && bytesRead > 0)
            {
                BCryptHashData(hHash, buffer.data(), bytesRead, 0);
            }

            BYTE hashBytes[32];
            BCryptFinishHash(hHash, hashBytes, 32, 0);
            BCryptDestroyHash(hHash);
            BCryptCloseAlgorithmProvider(hAlg, 0);

            std::ostringstream ss;
            for (int i = 0; i < 32; ++i)
            {
                ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hashBytes[i]);
            }
            std::string computedHash = ss.str();
            if (computedHash != att.sha256)
            {
                CloseHandle(h);
                return SaveDisposition::Conflict;
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
        try
        {
            Exec("BEGIN IMMEDIATE;"); began=true;
            {
                Statement p(db_, "SELECT closed FROM projects WHERE id=?1;");
                Bind(p.value,1,id);
                if (sqlite3_step(p.value)!=SQLITE_ROW || sqlite3_column_int(p.value,0)!=0)
                {
                    Exec("ROLLBACK;");return SaveDisposition::RejectedClosed;
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
                    { Exec("ROLLBACK;"); return SaveDisposition::Conflict; }
                }
                else
                {
                    // Pending retired only by a successful commit; identical replay is allowed.
                    if (!alreadyCommitted || storedProject!=id || storedBody!=body)
                    { Exec("ROLLBACK;"); return SaveDisposition::Conflict; }
                }
            }

            if (alreadyCommitted)
            {
                if (storedProject!=id || storedBody!=body)
                { Exec("ROLLBACK;");return SaveDisposition::Conflict; }

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
                    Exec("ROLLBACK;"); return SaveDisposition::Conflict;
                }
                duplicate=true;
            }
            else
            {
                // Validate exact staging identity, ownership, draft status, and manifest completeness
                Statement readyCount(db_, "SELECT count(*) FROM draft_attachments WHERE project_id=?1 AND status='READY';");
                Bind(readyCount.value, 1, id);
                if (sqlite3_step(readyCount.value) != SQLITE_ROW || sqlite3_column_int(readyCount.value, 0) != static_cast<int>(attachments.size()))
                {
                    Exec("ROLLBACK;"); return SaveDisposition::Conflict;
                }

                for (auto const& att : attachments)
                {
                    Statement checkDraft(db_, "SELECT file_size, sha256, staged_path, status FROM draft_attachments WHERE project_id=?1 AND attachment_id=?2;");
                    Bind(checkDraft.value, 1, id);
                    Bind(checkDraft.value, 2, att.id);
                    if (sqlite3_step(checkDraft.value) != SQLITE_ROW)
                    {
                        Exec("ROLLBACK;"); return SaveDisposition::Conflict;
                    }
                    if (Text(checkDraft.value, 3) != "READY" ||
                        static_cast<uint64_t>(sqlite3_column_int64(checkDraft.value, 0)) != att.fileSize ||
                        Text(checkDraft.value, 1) != att.sha256 ||
                        Text(checkDraft.value, 2) != att.storagePath)
                    {
                        Exec("ROLLBACK;"); return SaveDisposition::Conflict;
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
                for (auto& lockPtr : openLocks)
                {
                    FILE_BASIC_INFO basicInfo{};
                    if (GetFileInformationByHandleEx(lockPtr->handle, FileBasicInfo, &basicInfo, sizeof(basicInfo)))
                    {
                        basicInfo.FileAttributes |= FILE_ATTRIBUTE_READONLY;
                        SetFileInformationByHandle(lockPtr->handle, FileBasicInfo, &basicInfo, sizeof(basicInfo));
                    }
                    SetFileAttributesW(lockPtr->path.c_str(), FILE_ATTRIBUTE_READONLY);
                    DWORD attr = GetFileAttributesW(lockPtr->path.c_str());
                    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_READONLY))
                    {
                        Exec("ROLLBACK;");
                        return SaveDisposition::Conflict;
                    }
                    lockPtr->sealed = true;
                }
#endif
            }

            {
                Statement p(db_, "UPDATE projects SET draft='' WHERE id=?1;");
                Bind(p.value,1,id);Check(sqlite3_step(p.value),db_);
            }
            {
                Statement clrDraft(db_, "DELETE FROM draft_attachments WHERE project_id=?1;");
                Bind(clrDraft.value,1,id);Check(sqlite3_step(clrDraft.value),db_);
            }
            {
                Statement cleared(db_, "DELETE FROM pending_submissions WHERE project_id=?1 AND submission_id=?2;");
                Bind(cleared.value,1,id);Bind(cleared.value,2,submission);Check(sqlite3_step(cleared.value),db_);
            }
            Exec("COMMIT;");
        }
        catch (...)
        {
#ifdef _WIN32
            for (auto& lockPtr : openLocks)
            {
                if (lockPtr->sealed)
                {
                    SetFileAttributesW(lockPtr->path.c_str(), FILE_ATTRIBUTE_NORMAL);
                }
            }
#endif
            if (began) sqlite3_exec(db_,"ROLLBACK;",nullptr,nullptr,nullptr);
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

    void Storage::SaveDraftAttachment(std::string const& projectId, AttachmentRecord const& att)
    {
        std::lock_guard lock(mutex_);
        CheckWriteCapacity();
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
    }

    void Storage::RemoveDraftAttachment(std::string const& projectId, std::string const& attachmentId)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "DELETE FROM draft_attachments WHERE project_id=?1 AND attachment_id=?2;");
        Bind(q.value,1,projectId);
        Bind(q.value,2,attachmentId);
        Check(sqlite3_step(q.value),db_);
    }

    void Storage::ClearDraftAttachments(std::string const& projectId)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "DELETE FROM draft_attachments WHERE project_id=?1;");
        Bind(q.value,1,projectId);
        Check(sqlite3_step(q.value),db_);
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
                    throw std::runtime_error("Project not found");
                }
                currentProjectVersion = sqlite3_column_int(qVer.value, 0);
                currentProjectClosed = sqlite3_column_int(qVer.value, 1);
            }
            consistencyDigestInput += "proj:" + std::to_string(currentProjectVersion) + ":" + std::to_string(currentProjectClosed) + ";";

            {
                Statement qSubs(db_, "SELECT submission_id FROM pending_submissions WHERE project_id=?1 ORDER BY submission_id;");
                Bind(qSubs.value, 1, query.projectId);
                while (sqlite3_step(qSubs.value) == SQLITE_ROW)
                {
                    consistencyDigestInput += "sub:" + Text(qSubs.value, 0) + ";";
                }
            }

            {
                Statement qAtts(db_, "SELECT attachment_id, version, status FROM draft_attachments WHERE project_id=?1 AND (status='REJECTED' OR status='SOURCE_CHANGED' OR status='TIMED_OUT') ORDER BY rowid;");
                Bind(qAtts.value, 1, query.projectId);
                while (sqlite3_step(qAtts.value) == SQLITE_ROW)
                {
                    consistencyDigestInput += "att:" + Text(qAtts.value, 0) + ":" + std::to_string(sqlite3_column_int64(qAtts.value, 1)) + ":" + Text(qAtts.value, 2) + ";";
                }
            }

            std::string const consistencySignature = ComputeStringHashHex(consistencyDigestInput);

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
                    if (parts[3] != consistencySignature)
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
            Check(rc, db_);

            if (result.items.size() > query.pageSize)
            {
                result.items.pop_back();
                result.isComplete = false;
                result.continuationToken = "ac:v2:" + query.projectId + ":" +
                    consistencySignature + ":" + std::to_string(offset + query.pageSize);
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
