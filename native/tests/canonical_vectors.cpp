// Gate I1 evidence: `mayasaba-jcs-1` canonical bytes against the registered vectors.
//
// The oracle is docs/contracts/schemas/v1/canonical-vectors.json itself, read at
// run time, not a transcription of it. A copied expectation proves only that the
// copy and the code agree; reading the registered file proves the implementation
// matches the contract that the registry manifest pins by digest.
//
// Every vector is executed. The summary counts accepted and rejected cases that
// actually ran, so a vector that stopped executing cannot be reported as a pass.

#include "../Mayasaba/CanonicalJson.h"
#include "../Mayasaba/Storage.h"

#include <bcrypt.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#define EXPECT(cond) \
    do { \
        if (!(cond)) { \
            throw std::runtime_error(std::string("Assertion failed: ") + #cond + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (0)

namespace
{
    // ----------------------------------------------------------------------
    // Minimal JSON reader for the vectors file only.
    //
    // The vectors are authored by hand and are small, so this deliberately
    // supports exactly what the file contains: objects, arrays, strings,
    // numbers, booleans and null. It is NOT a second canonicalizer and is not
    // used by production code -- using CanonicalJson to read the file that
    // tests CanonicalJson would make a parser bug able to hide itself.
    // ----------------------------------------------------------------------
    struct Json
    {
        enum class Kind { Null, Bool, Number, String, Array, Object } kind{ Kind::Null };
        bool boolean{ false };
        double number{ 0.0 };
        std::string text;   // String: raw bytes from the file
        std::string raw;    // Number: the literal text, needed for exact integers
        std::vector<Json> items;
        std::vector<std::pair<std::string, Json>> members;

        Json const* Find(std::string const& key) const
        {
            for (auto const& member : members)
                if (member.first == key) return &member.second;
            return nullptr;
        }
        bool Has(std::string const& key) const { return Find(key) != nullptr; }
        std::string String(std::string const& key) const
        {
            auto const* found = Find(key);
            return found && found->kind == Kind::String ? found->text : std::string{};
        }
    };

    class Reader
    {
    public:
        explicit Reader(std::string bytes) : bytes_(std::move(bytes)) {}

        Json Parse()
        {
            Json value = ParseValue();
            Skip();
            if (at_ != bytes_.size()) throw std::runtime_error("Trailing bytes in vectors file");
            return value;
        }

    private:
        void Skip()
        {
            while (at_ < bytes_.size())
            {
                char const c = bytes_[at_];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++at_;
                else break;
            }
        }

        char Peek()
        {
            Skip();
            if (at_ >= bytes_.size()) throw std::runtime_error("Unexpected end of vectors file");
            return bytes_[at_];
        }

        Json ParseValue()
        {
            char const c = Peek();
            if (c == '{') return ParseObject();
            if (c == '[') return ParseArray();
            if (c == '"') { Json v; v.kind = Json::Kind::String; v.text = ParseString(); return v; }
            if (c == 't') { Expect("true");  Json v; v.kind = Json::Kind::Bool; v.boolean = true;  return v; }
            if (c == 'f') { Expect("false"); Json v; v.kind = Json::Kind::Bool; v.boolean = false; return v; }
            if (c == 'n') { Expect("null");  return Json{}; }
            return ParseNumber();
        }

        void Expect(std::string_view word)
        {
            Skip();
            if (bytes_.compare(at_, word.size(), word) != 0)
                throw std::runtime_error("Expected literal in vectors file");
            at_ += word.size();
        }

        Json ParseObject()
        {
            Json value; value.kind = Json::Kind::Object;
            ++at_; // '{'
            if (Peek() == '}') { ++at_; return value; }
            for (;;)
            {
                std::string key = ParseString();
                if (Peek() != ':') throw std::runtime_error("Expected ':' in vectors file");
                ++at_;
                value.members.emplace_back(std::move(key), ParseValue());
                char const next = Peek();
                ++at_;
                if (next == ',') continue;
                if (next == '}') return value;
                throw std::runtime_error("Expected ',' or '}' in vectors file");
            }
        }

        Json ParseArray()
        {
            Json value; value.kind = Json::Kind::Array;
            ++at_; // '['
            if (Peek() == ']') { ++at_; return value; }
            for (;;)
            {
                value.items.push_back(ParseValue());
                char const next = Peek();
                ++at_;
                if (next == ',') continue;
                if (next == ']') return value;
                throw std::runtime_error("Expected ',' or ']' in vectors file");
            }
        }

        std::string ParseString()
        {
            if (Peek() != '"') throw std::runtime_error("Expected string in vectors file");
            ++at_;
            std::string out;
            for (;;)
            {
                if (at_ >= bytes_.size()) throw std::runtime_error("Unterminated string in vectors file");
                char const c = bytes_[at_++];
                if (c == '"') return out;
                if (c != '\\') { out.push_back(c); continue; }
                char const escape = bytes_[at_++];
                switch (escape)
                {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u':
                {
                    // The vectors contain non-ASCII in both literal UTF-8 and
                    // \u escapes; both must decode to the same bytes so the
                    // canonical comparison is about content, not spelling.
                    unsigned value = Hex4();
                    if (value >= 0xD800 && value <= 0xDBFF && at_ + 1 < bytes_.size() &&
                        bytes_[at_] == '\\' && bytes_[at_ + 1] == 'u')
                    {
                        at_ += 2;
                        unsigned const low = Hex4();
                        value = 0x10000 + ((value - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(out, value);
                    break;
                }
                default: throw std::runtime_error("Unknown escape in vectors file");
                }
            }
        }

        unsigned Hex4()
        {
            if (at_ + 4 > bytes_.size()) throw std::runtime_error("Truncated \\u escape");
            unsigned value = 0;
            for (int i = 0; i < 4; ++i)
            {
                char const c = bytes_[at_++];
                value <<= 4;
                if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
                else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
                else throw std::runtime_error("Bad \\u escape");
            }
            return value;
        }

        static void AppendUtf8(std::string& out, unsigned codePoint)
        {
            if (codePoint < 0x80) out.push_back(static_cast<char>(codePoint));
            else if (codePoint < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
            else if (codePoint < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
        }

        Json ParseNumber()
        {
            Skip();
            std::size_t const start = at_;
            if (at_ < bytes_.size() && (bytes_[at_] == '-' || bytes_[at_] == '+')) ++at_;
            while (at_ < bytes_.size() &&
                   ((bytes_[at_] >= '0' && bytes_[at_] <= '9') || bytes_[at_] == '.' ||
                    bytes_[at_] == 'e' || bytes_[at_] == 'E' || bytes_[at_] == '-' || bytes_[at_] == '+'))
                ++at_;
            Json value; value.kind = Json::Kind::Number;
            value.raw = bytes_.substr(start, at_ - start);
            value.number = std::stod(value.raw);
            return value;
        }

        std::string bytes_;
        std::size_t at_{0};
    };

    std::string ReadFile(std::filesystem::path const& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("Could not open " + path.string());
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    std::string Sha256Hex(std::string_view input)
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
            throw std::runtime_error("BCryptOpenAlgorithmProvider failed");

        DWORD objectSize = 0, written = 0;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PBYTE>(&objectSize),
                              sizeof(DWORD), &written, 0) != 0)
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("BCryptGetProperty failed");
        }

        std::vector<BYTE> object(objectSize);
        BCRYPT_HASH_HANDLE hash = nullptr;
        if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) != 0)
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("BCryptCreateHash failed");
        }

        BCryptHashData(hash, reinterpret_cast<PBYTE>(const_cast<char*>(input.data())),
                       static_cast<ULONG>(input.size()), 0);

        std::array<BYTE, 32> digest{};
        if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) != 0)
        {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("BCryptFinishHash failed");
        }
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);

        static char const* kHex = "0123456789abcdef";
        std::string hex;
        for (BYTE byte : digest)
        {
            hex.push_back(kHex[byte >> 4]);
            hex.push_back(kHex[byte & 0x0F]);
        }
        return hex;
    }

    // Re-serializes a parsed vector `value` into JSON text so it can be fed to
    // Canonicalize. This is the vector author's input spelling, not a canonical
    // form: key order is preserved as written, and a number keeps its literal.
    void WriteJson(std::string& out, Json const& value)
    {
        switch (value.kind)
        {
        case Json::Kind::Null:   out.append("null"); break;
        case Json::Kind::Bool:   out.append(value.boolean ? "true" : "false"); break;
        case Json::Kind::Number: out.append(value.raw); break;
        case Json::Kind::String:
        {
            out.push_back('"');
            for (char const c : value.text)
            {
                unsigned char const byte = static_cast<unsigned char>(c);
                if (c == '"') out.append("\\\"");
                else if (c == '\\') out.append("\\\\");
                else if (c == '\b') out.append("\\b");
                else if (c == '\f') out.append("\\f");
                else if (c == '\n') out.append("\\n");
                else if (c == '\r') out.append("\\r");
                else if (c == '\t') out.append("\\t");
                else if (byte < 0x20)
                {
                    static char const* kHex = "0123456789abcdef";
                    out.append("\\u00");
                    out.push_back(kHex[byte >> 4]);
                    out.push_back(kHex[byte & 0x0F]);
                }
                else out.push_back(c);
            }
            out.push_back('"');
            break;
        }
        case Json::Kind::Array:
        {
            out.push_back('[');
            for (std::size_t i = 0; i < value.items.size(); ++i)
            {
                if (i) out.push_back(',');
                WriteJson(out, value.items[i]);
            }
            out.push_back(']');
            break;
        }
        case Json::Kind::Object:
        {
            out.push_back('{');
            for (std::size_t i = 0; i < value.members.size(); ++i)
            {
                if (i) out.push_back(',');
                out.push_back('"');
                out.append(value.members[i].first);
                out.append("\":");
                WriteJson(out, value.members[i].second);
            }
            out.push_back('}');
            break;
        }
        }
    }

    std::string HexToBytes(std::string const& hex)
    {
        std::string bytes;
        for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        {
            auto const nibble = [](char c) -> int
            {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                throw std::runtime_error("Bad hex digit");
            };
            bytes.push_back(static_cast<char>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
        }
        return bytes;
    }
}

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;

    // The vectors path is resolved relative to this executable's repository
    // location so the run is not sensitive to the working directory, and can be
    // overridden so the same binary can be pointed at a fixture.
    fs::path vectorsPath;
    if (argc > 1)
    {
        vectorsPath = argv[1];
    }
    else
    {
        fs::path here = fs::absolute(argv[0]).parent_path();
        vectorsPath = here / ".." / ".." / ".." / "docs" / "contracts" / "schemas" / "v1" / "canonical-vectors.json";
        if (!fs::exists(vectorsPath)) vectorsPath = "docs/contracts/schemas/v1/canonical-vectors.json";
    }

    std::cout << "canonical vectors: " << fs::absolute(vectorsPath).string() << "\n";

    int executedAccepted = 0;
    int executedRejected = 0;
    std::vector<std::string> failures;

    try
    {
        Json const root = Reader(ReadFile(vectorsPath)).Parse();

        EXPECT(root.String("canonical_profile") == "mayasaba-jcs-1");

        // ------------------------------------------------------------------
        // Accepted vectors: canonical bytes must match exactly, and the
        // registered SHA-256 of those bytes must match too.
        // ------------------------------------------------------------------
        auto const* accepted = root.Find("accepted");
        EXPECT(accepted != nullptr && accepted->kind == Json::Kind::Array);
        for (auto const& vector : accepted->items)
        {
            std::string const id = vector.String("id");
            std::string input;
            auto const* value = vector.Find("value");
            EXPECT(value != nullptr);
            WriteJson(input, *value);

            std::string const expectedBytes = vector.String("canonical_utf8");
            std::string const expectedDigest = vector.String("sha256");

            auto const result = mayasaba::canonical::Canonicalize(input);
            if (!result.ok())
            {
                failures.push_back(id + ": refused with " +
                    mayasaba::canonical::ErrorCodeText(result.error) + " for input " + input);
                continue;
            }
            if (result.bytes != expectedBytes)
            {
                failures.push_back(id + ": canonical bytes differ; expected [" + expectedBytes +
                    "] got [" + result.bytes + "]");
                continue;
            }
            std::string const digest = Sha256Hex(result.bytes);
            if (digest != expectedDigest)
            {
                failures.push_back(id + ": sha256 differs; expected " + expectedDigest + " got " + digest);
                continue;
            }
            ++executedAccepted;
        }

        // ------------------------------------------------------------------
        // Rejected vectors: the reported error code must be the one the
        // registry names for that input.
        // ------------------------------------------------------------------
        auto const* rejected = root.Find("rejected");
        EXPECT(rejected != nullptr && rejected->kind == Json::Kind::Array);
        for (auto const& vector : rejected->items)
        {
            std::string const id = vector.String("id");
            std::string const expectedCode = vector.String("error_code");
            EXPECT(!expectedCode.empty());

            // A vector is expressed either as JSON text or as raw bytes, and the
            // raw-byte form exists precisely for inputs that are not valid JSON.
            std::string input;
            if (auto const* hex = vector.Find("input_hex"))
                input = HexToBytes(hex->text);
            else if (auto const* text = vector.Find("json_text"))
                input = text->text;

            // Schema-level rejections are not canonicalization failures: the
            // input canonicalizes fine and the field fails its schema. Those are
            // proved separately below, against the same vector text.
            std::string const schemaRef = vector.String("schema_ref");
            if (!schemaRef.empty())
            {
                // The vector's `json_text` is the field value as it would appear
                // in a document, so it must parse and canonicalize, and then fail
                // the named schema. `unsafe-numeric-counter` is a bare JSON number
                // and fails because the schema requires a string at all; the
                // other three are strings that fail the pattern.
                auto const result = mayasaba::canonical::Canonicalize(input);
                if (!result.ok())
                {
                    failures.push_back(id + ": expected a schema failure but canonicalization refused with " +
                        mayasaba::canonical::ErrorCodeText(result.error));
                    continue;
                }

                // Every schema def named here is `type: string` plus a pattern, so
                // a non-string value is already invalid. The vectors' string
                // contents are all plain ASCII with no escapes, which is asserted
                // rather than assumed so a future vector cannot slip past.
                bool schemaValid = false;
                if (result.isString)
                {
                    EXPECT(result.bytes.size() >= 2 && result.bytes.front() == '"' && result.bytes.back() == '"');
                    std::string const content = result.bytes.substr(1, result.bytes.size() - 2);
                    EXPECT(content.find('\\') == std::string::npos);

                    if (schemaRef.find("CanonicalUnsignedInteger") != std::string::npos)
                        schemaValid = mayasaba::canonical::IsCanonicalUnsignedInteger(content);
                    else if (schemaRef.find("Uuid") != std::string::npos)
                        schemaValid = mayasaba::canonical::IsCanonicalUuid(content);
                    else if (schemaRef.find("UtcTimestamp") != std::string::npos)
                        schemaValid = mayasaba::canonical::IsCanonicalUtcTimestamp(content);
                    else
                    {
                        failures.push_back(id + ": unhandled schema_ref " + schemaRef);
                        continue;
                    }
                }
                else if (schemaRef.find("CanonicalUnsignedInteger") == std::string::npos &&
                         schemaRef.find("Uuid") == std::string::npos &&
                         schemaRef.find("UtcTimestamp") == std::string::npos)
                {
                    failures.push_back(id + ": unhandled schema_ref " + schemaRef);
                    continue;
                }

                if (schemaValid)
                {
                    failures.push_back(id + ": schema validation accepted a value the registry rejects as " +
                        expectedCode);
                    continue;
                }
                ++executedRejected;
                continue;
            }

            auto const result = mayasaba::canonical::Canonicalize(input);
            if (result.ok())
            {
                failures.push_back(id + ": expected " + expectedCode + " but canonicalization succeeded with [" +
                    result.bytes + "]");
                continue;
            }
            std::string const actual = mayasaba::canonical::ErrorCodeText(result.error);
            if (actual != expectedCode)
            {
                failures.push_back(id + ": expected " + expectedCode + " got " + actual);
                continue;
            }
            ++executedRejected;
        }

        // ------------------------------------------------------------------
        // Bounds. A bound that is not tested at its boundary is decoration, so
        // each C09 limit is proved to accept the largest permitted input and
        // refuse one more.
        // ------------------------------------------------------------------
        {
            namespace canonical = mayasaba::canonical;

            // Depth: a chain of nested arrays at exactly maxDepth parses; one more is refused.
            auto nest = [](std::size_t depth)
            {
                return std::string(depth, '[') + "1" + std::string(depth, ']');
            };
            canonical::Limits limits;
            limits.maxDepth = 4;
            EXPECT(canonical::Canonicalize(nest(4), limits).ok());
            auto const tooDeep = canonical::Canonicalize(nest(5), limits);
            EXPECT(!tooDeep.ok());
            EXPECT(tooDeep.error == canonical::Error::ParserLimitExceeded);

            // Collection members: exactly maxCollectionMembers is accepted.
            canonical::Limits members;
            members.maxCollectionMembers = 3;
            EXPECT(canonical::Canonicalize("[1,2,3]", members).ok());
            auto const tooMany = canonical::Canonicalize("[1,2,3,4]", members);
            EXPECT(!tooMany.ok());
            EXPECT(tooMany.error == canonical::Error::ParserLimitExceeded);

            // Bytes: exactly maxBytes is accepted.
            canonical::Limits bytes;
            bytes.maxBytes = 8;
            EXPECT(canonical::Canonicalize("\"abcdef\"", bytes).ok());   // 8 bytes
            auto const tooBig = canonical::Canonicalize("\"abcdefg\"", bytes);
            EXPECT(!tooBig.ok());
            EXPECT(tooBig.error == canonical::Error::ParserLimitExceeded);

            // String bytes: exactly maxStringBytes is accepted.
            canonical::Limits stringBytes;
            stringBytes.maxStringBytes = 4;
            EXPECT(canonical::Canonicalize("\"abcd\"", stringBytes).ok());
            auto const tooLong = canonical::Canonicalize("\"abcde\"", stringBytes);
            EXPECT(!tooLong.ok());
            EXPECT(tooLong.error == canonical::Error::ParserLimitExceeded);
        }

        // ------------------------------------------------------------------
        // Ordering and identity properties the vectors do not cover directly.
        // ------------------------------------------------------------------
        {
            namespace canonical = mayasaba::canonical;

            // Canonicalization is idempotent: canonical bytes are already canonical.
            auto const once = canonical::Canonicalize("{\"z\":1,\"a\":{\"y\":2,\"b\":3}}");
            EXPECT(once.ok());
            auto const twice = canonical::Canonicalize(once.bytes);
            EXPECT(twice.ok());
            EXPECT(twice.bytes == once.bytes);

            // Key order in the input does not change the canonical bytes, which is
            // the property the digest rests on.
            auto const forward = canonical::Canonicalize("{\"a\":1,\"b\":2}");
            auto const reverse = canonical::Canonicalize("{\"b\":2,\"a\":1}");
            EXPECT(forward.ok() && reverse.ok());
            EXPECT(forward.bytes == reverse.bytes);

            // Nested objects are sorted too, not only the top level.
            auto const nested = canonical::Canonicalize("{\"a\":{\"z\":1,\"b\":2}}");
            EXPECT(nested.ok());
            EXPECT(nested.bytes == "{\"a\":{\"b\":2,\"z\":1}}");

            // Array order is significant and must be preserved.
            auto const array = canonical::Canonicalize("[3,1,2]");
            EXPECT(array.ok());
            EXPECT(array.bytes == "[3,1,2]");

            // The empty string canonicalizes to two quote bytes, and the result
            // is distinguishable from a refusal by ok() alone.
            auto const emptyString = canonical::Canonicalize("\"\"");
            EXPECT(emptyString.ok());
            EXPECT(emptyString.bytes == "\"\"");
            EXPECT(emptyString.isString);
        }
    }
    catch (std::exception const& e)
    {
        std::cout << "FAIL: " << e.what() << "\n";
        return 1;
    }

    if (!failures.empty())
    {
        for (auto const& failure : failures) std::cout << "FAIL: " << failure << "\n";
        std::cout << "FAIL: " << failures.size() << " canonical vector(s) did not match\n";
        return 1;
    }

    std::cout << "PASS: " << executedAccepted << " accepted and " << executedRejected
              << " rejected canonical vectors, plus bounds and ordering properties\n";
    return 0;
}
