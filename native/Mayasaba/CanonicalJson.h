#pragma once
// `mayasaba-jcs-1` canonical bytes.
//
// AGENTS.md §10 requires authoritative hashes to use the RFC 8785/JCS-compatible
// I-JSON/UTF-8 profile registered in C06/C07, with duplicate keys, invalid
// Unicode, NaN/infinity/negative zero and unsafe numeric encodings rejected and
// no silent Unicode normalization. This header is that profile's only
// implementation. Nothing here may be replaced by a general-purpose JSON
// library: the registry pins the exact canonical output, and the registered
// vectors in docs/contracts/schemas/v1/canonical-vectors.json are the oracle.
//
// The module is pure: no IO, no SQLite, no WinRT, no window state. It is
// therefore natively testable without creating a window, which is what lets the
// registered vectors run as ordinary unit cases.
//
// ORDER OF OPERATIONS, because the vectors distinguish them. A caller must not
// assume "canonicalize" implies "parse": `validation_target` in the registry
// names the stage that must refuse each rejected vector, and a BOM or invalid
// UTF-8 has to fail at the byte layer before any JSON is considered. Canonicalize
// performs all stages in order and reports the stage's own error code, so a
// vector's expected `error_code` is what comes back.
//
//   1. RAW_UTF8_PARSE          - BOM forbidden, strict UTF-8, byte bound
//   2. RAW_JSON_PARSE          - duplicate keys, NaN/Infinity, unpaired
//                                surrogates, malformed syntax, parser bounds
//   3. MAYASABA_JCS_1_CANONICALIZE - negative zero, ES number rendering,
//                                UTF-16 code-unit key order
//
// A profile this strict is deliberately unforgiving: an input it cannot
// canonicalize has no authoritative hash, and the caller must fail closed rather
// than hash a near-miss.

#include <cstddef>
#include <string>
#include <string_view>

namespace mayasaba::canonical
{
    // Mirrors the registry's `error_code` spellings exactly, so a test can
    // compare a vector's expected code against the returned one by name and a
    // log line is the same string the contract uses.
    enum class Error
    {
        None = 0,
        Utf8BomForbidden,
        InvalidUtf8,
        DuplicateKey,
        NonIJsonNumber,
        NegativeZero,
        InvalidUnicode,
        MalformedJson,
        ParserLimitExceeded,
    };

    // C09 parser maximums: 1 MiB envelope bytes, 64 nesting levels, 10,000
    // collection members and 64 KiB per ordinary string. They are parameters
    // rather than constants so a test can prove the bound is enforced at the
    // boundary instead of only somewhere beyond it.
    struct Limits
    {
        std::size_t maxBytes{ 1u << 20 };
        std::size_t maxDepth{ 64 };
        std::size_t maxCollectionMembers{ 10000 };
        std::size_t maxStringBytes{ 64u << 10 };
    };

    struct Result
    {
        Error error{ Error::None };
        // Canonical UTF-8 bytes. Empty only when `error` is not None: even the
        // canonical form of the JSON value `""` is two bytes, so an empty
        // `bytes` with `ok()` is not a state this can reach.
        std::string bytes;
        // The JSON value is a string. Recorded so a caller can tell a scalar
        // string from an object or array without re-parsing the bytes.
        bool isString{ false };

        bool ok() const noexcept { return error == Error::None; }
    };

    // Parses `utf8Json` and returns its canonical bytes. Every rejection is
    // reported rather than thrown: a canonicalization failure is an ordinary
    // boundary result that the caller must turn into a refusal, and throwing
    // would make it indistinguishable from a programming error.
    Result Canonicalize(std::string_view utf8Json, Limits const& limits = Limits{});

    // The registry spelling of an error code, e.g. "DUPLICATE_KEY". Stable
    // strings: they appear in receipts and in the acceptance log.
    char const* ErrorCodeText(Error error) noexcept;

    // True when `value` is a schema-valid `Uuid` per the v1 registry:
    // lowercase hex, RFC 4122 version 1-5 and variant 8/9/a/b. This is stricter
    // than NativeIds.h's IsUuid, which accepts any hex in those positions; the
    // canonical vectors' `noncanonical-uuid` case is exactly the difference.
    bool IsCanonicalUuid(std::string_view value) noexcept;

    // True when `value` is a schema-valid `UtcTimestamp`: `YYYY-MM-DDTHH:MM:SS.mmmZ`
    // with a real calendar date, including the leap-year rule. `invalid-calendar-date`
    // (2026-02-31) and `timestamp-without-milliseconds` are the vectors.
    bool IsCanonicalUtcTimestamp(std::string_view value) noexcept;

    // True when `value` is a schema-valid `CanonicalUnsignedInteger`:
    // `0|[1-9][0-9]*`, at most 40 characters. `unsafe-numeric-counter`
    // (9007199254740992) is the vector: it is a perfectly good integer and a
    // number a JSON parser would accept, which is why the registry rejects it as
    // SCHEMA_INVALID rather than as a parse error.
    bool IsCanonicalUnsignedInteger(std::string_view value) noexcept;
}
