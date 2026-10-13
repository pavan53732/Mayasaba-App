#pragma once
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <winsqlite3.h>
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
    };
    struct Contribution
    {
        std::string submissionId;
        std::string body;
        std::string createdAt;
    };
    enum class SaveDisposition { Saved, Duplicate, Conflict, RejectedClosed, Unknown };
    struct PendingSubmission { std::string id; std::string body; };

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
        void BindProject(std::string const& id, std::string const& name, std::string const& canonicalPath);
        std::optional<ProjectRecord> Project(std::string const& id);
        void SaveDraft(std::string const& projectId, std::string const& body);
        // One durable pending ID per project until the contribution outcome is known.
        std::string StageSubmission(std::string const& projectId, std::string const& proposedId, std::string const& body);
        std::optional<PendingSubmission> Pending(std::string const& projectId);
        bool SetClosure(std::string const& projectId, int expectedVersion, bool closed);
        SaveDisposition SaveContribution(std::string const& projectId, std::string const& submissionId, std::string const& body);
        std::vector<Contribution> Contributions(std::string const& projectId);
        std::optional<std::string> LastBoundProject();

    private:
        sqlite3* db_{nullptr};
        std::mutex mutex_;
#ifdef _WIN32
        HANDLE instanceMutex_{nullptr};
#endif
        void Exec(char const* sql);
        static void Bind(sqlite3_stmt* statement, int index, std::string const& value);
        static std::string Text(sqlite3_stmt* statement, int column);
        static void Check(int status, sqlite3* database);
    };
}
