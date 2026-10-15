#include "story_actors.h"
#include "mod_actor.h"
#include "mod_actor_shadow_proof.h"
#include "story_items.h"
#include "story_notebook.h"
#include "story_mail.h"
#include "story_store.h"
#include "story_scenes.h"
#include "story_treasure.h"
#include "guest_addr.h"
#include "runtime.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>

namespace story_actors {
static_assert(kScratchBytes>=kModScratchOffset+mod_actor::kScratchBytes);
namespace {
using story_notebook::ManifestActor;
struct Actor {std::string mod; ManifestActor spec; uint32_t process=0xFFFFFFFFu; bool deleting=false; uint32_t mod_token=0;};
std::vector<Actor> g_actors;
struct ModelResource {std::string archive;int index;story_notebook::WorldPoint minimum,maximum;};
std::map<std::pair<std::string,std::string>,ModelResource> g_models;
std::map<uint16_t,std::pair<std::string,std::string>> g_flag_names;

void(*g_original)(Cpu*,uint32_t)=nullptr;
uint32_t(*g_call)(Cpu*,uint32_t,std::initializer_list<uint32_t>)=nullptr;
uint32_t g_scratch=0; bool g_installed=false;
std::function<bool(const std::string&,const std::string&)> g_scene;
const uint32_t kRoom=GC(0x025C2E8C),kCreate=GC(0x025D5834),kSearch=GC(0x025D54C4),kSearchName=GC(0x025C109C);
const uint32_t kPlay=GC(0x025200D4),kDelete=GC(0x025D57E0);
const uint32_t kStage=GD(0x1046F0B0)+0x5134,kSave=GD(0x101F84DC);
const uint32_t kChestCheck=GC(0x024B4AB4),kChestOpen=GC(0x024B63A8),kOnBox=GC(0x025B8C0C);
const uint32_t kSignExecute=GC(0x0218B234),kSignDelete=GC(0x0218C824);
const uint32_t kPeopleMsg=GC(0x022C6C68),kPeopleNext=GC(0x022C6108),kPeopleExecute=GC(0x022C36D0);
const uint32_t kPeopleCheckOrder=GC(0x022C045C),kPeopleEventMove=GC(0x022C2958),kPeopleEventOrder=GC(0x022C2A04);
constexpr uint32_t kCarrier=0x2CF3;
Actor* g_opening=nullptr;
struct Talk {uint32_t npc=0; Actor* actor=nullptr;std::u16string text;bool started=false;};
Talk g_talk;
Actor* find(uint32_t address) {
    if(!address||g_actors.empty())return nullptr;
    const uint32_t id=ld32(address+4);
    for(auto& a:g_actors)if(a.process==id)return &a;
    return nullptr;
}
bool eligible(const Actor& a) {
    if(!a.spec.quest.empty()&&story_notebook::state(a.mod,a.spec.quest)!=int32_t(a.spec.quest_step))return false;
    if(!a.spec.item.empty()&&story_items::count(a.mod,a.spec.item.substr(5))<=0)return false;
    if(!a.spec.time.empty()) {
        const uint32_t save=ld32(kSave);if(!save)return false;
        const float hour=u32_as_f32(ld32(save+0x44))/15.0f;
        const bool day=hour>=6&&hour<18;
        if(day!=(a.spec.time=="day"))return false;
    }
    return true;
}
void room_hook(Cpu* c) {const int room=int32_t(c->r[5]);g_original(c,kRoom);native_room_loaded(c,room);}
void chest_check(Cpu* c) {
    Actor* a=find(c->r[3]);if(!a||a->spec.actor!="chest")return g_original(c,kChestCheck);
    c->r[3]=flag(story_store::book(),a->mod,a->spec.flag)?1:0;
}
void chest_on(Cpu* c) {
    if(!g_opening)return g_original(c,kOnBox);
    if(!set_flag(story_store::book(),g_opening->mod,g_opening->spec.flag))LOG("[actors] cannot persist chest %s: story records full",g_opening->spec.id.c_str());
}
void chest_open(Cpu* c) {
    Actor* a=find(c->r[3]);
    Actor* keep=g_opening;g_opening=a&&a->spec.actor=="chest"?a:nullptr;
    g_original(c,kChestOpen);g_opening=keep;
}
void end_talk() {
    if(!g_talk.actor)return;
    Actor* actor=g_talk.actor;
    story_mail::unsay();g_talk={};
    LOG("[actors] %s:%s dialogue finished",actor->mod.c_str(),actor->spec.id.c_str());
    if(!actor->spec.flag.empty())set_flag(story_store::book(),actor->mod,actor->spec.flag);
    actions(actor->mod,actor->spec.then);
    story_scenes::trigger("talk",actor->mod,actor->spec.id);
}
bool scene_is_deleting(uint32_t scene){
    // Native delete_tag's in-queue flag is +0x4C+0x10. Follow layer ownership to include a
    // parent stage already draining its children, before this room itself reaches the delete queue.
    for(unsigned depth=0;scene&&depth<16;++depth){
        if(ld8(scene+0x5C))return true;
        const uint32_t parent_layer=ld32(scene+0x2C);
        if(!parent_layer)return false;
        scene=ld32(parent_layer+0x18);
        if(!scene)return false;
    }
    return scene!=0;
}
bool player_present(Cpu* c){Cpu probe=*c;const uint32_t play=g_call(&probe,kPlay,{});return play&&ld32(play+0x5B34);}
void sign_execute(Cpu* c) {
    const uint32_t npc=c->r[3];Actor* a=find(npc);
    if(!a||a->spec.actor!="sign")return g_original(c,kSignExecute);
    if(!player_present(c)){c->r[3]=1;return;}
    if(ld16(npc+0xF8)==1&&g_talk.npc==0&&!a->spec.lines.empty()&&story_mail::can_say()) {
        g_talk.npc=npc;g_talk.actor=a;g_talk.started=true;
        story_mail::append_utf16(a->spec.lines,true,g_talk.text);story_mail::say(kCarrier,&g_talk.text);
    }
    g_original(c,kSignExecute);
    if(g_talk.npc==npc&&ld16(npc+0xF8)==0)end_talk();
}
void sign_delete(Cpu* c) {
    Actor* a=find(c->r[3]);if(a){if(g_talk.actor==a){story_mail::unsay();g_talk={};}a->process=0xFFFFFFFFu;}
    g_original(c,kSignDelete);
}
// Um1 normally routes nighttime talks through a retail event script. Mod residents always
// use the generic talk command instead, and never acquire a retail event staff/cut interpreter.
void people_check_order(Cpu* c) {
    const uint32_t actor=c->r[3];Actor* a=find(actor);
    g_original(c,kPeopleCheckOrder);
    if(!a||a->spec.actor!="resident")return;
    st16(actor+0xFC,0xFFFF);  // eventInfo.mEventId: no retail event
    st8(actor+0x910,1);      // m79C: generic native dialogue at either time of day
}
void people_event_move(Cpu* c) {
    Actor* a=find(c->r[3]);
    if(!a||a->spec.actor!="resident")g_original(c,kPeopleEventMove);
}
void people_event_order(Cpu* c) {
    const uint32_t actor=c->r[3];Actor* a=find(actor);
    if(a&&a->spec.actor=="resident"){
        if(ld8(actor+0x901)>2)st8(actor+0x901,0); // permit native talk orders only
        st16(actor+0x8F2,uint16_t(ld16(actor+0x8F2)&0x8000)); // drop retail reward/event transitions
        st16(actor+0xFC,0xFFFF);
    }
    g_original(c,kPeopleEventOrder);
    if(a&&a->spec.actor=="resident")st16(actor+0xFA,uint16_t(ld16(actor+0xFA)&~0x0020u)); // no retail item-show conversation
}
void people_msg_hook(Cpu* c){if(!people_message(c))g_original(c,kPeopleMsg);}
void people_next_hook(Cpu* c){if(!people_next_message(c))g_original(c,kPeopleNext);}
void people_execute_hook(Cpu* c){const uint32_t actor=c->r[3];if(find(actor)&&!player_present(c)){c->r[3]=1;return;}people_execute(c,true);g_original(c,kPeopleExecute);Cpu keep=*c;c->r[3]=actor;people_execute(c,false);*c=keep;}
}
bool load(const std::string& mod,const std::vector<ManifestActor>& actors,std::string& why) {
    if(g_actors.size()+actors.size()>128)return why="enabled mods exceed 128 actors overall",false;
    std::vector<std::string> flags;
    for(const auto& a:actors){if(!a.flag.empty())flags.push_back(a.flag);for(const auto& action:a.then)if(action.kind==story_notebook::StoryAction::Kind::Flag)flags.push_back(action.id);}
    for(const auto& a:actors) {
        unsigned count=0;for(const auto& old:g_actors)if(old.spec.stage==a.stage&&old.spec.room==a.room)++count;
        for(const auto& other:actors)if(other.stage==a.stage&&other.room==a.room)++count;
        if(count>story_notebook::kMaxActorsPerRoom)return why="enabled mods exceed 16 actors per room",false;
    }
    if(!register_flags(mod,flags,why))return false;
    for(const auto& a:actors)g_actors.push_back({mod,a});return true;
}
bool register_flags(const std::string& mod,const std::vector<std::string>& flags,std::string& why){
    auto names=g_flag_names;
    for(const auto& id:flags){
        if(!story_mail::valid_id(id))return why="invalid mod-owned actor/scene flag id",false;
        const auto key=flag_key(mod,id);auto found=names.find(key);
        if(found!=names.end()&&found->second!=std::make_pair(mod,id))return why="mod-owned flag hash collision: "+mod+":"+id+" conflicts with "+found->second.first+":"+found->second.second,false;
        names[key]={mod,id};
    }
    g_flag_names=std::move(names);return true;
}
bool declared(){return !g_actors.empty();}
bool mod_declared(){return std::any_of(g_actors.begin(),g_actors.end(),[](const Actor& a){return a.spec.kind=="mod";});}
bool register_model(const std::string& mod,const std::string& model,const std::string& archive,int index,std::string& why,story_notebook::WorldPoint minimum,story_notebook::WorldPoint maximum){
    if(g_installed)return why="actor models must be registered before hooks are installed",false;
    if(mod.empty()||model.empty()||archive.empty()||archive.size()>63||index<0||index>16)return why="invalid prepared actor model resource",false;
    const float lo[]={minimum.x,minimum.y,minimum.z},hi[]={maximum.x,maximum.y,maximum.z};for(unsigned i=0;i<3;++i)if(!std::isfinite(lo[i])||!std::isfinite(hi[i])||lo[i]>hi[i])return why="invalid prepared model bounds",false;
    const auto key=std::make_pair(mod,model);if(g_models.contains(key))return why="duplicate prepared actor model",false;
    for(const auto& entry:g_models)if(entry.second.archive==archive)return why="actor model archive identity collision",false;
    g_models.emplace(key,ModelResource{archive,index,minimum,maximum});return true;
}
void set_game_calls(void(*original)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>)){g_original=original;g_call=call;}
void set_scratch_memory(uint32_t guest){g_scratch=guest;for(size_t i=0;i<g_actors.size();++i)st32(g_scratch+128+uint32_t(i)*4,0xFFFFFFFFu);std::memset(mem::ptr(g_scratch+0x300),0,28);std::memset(mem::ptr(g_scratch+0x400),0,256);}
void set_scene_start(std::function<bool(const std::string&,const std::string&)> start){g_scene=std::move(start);}
void native_room_loaded(Cpu* c,int room){
    if(!g_scratch||room<0||room>63)return;
    const uint32_t stage_process=ld32(GD(0x1047E6C4));
    if(ld32(g_scratch+0x318)!=stage_process){std::memset(mem::ptr(g_scratch+0x308),0,8);std::memset(mem::ptr(g_scratch+0x400),0,256);st32(g_scratch+0x318,stage_process);}
    st32(g_scratch+0x400+uint32_t(room)*4,ld32(GD(0x1047E8F0)+uint32_t(room)*0x22C));
    for(const auto& a:g_actors)if(a.spec.kind=="mod"&&a.spec.room==unsigned(room))mod_actor::retry(a.mod_token);room_loaded(c,room);}
