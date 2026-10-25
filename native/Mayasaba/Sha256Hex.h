#pragma once
// Lowercase hex SHA-256 over a byte range, for authoritative digests.
//
// One implementation, because a digest is only authoritative if every producer
// computes it identically. Returns `std::nullopt` when BCrypt fails rather than
// emitting a partial digest: a caller that cannot hash must fail closed, since a
// wrong digest presented as a right one is worse than no digest at all.
//
// This is deliberately not a general hashing API. The profile hashes canonical
// UTF-8 bytes (see CanonicalJson.h) or managed blob bytes, and nothing else.

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mayasaba
{
    std::optional<std::string> Sha256Hex(std::string_view input);
    std::optional<std::string> Sha256HexFile(std::filesystem::path const& path,
        std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());

    // Test hook for fault injection: if set > 0, Sha256HexFile will abort with std::nullopt
    // after hashing at most this many bytes, simulating a partial/interrupted read failure.
    void SetSha256FileFaultInjectionMaxBytesForTesting(std::size_t maxBytes);
}

