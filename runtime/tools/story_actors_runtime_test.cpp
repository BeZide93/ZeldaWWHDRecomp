// Synthetic guest memory: exercise the shipped actor spawn/check/open hooks, with no game assets.
#include "mods/story_actors.h"
#include "mods/mod_actor.h"
#include "mods/story_mail.h"
#include "mods/story_store.h"
#include "mods/story_items.h"
#include "mods/story_notebook.h"
#include "mods/story_treasure.h"
#include "runtime.h"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...) {}
namespace {
constexpr uint32_t kArena=0x20000000,kScratch=kArena+0x2000,kActor=kArena+0x4000,kSave=kArena+0x8000;
std::array<uint8_t,1024> extension{};
std::map<uint32_t,void(*)(Cpu*)> hooks;
uint32_t created=0,actor_id=100;unsigned originals=0;
uint32_t current_layer=kArena+0x6800;bool room_available=true;
bool pending=false,nested_chest=false;int32_t quest_state=1,item_count=1;unsigned deleted=0,native_calls=0;
std::map<uint32_t,uint32_t> live;

void map_guest(uint32_t at,uint32_t size) {
#ifdef _WIN32
    assert(VirtualAlloc(mem::ptr(at),size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
    assert(mmap(mem::ptr(at),size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
void original(Cpu* c,uint32_t fn) {
    ++originals;
    if(fn==0x022C36D0){
        assert(ld32(c->r[3]+0x100)==0&&ld32(c->r[3]+0x104)==0&&ld32(c->r[3]+0x108)==0);
        extension[100]=0xB7; // story extension remains independent
        st8(kSave+0x20,0x42); // unrelated game state is outside the rollback range
        c->r[3]=0xBEEF;
    }
    if(fn==0x022C2958)st8(kSave+0x644,0xEF); // dangerous retail event path
    if(fn==0x022C045C){st16(c->r[3]+0xFC,42);st8(c->r[3]+0x910,0);}
    if(fn==0x022C2A04&&c->r[3]==kActor+0x1000){assert(ld8(c->r[3]+0x901)<=2);assert(ld16(c->r[3]+0x8F2)==0x8000);assert(ld16(c->r[3]+0xFC)==0xFFFF);st16(c->r[3]+0xFA,0xFFFF);}
    if(fn==0x024B63A8){
        if(nested_chest&&c->r[3]==kActor){Cpu vanilla=*c;vanilla.r[3]=kActor+0x3000;hooks.at(0x024B63A8)(&vanilla);}
        Cpu set=*c;set.r[3]=kSave+0x798;set.r[4]=0;hooks.at(0x025B8C0C)(&set);
    }
    if(fn==0x025B8C0C)st32(c->r[3],ld32(c->r[3])|1);
}
uint32_t call(Cpu*,uint32_t fn,std::initializer_list<uint32_t> args) {
    ++native_calls;const auto* a=args.begin();
    if(fn==0x025200D4)return 0x1046F0B0;
    if(fn==0x025DC80C){assert((a[0]>=1000&&a[0]<=1003)||(a[0]>=2000&&a[0]<=2003));return room_available?kArena+0x6000+(a[0]%1000)*0x200:0;}
    if(fn==0x025DED64)return current_layer;
    if(fn==0x025DEAB4){current_layer=a[0];return 0;}
    if(fn==0x025D57E0){++deleted;assert(a[0]>=kActor&&a[0]<=kActor+0x2000);return 1;}
    if(fn==0x025C109C){assert(std::strcmp(reinterpret_cast<char*>(mem::ptr(a[0])),"takara")==0);st16(kArena+8,0x125);return kArena;}
    if(fn==0x025D5834){++created;assert(args.size()==8);assert(current_layer==kArena+0x6000+a[3]*0x200+0xC0);
        uint32_t address=kActor+(a[0]==0x175?0x1000:a[0]==0xB6?0x2000:a[0]==mod_actor::kProfile?0x3000:0);uint32_t id=actor_id+(address-kActor)/0x1000;
        if(a[0]==0x125){assert(a[1]==0);assert(a[3]==0);assert(ld16(a[4]+4)==0x06FF);}
        else if(a[0]==0x175){assert(a[1]==0x00FF0000);assert(a[3]==1);assert(a[6]==9);assert(ld16(a[4]+2)==0x4000);}
        else if(a[0]==mod_actor::kProfile){assert(a[1]==1&&a[3]==3);}
        else {assert(a[0]==0xB6&&a[1]==0x2CF3&&a[3]==2);}
        st32(address+4,id);st32(address+0x2C,current_layer);live[id]=address;return id;
    }
    if(fn==0x025D54C4){st32(a[1],pending?0:live[a[0]]);return live[a[0]]!=0;}
    assert(false);return 0;
}
}
namespace dispatch {PpcFunc lookup(uint32_t){return nullptr;}uint32_t register_host(PpcFunc,const char*){static uint32_t next=0xF1000000;next+=4;return next;}}
namespace story_store {story_mail::Book book(){return story_mail::Book(extension.data(),255);}}
namespace story_mail {void say(uint32_t,const std::u16string*){}void unsay(){}bool can_say(){return true;}}
namespace story_notebook {int32_t state(const std::string&,const std::string&){return quest_state;}int32_t reach(const std::string&,const std::string&,uint32_t){return 1;}}
namespace story_items {
int32_t give(const std::string&,const std::string&,uint32_t,const char*,bool){return 1;}
int32_t take(const std::string&,const std::string&,uint32_t){return 0;}
int32_t count(const std::string&,const std::string&){return item_count;}
}
namespace story_treasure {bool installed(){return false;}}
namespace story_scenes {void trigger(const std::string&,const std::string&,const std::string&,unsigned){}}
int main(){
    map_guest(0x101F0000,0x10000);map_guest(0x10460000,0x20000);map_guest(kArena,0x10000);
    st32(0x101F84DC,kSave);st32(0x1046F0B0+0x5B34,kArena+0x7000);for(unsigned room=0;room<4;++room)st32(0x1047E8F0+room*0x22C,1000+room);std::memcpy(mem::ptr(0x1046F0B0+0x5134),"sea",4);
    std::memset(mem::ptr(kSave),0xA5,0x800);std::array<uint8_t,0x800> before;std::memcpy(before.data(),mem::ptr(kSave),before.size());
    story_actors::set_game_calls(original,call);story_actors::set_scratch_memory(kScratch);
    auto replaced=[](uint32_t){return std::string{};};auto replace=[](uint32_t a,void(*h)(Cpu*)){hooks[a]=h;};
    Cpu idle{};story_actors::service(&idle);assert(native_calls==0);
    story_actors::install(replaced,replace);assert(hooks.empty());assert((extension==std::array<uint8_t,1024>{}));
    story_notebook::ManifestActor chest;chest.id="chest";chest.actor="chest";chest.stage="sea";chest.room=0;chest.flag="opened";
    std::string why;story_notebook::ManifestActor resident;resident.id="guide";resident.actor="resident";resident.stage="sea";resident.room=1;resident.rotation.y=90;resident.quest="q";resident.quest_step=1;resident.item="item:token";
    story_notebook::ManifestActor sign;sign.id="sign";sign.actor="sign";sign.stage="sea";sign.room=2;sign.flag="read";sign.time="night";
    story_notebook::ManifestActor own;own.kind="mod";own.id="post";own.model="postmodel";own.stage="sea";own.room=3;
    assert(story_actors::register_model("test","postmodel","unique_post",1,why));
    assert(!story_actors::register_model("test","othermodel","unique_post",1,why));
    st32(0x101F3EE0+0xB6*4,kArena+0x6000);
    assert(story_actors::load("test",{chest,resident,sign,own},why));story_actors::set_scratch_memory(kScratch);story_actors::install(replaced,replace);
    Cpu cpu{};story_actors::native_room_loaded(&cpu,0);assert(created==1&&current_layer==kArena+0x6800&&ld32(kActor+0x2C)==kArena+0x60C0);
    pending=true;story_actors::native_room_loaded(&cpu,0);assert(created==1);pending=false;
    Cpu check{};check.r[3]=kActor;hooks.at(0x024B4AB4)(&check);assert(check.r[3]==0);
    Cpu open{};open.r[3]=kActor;hooks.at(0x024B63A8)(&open);
    assert(story_actors::flag(story_store::book(),"test","opened"));
    check.r[3]=kActor;hooks.at(0x024B4AB4)(&check);assert(check.r[3]==1);
    assert(std::memcmp(before.data(),mem::ptr(kSave),before.size())==0);
    story_actors::state_loaded();assert(story_actors::actor_address(&cpu,"test","chest")==kActor);
    // A vanilla open nested inside a mod open clears the outer mapping for its own setter,
    // then restores it for the outer mod chest's setter.
    assert(story_actors::set_flag(story_store::book(),"test","opened",false));
    st32(kSave+0x798,0);nested_chest=true;open.r[3]=kActor;hooks.at(0x024B63A8)(&open);nested_chest=false;
    assert(ld32(kSave+0x798)==1);
    assert(story_actors::flag(story_store::book(),"test","opened"));
    // An ordinary chest still writes the game's own flag through the unmodified original route.
    st32(kSave+0x798,0);Cpu ordinary{};ordinary.r[3]=kSave+0x798;ordinary.r[4]=0;hooks.at(0x025B8C0C)(&ordinary);assert(ld32(kSave+0x798)==1);
    // Room/stage and mod prerequisites control creation; existing actor ids prevent duplicates.
    quest_state=0;story_actors::native_room_loaded(&cpu,1);assert(created==1);
    quest_state=1;item_count=0;story_actors::native_room_loaded(&cpu,1);assert(created==1);
    item_count=1;std::memcpy(mem::ptr(0x1046F0B0+0x5134),"other",6);story_actors::native_room_loaded(&cpu,1);assert(created==1);
    std::memcpy(mem::ptr(0x1046F0B0+0x5134),"sea",4);story_actors::native_room_loaded(&cpu,1);assert(created==2);
    story_actors::native_room_loaded(&cpu,1);assert(created==2);
    // Resident execution strips private retail callbacks and preserves CPU return.
    const uint8_t event_before=ld8(kSave+0x644),memory_before=ld8(kSave+0x798);
    Cpu execute{};execute.r[3]=kActor+0x1000;hooks.at(0x022C36D0)(&execute);
    assert(execute.r[3]==0xBEEF);assert(ld8(kSave+0x644)==event_before&&ld8(kSave+0x798)==memory_before);
    assert(extension[100]==0xB7&&ld8(kSave+0x20)==0x42);
    Cpu guarded{};guarded.r[3]=kActor+0x1000;
    unsigned calls_before=originals;hooks.at(0x022C2958)(&guarded);assert(originals==calls_before&&ld8(kSave+0x644)==event_before);
    hooks.at(0x022C045C)(&guarded);assert(ld16(guarded.r[3]+0xFC)==0xFFFF&&ld8(guarded.r[3]+0x910)==1);
    st8(guarded.r[3]+0x901,9);st16(guarded.r[3]+0x8F2,0xFFFF);hooks.at(0x022C2A04)(&guarded);
    assert(ld8(guarded.r[3]+0x901)==0&&ld16(guarded.r[3]+0xFA)==0xFFDF);
    Cpu ordinary_resident{};ordinary_resident.r[3]=kActor+0x3000;
    assert(!story_actors::people_execute(&ordinary_resident,true));
    hooks.at(0x022C2958)(&ordinary_resident);assert(ld8(kSave+0x644)==0xEF);
    st32(kSave+0x44,f32_as_u32(180.0f));story_actors::native_room_loaded(&cpu,2);assert(created==2);
    st32(kSave+0x44,f32_as_u32(300.0f));story_actors::native_room_loaded(&cpu,2);assert(created==3);assert(story_actors::actor_address(&cpu,"test","sign")==kActor+0x2000);
    // Reconcile the active native room only after its room setup readiness marker exists.
    st32(0x1046F0B0+0x5B34,kArena+0x7000);st8(kArena+0x7000+0x326,1);
    quest_state=0;story_actors::service(&cpu);assert(deleted==1&&created==3);
    story_actors::service(&cpu);assert(deleted==1); // native deletion is asynchronous
    quest_state=1;story_actors::service(&cpu);assert(created==3); // wait for queued deletion
    live.erase(101);story_actors::service(&cpu);assert(created==4);
    pending=true;quest_state=0;story_actors::service(&cpu);assert(deleted==1&&created==4);
    pending=false;story_actors::service(&cpu);assert(deleted==2); // create materialized: now remove
    live.erase(101);story_actors::service(&cpu);assert(created==4);
    quest_state=1;story_actors::service(&cpu);assert(created==5);
    // A state without readiness for this room must not race native room actor setup.
    st8(kScratch+0x308,uint8_t(ld8(kScratch+0x308)&~2u));quest_state=0;
    story_actors::service(&cpu);assert(deleted==2);
    story_actors::native_room_loaded(&cpu,1);story_actors::service(&cpu);assert(deleted==3);
    live.erase(101);quest_state=1;story_actors::service(&cpu);assert(created==6);
    item_count=0;story_actors::service(&cpu);assert(deleted==4);live.erase(101);item_count=1;story_actors::service(&cpu);assert(created==7);
    st8(kArena+0x7000+0x326,2);st32(kSave+0x44,f32_as_u32(180));story_actors::service(&cpu);assert(deleted==5);
    // Flag names from scene actions and actor actions share a collision-checked registry.
    std::map<uint16_t,std::string> candidates;std::string first,second;
    for(unsigned i=0;i<100000&&second.empty();++i){const std::string id="flag"+std::to_string(i);const auto key=story_actors::flag_key("collision",id);auto [it,inserted]=candidates.emplace(key,id);if(!inserted){first=it->second;second=id;}}
    assert(!second.empty());assert(story_actors::register_flags("collision",{first},why));
    assert(!story_actors::register_flags("collision",{second},why)&&why.find("collision")!=std::string::npos);
    auto colliding=sign;colliding.id="colliding";colliding.flag="unique";colliding.then={{story_notebook::StoryAction::Kind::Flag,second,1}};
    assert(!story_actors::load("collision",{colliding},why));
    assert(story_actors::register_flags("collision",{first},why));
    const unsigned prior_created=created;room_available=false;story_actors::native_room_loaded(&cpu,3);assert(created==prior_created);room_available=true;st8(kArena+0x6600+0x5C,1);story_actors::native_room_loaded(&cpu,3);assert(created==prior_created);st8(kArena+0x6600+0x5C,0);
    st32(kArena+0x6600+0x2C,kArena+0x6800);st32(kArena+0x6800+0x18,kArena+0x6A00);st8(kArena+0x6A00+0x5C,1);story_actors::native_room_loaded(&cpu,3);assert(created==prior_created);st8(kArena+0x6A00+0x5C,0);
    story_actors::native_room_loaded(&cpu,3);assert(created==prior_created+1);
    assert(story_actors::actor_address(&cpu,"test","post")==kActor+0x3000);
    story_actors::state_loaded();assert(story_actors::actor_address(&cpu,"test","post")==kActor+0x3000);
    assert(!story_actors::register_model("test","late","late_archive",1,why));
    // Failed native creation stays bounded while service polls the same room, then retries on real room setup.
    live.erase(103);st8(kScratch+story_actors::kModScratchOffset+256,1);const unsigned failed_count=created;
    story_actors::room_loaded(&cpu,3);story_actors::room_loaded(&cpu,3);assert(created==failed_count);
    cpu.r[5]=3;hooks.at(0x025C2E8C)(&cpu);assert(created==failed_count+1);
    // Old readiness bits cannot authorize placement in a newly created room or stage process.
    live.erase(103);const unsigned epoch_count=created;st32(0x1047E8F0+3*0x22C,2003);
    story_actors::room_loaded(&cpu,3);assert(created==epoch_count);story_actors::native_room_loaded(&cpu,3);assert(created==epoch_count+1);
    live.erase(103);st32(0x1047E6C4,99);story_actors::room_loaded(&cpu,3);assert(created==epoch_count+1);
    story_actors::native_room_loaded(&cpu,3);assert(created==epoch_count+2);
    // Owned native execute paths must tolerate the frame where native teardown has already removed Link.
    st32(0x1046F0B0+0x5B34,0);const unsigned teardown_originals=originals;
    Cpu teardown{};teardown.r[3]=kActor+0x2000;hooks.at(0x0218B234)(&teardown);assert(teardown.r[3]==1&&originals==teardown_originals);
    teardown.r[3]=kActor+0x1000;hooks.at(0x022C36D0)(&teardown);assert(teardown.r[3]==1&&originals==teardown_originals);
    teardown.r[3]=kActor+0x5000;hooks.at(0x0218B234)(&teardown);assert(originals==teardown_originals+1);
    std::puts("actor runtime spawn, conditions, CPU return, retail event prevention, chest isolation and state restore passed");
}
