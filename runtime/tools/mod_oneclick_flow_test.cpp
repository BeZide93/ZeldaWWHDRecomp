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
    bool missing=mode=="missing",broken=mode=="broken",catalogue=mode=="catalogue";
    bool cancel_mode=mode=="cancel"||mode=="cancel-rebuild",canceled=false;
    bool restart=mode=="restart"||mode=="cancel-rebuild";set_code_mod_support(!restart||resumed);initialize();
    auto plan=overlay::diagnostic::read_setup_plan(true,getenv("WWHD_MOD_MANAGER_DIR"),getenv("WWHD_TEST_MOD_SETUP"),getenv("WWHD_TEST_GAME_SOURCES"));
    assert(plan.id==id&&plan.error.empty());
    if(!resumed) {
        if(!catalogue) {  // already downloaded (switched on from the list); the catalogue mode downloads it
            auto index=mods::catalogue::load((root/"index.json").string(),{},root/"unused");
            auto it=std::find_if(index.index.entries.begin(),index.index.entries.end(),[&](const auto& e){return e.id==id;});
            assert(it!=index.index.entries.end());
            mods::catalogue::StagedPackage staged(*it,mods::catalogue::Version::parse("0.2.10"),"USA",platform_key(),root/"stage",index.fixture_root,{});
            std::string error;assert(install(staged.path().string(),error));
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
    SDL_Surface* surface=SDL_CreateSurface(width,height,SDL_PIXELFORMAT_RGBA32);assert(surface);
    SDL_Renderer* renderer=SDL_CreateSoftwareRenderer(surface);assert(renderer);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(float(width),float(height));io.DeltaTime=1.f/60;
    io.ConfigFlags=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;io.BackendFlags|=ImGuiBackendFlags_HasGamepad;
    // the overlay's font where the test machine has it (WWHD_TEST_FONT=<ttf>), for captures that look like the game
    if(const char* font=getenv("WWHD_TEST_FONT"))if(fs::is_regular_file(font)){io.Fonts->AddFontFromFileTTF(font,17.0f);ImGui::GetStyle().FontSizeBase=17.0f;}
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
    if(catalogue)set_env("WWHD_MOD_CATALOGUE",(root/"index.json").string());
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
    Clicker clicker;
    const std::string switch_label="##on_"+id;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(25);
    for(int frame=0;frame<6000&&std::chrono::steady_clock::now()<deadline;++frame) {
        assert(fs::space(root).available>10ull*1024*1024*1024);
        ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();setup_run_tick();ui::tick(host,true);
        // the settings window as the overlay sizes it
        ImGui::SetNextWindowPos(ImVec2(width*0.5f,height*0.5f),ImGuiCond_Always,ImVec2(0.5f,0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(860.f,width*0.94f),std::min(620.f,height*0.92f)));
        ImGui::Begin("Settings",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);
        {auto& rects=ui::state().rects;blue_seen=rects.count("choice_colour_blue");if(blue_seen)blue_last=rects["choice_colour_blue"];rects.erase("choice_colour_blue");}
        bool had_review=ImGui::IsPopupOpen("###mods_review",ImGuiPopupFlags_AnyPopupLevel);
        host.back=false;
        ui::draw(host,"");
        bool has_review=ImGui::IsPopupOpen("###mods_review",ImGuiPopupFlags_AnyPopupLevel);
        ImGui::End();
        if(has_review&&!had_review){++confirmations;review_since=frame;}
        auto& s=ui::state();auto& f=s.job.flow;
        // the player's actions, one at a time
        if(!clicker.busy()&&frame>3) {
            if(!switched) {
                // the list shows the mod (catalogue mode: once the list is loaded); "checkbox" uses the page's switch
                if(mode=="checkbox") {
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
                    std::string go=r.mode==ui::Review::SwitchOn?"Switch on##review":"Install##review";
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
        if(f.phase==ui::Phase::Failed) {
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
        if(canceled&&f.phase==ui::Phase::Idle){cancel_released=true;auto& worker=setup_work();std::lock_guard guard(worker.mutex);if(!worker.running)break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if(missing)assert(!view(id).enabled&&!accepted&&pending_setup_runs().empty()&&picks==1&&confirmations==2);
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
                ImGui::SetNextWindowSize(ImVec2(std::min(860.f,width*0.94f),std::min(620.f,height*0.92f)));
                ImGui::Begin("Settings",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);
                ui::draw(host,"");ImGui::End();
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
