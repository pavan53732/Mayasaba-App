#include "Sha256Hex.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>

#include <array>
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
}
