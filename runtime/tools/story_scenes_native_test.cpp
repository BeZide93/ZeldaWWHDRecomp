// Synthetic guest memory: proves native call contracts and lease cleanup, not game rendering.
#include "mods/story_scenes.h"
#include "mods/story_mail.h"
#include "mods/story_notebook_game.h"
#include "runtime.h"
#include "input.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace {
constexpr uint32_t kArena=0x20000000,kPlay=kArena,kCamera=kArena+0x10000,kManager=kArena+0x20000,kScratch=kArena+0x30000,kActor=kArena+0x40000;
unsigned native_calls=0,end_calls=0,finished=0,sounds=0;
uint32_t pad_buttons=0;void(*trim_hook)(Cpu*)=nullptr;bool armed=false;unsigned message_status=0,close_ticks=0,reset_calls=0;float fade_rate=0;bool admit_ok=true,advance_close=true;
std::vector<unsigned> mode_calls;
void* map(uint32_t at,uint32_t bytes){
#ifdef _WIN32
return VirtualAlloc(mem::ptr(at),bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
return mmap(mem::ptr(at),bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
#endif
}
void sound(Cpu*,const char*){++sounds;}
uint32_t game_call(Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){
    ++native_calls;std::vector<uint32_t> a(args);
    switch(fn){
    case 0x025200D4:return kPlay;
    case 0x024F8044:return kCamera;
    case 0x0253EC0C:assert(a.size()==9&&a[0]==kPlay+0x51D0&&a[1]==3&&a[2]==1&&a[5]==0&&a[6]==0&&a[7]==0xFFFFFFFF&&a[8]==0xFF);st8(kPlay+0x5290,1);return 1;
    case 0x0253FD1C:if(!admit_ok)return 0;assert(ld8(kPlay+0x5290)==1);st8(kPlay+0x5290,0);st8(kPlay+0x5292,3);st16(kPlay+0x51D0+0xD8,0xFFFF);st32(kPlay+0x51D0+0xC4,0xFFFFFFFF);st32(kPlay+0x51D0+0xC8,0xFFFFFFFF);return 1;
    case 0x0253EFA8:assert(a[0]==kPlay+0x51D0);++end_calls;st8(kPlay+0x5292,0);return 1;
    case 0x02514F2C:st8(kCamera+4,0);return 0;
    case 0x02514F38:st8(kCamera+4,1);return 0;
    case 0x02515280:st32(kCamera+0x600,a[1]);return 1;
    case 0x02515048:return 1;
    case 0x02514F50:assert(a.size()==3);std::memcpy(mem::ptr(kCamera+0x10),mem::ptr(a[1]),12);std::memcpy(mem::ptr(kCamera+0x1C),mem::ptr(a[2]),12);return 1;
    case 0x025F7DB0:assert(armed&&a[0]==kManager&&a[1]==0x2CF3&&a[2]==0);message_status=14;return 1;
    case 0x025F795C:return message_status;
    case 0x025F74D0:
        if(a[1]==19)assert(message_status==18||message_status==19);
        message_status=a[1];if(a[1]==16)close_ticks=3;return 0;
    case 0x025F78C4:++reset_calls;message_status=0;close_ticks=0;return 0;
    case 0x025916C4:st8(kPlay+0x5C22,0);return 0;
    case 0x0259169C:st8(kPlay+0x5C22,1);return 0;
    case 0x025F069C:fade_rate=float(c->f[1].ps0);return 0;
    case 0x025D54C4:st32(a[1],kActor);return 1;
    case 0x022BED70:st32(a[0]+0x89C,a[1]);return 0;
    case 0x022C2D6C:case 0x022C2CC0:case 0x022BF3A0:case 0x024F08A8:return 0;
    case 0x022BF480:assert(a[0]==kActor&&a[1]==kActor+0x690&&a[2]==kActor+0x314);return 0;
    case 0x025D6870:{float speed=u32_as_f32(ld32(a[0]+0x370));double angle=double(int16_t(ld16(a[0]+0x322)))*3.14159265358979323846/32768.0;
        st32(a[0]+0x314,f32_as_u32(u32_as_f32(ld32(a[0]+0x314))+float(std::sin(angle))*speed));
        st32(a[0]+0x31C,f32_as_u32(u32_as_f32(ld32(a[0]+0x31C))+float(std::cos(angle))*speed));return 0;}

    default:assert(false);return 0;
    }
}
void test_tick(Cpu* cpu,bool skip=false,unsigned frames=1){
    // The native message window updates asynchronously once per base game pass.
    if(advance_close&&close_ticks){--close_ticks;message_status=close_ticks?17:18;}
    story_scenes::tick(cpu,skip,frames);
}
void clear_guest(){std::memset(mem::ptr(kArena),0,0x50000);st32(0x101F4B5C,kManager);st8(kCamera+4,1);st8(kPlay+0x5C22,1);st32(kCamera+0x73C+0x6C,f32_as_u32(60));st32(kCamera+0x600,0);st32(kCamera+0x10,f32_as_u32(123));st32(kPlay+0x5B34,kActor);std::memcpy(mem::ptr(kPlay+0x5134),"sea",4);}
}
namespace input {PadState read(){PadState p;p.buttons=pad_buttons;return p;}}
namespace story_mail {void say(uint32_t,const std::u16string*){armed=true;}void unsay(){armed=false;}bool can_say(){return true;}}
namespace story_notebook {const GameUi& game_ui(){static GameUi ui=[] {GameUi u;u.sound=sound;return u;}();return ui;}}
int main(){
    assert(map(kArena,0x50000)==mem::ptr(kArena));assert(map(0x101F0000,0x10000)==mem::ptr(0x101F0000));clear_guest();st16(kActor+8,0x175);
    Cpu cpu{};story_scenes::set_game_calls(game_call);story_scenes::set_scratch_memory(kScratch);
    test_tick(&cpu);assert(native_calls==0); // no declarations means no guest calls/writes
    using namespace story_notebook;ManifestScene s;s.id="intro";SceneStep camera;camera.kind=SceneStep::Kind::Camera;camera.frames=2;camera.eye={1,2,3};camera.target={4,5,6};camera.pan=true;camera.eye_to={10,20,30};camera.target_to={40,50,60};
    SceneStep say;say.kind=SceneStep::Kind::Say;say.frames=5;say.text="Hello.";
    SceneStep fade;fade.kind=SceneStep::Kind::Fade;fade.frames=2;
    SceneStep end;end.kind=SceneStep::Kind::End;end.then.push_back({StoryAction::Kind::Flag,"done",1});
    SceneStep face;face.kind=SceneStep::Kind::Face;face.actor="resident";face.position={10,0,0};face.frames=1;
    SceneStep se;se.kind=SceneStep::Kind::Sound;se.sound="SE_MENU_CANCEL";se.frames=1;
    s.steps={camera,say,fade,face,se,end};std::string why;
    story_scenes::set_callbacks([](Cpu*,const std::string&,const std::string&){return kActor;},[](const std::string&,const std::vector<StoryAction>&){++finished;return true;});
    assert(story_scenes::declare("mod",{s},why));
    story_scenes::install([](Cpu*,uint32_t){},[](uint32_t){return std::string();},[](uint32_t address,void(*hook)(Cpu*)){assert(address==0x024FF8A0);trim_hook=hook;});assert(trim_hook);
    admit_ok=false;assert(story_scenes::request("mod","intro"));test_tick(&cpu);assert(!story_scenes::active()&&ld8(kPlay+0x5290)==0&&ld8(kPlay+0x5292)==0);admit_ok=true;
    assert(story_scenes::request("mod","intro"));assert(!story_scenes::request("mod","intro"));
    st8(kPlay+0x5292,1);test_tick(&cpu);assert(!story_scenes::active()&&end_calls==0);
    st8(kPlay+0x5292,0);test_tick(&cpu);assert(story_scenes::active()&&ld8(kPlay+0x5292)==3&&ld32(kCamera+0x600)==2);
    assert(ld8(kPlay+0x5C22)==0);Cpu trim_cpu{};trim_cpu.r[3]=kCamera;trim_hook(&trim_cpu);assert(u32_as_f32(ld32(kCamera+0x5FC))==15);
    uint8_t mid_state[256];std::memcpy(mid_state,mem::ptr(kScratch),256);
    for(unsigned i=0;i<20&&story_scenes::active();++i)test_tick(&cpu);
    assert(sounds==1&&ld16(kActor+0x322)==0x4000&&ld16(kActor+0x32A)==0x4000);
    assert(!story_scenes::active()&&finished==1&&end_calls==1&&!armed&&ld8(kPlay+0x5292)==0&&ld8(kCamera+4)==1&&ld8(kPlay+0x5C22)==1&&u32_as_f32(ld32(kCamera+0x10))==123&&fade_rate==-1);
    // Load a state captured during that now-finished scene: guest marker restores control even
    // though no host scene is active. The loaded camera snapshot is used, not stale live state.
    std::memcpy(mem::ptr(kScratch),mid_state,256);st8(kPlay+0x5292,3);st8(kCamera+4,0);story_scenes::state_loaded();test_tick(&cpu);
    assert(end_calls==2&&finished==1&&ld8(kPlay+0x5292)==0&&ld8(kCamera+4)==1);
    assert(story_scenes::request("mod","intro"));test_tick(&cpu);assert(story_scenes::active());test_tick(&cpu,true);assert(!story_scenes::active()&&finished==1&&end_calls==3);
    assert(story_scenes::request("mod","intro"));test_tick(&cpu);assert(story_scenes::active());pad_buttons=input::kPlus;test_tick(&cpu);assert(!story_scenes::active()&&finished==1);pad_buttons=0;test_tick(&cpu);
    assert(story_scenes::request("mod","intro"));test_tick(&cpu);assert(story_scenes::active());story_scenes::state_loaded();test_tick(&cpu);assert(!story_scenes::active()&&finished==1&&end_calls==5);
    // Interruption by a different native event never resets the new owner's control/camera.
    assert(story_scenes::request("mod","intro"));test_tick(&cpu);st8(kPlay+0x5292,1);test_tick(&cpu);assert(!story_scenes::active()&&end_calls==5&&ld8(kPlay+0x5292)==1);
    SceneStep move;move.kind=SceneStep::Kind::Move;move.frames=4;s.steps={move,end};assert(!story_scenes::declare("unsupported",{s},why));assert(why.find("placed resident")!=std::string::npos);
    st8(kPlay+0x5292,0);move.actor="resident";move.position={10,0,20};s.id="walking";s.steps={move,end};
    st16(kActor+8,0x175);st32(kActor+0x8A0,kActor+0x2000);st32(kActor+0x8C0,f32_as_u32(30));st32(kActor+0x2000+0x40,f32_as_u32(100));
    assert(story_scenes::declare("mod",{s},why));assert(story_scenes::request("mod","walking"));test_tick(&cpu);
    for(unsigned i=0;i<10&&story_scenes::active();++i)test_tick(&cpu);
    assert(!story_scenes::active()&&finished==2&&std::fabs(u32_as_f32(ld32(kActor+0x314))-10)<1&&std::fabs(u32_as_f32(ld32(kActor+0x31C))-20)<1);
    // Skip while a real message is waiting for input: native FINISH16 alone can
    // require another A press. Non-natural exits reset only the owned message core
    // immediately and restore control even when the window never progresses.
    advance_close=false;
    const unsigned before_abort_resets=reset_calls;
    for(unsigned cycle=0;cycle<5;++cycle){
        assert(story_scenes::request("mod","intro"));test_tick(&cpu);test_tick(&cpu);test_tick(&cpu);test_tick(&cpu);
        assert(message_status==14&&ld8(kScratch+13)==0&&ld32(kScratch+160)==0);
        const unsigned prior_ends=end_calls;
        if(cycle%2){story_scenes::abort();test_tick(&cpu);}else test_tick(&cpu,true);
        assert(!story_scenes::active()&&message_status==0&&end_calls==prior_ends+1);
        assert(reset_calls==before_abort_resets+cycle+1&&ld8(kPlay+0x5292)==0);
    }
    // A normal dialogue that receives no further confirm expires at its declared
    // scene deadline, resets immediately and cannot apply end rewards.
    const unsigned before_deadline_resets=reset_calls,before_deadline_rewards=finished;
    assert(story_scenes::request("mod","intro"));test_tick(&cpu);
    for(unsigned i=0;i<10&&story_scenes::active();++i)test_tick(&cpu);
    assert(!story_scenes::active()&&reset_calls==before_deadline_resets+1&&message_status==0&&finished==before_deadline_rewards);
    // A full-state abort during an owned dialogue follows the same no-input path.
    assert(story_scenes::request("mod","intro"));test_tick(&cpu);test_tick(&cpu);test_tick(&cpu);test_tick(&cpu);
    const unsigned before_state_resets=reset_calls;story_scenes::state_loaded();test_tick(&cpu);
    assert(!story_scenes::active()&&reset_calls==before_state_resets+1&&message_status==0&&ld8(kPlay+0x5292)==0);
    advance_close=true;
    // Walking cleanup also runs if a different event takes over the event control.
    st32(kActor+0x370,f32_as_u32(19));assert(story_scenes::request("mod","walking"));test_tick(&cpu);st32(kActor+0x314,0);test_tick(&cpu);st8(kPlay+0x5292,1);test_tick(&cpu);assert(!story_scenes::active()&&u32_as_f32(ld32(kActor+0x370))==19&&ld8(kPlay+0x5292)==1);
    // A queued room trigger belongs to its room; loading there aborts without retriggering.
    s.id="room-scene";s.trigger="room";s.stage="sea";s.room=1;s.steps={camera,end};assert(story_scenes::declare("mod",{s},why));st8(kActor+0x326,1);test_tick(&cpu);assert(!story_scenes::active());st8(kActor+0x326,2);st8(kPlay+0x5292,0);test_tick(&cpu);assert(!story_scenes::active());st8(kActor+0x326,1);test_tick(&cpu);assert(story_scenes::active());story_scenes::state_loaded();test_tick(&cpu);assert(!story_scenes::active());test_tick(&cpu);assert(!story_scenes::active());
    face.actor="link";s.id="link-face";s.trigger.clear();s.steps={face,end};assert(!story_scenes::declare("mod",{s},why));
    face.actor="resident";s.id="resident-face";s.steps={face,end};assert(story_scenes::declare("mod",{s},why));
    const auto before_face_rewards=finished;const auto prior_angle=ld16(kActor+0x322);
    for(uint16_t process:{uint16_t(0xB6),uint16_t(0x125)}){st16(kActor+8,process);assert(story_scenes::request("mod","resident-face"));test_tick(&cpu);assert(!story_scenes::active()&&finished==before_face_rewards&&ld16(kActor+0x322)==prior_angle);}
    st16(kActor+8,0x175);
    std::puts("story scene native contract tests passed (synthetic memory)");
}