void room_loaded(Cpu* c,int room) {
    if(!g_installed||!g_scratch)return;
    const char* stage=reinterpret_cast<const char*>(mem::ptr(kStage));
    if(room<0||room>63)return;
    if(ld32(g_scratch+0x318)!=ld32(GD(0x1047E6C4))||ld32(g_scratch+0x400+uint32_t(room)*4)!=ld32(GD(0x1047E8F0)+uint32_t(room)*0x22C))return;
    // Remember native room setup in guest memory so state loads preserve the readiness gate.
    if(std::memcmp(mem::ptr(g_scratch+0x300),stage,8)!=0){std::memcpy(mem::ptr(g_scratch+0x300),stage,8);std::memset(mem::ptr(g_scratch+0x308),0,8);}
    st8(g_scratch+0x308+uint32_t(room)/8,uint8_t(ld8(g_scratch+0x308+uint32_t(room)/8)|(1u<<(uint32_t(room)%8))));
    Cpu room_probe=*c;const uint32_t room_scene=g_call(&room_probe,GC(0x025DC80C),{ld32(GD(0x1047E8F0)+uint32_t(room)*0x22C)});
    if(!room_scene||scene_is_deleting(room_scene))return; // Never enqueue new children into a draining room/stage.
    const uint32_t room_layer=room_scene+0xC0;
    unsigned count=0;
    for(auto& a:g_actors) {
        if(a.spec.room!=unsigned(room)||std::strncmp(stage,a.spec.stage.c_str(),8)!=0||!eligible(a))continue;
        if(a.process!=0xFFFFFFFFu) {
            Cpu probe=*c;st32(g_scratch+0x40,0);const uint32_t exists=g_call(&probe,kSearch,{a.process,g_scratch+0x40});
            if(exists)continue;  // also true while asynchronous actor creation is pending
        }
        if(++count>story_notebook::kMaxActorsPerRoom)break;
        if(!a.spec.flag.empty()){
            const auto book=story_store::book();
            if(!book.has(kRecordActorFlag,flag_key(a.mod,a.spec.flag))&&!set_flag(book,a.mod,a.spec.flag,false)){
                LOG("[actors] %s:%s omitted: no story flag record available",a.mod.c_str(),a.spec.id.c_str());continue;
            }
        }
        if(a.spec.kind=="mod"&&(!a.mod_token||mod_actor::failed(a.mod_token)))continue;
        uint32_t proc=a.spec.kind=="mod"?mod_actor::kProfile:a.spec.actor=="sign"?0xB6u:0x175u;
        if(a.spec.actor=="chest") {
            std::memcpy(mem::ptr(g_scratch+0x50),"takara",7);Cpu probe=*c;
            const uint32_t entry=g_call(&probe,kSearchName,{g_scratch+0x50});
            if(!entry){LOG("[actors] takara object name unavailable; chest omitted");continue;}proc=ld16(entry+8);
        }
        const float p[]={a.spec.position.x,a.spec.position.y,a.spec.position.z};
        const float s[]={a.spec.scale.x,a.spec.scale.y,a.spec.scale.z};
        const float r[]={a.spec.rotation.x,a.spec.rotation.y,a.spec.rotation.z};
        for(unsigned i=0;i<3;++i){st32(g_scratch+i*4,f32_as_u32(p[i]));st32(g_scratch+0x14+i*4,f32_as_u32(s[i]));st16(g_scratch+0xC+i*2,uint16_t(int32_t(r[i]*65536.0f/360.0f)));}
        if(a.spec.actor=="chest")st16(g_scratch+0x10,uint16_t((uint32_t(a.spec.item_game)<<8)|255));
        const uint32_t params=a.spec.kind=="mod"?a.mod_token:a.spec.actor=="sign"?kCarrier:a.spec.actor=="resident"?0x00FF0000u:0;
        Cpu call=*c;a.deleting=false;
        const uint32_t previous_layer=g_call(&call,GC(0x025DED64),{});
        g_call(&call,GC(0x025DEAB4),{room_layer});
        try{a.process=g_call(&call,kCreate,{proc,params,g_scratch,uint32_t(room),g_scratch+0xC,g_scratch+0x14,a.spec.actor=="resident"?a.spec.resident:0xFFFFFFFFu,0});}
        catch(...){g_call(&call,GC(0x025DEAB4),{previous_layer});throw;}
        g_call(&call,GC(0x025DEAB4),{previous_layer});
        st32(g_scratch+128+uint32_t(&a-g_actors.data())*4,a.process);
        LOG("[actors] %s:%s room %d process %08X room_layer=%08X",a.mod.c_str(),a.spec.id.c_str(),room,a.process,room_layer);
    }
}
void service(Cpu* c){
    if(!g_installed||!g_scratch)return;
    Cpu probe=*c;const uint32_t play=g_call(&probe,kPlay,{});if(!play)return;
    const uint32_t player=ld32(play+0x5B34);if(!player)return;
    const int room=int8_t(ld8(player+0x326));if(room<0||room>63)return;
    if(ld32(g_scratch+0x318)!=ld32(GD(0x1047E6C4))||ld32(g_scratch+0x400+uint32_t(room)*4)!=ld32(GD(0x1047E8F0)+uint32_t(room)*0x22C))return;
    if(std::memcmp(mem::ptr(g_scratch+0x300),mem::ptr(kStage),8)!=0||!(ld8(g_scratch+0x308+uint32_t(room)/8)&(1u<<(uint32_t(room)%8))))return;
    for(auto& a:g_actors){
        if(a.spec.room!=unsigned(room)||std::strncmp(reinterpret_cast<const char*>(mem::ptr(kStage)),a.spec.stage.c_str(),8)!=0||a.process==0xFFFFFFFFu||eligible(a))continue;
        if(g_talk.actor==&a)continue; // finish native dialogue before removing its talk partner
        st32(g_scratch+0x40,0);probe=*c;const uint32_t exists=g_call(&probe,kSearch,{a.process,g_scratch+0x40});
        const uint32_t actor=ld32(g_scratch+0x40);
        if(mod_actor::talking(actor))continue; // native events retain their talk target until closure
        if(actor&&!a.deleting){probe=*c;if(g_call(&probe,kDelete,{actor}))a.deleting=true;}
        // Keep creating/queued-for-deletion IDs until native lookup confirms they are gone.
        if(!exists){a.process=0xFFFFFFFFu;st32(g_scratch+128+uint32_t(&a-g_actors.data())*4,a.process);}
    }
    room_loaded(c,room);
}
void state_loaded(){mod_actor::state_loaded();if(g_talk.actor)story_mail::unsay();g_talk={};g_opening=nullptr;for(size_t i=0;i<g_actors.size();++i){auto id=g_scratch?ld32(g_scratch+128+uint32_t(i)*4):0;g_actors[i].process=id?id:0xFFFFFFFFu;g_actors[i].deleting=false;}}
uint32_t actor_address(Cpu* c,const std::string& mod,const std::string& id) {
    if(!g_call||!g_scratch)return 0;
    for(auto& a:g_actors)if(a.mod==mod&&a.spec.id==id&&a.process!=0xFFFFFFFFu){Cpu call=*c;st32(g_scratch+0x40,0);g_call(&call,kSearch,{a.process,g_scratch+0x40});return ld32(g_scratch+0x40);}return 0;
}
bool position(Cpu* c,const std::string& mod,const std::string& id,story_notebook::WorldPoint& p){const uint32_t a=actor_address(c,mod,id);if(!a)return false;p={u32_as_f32(ld32(a+0x314)),u32_as_f32(ld32(a+0x318)),u32_as_f32(ld32(a+0x31C))};return true;}
bool actions(const std::string& mod,const std::vector<story_notebook::StoryAction>& list) {
    using Kind=story_notebook::StoryAction::Kind;
    for(const auto& a:list){bool ok=false;switch(a.kind){
        case Kind::Give:ok=story_items::give(mod,a.id.substr(5),a.value,"actor dialogue")>=0;break;
        case Kind::Take:ok=story_items::take(mod,a.id.substr(5),a.value)>=0;break;
        case Kind::QuestStep:ok=story_notebook::reach(mod,a.id,a.value)>=0;break;
        case Kind::Flag:ok=set_flag(story_store::book(),mod,a.id,a.value!=0);break;
        case Kind::StartScene:ok=g_scene&&g_scene(mod,a.id);break;
    }if(!ok){LOG("[actors] %s: action failed (%s)",mod.c_str(),a.id.c_str());return false;}}return true;
}
bool people_message(Cpu* c){Actor* a=find(c->r[3]);if(!a||a->spec.actor!="resident")return false;
    // A placed resident never falls through to original dialogue (which sets first-talk bits).
    if(!story_mail::can_say()){c->r[3]=0;return true;}
    if(g_talk.npc&&g_talk.npc!=c->r[3]){c->r[3]=0;return true;}
    g_talk={};g_talk.npc=c->r[3];g_talk.actor=a;g_talk.started=true;
    story_mail::append_utf16(a->spec.lines.empty()?"Hello.":a->spec.lines,true,g_talk.text);
    LOG("[actors] %s:%s native dialogue uses mod text (%zu UTF-16 units)",a->mod.c_str(),a->spec.id.c_str(),g_talk.text.size());
    story_mail::say(kCarrier,&g_talk.text);st32(c->r[3]+0x8A8,0);st32(c->r[3]+0x8AC,0);c->r[3]=kCarrier;return true;
}
bool people_next_message(Cpu* c){Actor* a=find(c->r[3]);if(!a||a->spec.actor!="resident")return false;c->r[3]=0x10;return true;}
bool people_execute(Cpu* c,bool before){Actor* a=find(c->r[3]);if(!a||a->spec.actor!="resident")return false;
    if(before){
        // Native wait/talk and visual/collision updates are retained. Script entry points are
        // intercepted separately, so no game-save snapshot/rollback is needed.
        st16(c->r[3]+0xFC,0xFFFF);st8(c->r[3]+0x910,1);
        // Item-show and photography callbacks belong to the retail character's quests.
        st32(c->r[3]+0x100,0);st32(c->r[3]+0x104,0);st32(c->r[3]+0x108,0);
    } else if(g_talk.npc==c->r[3]&&ld16(c->r[3]+0xF8)==0)end_talk();
    return true;
}

