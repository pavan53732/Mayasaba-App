#include "Storage.h"
#include <filesystem>
#include <utility>
#ifdef _WIN32
#include <shlobj.h>
#endif

namespace mayasaba
{
    namespace
    {
        struct Statement
        {
            sqlite3_stmt* value{};
            Statement(sqlite3* db, char const* sql)
            {
                if (sqlite3_prepare_v2(db, sql, -1, &value, nullptr) != SQLITE_OK)
                    throw std::runtime_error(sqlite3_errmsg(db));
            }
            ~Statement() { if (value) sqlite3_finalize(value); }
            Statement(Statement const&) = delete;
            Statement& operator=(Statement const&) = delete;
        };
    }

    void Storage::Check(int rc, sqlite3* db)
    {
        if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    void Storage::Exec(char const* sql)
    {
        char* error = nullptr;
        auto const rc = sqlite3_exec(db_, sql, nullptr, nullptr, &error);
        if (rc != SQLITE_OK)
        {
            std::string message = error ? error : sqlite3_errmsg(db_);
            if (error) sqlite3_free(error);
            throw std::runtime_error(message);
        }
    }
    void Storage::Bind(sqlite3_stmt* statement, int index, std::string const& value)
    {
        Check(sqlite3_bind_text(statement, index, value.c_str(),
            static_cast<int>(value.size()), SQLITE_TRANSIENT), sqlite3_db_handle(statement));
    }
    std::string Storage::Text(sqlite3_stmt* statement, int column)
    {
        auto const* ptr = sqlite3_column_text(statement, column);
        auto length = sqlite3_column_bytes(statement, column);
        if (!ptr || length <= 0) return {};
        return {reinterpret_cast<char const*>(ptr), static_cast<size_t>(length)};
    }

    std::filesystem::path Storage::DefaultFile()
    {
#ifdef _WIN32
        PWSTR raw = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw)))
            throw std::runtime_error("Could not locate local app data");
        std::filesystem::path result{raw};
        CoTaskMemFree(raw);
        return result / L"Mayasaba" / L"mayasaba.db";
#else
        return std::filesystem::temp_directory_path() / "mayasaba" / "mayasaba.db";
