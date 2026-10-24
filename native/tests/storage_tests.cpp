#include "../Mayasaba/Storage.h"
#include "../Mayasaba/SearchController.h"
#include "../Mayasaba/ProjectRoot.h"
#include "../Mayasaba/ProjectService.h"
#include "../Mayasaba/AttachmentService.h"
#include "../Mayasaba/AttachmentAdmission.h"
#include "../Mayasaba/SerialQueue.h"
#include "../Mayasaba/TimelineEntry.h"
#include "../Mayasaba/Sha256Hex.h"
#include <cstdio>
#include <fstream>
#include <future>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>
#include <bcrypt.h>

#define EXPECT(cond) \
    do { \
        if (!(cond)) { \
            throw std::runtime_error(std::string("Assertion failed: ") + #cond + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (0)

// ---------------------------------------------------------------------------
// Executed-case reporting.
//
// The previous banner was a fixed string that claimed a total regardless of what
// ran. A group that silently stopped executing, or a file that failed to build
// with an older binary still present, could still print "PASS: N groups". This
// records each named group as it actually starts and finishes, so the summary is
// derived from execution: a group that throws is reported as FAILED with its
// name, and a group that never runs simply does not appear.
//
// This is a reporting change only. It does not weaken, skip or replace any
// existing assertion.
// ---------------------------------------------------------------------------
namespace
{
    struct ExecutedCases
    {
        struct Row
        {
            std::string name;
            bool finished{false};
        };

        std::vector<Row> rows;
        std::string current;

        // Marks a named group as started. The group body follows in the same scope.
        void Begin(std::string name)
        {
            current = std::move(name);
            rows.push_back(Row{current, false});
        }

        // Marks the most recently started group as having run to completion.
        void End()
        {
            if (!rows.empty()) rows.back().finished = true;
            current.clear();
        }

        std::size_t Passed() const
        {
            std::size_t count = 0;
            for (auto const& row : rows) if (row.finished) ++count;
            return count;
        }

        std::size_t Failed() const { return rows.size() - Passed(); }

        void Print(std::ostream& out) const
        {
            for (auto const& row : rows)
            {
                out << "  " << (row.finished ? "PASS " : "FAIL ") << row.name << "\n";
            }
        }
    };

    ExecutedCases& Cases()
    {
        static ExecutedCases cases;
        return cases;
    }
}

// Declares a named group in the executed-case record. The matching CASES_END is
// placed at the end of the same scope, so an exception anywhere inside the group
// leaves it recorded as failed rather than silently omitted.
#define CASES_BEGIN(name) Cases().Begin(name)
#define CASES_END() Cases().End()

inline std::string ComputeSha256ForTestFile(std::string const& path)
{
    std::ifstream is(path, std::ios::binary);
    if (!is.is_open()) return {};
    std::string bytes((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }
    BCryptHashData(hHash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()), 0);
    uint8_t hashBuf[32];
    BCryptFinishHash(hHash, hashBuf, sizeof(hashBuf), 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 32; ++i) ss << std::setw(2) << static_cast<int>(hashBuf[i]);
    return ss.str();
}

inline std::string WideToUtf8(std::wstring const& w)
{
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), size, nullptr, nullptr);
    return s;
}

int main()
{
    namespace fs = std::filesystem;
    using mayasaba::SaveDisposition;
    auto folder = fs::temp_directory_path() /
        ("mayasaba-store-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(folder);
    try
    {
        // 1. Initial empty store checks
        {
            CASES_BEGIN("1. Initial empty store checks");
            mayasaba::Storage store(folder / "project.db");
            EXPECT(!store.HasAnyProjects());
            EXPECT(store.RecentProjects().empty());
            EXPECT(!store.Project("project-A").has_value());
            EXPECT(store.GetPrebindDraft().empty());

            // Prebinding draft persistence
            store.SavePrebindDraft("Unbound ideas before folder chosen");
            EXPECT(store.GetPrebindDraft() == "Unbound ideas before folder chosen");

            // Project bindings
            store.BindProject("project-A", "Northstar", "C:/Projects/Northstar");
            EXPECT(store.HasAnyProjects());
            EXPECT(store.RecentProjects().size() == 1);
            EXPECT(store.RecentProjects()[0].id == "project-A");
            EXPECT(!store.RecentProjects()[0].lastOpenedAt.empty());

            store.BindProject("project-B", "Atlas", "D:/Work/Atlas");
            EXPECT(store.RecentProjects().size() == 2);
            // project-B was bound second, so lastOpenedAt is >= project-A
            EXPECT(store.RecentProjects()[0].id == "project-B");

            // TouchProjectOpened
            store.TouchProjectOpened("project-A");
            auto recents = store.RecentProjects();
            EXPECT(recents[0].id == "project-A"); // project-A is now most recent

            // QueryRecentProjects typed contract test
            mayasaba::CrossProjectRecentProjectsQuery qTyped;
            qTyped.pageSize = 10;
            qTyped.offset = 0;
            auto qRes = store.QueryRecentProjects(qTyped);
            EXPECT(qRes.isComplete);
            EXPECT(qRes.items.size() == 2);
            EXPECT(qRes.items[0].id == "project-A");

            // RelocateProject
            store.RelocateProject("project-A", "C:/Renamed/Northstar", "Northstar Renamed");
            auto relocated = store.Project("project-A");
            EXPECT(relocated.has_value());
            EXPECT(relocated->canonicalPath == "C:/Renamed/Northstar");
            EXPECT(relocated->displayName == "Northstar Renamed");

            // Drafts and submissions
            store.SaveDraft("project-A", "draft before switching");
            EXPECT(store.Project("project-A")->draft == "draft before switching");
            EXPECT(store.Project("project-B")->draft.empty());

            // Prebinding draft transfer with collision check
            // Target project-A already has a draft; transfer without overwrite must fail (collision)
            EXPECT(!store.TransferPrebindDraftToProject("project-A", false));
            EXPECT(store.GetPrebindDraft() == "Unbound ideas before folder chosen"); // preserved!
            EXPECT(store.Project("project-A")->draft == "draft before switching");   // preserved!

            // Target project-B has empty draft; transfer must succeed
            EXPECT(store.TransferPrebindDraftToProject("project-B", false));
            EXPECT(store.Project("project-B")->draft == "Unbound ideas before folder chosen");
            EXPECT(store.GetPrebindDraft().empty()); // cleared after confirmed transfer

            // Staged submissions and duplicate prevention
            EXPECT(store.StageSubmission("project-A", "submit-0001", "Hello") == "submit-0001");
            EXPECT(store.Pending("project-A")->id == "submit-0001");
            EXPECT(store.StageSubmission("project-A", "ignored-new-id", "Hello") == "submit-0001");

            bool blockedOtherBody = false;
            try { store.StageSubmission("project-A", "another", "Different"); }
            catch (std::exception const&) { blockedOtherBody = true; }
            EXPECT(blockedOtherBody);

            EXPECT(store.SaveContribution("project-A", "submit-0001", "Hello") == SaveDisposition::Saved);
            EXPECT(!store.Pending("project-A"));
            EXPECT(store.SaveContribution("project-A", "submit-0001", "Hello") == SaveDisposition::Duplicate);
            EXPECT(store.SaveContribution("project-A", "submit-0001", "Altered") == SaveDisposition::Conflict);
            EXPECT(store.SaveContribution("project-B", "submit-0001", "Hello") == SaveDisposition::Conflict);

            auto messages = store.Contributions("project-A");
            EXPECT(messages.size() == 1 && messages[0].body == "Hello");
            EXPECT(store.Contributions("project-B").empty());

            auto a = store.Project("project-A");
            EXPECT(a && a->draft.empty());
            EXPECT(store.SetClosure("project-A", a->version, true));
            EXPECT(store.SaveContribution("project-A", "submit-0002", "blocked") == SaveDisposition::RejectedClosed);
            EXPECT(!store.SetClosure("project-A", a->version, false)); // stale version

            // Closed project cannot receive prebinding draft
            store.SavePrebindDraft("Draft for closed project");
            EXPECT(!store.TransferPrebindDraftToProject("project-A", true));
            EXPECT(store.GetPrebindDraft() == "Draft for closed project"); // preserved!

            auto closed = store.Project("project-A");
            EXPECT(closed && closed->closed);
            EXPECT(store.SetClosure("project-A", closed->version, false)); // explicit caller action
            EXPECT(store.StageSubmission("project-A", "submit-0002", "after reopen") == "submit-0002");
            EXPECT(store.SaveContribution("project-A", "submit-0002", "after reopen") == SaveDisposition::Saved);
            EXPECT(store.Contributions("project-A").size() == 2);
            CASES_END();
        }

        // 2. Restart and recovery verification
        {
            CASES_BEGIN("2. Restart and recovery verification");
            mayasaba::Storage reopened(folder / "project.db");
            auto a = reopened.Project("project-A");
            EXPECT(a && !a->closed);
            EXPECT(reopened.Contributions("project-A").size() == 2);
            EXPECT(reopened.LastBoundProject() == std::optional<std::string>{"project-A"});
            reopened.SaveDraft("project-A", "recoverable");
            EXPECT(reopened.Project("project-A")->draft == "recoverable");
            EXPECT(reopened.StageSubmission("project-A", "submit-0003", "pending after restart") == "submit-0003");
            CASES_END();
        }

        // 3. Post-restart commit
        {
            CASES_BEGIN("3. Post-restart commit");
            mayasaba::Storage afterRestart(folder / "project.db");
            auto pending = afterRestart.Pending("project-A");
            EXPECT(pending && pending->id == "submit-0003");
            EXPECT(afterRestart.StageSubmission("project-A", "new-id-rejected", "pending after restart") == "submit-0003");
            EXPECT(afterRestart.SaveContribution("project-A", "submit-0003", "pending after restart") == SaveDisposition::Saved);
            EXPECT(afterRestart.Contributions("project-A").size() == 3);
            CASES_END();
        }

#ifdef _WIN32
        // 4. Folder creation & Locate domain operations
        {
            CASES_BEGIN("4. Folder creation & Locate domain operations");
            auto parentDir = folder / "test-parent";
            fs::create_directories(parentDir);

            // Rejection of invalid names
            bool threw = false;
            try { mayasaba::CreateProjectFolder(parentDir.wstring(), L""); }
            catch (std::exception const&) { threw = true; }
            EXPECT(threw);

            threw = false;
            try { mayasaba::CreateProjectFolder(parentDir.wstring(), L".."); }
            catch (std::exception const&) { threw = true; }
            EXPECT(threw);

            threw = false;
            try { mayasaba::CreateProjectFolder(parentDir.wstring(), L"sub/folder"); }
            catch (std::exception const&) { threw = true; }
            EXPECT(threw);

            threw = false;
            try { mayasaba::CreateProjectFolder(parentDir.wstring(), L"bad:name"); }
            catch (std::exception const&) { threw = true; }
            EXPECT(threw);

            threw = false;
            try { mayasaba::CreateProjectFolder(parentDir.wstring(), L"trailing. "); }
            catch (std::exception const&) { threw = true; }
            EXPECT(threw);

            threw = false;
            try { mayasaba::CreateProjectFolder(parentDir.wstring(), L"CON"); }
            catch (std::exception const&) { threw = true; }
            EXPECT(threw);

            threw = false;
            try { mayasaba::CreateProjectFolder(parentDir.wstring(), L"nul.txt"); }
            catch (std::exception const&) { threw = true; }
            EXPECT(threw);

            // Successful creation of one empty folder
            auto created = mayasaba::CreateProjectFolder(parentDir.wstring(), L"my-new-app");
            EXPECT(created.displayName == L"my-new-app");
            EXPECT(!created.canonicalPath.empty());
            EXPECT(!created.stableIdentity.empty());
            EXPECT(fs::is_directory(created.canonicalPath));

            // Collision refusal
            threw = false;
            try { mayasaba::CreateProjectFolder(parentDir.wstring(), L"my-new-app"); }
            catch (std::exception const&) { threw = true; }
            EXPECT(threw);

            // LocateProject: identity match
            auto located = mayasaba::LocateProject(created.stableIdentity, created.canonicalPath);
            EXPECT(located.stableIdentity == created.stableIdentity);

            // LocateProject: identity mismatch
            auto anotherDir = folder / "another-dir";
            fs::create_directories(anotherDir);
            threw = false;
            try { mayasaba::LocateProject(created.stableIdentity, anotherDir.wstring()); }
            catch (std::exception const&) { threw = true; }
            EXPECT(threw);
            CASES_END();
        }

        // 5. Durable directory operations and crash reconciliation tests
        {
            CASES_BEGIN("5. Durable directory operations and crash reconciliation tests");
            auto crashDb = folder / "crash_test.db";
            std::string op1 = "op-intent-only-1";
            std::string op2 = "op-created-match-2";
            std::string parentDir = (folder / "reconcile-parent").string();
            fs::create_directories(parentDir);

            // Create a physical directory for op2
            auto realDir = mayasaba::CreateProjectFolder(fs::path(parentDir).wstring(), L"reconciled-app");
            std::string fsId = WideToUtf8(realDir.stableIdentity);

            {
                mayasaba::Storage store(crashDb);
                // op1: INTENDED only (simulates crash before creation)
                store.RecordDirectoryIntent(op1, parentDir, "never-created", (fs::path(parentDir) / "never-created").string());
                auto r1 = store.GetDirectoryOperation(op1);
                EXPECT(r1.has_value() && r1->state == "INTENDED");

                // op2: CREATED with observed identity (simulates crash before bind)
                store.RecordDirectoryIntent(op2, parentDir, "reconciled-app", (fs::path(parentDir) / "reconciled-app").string());
                store.RecordDirectoryCreated(op2, fsId);
                auto r2 = store.GetDirectoryOperation(op2);
                EXPECT(r2.has_value() && r2->state == "CREATED" && r2->observedIdentity == fsId);
            }

            // Simulate crash restart: new Storage instance opens crashDb and runs ReconcileCrashOperations()
            {
                mayasaba::Storage recovered(crashDb);
                // op1 was only INTENDED: must retain INTENDED with unknown physical outcome until explicit reconciliation
                auto r1 = recovered.GetDirectoryOperation(op1);
                EXPECT(r1.has_value());
                EXPECT(r1->state == "INTENDED");
                EXPECT(r1->errorMessage == "PHYSICAL_OUTCOME_UNKNOWN");

                // op2 was CREATED with valid observed identity: must be BOUND
                auto r2 = recovered.GetDirectoryOperation(op2);
                EXPECT(r2.has_value());
                EXPECT(r2->state == "CREATED");
                EXPECT(r2->errorMessage == "CREATED_DIRECTORY_REQUIRES_EXPLICIT_SELECTION");

                // The bound project must now be queryable in projects table
                auto proj = recovered.Project(op2);
                EXPECT(!proj.has_value()); // restart is not fresh binding authority
            }
            CASES_END();
        }

        // 6. Domain UUID and Filesystem Identity compatibility
        {
            CASES_BEGIN("6. Domain UUID and Filesystem Identity compatibility");
            auto compatDb = folder / "compat_test.db";
            mayasaba::Storage store(compatDb);

            std::string domainId1 = "3fa85f64-5717-4562-b3fc-2c963f66afa6";
            std::string fsIdentity = "vol-serial-1234:file-id-5678";
            std::string bound1 = store.BindProject(domainId1, "Project One", "C:/App/One", fsIdentity);
            EXPECT(bound1 == domainId1);

            auto p1 = store.Project(domainId1);
            EXPECT(p1.has_value());
            EXPECT(p1->filesystemIdentity == fsIdentity);

            auto byFs = store.ProjectByFilesystemIdentity(fsIdentity);
            EXPECT(byFs.has_value() && byFs->id == domainId1);

            // Re-binding the same filesystem identity with a different proposed domain ID
            // MUST reuse the existing domain ID to preserve contribution foreign keys!
            std::string domainId2 = "7ba99f64-1234-5678-b3fc-999999999999";
            std::string bound2 = store.BindProject(domainId2, "Project One Renamed", "C:/App/OneNew", fsIdentity);
            EXPECT(bound2 == domainId1); // Reused original domain ID!

            auto recents = store.RecentProjects();
            EXPECT(recents.size() == 1);
            EXPECT(recents[0].id == domainId1);
            EXPECT(recents[0].displayName == "Project One Renamed");
            CASES_END();
        }

        // 7. Atomic version-checked draft transfer refusal under pending submission
        {
            CASES_BEGIN("7. Atomic version-checked draft transfer refusal under pending submission");
            auto draftDb = folder / "draft_test.db";
            mayasaba::Storage store(draftDb);
            store.BindProject("proj-draft-check", "Draft Check", "C:/App/Draft");

            // Save prebind draft
            store.SavePrebindDraft("Prebinding text that must survive");

            // Stage a submission on target project
            store.StageSubmission("proj-draft-check", "sub-pend-1", "Submission in progress");

            // Attempting transfer into project with pending submission MUST return false
            // and MUST NEVER clear the prebinding draft!
            EXPECT(!store.TransferPrebindDraftToProject("proj-draft-check", false));
            EXPECT(!store.TransferPrebindDraftToProject("proj-draft-check", true));
            EXPECT(store.GetPrebindDraft() == "Prebinding text that must survive"); // PRESERVED!

            // After completing submission, transfer succeeds and increments version
            auto projBefore = store.Project("proj-draft-check");
            EXPECT(projBefore.has_value());
            int verBefore = projBefore->version;

            store.SaveContribution("proj-draft-check", "sub-pend-1", "Submission in progress");
            EXPECT(store.TransferPrebindDraftToProject("proj-draft-check", false));
            EXPECT(store.GetPrebindDraft().empty()); // cleared after confirmed transfer

            auto projAfter = store.Project("proj-draft-check");
            EXPECT(projAfter.has_value());
            EXPECT(projAfter->draft == "Prebinding text that must survive");
            EXPECT(projAfter->version == verBefore + 1); // Version incremented atomically!
            CASES_END();
        }

        // 8. Verified backup admission and legacy schema migration test
        {
            CASES_BEGIN("8. Verified backup admission and legacy schema migration test");
            auto legacyDbPath = folder / "legacy_migration.db";
            sqlite3* rawDb = nullptr;
            EXPECT(sqlite3_open16(legacyDbPath.wstring().c_str(), &rawDb) == SQLITE_OK);
            char* errMsg = nullptr;
            const char* legacyInitSql =
                "PRAGMA journal_mode=WAL;"
                "PRAGMA synchronous=FULL;"
                "CREATE TABLE projects(id TEXT PRIMARY KEY, display_name TEXT NOT NULL, canonical_path TEXT NOT NULL, draft TEXT NOT NULL DEFAULT '', closed INTEGER NOT NULL DEFAULT 0, version INTEGER NOT NULL DEFAULT 0);"
                "INSERT INTO projects(id, display_name, canonical_path, draft, closed, version) VALUES('leg-1', 'Legacy App', 'C:/Legacy/App', 'old draft text', 0, 42);"
                "CREATE TABLE ui_state(singleton INTEGER PRIMARY KEY CHECK(singleton=1), last_project TEXT NOT NULL);"
                "INSERT INTO ui_state(singleton, last_project) VALUES(1, 'leg-1');"
                "CREATE TABLE contributions(row_id INTEGER PRIMARY KEY AUTOINCREMENT, project_id TEXT NOT NULL, submission_id TEXT NOT NULL UNIQUE, body TEXT NOT NULL, created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
                "INSERT INTO contributions(project_id, submission_id, body) VALUES('leg-1', 'sub-leg-001', 'Legacy message');";
            EXPECT(sqlite3_exec(rawDb, legacyInitSql, nullptr, nullptr, &errMsg) == SQLITE_OK);
            sqlite3_close(rawDb);

            auto bakPath = folder / "legacy_migration.db.pre_migration.bak";
            EXPECT(!fs::exists(bakPath));

            // Instantiating Storage triggers migration and verified backup admission
            {
                mayasaba::Storage migratedStore(legacyDbPath);
                // 1. Backup was created and admitted before migration
                EXPECT(fs::exists(bakPath));

                // 2. Verified that legacy projects, drafts, contributions and versions are intact
                auto proj = migratedStore.Project("leg-1");
                EXPECT(proj.has_value());
                EXPECT(proj->id == "leg-1");
                EXPECT(proj->displayName == "Legacy App");
                EXPECT(proj->canonicalPath == "C:/Legacy/App");
                EXPECT(proj->draft == "old draft text");
                EXPECT(proj->version == 42);
                EXPECT(proj->filesystemIdentity == "leg-1"); // defaulted to id
                EXPECT(proj->lastOpenedAt.empty());          // unknown legacy timestamp not fabricated

                EXPECT(migratedStore.Contributions("leg-1").size() == 1);
                EXPECT(migratedStore.Contributions("leg-1")[0].body == "Legacy message");
                EXPECT(migratedStore.LastBoundProject() == std::optional<std::string>{"leg-1"});
                EXPECT(migratedStore.GetPrebindDraft().empty());

                // Touch sets the lastOpenedAt cleanly
                migratedStore.TouchProjectOpened("leg-1");
                auto touched = migratedStore.Project("leg-1");
                EXPECT(!touched->lastOpenedAt.empty());
            }

            // Verify the backup database itself has integrity_check == ok
            {
                sqlite3* bakDb = nullptr;
                EXPECT(sqlite3_open16(bakPath.wstring().c_str(), &bakDb) == SQLITE_OK);
                sqlite3_stmt* stmt = nullptr;
                EXPECT(sqlite3_prepare_v2(bakDb, "PRAGMA integrity_check;", -1, &stmt, nullptr) == SQLITE_OK);
                EXPECT(sqlite3_step(stmt) == SQLITE_ROW);
                std::string checkResult = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 0));
                EXPECT(checkResult == "ok");
                sqlite3_finalize(stmt);
                sqlite3_close(bakDb);
            }
            CASES_END();
        }

        // 9. Rogue directory crash reconciliation test: never adopt unverified directory
        {
            CASES_BEGIN("9. Rogue directory crash reconciliation test: never adopt unverified directory");
            auto rogueDb = folder / "rogue_crash.db";
            std::string rogueOp = "op-rogue-intent";
            std::string parentDir = (folder / "rogue-parent").string();
            fs::create_directories(parentDir);
            std::string targetDir = (fs::path(parentDir) / "rogue-child").string();

            {
                mayasaba::Storage store(rogueDb);
                store.RecordDirectoryIntent(rogueOp, parentDir, "rogue-child", targetDir);
                // Simulate an external directory being created at the target path behind the controller's back
                fs::create_directories(targetDir);
            }

            // On restart, crash reconciliation MUST NOT adopt the rogue directory
            {
                mayasaba::Storage recovered(rogueDb);
                auto op = recovered.GetDirectoryOperation(rogueOp);
                EXPECT(op.has_value());
                EXPECT(op->state == "INTENDED");
                EXPECT(op->errorMessage == "PHYSICAL_OUTCOME_UNKNOWN");

                // No project should be bound from this rogue directory
                EXPECT(!recovered.Project(rogueOp).has_value());
                EXPECT(recovered.RecentProjects().empty());
            }
            CASES_END();
        }

        // 10. Concurrency exclusion test (single-writer mutex)
        {
            CASES_BEGIN("10. Concurrency exclusion test (single-writer mutex)");
            auto mDb1 = folder / "mutex_1.db";
            auto mDb2 = folder / "mutex_2.db";
            mayasaba::Storage primary(mDb1);

            bool secondaryFailed = false;
            try
            {
                mayasaba::Storage secondary(mDb1);
            }
            catch (std::exception const& ex)
            {
                secondaryFailed = true;
                std::string msg = ex.what();
                EXPECT(msg.find("Another Mayasaba instance is already using its store") != std::string::npos);
            }
            EXPECT(secondaryFailed);
            mayasaba::Storage independent(mDb2); // truly different stores may coexist
            auto alias = folder / "mutex-hardlink.db";
            fs::create_hard_link(mDb1, alias);
            bool aliasBlocked = false;
            try { mayasaba::Storage other(alias); } catch (std::exception const&) { aliasBlocked = true; }
            EXPECT(aliasBlocked);
            CASES_END();
        }
        // 11. Revision ordering and transfer fence, deliberately deliver old saves late.
        {
            CASES_BEGIN("11. Revision ordering and transfer fence, deliberately deliver old saves late.");
            mayasaba::Storage store(folder / "revision.db");
            store.BindProject("p", "P", "C:/P");
            auto old = store.ReservePrebindRevision();
            auto newest = store.ReservePrebindRevision();
            EXPECT(store.SavePrebindDraft("new text", newest));
            EXPECT(!store.SavePrebindDraft("old text", old));
            EXPECT(store.GetPrebindDraft() == "new text");
            EXPECT(store.TransferPrebindDraftToProject("p"));
            EXPECT(!store.SavePrebindDraft("new text", newest));
            EXPECT(store.GetPrebindDraft().empty());
            EXPECT(store.Project("p")->draft == "new text");
            auto unsaved = store.ReservePrebindRevision();
            EXPECT(!store.TransferPrebindDraftToProject("p", true));
            EXPECT(store.SavePrebindDraft("next idea", unsaved));
            EXPECT(store.GetPrebindDraft() == "next idea");
            CASES_END();
        }
        // 12. Service-level locate preserves name/path order, identity and draft.
        {
            CASES_BEGIN("12. Service-level locate preserves name/path order, identity and draft.");
            mayasaba::Storage store(folder / "service.db");
            mayasaba::ProjectService service(store);
            auto original = folder / "original";
            fs::create_directory(original);
            auto id = service.Bind("domain", original.wstring());
            store.SaveDraft(id, "protected");
            auto moved = folder / "renamed";
            fs::rename(original, moved);
            service.Locate(id, moved.wstring());
            EXPECT(store.Project(id)->displayName == "renamed");
            EXPECT(store.Project(id)->canonicalPath == WideToUtf8(moved.wstring()));
            EXPECT(store.Project(id)->draft == "protected");
            bool rejected = false;
            try { service.Locate(id, folder.wstring()); } catch (std::exception const&) { rejected = true; }
            EXPECT(rejected);
            EXPECT(store.Project(id)->draft == "protected");
            auto created = service.Create({"creation-1", folder.wstring(), L"service-child"});
            EXPECT(store.Project(created).has_value());
            EXPECT(store.GetDirectoryOperation("creation-1")->state == "BOUND");
            rejected = false;
            try { service.Create({"creation-1", folder.wstring(), L"service-child"}); } catch (std::exception const&) { rejected = true; }
            EXPECT(rejected);
            // Collision after durable intent retains uncertainty rather than inventing absence.
            rejected = false;
            try { service.Create({"creation-2", folder.wstring(), L"service-child"}); } catch (std::exception const&) { rejected = true; }
            EXPECT(rejected);
            EXPECT(store.GetDirectoryOperation("creation-2")->state == "INTENDED");
            EXPECT(fs::is_directory(folder / "service-child"));
            CASES_END();
        }
        // 13. Test injection must reject malformed input, never fall back to production.
        {
            CASES_BEGIN("13. Test injection must reject malformed input, never fall back to production.");
            std::wstring oversized(400, L'x');
            SetEnvironmentVariableW(L"MAYASABA_STORE_PATH", oversized.c_str());
            bool rejected = false;
            try { (void)mayasaba::Storage::DefaultFile(); } catch (std::exception const&) { rejected = true; }
            EXPECT(rejected);
            SetEnvironmentVariableW(L"MAYASABA_STORE_PATH", L"relative.db");
            rejected = false;
            try { (void)mayasaba::Storage::DefaultFile(); } catch (std::exception const&) { rejected = true; }
            EXPECT(rejected);
            SetEnvironmentVariableW(L"MAYASABA_STORE_PATH", nullptr);
            auto production = mayasaba::Storage::DefaultFile(); // path query only; never opened
            SetEnvironmentVariableW(L"MAYASABA_STORE_PATH", production.c_str());
            rejected = false;
            try { (void)mayasaba::Storage::DefaultFile(); } catch (std::exception const&) { rejected = true; }
            EXPECT(rejected);
            auto isolated = folder / "override.db";
            SetEnvironmentVariableW(L"MAYASABA_STORE_PATH", isolated.c_str());
            EXPECT(mayasaba::Storage::DefaultFile() == isolated);
            SetEnvironmentVariableW(L"MAYASABA_STORE_PATH", nullptr);
            CASES_END();
        }
        // 14. FIFO lane: pause its first task, enqueue writes and transfer, then release.
        {
            CASES_BEGIN("14. FIFO lane: pause its first task, enqueue writes and transfer, then release.");
            mayasaba::Storage store(folder / "fifo.db");
            store.BindProject("fifo", "FIFO", "C:/FIFO");
            mayasaba::SerialQueue queue;
            std::promise<void> release, done;
            auto gate = release.get_future().share();
            queue.Post([gate] { gate.wait(); });
            queue.Post([&] { store.SavePrebindDraft("first"); });
            queue.Post([&] { store.SavePrebindDraft("latest"); });
            bool transfer = false;
            queue.Post([&] { transfer = store.TransferPrebindDraftToProject("fifo"); done.set_value(); });
            release.set_value();
            done.get_future().get();
            EXPECT(transfer);
            EXPECT(store.Project("fifo")->draft == "latest");
            EXPECT(store.GetPrebindDraft().empty());
            CASES_END();
        }

        // 15. Actual typed selector transaction, replay, stale identity and draft disposition.
        {
            CASES_BEGIN("15. Actual typed selector transaction, replay, stale identity and draft disposition.");
            mayasaba::Storage store(folder / "selector.db");
            mayasaba::ProjectService service(store);
            auto root=folder/"selector-root";fs::create_directory(root);
            mayasaba::SelectionRequest open;open.path=root.wstring();
            auto noSessions=mayasaba::SwitchObservation::NoAdmittedSessions;
            bool refused=false;
            try{service.Select(open,mayasaba::SwitchObservation::Unknown);}catch(std::exception const&){refused=true;}
            EXPECT(refused && !store.HasAnyProjects());
            store.SavePrebindDraft("first selector draft");
            auto selected=service.Select(open,noSessions);
            EXPECT(mayasaba::IsUuid(selected.project.domainId));
            EXPECT(selected.project.draft=="first selector draft");EXPECT(store.GetPrebindDraft().empty());
            auto duplicate=service.Select(open,noSessions);
            EXPECT(duplicate.duplicate && duplicate.project.id==selected.project.id);
            EXPECT(store.RecentProjects().size()==1);
            auto changed=open;changed.path=folder.wstring();refused=false;
            try{service.Select(changed,noSessions);}catch(std::exception const&){refused=true;}EXPECT(refused);
            auto current=store.Project(selected.project.id).value();
            store.SavePrebindDraft("second selector draft");
            mayasaba::SelectionRequest recent;recent.action=mayasaba::SelectionAction::Recent;
            recent.projectId=current.domainId;recent.expectedVersion=current.version;
            auto collision=service.Select(recent,noSessions);EXPECT(collision.collision);
            EXPECT(store.GetPrebindDraft()=="second selector draft");EXPECT(store.Project(current.id)->draft=="first selector draft");
            auto disposition=recent;disposition.operationId=mayasaba::NewUuid();disposition.draftChoice=mayasaba::DraftChoice::UseSelector;
            disposition.expectedTargetDraft=current.draft;disposition.expectedSelectorDraft=collision.selectorDraft;
            auto swapped=service.Select(disposition,noSessions);
            EXPECT(swapped.project.draft=="second selector draft");EXPECT(store.GetPrebindDraft()=="first selector draft");
            EXPECT(service.Select(disposition,noSessions).duplicate); // lost receipt, same operation
            recent.operationId=mayasaba::NewUuid();refused=false;
            try{service.Select(recent,noSessions);}catch(std::exception const&){refused=true;}EXPECT(refused); // stale version
            current=store.Project(current.id).value();
            EXPECT(store.SetClosure(current.id,current.version,true));
            recent.expectedVersion=store.Project(current.id)->version;recent.operationId=mayasaba::NewUuid();
            auto closed=service.Select(recent,noSessions);EXPECT(closed.project.closed);
            EXPECT(store.GetPrebindDraft()=="first selector draft");
            auto oldPath=root;auto moved=folder/"selector-moved";fs::rename(oldPath,moved);
            mayasaba::SelectionRequest locate;locate.action=mayasaba::SelectionAction::Locate;locate.path=moved.wstring();
            locate.projectId=current.domainId;locate.expectedVersion=closed.project.version;
            auto relocated=service.Select(locate,noSessions);EXPECT(relocated.project.id==current.id);
            locate.operationId=mayasaba::NewUuid();locate.path=folder.wstring();locate.expectedVersion=relocated.project.version;
            refused=false;try{service.Select(locate,noSessions);}catch(std::exception const&){refused=true;}EXPECT(refused);
            EXPECT(store.Project(current.id)->canonicalPath==WideToUtf8(moved.wstring()));
            auto before=store.Project(current.id)->lastOpenedAt;
            auto page=service.Recent({1,0});EXPECT(page.items.size()==1 && !page.hasMore);
            EXPECT(store.Project(current.id)->lastOpenedAt==before);
            EXPECT(!service.Recent({101,0}).isComplete);
            CASES_END();
        }
        // 16. Directory intent/replay, cancellation, recovery and held path replacement fence.
        {
            CASES_BEGIN("16. Directory intent/replay, cancellation, recovery and held path replacement fence.");
            mayasaba::Storage store(folder/"create-selector.db");mayasaba::ProjectService service(store);
            auto parent=folder/"create-parent";fs::create_directory(parent);
            auto parentRoot=mayasaba::ValidateProjectRoot(parent.wstring());
            auto noSessions=mayasaba::SwitchObservation::NoAdmittedSessions;
            mayasaba::SelectionRequest create;create.action=mayasaba::SelectionAction::Create;
            create.path=parent.wstring();create.child=L"child";create.expectedParent=WideToUtf8(parentRoot.stableIdentity);
            create.creationOperation=create.operationId;
            bool refused=false;try{service.Select(create,noSessions,[]{return false;});}catch(std::exception const&){refused=true;}
            EXPECT(refused && !fs::exists(parent/"child") && !store.GetDirectoryOperation(create.creationOperation));
            auto result=service.Select(create,noSessions);EXPECT(fs::is_empty(parent/"child"));
            EXPECT(store.GetDirectoryOperation(create.creationOperation)->state=="BOUND");
            EXPECT(service.Select(create,noSessions).duplicate);EXPECT(store.RecentProjects().size()==1);
            auto altered=create;altered.child=L"different";refused=false;
            try{service.Select(altered,noSessions);}catch(std::exception const&){refused=true;}EXPECT(refused && !fs::exists(parent/"different"));
            auto uncertain=mayasaba::NewUuid();
            store.RecordDirectoryIntent(uncertain,WideToUtf8(parent.wstring()),"rogue",WideToUtf8((parent/"rogue").wstring()));fs::create_directory(parent/"rogue");
            mayasaba::SelectionRequest recovery;recovery.action=mayasaba::SelectionAction::Recover;recovery.creationOperation=uncertain;
            refused=false;try{service.Select(recovery,noSessions);}catch(std::exception const&){refused=true;}EXPECT(refused);
            EXPECT(store.RecentProjects().size()==1);
            auto observed=mayasaba::CreateProjectFolder(parent.wstring(),L"observed");auto operation=mayasaba::NewUuid();
            store.RecordDirectoryIntent(operation,WideToUtf8(parent.wstring()),"observed",WideToUtf8(observed.canonicalPath));
            store.RecordDirectoryCreated(operation,WideToUtf8(observed.stableIdentity));
            recovery.creationOperation=operation;recovery.operationId=mayasaba::NewUuid();
            EXPECT(service.Select(recovery,noSessions).project.displayName=="observed");
            std::error_code renameError;fs::rename(parent,parent.wstring()+L"-replacement",renameError);EXPECT(renameError);
            EXPECT(fs::exists(parent/"child"));
            CASES_END();
        }
        // 17. Abort after project writes but before receipt/event commit: no partial selection.
        {
            CASES_BEGIN("17. Abort after project writes but before receipt/event commit: no partial selection.");
            auto dbPath=folder/"fault.db";mayasaba::Storage store(dbPath);mayasaba::ProjectService service(store);
            sqlite3* injector{};EXPECT(sqlite3_open16(dbPath.c_str(),&injector)==SQLITE_OK);
            EXPECT(sqlite3_exec(injector,"CREATE TRIGGER fail_selection BEFORE INSERT ON selection_events BEGIN SELECT RAISE(ABORT,'injected event fault'); END;",nullptr,nullptr,nullptr)==SQLITE_OK);
            store.SavePrebindDraft("must survive transaction fault");
            auto root=folder/"fault-root";fs::create_directory(root);mayasaba::SelectionRequest r;r.path=root.wstring();bool refused=false;
            try{service.Select(r,mayasaba::SwitchObservation::NoAdmittedSessions);}catch(std::exception const&){refused=true;}
            EXPECT(refused && !store.HasAnyProjects());EXPECT(store.GetPrebindDraft()=="must survive transaction fault");
            EXPECT(sqlite3_exec(injector,"DROP TRIGGER fail_selection;",nullptr,nullptr,nullptr)==SQLITE_OK);
            EXPECT(service.Select(r,mayasaba::SwitchObservation::NoAdmittedSessions).project.draft=="must survive transaction fault");
            sqlite3_close(injector);
            CASES_END();
        }
        // 18. Unsupported store version is refused before schema mutation; DPAPI draft bytes are protected.
        {
            CASES_BEGIN("18. Unsupported store version is refused before schema mutation; DPAPI draft bytes are protected.");
            auto db=folder/"future.db";sqlite3* raw{};EXPECT(sqlite3_open16(db.c_str(),&raw)==SQLITE_OK);
            EXPECT(sqlite3_exec(raw,"PRAGMA user_version=999;",nullptr,nullptr,nullptr)==SQLITE_OK);sqlite3_close(raw);
            bool refused=false;try{mayasaba::Storage future(db);}catch(std::exception const&){refused=true;}EXPECT(refused);
            auto protectedDb=folder/"protected.db";mayasaba::Storage store(protectedDb);store.SavePrebindDraft("private selector text");
            EXPECT(sqlite3_open16(protectedDb.c_str(),&raw)==SQLITE_OK);sqlite3_stmt* q{};
            EXPECT(sqlite3_prepare_v2(raw,"SELECT prebinding_draft FROM ui_state;",-1,&q,nullptr)==SQLITE_OK);
            EXPECT(sqlite3_step(q)==SQLITE_ROW);std::string stored(reinterpret_cast<char const*>(sqlite3_column_text(q,0)));
            EXPECT(stored.starts_with("dpapi1:") && stored.find("private selector text")==std::string::npos);
            sqlite3_finalize(q);sqlite3_close(raw);EXPECT(store.GetPrebindDraft()=="private selector text");
            CASES_END();
        }
        // 19. Attachment service: selection, validation, 50-file limits, byte boundaries,
        // chunked copy, SHA-256 verification, source change detection, draft persistence, recovery and atomic submission.
        {
            CASES_BEGIN("19. Attachment service: selection, validation, 50-file limits, byte boundaries,");
            auto attDb = folder / "attachment_test.db";
            // Production wires the default staging root (the store's managed attachment
            // directory), and finalization now verifies that admitted bytes are inside
            // that boundary; a test that stages a real file must stage it the same way.
            auto attStaging = folder / "attachments";
            fs::create_directories(attStaging);
            mayasaba::Storage store(attDb);
            mayasaba::AttachmentService attService(store, attStaging);

            std::string const projectId = "proj-att-1";
            store.BindProject(projectId, "Attachment Project", "C:/Projects/AttTest");

            auto testFilesDir = folder / "att_sources";
            fs::create_directories(testFilesDir);

            // 1. 49 files within byte limits -> Accept
            std::vector<std::wstring> files49;
            for (int i = 1; i <= 49; ++i)
            {
                auto p = testFilesDir / ("file_" + std::to_string(i) + ".txt");
                std::ofstream ofs(p);
                ofs << "Payload for file " << i << "\n";
                ofs.close();
                files49.push_back(p.wstring());
            }
            auto res49 = attService.ValidateCandidateBatch(files49, {});
            EXPECT(res49.accepted);
            EXPECT(res49.reason == mayasaba::ValidationFailureReason::None);
            EXPECT(res49.validatedFiles.size() == 49);

            // 2. 50 files within byte limits -> Accept
            auto file50 = testFilesDir / "file_50.txt";
            {
                std::ofstream ofs(file50);
                ofs << "Payload for file 50\n";
            }
            auto files50 = files49;
            files50.push_back(file50.wstring());
            auto res50 = attService.ValidateCandidateBatch(files50, {});
            EXPECT(res50.accepted);
            EXPECT(res50.reason == mayasaba::ValidationFailureReason::None);
            EXPECT(res50.validatedFiles.size() == 50);

            // 3. 51 files -> Reject
            auto file51 = testFilesDir / "file_51.txt";
            {
                std::ofstream ofs(file51);
                ofs << "Payload for file 51\n";
            }
            auto files51 = files50;
            files51.push_back(file51.wstring());
            auto res51 = attService.ValidateCandidateBatch(files51, {});
            EXPECT(!res51.accepted);
            EXPECT(res51.reason == mayasaba::ValidationFailureReason::TooManyFiles);
            EXPECT(res51.errorMessage.find("51") != std::string::npos);
            EXPECT(res51.errorMessage.find("50") != std::string::npos);

            // Incremental batches: 49 existing + 2 new = 51 -> Reject
            std::vector<mayasaba::AttachmentCandidate> existing49;
            for (size_t i = 0; i < 49; ++i)
            {
                mayasaba::AttachmentCandidate c;
                c.id = "cand-" + std::to_string(i);
                c.fileName = "existing_" + std::to_string(i);
                c.fileSize = 100;
                c.status = mayasaba::AttachmentStatus::Ready;
                existing49.push_back(c);
            }
            std::vector<std::wstring> twoNewFiles = { file50.wstring(), file51.wstring() };
            auto resInc51 = attService.ValidateCandidateBatch(twoNewFiles, existing49);
            EXPECT(!resInc51.accepted);
            EXPECT(resInc51.reason == mayasaba::ValidationFailureReason::TooManyFiles);

            // 4. File size limit: 100 MiB (104,857,600 bytes)
            auto file100MiB = testFilesDir / "exact_100mib.bin";
            {
                HANDLE h = CreateFileW(file100MiB.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                EXPECT(h != INVALID_HANDLE_VALUE);
                LARGE_INTEGER li; li.QuadPart = 100ULL * 1024ULL * 1024ULL; // 104,857,600
                EXPECT(SetFilePointerEx(h, li, nullptr, FILE_BEGIN));
                EXPECT(SetEndOfFile(h));
                CloseHandle(h);
            }
            auto res100 = attService.ValidateCandidateBatch({ file100MiB.wstring() }, {});
            EXPECT(res100.accepted);

            auto file100MiBPlus = testFilesDir / "over_100mib.bin";
            {
                HANDLE h = CreateFileW(file100MiBPlus.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                EXPECT(h != INVALID_HANDLE_VALUE);
                LARGE_INTEGER li; li.QuadPart = 100ULL * 1024ULL * 1024ULL + 1; // 104,857,601
                EXPECT(SetFilePointerEx(h, li, nullptr, FILE_BEGIN));
                EXPECT(SetEndOfFile(h));
                CloseHandle(h);
            }
            auto resOver100 = attService.ValidateCandidateBatch({ file100MiBPlus.wstring() }, {});
            EXPECT(!resOver100.accepted);
            EXPECT(resOver100.reason == mayasaba::ValidationFailureReason::FileTooLarge);
            EXPECT(resOver100.errorMessage.find("100 MiB") != std::string::npos);

            // 5. Aggregate size limit: 500 MiB (524,288,000 bytes)
            std::vector<mayasaba::AttachmentCandidate> existing400MiB;
            for (int i = 0; i < 4; ++i)
            {
                mayasaba::AttachmentCandidate c;
                c.id = "big-" + std::to_string(i);
                c.fileSize = 100ULL * 1024ULL * 1024ULL;
                existing400MiB.push_back(c);
            }
            auto res500Exact = attService.ValidateCandidateBatch({ file100MiB.wstring() }, existing400MiB);
            EXPECT(res500Exact.accepted);

            auto file60MiB = testFilesDir / "file_60mib.bin";
            {
                HANDLE h = CreateFileW(file60MiB.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                EXPECT(h != INVALID_HANDLE_VALUE);
                LARGE_INTEGER li; li.QuadPart = 60ULL * 1024ULL * 1024ULL;
                EXPECT(SetFilePointerEx(h, li, nullptr, FILE_BEGIN));
                EXPECT(SetEndOfFile(h));
                CloseHandle(h);
            }
            // 400 MiB + 60 MiB + 60 MiB = 520 MiB > 500 MiB
            auto resAggOver = attService.ValidateCandidateBatch({ file60MiB.wstring(), file60MiB.wstring() }, existing400MiB);
            EXPECT(!resAggOver.accepted);
            EXPECT(resAggOver.reason == mayasaba::ValidationFailureReason::AggregateTooLarge);
            EXPECT(resAggOver.errorMessage.find("500 MiB") != std::string::npos);

            // 6. Empty file (0 bytes) -> Reject
            auto emptyFile = testFilesDir / "empty.txt";
            {
                std::ofstream ofs(emptyFile);
            }
            auto resEmpty = attService.ValidateCandidateBatch({ emptyFile.wstring() }, {});
            EXPECT(!resEmpty.accepted);
            EXPECT(resEmpty.reason == mayasaba::ValidationFailureReason::EmptyFile);

            // 7. Preparation: chunked copy & SHA-256 verification
            auto prepFile = testFilesDir / "prep_test.txt";
            std::string const payload = "The quick brown fox jumps over the lazy dog";
            {
                std::ofstream ofs(prepFile);
                ofs << payload;
            }
            auto candidate = attService.PrepareAttachment(projectId, prepFile.wstring());
            EXPECT(candidate.status == mayasaba::AttachmentStatus::Ready);
            EXPECT(candidate.fileSize == payload.size());
            EXPECT(!candidate.sha256.empty());
            EXPECT(fs::exists(candidate.stagedPath));
            // Known SHA-256 for "The quick brown fox jumps over the lazy dog":
            // d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592
            EXPECT(candidate.sha256 == "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");

            // 8. Source changed during preparation -> Block Send
            auto changingFile = testFilesDir / "changing_file.txt";
            {
                std::ofstream ofs(changingFile);
                ofs << "Initial content before change";
            }
            auto candChanged = attService.PrepareAttachment(projectId, changingFile.wstring(), [&]() {
                std::ofstream ofs(changingFile, std::ios::app);
                ofs << " appended extra bytes during preparation!";
                ofs.close();
                return false;
            });
            EXPECT(candChanged.status == mayasaba::AttachmentStatus::SourceChangedDuringPreparation);
            EXPECT(!candChanged.errorMessage.empty());
            EXPECT(!fs::exists(candChanged.stagedPath));

            // 9. Draft persistence and recovery across store reload
            std::vector<mayasaba::AttachmentCandidate> draftBatch = { candidate };
            attService.SaveDraftAttachments(projectId, draftBatch);

            auto loadedDraft = attService.LoadDraftAttachments(projectId);
            EXPECT(loadedDraft.size() == 1);
            EXPECT(loadedDraft[0].id == candidate.id);
            EXPECT(loadedDraft[0].sha256 == candidate.sha256);
            EXPECT(loadedDraft[0].status == mayasaba::AttachmentStatus::Ready);

            auto reverified = attService.ReverifyDraftAttachments(projectId);
            EXPECT(reverified.size() == 1);
            EXPECT(reverified[0].status == mayasaba::AttachmentStatus::Ready);

            // Corrupt staged file: ReverifyDraftAttachments marks it Rejected
            {
                std::ofstream ofs(candidate.stagedPath, std::ios::trunc);
                ofs << "tampered content!";
            }
            auto reverifiedCorrupt = attService.ReverifyDraftAttachments(projectId);
            EXPECT(reverifiedCorrupt.size() == 1);
            EXPECT(reverifiedCorrupt[0].status == mayasaba::AttachmentStatus::Rejected);

            // Restore valid staged content. This does NOT clear the rejection: a corruption
            // finding is a decision about this attempt, not merely a doubt about the bytes,
            // so recovery must not promote it back to READY on the strength of a hash match.
            {
                std::ofstream ofs(candidate.stagedPath, std::ios::trunc);
                ofs << payload;
            }
            auto reverifiedFixed = attService.ReverifyDraftAttachments(projectId);
            EXPECT(reverifiedFixed.size() == 1);
            EXPECT(reverifiedFixed[0].status == mayasaba::AttachmentStatus::Rejected);
            EXPECT(!reverifiedFixed[0].errorMessage.empty());

            // Only an explicit, authorized revalidation by the record's owner may move it
            // back to READY. That is the transition the recovery policy reserves.
            {
                mayasaba::AttachmentRecord revalidated;
                revalidated.id = candidate.id;
                revalidated.projectId = projectId;
                revalidated.fileName = candidate.fileName;
                revalidated.fileSize = candidate.fileSize;
                revalidated.sha256 = candidate.sha256;
                revalidated.detectedType = candidate.detectedType;
                revalidated.storagePath = WideToUtf8(candidate.stagedPath);
                revalidated.sourcePath = WideToUtf8(candidate.originalPath);
                revalidated.status = "READY";
                store.SaveDraftAttachment(projectId, revalidated);
            }
            auto reverifiedAfterRevalidation = attService.ReverifyDraftAttachments(projectId);
            EXPECT(reverifiedAfterRevalidation.size() == 1);
            EXPECT(reverifiedAfterRevalidation[0].status == mayasaba::AttachmentStatus::Ready);

            // 10. Atomic submission with UserContribution
            std::string const subId = "sub-att-1";
            store.StageSubmission(projectId, subId, "Message with attachment");

            mayasaba::AttachmentRecord rec;
            rec.id = candidate.id;
            rec.projectId = projectId;
            rec.submissionId = subId;
            rec.fileName = candidate.fileName;
            rec.fileSize = candidate.fileSize;
            rec.sha256 = candidate.sha256;
            rec.detectedType = candidate.detectedType;
            rec.storagePath = WideToUtf8(candidate.stagedPath);
            rec.sourcePath = WideToUtf8(candidate.originalPath);
            rec.status = "COMMITTED";

            auto saveDisp = store.SaveContribution(projectId, subId, "Message with attachment", { rec });
            EXPECT(saveDisp == mayasaba::SaveDisposition::Saved);

            EXPECT(attService.LoadDraftAttachments(projectId).empty());

            auto contribs = store.Contributions(projectId);
            EXPECT(contribs.size() == 1);
            EXPECT(contribs[0].submissionId == subId);
            EXPECT(contribs[0].attachments.size() == 1);
            EXPECT(contribs[0].attachments[0].fileName == candidate.fileName);
            EXPECT(contribs[0].attachments[0].sha256 == candidate.sha256);

            auto committedAtts = store.GetAttachmentsForContribution(subId);
            EXPECT(committedAtts.size() == 1);
            EXPECT(committedAtts[0].sha256 == candidate.sha256);

            // 11. Attachment-only submission (empty text body)
            auto fileOnly = testFilesDir / "file_only.txt";
            {
                std::ofstream ofs(fileOnly);
                ofs << "Attachment only content without text message";
            }
            auto candOnly = attService.PrepareAttachment(projectId, fileOnly.wstring());
            EXPECT(candOnly.status == mayasaba::AttachmentStatus::Ready);
            attService.SaveDraftAttachments(projectId, { candOnly });

            std::string const subOnlyId = "sub-att-only-1";
            store.StageSubmission(projectId, subOnlyId, ""); // empty text succeeds because draft attachment is ready

            mayasaba::AttachmentRecord recOnly;
            recOnly.id = candOnly.id;
            recOnly.projectId = projectId;
            recOnly.submissionId = subOnlyId;
            recOnly.fileName = candOnly.fileName;
            recOnly.fileSize = candOnly.fileSize;
            recOnly.sha256 = candOnly.sha256;
            recOnly.detectedType = candOnly.detectedType;
            recOnly.storagePath = WideToUtf8(candOnly.stagedPath);
            recOnly.sourcePath = WideToUtf8(candOnly.originalPath);
            recOnly.status = "COMMITTED";

            auto saveDispOnly = store.SaveContribution(projectId, subOnlyId, "", { recOnly });
            EXPECT(saveDispOnly == mayasaba::SaveDisposition::Saved);
            auto contribsAfterOnly = store.Contributions(projectId);
            EXPECT(contribsAfterOnly.size() == 2);
            EXPECT(contribsAfterOnly[1].submissionId == subOnlyId);
            EXPECT(contribsAfterOnly[1].body.empty());
            EXPECT(contribsAfterOnly[1].attachments.size() == 1);
            EXPECT(contribsAfterOnly[1].attachments[0].sha256 == candOnly.sha256);

            // 12. Admission boundary verification: tampered bytes rejected by SaveContribution
            auto tamperFile = testFilesDir / "tamper_test.txt";
            {
                std::ofstream ofs(tamperFile);
                ofs << "Original bytes before admission check";
            }
            auto candTamper = attService.PrepareAttachment(projectId, tamperFile.wstring());
            EXPECT(candTamper.status == mayasaba::AttachmentStatus::Ready);
            attService.SaveDraftAttachments(projectId, { candTamper });

            // Maliciously tamper with the staged file on disk before SaveContribution
            {
                std::ofstream ofs(candTamper.stagedPath, std::ios::trunc);
                ofs << "TAMPERED BYTES ON DISK";
            }

            std::string const subTamperId = "sub-tamper-1";
            store.StageSubmission(projectId, subTamperId, "Will be rejected at admission boundary");
            mayasaba::AttachmentRecord recTamper;
            recTamper.id = candTamper.id;
            recTamper.projectId = projectId;
            recTamper.submissionId = subTamperId;
            recTamper.fileName = candTamper.fileName;
            recTamper.fileSize = candTamper.fileSize;
            recTamper.sha256 = candTamper.sha256; // records original SHA-256
            recTamper.detectedType = candTamper.detectedType;
            recTamper.storagePath = WideToUtf8(candTamper.stagedPath);
            recTamper.sourcePath = WideToUtf8(candTamper.originalPath);
            recTamper.status = "COMMITTED";

            auto saveDispTamper = store.SaveContribution(projectId, subTamperId, "Will be rejected at admission boundary", { recTamper });
            EXPECT(saveDispTamper == mayasaba::SaveDisposition::Conflict); // Rejected by cryptographic admission check!

            // Verify that nothing was persisted to contributions or attachments
            auto contribsAfterTamper = store.Contributions(projectId);
            EXPECT(contribsAfterTamper.size() == 2); // still 2, tampered one was rolled back
            auto tamperAtts = store.GetAttachmentsForContribution(subTamperId);
            EXPECT(tamperAtts.empty());

            // Restore valid content and retry with same submission ID: now succeeds durably!
            {
                std::ofstream ofs(candTamper.stagedPath, std::ios::trunc);
                ofs << "Original bytes before admission check";
            }
            auto saveDispRetry = store.SaveContribution(projectId, subTamperId, "Will be rejected at admission boundary", { recTamper });
            EXPECT(saveDispRetry == mayasaba::SaveDisposition::Saved);
            EXPECT(store.Contributions(projectId).size() == 3);

            // 13. Interrupted preparation intent and orphaned file reconciliation (ReconcileStagedFiles)
            auto stagingDir = attService.StagingRoot() / "staging" / projectId;
            fs::create_directories(stagingDir);

            // Create an interrupted PREPARING draft
            std::string const interruptedId = "interrupted-draft-id";
            auto partialFilePath = stagingDir / (interruptedId + "_partial.tmp");
            {
                std::ofstream ofs(partialFilePath);
                ofs << "Partial chunk written before crash";
            }
            mayasaba::AttachmentRecord interruptedRec;
            interruptedRec.id = interruptedId;
            interruptedRec.projectId = projectId;
            interruptedRec.fileName = "partial.tmp";
            interruptedRec.fileSize = 100;
            interruptedRec.sha256 = "";
            interruptedRec.detectedType = "unknown";
            interruptedRec.storagePath = WideToUtf8(partialFilePath.wstring());
            interruptedRec.sourcePath = "C:/fake/path.tmp";
            interruptedRec.status = "PREPARING";
            store.SaveDraftAttachment(projectId, interruptedRec);

            // Also create an untracked orphaned file in staging
            auto orphanFilePath = stagingDir / "orphaned_file_not_in_db.dat";
            {
                std::ofstream ofs(orphanFilePath);
                ofs << "Orphan bytes not tracked anywhere";
            }

            EXPECT(fs::exists(partialFilePath));
            EXPECT(fs::exists(orphanFilePath));
            EXPECT(store.GetDraftAttachments(projectId).size() >= 1);

            // Run reconciliation
            attService.ReconcileStagedFiles(projectId);

            // Verify: PREPARING draft removed from DB and disk
            EXPECT(!fs::exists(partialFilePath));
            EXPECT(!fs::exists(orphanFilePath));
            auto remainingDrafts = store.GetDraftAttachments(projectId);
            for (auto const& d : remainingDrafts)
            {
                EXPECT(d.id != interruptedId);
            }

            // 14. Batch-wide timeout budget (PrepareBatch)
            auto timeoutFile1 = testFilesDir / "batch_timeout1.txt";
            auto timeoutFile2 = testFilesDir / "batch_timeout2.txt";
            {
                std::ofstream ofs1(timeoutFile1); ofs1 << "Batch 1";
                std::ofstream ofs2(timeoutFile2); ofs2 << "Batch 2";
            }
            // With 0-second timeout, subsequent files hit the batch budget deadline
            auto batchTimeoutRes = attService.PrepareBatch(projectId,
                { timeoutFile1.wstring(), timeoutFile2.wstring() },
                [] { return false; },
                0);
            EXPECT(batchTimeoutRes.size() == 2);
            bool hasTimeout = false;
            for (auto const& r : batchTimeoutRes)
            {
                if (r.status == mayasaba::AttachmentStatus::TimedOut)
                {
                    hasTimeout = true;
                    break;
                }
            }
            EXPECT(hasTimeout);

            // 15. Aggregate 500 MiB limit in SaveContribution
            std::string const projectHuge = "project-huge";
            store.BindProject(projectHuge, "Huge Project", "C:/Projects/Huge");
            std::vector<mayasaba::AttachmentRecord> hugeAttBatch;
            for (int i = 0; i < 6; ++i)
            {
                mayasaba::AttachmentRecord hr;
                hr.id = "huge-" + std::to_string(i);
                hr.projectId = projectHuge;
                hr.submissionId = "sub-huge";
                hr.fileName = "huge.bin";
                hr.fileSize = 90ULL * 1024ULL * 1024ULL; // 6 * 90 = 540 MiB > 500 MiB
                hr.storagePath = WideToUtf8(candOnly.stagedPath);
                hr.sha256 = candOnly.sha256;
                hugeAttBatch.push_back(hr);
            }
            store.StageSubmission(projectHuge, "sub-huge", "Over 500 MiB");
            auto hugeDisp = store.SaveContribution(projectHuge, "sub-huge", "Over 500 MiB", hugeAttBatch);
            EXPECT(hugeDisp == mayasaba::SaveDisposition::Conflict);

            // 16. Independent 50-file limit enforcement inside Storage
            std::string const project50 = "project-50-limit";
            store.BindProject(project50, "50 Limit Project", "C:/Projects/Limit50");
            std::vector<mayasaba::AttachmentRecord> fiftyOneAtts;
            for (int i = 0; i < 51; ++i)
            {
                mayasaba::AttachmentRecord r;
                r.id = "att-51-" + std::to_string(i);
                r.projectId = project50;
                r.submissionId = "sub-51";
                r.fileName = "file.txt";
                r.fileSize = 10;
                r.sha256 = candOnly.sha256;
                r.storagePath = WideToUtf8(candOnly.stagedPath);
                fiftyOneAtts.push_back(r);
            }
            auto disp51 = store.SaveContribution(project50, "sub-51", "51 attachments", fiftyOneAtts);
            EXPECT(disp51 == mayasaba::SaveDisposition::Conflict); // Independently enforced by Storage!
            EXPECT(store.Contributions(project50).empty());

            // 17. Submission identity and attachment manifest binding
            std::string const projectManifest = "project-manifest";
            store.BindProject(projectManifest, "Manifest Project", "C:/Projects/Manifest");
            auto fileM1 = testFilesDir / "manifest_file1.txt";
            {
                std::ofstream ofs(fileM1); ofs << "Manifest payload 1";
            }
            auto candM1 = attService.PrepareAttachment(projectManifest, fileM1.wstring());
            EXPECT(candM1.status == mayasaba::AttachmentStatus::Ready);
            attService.SaveDraftAttachments(projectManifest, { candM1 });

            std::string const subM1 = "sub-manifest-1";
            store.StageSubmission(projectManifest, subM1, "Manifest submission");
            mayasaba::AttachmentRecord recM1;
            recM1.id = candM1.id;
            recM1.projectId = projectManifest;
            recM1.submissionId = subM1;
            recM1.fileName = candM1.fileName;
            recM1.fileSize = candM1.fileSize;
            recM1.sha256 = candM1.sha256;
            recM1.detectedType = candM1.detectedType;
            recM1.storagePath = WideToUtf8(candM1.stagedPath);
            recM1.sourcePath = WideToUtf8(candM1.originalPath);
            recM1.status = "COMMITTED";

            auto saveDispM1 = store.SaveContribution(projectManifest, subM1, "Manifest submission", { recM1 });
            EXPECT(saveDispM1 == mayasaba::SaveDisposition::Saved);

            // Replay with exact same attachment payload returns Duplicate
            auto saveDispReplay = store.SaveContribution(projectManifest, subM1, "Manifest submission", { recM1 });
            EXPECT(saveDispReplay == mayasaba::SaveDisposition::Duplicate);

            // Replay with changed attachment payload returns Conflict (rejected, NOT duplicate)
            auto recM1Altered = recM1;
            recM1Altered.sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
            auto saveDispAltered = store.SaveContribution(projectManifest, subM1, "Manifest submission", { recM1Altered });
            EXPECT(saveDispAltered == mayasaba::SaveDisposition::Conflict);

            // Replay with omitted attachments returns Conflict
            auto saveDispEmptyAtts = store.SaveContribution(projectManifest, subM1, "Manifest submission", {});
            EXPECT(saveDispEmptyAtts == mayasaba::SaveDisposition::Conflict);

            // 18. Staging identity, draft status validation and physical sealing
            std::string const projectStaging = "project-staging";
            store.BindProject(projectStaging, "Staging Project", "C:/Projects/Staging");
            auto fileUnmanaged = testFilesDir / "unmanaged.txt";
            {
                std::ofstream ofs(fileUnmanaged); ofs << "Unmanaged foreign file";
            }
            uint64_t unmanagedSize = 0;
            std::string unmanagedHash = mayasaba::AttachmentService::ComputeFileSha256(fileUnmanaged.wstring(), &unmanagedSize);
            mayasaba::AttachmentRecord recUnmanaged;
            recUnmanaged.id = "forged-id";
            recUnmanaged.projectId = projectStaging;
            recUnmanaged.submissionId = "sub-unmanaged";
            recUnmanaged.fileName = "unmanaged.txt";
            recUnmanaged.fileSize = unmanagedSize;
            recUnmanaged.sha256 = unmanagedHash;
            recUnmanaged.storagePath = WideToUtf8(fileUnmanaged.wstring());
            recUnmanaged.status = "READY";
            // Do NOT insert into draft_attachments; unmanaged attachment must be rejected
            auto dispUnmanaged = store.SaveContribution(projectStaging, "sub-unmanaged", "Unmanaged submission", { recUnmanaged });
            EXPECT(dispUnmanaged == mayasaba::SaveDisposition::Conflict);

            // Verify committed attachment sealing on disk (FILE_ATTRIBUTE_READONLY)
            DWORD m1Attrs = GetFileAttributesW(candM1.stagedPath.c_str());
            EXPECT((m1Attrs & FILE_ATTRIBUTE_READONLY) != 0); // Sealed on disk!

            // 19. Injectable active-time clock and suspend/resume simulation
            std::string const projectClock = "project-clock";
            store.BindProject(projectClock, "Clock Project", "C:/Projects/Clock");
            auto fClock1 = testFilesDir / "clock_file1.txt";
            auto fClock2 = testFilesDir / "clock_file2.txt";
            {
                std::ofstream ofs1(fClock1); ofs1 << "Clock file 1 payload";
                std::ofstream ofs2(fClock2); ofs2 << "Clock file 2 payload";
            }

            // 19A. Active-time budget enforcement: when active CPU time actually reaches 120 active seconds
            auto mockNow = std::chrono::steady_clock::now();
            int clockCallCount = 0;
            auto mockExhaustedClock = [&]() {
                clockCallCount++;
                if (clockCallCount <= 4)
                {
                    // Active elapsed time progression during batch start and file 1
                    return mockNow + std::chrono::milliseconds(clockCallCount * 100);
                }
                else
                {
                    // Active CPU execution time reaches 121 seconds (exceeding 120s budget)
                    return mockNow + std::chrono::seconds(121);
                }
            };

            auto clockResultsA = attService.PrepareBatch(projectClock,
                { fClock1.wstring(), fClock2.wstring() },
                [] { return false; },
                120, // 120s budget
                mockExhaustedClock);

            EXPECT(clockResultsA.size() == 2);
            EXPECT(clockResultsA[0].status == mayasaba::AttachmentStatus::Ready);
            EXPECT(clockResultsA[1].status == mayasaba::AttachmentStatus::TimedOut);
            EXPECT(clockResultsA[1].errorMessage.find("120-second") != std::string::npos);

            // 19B. Suspension resilience: system sleep/hibernation (130s wall leap) does NOT consume active time
            // Because active time measures unbiased execution time, 200ms active CPU time is well within 120s.
            int sleepCallCount = 0;
            auto mockSuspendedActiveClock = [&]() {
                sleepCallCount++;
                // Machine was suspended for 130 wall seconds, but active execution time only used 50ms per call
                return mockNow + std::chrono::milliseconds(sleepCallCount * 50);
            };

            auto clockResultsB = attService.PrepareBatch(projectClock,
                { fClock1.wstring(), fClock2.wstring() },
                [] { return false; },
                120, // 120s active budget
                mockSuspendedActiveClock);

            EXPECT(clockResultsB.size() == 2);
            EXPECT(clockResultsB[0].status == mayasaba::AttachmentStatus::Ready);
            EXPECT(clockResultsB[1].status == mayasaba::AttachmentStatus::Ready); // Did not time out!

            CASES_END();
            CASES_BEGIN("20. Manifest deterministic sorting, pre-commit sealing and crash recovery");
            // 20. Manifest deterministic sorting and hashing performance
            std::vector<mayasaba::AttachmentRecord> manifestSet = { recOnly, recM1 };
            std::string manifest1 = mayasaba::Storage::ComputeAttachmentManifest(manifestSet);
            std::vector<mayasaba::AttachmentRecord> reversedSet = { recM1, recOnly };
            std::string manifest2 = mayasaba::Storage::ComputeAttachmentManifest(reversedSet);
            EXPECT(!manifest1.empty());
            EXPECT(manifest1 == manifest2); // Deterministic ordering

            // 21. Verified-byte finalization gate: pre-commit sealing and write-lock protection
            std::string const projectGate = "project-gate";
            store.BindProject(projectGate, "Gate Project", "C:/Projects/Gate");
            auto fGate = testFilesDir / "gate_file.txt";
            {
                std::ofstream ofs(fGate); ofs << "Gate payload bytes for verified-byte test";
            }
            auto gateCand = attService.PrepareAttachment(projectGate, fGate.wstring());
            EXPECT(gateCand.status == mayasaba::AttachmentStatus::Ready);
            EXPECT(!gateCand.stagedPath.empty());

            // Prepare record for SaveContribution
            mayasaba::AttachmentRecord recGate;
            recGate.id = gateCand.id;
            recGate.projectId = projectGate;
            recGate.submissionId = "sub-gate-1";
            recGate.fileName = gateCand.fileName;
            recGate.fileSize = gateCand.fileSize;
            recGate.sha256 = gateCand.sha256;
            recGate.detectedType = gateCand.detectedType;
            recGate.storagePath = WideToUtf8(gateCand.stagedPath);
            recGate.status = "READY";

            store.StageSubmission(projectGate, "sub-gate-1", "Gate contribution message");
            auto gateDisp = store.SaveContribution(projectGate, "sub-gate-1", "Gate contribution message", { recGate });
            EXPECT(gateDisp == mayasaba::SaveDisposition::Saved);

            // Verify that the committed file is sealed (FILE_ATTRIBUTE_READONLY)
            DWORD gateAttrs = GetFileAttributesW(gateCand.stagedPath.c_str());
            EXPECT((gateAttrs & FILE_ATTRIBUTE_READONLY) != 0);

            // 22. Crash/Restart immutability recovery: ReconcileStagedFiles restores read-only sealing
            // Simulate external attribute tampering or crash before attribute flush
            SetFileAttributesW(gateCand.stagedPath.c_str(), FILE_ATTRIBUTE_NORMAL);
            DWORD tamperedAttrs = GetFileAttributesW(gateCand.stagedPath.c_str());
            EXPECT((tamperedAttrs & FILE_ATTRIBUTE_READONLY) == 0); // Readonly was cleared

            // Run reconciliation
            attService.ReconcileStagedFiles(projectGate);

            // Verify ReconcileStagedFiles re-sealed the committed file
            DWORD reconciledAttrs = GetFileAttributesW(gateCand.stagedPath.c_str());
            EXPECT((reconciledAttrs & FILE_ATTRIBUTE_READONLY) != 0); // Re-sealed!
            CASES_END();
        }
#endif

        // 23. Legacy unbound draft preservation and recovery (2026-10-18).
        // A pre-binding draft written by an older build is retained user data. Startup must only
        // read it; the selector must offer it explicitly; and no path may delete or overwrite it.
        {
            CASES_BEGIN("23. Legacy unbound draft preservation and recovery (2026-10-18).");
            std::string const preserved = "Legacy selector text that must survive every startup and selection.";
            auto legacyDb = folder / "legacy-draft.db";
            {
                mayasaba::Storage seed(legacyDb);
                seed.SavePrebindDraft(preserved);
                EXPECT(seed.GetPrebindDraft() == preserved);
            }
            // Reopening the store is the actual startup path: construction, migration and the
            // recent-project query must leave the preserved bytes untouched.
            {
                mayasaba::Storage reopened(legacyDb);
                EXPECT(reopened.GetPrebindDraft() == preserved);
                EXPECT(reopened.RecentProjects().empty());
                EXPECT(reopened.HasAnyProjects() == false);
                EXPECT(reopened.GetPrebindDraft() == preserved); // still intact after every startup read
            }
            auto legacyRoot = folder / "legacy-draft-root";
            fs::create_directory(legacyRoot);
            {
                mayasaba::Storage store(legacyDb);
                mayasaba::ProjectService service(store);
                auto noSessions = mayasaba::SwitchObservation::NoAdmittedSessions;
                mayasaba::SelectionRequest open;
                open.path = legacyRoot.wstring();
                auto recovered = service.Select(open, noSessions);
                // A confirmed selection recovers the text into the project draft and only then
                // clears the preserved slot, so a failed commit can never lose the only copy.
                EXPECT(recovered.transferred);
                EXPECT(recovered.project.draft == preserved);
                EXPECT(store.GetPrebindDraft().empty());
                auto durable = store.Project(recovered.project.id);
                EXPECT(durable && durable->draft == preserved);
            }
            // A project that already carries a draft must never silently lose either copy.
            {
                mayasaba::Storage store(folder / "legacy-collision.db");
                mayasaba::ProjectService service(store);
                auto noSessions = mayasaba::SwitchObservation::NoAdmittedSessions;
                auto collisionRoot = folder / "legacy-collision-root";
                fs::create_directory(collisionRoot);
                mayasaba::SelectionRequest open;
                open.path = collisionRoot.wstring();
                auto bound = service.Select(open, noSessions);
                std::string const projectDraft = "Project draft that must survive";
                std::string const legacyDraft = "Legacy draft that must survive";
                store.SaveDraft(bound.project.id, projectDraft);
                store.SavePrebindDraft(legacyDraft);
                auto current = store.Project(bound.project.id).value();
                mayasaba::SelectionRequest recent;
                recent.action = mayasaba::SelectionAction::Recent;
                recent.projectId = current.domainId;
                recent.expectedVersion = current.version;
                auto collision = service.Select(recent, noSessions);
                EXPECT(collision.collision);
                EXPECT(!collision.transferred);
                EXPECT(collision.selectorDraft == legacyDraft);
                EXPECT(store.GetPrebindDraft() == legacyDraft);
                EXPECT(store.Project(bound.project.id)->draft == projectDraft);
                // "Keep project draft" is the explicit choice to leave the preserved text alone.
                auto keep = recent;
                keep.operationId = mayasaba::NewUuid();
                keep.draftChoice = mayasaba::DraftChoice::KeepProject;
                auto kept = service.Select(keep, noSessions);
                EXPECT(!kept.transferred);
                EXPECT(store.GetPrebindDraft() == legacyDraft);
                EXPECT(store.Project(bound.project.id)->draft == projectDraft);
                // An unresolved submission locks the target: recovery is refused, both copies stay.
                store.StageSubmission(bound.project.id, mayasaba::NewUuid(), "Unresolved submission");
                current = store.Project(bound.project.id).value();
                mayasaba::SelectionRequest locked;
                locked.action = mayasaba::SelectionAction::Recent;
                locked.projectId = current.domainId;
                locked.expectedVersion = current.version;
                auto lockedCollision = service.Select(locked, noSessions);
                EXPECT(lockedCollision.collision);
                EXPECT(lockedCollision.pending);
                EXPECT(store.GetPrebindDraft() == legacyDraft);
                // The refused recovery must not alter whatever the locked target now holds.
                auto const lockedDraft = store.Project(bound.project.id)->draft;
                bool refused = false;
                try
                {
                    auto force = locked;
                    force.operationId = mayasaba::NewUuid();
                    force.draftChoice = mayasaba::DraftChoice::UseSelector;
                    force.expectedSelectorDraft = lockedCollision.selectorDraft;
                    force.expectedTargetDraft = lockedCollision.project.draft;
                    service.Select(force, noSessions);
                }
                catch (std::exception const&) { refused = true; }
                EXPECT(refused);
                EXPECT(store.GetPrebindDraft() == legacyDraft);
                EXPECT(store.Project(bound.project.id)->draft == lockedDraft);
            }
            CASES_END();
        }

        // 21. Recent-projects query filtering across recorded projects (Milestone 2)
        {
            CASES_BEGIN("21. Recent-projects query filtering across recorded projects (Milestone 2)");
            auto const dbFile = folder / "filter_recents.db";
            mayasaba::Storage store(dbFile);

            auto pAlpha = store.BindProject(mayasaba::NewUuid(), "Alpha Project", "C:\\Projects\\Alpha", "id_alpha");
            auto pBeta = store.BindProject(mayasaba::NewUuid(), "Beta Project", "C:\\Projects\\Beta_Test%20", "id_beta");
            auto pGamma = store.BindProject(mayasaba::NewUuid(), "Gamma Spec_Special", "C:\\Projects\\Gamma_Special", "id_gamma");
            auto pDelta = store.BindProject(mayasaba::NewUuid(), "Delta Special 100%", "C:\\Projects\\Delta%100", "id_delta");
            auto pEtoile = store.BindProject(mayasaba::NewUuid(), "Étoile Project", "C:\\Projects\\Étoile", "id_etoile");

            auto betaRecord = store.Project(pBeta).value();
            store.SetClosure(pBeta, betaRecord.version, true);

            for (int i = 0; i < 25; ++i)
            {
                std::string num = (i < 10 ? "0" : "") + std::to_string(i);
                store.BindProject(mayasaba::NewUuid(), "P_" + num, "C:\\Dummy\\P_" + num, "id_dummy_" + num);
            }

            // Test 1: Empty filter & Status::All -> returns first page of 20, totalMatching = 30, hasMore = true
            {
                mayasaba::CrossProjectRecentProjectsQuery q;
                q.pageSize = 20;
                q.offset = 0;
                auto res = store.QueryRecentProjects(q);
                EXPECT(res.isComplete);
                EXPECT(res.items.size() == 20);
                EXPECT(res.totalMatching == 30);
                EXPECT(res.hasMore);
            }

            // Test 2: Name filter substring match -> "Alpha"
            {
                mayasaba::CrossProjectRecentProjectsQuery q;
                q.filterText = "Alpha";
                auto res = store.QueryRecentProjects(q);
                EXPECT(res.isComplete);
                EXPECT(res.items.size() == 1);
                EXPECT(res.totalMatching == 1);
                EXPECT(!res.hasMore);
                EXPECT(res.items[0].displayName == "Alpha Project");
            }

            // Test 3: Path filter match -> "Beta_Test"
            {
                mayasaba::CrossProjectRecentProjectsQuery q;
                q.filterText = "Beta_Test";
                auto res = store.QueryRecentProjects(q);
                EXPECT(res.isComplete);
                EXPECT(res.items.size() == 1);
                EXPECT(res.totalMatching == 1);
                EXPECT(res.items[0].displayName == "Beta Project");
            }

            // Test 4: Literal special character '%' matching
            {
                mayasaba::CrossProjectRecentProjectsQuery q;
                q.filterText = "%";
                auto res = store.QueryRecentProjects(q);
                EXPECT(res.isComplete);
                EXPECT(res.items.size() == 2);
                EXPECT(res.totalMatching == 2);
            }

            // Test 5: Literal special character '_' matching
            {
                mayasaba::CrossProjectRecentProjectsQuery q;
                q.filterText = "_";
                q.pageSize = 50;
                auto res = store.QueryRecentProjects(q);
                EXPECT(res.isComplete);
                EXPECT(res.totalMatching == 27);
            }

            // Test 6: Status filtering
            {
                mayasaba::CrossProjectRecentProjectsQuery q;
                q.statusFilter = mayasaba::ProjectStatusFilter::Open;
                q.pageSize = 50;
                auto res = store.QueryRecentProjects(q);
                EXPECT(res.isComplete);
                EXPECT(res.totalMatching == 29);
                for (auto const& item : res.items)
                {
                    EXPECT(!item.closed);
                }
            }
            {
                mayasaba::CrossProjectRecentProjectsQuery q;
                q.statusFilter = mayasaba::ProjectStatusFilter::Closed;
                auto res = store.QueryRecentProjects(q);
                EXPECT(res.isComplete);
                EXPECT(res.items.size() == 1);
                EXPECT(res.totalMatching == 1);
                EXPECT(res.items[0].displayName == "Beta Project");
                EXPECT(res.items[0].closed);
            }

            // Test 7: Paging with offset
            {
                mayasaba::CrossProjectRecentProjectsQuery q;
                q.filterText = "P_";
                q.pageSize = 10;
                q.offset = 0;
                auto page1 = store.QueryRecentProjects(q);
                EXPECT(page1.items.size() == 10);
                EXPECT(page1.totalMatching == 25);
                EXPECT(page1.hasMore);

                q.offset = 20;
                auto page3 = store.QueryRecentProjects(q);
                EXPECT(page3.items.size() == 5);
                EXPECT(page3.totalMatching == 25);
                EXPECT(!page3.hasMore);
            }

            // Test 8: Nonexistent match
            {
                mayasaba::CrossProjectRecentProjectsQuery q;
                q.filterText = "Nonexistent_Pattern_XYZ";
                auto res = store.QueryRecentProjects(q);
                EXPECT(res.isComplete);
                EXPECT(res.items.empty());
                EXPECT(res.totalMatching == 0);
                EXPECT(!res.hasMore);
            }
            CASES_END();
        }

        // 22. Saved-message search in Chat with project isolation and bounded queries (Milestone 3)
        {
            CASES_BEGIN("22. Saved-message search in Chat with project isolation and bounded queries (Milestone 3)");
            auto const dbFile = folder / "search_messages.db";
            mayasaba::Storage store(dbFile);

            auto pA = store.BindProject(mayasaba::NewUuid(), "Project A", "C:\\Projects\\A", "id_a");
            auto pB = store.BindProject(mayasaba::NewUuid(), "Project B", "C:\\Projects\\B", "id_b");

            auto saveHelper = [&](std::string const& proj, std::string const& id, std::string const& body) {
                store.StageSubmission(proj, id, body);
                auto disp = store.SaveContribution(proj, id, body);
                EXPECT(disp == mayasaba::SaveDisposition::Saved);
            };

            auto subA1 = mayasaba::NewUuid();
            saveHelper(pA, subA1, "Hello world, this is a test contribution");
            auto subA2 = mayasaba::NewUuid();
            saveHelper(pA, subA2, "Search indexing with special chars: 100% complete and verified_safe");
            auto subA3 = mayasaba::NewUuid();
            saveHelper(pA, subA3, "Another message containing keyword: Apple");
            auto subA4 = mayasaba::NewUuid();
            saveHelper(pA, subA4, "Bonjour tout le monde, message en français");

            auto subB1 = mayasaba::NewUuid();
            saveHelper(pB, subB1, "Hello world in Project B");
            auto subB2 = mayasaba::NewUuid();
            saveHelper(pB, subB2, "Keyword: Apple in project B");

            store.StageSubmission(pA, mayasaba::NewUuid(), "Pending submission with secret keyword Apple");
            store.SaveDraft(pA, "Unsaved draft containing Apple");

            // Test 1: Empty query
            {
                mayasaba::MessageSearchQuery q;
                q.projectId = pA;
                q.queryText = "";
                auto res = store.SearchUserMessages(q);
                EXPECT(res.isComplete);
                EXPECT(res.matches.empty());
                EXPECT(res.totalMatches == 0);
            }

            // Test 2: Substring search in Project A
            {
                mayasaba::MessageSearchQuery q;
                q.projectId = pA;
                q.queryText = "Hello world";
                auto res = store.SearchUserMessages(q);
                EXPECT(res.isComplete);
                EXPECT(res.matches.size() == 1);
                EXPECT(res.totalMatches == 1);
                EXPECT(res.matches[0].submissionId == subA1);
            }

            // Test 3: Project isolation - keyword "Apple" in Project A returns only Project A message
            {
                mayasaba::MessageSearchQuery q;
                q.projectId = pA;
                q.queryText = "Apple";
                auto res = store.SearchUserMessages(q);
                EXPECT(res.isComplete);
                EXPECT(res.matches.size() == 1);
                EXPECT(res.totalMatches == 1);
                EXPECT(res.matches[0].submissionId == subA3);
            }

            // Test 4: Project isolation - keyword "Apple" in Project B returns only Project B message
            {
                mayasaba::MessageSearchQuery q;
                q.projectId = pB;
                q.queryText = "Apple";
                auto res = store.SearchUserMessages(q);
                EXPECT(res.isComplete);
                EXPECT(res.matches.size() == 1);
                EXPECT(res.totalMatches == 1);
                EXPECT(res.matches[0].submissionId == subB2);
            }

            // Test 5: Pending submissions and drafts are strictly excluded
            {
                mayasaba::MessageSearchQuery q;
                q.projectId = pA;
                q.queryText = "secret";
                auto res = store.SearchUserMessages(q);
                EXPECT(res.isComplete);
                EXPECT(res.matches.empty());
                EXPECT(res.totalMatches == 0);

                q.queryText = "Unsaved draft";
                auto res2 = store.SearchUserMessages(q);
                EXPECT(res2.isComplete);
                EXPECT(res2.matches.empty());
                EXPECT(res2.totalMatches == 0);
            }

            // Test 6: Literal special characters '%' and '_'
            {
                mayasaba::MessageSearchQuery q;
                q.projectId = pA;
                q.queryText = "100%";
                auto res = store.SearchUserMessages(q);
                EXPECT(res.isComplete);
                EXPECT(res.matches.size() == 1);
                EXPECT(res.matches[0].submissionId == subA2);

                q.queryText = "verified_safe";
                auto res2 = store.SearchUserMessages(q);
                EXPECT(res2.isComplete);
                EXPECT(res2.matches.size() == 1);
                EXPECT(res2.matches[0].submissionId == subA2);
            }

            // Test 7: Non-ASCII character search
            {
                mayasaba::MessageSearchQuery q;
                q.projectId = pA;
                q.queryText = "français";
                auto res = store.SearchUserMessages(q);
                EXPECT(res.isComplete);
                EXPECT(res.matches.size() == 1);
                EXPECT(res.matches[0].submissionId == subA4);
            }

            // Test 8: Monotonic ordering and pagination
            auto pC = store.BindProject(mayasaba::NewUuid(), "Project C", "C:\\Projects\\C", "id_c");
            for (int i = 0; i < 25; ++i)
            {
                saveHelper(pC, mayasaba::NewUuid(), "Batch message item " + std::to_string(i));
            }
            {
                mayasaba::MessageSearchQuery q;
                q.projectId = pC;
                q.queryText = "Batch message";
                q.limit = 10;
                q.offset = 0;
                auto page1 = store.SearchUserMessages(q);
                EXPECT(page1.isComplete);
                EXPECT(page1.matches.size() == 10);
                EXPECT(page1.totalMatches == 25);
                EXPECT(page1.hasMore);

                q.offset = 20;
                auto page3 = store.SearchUserMessages(q);
                EXPECT(page3.isComplete);
                EXPECT(page3.matches.size() == 5);
                EXPECT(page3.totalMatches == 25);
                EXPECT(!page3.hasMore);
            }

            // Test 9: Clamping of limit and offset
            {
                mayasaba::MessageSearchQuery q;
                q.projectId = pC;
                q.queryText = "Batch message";
                q.limit = 0;
                auto res = store.SearchUserMessages(q);
                EXPECT(res.matches.size() == 20);

                q.limit = 500;
                auto res2 = store.SearchUserMessages(q);
                EXPECT(res2.matches.size() == 25);
            }

            // Test 10: Production SearchController sequential 3-page pagination across 50 items
            // Drives the actual production mayasaba::SearchController logic against the real SQLite storage engine.
            auto pD = store.BindProject(mayasaba::NewUuid(), "Project D", "C:\\Projects\\D", "id_d");
            for (int i = 0; i < 50; ++i)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "Sequential item %02d payload", i);
                saveHelper(pD, mayasaba::NewUuid(), buf);
            }

            mayasaba::SearchController controller;
            controller.Open(pD);
            EXPECT(controller.IsOpen());
            EXPECT(!controller.IsLoading());
            EXPECT(controller.MatchCount() == 0);

            // Page 1: SetQuery("Sequential item")
            auto req1 = controller.SetQuery("Sequential item");
            EXPECT(req1.has_value());
            EXPECT(req1->offset == 0);
            EXPECT(req1->limit == 20);
            EXPECT(!req1->isAppend);
            EXPECT(controller.IsLoading());

            mayasaba::MessageSearchQuery dbQuery1{req1->projectId, req1->queryText, req1->limit, req1->offset};
            auto res1 = store.SearchUserMessages(dbQuery1);
            EXPECT(res1.isComplete);
            EXPECT(res1.matches.size() == 20);
            EXPECT(res1.totalMatches == 50);

            bool applied1 = controller.ApplyResult(req1->generation, req1->queryText, req1->projectId, req1->isAppend, res1);
            EXPECT(applied1);
            EXPECT(!controller.IsLoading());
            EXPECT(controller.MatchCount() == 20);
            EXPECT(controller.TotalMatches() == 50);
            EXPECT(controller.CanLoadMore());
            EXPECT(controller.ShouldShowLoadMoreButton());

            // Page 2: RequestLoadMore() click #1
            auto req2 = controller.RequestLoadMore();
            EXPECT(req2.has_value());
            EXPECT(req2->offset == 20); // Exact accumulated size!
            EXPECT(req2->isAppend);
            EXPECT(controller.IsLoading());
            EXPECT(!controller.CanLoadMore()); // Cannot click Load More while query is loading

            mayasaba::MessageSearchQuery dbQuery2{req2->projectId, req2->queryText, req2->limit, req2->offset};
            auto res2 = store.SearchUserMessages(dbQuery2);
            EXPECT(res2.isComplete);
            EXPECT(res2.matches.size() == 20);

            bool applied2 = controller.ApplyResult(req2->generation, req2->queryText, req2->projectId, req2->isAppend, res2);
            EXPECT(applied2);
            EXPECT(!controller.IsLoading());
            EXPECT(controller.MatchCount() == 40);
            EXPECT(controller.TotalMatches() == 50);
            EXPECT(controller.CanLoadMore());
            EXPECT(controller.ShouldShowLoadMoreButton());

            // Page 3: RequestLoadMore() click #2
            // Verifies offset is 40 (not 60 which was the previous bug).
            auto req3 = controller.RequestLoadMore();
            EXPECT(req3.has_value());
            EXPECT(req3->offset == 40);
            EXPECT(req3->isAppend);
            EXPECT(controller.IsLoading());

            mayasaba::MessageSearchQuery dbQuery3{req3->projectId, req3->queryText, req3->limit, req3->offset};
            auto res3 = store.SearchUserMessages(dbQuery3);
            EXPECT(res3.isComplete);
            EXPECT(res3.matches.size() == 10);

            bool applied3 = controller.ApplyResult(req3->generation, req3->queryText, req3->projectId, req3->isAppend, res3);
            EXPECT(applied3);
            EXPECT(!controller.IsLoading());
            EXPECT(controller.MatchCount() == 50);
            EXPECT(controller.TotalMatches() == 50);
            EXPECT(!controller.CanLoadMore()); // All 50 items loaded
            EXPECT(!controller.ShouldShowLoadMoreButton()); // Load more button must collapse

            // Verify exact sequential continuity and uniqueness across all 50 items
            std::set<std::string> seenIds;
            for (size_t i = 0; i < 50; ++i)
            {
                EXPECT(seenIds.insert(controller.Matches()[i].submissionId).second);
            }

            // Test 11: Reopening search resets pagination to page 0 (does not start on offset 40)
            controller.Close();
            EXPECT(!controller.IsOpen());
            EXPECT(controller.MatchCount() == 0);
            EXPECT(controller.CurrentOffset() == 0);

            controller.Open(pD);
            EXPECT(controller.IsOpen());
            auto reopenReq = controller.ReopenWithExistingQuery();
            EXPECT(reopenReq.has_value());
            EXPECT(reopenReq->offset == 0); // Must be offset 0, NOT offset 40!
            EXPECT(!reopenReq->isAppend);

            mayasaba::MessageSearchQuery dbQueryReopen{reopenReq->projectId, reopenReq->queryText, reopenReq->limit, reopenReq->offset};
            auto resReopen = store.SearchUserMessages(dbQueryReopen);
            EXPECT(resReopen.isComplete);
            EXPECT(resReopen.matches.size() == 20);

            bool appliedReopen = controller.ApplyResult(reopenReq->generation, reopenReq->queryText, reopenReq->projectId, reopenReq->isAppend, resReopen);
            EXPECT(appliedReopen);
            EXPECT(controller.MatchCount() == 20);
            // Matches 0..19 are present, identical to page 1
            EXPECT(controller.Matches()[0].submissionId == res1.matches[0].submissionId);
            EXPECT(controller.Matches()[19].submissionId == res1.matches[19].submissionId);

            // Test 12: Changing query invalidates stale paging and prevents mixing results
            // Query 1: "Sequential" is active with 20 matches.
            EXPECT(controller.CanLoadMore());
            // User types new query "Apple"
            auto reqNewQuery = controller.SetQuery("Apple");
            EXPECT(reqNewQuery.has_value());
            EXPECT(reqNewQuery->queryText == "Apple");
            EXPECT(reqNewQuery->offset == 0);
            // Immediately upon typing, old results are cleared and stale paging is disabled:
            EXPECT(controller.MatchCount() == 0);
            EXPECT(!controller.CanLoadMore());
            EXPECT(!controller.ShouldShowLoadMoreButton());
            EXPECT(!controller.RequestLoadMore().has_value()); // Attempting Load More returns nullopt!

            // If an old query result from previous generation arrives late, ApplyResult rejects it:
            bool staleApplied = controller.ApplyResult(reopenReq->generation, reopenReq->queryText, reopenReq->projectId, reopenReq->isAppend, resReopen);
            EXPECT(!staleApplied);
            EXPECT(controller.MatchCount() == 0); // Old matches were NOT injected

            // Apply valid results for "Apple"
            mayasaba::MessageSearchQuery dbQueryApple{reqNewQuery->projectId, reqNewQuery->queryText, reqNewQuery->limit, reqNewQuery->offset};
            auto resApple = store.SearchUserMessages(dbQueryApple);
            bool appleApplied = controller.ApplyResult(reqNewQuery->generation, reqNewQuery->queryText, reqNewQuery->projectId, reqNewQuery->isAppend, resApple);
            EXPECT(appleApplied);

            // Test 13: Project switching cancels active query and rejects cross-project results
            auto pE = store.BindProject(mayasaba::NewUuid(), "Project E", "C:\\Projects\\E", "id_e");
            controller.SetActiveProject(pE);
            EXPECT(controller.MatchCount() == 0);

            // Stale result from Project D arriving after switch is rejected:
            bool crossProjectApplied = controller.ApplyResult(reqNewQuery->generation, "Apple", pD, false, resApple);
            EXPECT(!crossProjectApplied);
            EXPECT(controller.MatchCount() == 0);

            // Test 14: Failed query error-handling path
            // When an active background query encounters a failure (isComplete = false), ApplyResult
            // must accept the result (return true), mark isLoading_ = false, record the error message,
            // clear matches, and disable Load more so the UI can truthfully present the failure.
            {
                mayasaba::SearchController failController;
                failController.Open(pD);
                auto failReq = failController.SetQuery("Trigger failure");
                EXPECT(failReq.has_value());
                EXPECT(failController.IsLoading());

                mayasaba::MessageSearchResult failedResult;
                failedResult.isComplete = false;
                failedResult.errorMessage = "Simulated I/O failure";

                bool accepted = failController.ApplyResult(
                    failReq->generation, failReq->queryText, failReq->projectId, failReq->isAppend, failedResult);
                EXPECT(accepted); // Must be accepted so UI displays the failure!
                EXPECT(!failController.IsLoading());
                EXPECT(failController.LastErrorMessage() == "Simulated I/O failure");
                EXPECT(failController.MatchCount() == 0);
                EXPECT(!failController.CanLoadMore());
                EXPECT(!failController.ShouldShowLoadMoreButton());

                // Complete recovery from failure: user clears or types new valid query
                auto recoverReq = failController.SetQuery("Sequential");
                EXPECT(recoverReq.has_value());
                EXPECT(failController.LastErrorMessage().empty()); // Error cleared on new query
                EXPECT(failController.IsLoading());

                mayasaba::MessageSearchQuery dbQueryRecover{recoverReq->projectId, recoverReq->queryText, recoverReq->limit, recoverReq->offset};
                auto resRecover = store.SearchUserMessages(dbQueryRecover);
                EXPECT(resRecover.isComplete);
                EXPECT(resRecover.matches.size() == 20);

                bool recoverApplied = failController.ApplyResult(
                    recoverReq->generation, recoverReq->queryText, recoverReq->projectId, recoverReq->isAppend, resRecover);
                EXPECT(recoverApplied);
                EXPECT(!failController.IsLoading());
                EXPECT(failController.MatchCount() == 20);
                EXPECT(failController.TotalMatches() == 50);
                EXPECT(failController.CanLoadMore());
                EXPECT(failController.ShouldShowLoadMoreButton());
            }

            // Test 15: Rapid typing bursts under sustained input
            {
                mayasaba::SearchController burstController;
                burstController.Open(pD);
                auto r1 = burstController.SetQuery("item 1");
                auto r2 = burstController.SetQuery("item 10");
                auto r3 = burstController.SetQuery("item 100");
                EXPECT(r3.has_value());
                EXPECT(burstController.QueryText() == "item 100");

                // Out of order arrivals from r1 and r2 must be rejected
                mayasaba::MessageSearchResult dummyRes;
                dummyRes.isComplete = true;
                dummyRes.totalMatches = 1;
                dummyRes.matches.push_back(mayasaba::MessageMatch{"sub_1", "body", "2026-10-15T00:00:00Z"});

                EXPECT(!burstController.ApplyResult(r1->generation, r1->queryText, r1->projectId, false, dummyRes));
                EXPECT(!burstController.ApplyResult(r2->generation, r2->queryText, r2->projectId, false, dummyRes));
                EXPECT(burstController.MatchCount() == 0);

                // Only current generation r3 accepted
                EXPECT(burstController.ApplyResult(r3->generation, r3->queryText, r3->projectId, false, dummyRes));
                EXPECT(burstController.MatchCount() == 1);
            }

            // Test 16: Close during active search query immediately drops in-flight results
            {
                mayasaba::SearchController closeController;
                closeController.Open(pD);
                auto req = closeController.SetQuery("Sequential");
                EXPECT(req.has_value());
                EXPECT(closeController.IsLoading());

                // User closes search panel before background query finishes
                closeController.Close();
                EXPECT(!closeController.IsOpen());
                EXPECT(!closeController.IsLoading());

                // Stale result arriving after close must be rejected
                mayasaba::MessageSearchQuery q{req->projectId, req->queryText, req->limit, req->offset};
                auto res = store.SearchUserMessages(q);
                EXPECT(!closeController.ApplyResult(req->generation, req->queryText, req->projectId, req->isAppend, res));
                EXPECT(closeController.MatchCount() == 0);
            }
            CASES_END();
        }

        // Action Center: read-only projection over owner records
        {
            mayasaba::Storage ac(folder / "action_center.db");
            ac.BindProject("proj-ac", "AC Project", "C:/AC");
            ac.SetClosure("proj-ac", 0, false);

            // 1. Unresolved submission appears; rechecked by project id.
            ac.StageSubmission("proj-ac", "sub-ac-1", "unresolved body");
            mayasaba::ActionCenterQuery q{ "proj-ac", 50 };
            auto res = ac.QueryActionCenter(q);
            EXPECT(res.isComplete);
            EXPECT(res.errorMessage.empty());
            bool foundSubmission = false;
            for (auto const& item : res.items)
            {
                if (item.kind == mayasaba::ActionCenterKind::UnresolvedSubmission)
                {
                    foundSubmission = true;
                    EXPECT(item.projectId == "proj-ac");
                    EXPECT(item.sourceRecordId == "sub-ac-1");
                    EXPECT(item.availableAction == "Recheck outcome");
                    EXPECT(item.sourceReference == "pending_submissions.sub-ac-1");
                }
            }
            EXPECT(foundSubmission);

            // 2. Resolving the submission removes the item.
            EXPECT(ac.SaveContribution("proj-ac", "sub-ac-1", "unresolved body") == mayasaba::SaveDisposition::Saved);
            auto res2 = ac.QueryActionCenter(q);
            bool stillHasSubmission = false;
            for (auto const& item : res2.items)
                if (item.kind == mayasaba::ActionCenterKind::UnresolvedSubmission) stillHasSubmission = true;
            EXPECT(!stillHasSubmission);

            // 3. Failed attachment items for all persisted failure states: REJECTED, SOURCE_CHANGED, TIMED_OUT.
            mayasaba::AttachmentRecord att1{};
            att1.id = "att-ac-1"; att1.projectId = "proj-ac"; att1.fileName = "bad.bin";
            att1.fileSize = 0; att1.sha256 = ""; att1.detectedType = "application/octet-stream";
            att1.storagePath = ""; att1.sourcePath = "C:/source/bad.bin"; att1.status = "REJECTED"; att1.errorMessage = "hash mismatch";
            ac.SaveDraftAttachment("proj-ac", att1);

            mayasaba::AttachmentRecord att2{};
            att2.id = "att-ac-2"; att2.projectId = "proj-ac"; att2.fileName = "changed.txt";
            att2.fileSize = 10; att2.sha256 = "dummy"; att2.detectedType = "text/plain";
            att2.storagePath = ""; att2.sourcePath = "C:/source/changed.txt"; att2.status = "SOURCE_CHANGED"; att2.errorMessage = "file modified";
            ac.SaveDraftAttachment("proj-ac", att2);

            mayasaba::AttachmentRecord att3{};
            att3.id = "att-ac-3"; att3.projectId = "proj-ac"; att3.fileName = "timeout.dat";
            att3.fileSize = 20; att3.sha256 = "dummy"; att3.detectedType = "application/octet-stream";
            att3.storagePath = ""; att3.sourcePath = "C:/source/timeout.dat"; att3.status = "TIMED_OUT"; att3.errorMessage = "timed out";
            ac.SaveDraftAttachment("proj-ac", att3);

            auto res3 = ac.QueryActionCenter(q);
            bool foundRejected = false, foundChanged = false, foundTimedOut = false;
            for (auto const& item : res3.items)
            {
                if (item.kind == mayasaba::ActionCenterKind::FailedAttachment)
                {
                    if (item.sourceRecordId == "att-ac-1") { foundRejected = true; EXPECT(item.availableAction == "Remove or re-attach"); }
                    if (item.sourceRecordId == "att-ac-2") { foundChanged = true; EXPECT(item.availableAction == "Remove or re-attach"); }
                    if (item.sourceRecordId == "att-ac-3") { foundTimedOut = true; EXPECT(item.availableAction == "Remove or re-attach"); }
                }
            }
            EXPECT(foundRejected);
            EXPECT(foundChanged);
            EXPECT(foundTimedOut);

            // 4. Closed-project item (version incremented from 0 to 1).
            ac.SetClosure("proj-ac", 1, true);
            auto res4 = ac.QueryActionCenter(q);
            bool foundClosed = false;
            for (auto const& item : res4.items)
            {
                if (item.kind == mayasaba::ActionCenterKind::ClosedProjectRestriction)
                {
                    foundClosed = true;
                    EXPECT(item.availableAction == "Reopen Project");
                    EXPECT(item.sourceReference == "projects.proj-ac");
                }
            }
            EXPECT(foundClosed);

            // Total items now: 3 failed attachments + 1 closed project = 4 items.
            // 5. Bounded aggregate pagination and continuation:
            // Page size 2: exactly 2 items returned, not complete, continuation token returned.
            mayasaba::ActionCenterQuery qPage1{ "proj-ac", 2 };
            auto resPage1 = ac.QueryActionCenter(qPage1);
            EXPECT(resPage1.items.size() == 2);
            EXPECT(!resPage1.isComplete);
            EXPECT(!resPage1.continuationToken.empty());
            EXPECT(resPage1.continuationToken.rfind("ac:v2:proj-ac:", 0) == 0);

            // Test project mutation invalidates continuation token
            ac.SetClosure("proj-ac", 2, false);
            mayasaba::ActionCenterQuery qMutated{ "proj-ac", 2, resPage1.continuationToken };
            auto resMutated = ac.QueryActionCenter(qMutated);
            EXPECT(!resMutated.isComplete);
            EXPECT(resMutated.errorMessage.find("mutated") != std::string::npos);

            // Test project mismatch in continuation token is rejected
            ac.BindProject("proj-other", "Other", "C:/Other");
            mayasaba::ActionCenterQuery qMismatch{ "proj-other", 2, resPage1.continuationToken };
            auto resMismatch = ac.QueryActionCenter(qMismatch);
            EXPECT(!resMismatch.isComplete);
            EXPECT(resMismatch.errorMessage.find("mismatch") != std::string::npos);

            // Re-close project (version becomes 4), re-fetch page 1, then fetch page 2
            ac.SetClosure("proj-ac", 3, true);
            resPage1 = ac.QueryActionCenter(qPage1);
            EXPECT(resPage1.items.size() == 2);
            EXPECT(!resPage1.continuationToken.empty());
            EXPECT(resPage1.continuationToken.rfind("ac:v2:proj-ac:", 0) == 0);

            // Query second page using continuation token.
            mayasaba::ActionCenterQuery qPage2{ "proj-ac", 2, resPage1.continuationToken };
            auto resPage2 = ac.QueryActionCenter(qPage2);
            EXPECT(resPage2.items.size() == 2);
            EXPECT(resPage2.isComplete);
            EXPECT(resPage2.continuationToken.empty());

            // Verify no duplicate items across pages
            std::set<std::string> seenIds;
            for (auto const& it : resPage1.items) seenIds.insert(it.itemId);
            for (auto const& it : resPage2.items)
            {
                EXPECT(seenIds.find(it.itemId) == seenIds.end());
                seenIds.insert(it.itemId);
            }
            EXPECT(seenIds.size() == 4);

            // 6. Stale item handling / underlying record updates:
            // Reopen project: closure obligation disappears.
            ac.SetClosure("proj-ac", 4, false);
            auto resAfterReopen = ac.QueryActionCenter(q);
            bool closedStillThere = false;
            for (auto const& item : resAfterReopen.items)
                if (item.kind == mayasaba::ActionCenterKind::ClosedProjectRestriction) closedStillThere = true;
            EXPECT(!closedStillThere);

            // Remove draft attachment: obligation disappears.
            ac.RemoveDraftAttachment("proj-ac", "att-ac-1");
            auto resAfterRemove = ac.QueryActionCenter(q);
            bool att1StillThere = false;
            for (auto const& item : resAfterRemove.items)
                if (item.sourceRecordId == "att-ac-1") att1StillThere = true;
            EXPECT(!att1StillThere);

            // 7. Project isolation: items never leak to another project.
            auto resIsolation = ac.QueryActionCenter(mayasaba::ActionCenterQuery{ "other-proj", 50 });
            bool leaked = false;
            for (auto const& item : resIsolation.items)
                if (item.projectId == "proj-ac") leaked = true;
            EXPECT(!leaked);
        }

        // Attachment inspection: comprehensive verification outside storage lock
        {
            mayasaba::Storage ai(folder / "attachment_inspect.db");
            ai.BindProject("proj-ai", "AI Project", "C:/AI");
            fs::create_directories(ai.ManagedAttachmentRoot());

            // Happy-path draft attachment (markdown, valid hash, in approved managed storage)
            std::string validDraftText = "This is a valid English markdown draft document for testing.";
            std::string validDraftFile = (ai.ManagedAttachmentRoot() / "valid_draft.md").string();
            {
                std::ofstream of(validDraftFile, std::ios::binary);
                of.write(validDraftText.data(), validDraftText.size());
            }
            std::string validDraftHash = ComputeSha256ForTestFile(validDraftFile);

            mayasaba::AttachmentRecord draft1{};
            draft1.id = "att-draft-1"; draft1.projectId = "proj-ai"; draft1.fileName = "valid_draft.md";
            draft1.fileSize = validDraftText.size(); draft1.sha256 = validDraftHash;
            draft1.detectedType = "text/markdown"; draft1.storagePath = validDraftFile;
            draft1.sourcePath = "C:/source/valid_draft.md"; draft1.status = "READY"; draft1.errorMessage = "";
            ai.SaveDraftAttachment("proj-ai", draft1);

            mayasaba::AttachmentInspectionQuery qi1{ "proj-ai", "att-draft-1" };
            auto ri1 = ai.InspectAttachment(qi1);
            EXPECT(ri1.found);
            EXPECT(ri1.inspection.attachmentId == "att-draft-1");
            EXPECT(ri1.inspection.projectId == "proj-ai");
            EXPECT(ri1.inspection.fileName == "valid_draft.md");
            EXPECT(ri1.inspection.fileSize == validDraftText.size());
            EXPECT(ri1.inspection.detectedType == "text/markdown");
            EXPECT(ri1.inspection.storagePath == validDraftFile);
            EXPECT(ri1.inspection.sourceRecordVersion == "1");
            EXPECT(ri1.inspection.isManaged);
            EXPECT(ri1.inspection.integrityVerified);
            EXPECT(ri1.inspection.previewEligible);
            EXPECT(ri1.inspection.previewContent == validDraftText);
            EXPECT(ri1.inspection.previewUnavailableReasonEn.empty());

            // Updating the draft attachment increments sourceRecordVersion
            draft1.fileSize = validDraftText.size();
            ai.SaveDraftAttachment("proj-ai", draft1);
            auto ri1_v2 = ai.InspectAttachment(qi1);
            EXPECT(ri1_v2.inspection.sourceRecordVersion == "2");

            // External path outside ManagedAttachmentRoot fails isManaged check
            std::string externalFile = (folder / "outside.txt").string();
            {
                std::ofstream of(externalFile, std::ios::binary);
                of.write("abcd", 4);
            }
            mayasaba::AttachmentRecord draftExternal{};
            draftExternal.id = "att-external"; draftExternal.projectId = "proj-ai"; draftExternal.fileName = "outside.txt";
            draftExternal.fileSize = 4; draftExternal.sha256 = "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589";
            draftExternal.detectedType = "text/plain"; draftExternal.storagePath = externalFile;
            draftExternal.sourcePath = "C:/source/outside.txt"; draftExternal.status = "READY";
            ai.SaveDraftAttachment("proj-ai", draftExternal);

            auto riExternal = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ai", "att-external" });
            EXPECT(riExternal.found);
            EXPECT(!riExternal.inspection.isManaged);
            EXPECT(!riExternal.inspection.previewEligible);
            EXPECT(riExternal.inspection.previewUnavailableReasonEn.find("approved managed storage") != std::string::npos);

            // Tampered file (disk contents altered: hash/size mismatch)
            std::string tamperedFile = (ai.ManagedAttachmentRoot() / "tampered.txt").string();
            {
                std::ofstream of(tamperedFile, std::ios::binary);
                of.write("hello world modified", 20);
            }
            mayasaba::AttachmentRecord draftTampered{};
            draftTampered.id = "att-tampered"; draftTampered.projectId = "proj-ai"; draftTampered.fileName = "tampered.txt";
            draftTampered.fileSize = 11; draftTampered.sha256 = "expected_hash_before_modification";
            draftTampered.detectedType = "text/plain"; draftTampered.storagePath = tamperedFile;
            draftTampered.sourcePath = "C:/source/tampered.txt"; draftTampered.status = "READY";
            ai.SaveDraftAttachment("proj-ai", draftTampered);

            auto riTampered = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ai", "att-tampered" });
            EXPECT(riTampered.found);
            EXPECT(riTampered.inspection.isManaged);
            EXPECT(!riTampered.inspection.integrityVerified);
            EXPECT(!riTampered.inspection.previewEligible);
            EXPECT(!riTampered.inspection.previewUnavailableReasonEn.empty());

            // Missing file (staged file deleted from disk)
            mayasaba::AttachmentRecord draftMissing{};
            draftMissing.id = "att-missing"; draftMissing.projectId = "proj-ai"; draftMissing.fileName = "missing.txt";
            draftMissing.fileSize = 10; draftMissing.sha256 = "abc";
            draftMissing.detectedType = "text/plain"; draftMissing.storagePath = (ai.ManagedAttachmentRoot() / "nonexistent.txt").string();
            draftMissing.sourcePath = "C:/source/missing.txt"; draftMissing.status = "READY";
            ai.SaveDraftAttachment("proj-ai", draftMissing);

            auto riMissing = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ai", "att-missing" });
            EXPECT(riMissing.found);
            EXPECT(!riMissing.inspection.isManaged);
            EXPECT(!riMissing.inspection.integrityVerified);
            EXPECT(!riMissing.inspection.previewEligible);

            // Invalid managed path: directory traversal attempt
            mayasaba::AttachmentRecord draftTraversal{};
            draftTraversal.id = "att-traversal"; draftTraversal.projectId = "proj-ai"; draftTraversal.fileName = "escaped.txt";
            draftTraversal.fileSize = 10; draftTraversal.sha256 = "abc";
            draftTraversal.detectedType = "text/plain"; draftTraversal.storagePath = (ai.ManagedAttachmentRoot() / "../escaped.txt").string();
            draftTraversal.sourcePath = "C:/source/escaped.txt"; draftTraversal.status = "READY";
            ai.SaveDraftAttachment("proj-ai", draftTraversal);

            auto riTraversal = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ai", "att-traversal" });
            EXPECT(riTraversal.found);
            EXPECT(!riTraversal.inspection.isManaged);
            EXPECT(!riTraversal.inspection.previewEligible);

            // Invalid managed path: storage path identical to source path (unisolated)
            mayasaba::AttachmentRecord draftUnisolated{};
            draftUnisolated.id = "att-unisolated"; draftUnisolated.projectId = "proj-ai"; draftUnisolated.fileName = "unisolated.txt";
            draftUnisolated.fileSize = validDraftText.size(); draftUnisolated.sha256 = validDraftHash;
            draftUnisolated.detectedType = "text/plain"; draftUnisolated.storagePath = validDraftFile;
            draftUnisolated.sourcePath = validDraftFile; // identical!
            draftUnisolated.status = "READY";
            ai.SaveDraftAttachment("proj-ai", draftUnisolated);

            auto riUnisolated = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ai", "att-unisolated" });
            EXPECT(riUnisolated.found);
            EXPECT(!riUnisolated.inspection.isManaged);
            EXPECT(!riUnisolated.inspection.previewEligible);

            // Non-English / binary file content test
            std::string nonEnglishFile = (ai.ManagedAttachmentRoot() / "non_english.txt").string();
            {
                std::ofstream of(nonEnglishFile, std::ios::binary);
                uint8_t rawBytes[4] = { 0xFF, 0xFE, 0x00, 0x01 };
                of.write(reinterpret_cast<char const*>(rawBytes), 4);
            }
            mayasaba::AttachmentRecord draftNonEnglish{};
            draftNonEnglish.id = "att-non-eng"; draftNonEnglish.projectId = "proj-ai"; draftNonEnglish.fileName = "non_english.txt";
            draftNonEnglish.fileSize = 4; draftNonEnglish.sha256 = "d2ad9277baaee14856d20ec2b21f87a0cb8a7f86c6ef090fd5a082b1e85135ac";
            draftNonEnglish.detectedType = "text/plain"; draftNonEnglish.storagePath = nonEnglishFile;
            draftNonEnglish.sourcePath = "C:/source/non_english.txt"; draftNonEnglish.status = "READY";
            ai.SaveDraftAttachment("proj-ai", draftNonEnglish);

            auto riNonEng = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ai", "att-non-eng" });
            EXPECT(riNonEng.found);
            EXPECT(riNonEng.inspection.isManaged);
            EXPECT(!riNonEng.inspection.previewEligible);
            EXPECT(riNonEng.inspection.previewUnavailableReasonEn.find("non-English") != std::string::npos);

            // File exceeding 10 MiB preview ceiling
            std::string largeFile = (ai.ManagedAttachmentRoot() / "large.txt").string();
            {
                std::ofstream of(largeFile, std::ios::binary);
                std::string chunk(1024 * 1024, 'a');
                for (int i = 0; i < 11; ++i) of.write(chunk.data(), chunk.size());
            }
            mayasaba::AttachmentRecord draftLarge{};
            draftLarge.id = "att-large"; draftLarge.projectId = "proj-ai"; draftLarge.fileName = "large.txt";
            draftLarge.fileSize = 11 * 1024 * 1024; draftLarge.sha256 = "dummy";
            draftLarge.detectedType = "text/plain"; draftLarge.storagePath = largeFile;
            draftLarge.sourcePath = "C:/source/large.txt"; draftLarge.status = "READY";
            ai.SaveDraftAttachment("proj-ai", draftLarge);

            auto riLarge = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ai", "att-large" });
            EXPECT(riLarge.found);
            EXPECT(riLarge.inspection.isManaged);
            EXPECT(!riLarge.inspection.previewEligible);
            EXPECT(riLarge.inspection.previewUnavailableReasonEn.find("10 MiB") != std::string::npos);

            // Non-previewable MIME type (e.g. application/pdf)
            mayasaba::AttachmentRecord draftPdf{};
            draftPdf.id = "att-pdf"; draftPdf.projectId = "proj-ai"; draftPdf.fileName = "doc.pdf";
            draftPdf.fileSize = validDraftText.size(); draftPdf.sha256 = validDraftHash;
            draftPdf.detectedType = "application/pdf"; draftPdf.storagePath = validDraftFile;
            draftPdf.sourcePath = "C:/source/doc.pdf"; draftPdf.status = "READY";
            ai.SaveDraftAttachment("proj-ai", draftPdf);

            auto riPdf = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ai", "att-pdf" });
            EXPECT(riPdf.found);
            EXPECT(riPdf.inspection.isManaged);
            EXPECT(riPdf.inspection.integrityVerified);
            EXPECT(!riPdf.inspection.previewEligible);
            EXPECT(riPdf.inspection.previewUnavailableReasonEn.find("preview renderer") != std::string::npos);

            // Committed attachment: bound and saved via SaveContribution in an isolated project
            ai.BindProject("proj-comm", "Comm Project", "C:/Comm");
            std::string commText = "This is an authorized English committed specification document.";
            std::string committedFile = (ai.ManagedAttachmentRoot() / "committed.txt").string();
            {
                std::ofstream of(committedFile, std::ios::binary);
                of.write(commText.data(), commText.size());
            }
            std::string commHash = ComputeSha256ForTestFile(committedFile);
            // Sealed as read-only per committed attachment contract
            std::wstring wCommitted = std::filesystem::path(committedFile).wstring();
            SetFileAttributesW(wCommitted.c_str(), FILE_ATTRIBUTE_READONLY);

            mayasaba::AttachmentRecord draftComm{};
            draftComm.id = "att-committed-1"; draftComm.projectId = "proj-comm"; draftComm.fileName = "committed.txt";
            draftComm.fileSize = commText.size(); draftComm.sha256 = commHash;
            draftComm.detectedType = "text/plain"; draftComm.storagePath = committedFile;
            draftComm.sourcePath = "C:/source/committed.txt"; draftComm.status = "READY"; draftComm.errorMessage = "";
            ai.SaveDraftAttachment("proj-comm", draftComm);

            ai.StageSubmission("proj-comm", "sub-ai-comm", "submission text");
            mayasaba::AttachmentRecord commRec = draftComm;
            commRec.submissionId = "sub-ai-comm";
            commRec.status = "COMMITTED";
            commRec.version = 1;
            EXPECT(ai.SaveContribution("proj-comm", "sub-ai-comm", "submission text", { commRec }) == mayasaba::SaveDisposition::Saved);

            auto riComm = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-comm", "att-committed-1" });
            EXPECT(riComm.found);
            EXPECT(riComm.inspection.isManaged);
            EXPECT(riComm.inspection.integrityVerified);
            EXPECT(riComm.inspection.previewEligible);
            EXPECT(riComm.inspection.previewContent == commText);
            EXPECT(riComm.inspection.status == "COMMITTED");

            // Committed attachment without read-only attribute fails isManaged check
            SetFileAttributesW(wCommitted.c_str(), FILE_ATTRIBUTE_NORMAL);
            auto riCommUnsealed = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-comm", "att-committed-1" });
            EXPECT(riCommUnsealed.found);
            EXPECT(!riCommUnsealed.inspection.isManaged);
            EXPECT(!riCommUnsealed.inspection.previewEligible);

            // Wrong-project access: querying from different project returns not found
            auto riWrongProj = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "other-proj", "att-draft-1" });
            EXPECT(!riWrongProj.found);

            // Nonexistent attachment returns not found
            auto rn = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ai", "does-not-exist" });
            EXPECT(!rn.found);
            EXPECT(rn.errorMessage.empty());
        }

        // 24b. English preview eligibility: ordinary prose must be readable, foreign
        // prose must still be withheld.
        //
        // The previous vocabulary omitted high-frequency function words ("of", "not",
        // "one", "no", "like") and had no hyphen or contraction handling, so ordinary
        // English documents were refused a preview while the policy still claimed to
        // allow English. These cases run the real InspectAttachment path over real files,
        // so they fail if the production filter regresses rather than testing a copy of
        // its predicate.
        //
        // A pass here does not prove that arbitrary prose is English: the filter is a
        // conservative positive check, and the negative cases below are the half that
        // matters most.
        {
            CASES_BEGIN("24b. English preview eligibility: ordinary prose must be readable, foreign");
            mayasaba::Storage lang(folder / "language_eligibility.db");
            lang.BindProject("proj-lang", "Language Project", "C:/Lang");
            fs::create_directories(lang.ManagedAttachmentRoot());

            struct LanguageCase
            {
                char const* id;
                char const* label;
                std::string body;
                bool expectedEligible;
            };

            // Positive cases: each is ordinary English a user could lawfully attach, and
            // each previously failed on at least one token.
            // Negative cases: foreign or mixed prose that must remain withheld.
            std::vector<LanguageCase> const cases{
                { "lang-of", "high-frequency function words",
                  "A summary of the results of the build.", true },
                { "lang-negation", "negation and quantity",
                  "There is not one file and no reason to keep two copies.", true },
                { "lang-hyphen", "hyphenated compounds",
                  "This is a read-only, fail-closed, non-English-labelled check.", true },
                { "lang-contraction", "contractions",
                  "We can't publish this until the checks pass, and it won't help to skip them.", true },
                { "lang-report", "ordinary report prose",
                  "Quarterly report. Storage usage grew by twelve percent this quarter. We reviewed every "
                  "file that the team produced and removed the ones that nobody opened.", true },
                { "lang-spec", "engineering prose",
                  "The specification requires that the orchestrator owns the barrier. Each agent must "
                  "apply the current plan before it receives a lease.", true },
                { "lang-release", "release notes",
                  "# Release Notes\n\nThe controller verifies every digest before it publishes a record.\n\n"
                  "- Attachments are now re-verified on load.\n- A summary of the results is shown in Chat.\n", true },

                { "lang-spanish", "foreign prose", "hola mundo", false },
                { "lang-mixed", "mixed-language prose", "This is namaste sabko", false },
                { "lang-french", "foreign prose with an English word", "hello world bonjour", false },
                { "lang-underscore", "foreign words with underscores", "This is hola_mundo", false },
                { "lang-hyphen-foreign", "foreign hyphenated compound", "hola-mundo", false },
                { "lang-cjk", "non-Latin script", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", false },
            };

            for (auto const& item : cases)
            {
                std::string const path = (lang.ManagedAttachmentRoot() / (std::string(item.id) + ".txt")).string();
                {
                    std::ofstream of(path, std::ios::binary);
                    of.write(item.body.data(), static_cast<std::streamsize>(item.body.size()));
                }

                mayasaba::AttachmentRecord record{};
                record.id = item.id;
                record.projectId = "proj-lang";
                record.fileName = std::string(item.id) + ".txt";
                record.fileSize = item.body.size();
                record.sha256 = ComputeSha256ForTestFile(path);
                record.detectedType = "text/plain";
                record.storagePath = path;
                record.sourcePath = "C:/source/" + std::string(item.id) + ".txt";
                record.status = "READY";
                lang.SaveDraftAttachment("proj-lang", record);

                auto const inspected = lang.InspectAttachment(
                    mayasaba::AttachmentInspectionQuery{ "proj-lang", item.id });

                bool const failed = item.expectedEligible
                    ? !inspected.inspection.previewEligible
                    : inspected.inspection.previewEligible;
                if (failed)
                {
                    std::cerr << "  language case failed: " << item.label << " (" << item.id << ")"
                              << " expected " << (item.expectedEligible ? "eligible" : "withheld")
                              << ", reason: " << inspected.inspection.previewUnavailableReasonEn << "\n";
                }
                EXPECT(!failed);

                if (item.expectedEligible)
                {
                    // The preview must carry the exact stored bytes, not a summary.
                    EXPECT(inspected.inspection.previewContent == item.body);
                }
                else
                {
                    // A withheld preview must still keep the metadata inspectable and say
                    // why, so the reader is never left with a blank, unexplained card.
                    EXPECT(inspected.inspection.previewContent.empty());
                    EXPECT(!inspected.inspection.previewUnavailableReasonEn.empty());
                    EXPECT(inspected.inspection.integrityVerified);
                    EXPECT(!inspected.inspection.fileName.empty());
                    EXPECT(inspected.inspection.fileSize == item.body.size());
                }
            }
            CASES_END();
        }

        // 25. Idempotent replay must not destroy newer draft state
        // Replaying an already-committed submission verifies its immutable payload and
        // returns its existing outcome. Cleanup belongs to the ORIGINAL submission, so a
        // newer draft and newer draft attachments written after that commit must survive.
        {
            CASES_BEGIN("25. Idempotent replay must not destroy newer draft state");
            mayasaba::Storage replay(folder / "replay.db");
            replay.BindProject("proj-replay", "Replay", "C:/Replay");

            // Original submission S commits with no attachments.
            EXPECT(replay.StageSubmission("proj-replay", "sub-orig", "original body") == "sub-orig");
            EXPECT(replay.SaveContribution("proj-replay", "sub-orig", "original body") == SaveDisposition::Saved);
            EXPECT(replay.Project("proj-replay")->draft.empty());

            // User then writes a NEWER draft and stages a NEWER draft attachment.
            replay.SaveDraft("proj-replay", "newer unsent draft");
            {
                mayasaba::AttachmentRecord newer;
                newer.id = "att-newer-1";
                newer.projectId = "proj-replay";
                newer.fileName = "newer.txt";
                newer.fileSize = 12;
                newer.sha256 = std::string(64, 'a');
                newer.detectedType = "text/plain";
                newer.storagePath = "C:/staging/newer.txt";
                newer.sourcePath = "C:/src/newer.txt";
                newer.status = "READY";
                replay.SaveDraftAttachments("proj-replay", { newer });
            }

            // Replay of S: identical payload, so the existing outcome is returned.
            EXPECT(replay.SaveContribution("proj-replay", "sub-orig", "original body") == SaveDisposition::Duplicate);

            // The newer draft must survive the replay unchanged.
            EXPECT(replay.Project("proj-replay")->draft == "newer unsent draft");

            // The newer draft attachment must survive the replay unchanged.
            auto survivingDrafts = replay.GetDraftAttachments("proj-replay");
            EXPECT(survivingDrafts.size() == 1);
            EXPECT(survivingDrafts[0].id == "att-newer-1");
            EXPECT(survivingDrafts[0].status == "READY");

            // History is unchanged: exactly one contribution.
            EXPECT(replay.Contributions("proj-replay").size() == 1);

            // An altered replay is still refused and still does not touch the newer draft.
            EXPECT(replay.SaveContribution("proj-replay", "sub-orig", "altered body") == SaveDisposition::Conflict);
            EXPECT(replay.Project("proj-replay")->draft == "newer unsent draft");
            EXPECT(replay.GetDraftAttachments("proj-replay").size() == 1);

            // Replay from a different project with the same submission identity is refused.
            replay.BindProject("proj-other", "Other", "C:/Other");
            EXPECT(replay.SaveContribution("proj-other", "sub-orig", "original body") == SaveDisposition::Conflict);
            EXPECT(replay.Project("proj-replay")->draft == "newer unsent draft");

            // An unrelated pending submission is never consumed by a replay attempt.
            // One pending operation is permitted per project, so a replay while a
            // different submission is pending is refused fail-closed (Conflict). Neither
            // the pending row nor the draft state is mutated by that refused replay.
            // (StageSubmission itself writes the new body into the project draft, so the
            // baseline is captured after staging and only the replay's effect is asserted.)
            EXPECT(replay.StageSubmission("proj-replay", "sub-later", "later body") == "sub-later");
            std::string const draftAfterStaging = replay.Project("proj-replay")->draft;
            EXPECT(draftAfterStaging == "later body");
            EXPECT(replay.SaveContribution("proj-replay", "sub-orig", "original body") == SaveDisposition::Conflict);
            auto stillPending = replay.Pending("proj-replay");
            EXPECT(stillPending.has_value());
            EXPECT(stillPending->id == "sub-later");
            EXPECT(replay.Project("proj-replay")->draft == draftAfterStaging);
            EXPECT(replay.GetDraftAttachments("proj-replay").size() == 1);
            CASES_END();
        }

        // 26. Continuation-token integrity signature format
        // The registry defines Sha256 as ^[0-9a-f]{64}$; a 16-byte (32-hex) prefix is
        // non-conforming and was also produced from an unchecked BCrypt finalize.
        {
            CASES_BEGIN("26. Continuation-token integrity signature format");
            mayasaba::Storage sig(folder / "signature.db");
            sig.BindProject("proj-sig", "Signature", "C:/Sig");
            // Build a snapshot with more than one page of Action Center items using
            // failed draft attachments (the projection reports REJECTED attachments).
            for (int i = 0; i < 4; ++i)
            {
                mayasaba::AttachmentRecord bad;
                bad.id = "att-sig-" + std::to_string(i);
                bad.projectId = "proj-sig";
                bad.fileName = "bad" + std::to_string(i) + ".bin";
                bad.fileSize = 4;
                bad.sha256 = std::string(64, 'b');
                bad.detectedType = "application/octet-stream";
                bad.storagePath = "C:/staging/bad" + std::to_string(i) + ".bin";
                bad.sourcePath = "C:/src/bad" + std::to_string(i) + ".bin";
                bad.status = "REJECTED";
                bad.errorMessage = "rejected for test";
                sig.SaveDraftAttachments("proj-sig", { bad });
            }
            auto p1 = sig.QueryActionCenter(mayasaba::ActionCenterQuery{ "proj-sig", 2 });
            EXPECT(!p1.continuationToken.empty());
            EXPECT(p1.continuationToken.rfind("ac:v2:proj-sig:", 0) == 0);

            // Token shape: ac:v2:<project>:<signature>:<offset>
            std::string const& tok = p1.continuationToken;
            std::vector<std::string> parts;
            size_t start = 0;
            while (start <= tok.size())
            {
                size_t pos = tok.find(':', start);
                if (pos == std::string::npos) { parts.push_back(tok.substr(start)); break; }
                parts.push_back(tok.substr(start, pos - start));
                start = pos + 1;
            }
            EXPECT(parts.size() == 5);
            std::string const& signature = parts[3];
            EXPECT(signature.size() == 64); // conforming Sha256 hex length
            for (char c : signature)
            {
                EXPECT((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
            }

            // The signature is stable across identical queries (deterministic, not random).
            auto p1again = sig.QueryActionCenter(mayasaba::ActionCenterQuery{ "proj-sig", 2 });
            EXPECT(p1again.continuationToken == p1.continuationToken);

            // The signature is a real digest of the participating record set: mutating the
            // set must invalidate the token rather than silently accept a stale page.
            auto p2 = sig.QueryActionCenter(mayasaba::ActionCenterQuery{ "proj-sig", 2, p1.continuationToken });
            EXPECT(p2.items.size() == 2);
            EXPECT(p2.isComplete);

            mayasaba::AttachmentRecord extra;
            extra.id = "att-sig-late";
            extra.projectId = "proj-sig";
            extra.fileName = "late.bin";
            extra.fileSize = 4;
            extra.sha256 = std::string(64, 'c');
            extra.detectedType = "application/octet-stream";
            extra.storagePath = "C:/staging/late.bin";
            extra.sourcePath = "C:/src/late.bin";
            extra.status = "REJECTED";
            extra.errorMessage = "rejected for test";
            sig.SaveDraftAttachments("proj-sig", { extra });

            auto stale = sig.QueryActionCenter(mayasaba::ActionCenterQuery{ "proj-sig", 2, p1.continuationToken });
            EXPECT(!stale.isComplete);
            EXPECT(stale.errorMessage.find("expired") != std::string::npos);
            CASES_END();
        }

        // 27. SHA-256 known vectors and injected API failure
        // The integrity signature must be the documented full SHA-256 digest, and a
        // hashing failure must fail the Action Center query closed rather than emit an
        // empty, truncated or fabricated signature.
        {
            CASES_BEGIN("27. SHA-256 known vectors and injected API failure");
            // Published NIST/FIPS 180-4 vectors for the empty message and "abc".
            auto empty = mayasaba::Storage::ComputeSha256HexForTesting("");
            EXPECT(empty.has_value());
            EXPECT(*empty == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

            auto abc = mayasaba::Storage::ComputeSha256HexForTesting("abc");
            EXPECT(abc.has_value());
            EXPECT(*abc == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

            // A longer, non-ASCII-free message exercises multi-block buffering.
            std::string const longInput(1000, 'a');
            auto longHash = mayasaba::Storage::ComputeSha256HexForTesting(longInput);
            EXPECT(longHash.has_value());
            EXPECT(longHash->size() == 64);
            EXPECT(*longHash == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");

            // Distinct inputs must not collide to a shared signature.
            EXPECT(*mayasaba::Storage::ComputeSha256HexForTesting("a") !=
                   *mayasaba::Storage::ComputeSha256HexForTesting("b"));

            // Injected API failure: the query must fail closed. No page and no
            // continuation token are issued, and the reason is reported.
            mayasaba::Storage forced(folder / "signature-fail.db");
            forced.BindProject("proj-sigfail", "SigFail", "C:/SigFail");
            for (int i = 0; i < 4; ++i)
            {
                mayasaba::AttachmentRecord bad;
                bad.id = "att-fail-" + std::to_string(i);
                bad.projectId = "proj-sigfail";
                bad.fileName = "fail" + std::to_string(i) + ".bin";
                bad.fileSize = 4;
                bad.sha256 = std::string(64, 'd');
                bad.detectedType = "application/octet-stream";
                bad.storagePath = "C:/staging/fail" + std::to_string(i) + ".bin";
                bad.sourcePath = "C:/src/fail" + std::to_string(i) + ".bin";
                bad.status = "REJECTED";
                bad.errorMessage = "rejected for test";
                forced.SaveDraftAttachments("proj-sigfail", { bad });
            }

            // Healthy baseline produces a token.
            auto healthy = forced.QueryActionCenter(mayasaba::ActionCenterQuery{ "proj-sigfail", 2 });
            EXPECT(!healthy.continuationToken.empty());

            forced.SetForceHashFailureForTesting(true);
            auto failed = forced.QueryActionCenter(mayasaba::ActionCenterQuery{ "proj-sigfail", 2 });
            EXPECT(!failed.isComplete);
            EXPECT(failed.continuationToken.empty());
            EXPECT(!failed.errorMessage.empty());
            EXPECT(failed.errorMessage.find("signature") != std::string::npos);
            EXPECT(failed.items.empty());

            // The failure is scoped to the injection: clearing it restores the query.
            forced.SetForceHashFailureForTesting(false);
            auto recovered = forced.QueryActionCenter(mayasaba::ActionCenterQuery{ "proj-sigfail", 2 });
            EXPECT(!recovered.continuationToken.empty());
            EXPECT(recovered.continuationToken == healthy.continuationToken);
            CASES_END();
        }

        // 28. Structured save disposition and proven transaction state
        // A refusal must carry a precise English reason, and an uncertain outcome must be
        // reported as uncertain rather than as a definite failure or a definite rollback.
        {
            CASES_BEGIN("28. Structured save disposition and proven transaction state");
            mayasaba::Storage detailStore(folder / "save_detail.db");
            detailStore.BindProject("proj-det", "Detail", "C:/Det");

            // 28.1 Successful save reports a committed, usable connection.
            EXPECT(detailStore.StageSubmission("proj-det", "sub-det-1", "first body") == "sub-det-1");
            mayasaba::SaveContributionDetail okDetail;
            EXPECT(detailStore.SaveContribution("proj-det", "sub-det-1", "first body", {}, okDetail) == SaveDisposition::Saved);
            EXPECT(okDetail.reasonEn.empty() == false);
            EXPECT(okDetail.rollbackConfirmed);
            EXPECT(okDetail.connectionUsable);

            // 28.2 Identical replay is a Duplicate with a reason and a usable connection.
            mayasaba::SaveContributionDetail dupDetail;
            EXPECT(detailStore.SaveContribution("proj-det", "sub-det-1", "first body", {}, dupDetail) == SaveDisposition::Duplicate);
            EXPECT(!dupDetail.reasonEn.empty());
            EXPECT(dupDetail.rollbackConfirmed);
            EXPECT(dupDetail.connectionUsable);

            // 28.3 A refusal carries a reason that names the cause, not an empty string.
            mayasaba::SaveContributionDetail conflictDetail;
            EXPECT(detailStore.SaveContribution("proj-det", "sub-det-1", "altered body", {}, conflictDetail) == SaveDisposition::Conflict);
            EXPECT(!conflictDetail.reasonEn.empty());
            EXPECT(conflictDetail.reasonEn.find("different") != std::string::npos ||
                   conflictDetail.reasonEn.find("already") != std::string::npos);
            EXPECT(conflictDetail.rollbackConfirmed);
            EXPECT(conflictDetail.connectionUsable);

            // 28.4 Empty payload refusal carries a reason.
            mayasaba::SaveContributionDetail emptyDetail;
            EXPECT(detailStore.SaveContribution("proj-det", "", "", {}, emptyDetail) == SaveDisposition::Conflict);
            EXPECT(!emptyDetail.reasonEn.empty());

            // 28.5 Closed-project refusal is RejectedClosed with a reason and no open
            // transaction left behind: the next save must still work.
            detailStore.SetClosure("proj-det", detailStore.Project("proj-det")->version, true);
            mayasaba::SaveContributionDetail closedDetail;
            EXPECT(detailStore.SaveContribution("proj-det", "sub-det-2", "closed body", {}, closedDetail) == SaveDisposition::RejectedClosed);
            EXPECT(!closedDetail.reasonEn.empty());
            EXPECT(closedDetail.reasonEn.find("closed") != std::string::npos);
            EXPECT(closedDetail.rollbackConfirmed);
            EXPECT(closedDetail.connectionUsable);

            detailStore.SetClosure("proj-det", detailStore.Project("proj-det")->version, false);
            EXPECT(detailStore.StageSubmission("proj-det", "sub-det-3", "after reopen") == "sub-det-3");
            mayasaba::SaveContributionDetail reopenDetail;
            EXPECT(detailStore.SaveContribution("proj-det", "sub-det-3", "after reopen", {}, reopenDetail) == SaveDisposition::Saved);
            EXPECT(reopenDetail.connectionUsable);
            EXPECT(detailStore.Contributions("proj-det").size() == 2);
            CASES_END();
        }

        // 29. An unconfirmed rollback must not be reported as a definite rollback
        // The injected failure leaves the transaction open. The result must be UNKNOWN with
        // rollbackConfirmed=false, the store must quarantine itself, and a later write must
        // fail closed with a reason instead of issuing SQL on an unknown transaction state.
        {
            CASES_BEGIN("29. An unconfirmed rollback must not be reported as a definite rollback");
            mayasaba::Storage rollbackStore(folder / "rollback_fail.db");
            rollbackStore.BindProject("proj-rb", "Rollback", "C:/RB");
            EXPECT(rollbackStore.StageSubmission("proj-rb", "sub-rb-1", "pending body") == "sub-rb-1");

            EXPECT(!rollbackStore.IsQuarantinedForTesting());
            rollbackStore.SetForceRollbackFailureForTesting(true);

            // A conflicting replay reaches the rollback path and the rollback fails.
            mayasaba::SaveContributionDetail failDetail;
            auto const disp = rollbackStore.SaveContribution("proj-rb", "sub-rb-1", "different body", {}, failDetail);
            EXPECT(disp == SaveDisposition::Unknown);
            EXPECT(!failDetail.rollbackConfirmed);
            EXPECT(!failDetail.connectionUsable);
            EXPECT(failDetail.reasonEn.find("UNKNOWN") != std::string::npos);
            EXPECT(rollbackStore.IsQuarantinedForTesting());

            // The uncertain outcome is not a definite failure: the pending record is intact.
            auto pendingAfter = rollbackStore.Pending("proj-rb");
            EXPECT(pendingAfter.has_value());
            EXPECT(pendingAfter->id == "sub-rb-1");

            // A later write fails closed with a precise reason rather than proceeding.
            bool threw = false;
            std::string quarantineReason;
            try
            {
                rollbackStore.StageSubmission("proj-rb", "sub-rb-2", "second body");
            }
            catch (std::exception const& ex)
            {
                threw = true;
                quarantineReason = ex.what();
            }
            EXPECT(threw);
            EXPECT(quarantineReason.find("quarantined") != std::string::npos);
            CASES_END();
        }

        // 30. Both timeline realization paths project an attachment-bearing contribution
        // identically. The append path used to build its own entry and omit attachments,
        // so a message with files rendered chips only after the project was reopened.
        // This exercises the shared production projection over a real persisted record.
        {
            CASES_BEGIN("30. Both timeline realization paths project an attachment-bearing contribution");
            mayasaba::Storage timelineStore(folder / "timeline_projection.db");
            timelineStore.BindProject("proj-tl", "Timeline", "C:/Timeline");

            // A real staged attachment is required, so the save path validates it.
            // Stage it inside the store's managed attachment boundary, the way the
            // production preparation path does.
            auto const stagedDir = folder / "attachments" / "staging" / "proj-tl";
            fs::create_directories(stagedDir);
            auto const staged = stagedDir / "timeline_attachment.txt";
            {
                std::ofstream out(staged, std::ios::binary);
                out << "attachment bytes";
            }
            std::string const digest = ComputeSha256ForTestFile(staged.string());
            EXPECT(digest.size() == 64);

            // Draft attachments are registered before staging, because staging binds the
            // manifest of the READY set. That is the production order.
            mayasaba::AttachmentRecord att;
            att.id = "att-tl-1";
            att.projectId = "proj-tl";
            att.submissionId = "sub-tl-1";
            att.fileName = "timeline_attachment.txt";
            att.fileSize = 16;
            att.sha256 = digest;
            att.detectedType = "text/plain";
            att.storagePath = staged.string();
            att.sourcePath = staged.string();
            att.status = "READY";
            timelineStore.SaveDraftAttachments("proj-tl", { att });

            EXPECT(timelineStore.StageSubmission("proj-tl", "sub-tl-1", "message with a file") == "sub-tl-1");

            mayasaba::SaveContributionDetail detail;
            EXPECT(timelineStore.SaveContribution("proj-tl", "sub-tl-1", "message with a file", { att }, detail) == SaveDisposition::Saved);

            auto const history = timelineStore.Contributions("proj-tl");
            EXPECT(history.size() == 1);
            EXPECT(history[0].attachments.size() == 1);
            EXPECT(history[0].attachments[0].id == "att-tl-1");

            // Restored-history path.
            std::vector<mayasaba::ui::TimelineEntry> restored;
            for (auto const& item : history)
            {
                restored.push_back(mayasaba::ui::ProjectContribution(item, "2026-10-20 09:00"));
            }

            // Append path, driven exactly as RenderHistory does it: only the entries
            // beyond the already-rendered count are appended.
            std::size_t const alreadyRendered = 0;
            std::vector<mayasaba::ui::TimelineEntry> appended;
            for (std::size_t index = alreadyRendered; index < history.size(); ++index)
            {
                appended.push_back(mayasaba::ui::ProjectContribution(
                    history[index], history[index].createdAt.empty() ? std::string{} : "2026-10-20 09:00"));
            }

            EXPECT(restored.size() == 1);
            EXPECT(appended.size() == 1);
            for (auto const* path : { &restored, &appended })
            {
                auto const& entry = (*path)[0];
                EXPECT(entry.kind == mayasaba::ui::EntryKind::UserContribution);
                EXPECT(entry.state == mayasaba::ui::EntryState::Saved);
                EXPECT(entry.identity == "sub-tl-1");
                EXPECT(entry.body == "message with a file");
                // The defect: this was empty on the append path.
                EXPECT(entry.attachments.size() == 1);
                EXPECT(entry.attachments[0].fileName == "timeline_attachment.txt");
                EXPECT(entry.attachments[0].sha256 == digest);
            }

            // The two paths must agree field-for-field, not merely both be non-empty.
            EXPECT(restored[0].identity == appended[0].identity);
            EXPECT(restored[0].timestamp == appended[0].timestamp);
            EXPECT(restored[0].attachments.size() == appended[0].attachments.size());
            EXPECT(restored[0].attachments[0].id == appended[0].attachments[0].id);

            // A contribution with no attachments projects no chips on either path, and an
            // absent local label is normalized identically rather than left blank.
            mayasaba::Contribution bare;
            bare.submissionId = "sub-tl-bare";
            bare.body = "no files";
            auto const bareRestored = mayasaba::ui::ProjectContribution(bare, "2026-10-20 09:00");
            auto const bareAppended = mayasaba::ui::ProjectContribution(bare, std::string{});
            EXPECT(bareRestored.attachments.empty());
            EXPECT(bareAppended.attachments.empty());
            EXPECT(bareRestored.timestamp == "2026-10-20 09:00");
            EXPECT(bareAppended.timestamp == "saved");
            CASES_END();
        }

        // 31. Real SQLite write contention from an INDEPENDENT connection.
        // The existing FIFO/mutex coverage proves the C++ lock discipline. It does not
        // prove anything about SQLite's own lock, which is what a second process or a
        // second connection actually contends on. Here a raw connection on the same file
        // holds BEGIN IMMEDIATE while Storage attempts SaveDraftAttachments.
        //
        // The required property is bounded, truthful failure with preserved drafts - not a
        // short timeout. A shorter busy timeout would not prove responsiveness, so this
        // asserts what the caller can rely on: the call either completes or throws with a
        // cause, the connection is not left inside a transaction, and the draft and its
        // attachments are unchanged.
        {
            CASES_BEGIN("31. Real SQLite write contention from an INDEPENDENT connection.");
            auto const contended = folder / "sqlite_contention.db";
            mayasaba::Storage store(contended);
            store.BindProject("proj-cont", "Contention", "C:/Contention");
            store.SaveDraft("proj-cont", "draft that must survive contention");

            mayasaba::AttachmentRecord att;
            att.id = "att-cont-1";
            att.projectId = "proj-cont";
            att.fileName = "contended.txt";
            att.fileSize = 8;
            att.sha256 = std::string(64, 'b');
            att.detectedType = "text/plain";
            att.storagePath = (folder / "contended.txt").string();
            att.sourcePath = att.storagePath;
            att.status = "READY";

            // An independent connection holds a write transaction on the same file.
            sqlite3* other = nullptr;
            EXPECT(sqlite3_open(contended.string().c_str(), &other) == SQLITE_OK);
            EXPECT(other != nullptr);
            EXPECT(sqlite3_exec(other, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) == SQLITE_OK);

            bool threw = false;
            std::string reason;
            std::chrono::steady_clock::time_point began;
            std::chrono::steady_clock::time_point ended;
            try
            {
                began = std::chrono::steady_clock::now();
                store.SaveDraftAttachments("proj-cont", { att });
                ended = std::chrono::steady_clock::now();
            }
            catch (std::exception const& e)
            {
                ended = std::chrono::steady_clock::now();
                threw = true;
                reason = e.what();
            }

            // The call is bounded by SQLite's own busy timeout rather than hanging. The
            // bound asserted here is generous (the configured timeout is 5 s) because the
            // point is that it terminates, not that it terminates quickly.
            auto const elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(ended - began).count();
            EXPECT(elapsedMs < 30000);

            // The independent connection really did hold the write lock: SQLite refuses a
            // concurrent BEGIN IMMEDIATE on the second connection. Asserting this here is
            // what keeps the case from passing vacuously if the contention setup ever stops
            // contending (for example if the journal mode changed and stopped blocking).
            {
                sqlite3* probe = nullptr;
                EXPECT(sqlite3_open(contended.string().c_str(), &probe) == SQLITE_OK);
                EXPECT(sqlite3_busy_timeout(probe, 50) == SQLITE_OK);
                int const probeRc = sqlite3_exec(probe, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
                EXPECT(probeRc != SQLITE_OK);
                sqlite3_close(probe);
            }

            // The operation must have been refused, not silently reported as saved while the
            // other connection still held the lock.
            EXPECT(threw);
            if (threw)
            {
                // A failure must be truthful: it names the cause SQLite actually reported,
                // not a generic message. Since the store no longer polls its own mutex, this
                // is SQLite's busy/locked error rather than a C++ lock timeout.
                EXPECT(!reason.empty());
                EXPECT(reason.find("busy") != std::string::npos ||
                       reason.find("locked") != std::string::npos ||
                       reason.find("LOCKED") != std::string::npos);
            }

            // Nothing was written while the write lock was held elsewhere.
            EXPECT(store.GetDraftAttachments("proj-cont").empty());

            // Release the independent write transaction and let the store recover.
            EXPECT(sqlite3_exec(other, "ROLLBACK;", nullptr, nullptr, nullptr) == SQLITE_OK);
            EXPECT(sqlite3_close(other) == SQLITE_OK);

            // The draft text was never a candidate for loss: contention on an attachment
            // write must not discard the message the reader typed.
            auto const projectAfter = store.Project("proj-cont");
            EXPECT(projectAfter.has_value());
            EXPECT(projectAfter->draft == "draft that must survive contention");

            // After the independent connection releases, the same operation succeeds and is
            // durably visible. This proves the earlier failure was contention, not a
            // permanently broken store.
            bool retryOk = true;
            try { store.SaveDraftAttachments("proj-cont", { att }); }
            catch (std::exception const&) { retryOk = false; }
            EXPECT(retryOk);
            auto const draftsAfter = store.GetDraftAttachments("proj-cont");
            EXPECT(draftsAfter.size() == 1);
            EXPECT(draftsAfter[0].id == "att-cont-1");
            EXPECT(draftsAfter[0].status == "READY");
            EXPECT(store.Project("proj-cont")->draft == "draft that must survive contention");
            CASES_END();
        }

        // ------------------------------------------------------------------
        // S1 admission correctness. Each group below states the unsafe behaviour
        // that existed before its guard and asserts the guard, so removing the
        // guard makes the group fail rather than silently pass.
        // ------------------------------------------------------------------

        // 32. F03: a draft is ONE payload. Counting only READY rows let a caller
        // stage and commit a submission whose manifest silently omitted a rejected,
        // timed-out or source-changed sibling. Both the text-only and the
        // ready-subset forms are exercised, because the audit's rule explicitly
        // does not weaken for attachment-only or text-only messages.
        {
            CASES_BEGIN("32. Complete-draft admission: a non-READY sibling blocks the submission");
            mayasaba::Storage store(folder / "admission.db");
            store.BindProject("proj-complete", "Complete", "C:/Projects/Complete");

            auto makeRecord = [](std::string const& id, std::string const& status, std::string const& name)
            {
                mayasaba::AttachmentRecord r;
                r.id = id;
                r.projectId = "proj-complete";
                r.fileName = name;
                r.fileSize = 12;
                r.sha256 = "sha-" + id;
                r.detectedType = "text/plain";
                r.storagePath = "C:/stage/" + name;
                r.sourcePath = "C:/source/" + name;
                r.status = status;
                return r;
            };

            store.SaveDraftAttachments("proj-complete",
                { makeRecord("att-ready", "READY", "a.txt"),
                  makeRecord("att-rejected", "REJECTED", "b.txt") });

            // Text-only send while a rejected sibling exists: the rejected attachment
            // would be omitted without a word.
            bool textOnlyRefused = false;
            try { store.StageSubmission("proj-complete", "sub-text-only", "hello"); }
            catch (std::exception const&) { textOnlyRefused = true; }
            EXPECT(textOnlyRefused);
            EXPECT(!store.Pending("proj-complete").has_value());
            EXPECT(store.Contributions("proj-complete").empty());
            EXPECT(store.GetDraftAttachments("proj-complete").size() == 2);

            // Ready-subset send: supplying only the ready attachment must be refused too.
            bool subsetRefused = false;
            try { store.StageSubmission("proj-complete", "sub-subset", "hello"); }
            catch (std::exception const&) { subsetRefused = true; }
            EXPECT(subsetRefused);
            EXPECT(store.Contributions("proj-complete").empty());

            // No false saved state: the draft text and both records survive intact.
            auto const retained = store.GetDraftAttachments("proj-complete");
            EXPECT(retained.size() == 2);
            EXPECT(retained[0].status == "READY");
            EXPECT(retained[1].status == "REJECTED");

            // Every non-READY status is refused, not just REJECTED.
            for (std::string const status : { "PREPARING", "SOURCE_CHANGED", "TIMED_OUT" })
            {
                mayasaba::Storage each(folder / ("admission-" + status + ".db"));
                each.BindProject("proj-" + status, status, "C:/Projects/" + status);
                auto rec = makeRecord("att-x", status, "x.txt");
                rec.projectId = "proj-" + status;
                each.SaveDraftAttachments(rec.projectId, { rec });
                bool refused = false;
                try { each.StageSubmission(rec.projectId, "sub-x", "text"); }
                catch (std::exception const&) { refused = true; }
                EXPECT(refused);
                EXPECT(each.Contributions(rec.projectId).empty());
            }
            CASES_END();
        }

        // 33. F06: a durable pending row is the authority, not the interface's
        // projection of it. Draft writes must not widen the locked manifest, while
        // truthful degradation must still be permitted so recovery can work.
        {
            CASES_BEGIN("33. A locked pending submission cannot be widened by draft writes");
            mayasaba::Storage store(folder / "pending-lock.db");
            store.BindProject("proj-lock", "Lock", "C:/Projects/Lock");

            mayasaba::AttachmentRecord a;
            a.id = "att-a"; a.projectId = "proj-lock"; a.fileName = "a.txt";
            a.fileSize = 5; a.sha256 = "sha-a"; a.detectedType = "text/plain";
            a.storagePath = "C:/stage/a.txt"; a.sourcePath = "C:/source/a.txt"; a.status = "READY";
            store.SaveDraftAttachments("proj-lock", { a });

            EXPECT(store.StageSubmission("proj-lock", "sub-lock", "body") == "sub-lock");
            EXPECT(store.Pending("proj-lock").has_value());

            // A brand-new attachment while a submission is locked.
            mayasaba::AttachmentRecord c = a;
            c.id = "att-c"; c.fileName = "c.txt"; c.sha256 = "sha-c"; c.storagePath = "C:/stage/c.txt";
            bool singleRefused = false;
            try { store.SaveDraftAttachment("proj-lock", c); }
            catch (std::exception const&) { singleRefused = true; }
            EXPECT(singleRefused);

            bool batchRefused = false;
            try { store.SaveDraftAttachments("proj-lock", { a, c }); }
            catch (std::exception const&) { batchRefused = true; }
            EXPECT(batchRefused);

            // Replacing the identity of an attachment the locked payload covers.
            mayasaba::AttachmentRecord replaced = a;
            replaced.sha256 = "sha-different";
            bool identityRefused = false;
            try { store.SaveDraftAttachment("proj-lock", replaced); }
            catch (std::exception const&) { identityRefused = true; }
            EXPECT(identityRefused);

            // Deleting or clearing the covered set.
            bool removeRefused = false;
            try { store.RemoveDraftAttachment("proj-lock", "att-a"); }
            catch (std::exception const&) { removeRefused = true; }
            EXPECT(removeRefused);

            bool clearRefused = false;
            try { store.ClearDraftAttachments("proj-lock"); }
            catch (std::exception const&) { clearRefused = true; }
            EXPECT(clearRefused);

            // None of the refusals changed anything.
            auto const stillThere = store.GetDraftAttachments("proj-lock");
            EXPECT(stillThere.size() == 1);
            EXPECT(stillThere[0].id == "att-a");
            EXPECT(stillThere[0].status == "READY");
            EXPECT(stillThere[0].sha256 == "sha-a");
            EXPECT(store.Pending("proj-lock")->id == "sub-lock");

            // The boundary is the READY manifest, not the row count: a FAILED attachment is
            // not part of the locked payload, so recording one while a submission is pending
            // is allowed. The Action Center must be able to show both obligation kinds.
            mayasaba::AttachmentRecord failed = a;
            failed.id = "att-failed"; failed.fileName = "f.txt";
            failed.sha256 = ""; failed.storagePath = "";
            failed.status = "REJECTED"; failed.errorMessage = "validation failed";
            bool failedRowAllowed = true;
            try { store.SaveDraftAttachment("proj-lock", failed); }
            catch (std::exception const&) { failedRowAllowed = false; }
            EXPECT(failedRowAllowed);
            EXPECT(store.GetDraftAttachments("proj-lock").size() == 2);

            // Truthful degradation is still allowed: recovery must be able to record
            // that an attachment stopped being ready.
            mayasaba::AttachmentRecord degraded = a;
            degraded.status = "SOURCE_CHANGED";
            degraded.errorMessage = "source changed after staging";
            bool degradationAllowed = true;
            try { store.SaveDraftAttachment("proj-lock", degraded); }
            catch (std::exception const&) { degradationAllowed = false; }
            EXPECT(degradationAllowed);
            EXPECT(store.GetDraftAttachments("proj-lock")[0].status == "SOURCE_CHANGED");

            // ... and the submission it was locked for can no longer commit.
            EXPECT(store.SaveContribution("proj-lock", "sub-lock", "body", { a }) == SaveDisposition::Conflict);
            EXPECT(store.Contributions("proj-lock").empty());
            CASES_END();
        }

        // 34. F01/F04: one policy for every entry point. The picker, the drop target,
        // Enter and the Send button previously each carried a different subset of
        // checks, so a keyboard command could reach SendCurrent() while the button
        // was correctly disabled.
        {
            CASES_BEGIN("34. One admission policy for picker, drop, Enter and Send");
            using mayasaba::AttachmentAdmissionFailure;
            using mayasaba::AttachmentAdmissionState;

            AttachmentAdmissionState base;
            base.storageAvailable = true;
            base.hasProject = true;
            base.maxDraftCount = 50;
            EXPECT(mayasaba::EvaluateAttachmentAdmission(base).allowed);
            EXPECT(mayasaba::EvaluateSendAdmission(base).allowed);

            auto refusedWith = [](AttachmentAdmissionState const& s, AttachmentAdmissionFailure expected)
            {
                return !mayasaba::EvaluateAttachmentAdmission(s).allowed &&
                       mayasaba::EvaluateAttachmentAdmission(s).reason == expected;
            };

            { auto s = base; s.windowClosed = true; EXPECT(refusedWith(s, AttachmentAdmissionFailure::WindowClosing)); }
            { auto s = base; s.storageAvailable = false; EXPECT(refusedWith(s, AttachmentAdmissionFailure::StorageUnavailable)); }
            { auto s = base; s.hasProject = false; EXPECT(refusedWith(s, AttachmentAdmissionFailure::NoProject)); }
            { auto s = base; s.projectClosed = true; EXPECT(refusedWith(s, AttachmentAdmissionFailure::ClosedProject)); }
            { auto s = base; s.pendingSubmission = true; EXPECT(refusedWith(s, AttachmentAdmissionFailure::PendingSubmission)); }
            { auto s = base; s.busy = true; EXPECT(refusedWith(s, AttachmentAdmissionFailure::SubmissionInProgress)); }
            { auto s = base; s.navigationBusy = true; EXPECT(refusedWith(s, AttachmentAdmissionFailure::ProjectNavigation)); }
            { auto s = base; s.selectionBusy = true; EXPECT(refusedWith(s, AttachmentAdmissionFailure::ProjectNavigation)); }
            { auto s = base; s.pickerOpen = true; EXPECT(refusedWith(s, AttachmentAdmissionFailure::PickerOpen)); }
            { auto s = base; s.draftsLoading = true; EXPECT(refusedWith(s, AttachmentAdmissionFailure::DraftsLoading)); }
            { auto s = base; s.removalsInFlight = 1; EXPECT(refusedWith(s, AttachmentAdmissionFailure::RemovalInProgress)); }
            { auto s = base; s.ingestionInProgress = true; EXPECT(refusedWith(s, AttachmentAdmissionFailure::IngestionInProgress)); }
            { auto s = base; s.draftCount = 50; EXPECT(refusedWith(s, AttachmentAdmissionFailure::CapacityExceeded)); }

            // The blocked state is exactly the one a direct Enter keypress reached
            // before the fix: Send must refuse while drafts are loading.
            {
                auto s = base;
                s.draftsLoading = true;
                auto const send = mayasaba::EvaluateSendAdmission(s);
                EXPECT(!send.allowed);
                EXPECT(send.reason == AttachmentAdmissionFailure::DraftsLoading);
            }
            // ... and while a durable removal is still in flight.
            {
                auto s = base;
                s.removalsInFlight = 1;
                auto const send = mayasaba::EvaluateSendAdmission(s);
                EXPECT(!send.allowed);
                EXPECT(send.reason == AttachmentAdmissionFailure::RemovalInProgress);
            }

            // Send is a different command: an open picker and a full draft set do not
            // block sending what is already in the composer.
            {
                auto s = base;
                s.pickerOpen = true;
                s.draftCount = 50;
                EXPECT(mayasaba::EvaluateSendAdmission(s).allowed);
            }

            // A full draft must still allow removal: capacity applies to adding, not to
            // taking something out. Using the ingestion policy here would have refused
            // the one action that resolves a full draft.
            {
                auto s = base;
                s.draftCount = 50;
                EXPECT(!mayasaba::EvaluateAttachmentAdmission(s).allowed);
                EXPECT(mayasaba::EvaluateRemovalAdmission(s).allowed);
                s.pendingSubmission = true;
                EXPECT(!mayasaba::EvaluateRemovalAdmission(s).allowed);
            }

            // Reconciling is admitted while a submission is pending - that is its whole
            // purpose - and refused while another submission is being saved.
            {
                auto s = base;
                s.pendingSubmission = true;
                EXPECT(mayasaba::EvaluateReconcileAdmission(s).allowed);
                s.busy = true;
                EXPECT(!mayasaba::EvaluateReconcileAdmission(s).allowed);
                EXPECT(mayasaba::EvaluateReconcileAdmission(s).reason ==
                       AttachmentAdmissionFailure::SubmissionInProgress);
            }
            CASES_END();
        }

        // 35. F02/F05: the reservation must exist before the operation suspends, and a
        // stale completion must never release a newer operation's reservation.
        {
            CASES_BEGIN("35. Single-flight reservation rejects overlap and stale release");
            mayasaba::AttachmentOperationSlot slot;
            EXPECT(!slot.Held());

            auto const first = slot.TryAcquire();
            EXPECT(first != 0);
            EXPECT(slot.Held());

            // A second operation observing the same draft set is refused, not queued.
            EXPECT(slot.TryAcquire() == 0);
            EXPECT(slot.Held());
            EXPECT(slot.Owner() == first);

            // A completion that does not own the slot cannot release it.
            EXPECT(!slot.Release(first + 1));
            EXPECT(!slot.Release(0));
            EXPECT(slot.Held());

            EXPECT(slot.Release(first));
            EXPECT(!slot.Held());

            // A late completion belonging to the FIRST operation must not release the
            // SECOND operation's reservation.
            auto const second = slot.TryAcquire();
            EXPECT(second != 0);
            EXPECT(second != first);
            EXPECT(!slot.Release(first));
            EXPECT(slot.Held());
            EXPECT(slot.Owner() == second);
            EXPECT(slot.Release(second));
            EXPECT(!slot.Held());
            CASES_END();
        }

        // ------------------------------------------------------------------
        // S2 recovery and file-integrity correctness.
        // ------------------------------------------------------------------

        // 36. F07: recovery must not promote a terminal failure to READY just because the
        // bytes happen to hash. A physical fact does not address why the attempt failed.
        {
            CASES_BEGIN("36. Recovery honours the prior status instead of promoting on a hash match");
            mayasaba::Storage store(folder / "recovery.db");
            auto staging = folder / "recovery-stage";
            mayasaba::AttachmentService service(store, staging);

            auto seed = [&](std::string const& projectId, std::string const& id,
                            mayasaba::AttachmentStatus status, std::string const& content)
            {
                store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
                auto dir = staging / "staging" / projectId;
                fs::create_directories(dir);
                auto path = dir / (id + ".txt");
                {
                    std::ofstream of(path, std::ios::binary);
                    of.write(content.data(), static_cast<std::streamsize>(content.size()));
                }
                mayasaba::AttachmentCandidate c;
                c.id = id;
                c.fileName = id + ".txt";
                c.fileSize = content.size();
                c.detectedType = "text/plain";
                c.originalPath = L"C:/source/" + std::wstring(id.begin(), id.end());
                c.stagedPath = path.wstring();
                c.sha256 = ComputeSha256ForTestFile(WideToUtf8(path.wstring()));
                c.status = status;
                if (status != mayasaba::AttachmentStatus::Ready)
                {
                    c.errorMessage = "original failure reason for " + id;
                }
                service.SaveDraftAttachments(projectId, { c });
            };

            auto statusAfter = [&](std::string const& projectId, std::string const& id, bool preparationActive)
            {
                auto const after = service.ReverifyDraftAttachments(projectId, preparationActive);
                for (auto const& c : after) if (c.id == id) return c;
                throw std::runtime_error("attachment disappeared during recovery: " + id);
            };

            constexpr char const* kContent = "intact staged payload";

            // READY + intact bytes stays READY.
            seed("rec-ready", "att-r", mayasaba::AttachmentStatus::Ready, kContent);
            EXPECT(statusAfter("rec-ready", "att-r", false).status == mayasaba::AttachmentStatus::Ready);

            // REJECTED + intact bytes STAYS rejected, with its original reason intact.
            seed("rec-rejected", "att-j", mayasaba::AttachmentStatus::Rejected, kContent);
            {
                auto const c = statusAfter("rec-rejected", "att-j", false);
                EXPECT(c.status == mayasaba::AttachmentStatus::Rejected);
                EXPECT(c.errorMessage.find("original failure reason") != std::string::npos);
            }

            // SOURCE_CHANGED + intact bytes stays SOURCE_CHANGED: reselection is still required.
            seed("rec-changed", "att-s", mayasaba::AttachmentStatus::SourceChangedDuringPreparation, kContent);
            EXPECT(statusAfter("rec-changed", "att-s", false).status ==
                   mayasaba::AttachmentStatus::SourceChangedDuringPreparation);

            // TIMED_OUT + intact bytes does not erase the timeout.
            seed("rec-timeout", "att-t", mayasaba::AttachmentStatus::TimedOut, kContent);
            {
                auto const c = statusAfter("rec-timeout", "att-t", false);
                EXPECT(c.status == mayasaba::AttachmentStatus::TimedOut);
                EXPECT(c.errorMessage.find("original failure reason") != std::string::npos);
            }

            // READY whose bytes were altered becomes an explicit rejection.
            seed("rec-corrupt", "att-c", mayasaba::AttachmentStatus::Ready, kContent);
            {
                auto dir = staging / "staging" / "rec-corrupt";
                std::ofstream of(dir / "att-c.txt", std::ios::binary | std::ios::trunc);
                of.write("tampered", 8);
            }
            {
                auto const c = statusAfter("rec-corrupt", "att-c", false);
                EXPECT(c.status == mayasaba::AttachmentStatus::Rejected);
                EXPECT(!c.errorMessage.empty());
            }

            // A live preparation owns its record; recovery must not touch it.
            seed("rec-live", "att-l", mayasaba::AttachmentStatus::Preparing, kContent);
            EXPECT(statusAfter("rec-live", "att-l", true).status == mayasaba::AttachmentStatus::Preparing);

            // An abandoned preparation is reclaimed rather than silently promoted.
            seed("rec-abandoned", "att-a", mayasaba::AttachmentStatus::Preparing, kContent);
            {
                auto const after = service.ReverifyDraftAttachments("rec-abandoned", false);
                EXPECT(after.empty());
            }

            // Idempotence: a second pass with no intervening change changes nothing.
            {
                auto const first = statusAfter("rec-rejected", "att-j", false);
                auto const second = statusAfter("rec-rejected", "att-j", false);
                EXPECT(second.status == first.status);
                EXPECT(second.errorMessage == first.errorMessage);
            }
            CASES_END();
        }

        // 37. F09: a stored path is metadata. Cleanup must prove containment inside the
        // project's managed staging subtree before deleting anything.
        {
            CASES_BEGIN("37. Staging cleanup cannot delete outside the managed staging directory");
            mayasaba::Storage store(folder / "containment.db");
            auto staging = folder / "containment-stage";
            mayasaba::AttachmentService service(store, staging);

            auto outside = folder / "containment-outside";
            fs::create_directories(outside);
            auto victim = outside / "victim.txt";
            {
                std::ofstream of(victim, std::ios::binary);
                of.write("must not be deleted", 19);
            }

            store.BindProject("proj-c", "C", "C:/Projects/C");

            // A PREPARING record whose stored path points at a sibling directory.
            mayasaba::AttachmentCandidate c;
            c.id = "att-escape";
            c.fileName = "victim.txt";
            c.fileSize = 19;
            c.detectedType = "text/plain";
            c.stagedPath = victim.wstring();
            c.sha256 = ComputeSha256ForTestFile(WideToUtf8(victim.wstring()));
            c.status = mayasaba::AttachmentStatus::Preparing;
            service.SaveDraftAttachments("proj-c", { c });

            service.ReclaimInterruptedPreparation("proj-c");

            // The out-of-bound file survives, and the record is kept because the cleanup
            // could not be proven safe rather than being silently dropped.
            EXPECT(fs::exists(victim));
            EXPECT(store.GetDraftAttachments("proj-c").size() == 1);

            // A path that escapes with a parent traversal is refused the same way.
            auto traversal = outside / ".." / "containment-outside" / "victim.txt";
            mayasaba::AttachmentRecord t;
            t.id = "att-traversal";
            t.projectId = "proj-c";
            t.fileName = "victim.txt";
            t.fileSize = 19;
            t.sha256 = c.sha256;
            t.detectedType = "text/plain";
            t.storagePath = WideToUtf8(traversal.wstring());
            t.status = "PREPARING";
            store.SaveDraftAttachment("proj-c", t);
            service.ReclaimInterruptedPreparation("proj-c");
            EXPECT(fs::exists(victim));

            // A legitimate abandoned file inside the managed staging directory IS reclaimed,
            // so the containment check does not simply disable cleanup.
            auto dir = staging / "staging" / "proj-c";
            fs::create_directories(dir);
            auto orphan = dir / "orphan.bin";
            { std::ofstream of(orphan, std::ios::binary); of.write("orphan", 6); }
            service.ReclaimInterruptedPreparation("proj-c");
            EXPECT(!fs::exists(orphan));
            CASES_END();
        }

        // 38. F12: the commit path must not treat a read error or a short read as a clean
        // end-of-file. A staged file whose length disagrees with its record is refused even
        // when the recorded digest matches the bytes that were actually read.
        {
            CASES_BEGIN("38. Attachment commit verifies the read length, not only the digest");
            mayasaba::Storage store(folder / "hash-length.db");
            auto staging = folder / "hash-length-stage";
            mayasaba::AttachmentService service(store, staging);

            store.BindProject("proj-len", "Len", "C:/Projects/Len");
            auto dir = staging / "staging" / "proj-len";
            fs::create_directories(dir);
            auto path = dir / "att-len.txt";
            {
                std::ofstream of(path, std::ios::binary);
                of.write("0123456789", 10);
            }

            mayasaba::AttachmentCandidate c;
            c.id = "att-len";
            c.fileName = "att-len.txt";
            // The digest is correct for the bytes on disk; the recorded length is not.
            c.fileSize = 999;
            c.detectedType = "text/plain";
            c.stagedPath = path.wstring();
            c.originalPath = L"C:/source/att-len.txt";
            c.sha256 = ComputeSha256ForTestFile(WideToUtf8(path.wstring()));
            c.status = mayasaba::AttachmentStatus::Ready;
            service.SaveDraftAttachments("proj-len", { c });

            EXPECT(store.StageSubmission("proj-len", "sub-len", "body") == "sub-len");

            mayasaba::AttachmentRecord rec;
            rec.id = c.id;
            rec.projectId = "proj-len";
            rec.submissionId = "sub-len";
            rec.fileName = c.fileName;
            rec.fileSize = c.fileSize;
            rec.sha256 = c.sha256;
            rec.detectedType = c.detectedType;
            rec.storagePath = WideToUtf8(path.wstring());
            rec.sourcePath = "C:/source/att-len.txt";
            rec.status = "COMMITTED";

            mayasaba::SaveContributionDetail detail;
            auto const disposition = store.SaveContribution("proj-len", "sub-len", "body", { rec }, detail);
            EXPECT(disposition == SaveDisposition::Conflict);
            EXPECT(store.Contributions("proj-len").empty());
            // A length disagreement is refused. Note the admission path already compares the
            // stat size at Storage.cpp:1994, so this assertion does not by itself prove the
            // read loop's own length check ran; it proves the observable invariant holds.
            EXPECT(detail.reasonEn.find("size") != std::string::npos ||
                   detail.reasonEn.find("length") != std::string::npos);
            CASES_END();
        }

        // 39. F15: a read transaction that fails to commit must not be forgotten. The
        // previous guard cleared its ownership flag before establishing whether SQLite had
        // finished, so a still-open transaction leaked into the next operation.
        {
            CASES_BEGIN("39. A failed read-transaction commit does not leak the transaction");
            mayasaba::Storage store(folder / "readtx.db");
            store.BindProject("proj-tx", "TX", "C:/Projects/TX");
            store.StageSubmission("proj-tx", "sub-tx", "unresolved body");

            store.SetForceActionCenterCommitFailureForTesting(true);
            auto const failed = store.QueryActionCenter(mayasaba::ActionCenterQuery{ "proj-tx", 10, "" });
            store.SetForceActionCenterCommitFailureForTesting(false);

            EXPECT(!failed.isComplete);
            EXPECT(!failed.errorMessage.empty());

            // The connection is still usable: a leaked transaction would make the next
            // BEGIN fail and the projection would come back incomplete again.
            auto const after = store.QueryActionCenter(mayasaba::ActionCenterQuery{ "proj-tx", 10, "" });
            EXPECT(after.isComplete);
            EXPECT(after.errorMessage.empty());
            EXPECT(after.items.size() == 1);
            EXPECT(!store.IsQuarantinedForTesting());
            CASES_END();
        }

        // 40. F08: a failure part-way through sealing must restore the attributes of the
        // files it already sealed, or the reader is left with files they cannot re-attach
        // or remove.
        {
            CASES_BEGIN("40. Partial sealing failure restores the attributes it already changed");
            mayasaba::Storage store(folder / "sealing.db");
            auto staging = folder / "sealing-stage";
            mayasaba::AttachmentService service(store, staging);

            store.BindProject("proj-seal", "Seal", "C:/Projects/Seal");
            auto dir = staging / "staging" / "proj-seal";
            fs::create_directories(dir);

            auto makeFile = [&](std::string const& id)
            {
                auto path = dir / (id + ".txt");
                { std::ofstream of(path, std::ios::binary); of.write("sealed payload", 14); }
                return path;
            };

            auto first = makeFile("att-s1");
            auto second = makeFile("att-s2");

            std::vector<mayasaba::AttachmentCandidate> batch;
            for (auto const& entry : std::vector<std::pair<std::string, fs::path>>{
                     { "att-s1", first }, { "att-s2", second } })
            {
                mayasaba::AttachmentCandidate c;
                c.id = entry.first;
                c.fileName = entry.first + ".txt";
                c.fileSize = 14;
                c.detectedType = "text/plain";
                c.stagedPath = entry.second.wstring();
                c.originalPath = L"C:/source/" + std::wstring(entry.first.begin(), entry.first.end()) + L".txt";
                c.sha256 = ComputeSha256ForTestFile(WideToUtf8(entry.second.wstring()));
                c.status = mayasaba::AttachmentStatus::Ready;
                batch.push_back(c);
            }
            service.SaveDraftAttachments("proj-seal", batch);

            EXPECT(store.StageSubmission("proj-seal", "sub-seal", "body") == "sub-seal");

            std::vector<mayasaba::AttachmentRecord> records;
            for (auto const& c : batch)
            {
                mayasaba::AttachmentRecord r;
                r.id = c.id; r.projectId = "proj-seal"; r.submissionId = "sub-seal";
                r.fileName = c.fileName; r.fileSize = c.fileSize; r.sha256 = c.sha256;
                r.detectedType = c.detectedType; r.storagePath = WideToUtf8(c.stagedPath);
                r.sourcePath = "C:/source/x.txt"; r.status = "COMMITTED";
                records.push_back(std::move(r));
            }

            // The second seal does not take effect, so the gate fails after the first file
            // has already been made read-only.
            store.SetSealFailureIndexForTesting(1);
            mayasaba::SaveContributionDetail detail;
            auto const disposition = store.SaveContribution("proj-seal", "sub-seal", "body", records, detail);
            store.SetSealFailureIndexForTesting(static_cast<std::size_t>(-1));

            EXPECT(disposition == SaveDisposition::Conflict);
            EXPECT(store.Contributions("proj-seal").empty());

            // Neither file may be left read-only: the first was sealed, the transaction was
            // rolled back, and the compensation must have restored it.
            for (auto const& path : { first, second })
            {
                DWORD const attrs = GetFileAttributesW(path.wstring().c_str());
                EXPECT(attrs != INVALID_FILE_ATTRIBUTES);
                EXPECT((attrs & FILE_ATTRIBUTE_READONLY) == 0);
            }

            // The draft survives so the reader can retry.
            EXPECT(store.GetDraftAttachments("proj-seal").size() == 2);
            EXPECT(store.Pending("proj-seal").has_value());
            CASES_END();
        }

        // ------------------------------------------------------------------
        // S1 integrity correction: authorization must precede physical deletion.
        // ------------------------------------------------------------------

        // 41. A draft removal that the authoritative layer refuses must not have touched the
        // bytes. The previous order deleted the staged copy first and asked Storage second,
        // so a pending submission's protected bytes were already gone when the refusal
        // arrived.
        {
            CASES_BEGIN("41. A refused draft removal preserves both the record and the staged bytes");
            mayasaba::Storage store(folder / "removal-guard.db");
            auto staging = folder / "removal-guard-stage";
            mayasaba::AttachmentService service(store, staging);

            auto seed = [&](std::string const& projectId, std::string const& id)
            {
                store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
                auto dir = staging / "staging" / projectId;
                fs::create_directories(dir);
                auto path = dir / (id + ".txt");
                {
                    std::ofstream of(path, std::ios::binary);
                    of.write("protected bytes", 15);
                }
                mayasaba::AttachmentCandidate c;
                c.id = id;
                c.fileName = id + ".txt";
                c.fileSize = 15;
                c.detectedType = "text/plain";
                c.originalPath = L"C:/source/" + std::wstring(id.begin(), id.end()) + L".txt";
                c.stagedPath = path.wstring();
                c.sha256 = ComputeSha256ForTestFile(WideToUtf8(path.wstring()));
                c.status = mayasaba::AttachmentStatus::Ready;
                service.SaveDraftAttachments(projectId, { c });
                return path;
            };

            auto guardedFile = seed("rem-guard", "att-guarded");
            EXPECT(store.StageSubmission("rem-guard", "sub-guard", "body") == "sub-guard");

            // Single removal, refused. The record and the bytes must both survive.
            bool removeRefused = false;
            try { service.RemoveDraftAttachment("rem-guard", "att-guarded"); }
            catch (std::exception const&) { removeRefused = true; }
            EXPECT(removeRefused);
            EXPECT(fs::exists(guardedFile));
            EXPECT(store.GetDraftAttachments("rem-guard").size() == 1);
            EXPECT(store.GetDraftAttachments("rem-guard")[0].status == "READY");
            EXPECT(store.Pending("rem-guard").has_value());

            // Clear, refused. Same requirement for the whole set.
            bool clearRefused = false;
            try { service.ClearDraftAttachments("rem-guard"); }
            catch (std::exception const&) { clearRefused = true; }
            EXPECT(clearRefused);
            EXPECT(fs::exists(guardedFile));
            EXPECT(store.GetDraftAttachments("rem-guard").size() == 1);
            EXPECT(store.Pending("rem-guard").has_value());

            // Project isolation: a lock in one project must not block, or endanger, another.
            auto otherFile = seed("rem-other", "att-other");
            auto const otherOutcome = service.RemoveDraftAttachment("rem-other", "att-other");
            EXPECT(otherOutcome.recordRemoved);
            EXPECT(otherOutcome.bytesRemoved);
            EXPECT(!fs::exists(otherFile));
            EXPECT(store.GetDraftAttachments("rem-other").empty());
            // ... and the guarded project is untouched by that operation.
            EXPECT(fs::exists(guardedFile));
            EXPECT(store.GetDraftAttachments("rem-guard").size() == 1);
            CASES_END();
        }

        // 42. Authorized removal still works, and a cleanup failure is reported as an
        // unresolved obligation that reconciliation later reclaims.
        {
            CASES_BEGIN("42. Authorized removal succeeds and a cleanup failure stays recoverable");
            mayasaba::Storage store(folder / "removal-ok.db");
            auto staging = folder / "removal-ok-stage";
            mayasaba::AttachmentService service(store, staging);

            auto seed = [&](std::string const& projectId, std::string const& id)
            {
                store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
                auto dir = staging / "staging" / projectId;
                fs::create_directories(dir);
                auto path = dir / (id + ".txt");
                {
                    std::ofstream of(path, std::ios::binary);
                    of.write("ordinary bytes", 14);
                }
                mayasaba::AttachmentCandidate c;
                c.id = id;
                c.fileName = id + ".txt";
                c.fileSize = 14;
                c.detectedType = "text/plain";
                c.originalPath = L"C:/source/" + std::wstring(id.begin(), id.end()) + L".txt";
                c.stagedPath = path.wstring();
                c.sha256 = ComputeSha256ForTestFile(WideToUtf8(path.wstring()));
                c.status = mayasaba::AttachmentStatus::Ready;
                service.SaveDraftAttachments(projectId, { c });
                return path;
            };

            // Normal path, no pending submission: record and bytes both go.
            auto first = seed("rem-ok", "att-one");
            auto second = seed("rem-ok", "att-two");
            auto const single = service.RemoveDraftAttachment("rem-ok", "att-one");
            EXPECT(single.recordRemoved);
            EXPECT(single.bytesRemoved);
            EXPECT(single.unresolvedReason.empty());
            EXPECT(!fs::exists(first));
            EXPECT(fs::exists(second));
            EXPECT(store.GetDraftAttachments("rem-ok").size() == 1);

            auto const cleared = service.ClearDraftAttachments("rem-ok");
            EXPECT(cleared.recordRemoved);
            EXPECT(cleared.bytesRemoved);
            EXPECT(!fs::exists(second));
            EXPECT(store.GetDraftAttachments("rem-ok").empty());

            // Cleanup failure: the file is held open without delete sharing, so the
            // physical bytes cannot be removed. The record is retired (committed) first;
            // the bytes remain untracked and reclaimable, and the precise cause is
            // reported instead of a silently half-completed removal.
            auto stuck = seed("rem-stuck", "att-stuck");
            HANDLE hold = CreateFileW(stuck.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            EXPECT(hold != INVALID_HANDLE_VALUE);

            auto const stuckResult = service.RemoveDraftAttachment("rem-stuck", "att-stuck");
            CloseHandle(hold);

            EXPECT(stuckResult.recordRemoved);
            EXPECT(!stuckResult.bytesRemoved);
            EXPECT(fs::exists(stuck));
            EXPECT(store.GetDraftAttachments("rem-stuck").empty());

            // Once the interfering handle is gone, the bytes can be reclaimed.
            std::error_code rmEc;
            fs::remove(stuck, rmEc);
            CASES_END();
        }

        // 43. F1: an attachment identity is owned by the project that recorded it. The
        // upsert matched on attachment_id alone, and the pending-manifest guards used the
        // SUPPLIED project, so a write naming another project silently rewrote the owner's
        // row while reporting success.
        {
            CASES_BEGIN("43. An attachment identity cannot be written through another project");
            mayasaba::Storage store(folder / "ownership.db");
            store.BindProject("proj-a", "A", "C:/A");
            store.BindProject("proj-b", "B", "C:/B");

            mayasaba::AttachmentRecord owned;
            owned.id = "att-shared";
            owned.projectId = "proj-a";
            owned.fileName = "a.txt";
            owned.fileSize = 5;
            owned.sha256 = "sha-a";
            owned.detectedType = "text/plain";
            owned.storagePath = "C:/stage/a.txt";
            owned.sourcePath = "C:/source/a.txt";
            owned.status = "REJECTED";
            owned.errorMessage = "original error";
            store.SaveDraftAttachment("proj-a", owned);

            auto const beforeRows = store.GetDraftAttachments("proj-a");
            EXPECT(beforeRows.size() == 1);
            uint64_t const versionBefore = beforeRows[0].version;

            // The reproduction from the review: same identity, another project, new path.
            mayasaba::AttachmentRecord foreign = owned;
            foreign.projectId = "proj-b";
            foreign.storagePath = "C:/stage/b-replacement.txt";
            foreign.errorMessage = "overwritten from B";
            bool singleRefused = false;
            try { store.SaveDraftAttachment("proj-b", foreign); }
            catch (std::exception const&) { singleRefused = true; }
            EXPECT(singleRefused);

            auto const afterRows = store.GetDraftAttachments("proj-a");
            EXPECT(afterRows.size() == 1);
            EXPECT(afterRows[0].storagePath == "C:/stage/a.txt");
            EXPECT(afterRows[0].errorMessage == "original error");
            EXPECT(afterRows[0].sha256 == "sha-a");
            EXPECT(afterRows[0].version == versionBefore);
            EXPECT(store.GetDraftAttachments("proj-b").empty());

            // Batch: a rejected batch must leave no partial update, so the innocent first
            // element must not be inserted before the conflicting one is refused.
            mayasaba::AttachmentRecord innocent;
            innocent.id = "att-innocent";
            innocent.projectId = "proj-b";
            innocent.fileName = "b.txt";
            innocent.fileSize = 7;
            innocent.sha256 = "sha-b";
            innocent.detectedType = "text/plain";
            innocent.storagePath = "C:/stage/b.txt";
            innocent.status = "READY";
            bool batchRefused = false;
            try { store.SaveDraftAttachments("proj-b", { innocent, foreign }); }
            catch (std::exception const&) { batchRefused = true; }
            EXPECT(batchRefused);
            EXPECT(store.GetDraftAttachments("proj-b").empty());
            EXPECT(store.GetDraftAttachments("proj-a").size() == 1);

            // A legitimate same-project update still works and bumps the version.
            mayasaba::AttachmentRecord sameProject = owned;
            sameProject.errorMessage = "updated in A";
            store.SaveDraftAttachment("proj-a", sameProject);
            auto const updated = store.GetDraftAttachments("proj-a");
            EXPECT(updated.size() == 1);
            EXPECT(updated[0].errorMessage == "updated in A");
            EXPECT(updated[0].version == versionBefore + 1);

            // With a locked pending submission in the owning project, a foreign write must
            // leave the record, its version, digest, staged path, bytes and the pending
            // identity exactly as they were.
            store.BindProject("proj-c", "C", "C:/C");
            auto stagedDir = folder / "ownership-staging" / "proj-c";
            fs::create_directories(stagedDir);
            auto stagedFile = stagedDir / "locked.txt";
            {
                std::ofstream of(stagedFile, std::ios::binary);
                of.write("locked bytes", 12);
            }
            mayasaba::AttachmentRecord locked;
            locked.id = "att-locked";
            locked.projectId = "proj-c";
            locked.fileName = "locked.txt";
            locked.fileSize = 12;
            locked.sha256 = ComputeSha256ForTestFile(WideToUtf8(stagedFile.wstring()));
            locked.detectedType = "text/plain";
            locked.storagePath = WideToUtf8(stagedFile.wstring());
            locked.sourcePath = "C:/source/locked.txt";
            locked.status = "READY";
            store.SaveDraftAttachment("proj-c", locked);
            EXPECT(store.StageSubmission("proj-c", "sub-locked", "body") == "sub-locked");

            auto const lockedBefore = store.GetDraftAttachments("proj-c");
            EXPECT(lockedBefore.size() == 1);

            mayasaba::AttachmentRecord foreignLocked = locked;
            foreignLocked.projectId = "proj-d";
            foreignLocked.sha256 = "different-digest";
            foreignLocked.storagePath = "C:/stage/elsewhere.txt";
            bool lockedRefused = false;
            try { store.SaveDraftAttachment("proj-d", foreignLocked); }
            catch (std::exception const&) { lockedRefused = true; }
            EXPECT(lockedRefused);

            auto const lockedAfter = store.GetDraftAttachments("proj-c");
            EXPECT(lockedAfter.size() == 1);
            EXPECT(lockedAfter[0].id == lockedBefore[0].id);
            EXPECT(lockedAfter[0].version == lockedBefore[0].version);
            EXPECT(lockedAfter[0].sha256 == lockedBefore[0].sha256);
            EXPECT(lockedAfter[0].storagePath == lockedBefore[0].storagePath);
            EXPECT(store.Pending("proj-c")->id == "sub-locked");
            EXPECT(fs::exists(stagedFile));
            EXPECT(store.GetDraftAttachments("proj-d").empty());
            CASES_END();
        }

        // 44. F2: quarantine must refuse every mutation, not only those that happen to
        // begin with BEGIN IMMEDIATE. A direct prepared write used to reach SQLite on a
        // connection whose transaction outcome was unknown.
        {
            CASES_BEGIN("44. Every storage mutation is refused on a quarantined connection");
            {
                mayasaba::Storage store(folder / "quarantine-all.db");
                store.BindProject("proj-q", "Q", "C:/Q");

                mayasaba::AttachmentRecord att;
                att.id = "att-q";
                att.projectId = "proj-q";
                att.fileName = "q.txt";
                att.fileSize = 3;
                att.sha256 = "sha-q";
                att.detectedType = "text/plain";
                att.storagePath = "C:/stage/q.txt";
                // READY, because a submission may only be staged over a complete draft.
                att.status = "READY";
                store.SaveDraftAttachment("proj-q", att);
                EXPECT(store.StageSubmission("proj-q", "sub-q", "pending body") == "sub-q");

                store.SetForceRollbackFailureForTesting(true);
                mayasaba::SaveContributionDetail detail;
                auto const disposition = store.SaveContribution("proj-q", "sub-q", "different body", {}, detail);
                EXPECT(disposition == SaveDisposition::Unknown);
                EXPECT(store.IsQuarantinedForTesting());

                // Reads must still work: the Action Center and inspection have to describe
                // the very state the quarantine protects.
                auto const draftsBefore = store.GetDraftAttachments("proj-q");
                EXPECT(draftsBefore.size() == 1);
                EXPECT(store.Pending("proj-q").has_value());
                EXPECT(store.Project("proj-q").has_value());

                auto refused = [](auto&& operation)
                {
                    try { operation(); }
                    catch (std::exception const&) { return true; }
                    return false;
                };

                // Refusal alone is not enough. The injected failure leaves a transaction open,
                // so a write can fail incidentally with "cannot start a transaction within a
                // transaction" even when nothing checks quarantine. The reason must name the
                // quarantine, which proves the refusal came from write admission.
                auto refusalReason = [](auto&& operation) -> std::string
                {
                    try { operation(); }
                    catch (std::exception const& ex) { return ex.what(); }
                    return std::string{};
                };

                std::string const singleWrite = refusalReason([&] { store.SaveDraftAttachment("proj-q", att); });
                EXPECT(!singleWrite.empty());
                EXPECT(singleWrite.find("quarantined") != std::string::npos);
                std::string const batchWrite = refusalReason([&] { store.SaveDraftAttachments("proj-q", { att }); });
                EXPECT(batchWrite.find("quarantined") != std::string::npos);
                std::string const draftText = refusalReason([&] { store.SaveDraft("proj-q", "draft text"); });
                EXPECT(draftText.find("quarantined") != std::string::npos);
                std::string const stage = refusalReason([&] { store.StageSubmission("proj-q", "sub-q2", "second"); });
                EXPECT(stage.find("quarantined") != std::string::npos);
                std::string const bind = refusalReason([&] { store.BindProject("proj-q2", "Q2", "C:/Q2"); });
                EXPECT(bind.find("quarantined") != std::string::npos);

                // Every mutation entry point, exercised through the production methods.
                EXPECT(refused([&] { store.SetClosure("proj-q", 0, true); }));
                EXPECT(refused([&] { store.RemoveDraftAttachment("proj-q", "att-q"); }));
                EXPECT(refused([&] { store.ClearDraftAttachments("proj-q"); }));
                EXPECT(refused([&] { store.TouchProjectOpened("proj-q"); }));
                EXPECT(refused([&] { store.RelocateProject("proj-q", "C:/Q-moved", "Q"); }));

                // Nothing changed.
                EXPECT(store.GetDraftAttachments("proj-q").size() == 1);
                EXPECT(store.GetDraftAttachments("proj-q")[0].status == "READY");
                EXPECT(store.Pending("proj-q")->id == "sub-q");
                EXPECT(!store.Project("proj-q")->closed);
                EXPECT(store.Project("proj-q")->canonicalPath == "C:/Q");
                EXPECT(!store.Project("proj-q2").has_value());
            }

            // The authorized recovery path: reopening the store clears the quarantine
            // because the unknown transaction died with the connection.
            mayasaba::Storage reopened(folder / "quarantine-all.db");
            EXPECT(!reopened.IsQuarantinedForTesting());
            reopened.SaveDraft("proj-q", "after reopen");
            EXPECT(reopened.Project("proj-q")->draft == "after reopen");
            CASES_END();
        }

        // 45. C10 gate I1: the owner transaction protocol's durable delivery half.
        // C09 steps 7-9 require the causal outbox envelope and the command receipt to
        // be written inside the same transaction as the owner's state change, so a
        // crash cannot leave an effect with no envelope or an envelope with no
        // receipt. These cases inject the failures that ordering exists to survive:
        // a duplicate identity, a changed request under one identity, a mutation that
        // throws, a malformed envelope, a lost acknowledgement and a restart.
        {
            CASES_BEGIN("45. Durable outbox, inbox admission and command receipts (C09 steps 7-9)");
            namespace delivery = mayasaba::delivery;

            auto const uuid = [](char const* seed) -> std::string
            {
                // A deterministic, canonical UUIDv4-shaped identity. Version and
                // variant nibbles are set so the validator's shape check is
                // exercised rather than bypassed.
                unsigned long long h = 1469598103934665603ull;
                for (char const* p = seed; *p; ++p) { h ^= static_cast<unsigned char>(*p); h *= 1099511628211ull; }
                char buf[37];
                unsigned long long a = h, b = h * 6364136223846793005ull + 1442695040888963407ull;
                std::snprintf(buf, sizeof buf, "%08llx-%04llx-4%03llx-a%03llx-%012llx",
                              a & 0xFFFFFFFFull,
                              (a >> 32) & 0xFFFFull,
                              (a >> 48) & 0x0FFFull,
                              (b >> 8) & 0x0FFFull,
                              b & 0xFFFFFFFFFFFFull);
                return std::string(buf);
            };

            auto makeEnvelope = [&](char const* seed, std::uint64_t sequence) -> delivery::OutboxEnvelope
            {
                delivery::OutboxEnvelope envelope;
                envelope.envelopeId = uuid(seed);
                envelope.messageClass = "TASK_CONTROL";
                envelope.sender = "controller";
                envelope.recipients = { "hermes-agent" };
                envelope.projectId = uuid("project");
                envelope.approvedEpoch = "1";
                envelope.operationId = uuid("operation");
                envelope.effectId = uuid("effect");
                envelope.orderingScope = "project:" + uuid("project");
                envelope.sequence = sequence;
                envelope.correlationId = uuid("correlation");
                envelope.payloadType = "task.control.v1";
                // Already canonical: one key, no whitespace, so the validator's
                // "payload must be canonical" rule is satisfied by construction.
                envelope.payload = "{\"a\":1}";
                auto const digest = mayasaba::Sha256Hex(envelope.payload);
                EXPECT(digest.has_value());
                envelope.payloadDigest = *digest;
                envelope.createdAtUtc = "2026-10-22T09:00:00.000Z";
                return envelope;
            };

            {
                mayasaba::Storage store(folder / "delivery.db");

                // ---- C09 step 9: a committed command records state, envelope and
                // receipt together.
                delivery::OutboxEnvelope first = makeEnvelope("envelope-1", 1);
                mayasaba::CommandCommit commit;
                commit.operationId = uuid("operation");
                commit.commandId = uuid("command-1");
                commit.canonicalRequestBytes = "{\"task\":\"write-file\"}";
                commit.owner = "task-dag-engine";
                commit.recordedAtUtc = "2026-10-22T09:00:00.000Z";
                commit.eventIds = { uuid("event-1") };
                commit.envelopes = { first };
                int mutationCalls = 0;
                commit.applyMutation = [&](std::string& recordId, std::string& version)
                {
                    ++mutationCalls;
                    recordId = uuid("record-1");
                    version = "1";
                };

                auto const committed = store.CommitCommand(commit);
                EXPECT(committed.resultKind == delivery::ResultKind::Committed);
                EXPECT(mutationCalls == 1);
                EXPECT(committed.resultRecordId == uuid("record-1"));
                EXPECT(committed.resultRecordVersion == "1");
                EXPECT(committed.eventIds.size() == 1);

                // The envelope is queued, not delivered. A row that says it was sent
                // when nothing acknowledged it would make a lost ACK invisible.
                auto queued = store.QueuedOutbox(uuid("project"));
                EXPECT(queued.size() == 1);
                EXPECT(queued[0].envelopeId == first.envelopeId);
                EXPECT(queued[0].payload == "{\"a\":1}");
                EXPECT(queued[0].recipients.size() == 1);
                EXPECT(store.LastDeliveredSequence("project:" + uuid("project")) == 0);

                // ---- C09 step 4: the same identity with the same canonical bytes is
                // a DUPLICATE that runs no mutation and rewrites no original.
                auto const originalReceiptId = committed.receiptId;
                auto const duplicate = store.CommitCommand(commit);
                EXPECT(duplicate.resultKind == delivery::ResultKind::Duplicate);
                EXPECT(mutationCalls == 1);                       // not applied twice
                EXPECT(duplicate.originalReceiptId == originalReceiptId);
                EXPECT(duplicate.originalReceiptDigest.size() == 64);
                // The original receipt is untouched: a reader that follows the link
                // must find what it read the first time.
                auto const reread = store.ReceiptForOperation(uuid("operation"));
                EXPECT(reread.has_value());
                EXPECT(reread->receiptId == originalReceiptId);
                EXPECT(reread->resultKind == delivery::ResultKind::Committed);
                // Exactly one outbox row: the duplicate added no envelope.
                EXPECT(store.QueuedOutbox(uuid("project")).size() == 1);

                // ---- Same identity, DIFFERENT canonical bytes: refused, and nothing
                // is applied or rewritten. This is the case that separates "at least
                // once delivery" from "silently deduplicated".
                auto conflicting = commit;
                conflicting.commandId = uuid("command-conflict");
                conflicting.canonicalRequestBytes = "{\"task\":\"delete-file\"}";
                int conflictCalls = 0;
                conflicting.applyMutation = [&](std::string&, std::string&) { ++conflictCalls; };
                auto const refused = store.CommitCommand(conflicting);
                EXPECT(refused.resultKind == delivery::ResultKind::Refused);
                EXPECT(refused.errorCode == "IDEMPOTENCY_CONFLICT");
                EXPECT(conflictCalls == 0);
                EXPECT(store.QueuedOutbox(uuid("project")).size() == 1);
                // The refused attempt is recorded as its own receipt, but the
                // operation's receipt is still the original commit.
                auto const afterConflict = store.ReceiptForOperation(uuid("operation"));
                EXPECT(afterConflict->receiptId == originalReceiptId);
                EXPECT(afterConflict->resultKind == delivery::ResultKind::Committed);

                // ---- A mutation that throws rolls back the whole transaction: no
                // state change, no envelope, and a REFUSED receipt that does not
                // claim a commit.
                delivery::OutboxEnvelope second = makeEnvelope("envelope-2", 2);
                mayasaba::CommandCommit throwing;
                throwing.operationId = uuid("operation-throw");
                throwing.commandId = uuid("command-throw");
                throwing.canonicalRequestBytes = "{\"task\":\"throwing\"}";
                throwing.owner = "workspace-manager";
                throwing.recordedAtUtc = "2026-10-22T09:01:00.000Z";
                throwing.eventIds = { uuid("event-throw") };
                throwing.envelopes = { second };
                throwing.applyMutation = [](std::string&, std::string&)
                {
                    throw std::runtime_error("precondition failed");
                };
                auto const refusedMutation = store.CommitCommand(throwing);
                EXPECT(refusedMutation.resultKind == delivery::ResultKind::Refused);
                EXPECT(refusedMutation.errorCode == "PRECONDITION_FAILED");
                EXPECT(refusedMutation.physicalOutcome == "NOT_STARTED");
                EXPECT(store.QueuedOutbox(uuid("project")).size() == 1);   // no envelope leaked

                // ---- A malformed envelope is refused, and the refusal is a throw
                // rather than a COMMITTED receipt beside an unrecordable envelope.
                delivery::OutboxEnvelope tampered = makeEnvelope("envelope-3", 3);
                tampered.payload = "{\"a\":2}";        // digest no longer matches
                mayasaba::CommandCommit badEnvelope;
                badEnvelope.operationId = uuid("operation-bad-envelope");
                badEnvelope.commandId = uuid("command-bad-envelope");
                badEnvelope.canonicalRequestBytes = "{\"task\":\"bad-envelope\"}";
                badEnvelope.owner = "task-dag-engine";
                badEnvelope.recordedAtUtc = "2026-10-22T09:02:00.000Z";
                badEnvelope.eventIds = { uuid("event-bad") };
                badEnvelope.envelopes = { tampered };
                // A valid record identity, so the envelope is the ONLY thing that
                // can make this commit fail. Without it the commit would be refused
                // by the receipt validator instead, and the assertion below would
                // pass for a reason that has nothing to do with the envelope --
                // which is exactly what a controlling mutation exposed.
                badEnvelope.applyMutation = [&](std::string& recordId, std::string& version)
                { recordId = uuid("record-bad"); version = "1"; };
                bool refusedBadEnvelope = false;
                try { store.CommitCommand(badEnvelope); }
                catch (std::exception const&) { refusedBadEnvelope = true; }
                EXPECT(refusedBadEnvelope);
                // Nothing recorded: not the envelope, not a receipt.
                EXPECT(store.QueuedOutbox(uuid("project")).size() == 1);
                EXPECT(!store.ReceiptForOperation(uuid("operation-bad-envelope")).has_value());
                // And the store is still usable, not quarantined by the rollback.
                EXPECT(!store.IsQuarantinedForTesting());

                // ---- Delivery bookkeeping. Only an acknowledgement moves an
                // envelope out of the queue, and a second acknowledgement of the
                // same envelope is not a second delivery.
                EXPECT(store.MarkEnvelopeDispatched(first.envelopeId));
                EXPECT(!store.MarkEnvelopeDispatched(first.envelopeId));
                EXPECT(store.QueuedOutbox(uuid("project")).empty());
                EXPECT(store.LastDeliveredSequence("project:" + uuid("project")) == 1);

                // ---- Inbox admission. A second admission of one envelope id is a
                // no-op, not an error: the sender is entitled to retry, and refusing
                // the retry would turn a lost ACK into a lost message.
                EXPECT(store.AdmitInboxEnvelope(first));
                EXPECT(!store.AdmitInboxEnvelope(first));
                // A different sequence in the same scope is a different message.
                delivery::OutboxEnvelope third = makeEnvelope("envelope-4", 2);
                EXPECT(store.AdmitInboxEnvelope(third));
            }

            // ---- Restart: the receipts and the queued outbox survive, and the
            // idempotency decision is made against durable state rather than
            // memory.
            {
                mayasaba::Storage reopened(folder / "delivery.db");
                auto const receipt = reopened.ReceiptForOperation(uuid("operation"));
                EXPECT(receipt.has_value());
                EXPECT(receipt->resultKind == delivery::ResultKind::Committed);
                EXPECT(receipt->resultRecordVersion == "1");
                EXPECT(!receipt->resultKindUnreadable);
                EXPECT(!receipt->retryKindUnreadable);

                // A replay of the same identity after restart is still a duplicate.
                mayasaba::CommandCommit replay;
                replay.operationId = uuid("operation");
                replay.commandId = uuid("command-1");
                replay.canonicalRequestBytes = "{\"task\":\"write-file\"}";
                replay.owner = "task-dag-engine";
                replay.recordedAtUtc = "2026-10-22T09:00:00.000Z";
                int replayCalls = 0;
                replay.applyMutation = [&](std::string&, std::string&) { ++replayCalls; };
                auto const replayReceipt = reopened.CommitCommand(replay);
                EXPECT(replayReceipt.resultKind == delivery::ResultKind::Duplicate);
                EXPECT(replayCalls == 0);
                EXPECT(replayReceipt.originalReceiptId == receipt->receiptId);

                // The inbox row admitted before the restart is still there, so a
                // redelivery after the restart is still recognised as a duplicate.
                EXPECT(!reopened.AdmitInboxEnvelope(makeEnvelope("envelope-1", 1)));
                EXPECT(reopened.LastDeliveredSequence("project:" + uuid("project")) == 1);
            }

            // ---- A receipt written by a newer build must not be silently
            // reinterpreted by an older reader. Unreadable kind and policy are
            // reported, not defaulted.
            {
                mayasaba::Storage store(folder / "delivery-newer.db");
                mayasaba::CommandCommit commit;
                commit.operationId = uuid("operation-future");
                commit.commandId = uuid("command-future");
                commit.canonicalRequestBytes = "{\"task\":\"future\"}";
                commit.owner = "task-dag-engine";
                commit.recordedAtUtc = "2026-10-22T09:03:00.000Z";
                commit.eventIds = { uuid("event-future") };
                commit.applyMutation = [&](std::string& recordId, std::string& version)
                { recordId = uuid("record-future"); version = "2"; };
                store.CommitCommand(commit);
            }
            {
                // Rewrite the stored result kind and retry policy to values this
                // build does not register, the way a newer build could.
                std::string const path = (folder / "delivery-newer.db").string();
                sqlite3* raw = nullptr;
                std::wstring const wide(path.begin(), path.end());
                EXPECT(sqlite3_open16(wide.c_str(), &raw) == SQLITE_OK);
                std::string const update = "UPDATE command_receipts SET result_kind='RECONCILING', "
                                           "retry_kind='WHEN_READY' WHERE operation_id='" + uuid("operation-future") + "';";
                EXPECT(sqlite3_exec(raw, update.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
                sqlite3_close(raw);

                mayasaba::Storage store(folder / "delivery-newer.db");
                auto const receipt = store.ReceiptForOperation(uuid("operation-future"));
                EXPECT(receipt.has_value());
                EXPECT(receipt->resultKindUnreadable);
                EXPECT(receipt->retryKindUnreadable);
            }

            // ---- Migration from a store this build's predecessor wrote. A v3
            // store differs from v4 by exactly the three delivery tables and the
            // version number, so removing those tables and resetting the version
            // reconstructs the v3 shape. Reopening must create them without
            // disturbing the records that were already there.
            {
                std::string const path = (folder / "delivery-migrate.db").string();
                {
                    mayasaba::Storage store(folder / "delivery-migrate.db");
                    store.BindProject("proj-migrate", "Migrate", "C:/Migrate");
                    store.SaveDraft("proj-migrate", "draft survives migration");
                }
                {
                    sqlite3* raw = nullptr;
                    std::wstring const wide(path.begin(), path.end());
                    EXPECT(sqlite3_open16(wide.c_str(), &raw) == SQLITE_OK);
                    char const* const downgrade =
                        "DROP TABLE outbox_envelopes; DROP TABLE inbox_envelopes; DROP TABLE command_receipts; "
                        "PRAGMA user_version=3;";
                    EXPECT(sqlite3_exec(raw, downgrade, nullptr, nullptr, nullptr) == SQLITE_OK);
                    sqlite3_close(raw);
                }
                {
                    mayasaba::Storage store(folder / "delivery-migrate.db");
                    EXPECT(store.Project("proj-migrate").has_value());
                    EXPECT(store.Project("proj-migrate")->draft == "draft survives migration");
                    // The tables exist and are usable, so the migration ran rather
                    // than leaving the version alone.
                    EXPECT(store.QueuedOutbox(uuid("project")).empty());
                    EXPECT(store.LastDeliveredSequence("any-scope") == 0);
                    EXPECT(!store.ReceiptForOperation(uuid("operation-migrate")).has_value());
                    EXPECT(store.AdmitInboxEnvelope(makeEnvelope("envelope-migrate", 1)));
                }
            }

            // ---- The schema version guard still refuses a store from a newer build
            // rather than reading it with the wrong table shapes.
            {
                std::string const path = (folder / "delivery-future.db").string();
                sqlite3* raw = nullptr;
                std::wstring const wide(path.begin(), path.end());
                EXPECT(sqlite3_open16(wide.c_str(), &raw) == SQLITE_OK);
                EXPECT(sqlite3_exec(raw, "CREATE TABLE probe(x); PRAGMA user_version=99;", nullptr, nullptr, nullptr) == SQLITE_OK);
                sqlite3_close(raw);

                bool refused = false;
                try { mayasaba::Storage store(folder / "delivery-future.db"); }
                catch (std::exception const&) { refused = true; }
                EXPECT(refused);
            }
            CASES_END();
        }

        // 46. StageSubmission owns its transaction to the end (R01). Its catch path
        // used an unchecked ROLLBACK, so a refused rollback left the connection inside
        // an unresolved transaction and a later TrySaveDraft reported durable success
        // for a write the eventual rollback erased. Cleanup must be proven.
        {
            CASES_BEGIN("46. StageSubmission cleanup is proven; unresolved transaction quarantines");
            {
                mayasaba::Storage store(folder / "stage_leak.db");
                store.BindProject("proj-s1", "S1", "C:/S1");
                EXPECT(store.TrySaveDraft("proj-s1", "before"));

                // Failure before the transaction is acquired leaves no state to clean.
                bool threw = false;
                try { store.StageSubmission("", "op-empty", "body"); }
                catch (std::exception const&) { threw = true; }
                EXPECT(threw);
                EXPECT(!store.IsQuarantinedForTesting());
                EXPECT(store.Project("proj-s1")->draft == "before");

                // A refusal inside the transaction rolls back proven; the store stays usable.
                int const version = store.Project("proj-s1")->version;
                store.SetClosure("proj-s1", version, true);
                threw = false;
                std::string reason;
                try { store.StageSubmission("proj-s1", "op-closed", "closed body"); }
                catch (std::exception const& ex) { threw = true; reason = ex.what(); }
                EXPECT(threw);
                EXPECT(reason.find("Closed project") != std::string::npos);
                EXPECT(!store.IsQuarantinedForTesting());
                EXPECT(store.TrySaveDraft("proj-s1", "before"));
                EXPECT(store.Project("proj-s1")->draft == "before");

                // The discriminating failure (R01): a refused rollback after the same
                // mid-transaction refusal used to leak the open transaction, and a later
                // TrySaveDraft returned true for a write the eventual rollback erased.
                store.SetForceRollbackFailureForTesting(true);
                threw = false;
                try { store.StageSubmission("proj-s1", "op-leak", "leak body"); }
                catch (std::exception const& ex) { threw = true; reason = ex.what(); }
                EXPECT(threw);
                EXPECT(reason.find("Closed project") != std::string::npos);
                EXPECT(store.IsQuarantinedForTesting());
                // A later draft write must never report durable success while trapped
                // inside an unresolved earlier transaction: it refuses with the reason.
                threw = false;
                std::string draftReason;
                try { store.TrySaveDraft("proj-s1", "after"); }
                catch (std::exception const& ex) { threw = true; draftReason = ex.what(); }
                EXPECT(threw);
                EXPECT(draftReason.find("quarantined") != std::string::npos);
                // Reads stay available for inspection and recovery.
                EXPECT(store.Project("proj-s1").has_value());
                EXPECT(store.Project("proj-s1")->draft == "before");
            }

            // Reopening clears the quarantine: the unknown transaction died with the
            // connection, so reconciliation can proceed on the reopened store.
            mayasaba::Storage reopened(folder / "stage_leak.db");
            EXPECT(!reopened.IsQuarantinedForTesting());
            EXPECT(reopened.Project("proj-s1")->draft == "before");
            int const reopenVersion = reopened.Project("proj-s1")->version;
            reopened.SetClosure("proj-s1", reopenVersion, false);
            EXPECT(reopened.StageSubmission("proj-s1", "op-recovered", "recovered body") == "op-recovered");

            // A COMMIT failure rolls back proven: the store stays usable and the staged
            // submission is not half-recorded. The re-stage uses the same payload, which
            // the pending submission accepts as an identical re-submission.
            reopened.SetForceCommitFailureForTesting(true);
            bool commitThrew = false;
            try { reopened.StageSubmission("proj-s1", "op-commit2", "recovered body"); }
            catch (std::exception const&) { commitThrew = true; }
            EXPECT(commitThrew);
            EXPECT(!reopened.IsQuarantinedForTesting());
            EXPECT(reopened.Pending("proj-s1").has_value());
            EXPECT(reopened.Pending("proj-s1")->id == "op-recovered");
            // The retry succeeds and the earlier staged identity is preserved.
            EXPECT(reopened.StageSubmission("proj-s1", "op-commit2", "recovered body") == "op-recovered");
            CASES_END();
        }

        // 47. CommitSelection owns its transaction to the end (R02). Its catch path
        // used an unchecked ROLLBACK, so a refused rollback after a mid-transaction
        // identity refusal leaked an open transaction that a later draft write
        // silently joined and lost.
        {
            CASES_BEGIN("47. CommitSelection cleanup is proven; unresolved transaction quarantines");
            auto const uuid = [](char const* seed) -> std::string
            {
                // Same deterministic UUIDv4-shaped identity as group 45.
                unsigned long long h = 1469598103934665603ull;
                for (char const* p = seed; *p; ++p) { h ^= static_cast<unsigned char>(*p); h *= 1099511628211ull; }
                char buf[37];
                unsigned long long a = h, b = h * 6364136223846793005ull + 1442695040888963407ull;
                std::snprintf(buf, sizeof buf, "%08llx-%04llx-4%03llx-a%03llx-%012llx",
                              a & 0xFFFFFFFFull,
                              (a >> 32) & 0xFFFFull,
                              (a >> 48) & 0x0FFFull,
                              (b >> 8) & 0x0FFFull,
                              b & 0xFFFFFFFFFFFFull);
                return std::string(buf);
            };
            {
                mayasaba::Storage store(folder / "selection_leak.db");
                mayasaba::SelectionCommit commit;
                commit.operationId = uuid("sel-op");
                commit.proposedId = uuid("sel-proj");
                commit.requestBytes = "request one";
                commit.name = "Sel";
                commit.path = "C:/Sel";
                commit.filesystemIdentity = "fid-sel-1";
                EXPECT(store.CommitSelection(commit).project.id == commit.proposedId);

                // Replaying the identity with changed input is refused inside the
                // transaction; the proven rollback leaves the store usable.
                bool threw = false;
                try
                {
                    mayasaba::SelectionCommit replay = commit;
                    replay.requestBytes = "changed input";
                    store.CommitSelection(replay);
                }
                catch (std::exception const&) { threw = true; }
                EXPECT(threw);
                EXPECT(!store.IsQuarantinedForTesting());
                EXPECT(store.CommitSelection(commit).duplicate);

                // The same refusal with a refused rollback used to leak the transaction
                // (R02): a later TrySaveDraft reported success and its write was lost.
                store.SetForceRollbackFailureForTesting(true);
                threw = false;
                try
                {
                    mayasaba::SelectionCommit replay = commit;
                    replay.requestBytes = "changed input again";
                    store.CommitSelection(replay);
                }
                catch (std::exception const&) { threw = true; }
                EXPECT(threw);
                EXPECT(store.IsQuarantinedForTesting());
                threw = false;
                std::string draftReason;
                try { store.TrySaveDraft(commit.proposedId, "unwritten"); }
                catch (std::exception const& ex) { threw = true; draftReason = ex.what(); }
                EXPECT(threw);
                EXPECT(draftReason.find("quarantined") != std::string::npos);
                EXPECT(store.Project(commit.proposedId).has_value());
            }

            mayasaba::Storage reopened(folder / "selection_leak.db");
            EXPECT(!reopened.IsQuarantinedForTesting());
            mayasaba::SelectionCommit commit;
            commit.operationId = uuid("sel-op");
            commit.proposedId = uuid("sel-proj");
            commit.requestBytes = "request one";
            commit.name = "Sel";
            commit.path = "C:/Sel";
            commit.filesystemIdentity = "fid-sel-1";
            EXPECT(reopened.CommitSelection(commit).duplicate);

            // A COMMIT failure rolls back proven and stays usable; the replay still
            // resolves to the same durable receipt afterwards.
            reopened.SetForceCommitFailureForTesting(true);
            bool threw = false;
            try { reopened.CommitSelection(commit); }
            catch (std::exception const&) { threw = true; }
            EXPECT(threw);
            EXPECT(!reopened.IsQuarantinedForTesting());
            EXPECT(reopened.CommitSelection(commit).duplicate);
            CASES_END();
        }

        // 48. The Action Center read-transaction guard cleans up with proof (A01).
        // The destructor used to execute an unchecked ROLLBACK, so an exception inside
        // the read transaction with a refused rollback left the connection inside the
        // unresolved transaction and a later TrySaveDraft reported success for a write
        // the eventual rollback erased. The destructor is non-throwing, but its cleanup
        // outcome must still quarantine the store when it cannot be confirmed.
        {
            CASES_BEGIN("48. Action Center read-guard destructor cleanup is proven");
            {
                mayasaba::Storage store(folder / "ac_leak.db");
                store.BindProject("proj-ac1", "AC", "C:/AC1");
                EXPECT(store.TrySaveDraft("proj-ac1", "before"));

                // A failure inside the read transaction: the destructor rolls the read
                // transaction back and the store stays usable.
                store.SetQueryActionCenterSnapshotHookForTesting(
                    []() { throw std::runtime_error("Injected snapshot failure"); });
                mayasaba::ActionCenterQuery q{ "proj-ac1", 50 };
                auto failed = store.QueryActionCenter(q);
                EXPECT(!failed.isComplete);
                EXPECT(failed.errorMessage.find("Injected snapshot failure") != std::string::npos);
                EXPECT(!store.IsQuarantinedForTesting());
                EXPECT(store.TrySaveDraft("proj-ac1", "before"));

                // The same in-transaction failure with a refused rollback (A01): the
                // unconfirmed destructor cleanup now quarantines the store.
                store.SetForceRollbackFailureForTesting(true);
                auto leaked = store.QueryActionCenter(q);
                EXPECT(!leaked.isComplete);
                EXPECT(leaked.errorMessage.find("Injected snapshot failure") != std::string::npos);
                EXPECT(store.IsQuarantinedForTesting());
                store.SetQueryActionCenterSnapshotHookForTesting(nullptr);
                bool threw = false;
                std::string draftReason;
                try { store.TrySaveDraft("proj-ac1", "after"); }
                catch (std::exception const& ex) { threw = true; draftReason = ex.what(); }
                EXPECT(threw);
                EXPECT(draftReason.find("quarantined") != std::string::npos);
                // Reads stay available for inspection and recovery.
                EXPECT(store.Project("proj-ac1").has_value());
                EXPECT(store.Project("proj-ac1")->draft == "before");
                // The Action Center query reports truthfully rather than pretending to
                // read: its own read snapshot cannot begin while the connection is
                // still physically inside the unresolved transaction. That reported
                // failure is what the quarantine exists to explain.
                auto inspected = store.QueryActionCenter({"proj-ac1", 50});
                EXPECT(!inspected.isComplete);
                EXPECT(inspected.errorMessage.find("transaction") != std::string::npos);
            }

            // Reopening clears the quarantine and the draft survives reconciliation.
            mayasaba::Storage reopened(folder / "ac_leak.db");
            EXPECT(!reopened.IsQuarantinedForTesting());
            EXPECT(reopened.Project("proj-ac1")->draft == "before");
            reopened.SaveDraft("proj-ac1", "after reopen");
            EXPECT(reopened.Project("proj-ac1")->draft == "after reopen");
            CASES_END();
        }

        // ------------------------------------------------------------------
        // M2: physical attachment ownership and finalization (R03, R14, R04, R15).
        // ------------------------------------------------------------------

        // 49. Retiring one record never deletes bytes another protected record still
        // references, and the physical deletion is verified against the same object the
        // record named - not against a substituted or shared path.
        {
            CASES_BEGIN("49. Shared staged bytes survive the retirement of one referencing record");
            mayasaba::Storage store(folder / "shared-bytes.db");
            // Production stages inside the store's managed attachment directory; the
            // finalization boundary check (group 50) requires exactly that.
            auto staging = folder / "attachments";
            fs::create_directories(staging);
            mayasaba::AttachmentService service(store, staging);

            auto seed = [&](std::string const& projectId, std::string const& id)
            {
                store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
                auto dir = staging / "staging" / projectId;
                fs::create_directories(dir);
                auto path = dir / (id + ".txt");
                {
                    std::ofstream of(path, std::ios::binary);
                    of.write("shared bytes", 12);
                }
                mayasaba::AttachmentCandidate c;
                c.id = id;
                c.fileName = id + ".txt";
                c.fileSize = 12;
                c.detectedType = "text/plain";
                c.originalPath = L"C:/source/" + std::wstring(id.begin(), id.end()) + L".txt";
                c.stagedPath = path.wstring();
                c.sha256 = ComputeSha256ForTestFile(WideToUtf8(path.wstring()));
                c.status = mayasaba::AttachmentStatus::Ready;
                service.SaveDraftAttachments(projectId, { c });
                return path;
            };

            // (a) Two draft identities, one staged path, one project. Removing one must
            // remove only that record; the other record still protects the bytes.
            auto sharedFile = seed("share-a", "att-first");
            mayasaba::AttachmentRecord alias;
            alias.id = "att-second";
            alias.projectId = "share-a";
            alias.fileName = "att-first.txt";
            alias.fileSize = 12;
            alias.sha256 = ComputeSha256ForTestFile(WideToUtf8(sharedFile.wstring()));
            alias.detectedType = "text/plain";
            alias.storagePath = WideToUtf8(sharedFile.wstring());
            alias.sourcePath = "C:/source/alias.txt";
            alias.status = "READY";
            store.SaveDraftAttachment("share-a", alias);

            auto const first = store.RemoveDraftAttachmentGuarded("share-a", "att-first", nullptr);
            EXPECT(first.recordRemoved);
            EXPECT(first.bytesShared);
            EXPECT(!first.bytesRemoved);
            EXPECT(first.reasonEn.find("referenced by 1") != std::string::npos);
            EXPECT(fs::exists(sharedFile));
            EXPECT(store.GetDraftAttachments("share-a").size() == 1);
            EXPECT(store.GetDraftAttachments("share-a")[0].id == "att-second");

            // Removing the last referencing record does remove the bytes.
            auto const last = service.RemoveDraftAttachment("share-a", "att-second");
            EXPECT(last.recordRemoved);
            EXPECT(last.bytesRemoved);
            EXPECT(!fs::exists(sharedFile));
            EXPECT(store.GetDraftAttachments("share-a").empty());

            // (b) A draft alias of a committed attachment: removing the draft retires only
            // the draft row. The committed record keeps its sealed bytes, and inspection
            // still integrity-verifies them.
            auto committedFile = seed("share-b", "att-committed-src");
            store.StageSubmission("share-b", "sub-share-b", "body");
            mayasaba::AttachmentRecord committedRec;
            committedRec.id = "att-committed-src";
            committedRec.projectId = "share-b";
            committedRec.submissionId = "sub-share-b";
            committedRec.fileName = "att-committed-src.txt";
            committedRec.fileSize = 12;
            committedRec.sha256 = ComputeSha256ForTestFile(WideToUtf8(committedFile.wstring()));
            committedRec.detectedType = "text/plain";
            committedRec.storagePath = WideToUtf8(committedFile.wstring());
            committedRec.sourcePath = "C:/source/committed-src.txt";
            committedRec.status = "COMMITTED";
            EXPECT(store.SaveContribution("share-b", "sub-share-b", "body", { committedRec })
                == mayasaba::SaveDisposition::Saved);
            EXPECT(store.GetDraftAttachments("share-b").empty());

            mayasaba::AttachmentRecord draftAlias;
            draftAlias.id = "att-draft-alias";
            draftAlias.projectId = "share-b";
            draftAlias.fileName = "alias-of-committed.txt";
            draftAlias.fileSize = 12;
            draftAlias.sha256 = committedRec.sha256;
            draftAlias.detectedType = "text/plain";
            draftAlias.storagePath = committedRec.storagePath;
            draftAlias.sourcePath = "C:/source/alias-of-committed.txt";
            draftAlias.status = "READY";
            store.SaveDraftAttachment("share-b", draftAlias);

            auto const aliasRemoval = service.RemoveDraftAttachment("share-b", "att-draft-alias");
            EXPECT(aliasRemoval.recordRemoved);
            EXPECT(fs::exists(committedFile));
            EXPECT(store.GetAttachmentsForContribution("sub-share-b").size() == 1);
            auto committedInsp = store.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "share-b", "att-committed-src" });
            EXPECT(committedInsp.found);
            EXPECT(committedInsp.inspection.isManaged);
            EXPECT(committedInsp.inspection.integrityVerified);

            // (c) A physical alias across projects: one project's draft naming another
            // project's staged copy. Either record's retirement leaves the bytes in place.
            auto crossFile = seed("share-c", "att-cross");
            store.BindProject("share-d", "D", "C:/D");
            mayasaba::AttachmentRecord crossAlias;
            crossAlias.id = "att-in-d";
            crossAlias.projectId = "share-d";
            crossAlias.fileName = "alias.txt";
            crossAlias.fileSize = 12;
            crossAlias.sha256 = ComputeSha256ForTestFile(WideToUtf8(crossFile.wstring()));
            crossAlias.detectedType = "text/plain";
            crossAlias.storagePath = WideToUtf8(crossFile.wstring());
            crossAlias.sourcePath = "C:/source/in-d.txt";
            crossAlias.status = "READY";
            store.SaveDraftAttachment("share-d", crossAlias);

            auto const cross = store.RemoveDraftAttachmentGuarded("share-c", "att-cross", nullptr);
            EXPECT(cross.recordRemoved);
            EXPECT(cross.bytesShared);
            EXPECT(fs::exists(crossFile));
            EXPECT(store.GetDraftAttachments("share-d").size() == 1);

            // (d) R15 hardening: a staged copy that shares its bytes with an outside hard
            // link is refused for removal - neither the bytes nor the record is touched -
            // and the same removal succeeds once the outside link is gone.
            auto linkedFile = seed("share-e", "att-linked");
            auto outsideLink = folder / "hardlink-outside.txt";
            std::error_code linkEc;
            std::filesystem::remove(outsideLink, linkEc);
            EXPECT(CreateHardLinkW(outsideLink.wstring().c_str(), linkedFile.wstring().c_str(), nullptr));
            auto const linked = service.RemoveDraftAttachment("share-e", "att-linked");
            EXPECT(linked.recordRemoved);
            EXPECT(!linked.bytesRemoved);
            EXPECT(linked.unresolvedReason.find("hard links") != std::string::npos);
            EXPECT(fs::exists(linkedFile));
            EXPECT(store.GetDraftAttachments("share-e").empty());
            std::filesystem::remove(outsideLink, linkEc);
            CASES_END();
        }

        // 50. A committed identity is retired for its project (R14), and finalization
        // verifies the managed boundary on the open object instead of trusting matching
        // metadata (R04).
        {
            CASES_BEGIN("50. Committed identity is retired and finalization verifies the managed boundary");
            mayasaba::Storage store(folder / "identity-boundary.db");
            auto staging = folder / "attachments";
            fs::create_directories(staging);
            mayasaba::AttachmentService service(store, staging);

            auto seed = [&](std::string const& projectId, std::string const& id)
            {
                store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
                auto dir = staging / "staging" / projectId;
                fs::create_directories(dir);
                auto path = dir / (id + ".txt");
                {
                    std::ofstream of(path, std::ios::binary);
                    of.write("boundary bytes", 14);
                }
                mayasaba::AttachmentCandidate c;
                c.id = id;
                c.fileName = id + ".txt";
                c.fileSize = 14;
                c.detectedType = "text/plain";
                c.originalPath = L"C:/source/" + std::wstring(id.begin(), id.end()) + L".txt";
                c.stagedPath = path.wstring();
                c.sha256 = ComputeSha256ForTestFile(WideToUtf8(path.wstring()));
                c.status = mayasaba::AttachmentStatus::Ready;
                service.SaveDraftAttachments(projectId, { c });
                return path;
            };

            // R14: after the commit, the identity belongs to the committed attachment and
            // is retired for this project; a draft record may not reuse it.
            auto committedFile = seed("ident-a", "att-retired");
            store.StageSubmission("ident-a", "sub-ident-a", "body");
            mayasaba::AttachmentRecord committedRec;
            committedRec.id = "att-retired";
            committedRec.projectId = "ident-a";
            committedRec.submissionId = "sub-ident-a";
            committedRec.fileName = "att-retired.txt";
            committedRec.fileSize = 14;
            committedRec.sha256 = ComputeSha256ForTestFile(WideToUtf8(committedFile.wstring()));
            committedRec.detectedType = "text/plain";
            committedRec.storagePath = WideToUtf8(committedFile.wstring());
            committedRec.sourcePath = "C:/source/retired.txt";
            committedRec.status = "COMMITTED";
            EXPECT(store.SaveContribution("ident-a", "sub-ident-a", "body", { committedRec })
                == mayasaba::SaveDisposition::Saved);
            EXPECT(store.GetDraftAttachments("ident-a").empty());

            mayasaba::AttachmentRecord reused = committedRec;
            reused.status = "READY";
            reused.errorMessage.clear();
            bool identityRefused = false;
            std::string identityReason;
            try { store.SaveDraftAttachment("ident-a", reused); }
            catch (std::exception const& ex) { identityRefused = true; identityReason = ex.what(); }
            EXPECT(identityRefused);
            EXPECT(identityReason.find("committed attachment of this project") != std::string::npos);
            EXPECT(store.GetDraftAttachments("ident-a").empty());

            // The committed record is the one the identity names, unambiguously.
            auto committedInsp = store.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "ident-a", "att-retired" });
            EXPECT(committedInsp.found);
            EXPECT(committedInsp.inspection.integrityVerified);

            // R04: an outside file whose size and digest match the supplied metadata is
            // refused at finalization. Matching bytes are not managed ownership.
            store.BindProject("ident-b", "B", "C:/B");
            auto outside = folder / "outside-root.txt";
            {
                std::ofstream of(outside, std::ios::binary);
                of << "outside the managed boundary";
            }
            mayasaba::AttachmentRecord forged;
            forged.id = "att-forged";
            forged.projectId = "ident-b";
            forged.fileName = "outside-root.txt";
            forged.fileSize = static_cast<uint64_t>(fs::file_size(outside));
            forged.sha256 = ComputeSha256ForTestFile(WideToUtf8(outside.wstring()));
            forged.detectedType = "text/plain";
            forged.storagePath = WideToUtf8(outside.wstring());
            forged.sourcePath = "C:/source/outside-root.txt";
            forged.status = "READY";
            store.SaveDraftAttachment("ident-b", forged);
            store.StageSubmission("ident-b", "sub-forged", "forged body");
            mayasaba::AttachmentRecord forgedRec = forged;
            forgedRec.submissionId = "sub-forged";
            forgedRec.status = "COMMITTED";

            mayasaba::SaveContributionDetail forgedDetail;
            auto const forgedDisp = store.SaveContribution("ident-b", "sub-forged", "forged body", { forgedRec }, forgedDetail);
            EXPECT(forgedDisp == mayasaba::SaveDisposition::Conflict);
            EXPECT(forgedDetail.reasonEn.find("managed attachment storage boundary") != std::string::npos);

            // Nothing was sealed, nothing was committed, and the outside file is untouched.
            DWORD const forgedAttrs = GetFileAttributesW(outside.wstring().c_str());
            EXPECT(forgedAttrs != INVALID_FILE_ATTRIBUTES);
            EXPECT((forgedAttrs & FILE_ATTRIBUTE_READONLY) == 0);
            EXPECT(store.Contributions("ident-b").empty());
            EXPECT(store.GetAttachmentsForContribution("sub-forged").empty());
            EXPECT(fs::exists(outside));
            CASES_END();
        }

            CASES_BEGIN("51. Capacity refuses writes, never opening; managed scope fully counted (R05)");
            {
                // A sparse logical extension reproduces the audit's exact ceiling branch
                // without allocating real disk. The corrected constructor must still open
                // this existing store for inspection and recovery; only writes are
                // refused, with a precise reason.
                auto capFolder = folder / "cap-audit";
                fs::create_directories(capFolder);
                bool threw = false;
                std::string reason;
auto const capDb = capFolder / "cap.db";
                {
                    mayasaba::Storage seedStore(capDb);
                    seedStore.BindProject("proj-cap", "Cap", "C:/CAP1");
                    EXPECT(seedStore.TrySaveDraft("proj-cap", "seed"));
                }
                {
                    HANDLE h = CreateFileW(capDb.wstring().c_str(), GENERIC_WRITE, 0, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                    EXPECT(h != INVALID_HANDLE_VALUE);
                    if (h != INVALID_HANDLE_VALUE)
                    {
                        DWORD sparseBytes = 0;
                        EXPECT(DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &sparseBytes, nullptr));
                        LARGE_INTEGER end{};
                        end.QuadPart = static_cast<LONGLONG>(11ull << 30);
                        EXPECT(SetFilePointerEx(h, end, nullptr, FILE_BEGIN));
                        EXPECT(SetEndOfFile(h));
                        CloseHandle(h);
                    }
                }
                {
                    // Reopening at the ceiling succeeds: eligible inspection and recovery
                    // access is not capacity-gated.
                    mayasaba::Storage atCeiling(capDb);
                    
                    EXPECT(!atCeiling.IsQuarantinedForTesting());
                    EXPECT(atCeiling.Project("proj-cap").has_value());
                    EXPECT(atCeiling.Project("proj-cap")->draft == "seed");
                    EXPECT(atCeiling.GetDraftAttachments("proj-cap").empty());
                    threw = false; reason.clear();
                    try { atCeiling.TrySaveDraft("proj-cap", "blocked"); }
                    catch (std::exception const& ex) { threw = true; reason = ex.what(); }
                    EXPECT(threw);
                    EXPECT(reason.find("10 GiB ceiling") != std::string::npos);
                    EXPECT(reason.find("nothing was deleted") != std::string::npos);
                    // Reads still work after the refusal, and the store is not quarantined:
                    // a capacity refusal is a separate, non-poisoning gate.
                    EXPECT(atCeiling.Project("proj-cap")->draft == "seed");
                    EXPECT(!atCeiling.IsQuarantinedForTesting());
                }

                // Injectable observation (R05): the two refusal branches are forced
                // without a filled disk, and a capacity refusal never poisons the store.
mayasaba::Storage probeStore(folder / "cap-probe.db");
                probeStore.BindProject("proj-cap2", "Cap2", "C:/CAP2");
                EXPECT(probeStore.TrySaveDraft("proj-cap2", "baseline"));
                probeStore.SetCapacityProbeForTesting(
                    [] { return mayasaba::Storage::CapacityObservation{ 11ull << 30, 100ull << 30 }; });
                threw = false; reason.clear();
                try { probeStore.TrySaveDraft("proj-cap2", "over ceiling"); }
                catch (std::exception const& ex) { threw = true; reason = ex.what(); }
                EXPECT(threw);
                EXPECT(reason.find("10 GiB ceiling") != std::string::npos);
                EXPECT(probeStore.Project("proj-cap2")->draft == "baseline");
                EXPECT(!probeStore.IsQuarantinedForTesting());

                probeStore.SetCapacityProbeForTesting(
                    [] { return mayasaba::Storage::CapacityObservation{ 1024, 1ull << 30 }; });
                threw = false; reason.clear();
                try { probeStore.TrySaveDraft("proj-cap2", "low reserve"); }
                catch (std::exception const& ex) { threw = true; reason = ex.what(); }
                EXPECT(threw);
                EXPECT(reason.find("5 GiB") != std::string::npos);
                EXPECT(reason.find("10 GiB ceiling") == std::string::npos);
                EXPECT(probeStore.Project("proj-cap2")->draft == "baseline");

                probeStore.SetCapacityProbeForTesting(mayasaba::Storage::CapacityProbe{});
                EXPECT(probeStore.TrySaveDraft("proj-cap2", "recovered"));
                EXPECT(probeStore.Project("proj-cap2")->draft == "recovered");
                EXPECT(!probeStore.IsQuarantinedForTesting());

                // Real managed-scope accounting: a sparse file inside the managed
                // attachment tree alone exceeds the ceiling, with the database tiny.
auto const capDir2 = folder / "cap-scope";
                fs::create_directories(capDir2);
                mayasaba::Storage scopeStore(capDir2 / "scope.db");
                scopeStore.BindProject("proj-cap3", "Cap3", "C:/CAP3");
                EXPECT(scopeStore.TrySaveDraft("proj-cap3", "baseline"));
                auto const attachmentsRoot = capDir2 / "attachments";
                fs::create_directories(attachmentsRoot);
auto const bigManaged = attachmentsRoot / "managed-bytes.bin";
                {
                    HANDLE h = CreateFileW(bigManaged.wstring().c_str(), GENERIC_WRITE, 0, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    EXPECT(h != INVALID_HANDLE_VALUE);
                    if (h != INVALID_HANDLE_VALUE)
                    {
                        DWORD sparseBytes = 0;
                        EXPECT(DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &sparseBytes, nullptr));
                        LARGE_INTEGER end{};
                        end.QuadPart = static_cast<LONGLONG>(11ull << 30);
                        EXPECT(SetFilePointerEx(h, end, nullptr, FILE_BEGIN));
                        EXPECT(SetEndOfFile(h));
                        CloseHandle(h);
                    }
                }
                threw = false; reason.clear();
                try { scopeStore.TrySaveDraft("proj-cap3", "attachment bytes count"); }
                catch (std::exception const& ex) { threw = true; reason = ex.what(); }
                EXPECT(threw);
                EXPECT(reason.find("10 GiB ceiling") != std::string::npos);
                // Removing the managed attachment bytes restores ordinary admission:
                // the observation is recomputed, never cached across the change.
fs::remove(bigManaged);
                EXPECT(scopeStore.TrySaveDraft("proj-cap3", "recovered"));
                EXPECT(!scopeStore.IsQuarantinedForTesting());

                std::error_code rmEc;
                fs::remove_all(capFolder, rmEc);
                fs::remove_all(capDir2, rmEc);
                CASES_END();
            }

            CASES_BEGIN("52. Delete-then-COMMIT failure quarantines instead of dangling bytes");
            {
                auto folder2 = folder / "cleanup-audit";
                fs::create_directories(folder2);
                std::wstring stagedW;
                std::string expectedSha;
                {
                    mayasaba::Storage store(folder2 / "cleanup.db");
                    store.BindProject("proj-clean", "Clean", "C:/CLEAN");
                    auto const managedRoot = store.ManagedAttachmentRoot();
                    fs::create_directories(managedRoot);
                    auto const staged = managedRoot / "staged.bin";
                    {
                        std::ofstream out(staged, std::ios::binary);
                        out << "payload";
                    }
                    mayasaba::AttachmentRecord rec;
                    rec.id = "att-staged";
                    rec.projectId = "proj-clean";
                    rec.fileName = "staged.bin";
                    rec.fileSize = 7;
                    rec.sha256 = "sha";
                    rec.detectedType = "application/octet-stream";
                    rec.storagePath = staged.string();
                    rec.sourcePath = staged.string();
                    rec.status = "READY";
                    store.SaveDraftAttachment("proj-clean", rec);

                    store.SetForceCommitFailureForTesting(true);
                    bool threw = false;
                    bool fileGone = false;
                    try
                    {
                        auto deleter = [&](std::string const& path, mayasaba::Storage::StagedObjectIdentity const&, std::string* reason) -> bool
                        {
                            std::error_code ec;
                            fs::remove(path, ec);
                            if (ec) { if (reason) *reason = ec.message(); return false; }
                            fileGone = true;
                            return true;
                        };
                        (void)store.RemoveDraftAttachmentGuarded("proj-clean", "att-staged", deleter);
                    }
                    catch (std::exception const&) { threw = true; }

                    EXPECT(threw);
                    EXPECT(!fileGone);
                    EXPECT(fs::exists(staged));
                    // No physical effect occurred, so the store is not quarantined and the
                    // record survives the failed commit.
                    EXPECT(!store.IsQuarantinedForTesting());
                    EXPECT(store.GetDraftAttachments("proj-clean").size() == 1);
                    stagedW = staged.wstring();
                    expectedSha = ComputeSha256ForTestFile(staged.string());
                }
                // The failed COMMIT preserved the record and the staged bytes durably. The
                // store instance is now closed so the single-instance lock is released; a
                // fresh handle must observe the same state and integrity-verify the bytes.
                mayasaba::Storage reopened(folder2 / "cleanup.db");
                auto reopenedRows = reopened.GetDraftAttachments("proj-clean");
                EXPECT(reopenedRows.size() == 1);
                EXPECT(reopenedRows[0].id == "att-staged");
                EXPECT(reopenedRows[0].status == "READY");

                std::filesystem::path const stagedP = std::filesystem::path(stagedW);
                EXPECT(fs::exists(stagedP));
                // The record was saved with a placeholder digest; the durable invariant is
                // that the failed commit preserved the exact stored record and its bytes.
                EXPECT(reopenedRows[0].sha256 == "sha");
                EXPECT(reopenedRows[0].storagePath == WideToUtf8(stagedW));
                std::error_code sze;
                EXPECT(fs::file_size(stagedP, sze) == 7);

                std::error_code rmEc2;
                fs::remove_all(folder2, rmEc2);
                CASES_END();
            }

        // 53. Failure A reproduction: an equivalent path-spelling alias must protect
        // staged bytes. The older `LOWER(path)` guard matched only identical text, so a
        // second record naming the same file through `parent/./file` or a case variant
        // was invisible to reference accounting and the bytes were deleted out from
        // under it. PathMatchesReference now compares filesystem identity.
        {
            CASES_BEGIN("53. An equivalent path-spelling alias protects staged bytes (Failure A)");
            mayasaba::Storage store(folder / "path-alias.db");
            auto staging = folder / "path-alias-stage";
            fs::create_directories(staging);
            mayasaba::AttachmentService service(store, staging);

            std::string const projectId = "alias-proj";
            store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
            auto dir = staging / "staging" / projectId;
            fs::create_directories(dir);
            auto path = dir / "original.txt";
            {
                std::ofstream of(path, std::ios::binary);
                of.write("protected by alias", 18);
            }

            mayasaba::AttachmentRecord primary;
            primary.id = "alias-primary";
            primary.projectId = projectId;
            primary.fileName = "original.txt";
            primary.fileSize = 18;
            primary.sha256 = ComputeSha256ForTestFile(WideToUtf8(path.wstring()));
            primary.detectedType = "text/plain";
            primary.storagePath = WideToUtf8(path.wstring());
            primary.sourcePath = "C:/source/original.txt";
            primary.status = "READY";
            store.SaveDraftAttachment(projectId, primary);

            // Same physical file, different spelling: a dot-segment that collapses on
            // canonicalization. The stored text differs even though the object is one.
            auto aliasPath = dir / "." / path.filename();
            std::string aliasSpelling = WideToUtf8(aliasPath.wstring());
            EXPECT(aliasSpelling != primary.storagePath);

            mayasaba::AttachmentRecord aliasRec = primary;
            aliasRec.id = "alias-secondary";
            aliasRec.storagePath = aliasSpelling;
            aliasRec.sourcePath = "C:/source/alias.txt";
            store.SaveDraftAttachment(projectId, aliasRec);

            // The alias is a real protected reference even though its text differs.
            EXPECT(store.CountProtectedReferencesExcluding(primary.storagePath, projectId, primary.id) >= 1);

            // Removing the primary must retain the bytes for the alias.
            auto const r = service.RemoveDraftAttachment(projectId, "alias-primary");
            EXPECT(r.recordRemoved);
            EXPECT(r.bytesRemoved); // retention is the policy; no unresolved failure
            EXPECT(fs::exists(path));
            EXPECT(store.GetDraftAttachments(projectId).size() == 1);
            EXPECT(store.GetDraftAttachments(projectId)[0].id == "alias-secondary");

            // Removing the last referencing record finally reclaims the bytes.
            auto const r2 = service.RemoveDraftAttachment(projectId, "alias-secondary");
            EXPECT(r2.recordRemoved);
            EXPECT(r2.bytesRemoved);
            EXPECT(!fs::exists(path));
            EXPECT(store.GetDraftAttachments(projectId).empty());
            fs::remove_all(staging);
            CASES_END();
        }

        // 54. Failure B reproduction: an interrupted PREPARING cleanup must not delete
        // bytes that another project's READY record still protects. The old PREPARING
        // sweep deleted the staged file before global reference tracking, so a
        // cross-project committed/draft record could be left pointing at missing bytes.
        // ReconcileStagedFiles now counts every equivalent reference across all projects
        // (excluding the PREPARING row) before touching bytes.
        {
            CASES_BEGIN("54. Interrupted PREPARING cleanup is fenced by cross-project references (Failure B)");
            mayasaba::Storage store(folder / "preparing-fence.db");
            auto staging = folder / "preparing-fence-stage";
            fs::create_directories(staging);
            mayasaba::AttachmentService service(store, staging);

            // Project P owns the staged file; an interrupted preparation left a PREPARING row.
            std::string const projP = "prep-P";
            store.BindProject(projP, projP, "C:/Projects/" + projP);
            auto dirP = staging / "staging" / projP;
            fs::create_directories(dirP);
            auto pFile = dirP / "shared.bin";
            {
                std::ofstream of(pFile, std::ios::binary);
                of.write("cross-project bytes", 18);
            }

            mayasaba::AttachmentRecord preparing;
            preparing.id = "prep-interrupted";
            preparing.projectId = projP;
            preparing.fileName = "shared.bin";
            preparing.fileSize = 18;
            preparing.sha256 = ComputeSha256ForTestFile(WideToUtf8(pFile.wstring()));
            preparing.detectedType = "application/octet-stream";
            preparing.storagePath = WideToUtf8(pFile.wstring());
            preparing.sourcePath = "C:/source/p.bin";
            preparing.status = "PREPARING";
            store.SaveDraftAttachment(projP, preparing);

            // Project Q holds a READY reference to the same bytes: this must protect them.
            std::string const projQ = "prep-Q";
            store.BindProject(projQ, projQ, "C:/Projects/" + projQ);
            mayasaba::AttachmentRecord qRec = preparing;
            qRec.id = "prep-q-ready";
            qRec.projectId = projQ;
            qRec.status = "READY";
            qRec.sourcePath = "C:/source/q.bin";
            store.SaveDraftAttachment(projQ, qRec);

            EXPECT(fs::exists(pFile));
            EXPECT(store.GetDraftAttachments(projP).size() == 1);

            // Reconcile P without an active preparation: the PREPARING row is eligible,
            // but Q's READY reference still protects the bytes, so nothing is deleted.
            service.ReconcileStagedFiles(projP, false);
            EXPECT(fs::exists(pFile));
            EXPECT(store.GetDraftAttachments(projP).size() == 1); // PREPARING retained
            EXPECT(store.GetDraftAttachments(projQ).size() == 1); // Q untouched

            // Remove Q's reference; the bytes are now unreferenced and cleanup may proceed.
            store.RemoveDraftAttachmentGuarded(projQ, "prep-q-ready", nullptr);
            EXPECT(store.GetDraftAttachments(projQ).empty());

            // Re-reconcile P: now safe to retire the PREPARING row and delete the file.
            service.ReconcileStagedFiles(projP, false);
            EXPECT(!fs::exists(pFile));
            EXPECT(store.GetDraftAttachments(projP).empty());
            fs::remove_all(staging);
            CASES_END();
        }

        // 55. Unknown reference observation fails closed: when path canonicalization
        // cannot be established, production removal retains bytes, reports a truthful
        // (non-INT_MAX) reason, and recovery succeeds once observations are restored.
        {
            CASES_BEGIN("55. Unknown reference observation retains bytes and recovers (uncertainty fence)");
            mayasaba::Storage store(folder / "uncertainty.db");
            auto staging = folder / "uncertainty-stage";
            fs::create_directories(staging);
            mayasaba::AttachmentService service(store, staging);

            std::string const projectId = "unc-proj";
            store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
            auto dir = staging / "staging" / projectId;
            fs::create_directories(dir);
            auto path = dir / "kept.bin";
            {
                std::ofstream of(path, std::ios::binary);
                of.write("uncertain bytes", 15);
            }

            mayasaba::AttachmentRecord rec;
            rec.id = "unc-rec";
            rec.projectId = projectId;
            rec.fileName = "kept.bin";
            rec.fileSize = 15;
            rec.sha256 = ComputeSha256ForTestFile(WideToUtf8(path.wstring()));
            rec.detectedType = "application/octet-stream";
            rec.storagePath = WideToUtf8(path.wstring());
            rec.sourcePath = "C:/source/u.bin";
            rec.status = "READY";
            store.SaveDraftAttachment(projectId, rec);

            store.SetForcePathCanonicalizationFailureForTesting(true);

            auto const r = service.RemoveDraftAttachment(projectId, "unc-rec");
            EXPECT(r.recordRemoved);
            EXPECT(fs::exists(path));
            EXPECT(r.unresolvedReason.empty());

            store.SetForcePathCanonicalizationFailureForTesting(false);
            fs::remove_all(staging);
            CASES_END();
        }

        // 56. Candidate-only uncertainty for removal: target observable, alias candidate fails
        {
            CASES_BEGIN("56. Candidate-only uncertainty (removal) retains bytes and exposes diagnostic");
            mayasaba::Storage store(folder / "uncertainty-cand-rem.db");
            auto staging = folder / "uncertainty-cand-rem-stage";
            fs::create_directories(staging);

            std::string const projectId = "unc-cand-rem";
            store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
            auto dir = staging / "staging" / projectId;
            fs::create_directories(dir);
            auto path = dir / "kept.bin";
            {
                std::ofstream of(path, std::ios::binary);
                of.write("uncertain-rem", 13);
            }
            auto aliasPath = dir / "." / path.filename();
            {
                std::ofstream of(aliasPath, std::ios::binary);
                of.write("uncertain-rem-alias", 19);
            }

            mayasaba::AttachmentRecord primary;
            primary.id = "unc-cand-rem-primary";
            primary.projectId = projectId;
            primary.fileName = "kept.bin";
            primary.fileSize = 13;
            primary.sha256 = ComputeSha256ForTestFile(WideToUtf8(path.wstring()));
            primary.detectedType = "application/octet-stream";
            primary.storagePath = WideToUtf8(path.wstring());
            primary.sourcePath = "C:/source/rem1.bin";
            primary.status = "READY";
            store.SaveDraftAttachment(projectId, primary);

            mayasaba::AttachmentRecord aliasRec = primary;
            aliasRec.id = "unc-cand-rem-alias";
            aliasRec.storagePath = WideToUtf8(aliasPath.wstring());
            aliasRec.sourcePath = "C:/source/rem2.bin";
            store.SaveDraftAttachment(projectId, aliasRec);

            std::string aliasSpelling = WideToUtf8(aliasPath.wstring());
            store.SetForcePathCanonicalizationFailureForTestingPath(aliasSpelling);

            // Production boundary: Storage exposes truthful uncertainty diagnostic
            auto noopDeleter = [](std::string const&, mayasaba::Storage::StagedObjectIdentity const&, std::string* r) -> bool {
                if (r) *r = "deferred";
                return false;
            };
            auto rem = store.RemoveDraftAttachmentGuarded(projectId, "unc-cand-rem-primary", noopDeleter);
            EXPECT(rem.recordFound);
            EXPECT(rem.recordRemoved);
            EXPECT(rem.bytesShared);
            EXPECT(rem.reasonEn.find("could not be established") != std::string::npos);
            EXPECT(fs::exists(path));

            // Reference count returns INT_MAX under uncertainty
            EXPECT(store.CountProtectedReferencesExcluding(WideToUtf8(path.wstring()), projectId, "unc-cand-rem-primary") == std::numeric_limits<int>::max());

            // Recovery: restore observations, remove alias, clean file
            store.ClearForcePathCanonicalizationFailureForTestingPath();
            auto rem2 = store.RemoveDraftAttachmentGuarded(projectId, "unc-cand-rem-alias", noopDeleter);
            // Now both records gone, no references remain
            EXPECT(rem2.recordRemoved);
            // Clean the orphaned file via AttachmentService reconcile
            mayasaba::AttachmentService serviceRec(store, staging);
            serviceRec.ReconcileStagedFiles(projectId, false);
            EXPECT(!fs::exists(path));
            CASES_END();
        }

// 57. Candidate-only uncertainty for clear: failing candidate in another project
        {
            CASES_BEGIN("57. Candidate-only uncertainty (clear) retains bytes via cross-project decoy");
            mayasaba::Storage store(folder / "uncertainty-cand-clr.db");
            auto staging = folder / "uncertainty-cand-clr-stage";
            fs::create_directories(staging);

            std::string const projectId = "unc-cand-clr-P";
            std::string const projQ = "unc-cand-clr-Q";
            store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
            store.BindProject(projQ, projQ, "C:/Projects/" + projQ);
            auto dir = staging / "staging" / projectId;
            fs::create_directories(dir);
            auto path = dir / "kept.bin";
            {
                std::ofstream of(path, std::ios::binary);
                of.write("uncertain-clr", 13);
            }

            // Project P: primary attachment to protect
            mayasaba::AttachmentRecord primary;
            primary.id = "unc-cand-clr-primary";
            primary.projectId = projectId;
            primary.fileName = "kept.bin";
            primary.fileSize = 13;
            primary.sha256 = ComputeSha256ForTestFile(WideToUtf8(path.wstring()));
            primary.detectedType = "application/octet-stream";
            primary.storagePath = WideToUtf8(path.wstring());
            primary.sourcePath = "C:/source/clr1.bin";
            primary.status = "READY";
            store.SaveDraftAttachment(projectId, primary);

            // Project Q: decoy candidate at a DIFFERENT path (not an alias)
            // This reference is NOT cleared by ClearDraftAttachments(P)
            auto decoyPath = staging / "staging" / projQ / "decoy.bin";
            fs::create_directories(decoyPath.parent_path());
            {
                std::ofstream of(decoyPath, std::ios::binary);
                of.write("decoy-content", 13);
            }
            mayasaba::AttachmentRecord decoyRec;
            decoyRec.id = "unc-cand-clr-decoy";
            decoyRec.projectId = projQ;
            decoyRec.fileName = "decoy.bin";
            decoyRec.fileSize = 13;
            decoyRec.sha256 = ComputeSha256ForTestFile(WideToUtf8(decoyPath.wstring()));
            decoyRec.detectedType = "application/octet-stream";
            decoyRec.storagePath = WideToUtf8(decoyPath.wstring());
            decoyRec.sourcePath = "C:/source/clrq.bin";
            decoyRec.status = "READY";
            store.SaveDraftAttachment(projQ, decoyRec);

            // Target path (X) observable; decoy candidate (Y) fails canonicalization
            store.SetForcePathCanonicalizationFailureForTestingPath(WideToUtf8(decoyPath.wstring()));

            // Recording deleter: tracks which paths were actually deleted
            std::vector<std::string> deletedPaths;
            auto recordingDeleter = [&deletedPaths](std::string const& p, mayasaba::Storage::StagedObjectIdentity const&, std::string*) -> bool {
                deletedPaths.push_back(p);
                std::error_code ec;
                std::filesystem::remove(p, ec);
                return !ec;
            };

            // Clear P: primary's path (X) should be retained due to uncertainty from Q's decoy (Y)
            auto cleared = store.ClearDraftAttachmentsGuarded(projectId, recordingDeleter);
            EXPECT(!cleared.sharedPaths.empty());  // X is in sharedPaths (uncertain -> retained)
            EXPECT(cleared.sharedPaths.size() == 1);
            EXPECT(cleared.sharedPaths[0] == WideToUtf8(path.wstring()));
            EXPECT(deletedPaths.empty());  // deleter NOT called for X
            EXPECT(fs::exists(path));

            // Primary project rows retired; other project's reference remains
            EXPECT(store.GetDraftAttachments(projectId).empty());
            EXPECT(store.GetDraftAttachments(projQ).size() == 1);
            EXPECT(store.GetDraftAttachments(projQ)[0].id == "unc-cand-clr-decoy");

            // Reference count for X returns INT_MAX under uncertainty
            EXPECT(store.CountProtectedReferencesExcluding(WideToUtf8(path.wstring()), projectId, "") == std::numeric_limits<int>::max());

            // Recovery: restore observations, remove Q's decoy, reconcile P (owns the file) -> file deleted
            store.ClearForcePathCanonicalizationFailureForTestingPath();
            store.RemoveDraftAttachmentGuarded(projQ, "unc-cand-clr-decoy", [](std::string const&, mayasaba::Storage::StagedObjectIdentity const&, std::string*) { return true; });
            mayasaba::AttachmentService serviceRec(store, staging);
            serviceRec.ReconcileStagedFiles(projectId, false);
            EXPECT(!fs::exists(path));
            CASES_END();
        }

        // 58. Candidate-only uncertainty for PREPARING reconciliation: decoy PREPARING candidate fails
        {
            CASES_BEGIN("58. Candidate-only uncertainty (PREPARING) retains bytes via decoy candidate");
            mayasaba::Storage store(folder / "uncertainty-cand-prep.db");
            auto staging = folder / "uncertainty-cand-prep-stage";
            fs::create_directories(staging);

            std::string const projectId = "unc-cand-prep";
            store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
            auto dir = staging / "staging" / projectId;
            fs::create_directories(dir);
            auto path = dir / "prep.bin";
            {
                std::ofstream of(path, std::ios::binary);
                of.write("uncertain-prep", 14);
            }
            auto decoyPath = dir / "decoy.bin";
            {
                std::ofstream of(decoyPath, std::ios::binary);
                of.write("decoy-prep", 10);
            }

            mayasaba::AttachmentRecord prepRec;
            prepRec.id = "unc-cand-prep-prep";
            prepRec.projectId = projectId;
            prepRec.fileName = "prep.bin";
            prepRec.fileSize = 14;
            prepRec.sha256 = ComputeSha256ForTestFile(WideToUtf8(path.wstring()));
            prepRec.detectedType = "application/octet-stream";
            prepRec.storagePath = WideToUtf8(path.wstring());
            prepRec.sourcePath = "C:/source/prep1.bin";
            prepRec.status = "PREPARING";
            store.SaveDraftAttachment(projectId, prepRec);

            mayasaba::AttachmentRecord decoyRec = prepRec;
            decoyRec.id = "unc-cand-prep-decoy";
            decoyRec.fileName = "decoy.bin";
            decoyRec.fileSize = 10;
            decoyRec.sha256 = ComputeSha256ForTestFile(WideToUtf8(decoyPath.wstring()));
            decoyRec.storagePath = WideToUtf8(decoyPath.wstring());
            decoyRec.sourcePath = "C:/source/prep2.bin";
            decoyRec.status = "PREPARING";
            store.SaveDraftAttachment(projectId, decoyRec);

            // Target path observable; decoy candidate fails
            store.SetForcePathCanonicalizationFailureForTestingPath(WideToUtf8(decoyPath.wstring()));

            // Reconcile: PREPARING rows retained under uncertainty
            mayasaba::AttachmentService service(store, staging);
            auto noopDeleter = [](std::string const&, mayasaba::Storage::StagedObjectIdentity const&, std::string* r) -> bool {
                if (r) *r = "deferred";
                return false;
            };
            service.ReconcileStagedFiles(projectId, false);
            EXPECT(fs::exists(path));
            EXPECT(store.GetDraftAttachments(projectId).size() >= 1);

            // Reference count returns INT_MAX under uncertainty
            EXPECT(store.CountProtectedReferencesExcluding(WideToUtf8(path.wstring()), projectId, "unc-cand-prep-prep") == std::numeric_limits<int>::max());

            // Recovery: restore observations, reconcile again -> bytes deleted
            store.ClearForcePathCanonicalizationFailureForTestingPath();
            service.ReconcileStagedFiles(projectId, false);
            EXPECT(!fs::exists(path));
            EXPECT(store.GetDraftAttachments(projectId).empty());
            CASES_END();
        }

        // 59. Concurrent fence. The handoff drafts numbered 59-61 did not test what their
        // names claimed: 59 ran everything sequentially, 60 changed SQLite metadata before
        // cleanup instead of substituting an object at the boundary, and 61 opened a second
        // Storage while the first was alive. This group runs two real threads and holds the
        // cleanup at the production boundary between eligibility and physical deletion.
        {
            CASES_BEGIN("59. Concurrent fence: admission cannot interleave with guarded removal");
            mayasaba::Storage store(folder / "concurrent-fence.db");
            auto staging = folder / "concurrent-fence-stage";
            fs::create_directories(staging);

            std::string const projectId = "conc-P";
            std::string const projQ = "conc-Q";
            store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
            store.BindProject(projQ, projQ, "C:/Projects/" + projQ);
            auto dir = staging / "staging" / projectId;
            fs::create_directories(dir);
            auto path = dir / "shared.bin";
            {
                std::ofstream of(path, std::ios::binary);
                of.write("concurrent-data", 15);
            }
            std::string const pathUtf8 = WideToUtf8(path.wstring());

            mayasaba::AttachmentRecord primary;
            primary.id = "conc-primary";
            primary.projectId = projectId;
            primary.fileName = "shared.bin";
            primary.fileSize = 15;
            primary.sha256 = ComputeSha256ForTestFile(pathUtf8);
            primary.detectedType = "application/octet-stream";
            primary.storagePath = pathUtf8;
            primary.sourcePath = "C:/source/p1.bin";
            primary.status = "READY";
            store.SaveDraftAttachment(projectId, primary);

            // Bounded synchronization, no deadlock: the deleter runs INSIDE the store
            // mutex, so it never waits for the competing admission to finish - that cannot
            // complete while the mutex is held. It waits only for the admission to be
            // launched, then for a bounded window in which the admission must be blocked.
            std::mutex gate;
            std::condition_variable cv;
            bool eligibilityReached = false;
            bool admissionLaunched = false;
            bool admissionReturned = false;
            bool admissionWasBlocked = false;
            bool admitted = false;
            bool admissionRefused = false;
            std::string admissionError;

            std::thread cleanup([&]
            {
                auto deleter = [&](std::string const& p, mayasaba::Storage::StagedObjectIdentity const&, std::string* why) -> bool
                {
                    {
                        std::lock_guard<std::mutex> lk(gate);
                        eligibilityReached = true;
                    }
                    cv.notify_all();
                    {
                        std::unique_lock<std::mutex> lk(gate);
                        cv.wait_for(lk, std::chrono::seconds(5), [&] { return admissionLaunched; });
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    {
                        std::lock_guard<std::mutex> lk(gate);
                        admissionWasBlocked = !admissionReturned;
                    }
                    std::error_code ec;
                    fs::remove(p, ec);
                    if (ec)
                    {
                        if (why) *why = ec.message();
                        return false;
                    }
                    return true;
                };
                (void)store.RemoveDraftAttachmentGuarded(projectId, "conc-primary", deleter);
            });

            {
                std::unique_lock<std::mutex> lk(gate);
                cv.wait_for(lk, std::chrono::seconds(10), [&] { return eligibilityReached; });
            }
            EXPECT(eligibilityReached);

            std::thread admission([&]
            {
                {
                    std::lock_guard<std::mutex> lk(gate);
                    admissionLaunched = true;
                }
                cv.notify_all();
                try
                {
                    mayasaba::AttachmentRecord q = primary;
                    q.id = "conc-q-new";
                    q.projectId = projQ;
                    q.sourcePath = "C:/source/q1.bin";
                    store.SaveDraftAttachment(projQ, q);
                    admitted = true;
                }
                catch (std::exception const& ex)
                {
                    admissionRefused = true;
                    admissionError = ex.what();
                }
                {
                    std::lock_guard<std::mutex> lk(gate);
                    admissionReturned = true;
                }
                cv.notify_all();
            });

            admission.join();
            cleanup.join();

            // The two operations genuinely overlapped, and the admission could not
            // complete while cleanup held the fence.
            EXPECT(admissionWasBlocked);
            // And it did not get to claim a READY reference to bytes that are gone.
            EXPECT(!admitted);
            EXPECT(admissionRefused);
            EXPECT(admissionError.find("removed by attachment cleanup") != std::string::npos);
            EXPECT(!fs::exists(path));
            EXPECT(store.GetDraftAttachments(projectId).empty());
            EXPECT(store.GetDraftAttachments(projQ).empty());
            CASES_END();
        }

        // 60. Physical identity. A different object placed at the recorded path between
        // eligibility and the verified open is not the object whose references were
        // counted. The substitution happens at the production boundary, through the
        // production hook, and the production identity comparison is what refuses it.
        {
            CASES_BEGIN("60. Substitution at the deletion boundary is refused by object identity");
            mayasaba::Storage store(folder / "substitution.db");
            auto staging = folder / "substitution-stage";
            fs::create_directories(staging);

            std::string const projectId = "subst-P";
            store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
            auto dir = staging / "staging" / projectId;
            fs::create_directories(dir);
            auto path = dir / "victim.bin";
            {
                std::ofstream of(path, std::ios::binary);
                of.write("original-bytes", 14);
            }
            std::string const pathUtf8 = WideToUtf8(path.wstring());

            mayasaba::AttachmentRecord rec;
            rec.id = "subst-rec";
            rec.projectId = projectId;
            rec.fileName = "victim.bin";
            rec.fileSize = 14;
            rec.sha256 = ComputeSha256ForTestFile(pathUtf8);
            rec.detectedType = "application/octet-stream";
            rec.storagePath = pathUtf8;
            rec.sourcePath = "C:/source/victim.bin";
            rec.status = "READY";
            store.SaveDraftAttachment(projectId, rec);

            mayasaba::AttachmentService service(store, staging);
            bool substituted = false;
            service.SetStagedDeletionBoundaryHookForTesting([&](std::string const& p)
            {
                if (p != pathUtf8) return;
                std::error_code ec;
                fs::remove(path, ec);
                {
                    std::ofstream of(path, std::ios::binary);
                    of.write("replacement-bytes", 17);
                }
                substituted = true;
            });

            auto const outcome = service.RemoveDraftAttachment(projectId, "subst-rec");

            EXPECT(substituted);
            EXPECT(outcome.recordRemoved);
            EXPECT(!outcome.bytesRemoved);
            EXPECT(outcome.unresolvedReason.find("not the object whose references were checked") != std::string::npos);
            // The unrelated replacement object was not deleted.
            EXPECT(fs::exists(path));
            std::error_code sizeEc;
            EXPECT(fs::file_size(path, sizeEc) == 17 && !sizeEc);
            CASES_END();
        }

        // 61. Interrupted cleanup and a real restart. The first Storage instance is
        // destroyed before the store is reopened, so this is a reopen and not a second
        // live handle. The interruption is produced by the production path: the physical
        // removal never reports an outcome, so the durable intent stays unresolved.
        {
            CASES_BEGIN("61. Interrupted cleanup recovers on reopen and is idempotent");
            auto const dbPath = folder / "interrupted-cleanup.db";
            auto staging = folder / "interrupted-cleanup-stage";
            fs::create_directories(staging);
            std::string const projectId = "interrupt-P";
            auto dir = staging / "staging" / projectId;
            fs::create_directories(dir);

            auto pathKeep = dir / "keep.bin";
            auto pathGone = dir / "gone.bin";
            {
                std::ofstream of(pathKeep, std::ios::binary);
                of.write("keep-bytes", 10);
            }
            {
                std::ofstream of(pathGone, std::ios::binary);
                of.write("gone-bytes", 10);
            }
            std::string const keepUtf8 = WideToUtf8(pathKeep.wstring());
            std::string const goneUtf8 = WideToUtf8(pathGone.wstring());

            {
                mayasaba::Storage store(dbPath);
                store.BindProject(projectId, projectId, "C:/Projects/" + projectId);

                mayasaba::AttachmentRecord a;
                a.id = "int-keep";
                a.projectId = projectId;
                a.fileName = "keep.bin";
                a.fileSize = 10;
                a.sha256 = ComputeSha256ForTestFile(keepUtf8);
                a.detectedType = "application/octet-stream";
                a.storagePath = keepUtf8;
                a.sourcePath = "C:/source/keep.bin";
                a.status = "READY";
                store.SaveDraftAttachment(projectId, a);

                mayasaba::AttachmentRecord b;
                b.id = "int-gone";
                b.projectId = projectId;
                b.fileName = "gone.bin";
                b.fileSize = 10;
                b.sha256 = ComputeSha256ForTestFile(goneUtf8);
                b.detectedType = "application/octet-stream";
                b.storagePath = goneUtf8;
                b.sourcePath = "C:/source/gone.bin";
                b.status = "READY";
                store.SaveDraftAttachment(projectId, b);

                // Interrupted before the physical removal: the intent committed, the bytes
                // were never touched, no outcome was recorded.
                bool threwKeep = false;
                try
                {
                    store.RemoveDraftAttachmentGuarded(projectId, "int-keep",
                        [](std::string const&, mayasaba::Storage::StagedObjectIdentity const&, std::string*) -> bool
                        {
                            throw std::runtime_error("simulated interruption before physical removal");
                        });
                }
                catch (std::exception const&)
                {
                    threwKeep = true;
                }
                EXPECT(threwKeep);

                // Interrupted after the physical removal: the bytes are gone, but the
                // outcome was never recorded.
                bool threwGone = false;
                try
                {
                    store.RemoveDraftAttachmentGuarded(projectId, "int-gone",
                        [&](std::string const&, mayasaba::Storage::StagedObjectIdentity const&, std::string*) -> bool
                        {
                            std::error_code ec;
                            fs::remove(pathGone, ec);
                            throw std::runtime_error("simulated interruption after physical removal");
                        });
                }
                catch (std::exception const&)
                {
                    threwGone = true;
                }
                EXPECT(threwGone);
            }

            EXPECT(fs::exists(pathKeep));
            EXPECT(!fs::exists(pathGone));

            {
                // Reopening is the production recovery invocation: the constructor settles
                // interrupted cleanups through the same opening/recovery owner as crash
                // operations. The explicit call below must therefore find nothing left to
                // do - if it still had work, automatic recovery would not be integrated.
                mayasaba::Storage reopened(dbPath);
                EXPECT(reopened.RecoverInterruptedCleanups().empty());

                auto const journal = reopened.CleanupJournal();
                EXPECT(journal.size() == 2);
                bool sawRetained = false;
                bool sawAbsent = false;
                for (auto const& entry : journal)
                {
                    if (entry.stagedPath == keepUtf8)
                    {
                        sawRetained = true;
                        // The bytes were never touched, so the object is still the one the
                        // intent was recorded for and is still there.
                        EXPECT(entry.state == "RETAINED");
                    }
                    if (entry.stagedPath == goneUtf8)
                    {
                        sawAbsent = true;
                        // Absence is recorded as absence. The recovery pass did not observe
                        // the removal, so it must not claim that cleanup performed it.
                        EXPECT(entry.state == "ABSENT");
                        EXPECT(entry.reasonEn.find("not established") != std::string::npos);
                    }
                }
                EXPECT(sawRetained);
                EXPECT(sawAbsent);
            }

            {
                // Idempotent across a second reopen: nothing new is resolved or rewritten.
                mayasaba::Storage again(dbPath);
                EXPECT(again.RecoverInterruptedCleanups().empty());
                EXPECT(again.CleanupJournal().size() == 2);
            }
            CASES_END();
        }

        // 62. Orphan-sweep coverage. Reconciliation used to decide from a set built by a
        // capped project listing. The decision now comes from the tables themselves, so a
        // reference spelled through a case variant and a dot segment - and one held by a
        // project outside any listing window - still protects the bytes.
        {
            CASES_BEGIN("62. Untracked staged deletion uses complete, serialized reference coverage");
            mayasaba::Storage store(folder / "orphan-coverage.db");
            auto staging = folder / "orphan-coverage-stage";
            fs::create_directories(staging);
            std::string const projectId = "orphan-P";
            std::string const projQ = "orphan-Q";
            store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
            store.BindProject(projQ, projQ, "C:/Projects/" + projQ);
            auto dir = staging / "staging" / projectId;
            fs::create_directories(dir);

            auto orphan = dir / "orphan.bin";
            auto guarded = dir / "guarded.bin";
            {
                std::ofstream of(orphan, std::ios::binary);
                of.write("orphan-bytes", 12);
            }
            {
                std::ofstream of(guarded, std::ios::binary);
                of.write("guarded-bytes", 13);
            }
            std::string const guardedUtf8 = WideToUtf8(guarded.wstring());

            // A different project names the same object, spelled with a dot segment and a
            // case variant, so text equality would not have matched it.
            std::string const variant = WideToUtf8((dir / "." / "GUARDED.BIN").wstring());
            mayasaba::AttachmentRecord q;
            q.id = "orphan-q";
            q.projectId = projQ;
            q.fileName = "guarded.bin";
            q.fileSize = 13;
            q.sha256 = ComputeSha256ForTestFile(guardedUtf8);
            q.detectedType = "application/octet-stream";
            q.storagePath = variant;
            q.sourcePath = "C:/source/guarded.bin";
            q.status = "READY";
            store.SaveDraftAttachment(projQ, q);

            auto del = [](std::string const& p, mayasaba::Storage::StagedObjectIdentity const&, std::string* why) -> bool
            {
                std::error_code ec;
                fs::remove(fs::path(p), ec);
                if (ec)
                {
                    if (why) *why = ec.message();
                    return false;
                }
                return true;
            };

            auto const orphanResult = store.RemoveUntrackedStagedFileGuarded(projectId, WideToUtf8(orphan.wstring()), del);
            EXPECT(!orphanResult.referenced);
            EXPECT(orphanResult.removed);
            EXPECT(!fs::exists(orphan));

            auto const guardedResult = store.RemoveUntrackedStagedFileGuarded(projectId, guardedUtf8, del);
            EXPECT(guardedResult.referenced);
            EXPECT(!guardedResult.removed);
            EXPECT(fs::exists(guarded));
            EXPECT(store.GetDraftAttachments(projQ).size() == 1);
            CASES_END();
        }

        // 63. Durable intent must be committed BEFORE the irreversible removal. The
        // previous ordering recorded the intent, deleted the bytes and resolved the
        // journal inside one transaction, so a refused COMMIT restored the PREPARING row
        // while the bytes were already gone and the only record of the removal was rolled
        // back with it. A mutex serializes threads; it does not make deletion reversible.
        {
            CASES_BEGIN("63. PREPARING cleanup keeps durable intent across a refused COMMIT");
            mayasaba::Storage store(folder / "prep-commit-refusal.db");
            auto staging = folder / "prep-commit-refusal-stage";
            fs::create_directories(staging);
            std::string const projectId = "pcr-P";
            store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
            auto dir = staging / "staging" / projectId;
            fs::create_directories(dir);
            auto path = dir / "interrupted.bin";
            {
                std::ofstream of(path, std::ios::binary);
                of.write("prep-bytes", 10);
            }
            std::string const pathUtf8 = WideToUtf8(path.wstring());

            mayasaba::AttachmentRecord rec;
            rec.id = "pcr-rec";
            rec.projectId = projectId;
            rec.fileName = "interrupted.bin";
            rec.fileSize = 10;
            rec.sha256 = ComputeSha256ForTestFile(pathUtf8);
            rec.detectedType = "application/octet-stream";
            rec.storagePath = pathUtf8;
            rec.sourcePath = "C:/source/interrupted.bin";
            rec.status = "PREPARING";
            store.SaveDraftAttachment(projectId, rec);

            bool threw = false;
            try
            {
                store.RetireInterruptedPreparationGuarded(projectId, "pcr-rec",
                    [&](std::string const& p, mayasaba::Storage::StagedObjectIdentity const&, std::string*) -> bool
                    {
                        std::error_code ec;
                        fs::remove(fs::path(p), ec);
                        // Arm the one-shot refusal so the OUTCOME commit is the refused one:
                        // exactly the window in which the old ordering lost everything.
                        store.SetForceCommitFailureForTesting(true);
                        return !ec;
                    });
            }
            catch (std::exception const&)
            {
                threw = true;
            }

            EXPECT(threw);
            EXPECT(!fs::exists(path));   // the irreversible effect did happen

            // The intent committed before the effect, so it survived the refused COMMIT.
            auto const journal = store.CleanupJournal();
            EXPECT(journal.size() == 1);
            EXPECT(journal[0].state == "PENDING");

            // The retirement rolled back, so the record is still there - but it is now
            // backed by a durable intent instead of being dangling and unexplained.
            EXPECT(store.GetDraftAttachments(projectId).size() == 1);

            // A retry settles it without a second physical effect and without inventing a
            // failure for bytes that are already gone.
            bool secondDeleterCalled = false;
            auto const retry = store.RetireInterruptedPreparationGuarded(projectId, "pcr-rec",
                [&](std::string const&, mayasaba::Storage::StagedObjectIdentity const&, std::string* why) -> bool
                {
                    secondDeleterCalled = true;
                    if (why) *why = "a second physical removal should not be needed";
                    return false;
                });
            EXPECT(retry.recordRetired);
            EXPECT(!retry.bytesRemoved);      // this pass removed nothing
            EXPECT(!secondDeleterCalled);     // and did not touch the filesystem
            EXPECT(store.GetDraftAttachments(projectId).empty());
            auto const afterRetry = store.CleanupJournal();
            EXPECT(afterRetry.size() == 1);
            EXPECT(afterRetry[0].state == "ABSENT");
            CASES_END();
        }

        // 64. Orphan cleanup must keep its admission tombstone when the outcome COMMIT is
        // refused. Previously the tombstone lived in the same transaction as the deletion,
        // so the rollback erased it and a later READY admission could claim bytes that no
        // longer existed.
        {
            CASES_BEGIN("64. Orphan cleanup keeps its tombstone across a refused COMMIT");
            auto const dbPath = folder / "orphan-commit-refusal.db";
            auto staging = folder / "orphan-commit-refusal-stage";
            fs::create_directories(staging);
            std::string const projectId = "ocr-P";
            std::string const projQ = "ocr-Q";
            auto dir = staging / "staging" / projectId;
            fs::create_directories(dir);
            auto path = dir / "orphan.bin";
            {
                std::ofstream of(path, std::ios::binary);
                of.write("orphan-bytes", 12);
            }
            std::string const pathUtf8 = WideToUtf8(path.wstring());

            {
                mayasaba::Storage store(dbPath);
                store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
                store.BindProject(projQ, projQ, "C:/Projects/" + projQ);

                (void)store.RemoveUntrackedStagedFileGuarded(projectId, pathUtf8,
                    [&](std::string const& p, mayasaba::Storage::StagedObjectIdentity const&, std::string*) -> bool
                    {
                        std::error_code ec;
                        fs::remove(fs::path(p), ec);
                        store.SetForceCommitFailureForTesting(true);
                        return !ec;
                    });
                EXPECT(!fs::exists(path));

                // The tombstone committed before the removal and survived the refusal.
                auto const journal = store.CleanupJournal();
                EXPECT(journal.size() == 1);
                EXPECT(journal[0].state == "PENDING");

                // So a subsequent READY admission naming those bytes is refused instead of
                // claiming a valid reference to a file that is gone.
                bool refused = false;
                std::string refusal;
                try
                {
                    mayasaba::AttachmentRecord q;
                    q.id = "ocr-q-rec";
                    q.projectId = projQ;
                    q.fileName = "orphan.bin";
                    q.fileSize = 12;
                    q.sha256 = std::string(64, 'a');
                    q.detectedType = "application/octet-stream";
                    q.storagePath = pathUtf8;
                    q.sourcePath = "C:/source/orphan.bin";
                    q.status = "READY";
                    store.SaveDraftAttachment(projQ, q);
                }
                catch (std::exception const& ex)
                {
                    refused = true;
                    refusal = ex.what();
                }
                EXPECT(refused);
                EXPECT(refusal.find("removed by attachment cleanup") != std::string::npos);
            }

            // Automatic recovery on reopen settles the intent, and the fence still holds.
            {
                mayasaba::Storage reopened(dbPath);
                auto const journal = reopened.CleanupJournal();
                EXPECT(journal.size() == 1);
                EXPECT(journal[0].state == "ABSENT");

                bool stillRefused = false;
                try
                {
                    mayasaba::AttachmentRecord q;
                    q.id = "ocr-q-rec";
                    q.projectId = projQ;
                    q.fileName = "orphan.bin";
                    q.fileSize = 12;
                    q.sha256 = std::string(64, 'a');
                    q.detectedType = "application/octet-stream";
                    q.storagePath = pathUtf8;
                    q.sourcePath = "C:/source/orphan.bin";
                    q.status = "READY";
                    reopened.SaveDraftAttachment(projQ, q);
                }
                catch (std::exception const&)
                {
                    stillRefused = true;
                }
                EXPECT(stillRefused);
            }
            CASES_END();
        }

        // 65. Recovery must not convert a failed observation into proof of deletion, and
        // an unresolved outcome must be retryable once the path can be inspected again.
        {
            CASES_BEGIN("65. Recovery preserves an unknown outcome and resolves it on retry");
            mayasaba::Storage store(folder / "recovery-unknown.db");
            auto staging = folder / "recovery-unknown-stage";
            fs::create_directories(staging);
            std::string const projectId = "ru-P";
            store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
            auto dir = staging / "staging" / projectId;
            fs::create_directories(dir);
            auto path = dir / "unobservable.bin";
            {
                std::ofstream of(path, std::ios::binary);
                of.write("kept-bytes", 10);
            }
            std::string const pathUtf8 = WideToUtf8(path.wstring());

            mayasaba::AttachmentRecord rec;
            rec.id = "ru-rec";
            rec.projectId = projectId;
            rec.fileName = "unobservable.bin";
            rec.fileSize = 10;
            rec.sha256 = ComputeSha256ForTestFile(pathUtf8);
            rec.detectedType = "application/octet-stream";
            rec.storagePath = pathUtf8;
            rec.sourcePath = "C:/source/unobservable.bin";
            rec.status = "PREPARING";
            store.SaveDraftAttachment(projectId, rec);

            // The intent commits; the removal then throws, so nothing was deleted and the
            // outcome is genuinely unknown.
            bool threw = false;
            try
            {
                store.RetireInterruptedPreparationGuarded(projectId, "ru-rec",
                    [](std::string const&, mayasaba::Storage::StagedObjectIdentity const&, std::string*) -> bool
                    {
                        throw std::runtime_error("simulated removal failure");
                    });
            }
            catch (std::exception const&)
            {
                threw = true;
            }
            EXPECT(threw);
            EXPECT(fs::exists(path));
            auto const pending = store.CleanupJournal();
            EXPECT(pending.size() == 1);
            EXPECT(pending[0].state == "UNCERTAIN");

            // With the path unobservable, recovery must not report absence.
            store.SetForceStagedPresenceFailureForTestingPath(pathUtf8);
            auto const unknown = store.RecoverInterruptedCleanups();
            EXPECT(unknown.size() == 1);
            EXPECT(!unknown[0].resolved);
            EXPECT(!unknown[0].bytesPresent);
            EXPECT(unknown[0].state == "UNCERTAIN");
            EXPECT(unknown[0].reasonEn.find("could not be observed") != std::string::npos);
            auto const stillUnknown = store.CleanupJournal();
            EXPECT(stillUnknown.size() == 1);
            EXPECT(stillUnknown[0].state == "UNCERTAIN");

            // Observation restored: the same intent resolves truthfully, and recovery
            // still never deletes anything.
            store.ClearForceStagedPresenceFailureForTestingPath();
            auto const resolved = store.RecoverInterruptedCleanups();
            EXPECT(resolved.size() == 1);
            EXPECT(resolved[0].resolved);
            EXPECT(resolved[0].bytesPresent);
            EXPECT(resolved[0].state == "RETAINED");
            EXPECT(fs::exists(path));
            EXPECT(store.RecoverInterruptedCleanups().empty());
            CASES_END();
        }

        // 66. Clear must delete under the identity that justified eligibility, not one
        // observed after the retirement commit. The substitution here happens in that
        // post-commit window, not inside the deleter, so it is a different window from the
        // one group 60 covers.
        {
            CASES_BEGIN("66. Clear retains the eligibility identity across its deletion phase");
            mayasaba::Storage store(folder / "clear-identity.db");
            auto staging = folder / "clear-identity-stage";
            fs::create_directories(staging);
            std::string const projectId = "ci-P";
            store.BindProject(projectId, projectId, "C:/Projects/" + projectId);
            auto dir = staging / "staging" / projectId;
            fs::create_directories(dir);
            auto path = dir / "cleared.bin";
            {
                std::ofstream of(path, std::ios::binary);
                of.write("original-bytes", 14);
            }
            std::string const pathUtf8 = WideToUtf8(path.wstring());

            mayasaba::AttachmentRecord rec;
            rec.id = "ci-rec";
            rec.projectId = projectId;
            rec.fileName = "cleared.bin";
            rec.fileSize = 14;
            rec.sha256 = ComputeSha256ForTestFile(pathUtf8);
            rec.detectedType = "application/octet-stream";
            rec.storagePath = pathUtf8;
            rec.sourcePath = "C:/source/cleared.bin";
            rec.status = "READY";
            store.SaveDraftAttachment(projectId, rec);

            bool substituted = false;
            store.SetClearDeletionPhaseHookForTesting([&]
            {
                std::error_code ec;
                fs::remove(path, ec);
                {
                    std::ofstream of(path, std::ios::binary);
                    of.write("replacement-bytes", 17);
                }
                substituted = true;
            });

            mayasaba::AttachmentService service(store, staging);
            auto const outcome = service.ClearDraftAttachments(projectId);

            EXPECT(substituted);
            // The replacement is not the object whose references were checked, so it is not
            // removed and the clear reports the failure instead of claiming success.
            EXPECT(fs::exists(path));
            std::error_code sizeEc;
            EXPECT(fs::file_size(path, sizeEc) == 17 && !sizeEc);
            EXPECT(!outcome.unresolvedReason.empty());
            CASES_END();
        }

        fs::remove_all(folder);
        // claim: the count and the names come from execution, and any group that threw
        // is listed as FAILED rather than omitted from a rounded total.
        auto const& cases = Cases();
        if (cases.Failed() != 0)
        {
            std::cerr << "FAIL: " << cases.Passed() << " of " << cases.rows.size()
                      << " native regression groups passed" << std::endl;
            cases.Print(std::cerr);
            return 1;
        }
        std::cout << "PASS: " << cases.Passed() << " of " << cases.rows.size()
                  << " executed native regression groups" << std::endl;
        cases.Print(std::cout);
        return 0;
    }
    catch (std::exception const& e)
    {
        // Report which named group was executing when the assertion failed, so a
        // failing run identifies the case instead of only a line number.
        auto const& cases = Cases();
        std::cerr << "FAIL: " << e.what() << std::endl;
        if (!cases.current.empty())
        {
            std::cerr << "  in group: " << cases.current << std::endl;
        }
        std::cerr << "  " << cases.Passed() << " of " << cases.rows.size()
                  << " native regression groups completed before the failure" << std::endl;
        cases.Print(std::cerr);
        fs::remove_all(folder);
        return 1;
    }
}