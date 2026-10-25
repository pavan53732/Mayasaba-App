#include "Sha256Hex.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <atomic>
#include <fstream>
#include <vector>
#endif

namespace mayasaba
{
    std::optional<std::string> Sha256Hex(std::string_view input)
    {
#ifdef _WIN32
        if (input.size() > static_cast<std::size_t>(ULONG_MAX)) return std::nullopt;

        BCRYPT_ALG_HANDLE algorithm = nullptr;
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
            return std::nullopt;

        struct AlgorithmCloser
        {
            BCRYPT_ALG_HANDLE handle;
            ~AlgorithmCloser() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
        } algorithmCloser{ algorithm };

        DWORD objectSize = 0, written = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                             reinterpret_cast<PBYTE>(&objectSize), sizeof(DWORD), &written, 0)) ||
            objectSize == 0)
            return std::nullopt;

        std::vector<BYTE> object(objectSize);

        BCRYPT_HASH_HANDLE hash = nullptr;
        if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0)))
            return std::nullopt;

        struct HashCloser
        {
            BCRYPT_HASH_HANDLE handle;
            ~HashCloser() { if (handle) BCryptDestroyHash(handle); }
        } hashCloser{ hash };

        if (!BCRYPT_SUCCESS(BCryptHashData(hash,
                reinterpret_cast<PBYTE>(const_cast<char*>(input.data())),
                static_cast<ULONG>(input.size()), 0)))
            return std::nullopt;

        std::array<BYTE, 32> digest{};
        if (!BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0)))
            return std::nullopt;

        static char const* kHex = "0123456789abcdef";
        std::string hex;
        hex.reserve(digest.size() * 2);
        for (BYTE byte : digest)
        {
            hex.push_back(kHex[byte >> 4]);
            hex.push_back(kHex[byte & 0x0F]);
        }
        return hex;
#else
        (void)input;
        return std::nullopt;
#endif
    }

    static std::atomic<std::size_t> g_faultInjectionMaxBytes{0};

    void SetSha256FileFaultInjectionMaxBytesForTesting(std::size_t maxBytes)
    {
        g_faultInjectionMaxBytes.store(maxBytes, std::memory_order_relaxed);
    }

    std::optional<std::string> Sha256HexFile(std::filesystem::path const& path,
        std::chrono::steady_clock::time_point deadline)
    {
#ifdef _WIN32
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
            return std::nullopt;

        struct AlgorithmCloser
        {
            BCRYPT_ALG_HANDLE handle;
            ~AlgorithmCloser() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
        } algorithmCloser{ algorithm };

        DWORD objectSize = 0, written = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                             reinterpret_cast<PBYTE>(&objectSize), sizeof(DWORD), &written, 0)) ||
            objectSize == 0)
            return std::nullopt;

        std::vector<BYTE> object(objectSize);

        BCRYPT_HASH_HANDLE hash = nullptr;
        if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0)))
            return std::nullopt;

        struct HashCloser
        {
            BCRYPT_HASH_HANDLE handle;
            ~HashCloser() { if (handle) BCryptDestroyHash(handle); }
        } hashCloser{ hash };

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return std::nullopt;

        std::vector<char> buffer(65536);
        std::size_t totalBytesHashed = 0;
        while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0)
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return std::nullopt;

            auto const bytesRead = static_cast<ULONG>(file.gcount());
            auto const maxFault = g_faultInjectionMaxBytes.load(std::memory_order_relaxed);
            if (maxFault > 0 && (totalBytesHashed + bytesRead) > maxFault)
            {
                // Inject interrupted-read failure mid-stream
                return std::nullopt;
            }

            if (!BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PBYTE>(buffer.data()), bytesRead, 0)))
                return std::nullopt;

            totalBytesHashed += bytesRead;
        }

        if (file.bad()) return std::nullopt;

        std::array<BYTE, 32> digest{};
        if (!BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0)))
            return std::nullopt;

        static char const* kHex = "0123456789abcdef";
        std::string hex;
        hex.reserve(digest.size() * 2);
        for (BYTE byte : digest)
        {
            hex.push_back(kHex[byte >> 4]);
            hex.push_back(kHex[byte & 0x0F]);
        }
        return hex;
#else
        (void)path;
        (void)deadline;
        return std::nullopt;
#endif
    }
}
