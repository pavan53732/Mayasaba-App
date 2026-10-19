#include "AttachmentService.h"
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#ifdef _WIN32
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace mayasaba
{
    std::string AttachmentService::Utf16ToUtf8(std::wstring const& w)
    {
        if (w.empty()) return {};
        int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), size, nullptr, nullptr);
        return s;
    }

    std::wstring AttachmentService::Utf8ToUtf16(std::string const& u)
    {
        if (u.empty()) return {};
        int size = MultiByteToWideChar(CP_UTF8, 0, u.data(), static_cast<int>(u.size()), nullptr, 0);
        std::wstring w(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, u.data(), static_cast<int>(u.size()), w.data(), size);
        return w;
    }

    std::string AttachmentService::FormatFileSize(uint64_t bytes)
    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1);
        if (bytes < 1024ULL)
        {
            ss << bytes << " B";
        }
        else if (bytes < 1024ULL * 1024ULL)
        {
            ss << (static_cast<double>(bytes) / 1024.0) << " KB";
        }
        else if (bytes < 1024ULL * 1024ULL * 1024ULL)
        {
            ss << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
        }
        else
        {
            ss << (static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0)) << " GB";
        }
        return ss.str();
    }

    std::string AttachmentService::DetectFileType(std::wstring const& path)
    {
        std::filesystem::path p(path);
        auto ext = p.extension().wstring();
        for (auto& c : ext) c = towlower(c);

        if (ext == L".png") return "image/png";
        if (ext == L".jpg" || ext == L".jpeg") return "image/jpeg";
        if (ext == L".gif") return "image/gif";
        if (ext == L".webp") return "image/webp";
        if (ext == L".svg") return "image/svg+xml";
        if (ext == L".txt") return "text/plain";
        if (ext == L".md") return "text/markdown";
        if (ext == L".cpp" || ext == L".h" || ext == L".hpp" || ext == L".c") return "text/x-c";
        if (ext == L".py") return "text/x-python";
        if (ext == L".json") return "application/json";
        if (ext == L".pdf") return "application/pdf";
        if (ext == L".zip" || ext == L".tar" || ext == L".gz" || ext == L".7z") return "application/octet-stream";
        return "application/octet-stream";
    }

    std::string AttachmentService::ComputeFileSha256(std::wstring const& path, uint64_t* outSize)
    {
#ifdef _WIN32
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error("Cannot open file for SHA-256 calculation: " + Utf16ToUtf8(path));
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(hFile, &size))
        {
            CloseHandle(hFile);
            throw std::runtime_error("Cannot determine file size: " + Utf16ToUtf8(path));
        }
        if (outSize) *outSize = static_cast<uint64_t>(size.QuadPart);

        BCRYPT_ALG_HANDLE hAlg = nullptr;
        NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (!BCRYPT_SUCCESS(status))
        {
            CloseHandle(hFile);
            throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
        }

        DWORD hashObjSize = 0, dataLen = 0;
        status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&hashObjSize, sizeof(DWORD), &dataLen, 0);
        if (!BCRYPT_SUCCESS(status))
        {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            CloseHandle(hFile);
            throw std::runtime_error("BCryptGetProperty failed");
        }

        std::vector<BYTE> hashObj(hashObjSize);
        BCRYPT_HASH_HANDLE hHash = nullptr;
        status = BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0);
        if (!BCRYPT_SUCCESS(status))
        {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            CloseHandle(hFile);
            throw std::runtime_error("BCryptCreateHash failed");
        }

        constexpr DWORD BUFFER_SIZE = 64 * 1024;
        std::vector<BYTE> buffer(BUFFER_SIZE);
        DWORD bytesRead = 0;
        while (ReadFile(hFile, buffer.data(), BUFFER_SIZE, &bytesRead, nullptr) && bytesRead > 0)
        {
            BCryptHashData(hHash, buffer.data(), bytesRead, 0);
        }

        BYTE hash[32];
        BCryptFinishHash(hHash, hash, 32, 0);
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        CloseHandle(hFile);

        std::ostringstream ss;
        for (int i = 0; i < 32; ++i)
        {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        return ss.str();
#else
        throw std::runtime_error("SHA-256 only implemented on Windows");
#endif
    }

    AttachmentService::AttachmentService(Storage& storage, std::filesystem::path stagingRoot)
        : storage_(storage)
    {
        if (stagingRoot.empty())
        {
            stagingRoot_ = storage_.FilePath().parent_path() / "attachments";
        }
        else
        {
            stagingRoot_ = std::move(stagingRoot);
        }
    }

    BatchValidationResult AttachmentService::ValidateCandidateBatch(
        std::vector<std::wstring> const& candidatePaths,
        std::vector<AttachmentCandidate> const& existingDraftAttachments) const
    {
        BatchValidationResult result;
        size_t const currentCount = existingDraftAttachments.size();
        size_t const newCount = candidatePaths.size();
        size_t const totalCount = currentCount + newCount;

        if (totalCount > MAX_ATTACHMENT_COUNT)
        {
            result.accepted = false;
            result.reason = ValidationFailureReason::TooManyFiles;
            result.errorMessage = "Cannot attach " + std::to_string(totalCount) +
                " files. A message can contain at most 50 attachments.";
            return result;
        }

        uint64_t existingBytes = 0;
        for (auto const& existing : existingDraftAttachments)
        {
            existingBytes += existing.fileSize;
        }

        uint64_t batchBytes = 0;
        for (auto const& pathStr : candidatePaths)
        {
            std::filesystem::path p(pathStr);
            std::string fileName = Utf16ToUtf8(p.filename().wstring());

            std::error_code ec;
            if (!std::filesystem::exists(p, ec) || std::filesystem::is_directory(p, ec))
            {
                result.accepted = false;
                result.reason = ValidationFailureReason::InaccessibleFile;
                result.errorMessage = "File does not exist or is inaccessible: " + fileName;
                result.validatedFiles.clear();
                return result;
            }

#ifdef _WIN32
            HANDLE hFile = CreateFileW(pathStr.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE)
            {
                result.accepted = false;
                result.reason = ValidationFailureReason::InaccessibleFile;
                result.errorMessage = "Cannot open file: " + fileName;
                result.validatedFiles.clear();
                return result;
            }

            LARGE_INTEGER fileSize{};
            if (!GetFileSizeEx(hFile, &fileSize))
            {
                CloseHandle(hFile);
                result.accepted = false;
                result.reason = ValidationFailureReason::InaccessibleFile;
                result.errorMessage = "Cannot query file size: " + fileName;
                result.validatedFiles.clear();
                return result;
            }

            uint64_t size = static_cast<uint64_t>(fileSize.QuadPart);
            if (size == 0)
            {
                CloseHandle(hFile);
                result.accepted = false;
                result.reason = ValidationFailureReason::EmptyFile;
                result.errorMessage = "File '" + fileName + "' is empty (0 bytes). Empty files cannot be attached.";
                result.validatedFiles.clear();
                return result;
            }

            if (size > MAX_FILE_SIZE_BYTES)
            {
                CloseHandle(hFile);
                result.accepted = false;
                result.reason = ValidationFailureReason::FileTooLarge;
                result.errorMessage = "File '" + fileName + "' (" + FormatFileSize(size) +
                    ") exceeds the 100 MiB limit per file.";
                result.validatedFiles.clear();
                return result;
            }

            batchBytes += size;
            if (existingBytes + batchBytes > MAX_AGGREGATE_SIZE_BYTES)
            {
                CloseHandle(hFile);
                result.accepted = false;
                result.reason = ValidationFailureReason::AggregateTooLarge;
                result.errorMessage = "Total attachment size (" + FormatFileSize(existingBytes + batchBytes) +
                    ") exceeds the 500 MiB aggregate limit.";
                result.validatedFiles.clear();
                return result;
            }

            AttachmentFileMetadata meta;
            meta.originalPath = pathStr;
            meta.fileName = fileName;
            meta.fileSize = size;
            meta.detectedType = DetectFileType(pathStr);
            GetFileTime(hFile, nullptr, nullptr, &meta.lastWriteTime);
            meta.hasFileIdInfo = (GetFileInformationByHandleEx(hFile, FileIdInfo, &meta.fileIdInfo, sizeof(meta.fileIdInfo)) != 0);

            CloseHandle(hFile);
            result.validatedFiles.push_back(std::move(meta));
#endif
        }

        result.accepted = true;
        result.reason = ValidationFailureReason::None;
        result.totalBytes = batchBytes;
        return result;
    }

    std::chrono::steady_clock::time_point AttachmentService::DefaultActiveClock()
    {
#ifdef _WIN32
        ULONGLONG unbiasedTime100ns = 0;
        // QueryUnbiasedInterruptTime counts in 100ns units and excludes time spent in sleep/hibernation
        if (QueryUnbiasedInterruptTime(&unbiasedTime100ns))
        {
            return std::chrono::steady_clock::time_point(
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::nanoseconds(unbiasedTime100ns * 100)));
        }
