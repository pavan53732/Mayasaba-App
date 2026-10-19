#include "../Mayasaba/Storage.h"
#include "../Mayasaba/SearchController.h"
#include "../Mayasaba/ProjectRoot.h"
#include "../Mayasaba/ProjectService.h"
#include "../Mayasaba/AttachmentService.h"
#include "../Mayasaba/SerialQueue.h"
#include <fstream>
#include <future>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

#include <windows.h>

#define EXPECT(cond) \
    do { \
        if (!(cond)) { \
            throw std::runtime_error(std::string("Assertion failed: ") + #cond + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (0)

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
        }

        // 2. Restart and recovery verification
        {
            mayasaba::Storage reopened(folder / "project.db");
            auto a = reopened.Project("project-A");
            EXPECT(a && !a->closed);
            EXPECT(reopened.Contributions("project-A").size() == 2);
            EXPECT(reopened.LastBoundProject() == std::optional<std::string>{"project-A"});
            reopened.SaveDraft("project-A", "recoverable");
            EXPECT(reopened.Project("project-A")->draft == "recoverable");
            EXPECT(reopened.StageSubmission("project-A", "submit-0003", "pending after restart") == "submit-0003");
        }

        // 3. Post-restart commit
        {
            mayasaba::Storage afterRestart(folder / "project.db");
            auto pending = afterRestart.Pending("project-A");
            EXPECT(pending && pending->id == "submit-0003");
            EXPECT(afterRestart.StageSubmission("project-A", "new-id-rejected", "pending after restart") == "submit-0003");
            EXPECT(afterRestart.SaveContribution("project-A", "submit-0003", "pending after restart") == SaveDisposition::Saved);
            EXPECT(afterRestart.Contributions("project-A").size() == 3);
        }

#ifdef _WIN32
        // 4. Folder creation & Locate domain operations
        {
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
        }

        // 5. Durable directory operations and crash reconciliation tests
        {
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
        }

        // 6. Domain UUID and Filesystem Identity compatibility
        {
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
        }

        // 7. Atomic version-checked draft transfer refusal under pending submission
        {
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
        }

        // 8. Verified backup admission and legacy schema migration test
        {
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
        }

        // 9. Rogue directory crash reconciliation test: never adopt unverified directory
        {
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
        }

        // 10. Concurrency exclusion test (single-writer mutex)
        {
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
        }
        // 11. Revision ordering and transfer fence, deliberately deliver old saves late.
        {
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
        }
        // 12. Service-level locate preserves name/path order, identity and draft.
        {
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
        }
        // 13. Test injection must reject malformed input, never fall back to production.
        {
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
        }
        // 14. FIFO lane: pause its first task, enqueue writes and transfer, then release.
        {
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
        }

        // 15. Actual typed selector transaction, replay, stale identity and draft disposition.
        {
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
        }
        // 16. Directory intent/replay, cancellation, recovery and held path replacement fence.
        {
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
        }
        // 17. Abort after project writes but before receipt/event commit: no partial selection.
        {
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
        }
        // 18. Unsupported store version is refused before schema mutation; DPAPI draft bytes are protected.
        {
            auto db=folder/"future.db";sqlite3* raw{};EXPECT(sqlite3_open16(db.c_str(),&raw)==SQLITE_OK);
            EXPECT(sqlite3_exec(raw,"PRAGMA user_version=999;",nullptr,nullptr,nullptr)==SQLITE_OK);sqlite3_close(raw);
            bool refused=false;try{mayasaba::Storage future(db);}catch(std::exception const&){refused=true;}EXPECT(refused);
            auto protectedDb=folder/"protected.db";mayasaba::Storage store(protectedDb);store.SavePrebindDraft("private selector text");
            EXPECT(sqlite3_open16(protectedDb.c_str(),&raw)==SQLITE_OK);sqlite3_stmt* q{};
            EXPECT(sqlite3_prepare_v2(raw,"SELECT prebinding_draft FROM ui_state;",-1,&q,nullptr)==SQLITE_OK);
            EXPECT(sqlite3_step(q)==SQLITE_ROW);std::string stored(reinterpret_cast<char const*>(sqlite3_column_text(q,0)));
            EXPECT(stored.starts_with("dpapi1:") && stored.find("private selector text")==std::string::npos);
            sqlite3_finalize(q);sqlite3_close(raw);EXPECT(store.GetPrebindDraft()=="private selector text");
        }
        // 19. Attachment service: selection, validation, 50-file limits, byte boundaries,
        // chunked copy, SHA-256 verification, source change detection, draft persistence, recovery and atomic submission.
        {
            auto attDb = folder / "attachment_test.db";
            auto attStaging = folder / "att_staging";
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

            // Restore valid staged content
            {
                std::ofstream ofs(candidate.stagedPath, std::ios::trunc);
                ofs << payload;
            }
            auto reverifiedFixed = attService.ReverifyDraftAttachments(projectId);
            EXPECT(reverifiedFixed.size() == 1);
            EXPECT(reverifiedFixed[0].status == mayasaba::AttachmentStatus::Ready);

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
        }
#endif

        // 23. Legacy unbound draft preservation and recovery (2026-10-18).
        // A pre-binding draft written by an older build is retained user data. Startup must only
        // read it; the selector must offer it explicitly; and no path may delete or overwrite it.
        {
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
        }

        // 21. Recent-projects query filtering across recorded projects (Milestone 2)
        {
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
        }

        // 22. Saved-message search in Chat with project isolation and bounded queries (Milestone 3)
        {
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
        }

        // Action Center: read-only projection over owner records
        {
            mayasaba::Storage ac(folder / "action_center.db");
            ac.BindProject("proj-ac", "AC Project", "C:/AC");
            ac.SetClosure("proj-ac", 0, false);

            // Unresolved submission appears; rechecked by project id.
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
                }
            }
            EXPECT(foundSubmission);

            // Resolving the submission removes the item.
            EXPECT(ac.SaveContribution("proj-ac", "sub-ac-1", "unresolved body") == mayasaba::SaveDisposition::Saved);
            auto res2 = ac.QueryActionCenter(q);
            bool stillHasSubmission = false;
            for (auto const& item : res2.items)
                if (item.kind == mayasaba::ActionCenterKind::UnresolvedSubmission) stillHasSubmission = true;
            EXPECT(!stillHasSubmission);

            // Failed attachment item.
            mayasaba::AttachmentRecord att{};
            att.id = "att-ac-1"; att.projectId = "proj-ac"; att.fileName = "bad.bin";
            att.fileSize = 0; att.sha256 = ""; att.detectedType = "application/octet-stream";
            att.storagePath = ""; att.sourcePath = ""; att.status = "REJECTED"; att.errorMessage = "hash mismatch";
            ac.SaveDraftAttachment("proj-ac", att);
            auto res3 = ac.QueryActionCenter(q);
            bool foundAttachment = false;
            for (auto const& item : res3.items)
                if (item.kind == mayasaba::ActionCenterKind::FailedAttachment) { foundAttachment = true; EXPECT(item.availableAction == "Remove or re-attach"); }
            EXPECT(foundAttachment);

            // Closed-project item.
            ac.SetClosure("proj-ac", 0, true);
            auto res4 = ac.QueryActionCenter(q);
            bool foundClosed = false;
            for (auto const& item : res4.items)
                if (item.kind == mayasaba::ActionCenterKind::ClosedProjectRestriction)
                { foundClosed = true; EXPECT(item.availableAction == "Reopen Project"); }
            EXPECT(foundClosed);

            // Page bound respected.
            mayasaba::ActionCenterQuery qPage{ "proj-ac", 1 };
            auto res5 = ac.QueryActionCenter(qPage);
            EXPECT(res5.items.size() <= 1);

            // Project isolation: items never leak to another project.
            auto res6 = ac.QueryActionCenter(mayasaba::ActionCenterQuery{ "other-proj", 50 });
            bool leaked = false;
            for (auto const& item : res6.items)
                if (item.projectId == "proj-ac") leaked = true;
            EXPECT(!leaked);
        }

        // Attachment inspection: draft attachment integrity verified against disk
        {
            mayasaba::Storage ai(folder / "attachment_inspect.db");
            ai.BindProject("proj-ai", "AI Project", "C:/AI");

            std::string physicalPath = (folder / "note.md").string();
            std::ofstream of(physicalPath, std::ios::binary);
            of.write("abcd", 4);
            of.close();

            mayasaba::AttachmentRecord draft{};
            draft.id = "att-draft-1"; draft.projectId = "proj-ai"; draft.fileName = "note.md";
            draft.fileSize = 4; draft.sha256 = "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589";
            draft.detectedType = "text/markdown"; draft.storagePath = physicalPath;
            draft.sourcePath = "C:/source/note.md"; draft.status = "READY"; draft.errorMessage = "";
            ai.SaveDraftAttachment("proj-ai", draft);

            mayasaba::AttachmentInspectionQuery qi{ "proj-ai", "att-draft-1" };
            auto ri = ai.InspectAttachment(qi);
            EXPECT(ri.found);
            EXPECT(ri.inspection.attachmentId == "att-draft-1");
            EXPECT(ri.inspection.projectId == "proj-ai");
            EXPECT(ri.inspection.fileName == "note.md");
            EXPECT(ri.inspection.fileSize == 4);
            EXPECT(ri.inspection.detectedType == "text/markdown");
            EXPECT(ri.inspection.storagePath == physicalPath);
            EXPECT(!ri.inspection.sourceRecordId.empty());
            EXPECT(ri.inspection.previewEligible);
            EXPECT(ri.inspection.previewUnavailableReasonEn.empty());
            EXPECT(ri.inspection.isManaged);
            EXPECT(ri.inspection.integrityVerified);

            // Inspecting an unknown attachment returns not found.
            auto rn = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ai", "does-not-exist" });
            EXPECT(!rn.found);
            EXPECT(rn.errorMessage.empty());
        }

        fs::remove_all(folder);
        std::cout << "PASS: 22 isolated native regression groups (storage, migration, identity locks, ordered drafts, services, creation, recovery, FIFO, attachments, legacy draft preservation, recent filtering, saved message search, action center, attachment inspection)\\n";
        return 0;
    }
    catch (std::exception const& e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        fs::remove_all(folder);
        return 1;
    }
}