void install(const std::function<std::string(uint32_t)>& replaced_by,const std::function<void(uint32_t,void(*)(Cpu*))>& replace) {
    if(g_installed||!declared()||!g_original||!g_call||!g_scratch)return;
    std::vector<std::pair<uint32_t,void(*)(Cpu*)>> hooks={{kChestCheck,chest_check},{kChestOpen,chest_open},{kOnBox,chest_on},{kSignExecute,sign_execute},{kSignDelete,sign_delete},{kPeopleCheckOrder,people_check_order},{kPeopleEventMove,people_event_move},{kPeopleEventOrder,people_event_order}};
    if(!story_treasure::installed())hooks.push_back({kRoom,room_hook});
    if(replaced_by(kPeopleMsg).empty()) {hooks.push_back({kPeopleMsg,people_msg_hook});hooks.push_back({kPeopleNext,people_next_hook});hooks.push_back({kPeopleExecute,people_execute_hook});}
    for(const auto& h:hooks)if(!replaced_by(h.first).empty()){LOG("[actors] hook conflict %08X; world actors disabled",h.first);return;}
    mod_actor::configure(g_original,g_call,g_scratch+kModScratchOffset);
    bool has_mod=false;
    for(size_t i=0;i<g_actors.size();++i){auto& a=g_actors[i];if(a.spec.kind!="mod")continue;
        auto model=g_models.find({a.mod,a.spec.model});if(model==g_models.end()){exception_report::raise("Prepared model unavailable for mod actor "+a.mod+":"+a.spec.id);}
        const std::string mod=a.mod,id=a.spec.id;
        a.mod_token=mod_actor::declare(mod,a.spec,model->second.archive,model->second.index,[mod,id](){
            auto found=std::find_if(g_actors.begin(),g_actors.end(),[&](const Actor& a){return a.mod==mod&&a.spec.id==id;});if(found==g_actors.end())return;
            if(!found->spec.flag.empty())set_flag(story_store::book(),mod,found->spec.flag);
            actions(mod,found->spec.then);story_scenes::trigger("talk",mod,id);
        },model->second.minimum,model->second.maximum);if(!a.mod_token)exception_report::raise("Cannot declare generic mod actor "+mod+":"+id);has_mod=true;
    }
    if(has_mod&&!mod_actor::install(replaced_by,replace))exception_report::raise("Cannot install generic mod actor profile");
    if(has_mod){mod_actor_shadow_proof::configure(g_original,g_call);mod_actor_shadow_proof::install(replaced_by,replace);}
    for(const auto& h:hooks)replace(h.first,h.second);g_installed=true;
}
}