#endif
        return std::chrono::steady_clock::now();
    }

    std::vector<AttachmentCandidate> AttachmentService::PrepareBatch(
        std::string const& projectId,
        std::vector<std::wstring> const& candidatePaths,
        std::function<bool()> const& isCancelled,
        uint32_t timeoutSeconds,
        SteadyClockFn clockFn)
    {
        auto const batchStart = clockFn();
        auto const batchDeadline = batchStart + std::chrono::seconds(timeoutSeconds);

        std::vector<AttachmentCandidate> results;
        results.reserve(candidatePaths.size());

        for (auto const& path : candidatePaths)
        {
            if (isCancelled()) break;

            if (clockFn() >= batchDeadline)
            {
                AttachmentCandidate timedOutCand;
                timedOutCand.id = NewUuid();
                timedOutCand.originalPath = path;
                std::filesystem::path p(path);
                timedOutCand.fileName = Utf16ToUtf8(p.filename().wstring());
                timedOutCand.status = AttachmentStatus::TimedOut;
                timedOutCand.errorMessage = "Batch preparation exceeded the 120-second active timeout budget.";
                results.push_back(std::move(timedOutCand));
                continue;
            }

            auto cand = PrepareAttachment(projectId, path, isCancelled, batchDeadline, clockFn);
            results.push_back(std::move(cand));
        }
        return results;
    }

    AttachmentCandidate AttachmentService::PrepareAttachment(
        std::string const& projectId,
        std::wstring const& sourcePath,
        std::function<bool()> const& isCancelled,
        std::optional<std::chrono::steady_clock::time_point> deadline,
        SteadyClockFn clockFn)
    {
        AttachmentCandidate candidate;
        candidate.id = NewUuid();
        candidate.originalPath = sourcePath;
        std::filesystem::path srcP(sourcePath);
        candidate.fileName = Utf16ToUtf8(srcP.filename().wstring());
        candidate.detectedType = DetectFileType(sourcePath);
        candidate.status = AttachmentStatus::Preparing;

#ifdef _WIN32
        HANDLE hSource = CreateFileW(sourcePath.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hSource == INVALID_HANDLE_VALUE)
        {
            candidate.status = AttachmentStatus::Rejected;
            candidate.errorMessage = "Cannot open source file: " + candidate.fileName;
            return candidate;
        }

        LARGE_INTEGER initialSize{};
        if (!GetFileSizeEx(hSource, &initialSize))
        {
            CloseHandle(hSource);
            candidate.status = AttachmentStatus::Rejected;
            candidate.errorMessage = "Cannot read source file size: " + candidate.fileName;
            return candidate;
        }

        if (initialSize.QuadPart == 0)
        {
            CloseHandle(hSource);
            candidate.status = AttachmentStatus::Rejected;
            candidate.errorMessage = "File '" + candidate.fileName + "' is empty (0 bytes).";
            return candidate;
        }

        if (static_cast<uint64_t>(initialSize.QuadPart) > MAX_FILE_SIZE_BYTES)
        {
            CloseHandle(hSource);
            candidate.status = AttachmentStatus::Rejected;
            candidate.errorMessage = "File '" + candidate.fileName + "' exceeds the 100 MiB limit.";
            return candidate;
        }

        FILETIME initialLastWrite{};
        GetFileTime(hSource, nullptr, nullptr, &initialLastWrite);
        candidate.sourceLastWrite = initialLastWrite;

        FILE_ID_INFO initialFileId{};
        candidate.hasSourceFileId = (GetFileInformationByHandleEx(hSource, FileIdInfo, &initialFileId, sizeof(initialFileId)) != 0);
        if (candidate.hasSourceFileId)
        {
            candidate.sourceFileId = initialFileId;
        }

        auto stagingDir = stagingRoot_ / "staging" / projectId;
        std::error_code ec;
        std::filesystem::create_directories(stagingDir, ec);

        std::wstring stagedFileName = Utf8ToUtf16(candidate.id) + L"_" + srcP.filename().wstring();
        auto stagedFilePath = stagingDir / stagedFileName;
        candidate.stagedPath = stagedFilePath.wstring();
        candidate.fileSize = static_cast<uint64_t>(initialSize.QuadPart);

        // Persist durable preparation intent before copying bytes
        AttachmentRecord intentRec;
        intentRec.id = candidate.id;
        intentRec.projectId = projectId;
        intentRec.fileName = candidate.fileName;
        intentRec.fileSize = candidate.fileSize;
        intentRec.sha256 = "";
        intentRec.detectedType = candidate.detectedType;
        intentRec.storagePath = Utf16ToUtf8(candidate.stagedPath);
        intentRec.sourcePath = Utf16ToUtf8(candidate.originalPath);
        intentRec.status = "PREPARING";
        storage_.SaveDraftAttachment(projectId, intentRec);

        HANDLE hDest = CreateFileW(stagedFilePath.c_str(), GENERIC_WRITE,
            0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hDest == INVALID_HANDLE_VALUE)
        {
            CloseHandle(hSource);
            storage_.RemoveDraftAttachment(projectId, candidate.id);
            candidate.status = AttachmentStatus::Rejected;
            candidate.errorMessage = "Cannot create staging file for: " + candidate.fileName;
            return candidate;
        }

        BCRYPT_ALG_HANDLE hAlg = nullptr;
        NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (!BCRYPT_SUCCESS(status))
        {
            CloseHandle(hDest);
            CloseHandle(hSource);
            std::filesystem::remove(stagedFilePath, ec);
            storage_.RemoveDraftAttachment(projectId, candidate.id);
            candidate.status = AttachmentStatus::Rejected;
            candidate.errorMessage = "BCryptOpenAlgorithmProvider failed";
            return candidate;
        }

        DWORD hashObjSize = 0, dataLen = 0;
        status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&hashObjSize, sizeof(DWORD), &dataLen, 0);
        if (!BCRYPT_SUCCESS(status))
        {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            CloseHandle(hDest);
            CloseHandle(hSource);
            std::filesystem::remove(stagedFilePath, ec);
            storage_.RemoveDraftAttachment(projectId, candidate.id);
            candidate.status = AttachmentStatus::Rejected;
            candidate.errorMessage = "BCryptGetProperty failed";
            return candidate;
        }

        std::vector<BYTE> hashObj(hashObjSize);
        BCRYPT_HASH_HANDLE hHash = nullptr;
        status = BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0);
        if (!BCRYPT_SUCCESS(status))
        {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            CloseHandle(hDest);
            CloseHandle(hSource);
            std::filesystem::remove(stagedFilePath, ec);
            storage_.RemoveDraftAttachment(projectId, candidate.id);
            candidate.status = AttachmentStatus::Rejected;
            candidate.errorMessage = "BCryptCreateHash failed";
            return candidate;
        }

        constexpr DWORD BUFFER_SIZE = 64 * 1024;
        std::vector<BYTE> buffer(BUFFER_SIZE);
        uint64_t totalBytesRead = 0;
        auto const startTime = clockFn();
        auto const effectiveDeadline = deadline.value_or(startTime + std::chrono::seconds(PREPARATION_TIMEOUT_SECONDS));
        bool timedOut = false;
        bool cancelled = false;
        bool writeFailed = false;

        while (totalBytesRead < static_cast<uint64_t>(initialSize.QuadPart))
        {
            if (isCancelled())
            {
                cancelled = true;
                break;
            }

            if (clockFn() >= effectiveDeadline)
            {
                timedOut = true;
                break;
            }

            DWORD bytesRead = 0;
            if (!ReadFile(hSource, buffer.data(), BUFFER_SIZE, &bytesRead, nullptr) || bytesRead == 0)
            {
                break;
            }

            DWORD bytesWritten = 0;
            if (!WriteFile(hDest, buffer.data(), bytesRead, &bytesWritten, nullptr) || bytesWritten != bytesRead)
            {
                writeFailed = true;
                break;
            }

            BCryptHashData(hHash, buffer.data(), bytesRead, 0);
            totalBytesRead += bytesRead;
        }

        // Post-copy verification on source handle
        LARGE_INTEGER finalSize{};
        FILETIME finalLastWrite{};
        FILE_ID_INFO finalFileId{};
        GetFileSizeEx(hSource, &finalSize);
        GetFileTime(hSource, nullptr, nullptr, &finalLastWrite);
        bool finalHasFileId = (GetFileInformationByHandleEx(hSource, FileIdInfo, &finalFileId, sizeof(finalFileId)) != 0);

        bool sourceChanged = false;
        if (totalBytesRead != static_cast<uint64_t>(initialSize.QuadPart) ||
            finalSize.QuadPart != initialSize.QuadPart ||
            finalLastWrite.dwLowDateTime != initialLastWrite.dwLowDateTime ||
            finalLastWrite.dwHighDateTime != initialLastWrite.dwHighDateTime)
        {
            sourceChanged = true;
        }
        if (candidate.hasSourceFileId && finalHasFileId)
        {
            if (memcmp(&initialFileId, &finalFileId, sizeof(FILE_ID_INFO)) != 0)
            {
                sourceChanged = true;
            }
        }

        BYTE hash[32];
        BCryptFinishHash(hHash, hash, 32, 0);
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        CloseHandle(hDest);
        CloseHandle(hSource);

        if (cancelled)
        {
            std::filesystem::remove(stagedFilePath, ec);
            storage_.RemoveDraftAttachment(projectId, candidate.id);
            candidate.status = AttachmentStatus::Rejected;
            candidate.errorMessage = "Preparation cancelled";
            return candidate;
        }
        if (timedOut)
        {
            std::filesystem::remove(stagedFilePath, ec);
            storage_.RemoveDraftAttachment(projectId, candidate.id);
            candidate.status = AttachmentStatus::TimedOut;
            candidate.errorMessage = "Attachment preparation exceeded 120 seconds active timeout.";
            return candidate;
        }
        if (writeFailed)
        {
            std::filesystem::remove(stagedFilePath, ec);
            storage_.RemoveDraftAttachment(projectId, candidate.id);
            candidate.status = AttachmentStatus::Rejected;
            candidate.errorMessage = "Writing staging copy failed for " + candidate.fileName;
            return candidate;
        }
        if (sourceChanged)
        {
            std::filesystem::remove(stagedFilePath, ec);
            storage_.RemoveDraftAttachment(projectId, candidate.id);
            candidate.status = AttachmentStatus::SourceChangedDuringPreparation;
            candidate.errorMessage = "Source file changed during preparation: " + candidate.fileName +
                ". Please reselect or remove it.";
            return candidate;
        }

        std::ostringstream ss;
        for (int i = 0; i < 32; ++i)
        {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        candidate.sha256 = ss.str();
        candidate.fileSize = totalBytesRead;
        candidate.status = AttachmentStatus::Ready;

        AttachmentRecord readyRec = intentRec;
        readyRec.fileSize = totalBytesRead;
        readyRec.sha256 = candidate.sha256;
        readyRec.status = "READY";
        storage_.SaveDraftAttachment(projectId, readyRec);

        return candidate;
#else
        candidate.status = AttachmentStatus::Rejected;
        candidate.errorMessage = "Only supported on Windows";
        return candidate;
#endif
    }

    void AttachmentService::SaveDraftAttachments(std::string const& projectId, std::vector<AttachmentCandidate> const& attachments)
    {
        for (auto const& att : attachments)
        {
            AttachmentRecord rec;
            rec.id = att.id;
            rec.projectId = projectId;
            rec.fileName = att.fileName;
            rec.fileSize = att.fileSize;
            rec.sha256 = att.sha256;
            rec.detectedType = att.detectedType;
            rec.storagePath = Utf16ToUtf8(att.stagedPath);
            rec.sourcePath = Utf16ToUtf8(att.originalPath);
            switch (att.status)
            {
                case AttachmentStatus::Selected: rec.status = "SELECTED"; break;
                case AttachmentStatus::Preparing: rec.status = "PREPARING"; break;
                case AttachmentStatus::Ready: rec.status = "READY"; break;
                case AttachmentStatus::Rejected: rec.status = "REJECTED"; break;
                case AttachmentStatus::SourceChangedDuringPreparation: rec.status = "SOURCE_CHANGED"; break;
                case AttachmentStatus::TimedOut: rec.status = "TIMED_OUT"; break;
                case AttachmentStatus::Committed: rec.status = "COMMITTED"; break;
            }
            rec.errorMessage = att.errorMessage;
            storage_.SaveDraftAttachment(projectId, rec);
        }
    }

    std::vector<AttachmentCandidate> AttachmentService::LoadDraftAttachments(std::string const& projectId)
    {
        auto records = storage_.GetDraftAttachments(projectId);
        std::vector<AttachmentCandidate> result;
        result.reserve(records.size());
        for (auto const& r : records)
        {
            AttachmentCandidate c;
            c.id = r.id;
            c.fileName = r.fileName;
            c.fileSize = r.fileSize;
            c.sha256 = r.sha256;
            c.detectedType = r.detectedType;
            c.stagedPath = Utf8ToUtf16(r.storagePath);
            c.originalPath = Utf8ToUtf16(r.sourcePath);
            c.errorMessage = r.errorMessage;
            if (r.status == "READY") c.status = AttachmentStatus::Ready;
            else if (r.status == "PREPARING") c.status = AttachmentStatus::Preparing;
            else if (r.status == "SOURCE_CHANGED") c.status = AttachmentStatus::SourceChangedDuringPreparation;
            else if (r.status == "TIMED_OUT") c.status = AttachmentStatus::TimedOut;
            else if (r.status == "REJECTED") c.status = AttachmentStatus::Rejected;
            else c.status = AttachmentStatus::Selected;
            result.push_back(std::move(c));
        }
        return result;
    }

    void AttachmentService::RemoveDraftAttachment(std::string const& projectId, std::string const& attachmentId)
    {
        auto draft = LoadDraftAttachments(projectId);
        for (auto const& c : draft)
        {
            if (c.id == attachmentId)
            {
                std::error_code ec;
                if (!c.stagedPath.empty() && std::filesystem::exists(c.stagedPath, ec))
                {
#ifdef _WIN32
                    SetFileAttributesW(c.stagedPath.c_str(), FILE_ATTRIBUTE_NORMAL);
#endif
                    std::filesystem::remove(c.stagedPath, ec);
                }
                break;
            }
        }
        storage_.RemoveDraftAttachment(projectId, attachmentId);
    }

    void AttachmentService::ClearDraftAttachments(std::string const& projectId)
    {
        auto draft = LoadDraftAttachments(projectId);
        for (auto const& c : draft)
        {
            std::error_code ec;
            if (!c.stagedPath.empty() && std::filesystem::exists(c.stagedPath, ec))
            {
#ifdef _WIN32
                SetFileAttributesW(c.stagedPath.c_str(), FILE_ATTRIBUTE_NORMAL);
#endif
                std::filesystem::remove(c.stagedPath, ec);
            }
        }
        storage_.ClearDraftAttachments(projectId);
    }

    void AttachmentService::ReconcileStagedFiles(std::string const& projectId)
    {
        std::error_code ec;
        auto to_lower = [](std::wstring s) {
            for (auto& ch : s) ch = static_cast<wchar_t>(::towlower(ch));
            return s;
        };

        // 1. Remove interrupted PREPARING draft attachments (both file and DB record)
        auto drafts = storage_.GetDraftAttachments(projectId);
        for (auto const& d : drafts)
        {
            if (d.status == "PREPARING")
            {
                if (!d.storagePath.empty())
                {
                    auto p = Utf8ToUtf16(d.storagePath);
#ifdef _WIN32
                    SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
#endif
                    std::filesystem::remove(p, ec);
                }
                storage_.RemoveDraftAttachment(projectId, d.id);
            }
        }

        // 2. Collect all valid tracked storage paths for this project
        std::unordered_set<std::wstring> trackedPaths;
        auto remainingDrafts = storage_.GetDraftAttachments(projectId);
        for (auto const& d : remainingDrafts)
        {
            if (!d.storagePath.empty())
            {
                std::filesystem::path p = Utf8ToUtf16(d.storagePath);
                trackedPaths.insert(to_lower(std::filesystem::weakly_canonical(p, ec).wstring()));
            }
        }

        auto committed = storage_.GetCommittedAttachmentsForProject(projectId);
        for (auto const& c : committed)
        {
            if (!c.storagePath.empty())
            {
                std::filesystem::path p = Utf8ToUtf16(c.storagePath);
                trackedPaths.insert(to_lower(std::filesystem::weakly_canonical(p, ec).wstring()));
#ifdef _WIN32
                // Ensure committed files are sealed as read-only (crash/restart immutability recovery)
                if (std::filesystem::exists(p, ec))
                {
                    DWORD attrs = GetFileAttributesW(p.c_str());
                    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_READONLY))
                    {
                        SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_READONLY);
                    }
                }
