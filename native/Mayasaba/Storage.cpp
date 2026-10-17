#include "Storage.h"
#include "ProjectRoot.h"
#include "NativeIds.h"
#include "ProtectedDraft.h"
#include <filesystem>
#include <utility>
#include <sstream>
#include <iomanip>
#ifdef _WIN32
#include <shlobj.h>
#include <bcrypt.h>
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

#ifdef _WIN32
        inline std::string WideToUtf8(std::wstring const& w)
        {
            if (w.empty()) return {};
            int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
            std::string s(size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), size, nullptr, nullptr);
            return s;
        }
        inline std::wstring Utf8ToWide(std::string const& s)
        {
            if (s.empty()) return {};
            int size = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(size, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), size);
            return w;
        }

        inline std::string ComputeFileSha256(std::wstring const& path, uint64_t* outSize = nullptr)
        {
            HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE) return {};

            LARGE_INTEGER size{};
            if (!GetFileSizeEx(hFile, &size))
            {
                CloseHandle(hFile);
                return {};
            }
            if (outSize) *outSize = static_cast<uint64_t>(size.QuadPart);

            BCRYPT_ALG_HANDLE hAlg = nullptr;
            NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
            if (!BCRYPT_SUCCESS(status))
            {
                CloseHandle(hFile);
                return {};
            }

            DWORD hashObjSize = 0, dataLen = 0;
            status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&hashObjSize, sizeof(DWORD), &dataLen, 0);
            if (!BCRYPT_SUCCESS(status))
            {
                BCryptCloseAlgorithmProvider(hAlg, 0);
                CloseHandle(hFile);
                return {};
            }

            std::vector<BYTE> hashObj(hashObjSize);
            BCRYPT_HASH_HANDLE hHash = nullptr;
            status = BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0);
            if (!BCRYPT_SUCCESS(status))
            {
                BCryptCloseAlgorithmProvider(hAlg, 0);
                CloseHandle(hFile);
                return {};
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
        }
