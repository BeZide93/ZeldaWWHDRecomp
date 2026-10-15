// SDK v3 preview: bounded declarative world content. No game save bits are accepted here.
#pragma once
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <utility>
#include <string>
#include <vector>
#include "mod_json.h"
#include "story_mail_book.h"
namespace story_notebook {
struct WorldPoint { float x=0,y=0,z=0; };
struct StoryAction {
    enum class Kind { Give,Take,QuestStep,Flag,StartScene };
    Kind kind=Kind::Flag; std::string id; unsigned value=1;
};
struct ManifestModel { std::string id,source; };
struct ManifestActor {
    std::string id,actor,stage,kind,model,prompt="check"; unsigned room=0;
    float collision_radius=30,collision_height=100; bool pushable=false;
    WorldPoint position,rotation,scale{1,1,1}; uint32_t params=0; unsigned resident=9; uint8_t item_game=6;
    std::string flag,quest,item,time,lines; unsigned quest_step=0;
    std::vector<StoryAction> then;
};
struct SceneStep {
    enum class Kind { Camera,Say,Move,Face,Wait,Sound,Fade,End };
    Kind kind=Kind::Wait; std::string actor,target_actor,text,sound;
    WorldPoint eye,target,position,eye_to,target_to; unsigned frames=0; bool pan=false,fade_in=false;
    std::vector<StoryAction> then;
};
struct ManifestScene {
    std::string id; std::vector<SceneStep> steps;
    std::string trigger,stage,actor,quest,chart; unsigned room=0,quest_step=0;
    WorldPoint position; float radius=0;
};
inline constexpr unsigned kMaxActorsPerRoom=16,kMaxActorsPerMod=64,kMaxScenesPerMod=16,kMaxSceneSteps=64,kMaxSceneFrames=18000;
namespace world_manifest {
using mods::json::Value;
inline bool integer(const Value& v,double lo,double hi) {return v.type==Value::Number&&std::isfinite(v.number)&&v.number>=lo&&v.number<=hi&&std::floor(v.number)==v.number;}
inline bool id(const Value& v) {return v.type==Value::String&&story_mail::valid_id(v.text);}
inline bool keys(const Value& v,std::initializer_list<const char*> allowed) {
    if(v.type!=Value::Object)return false;
    for(const auto& [key,value]:v.object){bool found=false;for(const auto name:allowed)found|=key==name;if(!found)return false;}return true;
}
inline bool flag_id(const Value& v) {return id(v)&&v.text.rfind("0x",0)!=0;}
inline bool point(const Value& v,WorldPoint& p,float limit=350000) {
    if(v.type!=Value::Object) return false;
    for(auto key:{"x","y","z"}) if(v.get(key).type!=Value::Number||!std::isfinite(v.get(key).number)||std::fabs(v.get(key).number)>limit) return false;
    p={float(v.get("x").number),float(v.get("y").number),float(v.get("z").number)}; return true;
}
inline bool declared(const Value& m,const char* section,const std::string& name) {
    for(const auto& v:m.get(section).array) if(v.get("id").type==Value::String&&v.get("id").text==name) return true;
    return false;
}
inline bool quest_step(const Value& m,const std::string& quest,unsigned step) {
    for(const auto& q:m.get("quests").array) if(q.get("id").string()==quest)
        for(const auto& s:q.get("steps").array) if(integer(s.get("n"),1,254)&&s.get("n").number==step) return true;
    return false;
}
inline bool character(const Value& m,const std::string& name) {return name=="link"||declared(m,"actors",name);}
inline bool resident_character(const Value& m,const std::string& name) {
    for(const auto& a:m.get("actors").array)if(a.get("id").string()==name){const auto type=a.get("actor").string();return type=="resident"||type=="NpcPeople"||type=="PROC_NPC_PEOPLE";}return false;
}
inline bool actions(const Value& m,const Value& v,std::vector<StoryAction>& out,std::string& why) {
    if(v.type==Value::Null) return true;
    if(v.type!=Value::Array||v.array.size()>8) return why="then must be a list of up to 8 actions",false;
    for(const auto& a:v.array) {
        StoryAction s; const auto kind=a.get("action").string(); s.id=a.get("id").string();
        if(kind=="give"||kind=="take") {s.kind=kind=="give"?StoryAction::Kind::Give:StoryAction::Kind::Take;
            if(!keys(a,{"action","id","count"}))return why="unknown give/take action field",false;
            if(a.object.contains("count")){if(!integer(a.get("count"),1,254))return why="action count must be 1..254",false;s.value=unsigned(a.get("count").number);}
            if(s.id.rfind("item:",0)!=0||!declared(m,"items",s.id.substr(5))) return why="action names an undeclared mod item",false;
        } else if(kind=="quest_step") {s.kind=StoryAction::Kind::QuestStep;if(!keys(a,{"action","id","step"}))return why="unknown quest action field",false;
            if(!integer(a.get("step"),1,254)||!quest_step(m,s.id,unsigned(a.get("step").number))) return why="action names an undeclared quest step",false;
            s.value=unsigned(a.get("step").number);
        } else if(kind=="flag") {s.kind=StoryAction::Kind::Flag;if(!keys(a,{"action","id","value"}))return why="unknown flag action field",false;
            if(a.object.contains("value")){if(a.get("value").type!=Value::Bool)return why="flag value must be boolean",false;s.value=a.get("value").boolean?1:0;}if(!flag_id(a.get("id"))) return why="flag must be a mod-owned id",false;
        } else if(kind=="scene") {s.kind=StoryAction::Kind::StartScene;if(!keys(a,{"action","id"}))return why="unknown scene action field",false;if(!declared(m,"scenes",s.id)) return why="action names an undeclared scene",false;
        } else return why="unknown then action",false;
        out.push_back(s);
    } return true;
}
inline bool place(const Value& v,std::string& stage,unsigned& room) {
    if(v.get("stage").type!=Value::String||v.get("stage").text.empty()||v.get("stage").text.size()>8||!integer(v.get("room"),0,63)) return false;
    stage=v.get("stage").text;
    for(char c:stage) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')) return false;
    room=unsigned(v.get("room").number);return true;
}
inline bool lines(const Value& v,std::string& text) {
    if(v.type==Value::String) text=v.text;
    else if(v.type==Value::Array&&v.array.size()<=3) {for(const auto& l:v.array){if(l.type!=Value::String)return false;if(!text.empty())text+='\n';text+=l.text;}}
    else return false;
    unsigned breaks=0;for(char c:text)breaks+=c=='\n';
    std::u16string ignored;return !text.empty()&&text.size()<=240&&breaks<=2&&story_mail::append_utf16(text,true,ignored);
}
}
inline bool parse_models(const mods::json::Value& m,std::vector<ManifestModel>& out,std::string& why) {
    using namespace world_manifest;out.clear();const auto& list=m.get("models");
    if(list.type==Value::Null)return true;
    if(list.type!=Value::Array||list.array.size()>8)return why="models must be a list of up to 8",false;
    std::vector<ManifestModel> parsed;
    for(const auto& v:list.array){
        if(!keys(v,{"id","source"})||!id(v.get("id"))||v.get("source").type!=Value::String)return why="invalid model declaration",false;
        const auto& source=v.get("source").text;
        if(source.empty()||source.size()>240||source.front()=='/'||source.find('\\')!=std::string::npos||source.find(':')!=std::string::npos||source.size()<5||source.substr(source.size()-4)!=".glb")return why="model source must be a package-relative .glb",false;
        size_t start=0;while(start<=source.size()){const auto end=source.find('/',start);const auto part=source.substr(start,end==std::string::npos?end:end-start);if(part.empty()||part=="."||part=="..")return why="invalid model source path",false;if(end==std::string::npos)break;start=end+1;}
        for(unsigned char c:source)if(c<32||c==127)return why="invalid model source path",false;
        for(const auto& old:parsed)if(old.id==v.get("id").text)return why="duplicate model id",false;
        parsed.push_back({v.get("id").text,source});
    }out=std::move(parsed);return true;
}
inline bool parse_actors_impl(const mods::json::Value& m,std::vector<ManifestActor>& out,std::string& why) {
    using namespace world_manifest;out.clear();std::vector<ManifestModel> models;if(!parse_models(m,models,why))return false;const auto& list=m.get("actors");
    if(list.type==Value::Null)return true;
    if(list.type!=Value::Array||list.array.size()>kMaxActorsPerMod)return why="actors must be a list of up to 64",false;
    for(const auto& v:list.array){ManifestActor a;
        if(!keys(v,{"id","actor","kind","model","collision","attention","stage","room","position","rotation","scale","params","parameters","flag","when","lines","then"}))return why="unknown actor field",false;
        if(!id(v.get("id")))return why="invalid actor id",false;a.id=v.get("id").text;a.actor=v.get("actor").string();
        if(v.object.contains("kind")) {
            if(v.get("kind").string()!="mod"||v.object.contains("actor"))return why="mod actor kind cannot name a native actor",false;
            a.kind="mod";a.actor="mod";a.model=v.get("model").string();
            if(!declared(m,"models",a.model))return why="mod actor names an undeclared model",false;
            if(v.object.contains("params")||v.object.contains("parameters"))return why="mod actors cannot use native parameters",false;
            const auto& collision=v.get("collision");
            if(!keys(collision,{"radius","height","mode"}))return why="mod actor needs cylinder collision",false;
            const auto& radius=collision.get("radius");const auto& height=collision.get("height");
            if(radius.type!=Value::Number||height.type!=Value::Number||!std::isfinite(radius.number)||!std::isfinite(height.number)||radius.number<=0||radius.number>1000||height.number<=0||height.number>2000)return why="collision radius must be 0..1000 and height 0..2000 (exclusive zero)",false;
            const auto mode=collision.get("mode").string();if(mode!="solid"&&mode!="pushable")return why="collision mode must be solid or pushable",false;
            a.collision_radius=float(radius.number);a.collision_height=float(height.number);if(a.collision_radius<=0||a.collision_height<=0)return why="collision dimensions are too small",false;a.pushable=mode=="pushable";
            const auto& attention=v.get("attention");if(attention.type!=Value::Null){if(!keys(attention,{"prompt"}))return why="invalid mod actor attention",false;a.prompt=attention.get("prompt").string();if(a.prompt!="talk"&&a.prompt!="check")return why="attention prompt must be talk or check",false;}
            if(v.object.contains("flag")){if(!flag_id(v.get("flag")))return why="mod actor flag must be a mod-owned id",false;a.flag=v.get("flag").text;}
        }else if(v.object.contains("model")||v.object.contains("collision")||v.object.contains("attention"))return why="model, collision and attention require kind mod",false;
        if(a.actor=="kanban"||a.actor=="PROC_KANBAN")a.actor="sign";
        if(a.actor=="Tbox"||a.actor=="takara"||a.actor=="PROC_TBOX")a.actor="chest";
        if(a.actor=="NpcPeople"||a.actor=="PROC_NPC_PEOPLE")a.actor="resident";
        if(a.actor!="sign"&&a.actor!="resident"&&a.actor!="chest"&&!(a.actor=="mod"&&a.kind=="mod"))return why="unknown or unsupported flag-setting actor: "+a.actor,false;
        if(!place(v,a.stage,a.room)||!point(v.get("position"),a.position))return why="actor "+a.id+": invalid stage, room or position",false;
        if(v.get("rotation").type!=Value::Null&&!point(v.get("rotation"),a.rotation,360))return why="invalid actor rotation",false;
        if(v.get("scale").type!=Value::Null&&(!point(v.get("scale"),a.scale,10)||a.scale.x<=0||a.scale.y<=0||a.scale.z<=0))return why="invalid actor scale",false;
        if(v.get("params").type!=Value::Null) {if(a.actor!="chest"||!integer(v.get("params"),0,0))return why="unsupported actor params: only ordinary chest params 0 are safe",false;}
        const auto& named=v.get("parameters");
        if(named.type!=Value::Null&&named.type!=Value::Object)return why="actor parameters must be an object",false;
        for(const auto& [key,value]:named.object) {
            if(key=="resident"&&a.actor=="resident") {if(!integer(value,9,9))return why="only resident 9 (Um1) is supported in this preview",false;a.resident=9;}
            else if(key=="item"&&a.actor=="chest") {static const char* names[]={"rupee_green","rupee_blue","rupee_yellow","rupee_red","rupee_purple","rupee_orange"};bool found=false;for(unsigned i=0;i<6;++i)if(value.type==Value::String&&value.text==names[i]){a.item_game=uint8_t(i+1);found=true;}if(!found)return why="chest item must be a rupee",false;}
            else return why="unsupported named actor parameter: "+key,false;
        }
        if(a.actor=="sign"||a.actor=="chest"){if(!flag_id(v.get("flag")))return why="chests and signs require a mod-owned flag id",false;a.flag=v.get("flag").text;}
        const auto& when=v.get("when");if(when.type!=Value::Null&&!keys(when,{"quest","step","item","time"}))return why="actor when must be an object",false;
        if(when.get("quest").type!=Value::Null){a.quest=when.get("quest").string();if(!integer(when.get("step"),1,254)||!quest_step(m,a.quest,unsigned(when.get("step").number)))return why="actor condition names an undeclared quest step",false;a.quest_step=unsigned(when.get("step").number);}
        if(when.get("item").type!=Value::Null){a.item=when.get("item").string();if(a.item.rfind("item:",0)!=0||!declared(m,"items",a.item.substr(5)))return why="actor condition names an undeclared mod item",false;}
        if(when.get("time").type!=Value::Null){a.time=when.get("time").string();if(a.time!="day"&&a.time!="night")return why="actor time must be day or night",false;}
        if(v.get("lines").type!=Value::Null&&!lines(v.get("lines"),a.lines))return why="invalid actor dialogue",false;
        if(!actions(m,v.get("then"),a.then,why))return false;
        if(!a.then.empty()&&a.lines.empty())return why="actor then requires dialogue lines",false;
        unsigned count=1;for(const auto& old:out){if(old.id==a.id)return why="duplicate actor id",false;if(old.stage==a.stage&&old.room==a.room)++count;}
        if(count>kMaxActorsPerRoom)return why="actors exceed the 16 per room limit",false;
        out.push_back(std::move(a));
    }return true;
}
inline bool parse_scenes_impl(const mods::json::Value& m,std::vector<ManifestScene>& out,std::string& why) {
    using namespace world_manifest;out.clear();const auto& list=m.get("scenes");
    if(list.type==Value::Null)return true;
    if(list.type!=Value::Array||list.array.size()>kMaxScenesPerMod)return why="scenes must be a list of up to 16",false;
    for(const auto& v:list.array){ManifestScene s;if(!keys(v,{"id","trigger","steps"}))return why="unknown scene field",false;if(!id(v.get("id")))return why="invalid scene id",false;s.id=v.get("id").text;
        for(const auto& old:out)if(old.id==s.id)return why="duplicate scene id",false;
        const auto& trigger=v.get("trigger");if(trigger.type!=Value::Null){s.trigger=trigger.get("type").string();
            bool trigger_keys=false;
            if(s.trigger=="room")trigger_keys=keys(trigger,{"type","stage","room"});
            else if(s.trigger=="area")trigger_keys=keys(trigger,{"type","stage","room","position","radius"});
            else if(s.trigger=="talk")trigger_keys=keys(trigger,{"type","actor"});
            else if(s.trigger=="quest_step")trigger_keys=keys(trigger,{"type","quest","step"});
            else if(s.trigger=="salvage")trigger_keys=keys(trigger,{"type","chart"});
            if(!trigger_keys)return why="unknown scene trigger or field",false;
            if(s.trigger=="room"||s.trigger=="area"){if(!place(trigger,s.stage,s.room))return why="invalid scene room trigger",false;if(s.trigger=="area"){if(!point(trigger.get("position"),s.position)||trigger.get("radius").type!=Value::Number||!std::isfinite(trigger.get("radius").number)||trigger.get("radius").number<=0||trigger.get("radius").number>10000)return why="invalid scene area trigger",false;s.radius=float(trigger.get("radius").number);}}
            else if(s.trigger=="talk"){s.actor=trigger.get("actor").string();if(!declared(m,"actors",s.actor))return why="unknown trigger actor",false;}
            else if(s.trigger=="quest_step"){s.quest=trigger.get("quest").string();if(!integer(trigger.get("step"),1,254)||!quest_step(m,s.quest,unsigned(trigger.get("step").number)))return why="unknown trigger quest step",false;s.quest_step=unsigned(trigger.get("step").number);}
            else if(s.trigger=="salvage"){s.chart=trigger.get("chart").string();if(!declared(m,"charts",s.chart))return why="unknown trigger chart",false;}
            else return why="unknown scene trigger",false;
        }
        const auto& steps=v.get("steps");if(steps.type!=Value::Array||steps.array.empty()||steps.array.size()>kMaxSceneSteps)return why="scene needs 1..64 steps",false;
        unsigned duration=0;for(const auto& st:steps.array){SceneStep p;const auto type=st.get("type").string();bool timed=false;
            bool step_keys=false;
            if(type=="camera")step_keys=keys(st,{"type","eye","target","target_actor","pan","eye_to","target_to","frames","seconds"});
            else if(type=="say")step_keys=keys(st,{"type","actor","text","frames","seconds"});
            else if(type=="move")step_keys=keys(st,{"type","actor","position","frames","seconds"});
            else if(type=="face")step_keys=keys(st,{"type","actor","position","target_actor"});
            else if(type=="wait")step_keys=keys(st,{"type","frames","seconds"});
            else if(type=="sound")step_keys=keys(st,{"type","name"});
            else if(type=="fade")step_keys=keys(st,{"type","direction","frames","seconds"});
            else if(type=="end")step_keys=keys(st,{"type","then"});
            if(!step_keys)return why="unknown scene step or field",false;
            if(type=="camera"){p.kind=SceneStep::Kind::Camera;timed=true;if(!point(st.get("eye"),p.eye))return why="camera needs an eye",false;if(st.get("target_actor").type!=Value::Null){p.target_actor=st.get("target_actor").string();if(!character(m,p.target_actor))return why="unknown camera character",false;}else if(!point(st.get("target"),p.target))return why="camera needs a target",false;if(st.get("pan").type!=Value::Null){if(st.get("pan").type!=Value::Bool)return why="pan must be boolean",false;p.pan=st.get("pan").boolean;if(p.pan&&(!point(st.get("eye_to"),p.eye_to)||!point(st.get("target_to"),p.target_to)))return why="pan needs eye_to and target_to",false;}}
            else if(type=="say"){p.kind=SceneStep::Kind::Say;timed=true;p.actor=st.get("actor").string();if(!character(m,p.actor)||!lines(st.get("text"),p.text))return why="say needs a known character and text",false;}
            else if(type=="move"||type=="face"){p.kind=type=="move"?SceneStep::Kind::Move:SceneStep::Kind::Face;timed=type=="move";p.actor=st.get("actor").string();if(!character(m,p.actor))return why="unknown scene character",false;if(!resident_character(m,p.actor))return why="move/face requires a placed resident",false;if(type=="face"&&st.get("target_actor").type!=Value::Null){p.target_actor=st.get("target_actor").string();if(!character(m,p.target_actor))return why="unknown face target",false;}else if(!point(st.get("position"),p.position))return why="move/face needs a position",false;}
            else if(type=="wait"){p.kind=SceneStep::Kind::Wait;timed=true;}
            else if(type=="sound"){p.kind=SceneStep::Kind::Sound;p.sound=st.get("name").string();if(p.sound!="SE_MENU_CANCEL"&&p.sound!="SE_MENU_DECIDE"&&p.sound!="SE_MENU_CURSOR"&&p.sound!="SE_MENU_MINI_WINDOW_OPEN"&&p.sound!="SE_MENU_MINI_WINDOW_CLOSE")return why="unknown scene sound",false;}
            else if(type=="fade"){p.kind=SceneStep::Kind::Fade;timed=true;const auto dir=st.get("direction").string();if(dir!="in"&&dir!="out")return why="fade direction must be in or out",false;p.fade_in=dir=="in";}
            else if(type=="end"){p.kind=SceneStep::Kind::End;if(!actions(m,st.get("then"),p.then,why))return false;if(&st!=&steps.array.back())return why="end must be the final step",false;}
            else return why="unknown scene step",false;
            if(timed){
                const auto& frames=st.get("frames");const auto& seconds=st.get("seconds");
                if(frames.type!=Value::Null&&seconds.type!=Value::Null)return why="use frames or seconds, not both",false;
                if(seconds.type!=Value::Null){if(seconds.type!=Value::Number||!std::isfinite(seconds.number)||seconds.number<=0||seconds.number>300)return why="scene seconds must be in (0,300]",false;p.frames=unsigned(std::ceil(seconds.number*60));}
                else {if(!integer(frames,1,kMaxSceneFrames))return why="scene step requires frames in 1..18000",false;p.frames=unsigned(frames.number);}
            }
            if(p.kind!=SceneStep::Kind::End&&p.frames==0)p.frames=1;
            duration+=p.frames;if(duration>kMaxSceneFrames)return why="scene exceeds five minute limit",false;s.steps.push_back(std::move(p));
        }
        if(s.steps.back().kind!=SceneStep::Kind::End)return why="scene must finish with end",false;out.push_back(std::move(s));
    }return true;
}
inline bool parse_actors(const mods::json::Value& m,std::vector<ManifestActor>& out,std::string& why) {
    out.clear();std::vector<ManifestActor> parsed;if(!parse_actors_impl(m,parsed,why))return false;out=std::move(parsed);return true;
}
inline bool parse_scenes(const mods::json::Value& m,std::vector<ManifestScene>& out,std::string& why) {
    out.clear();std::vector<ManifestScene> parsed;if(!parse_scenes_impl(m,parsed,why))return false;out=std::move(parsed);return true;
}
namespace world_manifest {
// Validate the selected package graph before any actor or scene registry is mutated.
inline bool validate_world_packages(const std::vector<std::pair<std::string,mods::json::Value>>& packages,std::string& why) {
    unsigned total=0;std::map<std::pair<std::string,unsigned>,unsigned> rooms;
    std::map<uint16_t,std::pair<std::string,std::string>> flags;
    for(const auto& [mod,manifest]:packages){
        std::vector<ManifestActor> actors;std::vector<ManifestScene> scenes;
        if(!parse_actors(manifest,actors,why)||!parse_scenes(manifest,scenes,why)){why=mod+": "+why;return false;}
        total+=unsigned(actors.size());if(total>128)return why="enabled packages exceed 128 actors overall",false;
        std::vector<std::string> names;
        for(const auto& actor:actors){
            if(++rooms[{actor.stage,actor.room}]>kMaxActorsPerRoom)return why="enabled packages exceed 16 actors in "+actor.stage+" room "+std::to_string(actor.room),false;
            if(!actor.flag.empty())names.push_back(actor.flag);
            for(const auto& action:actor.then)if(action.kind==StoryAction::Kind::Flag)names.push_back(action.id);
        }
        for(const auto& scene:scenes)for(const auto& step:scene.steps)for(const auto& action:step.then)
            if(action.kind==StoryAction::Kind::Flag)names.push_back(action.id);
        for(const auto& name:names){
            const auto identity=std::make_pair(mod,name);const uint16_t key=story_mail::letter_key(mod,"actor-flag:"+name);
            auto [found,inserted]=flags.emplace(key,identity);
            if(!inserted&&found->second!=identity)return why="mod-owned flag hash collision: "+mod+":"+name+" conflicts with "+found->second.first+":"+found->second.second,false;
        }
    }return true;
}
} // namespace world_manifest
} // namespace story_notebook