#endif
    }
    Storage::Storage(std::filesystem::path const& file)
    {
#ifdef _WIN32
        // Fail closed rather than create a second active native writer/controller in the same user session.
        instanceMutex_ = CreateMutexW(nullptr, TRUE, L"Local\\Mayasaba.AppStore.SingleWriter.v1");
        if (!instanceMutex_ || GetLastError() == ERROR_ALREADY_EXISTS)
        {
            if (instanceMutex_) CloseHandle(instanceMutex_);
            instanceMutex_ = nullptr;
            throw std::runtime_error("Another Mayasaba instance is already using its store");
        }
#endif
        try
        {
            std::filesystem::create_directories(file.parent_path());
#ifdef _WIN32
            int status = sqlite3_open16(file.c_str(), &db_);
#else
            int status = sqlite3_open(file.string().c_str(), &db_);
#endif
            if (status != SQLITE_OK) throw std::runtime_error(db_ ? sqlite3_errmsg(db_) : "SQLite open failed");
            sqlite3_busy_timeout(db_, 5000);
            Exec("PRAGMA foreign_keys=ON;");
            Exec("PRAGMA journal_mode=WAL;");
            Exec("PRAGMA synchronous=FULL;");
            // Refuse unverified fallback; a future B3 implementation must also record observed settings.
            {
                Statement mode(db_, "PRAGMA journal_mode;");
                if (sqlite3_step(mode.value) != SQLITE_ROW || Text(mode.value, 0) != "wal")
                    throw std::runtime_error("WAL not available: store writes refused");
            }
            {
                Statement sync(db_, "PRAGMA synchronous;");
                if (sqlite3_step(sync.value) != SQLITE_ROW || sqlite3_column_int(sync.value, 0) != 2)
                    throw std::runtime_error("FULL synchronous mode not observed");
            }
            Exec("CREATE TABLE IF NOT EXISTS projects("
                 "id TEXT PRIMARY KEY, display_name TEXT NOT NULL, canonical_path TEXT NOT NULL,"
                 "draft TEXT NOT NULL DEFAULT '', closed INTEGER NOT NULL DEFAULT 0,"
                 "version INTEGER NOT NULL DEFAULT 0);");
            Exec("CREATE TABLE IF NOT EXISTS contributions("
                 "row_id INTEGER PRIMARY KEY AUTOINCREMENT, project_id TEXT NOT NULL REFERENCES projects(id),"
                 "submission_id TEXT NOT NULL UNIQUE, body TEXT NOT NULL,"
                 "created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));");
            Exec("CREATE INDEX IF NOT EXISTS contribution_project ON contributions(project_id,row_id);");
            Exec("CREATE TABLE IF NOT EXISTS pending_submissions(project_id TEXT PRIMARY KEY REFERENCES projects(id),submission_id TEXT UNIQUE NOT NULL,body TEXT NOT NULL);");
            Exec("CREATE TABLE IF NOT EXISTS ui_state(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                 "last_project TEXT NOT NULL);");
        }
        catch (...)
        {
            if (db_) sqlite3_close(db_);
            db_ = nullptr;
#ifdef _WIN32
            if (instanceMutex_) { ReleaseMutex(instanceMutex_); CloseHandle(instanceMutex_); instanceMutex_ = nullptr; }
#endif
            throw;
        }
    }
    Storage::~Storage()
    {
        if (db_) sqlite3_close(db_);
#ifdef _WIN32
        if (instanceMutex_) { ReleaseMutex(instanceMutex_); CloseHandle(instanceMutex_); }
#endif
    }
    void Storage::BindProject(std::string const& id, std::string const& name, std::string const& path)
    {
        std::lock_guard lock(mutex_);
        Exec("BEGIN IMMEDIATE;");
        try
        {
            Statement p(db_, "INSERT INTO projects(id,display_name,canonical_path) VALUES(?1,?2,?3)"
                              "ON CONFLICT(id) DO UPDATE SET display_name=excluded.display_name,"
                              "canonical_path=excluded.canonical_path;");
            Bind(p.value,1,id); Bind(p.value,2,name); Bind(p.value,3,path);
            Check(sqlite3_step(p.value),db_);
            Statement u(db_, "INSERT INTO ui_state(singleton,last_project) VALUES(1,?1)"
                              "ON CONFLICT(singleton) DO UPDATE SET last_project=excluded.last_project;");
            Bind(u.value,1,id); Check(sqlite3_step(u.value),db_);
            Exec("COMMIT;");
        } catch (...) { sqlite3_exec(db_,"ROLLBACK;",nullptr,nullptr,nullptr); throw; }
    }
    std::optional<ProjectRecord> Storage::Project(std::string const& id)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT id,display_name,canonical_path,draft,closed,version FROM projects WHERE id=?1;");
        Bind(q.value,1,id);
        int rc=sqlite3_step(q.value);
        if (rc==SQLITE_DONE) return std::nullopt;
        Check(rc,db_);
        return ProjectRecord{Text(q.value,0),Text(q.value,1),Text(q.value,2),Text(q.value,3),
            sqlite3_column_int(q.value,4)!=0,sqlite3_column_int(q.value,5)};
    }
    std::optional<std::string> Storage::LastBoundProject()
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT last_project FROM ui_state WHERE singleton=1;");
        int rc=sqlite3_step(q.value);
        if (rc==SQLITE_DONE) return std::nullopt;
        Check(rc,db_);
        return Text(q.value,0);
    }
    void Storage::SaveDraft(std::string const& id, std::string const& body)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "UPDATE projects SET draft=?2 WHERE id=?1;");
        Bind(q.value,1,id); Bind(q.value,2,body);
        Check(sqlite3_step(q.value),db_);
        if (sqlite3_changes(db_)!=1) throw std::runtime_error("Draft project not found");
    }
    std::optional<PendingSubmission> Storage::Pending(std::string const& id)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT submission_id,body FROM pending_submissions WHERE project_id=?1;");
        Bind(q.value,1,id);
        int rc=sqlite3_step(q.value);
        if (rc==SQLITE_DONE) return std::nullopt;
        Check(rc,db_);
        return PendingSubmission{Text(q.value,0),Text(q.value,1)};
    }
    std::string Storage::StageSubmission(std::string const& id, std::string const& proposed, std::string const& body)
    {
        if(id.empty()||proposed.empty()||body.empty()) throw std::runtime_error("Invalid submission");
        std::lock_guard lock(mutex_);
        Exec("BEGIN IMMEDIATE;");
        try
        {
            {
                Statement project(db_, "SELECT closed FROM projects WHERE id=?1;");
                Bind(project.value,1,id);
                int rc=sqlite3_step(project.value);
                if (rc==SQLITE_DONE) throw std::runtime_error("Missing project");
                Check(rc,db_);
                if (sqlite3_column_int(project.value,0))
                    throw std::runtime_error("Closed project");
            }
            std::string winner=proposed;
            {
                Statement q(db_, "SELECT submission_id,body FROM pending_submissions WHERE project_id=?1;");
                Bind(q.value,1,id);
                if(sqlite3_step(q.value)==SQLITE_ROW)
                {
                    if(Text(q.value,1)!=body)
                        throw std::runtime_error("Different source text while prior submission is unresolved");
                    winner=Text(q.value,0);
                }
                else
                {
                    Statement add(db_, "INSERT INTO pending_submissions(project_id,submission_id,body) VALUES(?1,?2,?3);");
                    Bind(add.value,1,id);Bind(add.value,2,proposed);Bind(add.value,3,body);
                    Check(sqlite3_step(add.value),db_);
                    Statement draft(db_, "UPDATE projects SET draft=?2 WHERE id=?1;");
                    Bind(draft.value,1,id);Bind(draft.value,2,body);
                    Check(sqlite3_step(draft.value),db_);
                }
            }
            Exec("COMMIT;");
            return winner;
        }
        catch(...)
        {
            sqlite3_exec(db_,"ROLLBACK;",nullptr,nullptr,nullptr);
            throw;
        }
    }
    bool Storage::SetClosure(std::string const& id, int version, bool closed)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "UPDATE projects SET closed=?3,version=version+1 WHERE id=?1 AND version=?2;");
        Bind(q.value,1,id);
        Check(sqlite3_bind_int(q.value,2,version),db_);
        Check(sqlite3_bind_int(q.value,3,closed?1:0),db_);
        Check(sqlite3_step(q.value),db_);
        return sqlite3_changes(db_)==1;
    }
    SaveDisposition Storage::SaveContribution(std::string const& id, std::string const& submission, std::string const& body)
    {
        if (submission.empty() || body.empty()) return SaveDisposition::Conflict;
        std::lock_guard lock(mutex_);
        bool began=false;
        try
        {
            Exec("BEGIN IMMEDIATE;"); began=true;
            {
                Statement p(db_, "SELECT closed FROM projects WHERE id=?1;");
                Bind(p.value,1,id);
                if (sqlite3_step(p.value)!=SQLITE_ROW || sqlite3_column_int(p.value,0)!=0)
                {
                    Exec("ROLLBACK;");return SaveDisposition::RejectedClosed;
                }
            }
            // A fresh command cannot replace an unresolved project-local submission.
            {
                Statement pending(db_, "SELECT submission_id,body FROM pending_submissions WHERE project_id=?1;");
                Bind(pending.value,1,id);
                if(sqlite3_step(pending.value)==SQLITE_ROW)
                {
                    if(Text(pending.value,0)!=submission||Text(pending.value,1)!=body)
                    { Exec("ROLLBACK;"); return SaveDisposition::Conflict; }
                }
                else
                {
                    // Pending retired only by a successful commit; identical replay is allowed.
                    Statement stored(db_, "SELECT project_id,body FROM contributions WHERE submission_id=?1;");
                    Bind(stored.value,1,submission);
                    if(sqlite3_step(stored.value)!=SQLITE_ROW ||
                        Text(stored.value,0)!=id||Text(stored.value,1)!=body)
                    { Exec("ROLLBACK;"); return SaveDisposition::Conflict; }
                }
            }
            bool duplicate=false;
            {
                Statement p(db_, "SELECT project_id,body FROM contributions WHERE submission_id=?1;");
                Bind(p.value,1,submission);
                if (sqlite3_step(p.value)==SQLITE_ROW)
                {
                    if (Text(p.value,0)!=id || Text(p.value,1)!=body)
                    { Exec("ROLLBACK;");return SaveDisposition::Conflict; }
                    duplicate=true;
                }
            }
            if (!duplicate)
            {
                Statement p(db_, "INSERT INTO contributions(project_id,submission_id,body) VALUES(?1,?2,?3);");
                Bind(p.value,1,id); Bind(p.value,2,submission); Bind(p.value,3,body);
                Check(sqlite3_step(p.value),db_);
            }
            {
                Statement p(db_, "UPDATE projects SET draft='' WHERE id=?1;");
                Bind(p.value,1,id);Check(sqlite3_step(p.value),db_);
            }
            {
                Statement cleared(db_, "DELETE FROM pending_submissions WHERE project_id=?1 AND submission_id=?2;");
                Bind(cleared.value,1,id);Bind(cleared.value,2,submission);Check(sqlite3_step(cleared.value),db_);
            }
            Exec("COMMIT;");
            return duplicate?SaveDisposition::Duplicate:SaveDisposition::Saved;
        }
        catch (...)
        {
            if (began) sqlite3_exec(db_,"ROLLBACK;",nullptr,nullptr,nullptr);
            // Caller must reconcile the *original* ID; never blindly retry on a new one.
            return SaveDisposition::Unknown;
        }
    }
    std::vector<Contribution> Storage::Contributions(std::string const& id)
    {
        std::lock_guard lock(mutex_);
        Statement p(db_, "SELECT submission_id,body,created_at FROM contributions WHERE project_id=?1 ORDER BY row_id;");
        Bind(p.value,1,id);
        std::vector<Contribution> result;
        int rc=SQLITE_OK;
        while ((rc=sqlite3_step(p.value))==SQLITE_ROW)
            result.push_back({Text(p.value,0),Text(p.value,1),Text(p.value,2)});
        Check(rc,db_);
        return result;
    }
}