#endif
    }

    void Storage::Check(int rc, sqlite3* db)
    {
        if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    void Storage::Exec(char const* sql)
    {
        if(std::string_view(sql).starts_with("BEGIN IMMEDIATE")) CheckWriteCapacity();
        char* error = nullptr;
        auto const rc = sqlite3_exec(db_, sql, nullptr, nullptr, &error);
        if (rc != SQLITE_OK)
        {
            std::string message = error ? error : sqlite3_errmsg(db_);
            if (error) sqlite3_free(error);
            throw std::runtime_error(message);
        }
    }
    void Storage::CheckWriteCapacity()
    {
        auto space=std::filesystem::space(file_.parent_path());
        if(space.available < 5ull*1024*1024*1024)
            throw std::runtime_error("Local storage free-space reserve is below 5 GiB; protected data was retained");
        uintmax_t size=0;
        for(auto const& suffix:{L"",L"-wal",L"-shm"})
        {auto path=file_.wstring()+suffix;std::error_code ec;auto bytes=std::filesystem::file_size(path,ec);if(!ec)size+=bytes;}
        if(size>=10ull*1024*1024*1024)
            throw std::runtime_error("Local project store reached its 10 GiB ceiling; protected data was retained");
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
        auto production = std::filesystem::path(raw) / L"Mayasaba" / L"mayasaba.db";
        CoTaskMemFree(raw);
        SetLastError(ERROR_SUCCESS);
        wchar_t buffer[MAX_PATH]{};
        DWORD count = GetEnvironmentVariableW(L"MAYASABA_STORE_PATH", buffer, MAX_PATH);
        if (!count && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return production;
        if (!count || count >= MAX_PATH)
            throw std::runtime_error("Invalid test-store override; production fallback is forbidden");
        std::filesystem::path candidate(buffer);
        if (!candidate.is_absolute()) throw std::runtime_error("Test-store path must be absolute");
        auto canonical = std::filesystem::weakly_canonical(candidate).wstring();
        auto protectedPath = std::filesystem::weakly_canonical(production).wstring();
        std::error_code ec;
        bool sameFile = std::filesystem::equivalent(candidate, production, ec);
        if (_wcsicmp(canonical.c_str(), protectedPath.c_str()) == 0 || sameFile)
            throw std::runtime_error("Test-store override cannot target the production database");
        return candidate;
#else
        return std::filesystem::temp_directory_path() / "mayasaba" / "mayasaba.db";
#endif
    }
    Storage::Storage(std::filesystem::path const& file) : file_(file)
    {
        try
        {
            std::filesystem::create_directories(file.parent_path());
            CheckWriteCapacity();
#ifdef _WIN32
            // Lock the actual file identity, independent of environment, case,
            // hard links or path aliases. Hold it against rename for this lifetime.
            identityHandle_ = CreateFileW(file.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (identityHandle_ == INVALID_HANDLE_VALUE)
                throw std::runtime_error("Cannot hold database identity");
            FILE_ID_INFO identity{};
            if (!GetFileInformationByHandleEx(identityHandle_, FileIdInfo, &identity, sizeof(identity)))
                throw std::runtime_error("Cannot verify database file identity");
            std::wostringstream name;
            name << L"Local\\Mayasaba.AppStore.FileId." << std::hex << identity.VolumeSerialNumber << L".";
            for (auto byte : identity.FileId.Identifier)
                name << std::setw(2) << std::setfill(L'0') << static_cast<unsigned>(byte);
            instanceMutex_ = CreateMutexW(nullptr, FALSE, name.str().c_str());
            if (!instanceMutex_ || GetLastError() == ERROR_ALREADY_EXISTS)
                throw std::runtime_error("Another Mayasaba instance is already using its store");
            // Cooperate with older application versions for the production store.
            PWSTR raw = nullptr;
            if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw)))
                throw std::runtime_error("Cannot identify protected application store");
            auto production = std::filesystem::path(raw) / L"Mayasaba" / L"mayasaba.db";
            CoTaskMemFree(raw);
            std::error_code ec;
            if (std::filesystem::equivalent(file, production, ec))
            {
                legacyMutex_ = CreateMutexW(nullptr, FALSE, L"Local\\Mayasaba.AppStore.SingleWriter.v1");
                if (!legacyMutex_ || GetLastError() == ERROR_ALREADY_EXISTS)
                    throw std::runtime_error("Another Mayasaba instance is already using its store");
            }
            int status = sqlite3_open16(file.c_str(), &db_);
#else
            int status = sqlite3_open(file.string().c_str(), &db_);
#endif
            if (status != SQLITE_OK) throw std::runtime_error(db_ ? sqlite3_errmsg(db_) : "SQLite open failed");
            sqlite3_busy_timeout(db_, 5000);
            int schemaVersion{};
            {
                Statement version(db_, "PRAGMA user_version;");
                Check(sqlite3_step(version.value), db_);
                schemaVersion = sqlite3_column_int(version.value, 0);
                if (schemaVersion > 3) throw std::runtime_error("Store schema is newer than this application; no migration was attempted");
            }
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
            // Migration: ensure last_opened_at and filesystem_identity exist on projects, and prebinding_draft exists on ui_state
            bool hasLastOpened = false;
            bool hasFilesystemIdentity = false;
            bool hasDomain = false;
            bool hasProjectsTable = false;
            {
                Statement info(db_, "PRAGMA table_info(projects);");
                while (sqlite3_step(info.value) == SQLITE_ROW)
                {
                    std::string colName = Text(info.value, 1);
                    hasProjectsTable = true;
                    if (colName == "last_opened_at") hasLastOpened = true;
                    if (colName == "filesystem_identity") hasFilesystemIdentity = true;
                    if (colName == "domain_id") hasDomain = true;
                }
            }
            bool hasPrebind = false;
            {
                Statement info(db_, "PRAGMA table_info(ui_state);");
                while (sqlite3_step(info.value) == SQLITE_ROW)
                {
                    if (Text(info.value, 1) == "prebinding_draft") { hasPrebind = true; break; }
                }
            }

            // If migration is needed, perform consistent verified pre-migration online backup first
            if (hasProjectsTable && schemaVersion < 3)
            {
                auto backupPath = file.wstring() + L".pre_migration.bak";
                if (std::filesystem::exists(backupPath)) backupPath += L".v2." + Utf8ToWide(NewUuid());
                sqlite3* pDest = nullptr;
#ifdef _WIN32
                int openRc = sqlite3_open16(backupPath.c_str(), &pDest);
#else
                int openRc = sqlite3_open(std::string(backupPath.begin(), backupPath.end()).c_str(), &pDest);
#endif
                if (openRc != SQLITE_OK || !pDest)
                {
                    if (pDest) sqlite3_close(pDest);
                    throw std::runtime_error("Pre-migration backup admission failed: could not open backup destination");
                }
                sqlite3_backup* pBackup = sqlite3_backup_init(pDest, "main", db_, "main");
                if (!pBackup)
                {
                    sqlite3_close(pDest);
                    throw std::runtime_error("Pre-migration backup admission failed: sqlite3_backup_init returned null");
                }
                int stepRc = sqlite3_backup_step(pBackup, -1);
                int finishRc = sqlite3_backup_finish(pBackup);
                if (stepRc != SQLITE_DONE || finishRc != SQLITE_OK)
                {
                    sqlite3_close(pDest);
                    throw std::runtime_error("Pre-migration backup admission failed: backup step/finish did not complete successfully");
                }
                // Verify backup database integrity before admitting migration
                {
                    Statement check(pDest, "PRAGMA integrity_check;");
                    int checkRc = sqlite3_step(check.value);
                    if (checkRc != SQLITE_ROW || Text(check.value, 0) != "ok")
                    {
                        sqlite3_close(pDest);
                        throw std::runtime_error("Pre-migration backup admission failed: backup integrity check returned error");
                    }
                }
                sqlite3_close(pDest);
            }
            Exec("BEGIN IMMEDIATE;");
            Exec("CREATE TABLE IF NOT EXISTS projects(id TEXT PRIMARY KEY, display_name TEXT NOT NULL, canonical_path TEXT NOT NULL,"
                 "draft TEXT NOT NULL DEFAULT '', closed INTEGER NOT NULL DEFAULT 0, version INTEGER NOT NULL DEFAULT 0);");
            Exec("CREATE TABLE IF NOT EXISTS contributions(row_id INTEGER PRIMARY KEY AUTOINCREMENT, project_id TEXT NOT NULL REFERENCES projects(id),"
                 "submission_id TEXT NOT NULL UNIQUE, body TEXT NOT NULL, created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));");
            Exec("CREATE INDEX IF NOT EXISTS contribution_project ON contributions(project_id,row_id);");
            Exec("CREATE TABLE IF NOT EXISTS pending_submissions(project_id TEXT PRIMARY KEY REFERENCES projects(id),submission_id TEXT UNIQUE NOT NULL,body TEXT NOT NULL);");
            Exec("CREATE TABLE IF NOT EXISTS ui_state(singleton INTEGER PRIMARY KEY CHECK(singleton=1), last_project TEXT NOT NULL);");
            if (!hasLastOpened)
            {
                Exec("ALTER TABLE projects ADD COLUMN last_opened_at TEXT;");
            }
            if (!hasFilesystemIdentity)
            {
                Exec("ALTER TABLE projects ADD COLUMN filesystem_identity TEXT;");
                Exec("UPDATE projects SET filesystem_identity = id WHERE filesystem_identity IS NULL OR filesystem_identity = '';");
            }
            if (!hasPrebind)
            {
                Exec("ALTER TABLE ui_state ADD COLUMN prebinding_draft TEXT NOT NULL DEFAULT '';");
            }
            if (!hasDomain)
            {
                Exec("ALTER TABLE projects ADD COLUMN domain_id TEXT;");
                Statement rows(db_, "SELECT id FROM projects;");
                std::vector<std::string> keys;
                int rc{};
                while ((rc = sqlite3_step(rows.value)) == SQLITE_ROW) keys.push_back(Text(rows.value, 0));
                Check(rc, db_);
                for (auto const& key : keys)
                {
                    Statement mapping(db_, "UPDATE projects SET domain_id=?2 WHERE id=?1;");
                    Bind(mapping.value, 1, key); Bind(mapping.value, 2, IsUuid(key) ? key : NewUuid());
                    Check(sqlite3_step(mapping.value), db_);
                }
            }
            Exec("CREATE UNIQUE INDEX IF NOT EXISTS project_domain ON projects(domain_id);");
            Exec("CREATE UNIQUE INDEX IF NOT EXISTS project_filesystem ON projects(filesystem_identity);");
            Exec("CREATE TABLE IF NOT EXISTS selection_receipts(operation_id TEXT PRIMARY KEY, request_bytes TEXT NOT NULL, project_id TEXT NOT NULL REFERENCES projects(id), transferred INTEGER NOT NULL, committed_at TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%fZ','now')));");
            Exec("CREATE TABLE IF NOT EXISTS selection_events(operation_id TEXT PRIMARY KEY REFERENCES selection_receipts(operation_id), owner TEXT NOT NULL CHECK(owner='PROJECT_SERVICE'), event_type TEXT NOT NULL CHECK(event_type='ProjectSelected'), project_id TEXT NOT NULL REFERENCES projects(id));");

            Exec("CREATE TABLE IF NOT EXISTS directory_operations("
                 "operation_id TEXT PRIMARY KEY, parent_path TEXT NOT NULL, folder_name TEXT NOT NULL,"
                 "target_path TEXT NOT NULL, state TEXT NOT NULL, observed_identity TEXT,"
                 "error_message TEXT, created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
                 "updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));");

            Exec("CREATE TABLE IF NOT EXISTS draft_attachments("
                 "attachment_id TEXT PRIMARY KEY, project_id TEXT NOT NULL REFERENCES projects(id),"
                 "file_name TEXT NOT NULL, file_size INTEGER NOT NULL, sha256 TEXT NOT NULL,"
                 "detected_type TEXT NOT NULL, staged_path TEXT NOT NULL, source_path TEXT NOT NULL,"
                 "status TEXT NOT NULL, error_message TEXT NOT NULL DEFAULT '',"
                 "created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));");
            Exec("CREATE INDEX IF NOT EXISTS draft_attachment_project ON draft_attachments(project_id);");

            Exec("CREATE TABLE IF NOT EXISTS attachments("
                 "attachment_id TEXT PRIMARY KEY, project_id TEXT NOT NULL REFERENCES projects(id),"
                 "submission_id TEXT NOT NULL REFERENCES contributions(submission_id),"
                 "file_name TEXT NOT NULL, file_size INTEGER NOT NULL, sha256 TEXT NOT NULL,"
                 "detected_type TEXT NOT NULL, storage_path TEXT NOT NULL, source_path TEXT NOT NULL,"
                 "status TEXT NOT NULL, error_message TEXT NOT NULL DEFAULT '',"
                 "created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));");
            Exec("CREATE INDEX IF NOT EXISTS attachment_submission ON attachments(submission_id);");
            Exec("CREATE INDEX IF NOT EXISTS attachment_project ON attachments(project_id);");
            if(schemaVersion<3)
            {
                auto protectColumn=[&](char const* select,char const* update)
                {
                    std::vector<std::pair<std::string,std::string>> rows;
                    {Statement q(db_,select);int rc{};while((rc=sqlite3_step(q.value))==SQLITE_ROW)
                        rows.emplace_back(Text(q.value,0),Text(q.value,1));Check(rc,db_);}
                    for(auto const& row:rows){Statement q(db_,update);Bind(q.value,1,row.first);
                        Bind(q.value,2,ProtectDraft(ReadDraft(row.second)));Check(sqlite3_step(q.value),db_);}
                };
                protectColumn("SELECT id,draft FROM projects;","UPDATE projects SET draft=?2 WHERE id=?1;");
                protectColumn("SELECT singleton,prebinding_draft FROM ui_state;","UPDATE ui_state SET prebinding_draft=?2 WHERE singleton=?1;");
                protectColumn("SELECT project_id,body FROM pending_submissions;","UPDATE pending_submissions SET body=?2 WHERE project_id=?1;");
                protectColumn("SELECT operation_id,request_bytes FROM selection_receipts;","UPDATE selection_receipts SET request_bytes=?2 WHERE operation_id=?1;");
            }
            Exec("PRAGMA user_version=3;");
            Exec("COMMIT;");
            ReconcileCrashOperations();
        }
        catch (...)
        {
            if (db_) sqlite3_close(db_);
            db_ = nullptr;
#ifdef _WIN32
            if (instanceMutex_) { CloseHandle(instanceMutex_); instanceMutex_ = nullptr; }
            if (legacyMutex_) CloseHandle(legacyMutex_);
            if (identityHandle_ != INVALID_HANDLE_VALUE) CloseHandle(identityHandle_);
#endif
            throw;
        }
    }
    Storage::~Storage()
    {
        if (db_) sqlite3_close(db_);
#ifdef _WIN32
        if (instanceMutex_) CloseHandle(instanceMutex_);
        if (legacyMutex_) CloseHandle(legacyMutex_);
        if (identityHandle_ != INVALID_HANDLE_VALUE) CloseHandle(identityHandle_);
#endif
    }
    std::string Storage::BindProject(std::string const& id, std::string const& name, std::string const& path, std::string const& filesystemIdentity)
    {
        std::lock_guard lock(mutex_);
        Exec("BEGIN IMMEDIATE;");
        try
        {
            std::string actualId = id;
            std::string fsId = filesystemIdentity.empty() ? id : filesystemIdentity;

            // If a project already exists with this filesystem_identity, preserve its domain id!
            {
                Statement find(db_, "SELECT id FROM projects WHERE filesystem_identity = ?1;");
                Bind(find.value, 1, fsId);
                if (sqlite3_step(find.value) == SQLITE_ROW)
                {
                    actualId = Text(find.value, 0);
                }
            }

            Statement p(db_, "INSERT INTO projects(id, display_name, canonical_path, last_opened_at, filesystem_identity, domain_id) "
                             "VALUES(?1, ?2, ?3, strftime('%Y-%m-%dT%H:%M:%fZ','now'), ?4, ?5) "
                             "ON CONFLICT(id) DO UPDATE SET display_name=excluded.display_name,"
                             "canonical_path=excluded.canonical_path,"
                             "last_opened_at=strftime('%Y-%m-%dT%H:%M:%fZ','now'),"
                             "filesystem_identity=excluded.filesystem_identity;");
            Bind(p.value, 1, actualId);
            Bind(p.value, 2, name);
            Bind(p.value, 3, path);
            Bind(p.value, 4, fsId);
            Bind(p.value, 5, IsUuid(actualId) ? actualId : NewUuid());
            Check(sqlite3_step(p.value), db_);

            Statement u(db_, "INSERT INTO ui_state(singleton, last_project) VALUES(1, ?1)"
                             "ON CONFLICT(singleton) DO UPDATE SET last_project=excluded.last_project;");
            Bind(u.value, 1, actualId);
            Check(sqlite3_step(u.value), db_);

            Exec("COMMIT;");
            return actualId;
        }
        catch (...)
        {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            throw;
        }
    }
    std::optional<ProjectRecord> Storage::Project(std::string const& id)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT id, display_name, canonical_path, draft, closed, version, coalesce(last_opened_at,''), coalesce(filesystem_identity,''), domain_id FROM projects WHERE id=?1;");
        Bind(q.value, 1, id);
        int rc = sqlite3_step(q.value);
        if (rc == SQLITE_DONE) return std::nullopt;
        Check(rc, db_);
        return ProjectRecord{Text(q.value, 0), Text(q.value, 1), Text(q.value, 2), ReadDraft(Text(q.value, 3)),
            sqlite3_column_int(q.value, 4) != 0, sqlite3_column_int(q.value, 5), Text(q.value, 6), Text(q.value, 7), Text(q.value, 8)};
    }
    std::optional<ProjectRecord> Storage::ProjectByFilesystemIdentity(std::string const& fsId)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT id, display_name, canonical_path, draft, closed, version, coalesce(last_opened_at,''), coalesce(filesystem_identity,''), domain_id FROM projects WHERE filesystem_identity=?1 OR id=?1;");
        Bind(q.value, 1, fsId);
        int rc = sqlite3_step(q.value);
        if (rc == SQLITE_DONE) return std::nullopt;
        Check(rc, db_);
        return ProjectRecord{Text(q.value, 0), Text(q.value, 1), Text(q.value, 2), ReadDraft(Text(q.value, 3)),
            sqlite3_column_int(q.value, 4) != 0, sqlite3_column_int(q.value, 5), Text(q.value, 6), Text(q.value, 7), Text(q.value, 8)};
    }
    bool Storage::HasAnyProjects()
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT count(*) FROM projects;");
        int rc = sqlite3_step(q.value);
        Check(rc, db_);
        return sqlite3_column_int(q.value, 0) > 0;
    }
    std::vector<ProjectRecord> Storage::RecentProjects(size_t limit)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT id, display_name, canonical_path, draft, closed, version, coalesce(last_opened_at, ''), coalesce(filesystem_identity, ''), domain_id "
                         "FROM projects "
                         "ORDER BY (CASE WHEN last_opened_at IS NOT NULL AND last_opened_at != '' THEN 0 ELSE 1 END), "
                         "last_opened_at DESC, id ASC LIMIT ?1;");
        sqlite3_bind_int(q.value, 1, static_cast<int>(limit));
        std::vector<ProjectRecord> result;
        while (sqlite3_step(q.value) == SQLITE_ROW)
        {
            result.push_back({
                Text(q.value, 0),
                Text(q.value, 1),
                Text(q.value, 2),
                ReadDraft(Text(q.value, 3)),
                sqlite3_column_int(q.value, 4) != 0,
                sqlite3_column_int(q.value, 5),
                Text(q.value, 6),
                Text(q.value, 7), Text(q.value, 8)
            });
        }
        return result;
    }
    CrossProjectRecentProjectsResult Storage::QueryRecentProjects(CrossProjectRecentProjectsQuery const& query)
    {
        std::lock_guard lock(mutex_);
        CrossProjectRecentProjectsResult result;
        try
        {
            if (query.pageSize < 1 || query.pageSize > 100 || query.offset > 1000000)
                throw std::runtime_error("Recent-project query exceeds its bounds");
            Statement q(db_, "SELECT id, display_name, canonical_path, draft, closed, version, "
                             "coalesce(last_opened_at, ''), coalesce(filesystem_identity, ''), domain_id "
                             "FROM projects "
                             "ORDER BY (CASE WHEN last_opened_at IS NOT NULL AND last_opened_at != '' THEN 0 ELSE 1 END), "
                             "last_opened_at DESC, id ASC LIMIT ?1 OFFSET ?2;");
            sqlite3_bind_int(q.value, 1, static_cast<int>(query.pageSize + 1));
            sqlite3_bind_int(q.value, 2, static_cast<int>(query.offset));
            int rc{};
            while ((rc = sqlite3_step(q.value)) == SQLITE_ROW)
            {
                result.items.push_back({
                    Text(q.value, 0),
                    Text(q.value, 1),
                    Text(q.value, 2),
                    ReadDraft(Text(q.value, 3)),
                    sqlite3_column_int(q.value, 4) != 0,
                    sqlite3_column_int(q.value, 5),
                    Text(q.value, 6),
                    Text(q.value, 7), Text(q.value, 8)
                });
            }
            Check(rc, db_);
            result.hasMore = result.items.size() > query.pageSize;
            if (result.hasMore) result.items.pop_back();
            result.isComplete = true;
        }
        catch (std::exception const& e)
        {
            result.isComplete = false;
            result.errorMessage = e.what();
        }
        return result;
    }
    std::optional<ProjectRecord> Storage::ProjectByDomainId(std::string const& id)
    {
        std::string key;
        {
            std::lock_guard lock(mutex_);
            Statement q(db_, "SELECT id FROM projects WHERE domain_id=?1;");
            Bind(q.value, 1, id);
            int rc = sqlite3_step(q.value); Check(rc, db_);
            if (rc == SQLITE_DONE) return std::nullopt;
            key = Text(q.value, 0);
        }
        return Project(key);
    }

    SelectionReceipt Storage::CommitSelection(SelectionCommit const& c)
    {
        std::lock_guard lock(mutex_);
        if (!IsUuid(c.operationId) || !IsUuid(c.proposedId) || c.requestBytes.size() > 262144)
            throw std::runtime_error("Invalid selection identity or payload bound");
        auto read = [&](std::string const& key) -> ProjectRecord
        {
            Statement q(db_, "SELECT id,display_name,canonical_path,draft,closed,version,coalesce(last_opened_at,''),filesystem_identity,domain_id FROM projects WHERE id=?1;");
            Bind(q.value, 1, key);
            if (sqlite3_step(q.value) != SQLITE_ROW) throw std::runtime_error("Project record unavailable");
            return {Text(q.value,0),Text(q.value,1),Text(q.value,2),ReadDraft(Text(q.value,3)),sqlite3_column_int(q.value,4)!=0,
                sqlite3_column_int(q.value,5),Text(q.value,6),Text(q.value,7),Text(q.value,8)};
        };
        Exec("BEGIN IMMEDIATE;");
        try
        {
            {
                Statement receipt(db_, "SELECT request_bytes,project_id,transferred FROM selection_receipts WHERE operation_id=?1;");
                Bind(receipt.value,1,c.operationId);
                int rc = sqlite3_step(receipt.value); Check(rc,db_);
                if (rc == SQLITE_ROW)
                {
                    if (ReadDraft(Text(receipt.value,0)) != c.requestBytes) throw std::runtime_error("Selection identity reused with changed input");
                    SelectionReceipt result; result.project = read(Text(receipt.value,1));
                    if(result.project.filesystemIdentity!=c.filesystemIdentity)
                        throw std::runtime_error("Selection receipt refers to a different folder identity");
                    result.duplicate = true; result.transferred = sqlite3_column_int(receipt.value,2)!=0;
                    Exec("COMMIT;"); return result;
                }
            }
            std::string key;
            {
                Statement find(db_, "SELECT id FROM projects WHERE filesystem_identity=?1;");
                Bind(find.value,1,c.filesystemIdentity);
                int rc = sqlite3_step(find.value); Check(rc,db_);
                if (rc == SQLITE_ROW) key=Text(find.value,0);
            }
            if (!c.expectedProject.empty() && key != c.expectedProject)
                throw std::runtime_error("Folder identity differs; original project history is preserved. Use Locate folder");
            ProjectRecord project;
            if (!key.empty())
            {
                project=read(key);
                if (c.expectedVersion >= 0 && project.version != c.expectedVersion)
                    throw std::runtime_error("Project changed during selection; select it again");
            }
            else { key=c.proposedId; project={key,c.name,c.path,"",false,0,"",c.filesystemIdentity,c.proposedId}; }
            std::string prebind;
            {
                Statement q(db_, "SELECT prebinding_draft FROM ui_state WHERE singleton=1;");
                int rc=sqlite3_step(q.value); Check(rc,db_);
                if(rc==SQLITE_ROW) prebind=ReadDraft(Text(q.value,0));
            }
            if (savedDraftRevision_ != requestedDraftRevision_.load()) throw std::runtime_error("Selector draft save is not confirmed");
            bool pending=false;
            {
                Statement q(db_, "SELECT 1 FROM pending_submissions WHERE project_id=?1;"); Bind(q.value,1,key);
                int rc=sqlite3_step(q.value); Check(rc,db_); pending=rc==SQLITE_ROW;
            }
            SelectionReceipt result; result.project=project; result.pending=pending; result.selectorDraft=prebind;
            if (!project.closed && !prebind.empty() && (pending || !project.draft.empty()) && c.draftChoice==DraftChoice::Automatic)
            { result.collision=true; Exec("ROLLBACK;"); return result; }
            if (c.draftChoice==DraftChoice::UseSelector &&
                (project.closed || pending || prebind!=c.expectedSelectorDraft || project.draft!=c.expectedTargetDraft))
                throw std::runtime_error("Drafts changed or target is locked; both drafts were preserved");
            bool transfer=!project.closed && !prebind.empty() && c.draftChoice!=DraftChoice::KeepProject && !pending;
            {
                Statement q(db_, "INSERT INTO projects(id,display_name,canonical_path,draft,closed,version,last_opened_at,filesystem_identity,domain_id) VALUES(?1,?2,?3,?4,0,0,strftime('%Y-%m-%dT%H:%M:%fZ','now'),?5,?6) ON CONFLICT(id) DO UPDATE SET display_name=excluded.display_name,canonical_path=excluded.canonical_path,draft=excluded.draft,version=projects.version+1,last_opened_at=excluded.last_opened_at;");
                Bind(q.value,1,key);Bind(q.value,2,c.name);Bind(q.value,3,c.path);
                Bind(q.value,4,ProtectDraft(transfer?prebind:project.draft));Bind(q.value,5,c.filesystemIdentity);Bind(q.value,6,project.domainId);
                Check(sqlite3_step(q.value),db_);
            }
            {
                Statement q(db_, "INSERT INTO ui_state(singleton,last_project,prebinding_draft) VALUES(1,?1,?2) ON CONFLICT(singleton) DO UPDATE SET last_project=excluded.last_project,prebinding_draft=excluded.prebinding_draft;");
                result.selectorDraft=transfer?(c.draftChoice==DraftChoice::UseSelector?project.draft:""):prebind;
                Bind(q.value,1,key);Bind(q.value,2,ProtectDraft(result.selectorDraft));Check(sqlite3_step(q.value),db_);
            }
            {
                Statement q(db_, "INSERT INTO selection_receipts(operation_id,request_bytes,project_id,transferred) VALUES(?1,?2,?3,?4);");
                Bind(q.value,1,c.operationId);Bind(q.value,2,ProtectDraft(c.requestBytes));Bind(q.value,3,key);sqlite3_bind_int(q.value,4,transfer?1:0);Check(sqlite3_step(q.value),db_);
                Statement e(db_, "INSERT INTO selection_events(operation_id,owner,event_type,project_id) VALUES(?1,'PROJECT_SERVICE','ProjectSelected',?2);");
                Bind(e.value,1,c.operationId);Bind(e.value,2,key);Check(sqlite3_step(e.value),db_);
            }
            if (!c.creationOperation.empty())
            {
                Statement q(db_, "UPDATE directory_operations SET state='BOUND',error_message='',updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE operation_id=?1 AND observed_identity=?2 AND state IN ('CREATED','BOUND');");
                Bind(q.value,1,c.creationOperation);Bind(q.value,2,c.filesystemIdentity);Check(sqlite3_step(q.value),db_);
                if(sqlite3_changes(db_)!=1) throw std::runtime_error("Creation observation no longer matches binding");
            }
            result.project=read(key); result.transferred=transfer;
            Exec("COMMIT;");
            return result;
        }
        catch (...) { sqlite3_exec(db_,"ROLLBACK;",nullptr,nullptr,nullptr); throw; }
    }

    void Storage::TouchProjectOpened(std::string const& id)
    {
        std::lock_guard lock(mutex_);
        Exec("BEGIN IMMEDIATE;");
        try
        {
            Statement q(db_, "UPDATE projects SET last_opened_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=?1;");
            Bind(q.value, 1, id);
            Check(sqlite3_step(q.value), db_);
            Statement u(db_, "INSERT INTO ui_state(singleton, last_project) VALUES(1, ?1) "
                             "ON CONFLICT(singleton) DO UPDATE SET last_project=excluded.last_project;");
            Bind(u.value, 1, id);
            Check(sqlite3_step(u.value), db_);
            Exec("COMMIT;");
        }
        catch (...)
        {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            throw;
        }
    }
    void Storage::RelocateProject(std::string const& id, std::string const& newCanonicalPath, std::string const& newDisplayName)
    {
        std::lock_guard lock(mutex_);
        CheckWriteCapacity();
        Statement q(db_, "UPDATE projects SET canonical_path=?2, display_name=?3 WHERE id=?1;");
        Bind(q.value, 1, id);
        Bind(q.value, 2, newCanonicalPath);
        Bind(q.value, 3, newDisplayName);
        Check(sqlite3_step(q.value), db_);
    }
    void Storage::SavePrebindDraft(std::string const& body)
    {
        SavePrebindDraft(body, ReservePrebindRevision());
    }
    bool Storage::SavePrebindDraft(std::string const& body, uint64_t revision)
    {
        std::lock_guard lock(mutex_);
        CheckWriteCapacity();
        if (revision != requestedDraftRevision_.load()) return false;
        Statement q(db_, "INSERT INTO ui_state(singleton, last_project, prebinding_draft) VALUES(1, '', ?1) "
                         "ON CONFLICT(singleton) DO UPDATE SET prebinding_draft=excluded.prebinding_draft;");
        Bind(q.value, 1, ProtectDraft(body));
        Check(sqlite3_step(q.value), db_);
        savedDraftRevision_ = revision;
        return true;
    }
    std::string Storage::GetPrebindDraft()
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT coalesce(prebinding_draft, '') FROM ui_state WHERE singleton=1;");
        int rc = sqlite3_step(q.value);
        if (rc == SQLITE_DONE) return {};
        Check(rc, db_);
        return ReadDraft(Text(q.value, 0));
    }
    void Storage::ClearPrebindDraft()
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "UPDATE ui_state SET prebinding_draft='' WHERE singleton=1;");
        Check(sqlite3_step(q.value), db_);
    }
    bool Storage::TransferPrebindDraftToProject(std::string const& projectId, bool overwrite)
    {
        std::lock_guard lock(mutex_);
        if (savedDraftRevision_ != requestedDraftRevision_.load()) return false;
        std::string prebind;
        {
            Statement q(db_, "SELECT coalesce(prebinding_draft, '') FROM ui_state WHERE singleton=1;");
            if (sqlite3_step(q.value) == SQLITE_ROW) prebind = ReadDraft(Text(q.value, 0));
        }
        if (prebind.empty()) return true;

        Exec("BEGIN IMMEDIATE;");
        try
        {
            Statement p(db_, "SELECT draft, closed, version FROM projects WHERE id=?1;");
            Bind(p.value, 1, projectId);
            if (sqlite3_step(p.value) != SQLITE_ROW)
            {
                Exec("ROLLBACK;");
                return false;
            }
            bool closed = sqlite3_column_int(p.value, 1) != 0;
            if (closed)
            {
                // Never write draft into closed project!
                Exec("ROLLBACK;");
                return false;
            }
            int currentVersion = sqlite3_column_int(p.value, 2);
            std::string targetDraft = ReadDraft(Text(p.value, 0));

            // Check if project has a pending submission
            {
                Statement pend(db_, "SELECT count(*) FROM pending_submissions WHERE project_id=?1;");
                Bind(pend.value, 1, projectId);
                if (sqlite3_step(pend.value) == SQLITE_ROW && sqlite3_column_int(pend.value, 0) > 0)
                {
                    // Locked due to unresolved pending submission
                    Exec("ROLLBACK;");
                    return false;
                }
            }

            if (!targetDraft.empty() && !overwrite)
            {
                // Collision: target already has a draft and overwrite is false
                Exec("ROLLBACK;");
                return false;
            }

            // Atomic version-checked update
            Statement upd(db_, "UPDATE projects SET draft=?2, version=version+1 WHERE id=?1 AND version=?3;");
            Bind(upd.value, 1, projectId);
            Bind(upd.value, 2, ProtectDraft(prebind));
            sqlite3_bind_int(upd.value, 3, currentVersion);
            Check(sqlite3_step(upd.value), db_);
            if (sqlite3_changes(db_) != 1)
            {
                // Version mismatch or concurrent edit
                Exec("ROLLBACK;");
                return false;
            }

            // Only clear prebinding draft AFTER target draft update succeeds
            Statement clr(db_, "UPDATE ui_state SET prebinding_draft='' WHERE singleton=1;");
            Check(sqlite3_step(clr.value), db_);
            Exec("COMMIT;");
            auto expected = savedDraftRevision_;
            if (requestedDraftRevision_.compare_exchange_strong(expected, expected + 1))
                ++savedDraftRevision_;
            return true;
        }
        catch (...)
        {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            throw;
        }
    }

    void Storage::RecordDirectoryIntent(std::string const& opId, std::string const& parentPath, std::string const& folderName, std::string const& targetPath)
    {
        std::lock_guard lock(mutex_);
        CheckWriteCapacity();
        Statement q(db_, "INSERT INTO directory_operations(operation_id, parent_path, folder_name, target_path, state) "
                         "VALUES(?1, ?2, ?3, ?4, 'INTENDED');");
        Bind(q.value, 1, opId);
        Bind(q.value, 2, parentPath);
        Bind(q.value, 3, folderName);
        Bind(q.value, 4, targetPath);
        Check(sqlite3_step(q.value), db_);
    }

    void Storage::RecordDirectoryCreated(std::string const& opId, std::string const& observedIdentity)
    {
        std::lock_guard lock(mutex_);
        CheckWriteCapacity();
        Statement q(db_, "UPDATE directory_operations SET state='CREATED', observed_identity=?2, "
                         "updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE operation_id=?1;");
        Bind(q.value, 1, opId);
        Bind(q.value, 2, observedIdentity);
        Check(sqlite3_step(q.value), db_);
    }

    void Storage::RecordDirectoryBound(std::string const& opId)
    {
        std::lock_guard lock(mutex_);
        CheckWriteCapacity();
        Statement q(db_, "UPDATE directory_operations SET state='BOUND', "
                         "updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE operation_id=?1;");
        Bind(q.value, 1, opId);
        Check(sqlite3_step(q.value), db_);
    }

    void Storage::RecordDirectoryFailed(std::string const& opId, std::string const& errorMessage)
    {
        std::lock_guard lock(mutex_);
        CheckWriteCapacity();
        Statement q(db_, "UPDATE directory_operations SET state='FAILED', error_message=?2, "
                         "updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE operation_id=?1;");
        Bind(q.value, 1, opId);
        Bind(q.value, 2, errorMessage);
        Check(sqlite3_step(q.value), db_);
    }

    std::optional<DirectoryOperationRecord> Storage::GetDirectoryOperation(std::string const& opId)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT operation_id, parent_path, folder_name, target_path, state, "
                         "coalesce(observed_identity,''), coalesce(error_message,''), created_at, updated_at "
                         "FROM directory_operations WHERE operation_id=?1;");
        Bind(q.value, 1, opId);
        if (sqlite3_step(q.value) != SQLITE_ROW) return std::nullopt;
        return DirectoryOperationRecord{
            Text(q.value, 0), Text(q.value, 1), Text(q.value, 2), ReadDraft(Text(q.value, 3)),
            Text(q.value, 4), Text(q.value, 5), Text(q.value, 6), Text(q.value, 7), Text(q.value, 8)
        };
    }

    std::vector<DirectoryOperationRecord> Storage::UnreconciledDirectoryOperations()
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT operation_id, parent_path, folder_name, target_path, state, "
                         "coalesce(observed_identity,''), coalesce(error_message,''), created_at, updated_at "
                         "FROM directory_operations WHERE state NOT IN ('BOUND', 'FAILED') ORDER BY created_at ASC;");
        std::vector<DirectoryOperationRecord> result;
        while (sqlite3_step(q.value) == SQLITE_ROW)
        {
            result.push_back({
                Text(q.value, 0), Text(q.value, 1), Text(q.value, 2), ReadDraft(Text(q.value, 3)),
                Text(q.value, 4), Text(q.value, 5), Text(q.value, 6), Text(q.value, 7), Text(q.value, 8)
            });
        }
        return result;
    }

    void Storage::RecordDirectoryUncertain(std::string const& opId, std::string const& errorMessage)
    {
        std::lock_guard lock(mutex_);
        CheckWriteCapacity();
        Statement q(db_, "UPDATE directory_operations SET error_message=?2, updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE operation_id=?1 AND state NOT IN ('BOUND','FAILED');");
        Bind(q.value, 1, opId); Bind(q.value, 2, errorMessage);
        Check(sqlite3_step(q.value), db_);
    }
    void Storage::ReconcileCrashOperations()
    {
        // Storage does not originate project binding authority. Keep observations
        // for an explicit Project-service recovery; restart must not adopt roots.
        for (auto const& op : UnreconciledDirectoryOperations())
            RecordDirectoryUncertain(op.operationId, op.state == "INTENDED" ?
                "PHYSICAL_OUTCOME_UNKNOWN" : "CREATED_DIRECTORY_REQUIRES_EXPLICIT_SELECTION");
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
        CheckWriteCapacity();
        Statement q(db_, "UPDATE projects SET draft=?2 WHERE id=?1;");
        Bind(q.value,1,id); Bind(q.value,2,ProtectDraft(body));
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
        return PendingSubmission{Text(q.value,0),ReadDraft(Text(q.value,1))};
    }
    std::string Storage::StageSubmission(std::string const& id, std::string const& proposed, std::string const& body)
    {
        if (id.empty() || proposed.empty()) throw std::runtime_error("Invalid submission");
        std::lock_guard lock(mutex_);
        Exec("BEGIN IMMEDIATE;");
        try
        {
            if (body.empty())
            {
                // Must have at least one ready draft attachment if body is empty
                Statement checkDrafts(db_, "SELECT count(*) FROM draft_attachments WHERE project_id=?1 AND status='READY';");
                Bind(checkDrafts.value, 1, id);
                if (sqlite3_step(checkDrafts.value) != SQLITE_ROW || sqlite3_column_int(checkDrafts.value, 0) == 0)
                {
                    throw std::runtime_error("Submission cannot be empty: must provide text or attachments");
                }
            }
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
                    if(ReadDraft(Text(q.value,1))!=body)
                        throw std::runtime_error("Different source text while prior submission is unresolved");
                    winner=Text(q.value,0);
                }
                else
                {
                    Statement add(db_, "INSERT INTO pending_submissions(project_id,submission_id,body) VALUES(?1,?2,?3);");
                    Bind(add.value,1,id);Bind(add.value,2,proposed);Bind(add.value,3,ProtectDraft(body));
                    Check(sqlite3_step(add.value),db_);
                    Statement draft(db_, "UPDATE projects SET draft=?2 WHERE id=?1;");
                    Bind(draft.value,1,id);Bind(draft.value,2,ProtectDraft(body));
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
        CheckWriteCapacity();
        Statement q(db_, "UPDATE projects SET closed=?3,version=version+1 WHERE id=?1 AND version=?2;");
        Bind(q.value,1,id);
        Check(sqlite3_bind_int(q.value,2,version),db_);
        Check(sqlite3_bind_int(q.value,3,closed?1:0),db_);
        Check(sqlite3_step(q.value),db_);
        return sqlite3_changes(db_)==1;
    }
    SaveDisposition Storage::SaveContribution(std::string const& id, std::string const& submission, std::string const& body)
    {
        return SaveContribution(id, submission, body, {});
    }

    SaveDisposition Storage::SaveContribution(std::string const& id, std::string const& submission, std::string const& body, std::vector<AttachmentRecord> const& attachments)
    {
        if (submission.empty() || (body.empty() && attachments.empty())) return SaveDisposition::Conflict;
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
                    if(Text(pending.value,0)!=submission||ReadDraft(Text(pending.value,1))!=body)
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
                // Admission boundary check for all attachments: project association, size limits, and cryptographic verification
                uint64_t aggregateBytes = 0;
                for (auto const& att : attachments)
                {
                    if ((!att.projectId.empty() && att.projectId != id) ||
                        (!att.submissionId.empty() && att.submissionId != submission))
                    {
                        Exec("ROLLBACK;"); return SaveDisposition::Conflict;
                    }
                    if (att.fileSize == 0 || att.fileSize > 100ULL * 1024ULL * 1024ULL) // 100 MiB limit
                    {
                        Exec("ROLLBACK;"); return SaveDisposition::Conflict;
                    }
                    if (att.sha256.size() != 64)
                    {
                        Exec("ROLLBACK;"); return SaveDisposition::Conflict;
                    }
                    aggregateBytes += att.fileSize;
                    if (aggregateBytes > 500ULL * 1024ULL * 1024ULL) // 500 MiB aggregate limit
                    {
                        Exec("ROLLBACK;"); return SaveDisposition::Conflict;
                    }

#ifdef _WIN32
                    // Verify the immutable physical file bytes on disk at final admission
                    std::filesystem::path stagedP = Utf8ToWide(att.storagePath);
                    std::error_code ec;
                    if (!std::filesystem::exists(stagedP, ec) || std::filesystem::is_directory(stagedP, ec))
                    {
                        Exec("ROLLBACK;"); return SaveDisposition::Conflict;
                    }
                    auto actualDiskSize = std::filesystem::file_size(stagedP, ec);
                    if (ec || actualDiskSize != att.fileSize)
                    {
                        Exec("ROLLBACK;"); return SaveDisposition::Conflict;
                    }

                    // Cryptographic digest verification
                    std::string computedHash = ComputeFileSha256(stagedP.wstring());
                    if (computedHash.empty() || computedHash != att.sha256)
                    {
                        Exec("ROLLBACK;"); return SaveDisposition::Conflict;
                    }
#endif
                }

                Statement p(db_, "INSERT INTO contributions(project_id,submission_id,body) VALUES(?1,?2,?3);");
                Bind(p.value,1,id); Bind(p.value,2,submission); Bind(p.value,3,body);
                Check(sqlite3_step(p.value),db_);

                // Atomically link verified attachments to this submission
                for (auto const& att : attachments)
                {
                    Statement ins(db_, "INSERT INTO attachments(attachment_id,project_id,submission_id,file_name,file_size,sha256,detected_type,storage_path,source_path,status,error_message) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11);");
                    Bind(ins.value,1,att.id);
                    Bind(ins.value,2,id);
                    Bind(ins.value,3,submission);
                    Bind(ins.value,4,att.fileName);
                    sqlite3_bind_int64(ins.value,5,static_cast<sqlite3_int64>(att.fileSize));
                    Bind(ins.value,6,att.sha256);
                    Bind(ins.value,7,att.detectedType);
                    Bind(ins.value,8,att.storagePath);
                    Bind(ins.value,9,att.sourcePath);
                    Bind(ins.value,10,"COMMITTED");
                    Bind(ins.value,11,att.errorMessage);
                    Check(sqlite3_step(ins.value),db_);
                }
            }
            {
                Statement p(db_, "UPDATE projects SET draft='' WHERE id=?1;");
                Bind(p.value,1,id);Check(sqlite3_step(p.value),db_);
            }
            {
                Statement clrDraft(db_, "DELETE FROM draft_attachments WHERE project_id=?1;");
                Bind(clrDraft.value,1,id);Check(sqlite3_step(clrDraft.value),db_);
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
        {
            Contribution c{Text(p.value,0),Text(p.value,1),Text(p.value,2)};
            Statement att(db_, "SELECT attachment_id,project_id,submission_id,file_name,file_size,sha256,detected_type,storage_path,source_path,status,error_message,created_at FROM attachments WHERE submission_id=?1 ORDER BY rowid;");
            Bind(att.value,1,c.submissionId);
            int attRc=SQLITE_OK;
            while ((attRc=sqlite3_step(att.value))==SQLITE_ROW)
            {
                c.attachments.push_back({
                    Text(att.value,0),
                    Text(att.value,1),
                    Text(att.value,2),
                    Text(att.value,3),
                    static_cast<uint64_t>(sqlite3_column_int64(att.value,4)),
                    Text(att.value,5),
                    Text(att.value,6),
                    Text(att.value,7),
                    Text(att.value,8),
                    Text(att.value,9),
                    Text(att.value,10),
                    Text(att.value,11)
                });
            }
            result.push_back(std::move(c));
        }
        Check(rc,db_);
        return result;
    }

    void Storage::SaveDraftAttachment(std::string const& projectId, AttachmentRecord const& att)
    {
        std::lock_guard lock(mutex_);
        CheckWriteCapacity();
        Statement q(db_, "INSERT INTO draft_attachments(attachment_id,project_id,file_name,file_size,sha256,detected_type,staged_path,source_path,status,error_message) "
                         "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10) "
                         "ON CONFLICT(attachment_id) DO UPDATE SET file_size=excluded.file_size,sha256=excluded.sha256,staged_path=excluded.staged_path,status=excluded.status,error_message=excluded.error_message;");
        Bind(q.value,1,att.id);
        Bind(q.value,2,projectId);
        Bind(q.value,3,att.fileName);
        sqlite3_bind_int64(q.value,4,static_cast<sqlite3_int64>(att.fileSize));
        Bind(q.value,5,att.sha256);
        Bind(q.value,6,att.detectedType);
        Bind(q.value,7,att.storagePath);
        Bind(q.value,8,att.sourcePath);
        Bind(q.value,9,att.status);
        Bind(q.value,10,att.errorMessage);
        Check(sqlite3_step(q.value),db_);
    }

    void Storage::RemoveDraftAttachment(std::string const& projectId, std::string const& attachmentId)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "DELETE FROM draft_attachments WHERE project_id=?1 AND attachment_id=?2;");
        Bind(q.value,1,projectId);
        Bind(q.value,2,attachmentId);
        Check(sqlite3_step(q.value),db_);
    }

    void Storage::ClearDraftAttachments(std::string const& projectId)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "DELETE FROM draft_attachments WHERE project_id=?1;");
        Bind(q.value,1,projectId);
        Check(sqlite3_step(q.value),db_);
    }

    std::vector<AttachmentRecord> Storage::GetDraftAttachments(std::string const& projectId)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT attachment_id,project_id,'',file_name,file_size,sha256,detected_type,staged_path,source_path,status,error_message,created_at FROM draft_attachments WHERE project_id=?1 ORDER BY rowid;");
        Bind(q.value,1,projectId);
        std::vector<AttachmentRecord> result;
        int rc=SQLITE_OK;
        while ((rc=sqlite3_step(q.value))==SQLITE_ROW)
        {
            result.push_back({
                Text(q.value,0),
                Text(q.value,1),
                Text(q.value,2),
                Text(q.value,3),
                static_cast<uint64_t>(sqlite3_column_int64(q.value,4)),
                Text(q.value,5),
                Text(q.value,6),
                Text(q.value,7),
                Text(q.value,8),
                Text(q.value,9),
                Text(q.value,10),
                Text(q.value,11)
            });
        }
        Check(rc,db_);
        return result;
    }

    std::vector<AttachmentRecord> Storage::GetAttachmentsForContribution(std::string const& submissionId)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT attachment_id,project_id,submission_id,file_name,file_size,sha256,detected_type,storage_path,source_path,status,error_message,created_at FROM attachments WHERE submission_id=?1 ORDER BY rowid;");
        Bind(q.value,1,submissionId);
        std::vector<AttachmentRecord> result;
        int rc=SQLITE_OK;
        while ((rc=sqlite3_step(q.value))==SQLITE_ROW)
        {
            result.push_back({
                Text(q.value,0),
                Text(q.value,1),
                Text(q.value,2),
                Text(q.value,3),
                static_cast<uint64_t>(sqlite3_column_int64(q.value,4)),
                Text(q.value,5),
                Text(q.value,6),
                Text(q.value,7),
                Text(q.value,8),
                Text(q.value,9),
                Text(q.value,10),
                Text(q.value,11)
            });
        }
        Check(rc,db_);
        return result;
    }

    std::vector<AttachmentRecord> Storage::GetCommittedAttachmentsForProject(std::string const& projectId)
    {
        std::lock_guard lock(mutex_);
        Statement q(db_, "SELECT attachment_id,project_id,submission_id,file_name,file_size,sha256,detected_type,storage_path,source_path,status,error_message,created_at FROM attachments WHERE project_id=?1 ORDER BY rowid;");
        Bind(q.value,1,projectId);
        std::vector<AttachmentRecord> result;
        int rc=SQLITE_OK;
        while ((rc=sqlite3_step(q.value))==SQLITE_ROW)
        {
            result.push_back({
                Text(q.value,0),
                Text(q.value,1),
                Text(q.value,2),
                Text(q.value,3),
                static_cast<uint64_t>(sqlite3_column_int64(q.value,4)),
                Text(q.value,5),
                Text(q.value,6),
                Text(q.value,7),
                Text(q.value,8),
                Text(q.value,9),
                Text(q.value,10),
                Text(q.value,11)
            });
        }
        Check(rc,db_);
        return result;
    }
}
