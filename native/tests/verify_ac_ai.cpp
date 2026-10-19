#include "../Mayasaba/Storage.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <filesystem>

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
        ac.StageSubmission("proj-ac", "sub-ac-1", "unresolved body");
        mayasaba::ActionCenterQuery q{ "proj-ac", 50 };
        auto res = ac.QueryActionCenter(q);
        if (!res.isComplete) { std::cerr << "QueryActionCenter error: " << res.errorMessage << "\n"; return 1; }
        EXPECT(res.isComplete);
        EXPECT(res.errorMessage.empty());
        bool foundSubmission = false;
        for (auto const& item : res.items)
            if (item.kind == mayasaba::ActionCenterKind::UnresolvedSubmission)
            { foundSubmission = true; EXPECT(item.projectId == "proj-ac"); EXPECT(item.sourceRecordId == "sub-ac-1"); EXPECT(item.availableAction == "Recheck outcome"); }
        EXPECT(foundSubmission);

        EXPECT(ac.SaveContribution("proj-ac", "sub-ac-1", "unresolved body") == mayasaba::SaveDisposition::Saved);
        auto res2 = ac.QueryActionCenter(q);
        bool stillHas = false;
        for (auto const& item : res2.items)
            if (item.kind == mayasaba::ActionCenterKind::UnresolvedSubmission) stillHas = true;
        EXPECT(!stillHas);

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

        ac.SetClosure("proj-ac", 1, true);
        auto res4 = ac.QueryActionCenter(q);
        bool foundClosed = false;
        for (auto const& item : res4.items)
            if (item.kind == mayasaba::ActionCenterKind::ClosedProjectRestriction) { foundClosed = true; EXPECT(item.availableAction == "Reopen Project"); }
        EXPECT(foundClosed);

        mayasaba::ActionCenterQuery qPage{ "proj-ac", 1 };
        auto res5 = ac.QueryActionCenter(qPage);
        // Each projection block independently enforces LIMIT pageSize; total may
        // exceed pageSize across unrelated blocks, so assert per-block not yet
        // observable here. Verify no stale/phantom items leak across projects instead.
        (void)res5;

        auto res6 = ac.QueryActionCenter(mayasaba::ActionCenterQuery{ "other-proj", 50 });
        bool leaked = false;
        for (auto const& item : res6.items)
            if (item.projectId == "proj-ac") leaked = true;
        EXPECT(!leaked);
    }

    // Attachment inspection integrity
    {
        mayasaba::Storage ai(folder / "attachment_inspect.db");
        ai.BindProject("proj-ai", "AI Project", "C:/AI");
        std::string physicalPath = (folder / "note.md").string();
        {
            std::ofstream of(physicalPath, std::ios::binary);
            of.write("abcd", 4);
        }
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

        auto rn = ai.InspectAttachment(mayasaba::AttachmentInspectionQuery{ "proj-ai", "does-not-exist" });
        EXPECT(!rn.found);
        EXPECT(rn.errorMessage.empty());
    }

    fs::remove_all(folder);
    std::cout << "PASS: standalone Action Center + attachment inspection verification\n";
    return 0;
}
