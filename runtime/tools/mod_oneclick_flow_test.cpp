// The Mods tab as a player uses it (overlay/mods_tab.h), with the production setup loop and real workers,
// without a game or window: switch a mod on, answer the one review, watch the progress, end with the mod
// on. Driven by runtime/tools/test_mod_oneclick_flow.py with synthetic packages; clicks land on the
// rectangles the tab's own buttons reported in the previous frame.
#include <atomic>
#include "mod_test_host.h"
#include "overlay/mods_tab.h"
#include "mods/catalogue_client.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
namespace fs=std::filesystem;
namespace hostui {void post(std::function<void()> fn){fn();}}
namespace {
mods::packages::View view(const std::string& id) {
    for(const auto& mod:mods::packages::list())if(mod.id==id)return mod;return {};
}
// One click on a labelled button: move there and press, then release (ImGui trickles the queued input).
struct Clicker {
    std::string target;int phase=0;
    bool step(ImGuiIO& io) {  // true when the click is complete
        auto& rects=overlay::modsui::state().rects;auto it=rects.find(target);
        if(target.empty()||it==rects.end())return false;
        auto c=it->second.GetCenter();
        if(phase==0){io.AddMousePosEvent(c.x,c.y);io.AddMouseButtonEvent(0,true);phase=1;return false;}
        io.AddMouseButtonEvent(0,false);phase=0;target.clear();return true;
    }
    void click(const std::string& name){if(target.empty()){target=name;phase=0;}}
    bool busy()const{return !target.empty();}
};
// the review is open (whatever ID stack the tab opened it from)
bool review_open() {
    for(const auto& popup:GImGui->OpenPopupStack)if(popup.Window&&strstr(popup.Window->Name,"###mods_review"))return true;
    return false;
}
void set_env(const char* key,const std::string& value) {
#ifdef _WIN32
    _putenv_s(key,value.c_str());
#else
    setenv(key,value.c_str(),1);
#endif
}
}
int main(int argc,char** argv) {
    assert(argc==3);assert(getenv("WWHD_NO_AUDIO")&&getenv("WWHD_NO_HOST_INPUT"));
    assert(getenv("WWHD_MOD_MANAGER_DIR")&&getenv("WWHD_TEST_MOD_SETUP")&&getenv("WWHD_TEST_GAME_SOURCES"));
    using namespace mods::packages;using namespace overlay::setupflow;namespace ui=overlay::modsui;
    fs::path root=fs::absolute(argv[1]);std::string mode=argv[2],id=getenv("WWHD_TEST_MOD_SETUP");
    bool resumed=fs::weakly_canonical(fs::absolute(argv[0])).parent_path()==fs::weakly_canonical(root/"code-builds/synthetic/bin");
    // exp-*: an experimental mod (the review warns; switching it on backs up the saves once, first)
    bool experimental=mode.starts_with("exp-"),backup_fails=mode=="exp-backup-fails";
    bool missing=mode=="missing",broken=mode=="broken",catalogue=mode=="catalogue"||mode=="exp-catalogue"||backup_fails;
    bool update_off=mode=="update-off",update_on=mode=="update-on"||mode=="exp-update-on",ids=mode=="ids",update=update_off||update_on||ids;
    if(experimental) {
        // the saves the backup copies; an earlier enable (exp-update-on) backs up elsewhere, so only the
        // backups of this flow are counted in <root>/save-backups
        fs::create_directories(root/"save");std::ofstream(root/"save"/"slot.bin")<<"synthetic save";
        std::lock_guard guard(mods::saves::mutex());
        mods::saves::store().log=[](const std::string&){};
        mods::saves::store().configure(root/"setup-save",root/"setup-backups");
        fs::create_directories(root/"setup-save");std::ofstream(root/"setup-save"/"slot.bin")<<"synthetic save";
    }
    bool cancel_mode=mode=="cancel"||mode=="cancel-rebuild",canceled=false;
    bool restart=mode=="restart"||mode=="cancel-rebuild";set_code_mod_support(!restart||resumed);initialize();
    auto plan=overlay::diagnostic::read_setup_plan(true,getenv("WWHD_MOD_MANAGER_DIR"),getenv("WWHD_TEST_MOD_SETUP"),getenv("WWHD_TEST_GAME_SOURCES"));
    assert(plan.id==id&&plan.error.empty());
    if(!resumed) {
        if(!catalogue) {  // already downloaded (switched on from the list); the catalogue mode downloads it
            // update modes: the older version is installed; the catalogue offers the newer one
            auto index=mods::catalogue::load((root/(update?"installed-index.json":"index.json")).string(),{},root/"unused");
            auto it=std::find_if(index.index.entries.begin(),index.index.entries.end(),[&](const auto& e){return e.id==id;});
            assert(it!=index.index.entries.end());
            mods::catalogue::StagedPackage staged(*it,mods::catalogue::Version::parse("0.2.10"),"USA",platform_key(),root/"stage",index.fixture_root,{});
            std::string error;assert(install(staged.path().string(),error));
            if(update_on) {  // on before the update; it must be on after it
                if(experimental)assert(acknowledge_experimental(id,error));
                assert(enable(id,true,error));
            }
        }
        mods::code::startup(argc,argv); // real service launch arguments, fake game-code rebuild output
    } else {
        assert(pending_setup_runs()==std::vector<std::string>{id});assert(!view(id).enabled);
        assert(unconfirmed_native(id).empty());
    }
    int builds=0;std::atomic<bool> cancel_released{false};
    set_guest_builder([](const GuestPackage&){return uint32_t(65536);},[&](const GuestPackage&,uint32_t){
        ++builds;std::this_thread::sleep_for(std::chrono::milliseconds(35));
        // cancel: the build lasts until the cancel has taken effect (ImGui trickles one input per frame)
        for(int i=0;mode=="cancel"&&!cancel_released&&i<5000;++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        auto module=root/"synthetic.module";std::ofstream(module)<<"synthetic guest cache";return GuestBuilt{module.string(),65536};
    });
    // the display: a desktop window, or a phone (WWHD_TEST_DISPLAY=WxH) for the captures
    int width=960,height=640;
    if(const char* d=getenv("WWHD_TEST_DISPLAY"))sscanf(d,"%dx%d",&width,&height);
    else if(ids)height=2400;  // tall enough that nothing in a view scrolls out of the sweep
    SDL_Surface* surface=SDL_CreateSurface(width,height,SDL_PIXELFORMAT_RGBA32);assert(surface);
    SDL_Renderer* renderer=SDL_CreateSoftwareRenderer(surface);assert(renderer);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(float(width),float(height));io.DeltaTime=1.f/60;
    io.ConfigFlags=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;io.BackendFlags|=ImGuiBackendFlags_HasGamepad;
    // the overlay's font where the test machine has it (WWHD_TEST_FONT=<ttf>), for captures that look like the game
    if(const char* font=getenv("WWHD_TEST_FONT"))if(fs::is_regular_file(font)){io.Fonts->AddFontFromFileTTF(font,17.0f);ImGui::GetStyle().FontSizeBase=17.0f;}
    // WWHD_TEST_ANDROID_STYLE=1: the overlay's Android touch style and near-full-screen Settings window (with
    // WWHD_TEST_DISPLAY at the logical size a phone gets from mobile_ui::scale, e.g. UI size Large)
    const bool android_style=getenv("WWHD_TEST_ANDROID_STYLE")!=nullptr;
    if(android_style){auto& st=ImGui::GetStyle();st.FramePadding=ImVec2(10,16);st.ItemInnerSpacing=ImVec2(10,8);st.ScrollbarSize=49;st.GrabMinSize=49;}
    // the rest of the Settings window around the tab: the settings tabs above; on Android the page ends
    // above the full-width "Close Settings" button
    auto draw_tab=[&](ui::Host& h) {
        if(ImGui::BeginTabBar("settings_tabs")){for(const char* t:{"Saves","Graphics","Display","Audio","Mods","Controls"})
            if(ImGui::BeginTabItem(t,nullptr,std::string(t)=="Mods"?ImGuiTabItemFlags_SetSelected:0))ImGui::EndTabItem();ImGui::EndTabBar();}
        ImGui::BeginChild("page",ImVec2(0,android_style?-ImGui::GetFrameHeightWithSpacing():0),ImGuiChildFlags_NavFlattened);
        ui::draw(h,"");
        ImGui::EndChild();
        if(android_style)ImGui::Button("Close Settings",ImVec2(-FLT_MIN,0));
    };
    auto window_size=[&]{return android_style?ImVec2(width*0.98f,height*0.97f):ImVec2(std::min(860.f,width*0.94f),std::min(620.f,height*0.92f));};
    assert(ImGui_ImplSDLRenderer3_Init(renderer));
    ui::Host host;
    host.port_version=mods::catalogue::Version::parse("0.2.10");
    host.can_restart=[]{return true;};
    host.restart=[](bool){};
    int picks=0;
    // the file picker: the player picks the synthetic disc (or closes the picker: "missing")
    host.choose_game_source=[&](bool,std::function<void(std::string)> chosen){
        ++picks;std::string path;
        if(!missing)for(const auto& [game,value]:plan.sources.object)path=value.string();
        chosen(path);
    };
    if(experimental) {
        std::lock_guard guard(mods::saves::mutex());
        // exp-backup-fails: the backups folder can't be created (a file is in its place)
        if(backup_fails)std::ofstream(root/"blocked")<<"not a folder";
        mods::saves::store().configure(root/"save",backup_fails?root/"blocked"/"save-backups":fs::path());
        hostui::set("mod.showExperimental","1");  // the list shows experimental mods
    }
    if(catalogue||update)set_env("WWHD_MOD_CATALOGUE",(root/"index.json").string());
    else set_env("WWHD_MOD_CATALOGUE",(root/"no-index.json").string());  // the list from the manager only
    int confirmations=0;bool accepted=resumed,saw_progress=false,saw_failure=false,retried=false,corrected=false,saw_done=false;
    int pad_pressed=0,review_since=0;bool switched=resumed,blue_seen=false;ImRect blue_last;int failure_frames=0;
    std::set<std::string> captured;
    auto capture=[&](const std::string& label) {
        if(!captured.insert(label).second)return;
        fs::path dir=getenv("WWHD_TEST_CAPTURE_DIR")?fs::path(getenv("WWHD_TEST_CAPTURE_DIR")):root/"captures";
        fs::create_directories(dir);
        auto name=std::string(getenv("WWHD_TEST_CAPTURE_PREFIX")?getenv("WWHD_TEST_CAPTURE_PREFIX"):"")+label+".bmp";
        assert(SDL_SaveBMP(surface,(dir/name).string().c_str()));
    };
    // ids: every view of the tab, with every section open, hovered all over: Dear ImGui reports two items with
    // one ID (it would show its red "conflicting ID" box in a debug build) when the hovered ID was submitted twice
    if(ids) {
        assert(io.ConfigDebugHighlightIdConflicts);
        host.expand_all=true;
        int conflicts=0;
        auto frame_once=[&](float mx,float my) {
            io.AddMousePosEvent(mx,my);
            ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();
            if(GImGui->DebugDrawIdConflictsId){++conflicts;return false;}
            ImGui::SetNextWindowPos(ImVec2(0,0),ImGuiCond_Always);ImGui::SetNextWindowSize(ImVec2(float(width),float(height)));
            ImGui::Begin("Settings",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);
            draw_tab(host);ImGui::End();ImGui::Render();
            return true;
        };
        auto sweep=[&](const std::string& view,const std::function<void()>& set) {
            // close what the previous view left open (the review)
            ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();
            if(!GImGui->OpenPopupStack.empty())ImGui::ClosePopupToLevel(0,true);
            ImGui::EndFrame();
            set();
            for(int i=0;i<4;i++)frame_once(-100,-100);  // let the view (and a popup) appear
            float bottom=0;  // the lowest item drawn: sweep down to it
            for(auto* w:GImGui->Windows)if(w->Active)bottom=std::max(bottom,w->DC.CursorMaxPos.y);
            bottom=std::min(bottom+8.f,float(height));
            int before=conflicts;
            for(float y=2;y<bottom;y+=6)for(float x=4;x<width;x+=40)
                if(!frame_once(x,y)){
                    fprintf(stderr,"ID conflict in %s at %.0f,%.0f (ID %08X)\n",view.c_str(),x,y,GImGui->DebugDrawIdConflictsId);
                    ImGui::EndFrame();
                }
            fprintf(stderr,"[ids] %s: %d conflicts (swept to y=%.0f)\n",view.c_str(),conflicts-before,bottom);
        };
        auto& s=ui::state();
        auto page=[&](const std::string& pid,bool builtin){return [&s,pid,builtin]{s.review={};s.job.flow={};ui::go_area(ui::kMods);s.page=pid;s.page_builtin=builtin;};};
        // wait for the catalogue list (local fixture) so the update and the downloadable mod show
        for(int i=0;i<400&&!s.loader.snapshot().have_list;i++){frame_once(-100,-100);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        assert(s.loader.snapshot().have_list);
        sweep("list",page("",false));
        sweep("more",[&]{s.page.clear();ui::go_area(ui::kMore);});
        for(const auto& e:mods::manager::entries())sweep(std::string("page ")+e.id,page(e.id,true));
        sweep("page "+id,page(id,false));
        sweep("page available-fixture",page("available-fixture",false));
        sweep("review switch on",[&]{page(id,false)();ui::open_review_for_installed(view(id));});
        sweep("review update",[&]{page(id,false)();auto snap=s.loader.snapshot();auto v=view(id);
            ui::open_review_for_entry(*ui::catalogue_entry(snap,id),snap.catalogue.fixture_root,true,&v);});
        sweep("review install",[&]{page("available-fixture",false)();auto snap=s.loader.snapshot();
            ui::open_review_for_entry(*ui::catalogue_entry(snap,"available-fixture"),snap.catalogue.fixture_root,false);});
        // the progress view's states: working, failed (with its details open), done with a restart
        auto progress=[&](ui::Phase phase){return [&,phase]{page(id,false)();ui::Plan plan;plan.download=true;plan.prepare={"Prepare"};plan.build=true;plan.restart=true;
            s.job.flow=ui::Flow::make(id,"Sea minimap",plan);s.job.flow.accept(0);
            if(phase==ui::Phase::Failed)s.job.flow.fail("Preparing the mod's files failed.","synthetic details");
            if(phase==ui::Phase::Done)s.job.flow.finish(true,1);};};
        sweep("progress working",progress(ui::Phase::Working));
        sweep("progress failed",progress(ui::Phase::Failed));
        sweep("progress done",progress(ui::Phase::Done));
        s.job.flow={};
        ImGui_ImplSDLRenderer3_Shutdown();ImGui::DestroyContext();SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);
        fprintf(stderr,"[ids] %d conflicts in all views\n",conflicts);
        return conflicts?1:0;
    }
    Clicker clicker;
    const std::string switch_label="##on_"+id;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(25);
    for(int frame=0;frame<6000&&std::chrono::steady_clock::now()<deadline;++frame) {
        assert(fs::space(root).available>10ull*1024*1024*1024);
        ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();setup_run_tick();ui::tick(host,true);
        // the settings window as the overlay sizes it
        ImGui::SetNextWindowPos(ImVec2(width*0.5f,height*0.5f),ImGuiCond_Always,ImVec2(0.5f,0.5f));
        ImGui::SetNextWindowSize(window_size());
        ImGui::Begin("Settings",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);
        {auto& rects=ui::state().rects;blue_seen=rects.count("choice_colour_blue");if(blue_seen)blue_last=rects["choice_colour_blue"];rects.erase("choice_colour_blue");}
        bool had_review=review_open();
        host.back=false;
        draw_tab(host);
        bool has_review=review_open();
        ImGui::End();
        if(has_review&&!had_review){++confirmations;review_since=frame;}
        auto& s=ui::state();auto& f=s.job.flow;
        // the player's actions, one at a time
        if(!clicker.busy()&&frame>3) {
            if(!switched) {
                // the list shows the mod (catalogue mode: once the list is loaded); "checkbox" uses the page's switch
                if(update) {  // the mod's page offers Update once the catalogue is loaded
                    if(s.page.empty())clicker.click("open_"+id);else if(s.rects.count("Update##page")){clicker.click("Update##page");switched=true;}
                } else if(mode=="checkbox") {
                    if(s.page.empty())clicker.click("open_"+id);else if(s.rects.count("##on_page")){clicker.click("##on_page");switched=true;}
                } else if(s.rects.count(switch_label)){clicker.click(switch_label);switched=true;}
            } else if(has_review&&!accepted&&frame>8) {
                auto& r=s.review;
                bool need_source=!r.source_game.empty()&&!ui::source_ready(r);
                // missing: first the controller's A on the review's default button, which is Cancel
                if(missing&&!pad_pressed){if(frame-review_since>=5){
                    // a controller player: the cursor is visible (any D-pad press shows it), A presses the focused button
                    io.AddKeyEvent(ImGuiKey_GamepadFaceDown,true);pad_pressed=frame;}
                    else if(frame-review_since>=2){io.AddMousePosEvent(-100,-100);io.ConfigNavCursorVisibleAlways=true;}}
                else if(missing&&confirmations<2){}
                else if(need_source&&picks==0)clicker.click("Choose disc image…##source");
                else if(mode=="choice"&&r.chosen["colour"]!="blue") {
                    // open the list, then pick blue in it
                    // the list is open when its item was drawn this frame at the same place as the last one
                    bool list_open=s.rects.count("choice_colour_blue")&&blue_seen&&blue_last.Min.x==s.rects["choice_colour_blue"].Min.x&&
                                   blue_last.Min.y==s.rects["choice_colour_blue"].Min.y;
                    if(list_open)clicker.click("choice_colour_blue");
                    else if(!s.rects.count("choice_colour_blue"))clicker.click("##choice_colour");
                } else {
                    std::string go=r.mode==ui::Review::SwitchOn?"Switch on##review":r.mode==ui::Review::Update?"Update##review":"Install##review";
                    // missing: the button is unavailable (the reason is next to it); the player cancels
                    clicker.click(missing&&frame>60?"Cancel##review":go);
                }
            } else if(f.phase==ui::Phase::Failed&&broken&&!corrected) {
                if(++failure_frames==4){fs::remove(root/"fail-tool");corrected=true;clicker.click("Try again##flow");}
            } else if(cancel_mode&&f.phase==ui::Phase::Working&&!canceled) {
                size_t c=f.current();
                bool at=c<f.steps.size()&&(mode=="cancel-rebuild"?setup_run.rebuilding:f.steps[c].kind==ui::StepKind::Build);
                if(at)clicker.click("Cancel##flow");
            }
        }
        if(pad_pressed&&frame==pad_pressed+1)io.AddKeyEvent(ImGuiKey_GamepadFaceDown,false);
        // ... it closed: switch the mod on again for the second review
        if(missing&&pad_pressed&&frame==pad_pressed+6){assert(!has_review&&confirmations==1);switched=false;io.ConfigNavCursorVisibleAlways=false;}
        if(clicker.busy()) {
            bool cancel_click=clicker.target=="Cancel##flow";
            if(clicker.step(io)&&cancel_click)canceled=true;
        }
        if(f.phase==ui::Phase::Working&&!accepted){accepted=true;assert(confirmations==1);std::ofstream(root/"consent-count")<<confirmations;}
        if(f.phase==ui::Phase::Working)saw_progress=true;
        if(f.phase==ui::Phase::Failed&&backup_fails) {
            // the reason (no "confirm the warning first"): the review was the confirmation
            assert(f.error.starts_with("Your saves couldn't be backed up")&&f.error.find("confirm")==std::string::npos);
            assert(!view(id).enabled&&pending_setup_runs().empty());
            saw_failure=true;break;
        }
        if(f.phase==ui::Phase::Failed) {
            fprintf(stderr,"flow failed: %s | %s\n",f.error.c_str(),f.details.c_str());
            saw_failure=true;assert(broken);assert(!view(id).enabled&&pending_setup_runs().empty());
            assert(f.details.find("synthetic deliberate failure")!=std::string::npos);
            assert(f.error=="Preparing the mod's files failed.");  // one plain sentence; the output is behind Show details
        }
        if(saw_failure&&corrected&&f.phase==ui::Phase::Working)retried=true;
        if(f.phase==ui::Phase::Done)saw_done=true;
        ImGui::Render();SDL_SetRenderDrawColor(renderer,20,23,29,255);SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);SDL_RenderPresent(renderer);
        if(frame==10&&!resumed&&!catalogue)capture("list");
        if(frame==30&&catalogue)capture("list");
        if(has_review&&frame>12)capture(mode=="choice"?"review-choice":"review");
        if(f.phase==ui::Phase::Working&&f.current()<f.steps.size()&&f.steps[f.current()].kind!=ui::StepKind::Download&&f.overall()>0.05)
            capture(resumed?"resume-progress":"progress");
        if(f.phase==ui::Phase::Failed&&failure_frames==2)capture("error");
        if(f.phase==ui::Phase::Done)capture("done");
        if(missing&&frame>80) {assert(f.phase==ui::Phase::Idle&&setup_run.id.empty()&&!has_review);break;}
        if(view(id).enabled&&setup_run.id.empty()&&saw_done&&captured.count("done"))break;
        if(update_off&&saw_done&&captured.count("done"))break;
        if(canceled&&f.phase==ui::Phase::Idle){cancel_released=true;auto& worker=setup_work();std::lock_guard guard(worker.mutex);if(!worker.running)break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if(experimental)assert(ui::state().job.accepted.experimental);  // the review showed the warning
    if(missing)assert(!view(id).enabled&&!accepted&&pending_setup_runs().empty()&&picks==1&&confirmations==2);
    else if(backup_fails)assert(saw_failure&&!view(id).enabled&&confirmations==1);
    else if(update) {
        // an update ends in the state before it: off stays off (prepared), on stays on
        assert(view(id).version=="1.1.0"&&view(id).enabled==update_on&&saw_done&&confirmations==1);
        assert(pending_setup_runs().empty()&&setup_run.id.empty());
        if(update_off) {
            assert(ui::state().job.flow.keep_off&&builds==1&&!ui::state().job.flow.restart_needed);
            for(const auto& step:setup_steps(id))assert(step.satisfied);
        } else assert(builds==0&&!ui::state().job.flow.keep_off);
    }
    else if(cancel_mode) {
        assert(canceled&&!view(id).enabled&&pending_setup_runs().empty());
        assert(setup_steps(id)[1].satisfied);assert(confirmations==1);  // the preparation stays done
        if(mode=="cancel-rebuild")assert(!mods::code::status().requested&&builds==0);
    }
    else {
        assert(view(id).enabled&&view(id).pending_restart&&setup_run.id.empty());assert(builds==1&&saw_progress&&saw_done);
        assert(confirmations==(resumed?0:1));assert(pending_setup_runs().empty());
        assert(ui::state().job.flow.restart_needed);  // "Restart now / Later" at the end
        if(broken)assert(saw_failure&&retried);
        if(restart)assert(resumed);
    }
    // the screens after the end, for the report (only with WWHD_TEST_CAPTURE_DIR)
    if(getenv("WWHD_TEST_CAPTURE_DIR")&&(mode=="normal"||mode=="catalogue")) {
        auto draw_frames=[&](int count,const std::string& target) {
            if(!target.empty())clicker.click(target);
            for(int i=0;i<count;i++) {
                ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();setup_run_tick();ui::tick(host,true);
                ImGui::SetNextWindowPos(ImVec2(width*0.5f,height*0.5f),ImGuiCond_Always,ImVec2(0.5f,0.5f));
                ImGui::SetNextWindowSize(window_size());
                ImGui::Begin("Settings",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);
                draw_tab(host);ImGui::End();
                if(clicker.busy())clicker.step(io);
                ImGui::Render();SDL_SetRenderDrawColor(renderer,20,23,29,255);SDL_RenderClear(renderer);
                ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);SDL_RenderPresent(renderer);
            }
        };
        draw_frames(6,"Later##flow");capture("page-on");
        draw_frames(6,ui::state().rects.count("< Mods##back")?"< Mods##back":"\xE2\x80\xB9 Mods##back");capture("list-on");
        ui::shoulder(+1);draw_frames(4,"");capture("more");
    }
    std::cout<<"FLOW "<<id<<" "<<mode<<" enabled="<<view(id).enabled<<" confirmations="<<confirmations<<" resumed="<<resumed<<" failed="<<saw_failure<<"\n";
    ImGui_ImplSDLRenderer3_Shutdown();ImGui::DestroyContext();SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);
    mods::json::Value result;result["enabled"]=view(id).enabled;result["resumed"]=resumed;result["confirmations"]=confirmations;
    std::ofstream(root/"flow-result.json.tmp")<<mods::json::dump(result);
    fs::rename(root/"flow-result.json.tmp",root/"flow-result.json");
}
