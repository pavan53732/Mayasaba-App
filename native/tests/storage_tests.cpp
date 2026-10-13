#include "../Mayasaba/Storage.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main()
{
    namespace fs=std::filesystem;
    using mayasaba::SaveDisposition;
    auto folder=fs::temp_directory_path()/
        ("mayasaba-store-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(folder);
    try
    {
        {
            mayasaba::Storage store(folder/"project.db");
            assert(!store.Project("project-A").has_value());
            store.BindProject("project-A","Northstar","C:/Projects/Northstar");
            store.BindProject("project-B","Atlas","D:/Work/Atlas");
            store.SaveDraft("project-A","draft before switching");
            assert(store.Project("project-A")->draft=="draft before switching");
            assert(store.Project("project-B")->draft.empty());
            assert(store.StageSubmission("project-A","submit-0001","Hello")=="submit-0001");
            assert(store.Pending("project-A")->id=="submit-0001");
            assert(store.StageSubmission("project-A","ignored-new-id","Hello")=="submit-0001");
            bool blockedOtherBody=false;
            try { store.StageSubmission("project-A","another","Different"); }
            catch(std::exception const&) {blockedOtherBody=true;}
            assert(blockedOtherBody);
            assert(store.SaveContribution("project-A","submit-0001","Hello") == SaveDisposition::Saved);
            assert(!store.Pending("project-A"));
            assert(store.SaveContribution("project-A","submit-0001","Hello") == SaveDisposition::Duplicate);
            assert(store.SaveContribution("project-A","submit-0001","Altered") == SaveDisposition::Conflict);
            assert(store.SaveContribution("project-B","submit-0001","Hello") == SaveDisposition::Conflict);
            auto messages=store.Contributions("project-A");
            assert(messages.size()==1 && messages[0].body=="Hello");
            assert(store.Contributions("project-B").empty());
            auto a=store.Project("project-A");
            assert(a && a->draft.empty());
            assert(store.SetClosure("project-A",a->version,true));
            assert(store.SaveContribution("project-A","submit-0002","blocked")==SaveDisposition::RejectedClosed);
            assert(!store.SetClosure("project-A",a->version,false)); // stale version
            auto closed=store.Project("project-A");
            assert(closed && closed->closed);
            assert(store.SetClosure("project-A",closed->version,false)); // explicit caller action
            assert(store.StageSubmission("project-A","submit-0002","after reopen")=="submit-0002");
            assert(store.SaveContribution("project-A","submit-0002","after reopen")==SaveDisposition::Saved);
            assert(store.Contributions("project-A").size()==2);
        }
        {
            mayasaba::Storage reopened(folder/"project.db");
            auto a=reopened.Project("project-A");
            assert(a && !a->closed);
            assert(reopened.Contributions("project-A").size()==2);
            assert(reopened.LastBoundProject()==std::optional<std::string>{"project-B"});
            reopened.SaveDraft("project-A","recoverable");
            assert(reopened.Project("project-A")->draft=="recoverable");
            assert(reopened.StageSubmission("project-A","submit-0003","pending after restart")=="submit-0003");
        }
        {
            mayasaba::Storage afterRestart(folder/"project.db");
            auto pending=afterRestart.Pending("project-A");
            assert(pending && pending->id=="submit-0003");
            assert(afterRestart.StageSubmission("project-A","new-id-rejected","pending after restart")=="submit-0003");
            assert(afterRestart.SaveContribution("project-A","submit-0003","pending after restart")==SaveDisposition::Saved);
            assert(afterRestart.Contributions("project-A").size()==3);
        }
        fs::remove_all(folder);
        std::cout<<"PASS: native store durable contribution IDs, conflict refusal, project isolation, closure and reopen\n";
        return 0;
    }
    catch (std::exception const& e)
    {
        std::cerr<<"FAIL: "<<e.what()<<"\n";
        fs::remove_all(folder);
        return 1;
    }
}