#endif
            }
        }

        // 3. Scan the project's staging directory and remove any untracked regular files
        auto stagingDir = stagingRoot_ / "staging" / projectId;
        if (std::filesystem::exists(stagingDir, ec) && std::filesystem::is_directory(stagingDir, ec))
        {
            for (auto const& entry : std::filesystem::directory_iterator(stagingDir, ec))
            {
                if (entry.is_regular_file(ec))
                {
                    std::wstring canon = to_lower(std::filesystem::weakly_canonical(entry.path(), ec).wstring());
                    if (trackedPaths.find(canon) == trackedPaths.end())
                    {
#ifdef _WIN32
                        SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
#endif
                        std::filesystem::remove(entry.path(), ec);
                    }
                }
            }
        }
    }

    std::vector<AttachmentCandidate> AttachmentService::ReverifyDraftAttachments(std::string const& projectId)
    {
        ReconcileStagedFiles(projectId);
        auto candidates = LoadDraftAttachments(projectId);
        for (auto& c : candidates)
        {
            if (c.status == AttachmentStatus::Committed)
            {
                continue;
            }

            std::error_code ec;
            if (c.stagedPath.empty() || !std::filesystem::exists(c.stagedPath, ec))
            {
                c.status = AttachmentStatus::Rejected;
                c.errorMessage = "Staged attachment file is missing after recovery: " + c.fileName + ". Please reselect or remove it.";
            }
            else
            {
                uint64_t actualSize = 0;
                try
                {
                    std::string actualSha256 = ComputeFileSha256(c.stagedPath, &actualSize);
                    if (actualSize != c.fileSize || actualSha256 != c.sha256)
                    {
                        c.status = AttachmentStatus::Rejected;
                        c.errorMessage = "Staged attachment integrity verification failed for " + c.fileName + ". Please reselect or remove it.";
                    }
                    else
                    {
                        c.status = AttachmentStatus::Ready;
                        c.errorMessage.clear();
                    }
                }
                catch (std::exception const& ex)
                {
                    c.status = AttachmentStatus::Rejected;
                    c.errorMessage = std::string("Integrity check failed: ") + ex.what();
                }
            }
        }
        SaveDraftAttachments(projectId, candidates);
        return candidates;
    }

    std::vector<AttachmentRecord> AttachmentService::GetAttachmentsForContribution(std::string const& submissionId)
    {
        return storage_.GetAttachmentsForContribution(submissionId);
    }
}
