#include "../Mayasaba/Storage.h"
#include "../Mayasaba/AttachmentService.h"
#include "../Mayasaba/ActionCenterPresenter.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <set>
#include <sstream>
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
    }

    // Attachment inspection: comprehensive verification outside storage lock
    {
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
    }

    // -------------------------------------------------------------------------
    // Test 3: Production Action Center Presenter & Presentation Invariants
    // - Held query dismissed while pending: completion does not reopen panel (Finding 2)
    // - Paging cleanup & identity race: old-page -> refresh -> new-page -> old-completion (Finding 3)
    // - Refresh sequence capture, supersession by newer refresh, and coalesced publication
    // -------------------------------------------------------------------------
    {
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
    }

    // -------------------------------------------------------------------------
    // Test 4: Action Center expected-version verification at invocation
    // -------------------------------------------------------------------------
    {
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
    }

    // -------------------------------------------------------------------------
    // Test 5: Production AttachmentService file drop ingestion, mixed-batch folder rejection,
    // and project switching cancellation during delayed drop
    // -------------------------------------------------------------------------
    {
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

        // 4. Delayed drop with project switching (A -> B) through actual production ingestion path
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
    }

    // -------------------------------------------------------------------------
    // Test 6: Non-English ASCII prose, mixed language, and digit/underscore dressed
    // prose rejected for preview under English presentation policy
    // -------------------------------------------------------------------------
    {
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
    }

    // -------------------------------------------------------------------------
    // Test 7: Inspection without original source file access and zero filesystem mutations
    // -------------------------------------------------------------------------
    {
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
    }

    // -------------------------------------------------------------------------
    // Test 8: Continuation token invalidation when participating records change between pages
    // -------------------------------------------------------------------------
    {
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

        // Fresh Page 1 query to get new token
        auto fresh3 = store.QueryActionCenter(q1);
        std::string freshToken3 = fresh3.continuationToken;

        // 8D. Mutation between pages: PENDING SUBMISSION ADDED
        {
            store.StageSubmission("proj-pages", "sub-between-pages", "unresolved send");

            auto resMut4 = store.QueryActionCenter(mayasaba::ActionCenterQuery{ "proj-pages", 2, freshToken3 });
            EXPECT(!resMut4.isComplete);
            EXPECT(resMut4.errorMessage.find("participating record set mutated") != std::string::npos);
        }
    }

    // -------------------------------------------------------------------------
    // Test 9: Deterministic in-flight inspection-mutation race handling under mutex
    // -------------------------------------------------------------------------
    {
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
    }

    // -------------------------------------------------------------------------
    // Test 10: Atomic SQLite read transaction snapshot in QueryActionCenter
    // -------------------------------------------------------------------------
    {
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
    }

    fs::remove_all(folder);
    std::cout << "PASS: standalone Action Center + attachment inspection verification (10/10 tests passed)\n";
    return 0;
}
