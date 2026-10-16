#pragma once
#include "Storage.h"
#include "ProjectRoot.h"
#include "NativeIds.h"
#include <functional>

namespace mayasaba
{
    enum class SelectionAction { Open, Recent, Locate, Create, Recover };
    enum class SwitchObservation { NoAdmittedSessions, IsolatedAndSupervised, Unknown };
    struct SelectionRequest
    {
        unsigned version{1};
        std::string owner{"PROJECT_SERVICE"};
        std::string operationId{NewUuid()};
        SelectionAction action{SelectionAction::Open};
        std::string projectId;
        int expectedVersion{-1};
        std::wstring path, child;
        std::string expectedParent, creationOperation;
        DraftChoice draftChoice{DraftChoice::Automatic};
        std::string expectedSelectorDraft, expectedTargetDraft;
    };
    // Narrow Project-service slice; no CLI session or work admission exists here.
    // Caller must execute on the serialized application lane. SQL stays in Storage.
    class ProjectService
    {
        Storage& store_;
        static std::string Utf8(std::wstring const& value)
        {
            int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            if (!n && !value.empty()) throw std::runtime_error("Invalid Unicode path");
            std::string result(n, '\0');
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), n, nullptr, nullptr);
            return result;
        }
        static std::wstring Wide(std::string const& value)
        {
            int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
            if(!n && !value.empty())throw std::runtime_error("Invalid UTF-8 path");
            std::wstring result(n,L'\0');
            MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),n);
            return result;
        }
    public:
        explicit ProjectService(Storage& store) : store_(store) {}
        CrossProjectRecentProjectsResult Recent(CrossProjectRecentProjectsQuery const& query) { return store_.QueryRecentProjects(query); }
        SelectionReceipt Select(SelectionRequest const& request, SwitchObservation observation,
            std::function<bool()> const& current = [] { return true; })
        {
            if (request.version != 1 || request.owner != "PROJECT_SERVICE")
                throw std::runtime_error("Unsupported selector contract version or owner");
            if(static_cast<unsigned>(request.action)>static_cast<unsigned>(SelectionAction::Recover) ||
                static_cast<unsigned>(request.draftChoice)>static_cast<unsigned>(DraftChoice::UseSelector))
                throw std::runtime_error("Unsupported selector action or draft disposition");
            if (!IsUuid(request.operationId) || (!request.projectId.empty() && !IsUuid(request.projectId)) ||
                (!request.creationOperation.empty() && !IsUuid(request.creationOperation)) || request.path.size() > 32760 ||
                request.child.size() > 255 || request.expectedSelectorDraft.size() > 65536 || request.expectedTargetDraft.size() > 65536)
                throw std::runtime_error("Invalid selector identity or input exceeds declared bounds");
            if (observation == SwitchObservation::Unknown)
                throw std::runtime_error("Project switch blocked: current activity, isolation or effect cessation is unverified. Old project remains active");
            if (!current()) throw std::runtime_error("Selection cancelled before admission");
            std::optional<ProjectRecord> expected;
            if (!request.projectId.empty())
            {
                expected=store_.ProjectByDomainId(request.projectId);
                if(!expected)
                    throw std::runtime_error("Project changed; refresh the recent list and select it again");
            }
            auto path=request.path;
            if(request.action==SelectionAction::Recent)
            {
                if(!expected) throw std::runtime_error("Recent selection requires a project identity and version");
                path=Wide(expected->canonicalPath);
            }
            if(request.action==SelectionAction::Locate && !expected)
                throw std::runtime_error("Locate requires the original project identity and version");
            ProjectRoot root;
            std::string creation=request.creationOperation;
            if(request.action==SelectionAction::Create || request.action==SelectionAction::Recover)
            {
                if(creation.empty()) creation=request.operationId;
                auto previous=store_.GetDirectoryOperation(creation);
                if(previous)
                {
                    if(request.action==SelectionAction::Create &&
                        (previous->parentPath!=Utf8(path) || previous->folderName!=Utf8(request.child)))
                        throw std::runtime_error("Creation identity reused with changed target");
                    if(previous->state=="INTENDED")
                        throw std::runtime_error("Creation outcome remains unknown at " + previous->targetPath + ". No creation was repeated; original operation " + creation + " is blocked");
                    if(previous->state!="CREATED" && previous->state!="BOUND")
                        throw std::runtime_error("Recorded creation is not eligible for recovery");
                    root=ValidateProjectRoot(Wide(previous->targetPath));
                    if(Utf8(root.stableIdentity)!=previous->observedIdentity)
                        throw std::runtime_error("Created folder identity changed; competing directory was not adopted");
                }
                else
                {
                    if(request.action==SelectionAction::Recover) throw std::runtime_error("Creation record is unavailable");
                    auto error=ValidateSingleChildFolderName(request.child);
                    if(!error.empty()) throw std::runtime_error(error);
                    auto parent=ValidateProjectRoot(path);
                    if(request.expectedParent.empty() || Utf8(parent.stableIdentity)!=request.expectedParent)
                        throw std::runtime_error("Parent identity changed or was not observed; choose the parent again");
                    auto target=std::filesystem::path(parent.canonicalPath)/request.child;
                    if(!current()) throw std::runtime_error("Creation cancelled before durable intent");
                    store_.RecordDirectoryIntent(creation,Utf8(path),Utf8(request.child),Utf8(target.wstring()));
                    try
                    {
                        root=CreateProjectFolder(parent.canonicalPath,request.child);
                        store_.RecordDirectoryCreated(creation,Utf8(root.stableIdentity));
                    }
                    catch(std::exception const& e)
                    {
                        store_.RecordDirectoryUncertain(creation,e.what());
                        throw std::runtime_error("Creation unconfirmed at " + Utf8(target.wstring()) + ". Nothing was deleted. Recheck operation " + creation + ": " + e.what());
                    }
                }
            }
            else root=ValidateProjectRoot(path);
            if(expected && Utf8(root.stableIdentity)!=expected->filesystemIdentity)
                throw std::runtime_error("Folder identity changed. Use Locate folder; the original project history is preserved");
            if(!current()) throw std::runtime_error("Selection cancelled; any created folder and its observation were preserved");
            SelectionCommit commit;
            commit.operationId=request.operationId; commit.proposedId=request.operationId;
            commit.expectedProject=expected?expected->id:""; commit.expectedVersion=request.expectedVersion;
            commit.name=Utf8(root.displayName);commit.path=Utf8(root.canonicalPath);commit.filesystemIdentity=Utf8(root.stableIdentity);
            commit.draftChoice=request.draftChoice;commit.expectedSelectorDraft=request.expectedSelectorDraft;commit.expectedTargetDraft=request.expectedTargetDraft;
            commit.creationOperation=creation;
            // Exact length-framed request bytes, not an authoritative hash or JSON canonicalizer.
            auto field=[&](std::string const& value){commit.requestBytes+=std::to_string(value.size())+":"+value;};
            field("selector-v1");field(std::to_string(static_cast<int>(request.action)));field(request.projectId);
            field(std::to_string(request.expectedVersion));field(Utf8(request.path));field(Utf8(request.child));
            field(request.expectedParent);field(creation);field(std::to_string(static_cast<int>(request.draftChoice)));
            field(request.expectedSelectorDraft);field(request.expectedTargetDraft);
            return store_.CommitSelection(commit);
        }
        std::string Bind(std::string const& proposedId, std::wstring const& path)
        {
            auto root = ValidateProjectRoot(path);
            return store_.BindProject(proposedId, Utf8(root.displayName), Utf8(root.canonicalPath), Utf8(root.stableIdentity));
        }
        void Locate(std::string const& id, std::wstring const& path)
        {
            auto project = store_.Project(id);
            if (!project) throw std::runtime_error("Project no longer exists");
            auto root = ValidateProjectRoot(path);
            if (Utf8(root.stableIdentity) != project->filesystemIdentity)
                throw std::runtime_error("Folder identity differs; original history was preserved");
            store_.RelocateProject(id, Utf8(root.canonicalPath), Utf8(root.displayName));
        }
        struct CreateCommand
        {
            std::string operationId;
            std::wstring parent;
            std::wstring child;
        };
        std::string Create(CreateCommand const& command)
        {
            auto target = std::filesystem::path(command.parent) / command.child;
            if (auto previous = store_.GetDirectoryOperation(command.operationId))
                throw std::runtime_error("Creation intent already exists; inspect its recorded outcome instead of repeating it");
            auto error = ValidateSingleChildFolderName(command.child);
            if (!error.empty()) throw std::runtime_error(error);
            ValidateProjectRoot(command.parent);
            store_.RecordDirectoryIntent(command.operationId, Utf8(command.parent), Utf8(command.child), Utf8(target.wstring()));
            try
            {
                auto root = CreateProjectFolder(command.parent, command.child);
                store_.RecordDirectoryCreated(command.operationId, Utf8(root.stableIdentity));
                // Operation identity also supplies stable proposed project identity on replay.
                auto id = store_.BindProject(command.operationId, Utf8(root.displayName), Utf8(root.canonicalPath), Utf8(root.stableIdentity));
                store_.RecordDirectoryBound(command.operationId);
                return id;
            }
            catch (std::exception const& e)
            {
                // A thrown verification/commit does not establish absence of the folder.
                store_.RecordDirectoryUncertain(command.operationId, e.what());
                throw std::runtime_error("Creation/binding unconfirmed at " + Utf8(target.wstring()) +
                    ". Folder was not deleted. Inspect the recorded operation before retrying: " + e.what());
            }
        }
    };
}
