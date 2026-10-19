#include "../Mayasaba/Storage.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <set>
#include <windows.h>

#define EXPECT(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " at " __FILE__ ":" << __LINE__ << "\n"; \
            return 1; \
        } \
    } while (0)

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
        ac.SetClosure("proj-ac", 2, false);
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

        // Happy-path draft attachment (markdown, valid hash, distinct staged path)
        std::string validDraftFile = (folder / "valid_draft.md").string();
        {
            std::ofstream of(validDraftFile, std::ios::binary);
            of.write("abcd", 4);
        }

        mayasaba::AttachmentRecord draft1{};
        draft1.id = "att-draft-1"; draft1.projectId = "proj-ai"; draft1.fileName = "valid_draft.md";
        draft1.fileSize = 4; draft1.sha256 = "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589";
        draft1.detectedType = "text/markdown"; draft1.storagePath = validDraftFile;
        draft1.sourcePath = "C:/source/valid_draft.md"; draft1.status = "READY"; draft1.errorMessage = "";
        ai.SaveDraftAttachment("proj-ai", draft1);

        mayasaba::AttachmentInspectionQuery qi1{ "proj-ai", "att-draft-1" };
        auto ri1 = ai.InspectAttachment(qi1);
        EXPECT(ri1.found);
        EXPECT(ri1.inspection.attachmentId == "att-draft-1");
        EXPECT(ri1.inspection.projectId == "proj-ai");
        EXPECT(ri1.inspection.fileName == "valid_draft.md");
        EXPECT(ri1.inspection.fileSize == 4);
        EXPECT(ri1.inspection.detectedType == "text/markdown");
        EXPECT(ri1.inspection.storagePath == validDraftFile);
        EXPECT(ri1.inspection.sourceRecordVersion == "1");
        EXPECT(ri1.inspection.isManaged);
        EXPECT(ri1.inspection.integrityVerified);
        EXPECT(ri1.inspection.previewEligible);
        EXPECT(ri1.inspection.previewUnavailableReasonEn.empty());

        // Updating the draft attachment increments sourceRecordVersion
        draft1.fileSize = 4;
        ai.SaveDraftAttachment("proj-ai", draft1);
        auto ri1_v2 = ai.InspectAttachment(qi1);
        EXPECT(ri1_v2.inspection.sourceRecordVersion == "2");

        // Tampered file (disk contents altered: hash/size mismatch)
        std::string tamperedFile = (folder / "tampered.txt").string();
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
        draftMissing.detectedType = "text/plain"; draftMissing.storagePath = (folder / "nonexistent.txt").string();
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
        draftTraversal.detectedType = "text/plain"; draftTraversal.storagePath = (folder / "../escaped.txt").string();
        draftTraversal.sourcePath = "C:/source/escaped.txt"; draftTraversal.status = "READY";
        ai.SaveDraftAttachment("proj-ai", draftTraversal);

        auto riTraversal = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ai", "att-traversal" });
        EXPECT(riTraversal.found);
        EXPECT(!riTraversal.inspection.isManaged);
        EXPECT(!riTraversal.inspection.previewEligible);

        // Invalid managed path: storage path identical to source path (unisolated)
        mayasaba::AttachmentRecord draftUnisolated{};
        draftUnisolated.id = "att-unisolated"; draftUnisolated.projectId = "proj-ai"; draftUnisolated.fileName = "unisolated.txt";
        draftUnisolated.fileSize = 4; draftUnisolated.sha256 = "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589";
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
        draftPdf.fileSize = 4; draftPdf.sha256 = "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589";
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
        std::string committedFile = (folder / "committed.txt").string();
        {
            std::ofstream of(committedFile, std::ios::binary);
            of.write("abcd", 4);
        }
        // Sealed as read-only per committed attachment contract
        std::wstring wCommitted = std::filesystem::path(committedFile).wstring();
        SetFileAttributesW(wCommitted.c_str(), FILE_ATTRIBUTE_READONLY);

        mayasaba::AttachmentRecord draftComm{};
        draftComm.id = "att-committed-1"; draftComm.projectId = "proj-comm"; draftComm.fileName = "committed.txt";
        draftComm.fileSize = 4; draftComm.sha256 = "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589";
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

    fs::remove_all(folder);
    std::cout << "PASS: standalone Action Center + attachment inspection verification\n";
    return 0;
}
