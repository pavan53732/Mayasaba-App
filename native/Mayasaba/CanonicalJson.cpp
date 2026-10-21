#include "CanonicalJson.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mayasaba::canonical
{
    namespace
    {
        // ------------------------------------------------------------------
        // Stage 1: raw UTF-8
        // ------------------------------------------------------------------

        // Strict UTF-8 validation. Overlong encodings, encoded surrogates,
        // anything above U+10FFFF and truncated sequences are all invalid: the
        // profile hashes exact bytes, so a permissive decoder that silently
        // repaired input would give two different byte sequences one hash.
        //
        // Returns the offset of the first invalid byte, or npos when valid.
        std::size_t FirstInvalidUtf8Offset(std::string_view bytes) noexcept
        {
            std::size_t i = 0;
            auto const n = bytes.size();
            auto const byte = [&](std::size_t index) { return static_cast<unsigned char>(bytes[index]); };

            while (i < n)
            {
                unsigned char const lead = byte(i);
                if (lead < 0x80) { ++i; continue; }

                std::size_t length = 0;
                std::uint32_t codePoint = 0;
                std::uint32_t minimum = 0;
                if ((lead & 0xE0) == 0xC0)      { length = 2; codePoint = lead & 0x1F; minimum = 0x80; }
                else if ((lead & 0xF0) == 0xE0) { length = 3; codePoint = lead & 0x0F; minimum = 0x800; }
                else if ((lead & 0xF8) == 0xF0) { length = 4; codePoint = lead & 0x07; minimum = 0x10000; }
                else return i; // continuation byte as lead, or 0xF8-0xFF

                if (i + length > n) return i;
                for (std::size_t k = 1; k < length; ++k)
                {
                    unsigned char const continuation = byte(i + k);
                    if ((continuation & 0xC0) != 0x80) return i;
                    codePoint = (codePoint << 6) | (continuation & 0x3F);
                }
                if (codePoint < minimum) return i;                       // overlong
                if (codePoint > 0x10FFFF) return i;                      // beyond Unicode
                if (codePoint >= 0xD800 && codePoint <= 0xDFFF) return i; // encoded surrogate
                i += length;
            }
            return std::string_view::npos;
        }

        // ------------------------------------------------------------------
        // UTF-8 <-> UTF-16 helpers
        // ------------------------------------------------------------------

        // Decodes one code point from valid UTF-8, advancing `i`.
        std::uint32_t DecodeOne(std::string_view bytes, std::size_t& i) noexcept
        {
            unsigned char const lead = static_cast<unsigned char>(bytes[i]);
            if (lead < 0x80) { ++i; return lead; }
            std::size_t length = 2;
            std::uint32_t codePoint = lead & 0x1F;
            if ((lead & 0xF0) == 0xE0) { length = 3; codePoint = lead & 0x0F; }
            else if ((lead & 0xF8) == 0xF0) { length = 4; codePoint = lead & 0x07; }
            for (std::size_t k = 1; k < length; ++k)
                codePoint = (codePoint << 6) | (static_cast<unsigned char>(bytes[i + k]) & 0x3F);
            i += length;
            return codePoint;
        }

        void AppendUtf8(std::string& out, std::uint32_t codePoint)
        {
            if (codePoint < 0x80)
            {
                out.push_back(static_cast<char>(codePoint));
            }
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

        // JCS orders object members by UTF-16 code units, not by code point.
        // The two disagree exactly where a supplementary character meets a
        // BMP character at or above U+E000: U+1F600 is a larger code point than
        // U+E000 but its first UTF-16 unit (0xD83D) is smaller. The
        // `utf16-property-order` vector is that case.
        std::u16string ToUtf16(std::string_view utf8)
        {
            std::u16string units;
            units.reserve(utf8.size());
            std::size_t i = 0;
            while (i < utf8.size())
            {
                std::uint32_t const codePoint = DecodeOne(utf8, i);
                if (codePoint < 0x10000)
                {
                    units.push_back(static_cast<char16_t>(codePoint));
                }
                else
                {
                    std::uint32_t const adjusted = codePoint - 0x10000;
                    units.push_back(static_cast<char16_t>(0xD800 + (adjusted >> 10)));
                    units.push_back(static_cast<char16_t>(0xDC00 + (adjusted & 0x3FF)));
                }
            }
            return units;
        }

        // ------------------------------------------------------------------
        // Stage 3: ECMAScript number rendering
        // ------------------------------------------------------------------

        // `Number::toString` for a finite non-zero double, which is what RFC 8785
        // requires. `std::to_chars` supplies the shortest round-tripping digit
        // string; the ES layout rules then decide where the decimal point and
        // exponent go. Rendering the digits ourselves is the point: the vectors
        // pin 1e-7 as `1e-7` (no zero padding), 1e20 as 21 digits and 1e21 in
        // exponent form, and no printf format produces all three.
        std::string FormatEcmaNumber(double value)
        {
            // +0 and -0 both render as "0"; negative zero is refused earlier, so
            // reaching here with zero means positive zero.
            if (value == 0.0) return "0";

            std::array<char, 64> buffer{};
            auto const converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                                value, std::chars_format::scientific);
            // `to_chars` cannot fail for a finite double into a 64-byte buffer, but
            // a silently wrong answer here would be an authoritative hash of the
            // wrong bytes, so it is treated as fatal rather than ignored.
            if (converted.ec != std::errc{})
                throw std::runtime_error("Canonical number rendering failed");

            std::string_view const text(buffer.data(), static_cast<std::size_t>(converted.ptr - buffer.data()));
            auto const exponentAt = text.find('e');
            std::string_view const mantissa = text.substr(0, exponentAt);

            std::string digits;
            for (char const c : mantissa)
                if (c >= '0' && c <= '9') digits.push_back(c);

            int exponent = 0;
            {
                auto const digitsStart = exponentAt + 1;
                bool negative = false;
                std::size_t at = digitsStart;
                if (at < text.size() && (text[at] == '+' || text[at] == '-'))
                {
                    negative = text[at] == '-';
                    ++at;
                }
                for (; at < text.size(); ++at)
                    exponent = exponent * 10 + (text[at] - '0');
                if (negative) exponent = -exponent;
            }

            // Trailing zeros carry no information once the exponent is explicit.
            while (digits.size() > 1 && digits.back() == '0') digits.pop_back();

            auto const k = static_cast<int>(digits.size());
            int const n = exponent + 1; // digits[0] . digits[1..] x 10^exponent

            std::string out;
            if (k <= n && n <= 21)
            {
                out = digits;
                out.append(static_cast<std::size_t>(n - k), '0');
            }
            else if (0 < n && n <= 21)
            {
                out = digits.substr(0, static_cast<std::size_t>(n));
                out.push_back('.');
                out.append(digits.substr(static_cast<std::size_t>(n)));
            }
            else if (-6 < n && n <= 0)
            {
                out = "0.";
                out.append(static_cast<std::size_t>(-n), '0');
                out.append(digits);
            }
            else
            {
                if (k == 1)
                {
                    out = digits;
                }
                else
                {
                    out = digits.substr(0, 1);
                    out.push_back('.');
                    out.append(digits.substr(1));
                }
                out.push_back('e');
                out.push_back(n - 1 >= 0 ? '+' : '-');
                out.append(std::to_string(std::abs(n - 1)));
            }
            return out;
        }

        // ------------------------------------------------------------------
        // Parsed value
        // ------------------------------------------------------------------

        struct Member
        {
            std::string key;   // decoded UTF-8
            std::string canonicalValue;
        };

        struct Value
        {
            enum class Kind { Null, True, False, Number, String, Array, Object } kind{ Kind::Null };
            std::string text;             // Number: canonical digits; String: decoded UTF-8
            std::vector<std::string> items;    // Array
            std::vector<Member> members;       // Object
        };

        // ------------------------------------------------------------------
        // Stage 2: JSON parse
        // ------------------------------------------------------------------

        class Parser
        {
        public:
            Parser(std::string_view input, Limits const& limits) : input_(input), limits_(limits) {}

            bool Parse(Value& out)
            {
                SkipWhitespace();
                if (!ParseValue(out, 0)) return false;
                SkipWhitespace();
                if (position_ != input_.size()) return Fail(Error::MalformedJson);
                return true;
            }

            Error error() const noexcept { return error_; }

        private:
            bool Fail(Error error) noexcept
            {
                // The first failure is the one reported: a later stage cannot run
                // on input an earlier one refused, and the vectors name one code.
                if (error_ == Error::None) error_ = error;
                return false;
            }

            void SkipWhitespace() noexcept
            {
                while (position_ < input_.size())
                {
                    char const c = input_[position_];
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++position_;
                    else break;
                }
            }

            bool Consume(char expected) noexcept
            {
                if (position_ < input_.size() && input_[position_] == expected) { ++position_; return true; }
                return false;
            }

            bool Literal(std::string_view word)
            {
                if (input_.compare(position_, word.size(), word) != 0) return Fail(Error::MalformedJson);
                position_ += word.size();
                return true;
            }

            bool ParseValue(Value& out, std::size_t depth)
            {
                if (depth > limits_.maxDepth) return Fail(Error::ParserLimitExceeded);
                if (position_ >= input_.size()) return Fail(Error::MalformedJson);

                switch (input_[position_])
                {
                case '{': return ParseObject(out, depth);
                case '[': return ParseArray(out, depth);
                case '"':
                {
                    out.kind = Value::Kind::String;
                    return ParseString(out.text);
                }
                case 't': out.kind = Value::Kind::True;  return Literal("true");
                case 'f': out.kind = Value::Kind::False; return Literal("false");
                case 'n': out.kind = Value::Kind::Null;  return Literal("null");
                default: break;
                }

                // NaN and Infinity are not JSON numbers, and a strict grammar
                // would report them as a malformed document. The registry names
                // them NON_I_JSON_NUMBER, so they are recognised here, before the
                // grammar runs, and reported as the non-I-JSON value they are.
                if (input_.compare(position_, 3, "NaN") == 0 ||
                    input_.compare(position_, 8, "Infinity") == 0 ||
                    input_.compare(position_, 9, "-Infinity") == 0)
                {
                    return Fail(Error::NonIJsonNumber);
                }
                return ParseNumber(out);
            }

            bool ParseObject(Value& out, std::size_t depth)
            {
                out.kind = Value::Kind::Object;
                ++position_; // '{'
                SkipWhitespace();
                if (Consume('}')) return true;

                for (;;)
                {
                    if (out.members.size() >= limits_.maxCollectionMembers)
                        return Fail(Error::ParserLimitExceeded);

                    Member member;
                    if (position_ >= input_.size() || input_[position_] != '"')
                        return Fail(Error::MalformedJson);
                    if (!ParseString(member.key)) return false;

                    SkipWhitespace();
                    if (!Consume(':')) return Fail(Error::MalformedJson);
                    SkipWhitespace();

                    Value child;
                    if (!ParseValue(child, depth + 1)) return false;
                    member.canonicalValue = Serialize(child);

                    // Duplicate detection is on the decoded key, so "A" and
                    // "A" collide exactly as the vector requires.
                    for (auto const& existing : out.members)
                        if (existing.key == member.key) return Fail(Error::DuplicateKey);
                    out.members.push_back(std::move(member));

                    SkipWhitespace();
                    if (Consume(',')) { SkipWhitespace(); continue; }
                    if (Consume('}')) return true;
                    return Fail(Error::MalformedJson);
                }
            }

            bool ParseArray(Value& out, std::size_t depth)
            {
                out.kind = Value::Kind::Array;
                ++position_; // '['
                SkipWhitespace();
                if (Consume(']')) return true;

                for (;;)
                {
                    if (out.items.size() >= limits_.maxCollectionMembers)
                        return Fail(Error::ParserLimitExceeded);

                    Value child;
                    if (!ParseValue(child, depth + 1)) return false;
                    out.items.push_back(Serialize(child));

                    SkipWhitespace();
                    if (Consume(',')) { SkipWhitespace(); continue; }
                    if (Consume(']')) return true;
                    return Fail(Error::MalformedJson);
                }
            }

            bool ParseString(std::string& out)
            {
                if (!Consume('"')) return Fail(Error::MalformedJson);
                for (;;)
                {
                    if (position_ >= input_.size()) return Fail(Error::MalformedJson);
                    unsigned char const c = static_cast<unsigned char>(input_[position_]);

                    if (c == '"') { ++position_; return true; }
                    if (c < 0x20) return Fail(Error::MalformedJson); // raw control in a string

                    if (c != '\\')
                    {
                        // Copy the whole UTF-8 sequence. Validity was established
                        // for the entire input before parsing began.
                        std::size_t start = position_;
                        DecodeOne(input_, position_);
                        out.append(input_.substr(start, position_ - start));
                    }
                    else
                    {
                        ++position_;
                        if (position_ >= input_.size()) return Fail(Error::MalformedJson);
                        char const escape = input_[position_++];
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
                            std::uint32_t codePoint = 0;
                            if (!ParseHex4(codePoint)) return false;

                            if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
                            {
                                // A high surrogate must be followed by an escaped
                                // low surrogate. A lone one is invalid Unicode, not
                                // a malformed document: the `unpaired-surrogate`
                                // vector names INVALID_UNICODE.
                                if (position_ + 1 >= input_.size() || input_[position_] != '\\' ||
                                    input_[position_ + 1] != 'u')
                                    return Fail(Error::InvalidUnicode);
                                position_ += 2;
                                std::uint32_t low = 0;
                                if (!ParseHex4(low)) return false;
                                if (low < 0xDC00 || low > 0xDFFF) return Fail(Error::InvalidUnicode);
                                codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                            }
                            else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF)
                            {
                                return Fail(Error::InvalidUnicode);
                            }
                            AppendUtf8(out, codePoint);
                            break;
                        }
                        default: return Fail(Error::MalformedJson);
                        }
                    }

                    if (out.size() > limits_.maxStringBytes) return Fail(Error::ParserLimitExceeded);
                }
            }

            bool ParseHex4(std::uint32_t& out)
            {
                if (position_ + 4 > input_.size()) return Fail(Error::MalformedJson);
                std::uint32_t value = 0;
                for (int i = 0; i < 4; ++i)
                {
                    char const c = input_[position_ + static_cast<std::size_t>(i)];
                    std::uint32_t digit = 0;
                    if (c >= '0' && c <= '9') digit = static_cast<std::uint32_t>(c - '0');
                    else if (c >= 'a' && c <= 'f') digit = static_cast<std::uint32_t>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') digit = static_cast<std::uint32_t>(c - 'A' + 10);
                    else return Fail(Error::MalformedJson);
                    value = (value << 4) | digit;
                }
                position_ += 4;
                out = value;
                return true;
            }

            bool ParseNumber(Value& out)
            {
                out.kind = Value::Kind::Number;
                std::size_t const start = position_;

                // JSON grammar. NaN and Infinity are not numbers in JSON at all;
                // they are named separately because the vectors do.
                if (position_ < input_.size() && input_[position_] == '-') ++position_;
                if (position_ >= input_.size()) return Fail(Error::MalformedJson);

                if (input_[position_] == '0')
                {
                    ++position_;
                }
                else if (input_[position_] >= '1' && input_[position_] <= '9')
                {
                    while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
                }
                else
                {
                    return Fail(Error::MalformedJson);
                }

                if (position_ < input_.size() && input_[position_] == '.')
                {
                    ++position_;
                    std::size_t const fractionStart = position_;
                    while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
                    if (position_ == fractionStart) return Fail(Error::MalformedJson);
                }

                if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E'))
                {
                    ++position_;
                    if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
                    std::size_t const exponentStart = position_;
                    while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
                    if (position_ == exponentStart) return Fail(Error::MalformedJson);
                }

                std::string_view const text = input_.substr(start, position_ - start);

                double value = 0.0;
                auto const parsed = std::from_chars(text.data(), text.data() + text.size(), value);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                {
                    // Out of double range, or not representable. I-JSON has no
                    // encoding for it, so there is no canonical form to emit.
                    return Fail(Error::NonIJsonNumber);
                }

                // Negative zero survives parsing and is refused at canonicalization,
                // which is the stage the registry names for it.
                if (value == 0.0 && !text.empty() && text.front() == '-')
                    return Fail(Error::NegativeZero);

                out.text = FormatEcmaNumber(value);
                return true;
            }

        public:
            // Serializes a parsed child immediately, so the tree only ever holds
            // canonical fragments and an object's members are sorted once.
            // Public because Canonicalize serializes the root through the same
            // routine the children go through: one serializer means a child and
            // the root cannot disagree about key order or escaping.
            static std::string Serialize(Value const& value)
            {
                switch (value.kind)
                {
                case Value::Kind::Null:  return "null";
                case Value::Kind::True:  return "true";
                case Value::Kind::False: return "false";
                case Value::Kind::Number: return value.text;
                case Value::Kind::String:
                {
                    std::string out;
                    out.push_back('"');
                    AppendEscaped(out, value.text);
                    out.push_back('"');
                    return out;
                }
                case Value::Kind::Array:
                {
                    std::string out;
                    out.push_back('[');
                    for (std::size_t i = 0; i < value.items.size(); ++i)
                    {
                        if (i) out.push_back(',');
                        out.append(value.items[i]);
                    }
                    out.push_back(']');
                    return out;
                }
                case Value::Kind::Object:
                {
                    auto sorted = value.members;
                    std::sort(sorted.begin(), sorted.end(), [](Member const& a, Member const& b)
                    {
                        return ToUtf16(a.key) < ToUtf16(b.key);
                    });
                    std::string out;
                    out.push_back('{');
                    for (std::size_t i = 0; i < sorted.size(); ++i)
                    {
                        if (i) out.push_back(',');
                        out.push_back('"');
                        AppendEscaped(out, sorted[i].key);
                        out.append("\":");
                        out.append(sorted[i].canonicalValue);
                    }
                    out.push_back('}');
                    return out;
                }
                }
                return "null";
            }

            // Minimal escaping: only quote, reverse solidus and the control
            // characters below U+0020. Everything else is emitted as literal
            // UTF-8, which is why "é" stays one two-byte sequence and is never
            // normalized.
            static void AppendEscaped(std::string& out, std::string_view utf8)
            {
                static char const* kHex = "0123456789abcdef";
                for (char const c : utf8)
                {
                    unsigned char const byte = static_cast<unsigned char>(c);
                    switch (c)
                    {
                    case '"':  out.append("\\\""); break;
                    case '\\': out.append("\\\\"); break;
                    case '\b': out.append("\\b");  break;
                    case '\f': out.append("\\f");  break;
                    case '\n': out.append("\\n");  break;
                    case '\r': out.append("\\r");  break;
                    case '\t': out.append("\\t");  break;
                    default:
                        if (byte < 0x20)
                        {
                            out.append("\\u00");
                            out.push_back(kHex[byte >> 4]);
                            out.push_back(kHex[byte & 0x0F]);
                        }
                        else
                        {
                            out.push_back(c);
                        }
                        break;
                    }
                }
            }

        private:
            std::string_view input_;
            Limits limits_;
            std::size_t position_{0};
            Error error_{ Error::None };
        };
    }

    Result Canonicalize(std::string_view utf8Json, Limits const& limits)
    {
        Result result;

        if (utf8Json.size() > limits.maxBytes)
        {
            result.error = Error::ParserLimitExceeded;
            return result;
        }

        // A BOM is refused before anything else: it is valid UTF-8 and a
        // permissive parser would skip it, producing canonical bytes for input
        // that is not the bytes the profile admits.
        if (utf8Json.size() >= 3 &&
            static_cast<unsigned char>(utf8Json[0]) == 0xEF &&
            static_cast<unsigned char>(utf8Json[1]) == 0xBB &&
            static_cast<unsigned char>(utf8Json[2]) == 0xBF)
        {
            result.error = Error::Utf8BomForbidden;
            return result;
        }

        if (FirstInvalidUtf8Offset(utf8Json) != std::string_view::npos)
        {
            result.error = Error::InvalidUtf8;
            return result;
        }

        Parser parser(utf8Json, limits);
        Value root;
        if (!parser.Parse(root))
        {
            result.error = parser.error();
            return result;
        }

        result.bytes = Parser::Serialize(root);
        result.isString = root.kind == Value::Kind::String;
        return result;
    }

    char const* ErrorCodeText(Error error) noexcept
    {
        switch (error)
        {
        case Error::None:               return "NONE";
        case Error::Utf8BomForbidden:   return "UTF8_BOM_FORBIDDEN";
        case Error::InvalidUtf8:        return "INVALID_UTF8";
        case Error::DuplicateKey:       return "DUPLICATE_KEY";
        case Error::NonIJsonNumber:     return "NON_I_JSON_NUMBER";
        case Error::NegativeZero:       return "NEGATIVE_ZERO";
        case Error::InvalidUnicode:     return "INVALID_UNICODE";
        case Error::MalformedJson:      return "SCHEMA_INVALID";
        case Error::ParserLimitExceeded: return "PARSER_LIMIT_EXCEEDED";
        }
        return "INTERNAL_ERROR";
    }

    bool IsCanonicalUuid(std::string_view value) noexcept
    {
        if (value.size() != 36) return false;
        for (std::size_t i = 0; i < value.size(); ++i)
        {
            if (i == 8 || i == 13 || i == 18 || i == 23)
            {
                if (value[i] != '-') return false;
                continue;
            }
            char const c = value[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        }
        // RFC 4122 version 1-5 in the version nibble, variant 8/9/a/b in the
        // variant nibble. The registry's pattern is exactly this, and
        // NativeIds.h's IsUuid is deliberately looser.
        char const version = value[14];
        if (version < '1' || version > '5') return false;
        char const variant = value[19];
        return variant == '8' || variant == '9' || variant == 'a' || variant == 'b';
    }

    bool IsCanonicalUtcTimestamp(std::string_view value) noexcept
    {
        // YYYY-MM-DDTHH:MM:SS.mmmZ, exactly. Milliseconds are mandatory: the
        // `timestamp-without-milliseconds` vector is a real ISO timestamp that
        // the profile still refuses.
        if (value.size() != 24) return false;
        auto const digits = [&](std::size_t at, std::size_t count)
        {
            for (std::size_t i = 0; i < count; ++i)
                if (value[at + i] < '0' || value[at + i] > '9') return false;
            return true;
        };
        if (!digits(0, 4) || value[4] != '-' || !digits(5, 2) || value[7] != '-' || !digits(8, 2)) return false;
        if (value[10] != 'T') return false;
        if (!digits(11, 2) || value[13] != ':' || !digits(14, 2) || value[16] != ':' || !digits(17, 2)) return false;
        if (value[19] != '.' || !digits(20, 3) || value[23] != 'Z') return false;

        auto const number = [&](std::size_t at, std::size_t count)
        {
            int result = 0;
            for (std::size_t i = 0; i < count; ++i) result = result * 10 + (value[at + i] - '0');
            return result;
        };

        int const year = number(0, 4);
        int const month = number(5, 2);
        int const day = number(8, 2);
        int const hour = number(11, 2);
        int const minute = number(14, 2);
        int const second = number(17, 2);

        if (month < 1 || month > 12) return false;
        if (hour > 23 || minute > 59 || second > 59) return false;

        static int const kDaysPerMonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        bool const leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        int const limit = month == 2 && leap ? 29 : kDaysPerMonth[month - 1];
        return day >= 1 && day <= limit;
    }

    bool IsCanonicalUnsignedInteger(std::string_view value) noexcept
    {
        if (value.empty() || value.size() > 40) return false;
        if (value[0] == '0') return value.size() == 1; // no leading zeros
        for (char const c : value)
            if (c < '0' || c > '9') return false;
        return true;
    }
}
