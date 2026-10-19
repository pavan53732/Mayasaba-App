#pragma once
#include "Storage.h"
#include "NativeIds.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mayasaba
{
    constexpr size_t MAX_ATTACHMENT_COUNT = 50;
    constexpr uint64_t MAX_FILE_SIZE_BYTES = 100ULL * 1024ULL * 1024ULL; // 100 MiB = 104,857,600 bytes
    constexpr uint64_t MAX_AGGREGATE_SIZE_BYTES = 500ULL * 1024ULL * 1024ULL; // 500 MiB = 524,288,000 bytes
    constexpr uint32_t PREPARATION_TIMEOUT_SECONDS = 120; // 120 active seconds

    enum class AttachmentStatus
    {
        Selected,
        Preparing,
        Ready,
        Rejected,
        SourceChangedDuringPreparation,
        TimedOut,
        Committed
    };

    enum class ValidationFailureReason
    {
        None,
        TooManyFiles,
        FileTooLarge,
        AggregateTooLarge,
        EmptyFile,
        InaccessibleFile,
        InvalidPath
    };

    struct AttachmentFileMetadata
    {
        std::wstring originalPath;
        std::string fileName;
        uint64_t fileSize{0};
        std::string detectedType;
#ifdef _WIN32
        FILETIME lastWriteTime{};
        FILE_ID_INFO fileIdInfo{};
        bool hasFileIdInfo{false};
#endif
    };

    struct AttachmentCandidate
    {
        std::string id;
        std::wstring originalPath;
        std::string fileName;
        uint64_t fileSize{0};
        std::string sha256;
        std::string detectedType;
        AttachmentStatus status{AttachmentStatus::Selected};
        std::string errorMessage;
        std::wstring stagedPath;
#ifdef _WIN32
        FILETIME sourceLastWrite{};
        FILE_ID_INFO sourceFileId{};
        bool hasSourceFileId{false};
#endif
    };

    struct BatchValidationResult
    {
        bool accepted{false};
        ValidationFailureReason reason{ValidationFailureReason::None};
        std::string errorMessage;
        std::vector<AttachmentFileMetadata> validatedFiles;
        uint64_t totalBytes{0};
    };

    class AttachmentService
    {
    public:
        explicit AttachmentService(Storage& storage, std::filesystem::path stagingRoot = {});

        // Validation slice: enforces count <= 50, file size <= 100 MiB, aggregate <= 500 MiB, non-empty, accessible
        BatchValidationResult ValidateCandidateBatch(
            std::vector<std::wstring> const& candidatePaths,
            std::vector<AttachmentCandidate> const& existingDraftAttachments) const;

        using SteadyClockFn = std::function<std::chrono::steady_clock::time_point()>;
        static std::chrono::steady_clock::time_point DefaultActiveClock();

        // Batch preparation: chunked copy with single 120-second active-time deadline
        std::vector<AttachmentCandidate> PrepareBatch(
            std::string const& projectId,
            std::vector<std::wstring> const& candidatePaths,
            std::function<bool()> const& isCancelled = [] { return false; },
            uint32_t timeoutSeconds = PREPARATION_TIMEOUT_SECONDS,
            SteadyClockFn clockFn = DefaultActiveClock);

        // Preparation slice: chunked copy to project staging, SHA-256 computation, and source integrity check
        AttachmentCandidate PrepareAttachment(
            std::string const& projectId,
            std::wstring const& sourcePath,
            std::function<bool()> const& isCancelled = [] { return false; },
            std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt,
            SteadyClockFn clockFn = DefaultActiveClock);

        // Staging and Draft persistence
        void SaveDraftAttachments(std::string const& projectId, std::vector<AttachmentCandidate> const& attachments);
        std::vector<AttachmentCandidate> LoadDraftAttachments(std::string const& projectId);
        void RemoveDraftAttachment(std::string const& projectId, std::string const& attachmentId);
        void ClearDraftAttachments(std::string const& projectId);

        // Reconciles orphaned files in the project's staging directory and clears incomplete preparation intents
        void ReconcileStagedFiles(std::string const& projectId);

        // Verification and crash recovery: checks that staged draft files exist and match recorded hashes
        std::vector<AttachmentCandidate> ReverifyDraftAttachments(std::string const& projectId);

        // Committed attachments query
        std::vector<AttachmentRecord> GetAttachmentsForContribution(std::string const& submissionId);

        // Static helpers
        static std::string DetectFileType(std::wstring const& path);
        static std::string ComputeFileSha256(std::wstring const& path, uint64_t* outSize = nullptr);
        static std::string FormatFileSize(uint64_t bytes);

        std::filesystem::path const& StagingRoot() const noexcept { return stagingRoot_; }

        // Testing hook for in-flight active chunk preparation verification
        void SetPreparationChunkHookForTesting(std::function<void()> hook)
        {
            preparationChunkHook_ = std::move(hook);
        }

    private:
        Storage& storage_;
        std::filesystem::path stagingRoot_;
        std::function<void()> preparationChunkHook_;

        static std::string Utf16ToUtf8(std::wstring const& w);
        static std::wstring Utf8ToUtf16(std::string const& u);
    };
}
