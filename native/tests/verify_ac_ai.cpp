#include "../Mayasaba/Storage.h"
#include "../Mayasaba/AttachmentService.h"
#include "../Mayasaba/AttachmentIngestionCoordinator.h"
#include "../Mayasaba/ActionCenterPresenter.h"
#include <chrono>
#include <thread>
#include <mutex>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <set>
#include <unordered_set>
#include <sstream>
#include <vector>
#include <iomanip>
#include <windows.h>
#include <bcrypt.h>

#define EXPECT(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " at " __FILE__ ":" << __LINE__ << "\n"; \
            return 1; \
        } \
    } while (0)

// ---------------------------------------------------------------------------
// Executed-case reporting.
//
// The previous summary printed a fixed "12/12" regardless of what ran. A case that
// silently stopped executing, or an older binary left in place, could still print
// the same banner. Each named case is now recorded when it starts and marked only
// when it finishes, so the summary is derived from execution and a failure names
// the case. This changes reporting only; no assertion is weakened or removed.
// ---------------------------------------------------------------------------
namespace
{
    struct ExecutedCases
    {
        struct Row { std::string name; bool finished{false}; };
        std::vector<Row> rows;
        std::string current;

        void Begin(std::string name)
        {
            current = std::move(name);
            rows.push_back(Row{current, false});
        }
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

int main()
{
    namespace fs = std::filesystem;
    auto folder = fs::temp_directory_path() / ("mayasaba-verify-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(folder);

    // Action Center projection
    {
        CASES_BEGIN("Test 1: Action Center unresolved submission projection");
        mayasaba::Storage ac(folder / "action_center.db");
        ac.BindProject("proj-ac", "AC Project", "C:/AC");
        ac.SetClosure("proj-ac", 0, false);

        // 1. Unresolved submission appears; rechecked by project id.
        ac.StageSubmission("proj-ac", "sub-ac-1", "unresolved body");
        mayasaba::ActionCenterQuery q{ "proj-ac", 50 };
        auto res = ac.QueryActionCenter(q);
        if (!res.isComplete) { std::cerr << "QueryActionCenter error: " << res.errorMessage << "\n"; return 1; }
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
        bool stillHas = false;
        for (auto const& item : res2.items)
            if (item.kind == mayasaba::ActionCenterKind::UnresolvedSubmission) stillHas = true;
        EXPECT(!stillHas);

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
        EXPECT(resPage1.continuationToken.rfind("ac:v2:proj-ac:", 0) == 0); // Bound to project consistency signature

        // Continuation token project mismatch rejection
        ac.BindProject("other-proj", "Other Project", "C:/Other");
        mayasaba::ActionCenterQuery qMismatch{ "other-proj", 2, resPage1.continuationToken };
        auto resMismatch = ac.QueryActionCenter(qMismatch);
        EXPECT(!resMismatch.isComplete);
        EXPECT(resMismatch.errorMessage.find("project mismatch") != std::string::npos);

        // Query second page using valid continuation token.
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

        // Test mutation between pages: mutate project version, token must be rejected as expired
        ac.SetClosure("proj-ac", 2, false); // version increments to 2
        auto resExpired = ac.QueryActionCenter(mayasaba::ActionCenterQuery{ "proj-ac", 2, resPage1.continuationToken });
        EXPECT(!resExpired.isComplete);
        EXPECT(resExpired.errorMessage.find("expired") != std::string::npos);

        // 6. Stale item handling / underlying record updates:
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
        CASES_END();
    }

    // Attachment inspection: comprehensive verification outside storage lock
    {
        CASES_BEGIN("Test 2: Attachment inspection diagnostics");
        mayasaba::Storage ai(folder / "attachment_inspect.db");
        ai.BindProject("proj-ai", "AI Project", "C:/AI");

        auto attachmentsDir = folder / "attachments";
        fs::create_directories(attachmentsDir);

        // Happy-path draft attachment (markdown, valid hash, in approved managed storage)
        std::string validDraftText = "This is a valid English markdown draft document for testing.";
        std::string validDraftFile = (attachmentsDir / "valid_draft.md").string();
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
        EXPECT(ri1.inspection.previewContent == validDraftText); // Content preview loaded!
        EXPECT(ri1.inspection.previewUnavailableReasonEn.empty());

        // Updating the draft attachment increments sourceRecordVersion
        draft1.fileSize = validDraftText.size();
        ai.SaveDraftAttachment("proj-ai", draft1);
        auto ri1_v2 = ai.InspectAttachment(qi1);
        EXPECT(ri1_v2.inspection.sourceRecordVersion == "2");

        // Negative test: Arbitrary external path outside managed storage boundary
        std::string externalFile = (folder / "outside.txt").string();
        {
            std::ofstream of(externalFile, std::ios::binary);
            of.write("external content", 16);
        }
        mayasaba::AttachmentRecord draftExternal{};
        draftExternal.id = "att-external"; draftExternal.projectId = "proj-ai"; draftExternal.fileName = "outside.txt";
        draftExternal.fileSize = 16; draftExternal.sha256 = "dummy";
        draftExternal.detectedType = "text/plain"; draftExternal.storagePath = externalFile;
        draftExternal.sourcePath = "C:/source/outside.txt"; draftExternal.status = "READY";
        ai.SaveDraftAttachment("proj-ai", draftExternal);

        auto riExternal = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ai", "att-external" });
        EXPECT(riExternal.found);
        EXPECT(!riExternal.inspection.isManaged); // Rejected by managed storage boundary check!
        EXPECT(!riExternal.inspection.previewEligible);
        EXPECT(riExternal.inspection.previewUnavailableReasonEn.find("outside approved managed storage") != std::string::npos);

        // Negative test: Non-English source text rejected by English-only preview rule
        std::string nonEngFile = (attachmentsDir / "russian.txt").string();
        {
            std::ofstream of(nonEngFile, std::ios::binary);
            std::string utf8Text = "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82"; // "Привет"
            of.write(utf8Text.data(), utf8Text.size());
        }
        mayasaba::AttachmentRecord draftNonEng{};
        draftNonEng.id = "att-non-eng"; draftNonEng.projectId = "proj-ai"; draftNonEng.fileName = "russian.txt";
        draftNonEng.fileSize = 12;
        draftNonEng.sha256 = "dd679c0b9fd408a04148aa7d30c9df393f67b7227f65693fffe0ed6d0f0ade59";
        draftNonEng.detectedType = "text/plain"; draftNonEng.storagePath = nonEngFile;
        draftNonEng.sourcePath = "C:/source/russian.txt"; draftNonEng.status = "READY";
        ai.SaveDraftAttachment("proj-ai", draftNonEng);

        auto riNonEng = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ai", "att-non-eng" });
        EXPECT(riNonEng.found);
        EXPECT(riNonEng.inspection.isManaged);
        EXPECT(riNonEng.inspection.integrityVerified);
        EXPECT(!riNonEng.inspection.previewEligible); // English presentation rule rejected non-English prose!
        EXPECT(riNonEng.inspection.previewUnavailableReasonEn.find("non-English") != std::string::npos);

        // Negative test: File size exceeding 10 MiB preview ceiling
        mayasaba::AttachmentRecord draftOversized{};
        draftOversized.id = "att-oversized"; draftOversized.projectId = "proj-ai"; draftOversized.fileName = "big.txt";
        draftOversized.fileSize = 11ULL * 1024ULL * 1024ULL; // 11 MiB
        draftOversized.sha256 = "dummy";
        draftOversized.detectedType = "text/plain"; draftOversized.storagePath = validDraftFile;
        draftOversized.sourcePath = "C:/source/big.txt"; draftOversized.status = "READY";
        ai.SaveDraftAttachment("proj-ai", draftOversized);

        auto riOversized = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ai", "att-oversized" });
        EXPECT(riOversized.found);
        EXPECT(!riOversized.inspection.previewEligible);
        EXPECT(riOversized.inspection.previewUnavailableReasonEn.find("10 MiB") != std::string::npos);

        // Tampered file (disk contents altered: hash/size mismatch)
        std::string tamperedFile = (attachmentsDir / "tampered.txt").string();
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
        draftMissing.detectedType = "text/plain"; draftMissing.storagePath = (attachmentsDir / "nonexistent.txt").string();
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
        draftTraversal.detectedType = "text/plain"; draftTraversal.storagePath = (attachmentsDir / "../escaped.txt").string();
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
        std::string committedFile = (attachmentsDir / "committed.txt").string();
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
        CASES_END();
    }

    // -------------------------------------------------------------------------
    // Test 3: Production Action Center Presenter & Presentation Invariants
    // - Held query dismissed while pending: completion does not reopen panel (Finding 2)
    // - Paging cleanup & identity race: old-page -> refresh -> new-page -> old-completion (Finding 3)
    // - Refresh sequence capture, supersession by newer refresh, and coalesced publication
    // -------------------------------------------------------------------------
    {
        CASES_BEGIN("Test 3: Production Action Center Presenter & Presentation Invariants");
        mayasaba::Storage store(folder / "refresh_loop.db");
        store.BindProject("proj-rl", "Refresh Loop Proj", "C:/RL");

        mayasaba::ActionCenterPresenter presenter;
        presenter.BindProject("proj-rl", 1);
        EXPECT(presenter.IsDismissed() == true);
        EXPECT(presenter.ViewState().isVisible == false);

        // --- Part A: Held query dismissed while pending (Finding 2) ---
        // 1. User/system triggers refresh
        uint64_t const reqSeq1 = presenter.BeginRefresh(1);
        EXPECT(reqSeq1 > 0);
        EXPECT(presenter.IsDismissed() == false);
        EXPECT(presenter.IsRefreshing() == true);

        // 2. Query is executing on background (simulated with held query)
        // While query is pending, user dismisses panel via OnCloseActionCenter / presenter.Dismiss()
        presenter.Dismiss();
        EXPECT(presenter.IsDismissed() == true);
        EXPECT(presenter.ViewState().isVisible == false);

        // 3. Held background query completes and returns its result
        mayasaba::ActionCenterResult heldResult;
        heldResult.continuationToken = "tok-held";
        mayasaba::ActionCenterItem heldItem;
        heldItem.titleEn = "Held Item";
        heldItem.explanationEn = "Held Explanation";
        heldResult.items.push_back(heldItem);

        bool rerunNeeded = false;
        bool accepted = presenter.ApplyRefreshResult(reqSeq1, "proj-rl", 1, heldResult, rerunNeeded);
        // Completion from before dismissal must NOT reopen the panel!
        EXPECT(accepted == false);
        EXPECT(rerunNeeded == false);
        EXPECT(presenter.ViewState().isVisible == false); // Panel remains CLOSED!
        EXPECT(presenter.IsRefreshing() == false);        // Querying settled!
        EXPECT(presenter.Items().empty());                // Obsolete items discarded!

        // 4. Verify panel reopens only when a subsequent explicit event requests it
        uint64_t const reqSeq2 = presenter.BeginRefresh(1);
        EXPECT(reqSeq2 > reqSeq1); // Explicit refresh gets newer sequence
        EXPECT(presenter.IsDismissed() == false);
        EXPECT(presenter.IsRefreshing() == true);

        mayasaba::ActionCenterResult freshResult;
        freshResult.continuationToken = "tok-page-1";
        mayasaba::ActionCenterItem freshItem;
        freshItem.titleEn = "Fresh Item";
        freshItem.explanationEn = "Fresh Explanation";
        freshResult.items.push_back(freshItem);

        accepted = presenter.ApplyRefreshResult(reqSeq2, "proj-rl", 1, freshResult, rerunNeeded);
        EXPECT(accepted == true);
        EXPECT(rerunNeeded == false);
        EXPECT(presenter.ViewState().isVisible == true); // Explicit refresh legitimately opened panel
        EXPECT(presenter.ViewState().moreItemsVisible == true);
        EXPECT(presenter.Items().size() == 1);
        EXPECT(presenter.ContinuationToken() == "tok-page-1");

        // --- Part B: Controlled old-page -> refresh -> new-page -> old-completion race (Finding 3) ---
        // 1. Start old paging operation (id 1)
        std::string tokenForPaging1;
        uint64_t const oldPagingId = presenter.BeginPaging(1, tokenForPaging1);
        EXPECT(oldPagingId == 1);
        EXPECT(tokenForPaging1 == "tok-page-1");
        EXPECT(presenter.IsPaging() == true);
        EXPECT(presenter.ActivePagingId() == oldPagingId);
        EXPECT(presenter.ViewState().moreItemsEnabled == false); // Button disabled during paging

        // 2. An explicit refresh arrives before old paging completes
        uint64_t const refreshSeq3 = presenter.BeginRefresh(1);
        EXPECT(presenter.ActivePagingId() == 0); // Paging identity invalidated by refresh!
        EXPECT(presenter.IsPaging() == false);

        mayasaba::ActionCenterResult refreshResult3;
        refreshResult3.continuationToken = "tok-page-2";
        mayasaba::ActionCenterItem refreshItem3;
        refreshItem3.titleEn = "Refreshed Item";
        refreshItem3.explanationEn = "Refreshed Explanation";
        refreshResult3.items.push_back(refreshItem3);

        accepted = presenter.ApplyRefreshResult(refreshSeq3, "proj-rl", 1, refreshResult3, rerunNeeded);
        EXPECT(accepted == true);
        EXPECT(presenter.ContinuationToken() == "tok-page-2");
        EXPECT(presenter.Items().size() == 1);

        // 3. A new paging operation starts under the fresh state
        std::string tokenForPaging2;
        uint64_t const newPagingId = presenter.BeginPaging(1, tokenForPaging2);
        EXPECT(newPagingId == 2);
        EXPECT(tokenForPaging2 == "tok-page-2");
        EXPECT(presenter.IsPaging() == true);
        EXPECT(presenter.ActivePagingId() == newPagingId);
        EXPECT(presenter.ViewState().moreItemsEnabled == false);

        // 4. Now old paging operation #1 finishes late: cleanup runs first, then apply result
        bool const oldCleanupClaimed = presenter.CleanupPaging(oldPagingId);
        EXPECT(oldCleanupClaimed == false); // Old cleanup does NOT own active paging!
        EXPECT(presenter.IsPaging() == true); // Newer paging guard was NOT cleared!
        EXPECT(presenter.ActivePagingId() == newPagingId);
        EXPECT(presenter.ViewState().moreItemsEnabled == false); // Button NOT prematurely re-enabled!

        mayasaba::ActionCenterResult oldPageResult;
        mayasaba::ActionCenterItem obsoleteItem;
        obsoleteItem.titleEn = "Obsolete Page Item";
        obsoleteItem.explanationEn = "Obsolete Page Explanation";
        oldPageResult.items.push_back(obsoleteItem);
        oldPageResult.continuationToken = "tok-old-page";

        bool const oldPageAccepted = presenter.ApplyPagingResult(
            oldPagingId, "proj-rl", 1, refreshSeq3, tokenForPaging1, oldPageResult);
        EXPECT(oldPageAccepted == false); // Obsolete page rejected by token and paging identity mismatch!
        EXPECT(presenter.Items().size() == 1); // No obsolete items appended!

        // 5. Now new paging operation #2 completes legitimately
        mayasaba::ActionCenterResult newPageResult;
        mayasaba::ActionCenterItem validNewItem;
        validNewItem.titleEn = "Valid New Page Item";
        validNewItem.explanationEn = "Valid New Page Explanation";
        newPageResult.items.push_back(validNewItem);
        newPageResult.continuationToken = ""; // End of pages

        bool const newPageAccepted = presenter.ApplyPagingResult(
            newPagingId, "proj-rl", 1, refreshSeq3, tokenForPaging2, newPageResult);
        EXPECT(newPageAccepted == true);
        EXPECT(presenter.Items().size() == 2);

        bool const newCleanupClaimed = presenter.CleanupPaging(newPagingId);
        EXPECT(newCleanupClaimed == true); // New cleanup successfully released active paging
        EXPECT(presenter.IsPaging() == false);
        EXPECT(presenter.ActivePagingId() == 0);
        EXPECT(presenter.ViewState().moreItemsEnabled == true); // Re-enabled truthfully
        EXPECT(presenter.ViewState().moreItemsVisible == false); // End of pages

        // --- Part C: Concurrent refresh requests coalescing to newest ---
        uint64_t const cSeq1 = presenter.BeginRefresh(1);
        uint64_t const cSeq2 = presenter.BeginRefresh(1);
        uint64_t const cSeq3 = presenter.BeginRefresh(1);
        EXPECT(cSeq2 > cSeq1);
        EXPECT(cSeq3 > cSeq2);

        mayasaba::ActionCenterResult cRes1;
        cRes1.continuationToken = "";
        bool rerunC1 = false;
        bool accC1 = presenter.ApplyRefreshResult(cSeq1, "proj-rl", 1, cRes1, rerunC1);
        EXPECT(accC1 == false); // Older refresh #1 superseded
        EXPECT(rerunC1 == true); // Loop continues to newest sequence

        mayasaba::ActionCenterResult cRes3;
        mayasaba::ActionCenterItem cItem3;
        cItem3.titleEn = "Coalesced Final Item";
        cRes3.items.push_back(cItem3);
        bool rerunC3 = false;
        bool accC3 = presenter.ApplyRefreshResult(cSeq3, "proj-rl", 1, cRes3, rerunC3);
        EXPECT(accC3 == true); // Newest refresh #3 accepted
        EXPECT(rerunC3 == false);
        EXPECT(presenter.Items().size() == 1);
        EXPECT(presenter.Items()[0].titleEn == "Coalesced Final Item");
        CASES_END();
    }

    // -------------------------------------------------------------------------
    // Test 4: Action Center expected-version verification at invocation
    // -------------------------------------------------------------------------
    {
        CASES_BEGIN("Test 4: Action Center expected-version verification at invocation");
        mayasaba::Storage store(folder / "invocation_version.db");
        store.BindProject("proj-iv", "Invocation Proj", "C:/IV");
        store.SetClosure("proj-iv", 0, false);

        mayasaba::AttachmentRecord att{};
        att.id = "att-iv-1"; att.projectId = "proj-iv"; att.fileName = "test.txt";
        att.fileSize = 10; att.sha256 = "dummy"; att.detectedType = "text/plain";
        att.storagePath = ""; att.sourcePath = "C:/source/test.txt"; att.status = "REJECTED";
        store.SaveDraftAttachment("proj-iv", att);

        auto acRes = store.QueryActionCenter(mayasaba::ActionCenterQuery{ "proj-iv", 50 });
        EXPECT(acRes.items.size() == 1);
        auto item = acRes.items[0];
        EXPECT(item.kind == mayasaba::ActionCenterKind::FailedAttachment);
        EXPECT(item.sourceRecordVersion == "1");

        // When version matches current record -> invocation check passes
        auto drafts = store.GetDraftAttachments("proj-iv");
        EXPECT(!drafts.empty());
        EXPECT(std::to_string(drafts[0].version) == item.sourceRecordVersion);

        // Mutate record version in DB (e.g. attachment updated or version incremented)
        att.status = "SOURCE_CHANGED";
        store.SaveDraftAttachment("proj-iv", att);

        drafts = store.GetDraftAttachments("proj-iv");
        EXPECT(drafts[0].version == 2);
        // Stale check compares displayed item version ("1") with current record version ("2")
        bool isStale = (std::to_string(drafts[0].version) != item.sourceRecordVersion);
        EXPECT(isStale == true); // Stale detected! Must refuse to apply stale action.
        CASES_END();
    }

    // -------------------------------------------------------------------------
    // Test 5: Production AttachmentService file drop ingestion, mixed-batch folder rejection,
    // and project switching cancellation during delayed drop
    // -------------------------------------------------------------------------
    {
        CASES_BEGIN("Test 5: Production AttachmentService file drop ingestion, mixed-batch folder rejection,");
        mayasaba::Storage store(folder / "drop_ingestion.db");
        store.BindProject("proj-A", "Project Alpha", "C:/Alpha");
        store.BindProject("proj-B", "Project Beta", "C:/Beta");

        auto stagingDir = folder / "drop_staging";
        fs::create_directories(stagingDir);
        mayasaba::AttachmentService attService(store, stagingDir);

        auto dropSourceDir = folder / "drop_sources";
        fs::create_directories(dropSourceDir);

        // Create a valid test file
        auto singleFile = dropSourceDir / "valid_drop.txt";
        {
            std::ofstream ofs(singleFile);
            ofs << "Valid English prose payload for native drag-and-drop ingestion testing.\n";
        }

        // Create a folder for mixed-batch testing
        auto subFolder = dropSourceDir / "folder_item";
        fs::create_directories(subFolder);

        // 1. Single valid file validation
        auto resSingle = attService.ValidateCandidateBatch({ singleFile.wstring() }, {});
        EXPECT(resSingle.accepted == true);
        EXPECT(resSingle.reason == mayasaba::ValidationFailureReason::None);
        EXPECT(resSingle.validatedFiles.size() == 1);
        EXPECT(resSingle.validatedFiles[0].fileName == "valid_drop.txt");

        // 2. Mixed batch rejection: folder + file in batch
        // In accordance with contract: Folders cannot be attached; entire batch rejected!
        std::vector<std::wstring> mixedPaths = { singleFile.wstring(), subFolder.wstring() };
        auto resMixed = attService.ValidateCandidateBatch(mixedPaths, {});
        EXPECT(resMixed.accepted == false);
        EXPECT(resMixed.reason == mayasaba::ValidationFailureReason::InaccessibleFile);
        EXPECT(resMixed.validatedFiles.empty());

        // 3. Pure folder rejection
        auto resFolder = attService.ValidateCandidateBatch({ subFolder.wstring() }, {});
        EXPECT(resFolder.accepted == false);
        EXPECT(resFolder.reason == mayasaba::ValidationFailureReason::InaccessibleFile);

        // 4a. Already-canceled preparation (boundary check prior to preparation start)
        std::string activeProject = "proj-A";
        uint64_t currentGen = 1;

        // Simulate async drop preparation initiated for project A
        std::string const capturedProjectId = activeProject;
        uint64_t const capturedGen = currentGen;

        // User switches project to B during in-flight preparation
        activeProject = "proj-B";
        currentGen = 2;

        // In PrepareBatch, cancellation predicate checks project boundary and generation
        auto isCancelled = [&] {
            return activeProject != capturedProjectId || currentGen != capturedGen;
        };

        auto prepared = attService.PrepareBatch("proj-A", { singleFile.wstring() }, isCancelled, 120);

        // In MainWindow::ProcessCandidatePathsAsync, foreground return checks project boundary:
        // if (windowClosed_ || activeProject_ != projectId || selectorGeneration_.load() != gen) co_return;
        bool const dropCancelled = (activeProject != capturedProjectId || currentGen != capturedGen);
        EXPECT(dropCancelled == true);

        // When cancelled, results are discarded and never saved to either project
        if (!dropCancelled)
        {
            attService.SaveDraftAttachments(capturedProjectId, prepared);
        }

        auto draftsA = attService.LoadDraftAttachments("proj-A");
        EXPECT(draftsA.empty()); // Dropped file was NOT admitted to source project A

        auto draftsB = attService.LoadDraftAttachments("proj-B");
        EXPECT(draftsB.empty()); // Dropped file was NOT admitted to target project B

        // 4b. Controlled in-flight asynchronous project switch during ACTIVE preparation
        // Seed existing drafts in Project A and Project B to verify draft preservation
        mayasaba::AttachmentCandidate draftAExisting;
        draftAExisting.id = "draft-a-existing";
        draftAExisting.fileName = "existing_a.txt";
        draftAExisting.fileSize = 100;
        draftAExisting.status = mayasaba::AttachmentStatus::Ready;
        draftAExisting.detectedType = "text/plain";
        draftAExisting.originalPath = L"C:/alpha/existing_a.txt";
        attService.SaveDraftAttachments("proj-A", { draftAExisting });

        mayasaba::AttachmentCandidate draftBExisting;
        draftBExisting.id = "draft-b-existing";
        draftBExisting.fileName = "existing_b.txt";
        draftBExisting.fileSize = 200;
        draftBExisting.status = mayasaba::AttachmentStatus::Ready;
        draftBExisting.detectedType = "text/plain";
        draftBExisting.originalPath = L"C:/beta/existing_b.txt";
        attService.SaveDraftAttachments("proj-B", { draftBExisting });

        // Thread-safe state modeled exactly on MainWindow:
        // activeProject: mutated and read ONLY on UI thread.
        // windowClosed: atomic<bool> read by UI and worker cancellation predicate.
        // selectorGeneration: atomic_uint64_t incremented on UI thread, read by worker cancellation predicate.
        activeProject = "proj-A";
        std::atomic<bool> windowClosed{false};
        std::atomic_uint64_t selectorGeneration{10};

        mayasaba::AttachmentIngestionCoordinator coordinator;

        auto waitBounded = [](std::atomic<bool> const& flag, int maxMs = 3000) -> bool {
            auto start = std::chrono::steady_clock::now();
            while (!flag.load(std::memory_order_relaxed)) {
                if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() > maxMs) {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return true;
        };

        auto activeDropFile = dropSourceDir / "active_drop.txt";
        {
            std::ofstream ofs(activeDropFile);
            ofs << "Payload for testing cancellation during active background preparation chunks.\n";
        }

        std::atomic<bool> prepActive{false};
        std::atomic<bool> resumePrep{false};
        std::atomic<bool> prepHookTimedOut{false};

        attService.SetPreparationChunkHookForTesting([&]() {
            prepActive = true;
            auto hookStart = std::chrono::steady_clock::now();
            while (!resumePrep.load(std::memory_order_relaxed)) {
                if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - hookStart).count() > 3000) {
                    prepHookTimedOut = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });

        // UI thread initiates drop coordination context
        auto dropCtx = coordinator.CreateContext(
            activeProject,
            selectorGeneration.load(),
            &windowClosed,
            &selectorGeneration);

        // Worker thread cancellation predicate strictly reads atomic state only
        auto isWorkerCancelled = coordinator.CreateWorkerCancellationPredicate(dropCtx);

        prepared.clear();
        std::thread inFlightWorkerThread([&]() {
            prepared = attService.PrepareBatch("proj-A", { activeDropFile.wstring() }, isWorkerCancelled, 120);
        });

        // Wait boundedly until preparation has ACTUALLY BEGUN and reached the active copy chunk loop
        EXPECT(waitBounded(prepActive, 3000) == true);
        EXPECT(!prepHookTimedOut);

        // While preparation is active, verify that durable intent record was saved in SQLite with status PREPARING
        auto inFlightDrafts = attService.LoadDraftAttachments("proj-A");
        bool foundPreparingIntent = false;
        for (auto const& d : inFlightDrafts) {
            if (d.fileName == "active_drop.txt" && d.status == mayasaba::AttachmentStatus::Preparing) {
                foundPreparingIntent = true;
            }
        }
        EXPECT(foundPreparingIntent == true);

        // Perform project switch on UI thread while background worker is actively copying chunks:
        activeProject = "proj-B";
        ++selectorGeneration; // Now 11

        // Resume worker so it detects cancellation
        resumePrep = true;
        inFlightWorkerThread.join();
        EXPECT(!prepHookTimedOut);

        // Worker result reflects cancellation
        EXPECT(!prepared.empty());
        EXPECT(prepared[0].status == mayasaba::AttachmentStatus::Rejected);
        EXPECT(prepared[0].errorMessage == "Preparation cancelled");

        // Execute the shared production operation (ValidateAndCommitPreparedBatch) for the stale completion:
        // Project switch to proj-B occurred while preparation was in flight for proj-A.
        std::vector<mayasaba::AttachmentCandidate> uiDraftsB = attService.LoadDraftAttachments("proj-B");
        bool persistenceInvoked = false;
        bool const committed = coordinator.ValidateAndCommitPreparedBatch(
            dropCtx,
            activeProject, // "proj-B"
            selectorGeneration.load(), // 11
            windowClosed.load(), // false
            uiDraftsB,
            prepared,
            { "stale-preparing-id" },
            [&](std::string const& proj, std::vector<mayasaba::AttachmentCandidate> const& drafts) {
                persistenceInvoked = true;
                attService.SaveDraftAttachments(proj, drafts);
            });

        // The shared production operation MUST reject the stale completion:
        EXPECT(committed == false);
        // Persistence is NEVER invoked on stale completion:
        EXPECT(persistenceInvoked == false);
        // Current draft list in memory is NOT mutated:
        EXPECT(uiDraftsB.size() == 1);
        EXPECT(uiDraftsB[0].id == "draft-b-existing");

        // In production MainWindow, because ValidateAndCommitPreparedBatch returns false,
        // no drafts are mutated and saveDrafts is never invoked.
        // Assert:
        // 1. No stale attachment admission to either project
        auto postDraftsA = attService.LoadDraftAttachments("proj-A");
        EXPECT(postDraftsA.size() == 1);
        EXPECT(postDraftsA[0].id == "draft-a-existing");
        EXPECT(postDraftsA[0].fileName == "existing_a.txt");

        auto postDraftsB = attService.LoadDraftAttachments("proj-B");
        EXPECT(postDraftsB.size() == 1);
        EXPECT(postDraftsB[0].id == "draft-b-existing");
        EXPECT(postDraftsB[0].fileName == "existing_b.txt");

        // 2. Existing drafts remain intact
        EXPECT(postDraftsA[0].fileName == "existing_a.txt");
        EXPECT(postDraftsB[0].fileName == "existing_b.txt");

        // 3. No automatic submission
        EXPECT(store.Contributions("proj-A").empty());
        EXPECT(!store.Pending("proj-A").has_value());
        EXPECT(store.Contributions("proj-B").empty());
        EXPECT(!store.Pending("proj-B").has_value());

        // 4. Cancellation and preparation state recover correctly:
        // Clean up hook and perform retry with valid completion
        attService.SetPreparationChunkHookForTesting(nullptr);

        activeProject = "proj-A";
        ++selectorGeneration; // 12
        auto retryCtx = coordinator.CreateContext(activeProject, selectorGeneration.load(), &windowClosed, &selectorGeneration);
        auto retryCancelled = coordinator.CreateWorkerCancellationPredicate(retryCtx);

        // UI sets up preparing slot prior to background preparation (matching MainWindow)
        std::vector<mayasaba::AttachmentCandidate> uiDraftsA = attService.LoadDraftAttachments("proj-A");
        std::string const slotId = "slot-retry-1";
        mayasaba::AttachmentCandidate slot;
        slot.id = slotId;
        slot.fileName = "active_drop.txt";
        slot.status = mayasaba::AttachmentStatus::Preparing;
        uiDraftsA.push_back(slot);

        auto retryPrepared = attService.PrepareBatch("proj-A", { activeDropFile.wstring() }, retryCancelled, 120);
        EXPECT(!retryPrepared.empty());
        EXPECT(retryPrepared[0].status == mayasaba::AttachmentStatus::Ready);

        // Execute the shared production operation for the valid completion:
        persistenceInvoked = false;
        bool const retryCommitted = coordinator.ValidateAndCommitPreparedBatch(
            retryCtx,
            activeProject, // "proj-A"
            selectorGeneration.load(), // 12
            windowClosed.load(), // false
            uiDraftsA,
            retryPrepared,
            { slotId },
            [&](std::string const& proj, std::vector<mayasaba::AttachmentCandidate> const& drafts) {
                persistenceInvoked = true;
                attService.SaveDraftAttachments(proj, drafts);
            });

        // Valid completion is accepted and committed:
        EXPECT(retryCommitted == true);
        EXPECT(persistenceInvoked == true);

        // Draft in memory is updated to Ready:
        EXPECT(uiDraftsA.size() == 2);
        EXPECT(uiDraftsA[1].id == retryPrepared[0].id);
        EXPECT(uiDraftsA[1].status == mayasaba::AttachmentStatus::Ready);

        // Draft in SQLite storage is updated:
        auto finalDraftsA = attService.LoadDraftAttachments("proj-A");
        EXPECT(finalDraftsA.size() == 2);
        EXPECT(finalDraftsA[1].id == retryPrepared[0].id);
        EXPECT(finalDraftsA[1].status == mayasaba::AttachmentStatus::Ready);

        // Clean up retry draft to keep proj-A baseline
        attService.RemoveDraftAttachment("proj-A", retryPrepared[0].id);
        EXPECT(attService.LoadDraftAttachments("proj-A").size() == 1);

        // 4c. Controlled in-flight asynchronous project switch during retrieval boundary
        std::atomic<bool> heldAtRetrievalBoundary{false};
        std::atomic<bool> resumeRetrievalBoundary{false};
        std::atomic<bool> retrievalHookTimedOut{false};

        coordinator.SetRetrievalPendingHookForTesting([&]() {
            heldAtRetrievalBoundary = true;
            auto hookStart = std::chrono::steady_clock::now();
            while (!resumeRetrievalBoundary.load(std::memory_order_relaxed)) {
                if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - hookStart).count() > 3000) {
                    retrievalHookTimedOut = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });

        activeProject = "proj-A";
        selectorGeneration = 20;
        auto dropCtxRetrieval = coordinator.CreateContext(activeProject, selectorGeneration.load(), &windowClosed, &selectorGeneration);

        bool retrievalBoundaryPassed = false;
        std::vector<std::wstring> retrievedPaths;
        std::thread inFlightRetrievalThread([&]() {
            // Production OnComposerDrop sequence:
            coordinator.OnRetrievalPending();
            retrievedPaths = { singleFile.wstring() };
            retrievalBoundaryPassed = coordinator.ValidateRetrievalBoundary(
                dropCtxRetrieval,
                activeProject,
                selectorGeneration.load(),
                windowClosed.load(),
                false);
        });

        EXPECT(waitBounded(heldAtRetrievalBoundary, 3000) == true);
        EXPECT(!retrievalHookTimedOut);

        // Switch project while retrieval is pending:
        activeProject = "proj-B";
        selectorGeneration = 21;

        resumeRetrievalBoundary = true;
        inFlightRetrievalThread.join();
        EXPECT(!retrievalHookTimedOut);

        EXPECT(retrievalBoundaryPassed == false);

        // Assert drafts untouched
        auto retrievalDraftsA = attService.LoadDraftAttachments("proj-A");
        EXPECT(retrievalDraftsA.size() == 1);
        EXPECT(retrievalDraftsA[0].id == "draft-a-existing");

        auto retrievalDraftsB = attService.LoadDraftAttachments("proj-B");
        EXPECT(retrievalDraftsB.size() == 1);
        EXPECT(retrievalDraftsB[0].id == "draft-b-existing");

        EXPECT(store.Contributions("proj-A").empty());
        EXPECT(!store.Pending("proj-A").has_value());
        EXPECT(store.Contributions("proj-B").empty());
        EXPECT(!store.Pending("proj-B").has_value());

        coordinator.SetRetrievalPendingHookForTesting(nullptr);

        // Demonstrate boundary guard failure on retrieval:
        EXPECT(coordinator.ValidateRetrievalBoundary(dropCtxRetrieval, "proj-B", 21, false, false) == false);
        EXPECT(coordinator.ValidateRetrievalBoundary(dropCtxRetrieval, "proj-A", 21, false, false) == false);
        EXPECT(coordinator.ValidateRetrievalBoundary(dropCtxRetrieval, "proj-A", 20, true, false) == false);
        EXPECT(coordinator.ValidateRetrievalBoundary(dropCtxRetrieval, "proj-A", 20, false, true) == false);

        // 4d. Verify shared coordinator CoordinateDrop method with active chunk hook
        {
            activeProject = "proj-A";
            selectorGeneration = 30;
            auto sharedCtx = coordinator.CreateContext(activeProject, selectorGeneration.load(), &windowClosed, &selectorGeneration);

            std::atomic<bool> sharedPrepActive{false};
            std::atomic<bool> sharedResumePrep{false};
            std::atomic<bool> sharedHookTimedOut{false};

            attService.SetPreparationChunkHookForTesting([&]() {
                sharedPrepActive = true;
                auto hookStart = std::chrono::steady_clock::now();
                while (!sharedResumePrep.load(std::memory_order_relaxed)) {
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - hookStart).count() > 3000) {
                        sharedHookTimedOut = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            });

            bool sharedDropResult = true;
            std::thread sharedThread([&]() {
                sharedDropResult = coordinator.CoordinateDrop(
                    sharedCtx,
                    [&]() { return activeProject; },
                    [&]() { return selectorGeneration.load(); },
                    [&]() { return windowClosed.load(); },
                    [&]() { return std::vector<std::wstring>{ activeDropFile.wstring() }; },
                    [&](std::vector<std::wstring> const& paths, std::function<bool()> const& isCanc) {
                        return attService.PrepareBatch("proj-A", paths, isCanc, 120);
                    },
                    [&](std::vector<mayasaba::AttachmentCandidate> const& p) {
                        attService.SaveDraftAttachments("proj-A", p);
                    }
                );
            });

            EXPECT(waitBounded(sharedPrepActive, 3000) == true);
            // Switch project
            activeProject = "proj-B";
            selectorGeneration = 31;
            sharedResumePrep = true;
            sharedThread.join();
            EXPECT(!sharedHookTimedOut);
            EXPECT(sharedDropResult == false);

            attService.SetPreparationChunkHookForTesting(nullptr);

            auto draftsA_4d = attService.LoadDraftAttachments("proj-A");
            EXPECT(draftsA_4d.size() == 1);
            EXPECT(draftsA_4d[0].id == "draft-a-existing");
        }

        // 4e. Final attachment persistence failure handling and contention verification
        {
            // Case 1: Failure after some batch work has already occurred
            // Seed a multi-item batch: cand1 and cand2
            auto batchFile1 = folder / "batch_fail_1.txt";
            auto batchFile2 = folder / "batch_fail_2.txt";
            {
                std::ofstream f1(batchFile1); f1 << "batch candidate 1 content";
                std::ofstream f2(batchFile2); f2 << "batch candidate 2 content";
            }

            activeProject = "proj-A";
            selectorGeneration = 40;
            auto failCtx = coordinator.CreateContext(activeProject, selectorGeneration.load(), &windowClosed, &selectorGeneration);
            // In-memory composer draft has existing draft before new batch
            std::vector<mayasaba::AttachmentCandidate> uiDraftsA_4e = attService.LoadDraftAttachments("proj-A");
            EXPECT(uiDraftsA_4e.size() == 1);
            EXPECT(uiDraftsA_4e[0].id == "draft-a-existing");

            // Give the candidate batch explicit test IDs
            std::string const candId1 = "cand-batch-fail-1";
            std::string const candId2 = "cand-batch-fail-2";

            mayasaba::AttachmentCandidate cand1, cand2;
            cand1.id = candId1; cand1.fileName = "batch_fail_1.txt"; cand1.fileSize = 100; cand1.sha256 = "hash1"; cand1.status = mayasaba::AttachmentStatus::Ready;
            cand2.id = candId2; cand2.fileName = "batch_fail_2.txt"; cand2.fileSize = 100; cand2.sha256 = "hash2"; cand2.status = mayasaba::AttachmentStatus::Ready;
            std::vector<mayasaba::AttachmentCandidate> preparedBatch = { cand1, cand2 };

            // In SQLite, write uncommitted PREPARING records (simulating active preparation intent)
            mayasaba::AttachmentRecord intent1, intent2;
            intent1.id = candId1; intent1.projectId = "proj-A"; intent1.fileName = "batch_fail_1.txt"; intent1.fileSize = 100; intent1.status = "PREPARING";
            intent2.id = candId2; intent2.projectId = "proj-A"; intent2.fileName = "batch_fail_2.txt"; intent2.fileSize = 100; intent2.status = "PREPARING";
            store.SaveDraftAttachment("proj-A", intent1);
            store.SaveDraftAttachment("proj-A", intent2);

            // In-memory composer draft has existing draft + preparing placeholders matching batch
            std::vector<std::string> preparingIds = { candId1, candId2 };
            mayasaba::AttachmentCandidate slot1 = preparedBatch[0];
            slot1.id = candId1;
            slot1.status = mayasaba::AttachmentStatus::Preparing;
            mayasaba::AttachmentCandidate slot2 = preparedBatch[1];
            slot2.id = candId2;
            slot2.status = mayasaba::AttachmentStatus::Preparing;
            uiDraftsA_4e.push_back(slot1);
            uiDraftsA_4e.push_back(slot2);

            // Inject simulated failure after some batch work has already occurred:
            // Item 0 (draft-a-existing) and Item 1 (candId1) update in statement step;
            // Item 2 (candId2) throws write failure midway through the batch.
            store.SetSaveDraftAttachmentsItemHookForTesting([&](size_t index, std::string const& /*id*/) {
                if (index == 2)
                {
                    throw std::runtime_error("Simulated SQLite write error midway through batch");
                }
            });

            std::string capturedError;
            bool const failCommitted = coordinator.ValidateAndCommitPreparedBatch(
                failCtx,
                activeProject,
                selectorGeneration.load(),
                windowClosed.load(),
                uiDraftsA_4e,
                preparedBatch,
                preparingIds,
                [&](std::string const& proj, std::vector<mayasaba::AttachmentCandidate> const& drafts) {
                    attService.SaveDraftAttachments(proj, drafts);
                },
                &capturedError);

            // 1. No unhandled exception: safely caught, returns false
            EXPECT(failCommitted == false);
            EXPECT(!capturedError.empty());
            EXPECT(capturedError.find("Simulated SQLite write error midway through batch") != std::string::npos);

            // 2. No false saved state in memory:
            // uiDraftsA_4e candidate changes are discarded; placeholders remain uncommitted
            EXPECT(uiDraftsA_4e.size() == 3);
            EXPECT(uiDraftsA_4e[0].id == "draft-a-existing");
            EXPECT(uiDraftsA_4e[1].status == mayasaba::AttachmentStatus::Preparing);
            EXPECT(uiDraftsA_4e[2].status == mayasaba::AttachmentStatus::Preparing);

            // In production MainWindow, on !committed, uncommitted preparing placeholders are removed:
            {
                std::unordered_set<std::string> prepSet(preparingIds.begin(), preparingIds.end());
                auto it = std::remove_if(uiDraftsA_4e.begin(), uiDraftsA_4e.end(),
                    [&](mayasaba::AttachmentCandidate const& c) { return prepSet.count(c.id) > 0; });
                uiDraftsA_4e.erase(it, uiDraftsA_4e.end());
            }
            EXPECT(uiDraftsA_4e.size() == 1);
            EXPECT(uiDraftsA_4e[0].id == "draft-a-existing");

            // 3. No false saved state in SQLite storage:
            // Transaction rolled back; candId1's partial update to READY was undone back to PREPARING!
            auto dbDraftsAfterFail = attService.LoadDraftAttachments("proj-A");
            EXPECT(dbDraftsAfterFail[0].id == "draft-a-existing");
            EXPECT(dbDraftsAfterFail[0].fileName == "existing_a.txt");
            EXPECT(dbDraftsAfterFail[0].status == mayasaba::AttachmentStatus::Ready);
            for (auto const& d : dbDraftsAfterFail)
            {
                if (d.id == candId1 || d.id == candId2)
                {
                    EXPECT(d.status != mayasaba::AttachmentStatus::Ready);
                }
            }

            // 4. Preserved existing drafts in both memory and storage
            EXPECT(uiDraftsA_4e[0].fileName == "existing_a.txt");
            EXPECT(dbDraftsAfterFail[0].fileName == "existing_a.txt");

            // 5. No automatic submission
            EXPECT(store.Contributions("proj-A").empty());
            EXPECT(!store.Pending("proj-A").has_value());

            // 6. Successful controlled recovery without duplicates on retry
            store.SetSaveDraftAttachmentsItemHookForTesting(nullptr);

            // Prepare UI state for retry:
            uiDraftsA_4e.push_back(slot1);
            uiDraftsA_4e.push_back(slot2);
            capturedError.clear();

            bool const retryCommitted_4e = coordinator.ValidateAndCommitPreparedBatch(
                failCtx,
                activeProject,
                selectorGeneration.load(),
                windowClosed.load(),
                uiDraftsA_4e,
                preparedBatch,
                preparingIds,
                [&](std::string const& proj, std::vector<mayasaba::AttachmentCandidate> const& drafts) {
                    attService.SaveDraftAttachments(proj, drafts);
                },
                &capturedError);

            EXPECT(retryCommitted_4e == true);
            EXPECT(capturedError.empty());

            // Both items committed cleanly to memory as Ready
            EXPECT(uiDraftsA_4e.size() == 3);
            EXPECT(uiDraftsA_4e[0].id == "draft-a-existing");
            EXPECT(uiDraftsA_4e[1].status == mayasaba::AttachmentStatus::Ready);
            EXPECT(uiDraftsA_4e[2].status == mayasaba::AttachmentStatus::Ready);

            // Both items committed cleanly to SQLite storage without duplicates
            auto dbDraftsAfterRetry = attService.LoadDraftAttachments("proj-A");
            EXPECT(dbDraftsAfterRetry.size() == 3);
            EXPECT(dbDraftsAfterRetry[0].id == "draft-a-existing");

            std::unordered_set<std::string> uniqueIds;
            for (auto const& d : dbDraftsAfterRetry) uniqueIds.insert(d.id);
            EXPECT(uniqueIds.size() == 3);
            EXPECT(uniqueIds.count(candId1) == 1);
            EXPECT(uniqueIds.count(candId2) == 1);

            // Clean up retry drafts, restoring single existing draft baseline
            store.RemoveDraftAttachment("proj-A", candId1);
            store.RemoveDraftAttachment("proj-A", candId2);
            EXPECT(attService.LoadDraftAttachments("proj-A").size() == 1);

            // Case 2: C++ mutex contention.
            // This used to assert that SaveDraftAttachments gave up after a fixed ~100 ms of
            // try_lock polling. That behaviour was removed: it did not bound SQLite
            // contention (the real wait happens inside BEGIN IMMEDIATE under SQLite's busy
            // timeout), and it turned a transient lock into a spurious failure. Every caller
            // now runs on the controlled background lane, so the operation waits for the
            // store mutex instead of polling it.
            //
            // The property asserted here is the replacement contract: while another thread
            // holds the store mutex the save does not return early and does not lose the
            // draft, and once the lock is released it completes and the draft is applied.
            // SQLite-level contention is covered separately in storage_tests.cpp against an
            // independent connection, which is the contention a second process actually hits.
            {
                // Prepare drafts before acquiring contention lock to avoid blocking on LoadDraftAttachments
                std::vector<mayasaba::AttachmentCandidate> contentionDrafts = attService.LoadDraftAttachments("proj-A");
                EXPECT(contentionDrafts.size() == 1);
                contentionDrafts.push_back(slot1);

                std::atomic<bool> blockerLocked{false};
                std::atomic<bool> releaseBlocker{false};
                std::thread contentionBlocker([&]() {
                    std::unique_lock<std::mutex> lock(store.MutexForTesting());
                    blockerLocked = true;
                    while (!releaseBlocker.load())
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                });

                EXPECT(waitBounded(blockerLocked, 2000) == true);

                // The save is issued on another thread and must not complete while the store
                // mutex is held elsewhere.
                std::atomic<bool> saveFinished{false};
                std::atomic<bool> saveSucceeded{false};
                std::string contentionError;
                std::thread saver([&]() {
                    try
                    {
                        attService.SaveDraftAttachments(activeProject, contentionDrafts);
                        saveSucceeded = true;
                    }
                    catch (std::exception const& e) { contentionError = e.what(); }
                    saveFinished = true;
                });

                // It waits rather than failing fast: a spurious early failure would be the
                // old, wrong behaviour. This is an observation of the wait, not a timing
                // guarantee about any UI thread.
                EXPECT(waitBounded(saveFinished, 250) == false);

                // Release the blocker; the waiting save must now complete successfully.
                releaseBlocker = true;
                contentionBlocker.join();
                EXPECT(waitBounded(saveFinished, 10000) == true);
                saver.join();

                EXPECT(saveSucceeded == true);
                EXPECT(contentionError.empty());

                // No false saved state and no lost draft: the write really landed.
                auto const persisted = attService.LoadDraftAttachments(activeProject);
                bool found = false;
                for (auto const& d : persisted)
                {
                    if (d.id == slot1.id) found = true;
                }
                EXPECT(found);

                attService.RemoveDraftAttachment("proj-A", preparedBatch[0].id);
                EXPECT(attService.LoadDraftAttachments("proj-A").size() == 1);
            }
        }

        // 5. Overlapping drop guard
        bool dropProcessing = false;
        auto ProcessDrop = [&dropProcessing]() -> bool
        {
            if (dropProcessing) return false; // Rejected
            dropProcessing = true;
            return true;
        };

        EXPECT(ProcessDrop() == true);
        EXPECT(ProcessDrop() == false); // Overlapping drop rejected!
        dropProcessing = false;
        EXPECT(ProcessDrop() == true);
        CASES_END();
    }

    // -------------------------------------------------------------------------
    // Test 6: Non-English ASCII prose, mixed language, and digit/underscore dressed
    // prose rejected for preview under English presentation policy
    // -------------------------------------------------------------------------
    {
        CASES_BEGIN("Test 6: Non-English ASCII prose, mixed language, and digit/underscore dressed");
        mayasaba::Storage store(folder / "english_policy.db");
        store.BindProject("proj-eng", "English Policy Proj", "C:/Eng");
        auto attDir = store.ManagedAttachmentRoot();
        fs::create_directories(attDir);

        struct TestCase {
            std::string id;
            std::string text;
            bool expectEligible;
        };

        std::vector<TestCase> cases = {
            { "att-ascii-sp1", "hola mundo", false },
            { "att-ascii-sp2", "buenos dias amigo gracias por todo", false },
            { "att-ascii-fr1", "bonjour le monde merci beaucoup", false },
            { "att-ascii-de1", "hallo guten tag danke", false },
            { "att-ascii-it1", "ciao mondo grazie arrivederci", false },
            { "att-ascii-mixed", "This is namaste sabko", false },
            { "att-ascii-digit-suffixed", "This is hola1 mundo1", false },
            { "att-ascii-underscore-noneng", "This is hola_mundo", false },
            { "att-ascii-digit-mixed", "test_hola1_mundo2", false },
            { "att-ascii-underscore-dressed", "This is palabra_desconocida dressed with underscores", false },
            { "att-ascii-long-foreign", "This is a test of long unrecognized prose luego los usuarios no pudieron continuar con el proceso de autorizacion del sistema", false },
            { "att-ascii-valid-per", "Requests per second were within limits as per the specification.", true },
            { "att-ascii-eng", "This is an authorized English specification file for project inspection.", true }
        };

        for (auto const& tc : cases)
        {
            std::string filePath = (attDir / (tc.id + ".txt")).string();
            {
                std::ofstream of(filePath, std::ios::binary);
                of.write(tc.text.data(), tc.text.size());
            }

            std::string hash = ComputeSha256ForTestFile(filePath);

            mayasaba::AttachmentRecord rec{};
            rec.id = tc.id;
            rec.projectId = "proj-eng";
            rec.fileName = tc.id + ".txt";
            rec.fileSize = tc.text.size();
            rec.sha256 = hash;
            rec.detectedType = "text/plain";
            rec.storagePath = filePath;
            rec.sourcePath = "C:/source/" + tc.id + ".txt";
            rec.status = "READY";
            store.SaveDraftAttachment("proj-eng", rec);

            auto res = store.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-eng", tc.id });
            EXPECT(res.found);
            EXPECT(res.inspection.isManaged);
            EXPECT(res.inspection.integrityVerified);
            EXPECT(res.inspection.fileSize == tc.text.size());
            EXPECT(res.inspection.sha256 == hash);

            if (tc.expectEligible)
            {
                EXPECT(res.inspection.previewEligible);
                EXPECT(res.inspection.previewContent == tc.text);
                EXPECT(res.inspection.previewUnavailableReasonEn.empty());
            }
            else
            {
                // Metadata inspection MUST be preserved, but content preview withheld!
                EXPECT(!res.inspection.previewEligible);
                EXPECT(res.inspection.previewContent.empty());
                EXPECT(res.inspection.previewUnavailableReasonEn.find("non-English") != std::string::npos);
            }
        }
        CASES_END();
    }

    // -------------------------------------------------------------------------
    // Test 7: Inspection without original source file access and zero filesystem mutations
    // -------------------------------------------------------------------------
    {
        CASES_BEGIN("Test 7: Inspection without original source file access and zero filesystem mutations");
        mayasaba::Storage store(folder / "readonly_inspect.db");
        store.BindProject("proj-ro", "Readonly Inspect", "C:/RO");
        auto attDir = store.ManagedAttachmentRoot();
        fs::create_directories(attDir);

        // 7.1 Source file does NOT exist on disk: inspection must still succeed!
        std::string stagedFile = (attDir / "managed_copy.txt").string();
        std::string text = "This is a verified English document in managed isolated storage.";
        {
            std::ofstream of(stagedFile, std::ios::binary);
            of.write(text.data(), text.size());
        }
        std::string hash = ComputeSha256ForTestFile(stagedFile);

        mayasaba::AttachmentRecord recNoSrc{};
        recNoSrc.id = "att-no-src";
        recNoSrc.projectId = "proj-ro";
        recNoSrc.fileName = "managed_copy.txt";
        recNoSrc.fileSize = text.size();
        recNoSrc.sha256 = hash;
        recNoSrc.detectedType = "text/plain";
        recNoSrc.storagePath = stagedFile;
        recNoSrc.sourcePath = "Z:/definitely/nonexistent/disk/drive/never_existed.txt";
        recNoSrc.status = "READY";
        store.SaveDraftAttachment("proj-ro", recNoSrc);

        auto resNoSrc = store.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ro", "att-no-src" });
        EXPECT(resNoSrc.found);
        EXPECT(resNoSrc.inspection.isManaged);
        EXPECT(resNoSrc.inspection.integrityVerified);
        EXPECT(resNoSrc.inspection.previewEligible);
        EXPECT(resNoSrc.inspection.previewContent == text);

        // 7.2 Source file DOES exist but is locked EXCLUSIVELY (0 share mode):
        // If inspection tried to open the source file, it would fail or crash with sharing violation.
        std::string lockedSourceFile = (folder / "locked_source.txt").string();
        {
            std::ofstream of(lockedSourceFile, std::ios::binary);
            of.write("original source bytes", 21);
        }
        std::wstring wLockedSource = fs::path(lockedSourceFile).wstring();
        HANDLE hLocked = CreateFileW(wLockedSource.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        EXPECT(hLocked != INVALID_HANDLE_VALUE);

        mayasaba::AttachmentRecord recLockedSrc{};
        recLockedSrc.id = "att-locked-src";
        recLockedSrc.projectId = "proj-ro";
        recLockedSrc.fileName = "managed_copy2.txt";
        recLockedSrc.fileSize = text.size();
        recLockedSrc.sha256 = hash;
        recLockedSrc.detectedType = "text/plain";
        recLockedSrc.storagePath = stagedFile;
        recLockedSrc.sourcePath = lockedSourceFile;
        recLockedSrc.status = "READY";
        store.SaveDraftAttachment("proj-ro", recLockedSrc);

        auto resLockedSrc = store.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ro", "att-locked-src" });
        EXPECT(resLockedSrc.found);
        EXPECT(resLockedSrc.inspection.isManaged);
        EXPECT(resLockedSrc.inspection.integrityVerified);
        EXPECT(resLockedSrc.inspection.previewEligible);
        EXPECT(resLockedSrc.inspection.previewContent == text);

        CloseHandle(hLocked);

        // 7.3 Zero filesystem mutations:
        // When managed storage directory does NOT exist, InspectAttachment must NOT create it.
        auto cleanSubdir = folder / "clean_empty_parent";
        fs::create_directories(cleanSubdir);
        auto nonExistentManagedRoot = cleanSubdir / "attachments";
        EXPECT(!fs::exists(nonExistentManagedRoot));

        mayasaba::Storage storeEmpty(cleanSubdir / "store.db");
        storeEmpty.BindProject("proj-empty", "Empty Proj", "C:/Empty");

        mayasaba::AttachmentRecord recEmpty{};
        recEmpty.id = "att-empty";
        recEmpty.projectId = "proj-empty";
        recEmpty.fileName = "test.txt";
        recEmpty.fileSize = 10;
        recEmpty.sha256 = "abc";
        recEmpty.detectedType = "text/plain";
        recEmpty.storagePath = (nonExistentManagedRoot / "test.txt").string();
        recEmpty.sourcePath = "C:/source/test.txt";
        recEmpty.status = "READY";
        storeEmpty.SaveDraftAttachment("proj-empty", recEmpty);

        auto resEmpty = storeEmpty.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-empty", "att-empty" });
        EXPECT(resEmpty.found);
        EXPECT(!resEmpty.inspection.isManaged);
        EXPECT(!resEmpty.inspection.previewEligible);
        EXPECT(resEmpty.inspection.previewUnavailableReasonEn == "Managed attachment storage directory is unavailable.");
        // Crucial check: nonExistentManagedRoot was NOT created!
        EXPECT(!fs::exists(nonExistentManagedRoot));
        CASES_END();
    }

    // -------------------------------------------------------------------------
    // Test 8: Continuation token invalidation when participating records change between pages
    // -------------------------------------------------------------------------
    {
        CASES_BEGIN("Test 8: Continuation token invalidation when participating records change between pages");
        mayasaba::Storage store(folder / "continuation_consistency.db");
        store.BindProject("proj-pages", "Pages Proj", "C:/Pages");
        store.SetClosure("proj-pages", 0, false);

        // Populate initial set of 5 failed draft attachments
        for (int i = 1; i <= 5; ++i)
        {
            mayasaba::AttachmentRecord att{};
            att.id = "att-p" + std::to_string(i);
            att.projectId = "proj-pages";
            att.fileName = "file" + std::to_string(i) + ".txt";
            att.fileSize = 100;
            att.sha256 = "hash" + std::to_string(i);
            att.detectedType = "text/plain";
            att.storagePath = "";
            att.sourcePath = "C:/source/file" + std::to_string(i) + ".txt";
            att.status = "REJECTED";
            att.errorMessage = "Failed validation";
            store.SaveDraftAttachment("proj-pages", att);
        }

        // Query Page 1 with pageSize = 2
        mayasaba::ActionCenterQuery q1{ "proj-pages", 2, "" };
        auto r1 = store.QueryActionCenter(q1);
        EXPECT(r1.items.size() == 2);
        EXPECT(!r1.isComplete);
        EXPECT(!r1.continuationToken.empty());
        std::string token1 = r1.continuationToken;

        // Query Page 2 immediately without mutations: should succeed
        mayasaba::ActionCenterQuery q2{ "proj-pages", 2, token1 };
        auto r2 = store.QueryActionCenter(q2);
        EXPECT(r2.items.size() == 2);
        EXPECT(!r2.isComplete);
        std::string token2 = r2.continuationToken;

        // 8A. Mutation between pages: INSERTION of a new failed attachment
        {
            mayasaba::AttachmentRecord attNew{};
            attNew.id = "att-p-new";
            attNew.projectId = "proj-pages";
            attNew.fileName = "new.txt";
            attNew.fileSize = 50;
            attNew.sha256 = "hashnew";
            attNew.detectedType = "text/plain";
            attNew.storagePath = "";
            attNew.sourcePath = "C:/source/new.txt";
            attNew.status = "REJECTED";
            store.SaveDraftAttachment("proj-pages", attNew);

            // Re-querying with token1 MUST return incomplete with expiration error
            auto resMut1 = store.QueryActionCenter(mayasaba::ActionCenterQuery{ "proj-pages", 2, token1 });
            EXPECT(!resMut1.isComplete);
            EXPECT(resMut1.errorMessage.find("participating record set mutated") != std::string::npos);
        }

        // Fresh Page 1 query to get new token
        auto fresh1 = store.QueryActionCenter(q1);
        std::string freshToken1 = fresh1.continuationToken;

        // 8B. Mutation between pages: REMOVAL of an attachment
        {
            mayasaba::AttachmentRecord attMod{};
            attMod.id = "att-p-new";
            attMod.projectId = "proj-pages";
            attMod.fileName = "new.txt";
            attMod.fileSize = 50;
            attMod.sha256 = "hashnew";
            attMod.detectedType = "text/plain";
            attMod.storagePath = "";
            attMod.sourcePath = "C:/source/new.txt";
            attMod.status = "READY"; // No longer failed!
            store.SaveDraftAttachment("proj-pages", attMod);

            auto resMut2 = store.QueryActionCenter(mayasaba::ActionCenterQuery{ "proj-pages", 2, freshToken1 });
            EXPECT(!resMut2.isComplete);
            EXPECT(resMut2.errorMessage.find("participating record set mutated") != std::string::npos);
        }

        // Fresh Page 1 query to get new token
        auto fresh2 = store.QueryActionCenter(q1);
        std::string freshToken2 = fresh2.continuationToken;

        // 8C. Mutation between pages: STATUS CHANGE of an attachment (REJECTED -> SOURCE_CHANGED)
        {
            mayasaba::AttachmentRecord attMod{};
            attMod.id = "att-p1";
            attMod.projectId = "proj-pages";
            attMod.fileName = "file1.txt";
            attMod.fileSize = 100;
            attMod.sha256 = "hash1";
            attMod.detectedType = "text/plain";
            attMod.storagePath = "";
            attMod.sourcePath = "C:/source/file1.txt";
            attMod.status = "SOURCE_CHANGED";
            store.SaveDraftAttachment("proj-pages", attMod);

            auto resMut3 = store.QueryActionCenter(mayasaba::ActionCenterQuery{ "proj-pages", 2, freshToken2 });
            EXPECT(!resMut3.isComplete);
            EXPECT(resMut3.errorMessage.find("participating record set mutated") != std::string::npos);
        }

        // 8D. PENDING SUBMISSION ADDED. A pending row can only be created while the draft
        // is complete: Storage refuses a manifest that would silently omit a failed
        // attachment. The state this case previously built - a pending row inserted over a
        // draft of failed attachments - is therefore no longer reachable through the
        // production API, and the projection must stay exactly as it was.
        // Token invalidation for insertion, removal and status change is covered by
        // 8A/8B/8C; this case now asserts the admission boundary that replaced it.
        {
            bool stagedOverFailedDraft = false;
            try { store.StageSubmission("proj-pages", "sub-between-pages", "unresolved send"); }
            catch (std::exception const&) { stagedOverFailedDraft = true; }
            EXPECT(stagedOverFailedDraft);
            EXPECT(!store.Pending("proj-pages").has_value());

            auto afterRefusal = store.QueryActionCenter(mayasaba::ActionCenterQuery{ "proj-pages", 50 });
            bool hasPendingItem = false;
            for (auto const& item : afterRefusal.items)
            {
                if (item.kind == mayasaba::ActionCenterKind::UnresolvedSubmission) hasPendingItem = true;
            }
            EXPECT(!hasPendingItem);
        }
        CASES_END();
    }

    // -------------------------------------------------------------------------
    // Test 9: Deterministic in-flight inspection-mutation race handling under mutex
    // -------------------------------------------------------------------------
    {
        CASES_BEGIN("Test 9: Deterministic in-flight inspection-mutation race handling under mutex");
        mayasaba::Storage store(folder / "race_inspect.db");
        store.BindProject("proj-race", "Race Proj", "C:/Race");
        auto attDir = store.ManagedAttachmentRoot();
        fs::create_directories(attDir);

        std::string filePath = (attDir / "att-race-1.txt").string();
        std::string testContent = "Valid English payload for race verification.";
        {
            std::ofstream of(filePath, std::ios::binary);
            of.write(testContent.data(), testContent.size());
        }
        std::string hash = ComputeSha256ForTestFile(filePath);

        mayasaba::AttachmentRecord rec{};
        rec.id = "att-race-1";
        rec.projectId = "proj-race";
        rec.fileName = "att-race-1.txt";
        rec.fileSize = testContent.size();
        rec.sha256 = hash;
        rec.detectedType = "text/plain";
        rec.storagePath = filePath;
        rec.sourcePath = "C:/source/att-race-1.txt";
        rec.status = "READY";
        store.SaveDraftAttachment("proj-race", rec);

        // 9.1 Baseline: inspection without race succeeds
        auto resBase = store.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-race", "att-race-1" });
        EXPECT(resBase.found);
        EXPECT(resBase.inspection.previewEligible);

        // 9.2 In-flight mutation: attachment version mutated while hashing was in flight
        bool hookFired = false;
        store.SetInspectionFileHashedHookForTesting([&]() {
            hookFired = true;
            mayasaba::AttachmentRecord mod = rec;
            mod.status = "REJECTED"; // Mutated in SQLite while inspection was in flight
            mod.errorMessage = "Source corrupted concurrently";
            store.SaveDraftAttachment("proj-race", mod);
        });

        auto resMut = store.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-race", "att-race-1" });
        EXPECT(hookFired);
        EXPECT(!resMut.found);
        EXPECT(resMut.errorMessage.find("during inspection") != std::string::npos);

        // 9.3 In-flight deletion: attachment deleted while hashing was in flight
        hookFired = false;
        store.SaveDraftAttachment("proj-race", rec); // Restore to READY
        store.SetInspectionFileHashedHookForTesting([&]() {
            hookFired = true;
            store.RemoveDraftAttachment("proj-race", "att-race-1");
        });

        auto resDel = store.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-race", "att-race-1" });
        EXPECT(hookFired);
        EXPECT(!resDel.found);
        EXPECT(resDel.errorMessage.find("during inspection") != std::string::npos);

        store.SetInspectionFileHashedHookForTesting(nullptr);
        CASES_END();
    }

    // -------------------------------------------------------------------------
    // Test 10: Atomic SQLite read transaction snapshot in QueryActionCenter
    // -------------------------------------------------------------------------
    {
        CASES_BEGIN("Test 10: Atomic SQLite read transaction snapshot in QueryActionCenter");
        mayasaba::Storage store(folder / "snapshot_consistency.db");
        store.BindProject("proj-snap", "Snap Proj", "C:/Snap");
        store.SetClosure("proj-snap", 0, false);

        for (int i = 1; i <= 3; ++i)
        {
            mayasaba::AttachmentRecord att{};
            att.id = "att-s" + std::to_string(i);
            att.projectId = "proj-snap";
            att.fileName = "snap" + std::to_string(i) + ".txt";
            att.fileSize = 100;
            att.sha256 = "hash" + std::to_string(i);
            att.detectedType = "text/plain";
            att.storagePath = "";
            att.sourcePath = "C:/source/snap" + std::to_string(i) + ".txt";
            att.status = "REJECTED";
            store.SaveDraftAttachment("proj-snap", att);
        }

        // Test concurrent writer hook right after signature collection, inside read transaction:
        // Because of BEGIN DEFERRED read transaction snapshot, the unified query MUST see the
        // snapshot captured at the start of the transaction, and the token signature matches!
        bool hookRan = false;
        store.SetQueryActionCenterSnapshotHookForTesting([&]() {
            hookRan = true;
            // Another connection or thread mutates the store externally
            sqlite3* dbExt = nullptr;
            std::string dbPath = (folder / "snapshot_consistency.db").string();
            if (sqlite3_open(dbPath.c_str(), &dbExt) == SQLITE_OK)
            {
                sqlite3_exec(dbExt, "INSERT INTO pending_submissions (submission_id, project_id, body) VALUES ('sub-ext-writer', 'proj-snap', 'external writer body');", nullptr, nullptr, nullptr);
                sqlite3_close(dbExt);
            }
        });

        mayasaba::ActionCenterQuery q1{ "proj-snap", 2, "" };
        auto r1 = store.QueryActionCenter(q1);
        EXPECT(hookRan);
        EXPECT(r1.items.size() == 2);
        EXPECT(!r1.isComplete);
        EXPECT(!r1.continuationToken.empty());

        // When Page 2 is subsequently requested, the continuation token (built with the old signature)
        // is validated against a NEW read snapshot containing the external writer's commit:
        store.SetQueryActionCenterSnapshotHookForTesting(nullptr);
        mayasaba::ActionCenterQuery q2{ "proj-snap", 2, r1.continuationToken };
        auto r2 = store.QueryActionCenter(q2);
        EXPECT(!r2.isComplete);
        EXPECT(r2.errorMessage.find("participating record set mutated between pages") != std::string::npos);
        CASES_END();
    }

    // -------------------------------------------------------------------------
    // Test 11: Incomplete / malformed preparation result sets
    // A preparation that throws, is cancelled mid-batch, or returns a result set that
    // cannot be attributed to the selection must never commit stranded PREPARING
    // placeholders and report success. The selection stays recoverable.
    // -------------------------------------------------------------------------
    {
        CASES_BEGIN("Test 11: Incomplete / malformed preparation result sets");
        using Outcome = mayasaba::AttachmentIngestionCoordinator::BatchCommitOutcome;
        namespace fs2 = std::filesystem;

        mayasaba::Storage store(folder / "incomplete_prep.db");
        store.BindProject("proj-inc", "Incomplete Prep", "C:/Inc");
        auto stagingDir = folder / "incomplete_staging";
        fs2::create_directories(stagingDir);
        mayasaba::AttachmentService attService(store, stagingDir);
        mayasaba::AttachmentIngestionCoordinator coordinator;

        auto srcDir = folder / "incomplete_sources";
        fs2::create_directories(srcDir);
        auto src1 = srcDir / "first.txt";
        auto src2 = srcDir / "second.txt";
        {
            std::ofstream a(src1); a << "First attachment payload for incomplete batch testing.\n";
            std::ofstream b(src2); b << "Second attachment payload for incomplete batch testing.\n";
        }

        std::atomic<bool> windowClosed{false};
        std::atomic_uint64_t gen{1};
        auto ctx = coordinator.CreateContext("proj-inc", gen.load(), &windowClosed, &gen);

        auto makeSlot = [](std::string const& id, std::wstring const& path, std::string const& name) {
            mayasaba::AttachmentCandidate c;
            c.id = id;
            c.originalPath = path;
            c.fileName = name;
            c.status = mayasaba::AttachmentStatus::Preparing;
            return c;
        };

        // 11.1 First-item failure: preparation threw before producing any result.
        {
            std::vector<mayasaba::AttachmentCandidate> drafts = {
                makeSlot("slot-1", src1.wstring(), "first.txt"),
                makeSlot("slot-2", src2.wstring(), "second.txt")
            };
            std::vector<std::string> ids = { "slot-1", "slot-2" };
            std::string err;
            Outcome outcome = Outcome::Committed;
            bool saved = false;
            bool const committed = coordinator.ValidateAndCommitPreparedBatch(
                ctx, "proj-inc", gen.load(), false, drafts, {}, ids,
                [&](std::string const& p, std::vector<mayasaba::AttachmentCandidate> const& d) {
                    saved = true;
                    attService.SaveDraftAttachments(p, d);
                },
                &err, &outcome);

            // An empty result set for two selected files is an incomplete preparation, not
            // a successful commit of untouched placeholders.
            EXPECT(outcome == Outcome::IncompletePreparation);
            EXPECT(committed == true);          // recoverable portion still persisted
            EXPECT(saved == true);
            EXPECT(!err.empty());
            EXPECT(err.find("missing") != std::string::npos);

            // Every placeholder is now an explicit, recoverable failure that keeps its
            // identity and source path.
            EXPECT(drafts.size() == 2);
            for (auto const& d : drafts)
            {
                EXPECT(d.status == mayasaba::AttachmentStatus::Rejected);
                EXPECT(!d.errorMessage.empty());
                EXPECT(!d.originalPath.empty());
            }
            EXPECT(drafts[0].id == "slot-1");
            EXPECT(drafts[1].id == "slot-2");

            // Durable state matches memory: nothing is left as PREPARING.
            auto durable = attService.LoadDraftAttachments("proj-inc");
            EXPECT(durable.size() == 2);
            for (auto const& d : durable)
            {
                EXPECT(d.status == mayasaba::AttachmentStatus::Rejected);
            }
        }

        // 11.2 Later-item failure: the first file prepared, the second did not.
        {
            auto firstResult = attService.PrepareAttachment("proj-inc", src1.wstring());
            EXPECT(firstResult.status == mayasaba::AttachmentStatus::Ready);

            std::vector<mayasaba::AttachmentCandidate> drafts = attService.LoadDraftAttachments("proj-inc");
            drafts.push_back(makeSlot("slot-a", src1.wstring(), "first.txt"));
            drafts.push_back(makeSlot("slot-b", src2.wstring(), "second.txt"));
            std::vector<std::string> ids = { "slot-a", "slot-b" };

            std::string err;
            Outcome outcome = Outcome::Committed;
            bool const committed = coordinator.ValidateAndCommitPreparedBatch(
                ctx, "proj-inc", gen.load(), false, drafts, { firstResult }, ids,
                [&](std::string const& p, std::vector<mayasaba::AttachmentCandidate> const& d) {
                    attService.SaveDraftAttachments(p, d);
                },
                &err, &outcome);

            EXPECT(outcome == Outcome::IncompletePreparation);
            EXPECT(committed == true);
            EXPECT(!err.empty());

            // The prepared file is admitted; the unanswered one is an explicit failure.
            auto const& a = drafts[drafts.size() - 2];
            auto const& b = drafts[drafts.size() - 1];
            EXPECT(a.id == firstResult.id);
            EXPECT(a.status == mayasaba::AttachmentStatus::Ready);
            EXPECT(b.id == "slot-b");
            EXPECT(b.status == mayasaba::AttachmentStatus::Rejected);

            auto durable = attService.LoadDraftAttachments("proj-inc");
            bool sawReady = false, sawRejectedB = false;
            for (auto const& d : durable)
            {
                if (d.id == firstResult.id && d.status == mayasaba::AttachmentStatus::Ready) sawReady = true;
                if (d.id == "slot-b" && d.status == mayasaba::AttachmentStatus::Rejected) sawRejectedB = true;
                EXPECT(d.status != mayasaba::AttachmentStatus::Preparing);
            }
            EXPECT(sawReady);
            EXPECT(sawRejectedB);

            attService.RemoveDraftAttachment("proj-inc", firstResult.id);
        }

        // 11.3 Explicit rejected results are a COMPLETED preparation, not an incomplete one.
        // The batch produced one result per selected file; each is a definite outcome.
        {
            auto bad = attService.PrepareAttachment("proj-inc", (srcDir / "does-not-exist.txt").wstring());
            EXPECT(bad.status == mayasaba::AttachmentStatus::Rejected);

            std::vector<mayasaba::AttachmentCandidate> drafts;
            drafts.push_back(makeSlot("slot-r", (srcDir / "does-not-exist.txt").wstring(), "does-not-exist.txt"));

            std::string err;
            Outcome outcome = Outcome::Committed;
            bool const committed = coordinator.ValidateAndCommitPreparedBatch(
                ctx, "proj-inc", gen.load(), false, drafts, { bad }, { "slot-r" },
                [&](std::string const& p, std::vector<mayasaba::AttachmentCandidate> const& d) {
                    attService.SaveDraftAttachments(p, d);
                },
                &err, &outcome);

            EXPECT(outcome == Outcome::Committed);
            EXPECT(committed == true);
            EXPECT(err.empty());
            EXPECT(drafts.size() == 1);
            EXPECT(drafts[0].status == mayasaba::AttachmentStatus::Rejected);
            EXPECT(!drafts[0].errorMessage.empty());
        }

        // 11.4 Malformed correspondence: more results than selected files is refused with no
        // mutation and no persistence.
        {
            std::vector<mayasaba::AttachmentCandidate> drafts = {
                makeSlot("slot-m", src1.wstring(), "first.txt")
            };
            std::vector<mayasaba::AttachmentCandidate> tooMany = {
                attService.PrepareAttachment("proj-inc", src1.wstring()),
                attService.PrepareAttachment("proj-inc", src2.wstring())
            };
            std::string err;
            Outcome outcome = Outcome::Committed;
            bool saved = false;
            bool const committed = coordinator.ValidateAndCommitPreparedBatch(
                ctx, "proj-inc", gen.load(), false, drafts, tooMany, { "slot-m" },
                [&](std::string const& p, std::vector<mayasaba::AttachmentCandidate> const& d) {
                    saved = true;
                    attService.SaveDraftAttachments(p, d);
                },
                &err, &outcome);

            EXPECT(committed == false);
            EXPECT(saved == false);
            EXPECT(outcome == Outcome::MalformedResult);
            EXPECT(!err.empty());
            EXPECT(drafts.size() == 1);
            EXPECT(drafts[0].status == mayasaba::AttachmentStatus::Preparing);
        }

        // 11.5 Duplicate placeholder identities cannot be attributed to files.
        {
            auto one = attService.PrepareAttachment("proj-inc", src1.wstring());
            std::vector<mayasaba::AttachmentCandidate> drafts = {
                makeSlot("dup", src1.wstring(), "first.txt"),
                makeSlot("dup", src2.wstring(), "second.txt")
            };
            std::string err;
            Outcome outcome = Outcome::Committed;
            bool saved = false;
            bool const committed = coordinator.ValidateAndCommitPreparedBatch(
                ctx, "proj-inc", gen.load(), false, drafts, { one }, { "dup", "dup" },
                [&](std::string const& p, std::vector<mayasaba::AttachmentCandidate> const& d) {
                    saved = true;
                    attService.SaveDraftAttachments(p, d);
                },
                &err, &outcome);

            EXPECT(committed == false);
            EXPECT(saved == false);
            EXPECT(outcome == Outcome::MalformedResult);
            EXPECT(err.find("duplicate") != std::string::npos);
        }

        // 11.6 Stale completion is still refused and never persists.
        {
            std::vector<mayasaba::AttachmentCandidate> drafts = {
                makeSlot("slot-s", src1.wstring(), "first.txt")
            };
            std::string err;
            Outcome outcome = Outcome::Committed;
            bool saved = false;
            bool const committed = coordinator.ValidateAndCommitPreparedBatch(
                ctx, "proj-inc", gen.load() + 1, false, drafts,
                { attService.PrepareAttachment("proj-inc", src1.wstring()) }, { "slot-s" },
                [&](std::string const& p, std::vector<mayasaba::AttachmentCandidate> const& d) {
                    saved = true;
                    attService.SaveDraftAttachments(p, d);
                },
                &err, &outcome);

            EXPECT(committed == false);
            EXPECT(saved == false);
            EXPECT(outcome == Outcome::StaleCompletion);
            EXPECT(drafts[0].status == mayasaba::AttachmentStatus::Preparing);
        }

        // 11.7 Persistence failure leaves the draft list unmutated and reports the reason.
        {
            std::vector<mayasaba::AttachmentCandidate> drafts = {
                makeSlot("slot-p", src1.wstring(), "first.txt")
            };
            std::string err;
            Outcome outcome = Outcome::Committed;
            bool const committed = coordinator.ValidateAndCommitPreparedBatch(
                ctx, "proj-inc", gen.load(), false, drafts,
                { attService.PrepareAttachment("proj-inc", src1.wstring()) }, { "slot-p" },
                [&](std::string const&, std::vector<mayasaba::AttachmentCandidate> const&) {
                    throw std::runtime_error("simulated persistence failure");
                },
                &err, &outcome);

            EXPECT(committed == false);
            EXPECT(outcome == Outcome::PersistenceFailed);
            EXPECT(err.find("simulated persistence failure") != std::string::npos);
            EXPECT(drafts.size() == 1);
            EXPECT(drafts[0].status == mayasaba::AttachmentStatus::Preparing);
        }

        // 11.8 Prior drafts survive an incomplete batch.
        {
            std::vector<mayasaba::AttachmentCandidate> drafts = attService.LoadDraftAttachments("proj-inc");
            size_t const before = drafts.size();
            drafts.push_back(makeSlot("slot-x", src1.wstring(), "first.txt"));

            std::string err;
            Outcome outcome = Outcome::Committed;
            coordinator.ValidateAndCommitPreparedBatch(
                ctx, "proj-inc", gen.load(), false, drafts, {}, { "slot-x" },
                [&](std::string const& p, std::vector<mayasaba::AttachmentCandidate> const& d) {
                    attService.SaveDraftAttachments(p, d);
                },
                &err, &outcome);

            EXPECT(outcome == Outcome::IncompletePreparation);
            EXPECT(drafts.size() == before + 1);
            for (size_t i = 0; i < before; ++i)
            {
                EXPECT(drafts[i].id == attService.LoadDraftAttachments("proj-inc")[i].id);
            }
        }
        CASES_END();
    }

    // -------------------------------------------------------------------------
    // Test 12: Staging reconciliation must not reclaim live or unprovable files
    // -------------------------------------------------------------------------
    {
        CASES_BEGIN("Test 12: Staging reconciliation must not reclaim live or unprovable files");
        namespace fs2 = std::filesystem;
        mayasaba::Storage store(folder / "reconcile.db");
        store.BindProject("proj-rec", "Reconcile", "C:/Rec");
        auto stagingDir = folder / "reconcile_staging";
        fs2::create_directories(stagingDir);
        mayasaba::AttachmentService attService(store, stagingDir);

        auto srcDir = folder / "reconcile_sources";
        fs2::create_directories(srcDir);
        auto src = srcDir / "live.txt";
        { std::ofstream o(src); o << "Live preparation payload for reconciliation testing.\n"; }

        auto projectStaging = stagingDir / "staging" / "proj-rec";

        // 12.1 A live PREPARING record and its staged copy survive a pass that is told
        // preparation is active.
        auto live = attService.PrepareAttachment("proj-rec", src.wstring());
        EXPECT(live.status == mayasaba::AttachmentStatus::Ready);
        EXPECT(fs2::exists(live.stagedPath));

        // Rewrite the durable record back into an in-flight intent pointing at the same
        // staged copy, which is exactly what a genuinely running batch looks like.
        {
            mayasaba::AttachmentRecord liveIntent;
            liveIntent.id = live.id;
            liveIntent.projectId = "proj-rec";
            liveIntent.fileName = live.fileName;
            liveIntent.fileSize = live.fileSize;
            liveIntent.detectedType = live.detectedType;
            liveIntent.storagePath = fs2::path(live.stagedPath).string();
            liveIntent.status = "PREPARING";
            store.SaveDraftAttachment("proj-rec", liveIntent);
        }

        attService.ReconcileStagedFiles("proj-rec", /*preparationActive=*/true);
        EXPECT(fs2::exists(live.stagedPath));
        auto stillThere = attService.LoadDraftAttachments("proj-rec");
        bool foundPreparing = false;
        for (auto const& d : stillThere) if (d.id == live.id && d.status == mayasaba::AttachmentStatus::Preparing) foundPreparing = true;
        EXPECT(foundPreparing);

        // 12.2 With no preparation active, the abandoned intent is reclaimed and its staged
        // copy is removed.
        attService.ReconcileStagedFiles("proj-rec", /*preparationActive=*/false);
        EXPECT(!fs2::exists(live.stagedPath));
        bool gone = true;
        for (auto const& d : attService.LoadDraftAttachments("proj-rec")) if (d.id == live.id) gone = false;
        EXPECT(gone);

        // 12.3 Tracked READY drafts and their staged copies are never reclaimed.
        auto kept = attService.PrepareAttachment("proj-rec", src.wstring());
        EXPECT(kept.status == mayasaba::AttachmentStatus::Ready);
        attService.ReconcileStagedFiles("proj-rec", false);
        EXPECT(fs2::exists(kept.stagedPath));
        auto keptDrafts = attService.LoadDraftAttachments("proj-rec");
        EXPECT(keptDrafts.size() == 1);
        EXPECT(keptDrafts[0].id == kept.id);
        EXPECT(keptDrafts[0].status == mayasaba::AttachmentStatus::Ready);

        // 12.4 A truly untracked file in the project staging directory is reclaimed.
        auto orphan = projectStaging / "orphan.bin";
        { std::ofstream o(orphan); o << "orphan"; }
        EXPECT(fs2::exists(orphan));
        attService.ReconcileStagedFiles("proj-rec", false);
        EXPECT(!fs2::exists(orphan));
        EXPECT(fs2::exists(kept.stagedPath));

        // 12.5 Reverification with active preparation leaves live PREPARING records alone
        // rather than converting in-flight work into a spurious failure.
        store.SaveDraftAttachment("proj-rec", [&] {
            mayasaba::AttachmentRecord live2;
            live2.id = "att-live-2";
            live2.projectId = "proj-rec";
            live2.fileName = "live2.txt";
            live2.fileSize = 10;
            live2.detectedType = "text/plain";
            live2.storagePath = (projectStaging / "att-live-2_live2.txt").string();
            live2.status = "PREPARING";
            return live2;
        }());
        auto reverified = attService.ReverifyDraftAttachments("proj-rec", /*preparationActive=*/true);
        bool livePreserved = false;
        for (auto const& d : reverified) if (d.id == "att-live-2") livePreserved = (d.status == mayasaba::AttachmentStatus::Preparing);
        EXPECT(livePreserved);

        // With preparation finished the same record is either reclaimed or reported as an
        // explicit failure — never silently Ready.
        auto reverified2 = attService.ReverifyDraftAttachments("proj-rec", false);
        for (auto const& d : reverified2)
        {
            if (d.id == "att-live-2") EXPECT(d.status == mayasaba::AttachmentStatus::Rejected);
        }
        CASES_END();
    }

    // -------------------------------------------------------------------------
    // Test 13: A failed recovery must not hide the attachments the draft had
    // -------------------------------------------------------------------------
    {
        CASES_BEGIN("Test 13: Failed recovery preserves prior drafts or retains reload results");

        auto makeSlot = [](std::string const& id, std::wstring const& path, std::string const& name) {
            mayasaba::AttachmentCandidate c;
            c.id = id;
            c.fileName = name;
            c.originalPath = path;
            c.status = mayasaba::AttachmentStatus::Ready;
            return c;
        };

        std::vector<mayasaba::AttachmentCandidate> const previous = {
            makeSlot("existing-1", L"C:/src/first.txt", "first.txt"),
            makeSlot("existing-2", L"C:/src/second.txt", "second.txt")
        };

        // 13.1 A reload that failed falls back to the snapshot from before the batch, so
        // an unreadable store cannot hide the chips the draft already had.
        {
            auto const result = mayasaba::AttachmentIngestionCoordinator::ResolveRecoveryDrafts(
                {}, false, true, previous);
            EXPECT(result.size() == previous.size());
            EXPECT(result[0].id == "existing-1");
            EXPECT(result[1].id == "existing-2");
        }

        // 13.2 A cleanup failure does not invalidate a reload that succeeded: the reloaded
        // truth is the set that survives, and the cleanup failure is reported instead.
        {
            std::vector<mayasaba::AttachmentCandidate> reloaded = {
                makeSlot("existing-1", L"C:/src/first.txt", "first.txt"),
                makeSlot("reloaded-2", L"C:/src/third.txt", "third.txt")
            };
            auto const result = mayasaba::AttachmentIngestionCoordinator::ResolveRecoveryDrafts(
                reloaded, true, false, previous);
            EXPECT(result.size() == 2);
            EXPECT(result[0].id == "existing-1");
            EXPECT(result[1].id == "reloaded-2");
        }

        // 13.3 A fully successful recovery keeps the reloaded truth unchanged.
        {
            std::vector<mayasaba::AttachmentCandidate> reloaded = {
                makeSlot("reloaded-1", L"C:/src/only.txt", "only.txt")
            };
            auto const result = mayasaba::AttachmentIngestionCoordinator::ResolveRecoveryDrafts(
                reloaded, true, true, previous);
            EXPECT(result.size() == 1);
            EXPECT(result[0].id == "reloaded-1");
        }

        // 13.4 Both failed: the snapshot still stands, because nothing readable
        // replaced it.
        {
            auto const result = mayasaba::AttachmentIngestionCoordinator::ResolveRecoveryDrafts(
                {}, false, false, previous);
            EXPECT(result.size() == previous.size());
            EXPECT(result[1].id == "existing-2");
        }

        CASES_END();
    }

    // -------------------------------------------------------------------------
    // Test 14: A stale completion must not deny the write that completed
    // -------------------------------------------------------------------------
    {
        CASES_BEGIN("Test 14: Stale completion distinguishes a persisted batch from a cancellation");
        using Report = mayasaba::AttachmentIngestionCoordinator::StaleCompletionReport;

        // 14.1 The write completed: the files exist as durable records under the previous
        // project, so the completion may not claim nothing was added.
        EXPECT(mayasaba::AttachmentIngestionCoordinator::ClassifyStaleCompletion(true, true) ==
            Report::PersistedUnderPreviousProject);

        // 14.2 The write was never attempted: nothing was added, and that is what must be
        // said.
        EXPECT(mayasaba::AttachmentIngestionCoordinator::ClassifyStaleCompletion(false, false) ==
            Report::NothingPersisted);

        // 14.3 An attempted write that failed persisted nothing durable; the caller turns
        // that into a persistence failure rather than a stale completion.
        EXPECT(mayasaba::AttachmentIngestionCoordinator::ClassifyStaleCompletion(true, false) ==
            Report::NothingPersisted);

        CASES_END();
    }

    fs::remove_all(folder);
    // The summary is derived from the cases that actually ran, not a fixed count.
    auto const& cases = Cases();
    if (cases.Failed() != 0)
    {
        std::cerr << "FAIL: " << cases.Passed() << " of " << cases.rows.size()
                  << " verification cases passed" << std::endl;
        cases.Print(std::cerr);
        return 1;
    }
    std::cout << "PASS: " << cases.Passed() << " of " << cases.rows.size()
              << " executed Action Center and attachment inspection verification cases" << std::endl;
    cases.Print(std::cout);
    return 0;
}