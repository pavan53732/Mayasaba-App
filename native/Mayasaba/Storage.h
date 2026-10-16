#pragma once
#include <filesystem>
#include <atomic>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
// The Windows SDK is expected to provide winsqlite3.h. Some SDK installs ship
// winsqlite3.lib without the header; in that case compile against the pinned
// declaration shim instead of failing the build. The system header is always
// preferred when present.
#if defined(__has_include)
#if __has_include(<winsqlite3.h>)
#include <winsqlite3.h>
#define MAYASABA_HAVE_SYSTEM_WINSQLITE3 1
#endif
#endif
#ifndef MAYASABA_HAVE_SYSTEM_WINSQLITE3
#include "winsqlite3_compat.h"
#endif
#else
#include <sqlite3.h>
#endif

namespace mayasaba
{
    struct ProjectRecord
    {
        std::string id;
        std::string displayName;
        std::string canonicalPath;
        std::string draft;
        bool closed{};
        int version{};
        std::string lastOpenedAt;
        std::string filesystemIdentity;
        std::string domainId; // canonical UUID; legacy storage keys remain immutable
    };
    struct DirectoryOperationRecord
    {
        std::string operationId;
        std::string parentPath;
        std::string folderName;
        std::string targetPath;
        std::string state; // "INTENDED", "CREATED", "BOUND", "FAILED"
        std::string observedIdentity;
        std::string errorMessage;
        std::string createdAt;
        std::string updatedAt;
    };
    struct Contribution
    {
        std::string submissionId;
        std::string body;
        std::string createdAt;
    };
    enum class SaveDisposition { Saved, Duplicate, Conflict, RejectedClosed, Unknown };
    struct PendingSubmission { std::string id; std::string body; };

    struct CrossProjectRecentProjectsQuery
    {
        size_t pageSize{20};
        size_t offset{0};
    };

    struct CrossProjectRecentProjectsResult
    {
        std::vector<ProjectRecord> items;
        bool isComplete{true};
        std::string errorMessage;
        bool hasMore{};
    };

    enum class DraftChoice { Automatic, KeepProject, UseSelector };
    struct SelectionCommit
    {
        std::string operationId, requestBytes, proposedId, expectedProject;
        int expectedVersion{-1};
        std::string name, path, filesystemIdentity;
        DraftChoice draftChoice{DraftChoice::Automatic};
        std::string expectedSelectorDraft, expectedTargetDraft;
        std::string creationOperation;
    };
    struct SelectionReceipt
    {
        ProjectRecord project;
        bool collision{}, pending{}, transferred{}, duplicate{};
        std::string selectorDraft;
    };

    // One owning connection and an OS-visible writer mutex, not a domain authority service.
    // ALL public calls are serialized on the owning connection.
    class Storage final
    {
    public:
        explicit Storage(std::filesystem::path const& file);
        ~Storage();
        Storage(Storage const&) = delete;
        Storage& operator=(Storage const&) = delete;

        static std::filesystem::path DefaultFile();
        std::string BindProject(std::string const& id, std::string const& name, std::string const& canonicalPath, std::string const& filesystemIdentity = "");
        std::optional<ProjectRecord> Project(std::string const& id);
        std::optional<ProjectRecord> ProjectByFilesystemIdentity(std::string const& filesystemIdentity);
        bool HasAnyProjects();
        std::vector<ProjectRecord> RecentProjects(size_t limit = 50);
        CrossProjectRecentProjectsResult QueryRecentProjects(CrossProjectRecentProjectsQuery const& query);
        std::optional<ProjectRecord> ProjectByDomainId(std::string const& id);
        SelectionReceipt CommitSelection(SelectionCommit const& command);
        void TouchProjectOpened(std::string const& projectId);
        void RelocateProject(std::string const& id, std::string const& newCanonicalPath, std::string const& newDisplayName);
        void SaveDraft(std::string const& projectId, std::string const& body);
        void SavePrebindDraft(std::string const& body);
        uint64_t ReservePrebindRevision() noexcept { return ++requestedDraftRevision_; }
        bool SavePrebindDraft(std::string const& body, uint64_t revision);
        std::string GetPrebindDraft();
        void ClearPrebindDraft();
        bool TransferPrebindDraftToProject(std::string const& projectId, bool overwrite = false);

        // Directory creation durable intent, observation and crash reconciliation
        void RecordDirectoryIntent(std::string const& opId, std::string const& parentPath, std::string const& folderName, std::string const& targetPath);
        void RecordDirectoryCreated(std::string const& opId, std::string const& observedIdentity);
        void RecordDirectoryBound(std::string const& opId);
        void RecordDirectoryUncertain(std::string const& opId, std::string const& errorMessage);
        void RecordDirectoryFailed(std::string const& opId, std::string const& errorMessage);
        std::optional<DirectoryOperationRecord> GetDirectoryOperation(std::string const& opId);
        std::vector<DirectoryOperationRecord> UnreconciledDirectoryOperations();
        void ReconcileCrashOperations();

        // One durable pending ID per project until the contribution outcome is known.
        std::string StageSubmission(std::string const& projectId, std::string const& proposedId, std::string const& body);
        std::optional<PendingSubmission> Pending(std::string const& projectId);
        bool SetClosure(std::string const& projectId, int expectedVersion, bool closed);
        SaveDisposition SaveContribution(std::string const& projectId, std::string const& submissionId, std::string const& body);
        std::vector<Contribution> Contributions(std::string const& projectId);
        std::optional<std::string> LastBoundProject();

    private:
        sqlite3* db_{nullptr};
        std::filesystem::path file_;
        std::mutex mutex_;
        std::atomic_uint64_t requestedDraftRevision_{0};
        uint64_t savedDraftRevision_{0};
#ifdef _WIN32
        HANDLE instanceMutex_{nullptr};
        HANDLE identityHandle_{INVALID_HANDLE_VALUE};
        HANDLE legacyMutex_{nullptr};
#endif
        void Exec(char const* sql);
        void CheckWriteCapacity();
        static void Bind(sqlite3_stmt* statement, int index, std::string const& value);
        static std::string Text(sqlite3_stmt* statement, int column);
        static void Check(int status, sqlite3* database);
    };
}
