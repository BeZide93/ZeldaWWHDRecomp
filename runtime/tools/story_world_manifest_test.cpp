#include "mods/story_world_manifest.h"
#include <cassert>
#include <iostream>
using namespace story_notebook;
using mods::json::Value;
static Value example() {
    return mods::json::parse(R"({"actors":[{"id":"guide","actor":"resident","stage":"town","room":0,"position":{"x":0,"y":0,"z":0},"lines":["Hello"],"then":[{"action":"scene","id":"intro"}]},{"id":"notice","actor":"sign","flag":"read","stage":"town","room":0,"position":{"x":1,"y":2,"z":3}}],"scenes":[{"id":"intro","trigger":{"type":"talk","actor":"guide"},"steps":[{"type":"camera","eye":{"x":0,"y":100,"z":100},"target_actor":"guide","frames":60},{"type":"say","actor":"guide","text":"Welcome","frames":600},{"type":"fade","direction":"out","frames":30},{"type":"end","then":[{"action":"flag","id":"seen"}]}]}]})");
}
int main() {
    std::string why;std::vector<ManifestActor> actors;std::vector<ManifestScene> scenes;
    auto v=example();assert(parse_actors(v,actors,why)&&actors.size()==2);assert(parse_scenes(v,scenes,why)&&scenes[0].steps.size()==4);
    assert(parse_actors(Value{},actors,why)&&actors.empty());assert(parse_scenes(Value{},scenes,why)&&scenes.empty());
    v=example();v["actors"].array[0]["actor"]="bomb";assert(!parse_actors(v,actors,why));
    v=example();v["actors"].array[1].object.erase("flag");assert(!parse_actors(v,actors,why));
    v=example();v["actors"].array[0]["position"]["x"]=350001;assert(!parse_actors(v,actors,why));
    v=example();v["actors"].array[0]["scale"]["x"]=-1;assert(!parse_actors(v,actors,why));
    v=example();v["actors"].array[0]["params"]=0;assert(!parse_actors(v,actors,why));
    v=example();auto actor=v["actors"].array[0];for(int i=0;i<15;++i){actor["id"]="extra"+std::to_string(i);v["actors"].array.push_back(actor);}assert(!parse_actors(v,actors,why));
    v=example();v["actors"].array[0]["then"].array[0]["id"]="missing";assert(!parse_actors(v,actors,why));
    v=example();v["scenes"].array[0]["steps"].array[1]["actor"]="missing";assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["steps"].array[0].object.erase("frames");assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["steps"].array[0]["frames"]=18000;assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["steps"].array.pop_back();assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["steps"].array[0]["pan"]=true;assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["trigger"]["actor"]="missing";assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["steps"].array[3]["then"].array[0]["id"]="0x1234";assert(!parse_scenes(v,scenes,why));
    v=example();v["actors"].array[1]["actor"]="Tbox";v["actors"].array[1]["params"]=0;v["actors"].array[1]["parameters"]["item"]="rupee_blue";assert(parse_actors(v,actors,why)&&actors[1].item_game==2);
    v["actors"].array[1]["parameters"]["item"]="heart_piece";assert(!parse_actors(v,actors,why)&&actors.empty());
    v=example();v["actors"].array[0]["parameters"]["resident"]=10;assert(!parse_actors(v,actors,why)&&actors.empty());
    v=example();v["actors"].array[0]["then"]=Value("bad");assert(!parse_actors(v,actors,why));
    v=example();v["scenes"].array.push_back(v["scenes"].array[0]);assert(!parse_scenes(v,scenes,why)&&scenes.empty());
    v=example();v["scenes"].array[0]["steps"].array[3]["then"]=true;assert(!parse_scenes(v,scenes,why)&&scenes.empty());
    v=example();v["scenes"].array[0]["steps"].array[0]["frames"]=1.5;assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["steps"].array[0]["eye"]["x"]=350001;assert(!parse_scenes(v,scenes,why));
    v=example();auto& shot=v["scenes"].array[0]["steps"].array[0];shot.object.erase("frames");shot["seconds"]=0.5;assert(parse_scenes(v,scenes,why)&&scenes[0].steps[0].frames==30);shot["frames"]=30;assert(!parse_scenes(v,scenes,why));
    v=example();v["actors"].array[0]["rotation"]["x"]=0;v["actors"].array[0]["rotation"]["y"]=361;v["actors"].array[0]["rotation"]["z"]=0;assert(!parse_actors(v,actors,why));
    v=example();auto& move=v["scenes"].array[0]["steps"].array[0];move=mods::json::parse(R"({"type":"move","actor":"guide","position":{"x":0,"y":0,"z":0},"frames":60})");assert(parse_scenes(v,scenes,why));move["actor"]="link";assert(!parse_scenes(v,scenes,why));move["actor"]="notice";assert(!parse_scenes(v,scenes,why));
    v=example();auto& flag=v["scenes"].array[0]["steps"].array[3]["then"].array[0];flag["value"]=false;assert(parse_scenes(v,scenes,why)&&scenes[0].steps.back().then[0].value==0);flag["value"]=Value{};assert(!parse_scenes(v,scenes,why));flag["value"]=0;assert(!parse_scenes(v,scenes,why));flag["value"]=true;flag["switch"]=1;assert(!parse_scenes(v,scenes,why));
    v=example();v["items"]=mods::json::parse(R"([{"id":"token"}])");auto& give=v["actors"].array[0]["then"].array[0];give=mods::json::parse(R"({"action":"give","id":"item:token","count":254})");assert(parse_actors(v,actors,why)&&actors[0].then[0].value==254);
    for(const auto& bad:{Value{},Value(0),Value(255),Value(1.5),Value(true),Value("2")}){give["count"]=bad;assert(!parse_actors(v,actors,why));}
    give["count"]=2;give["action"]="take";assert(parse_actors(v,actors,why)&&actors[0].then[0].value==2);
    v=example();v["actors"].array[0]["switch"]=1;assert(!parse_actors(v,actors,why));
    v=example();v["actors"].array[0]["parameters"]["switch"]=1;assert(!parse_actors(v,actors,why));
    v=example();v["scenes"].array[0]["kind"]="mod";assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["trigger"]["event_bit"]=1;assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["steps"].array[0]["duration"]=100;assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["steps"].array[3]["event_bit"]=1;assert(!parse_scenes(v,scenes,why));
    auto package=[](unsigned count,unsigned stage_index){Value m;m["actors"].type=Value::Array;for(unsigned i=0;i<count;++i){auto a=mods::json::parse(R"({"id":"actor","actor":"resident","stage":"stage","room":0,"position":{"x":0,"y":0,"z":0}})");a["id"]="actor"+std::to_string(i);a["stage"]="stage"+std::to_string(stage_index);a["room"]=int(i/16);m["actors"].array.push_back(a);}return m;};
    auto nine=package(9,0),eight=package(8,0);assert(!world_manifest::validate_world_packages({{"one",nine},{"two",eight}},why)&&why.find("16 actors")!=std::string::npos);
    eight["actors"].array.pop_back();assert(world_manifest::validate_world_packages({{"one",nine},{"two",eight}},why));
    assert(!world_manifest::validate_world_packages({{"one",package(48,0)},{"two",package(48,1)},{"three",package(48,2)}},why)&&why.find("128 actors")!=std::string::npos);
    std::map<uint16_t,std::string> flag_names;std::string one_flag,two_flag;
    for(unsigned i=0;i<1000;++i){const std::string name="flag"+std::to_string(i);flag_names.emplace(story_mail::letter_key("one","actor-flag:"+name),name);}
    for(unsigned i=0;i<100000&&two_flag.empty();++i){const std::string name="flag"+std::to_string(i);auto found=flag_names.find(story_mail::letter_key("two","actor-flag:"+name));if(found!=flag_names.end()){one_flag=found->second;two_flag=name;}}
    assert(!two_flag.empty());auto one=package(1,0);one["actors"].array[0]["actor"]="sign";one["actors"].array[0]["flag"]=one_flag;
    auto two=mods::json::parse(R"({"scenes":[{"id":"scene","steps":[{"type":"end","then":[{"action":"flag","id":"flag"}]}]}]})");two["scenes"].array[0]["steps"].array[0]["then"].array[0]["id"]=two_flag;
    assert(!world_manifest::validate_world_packages({{"one",one},{"two",two}},why)&&why.find("hash collision")!=std::string::npos);
    two=package(1,1);two["actors"].array[0]["lines"]="Hello";two["actors"].array[0]["then"]=mods::json::parse(R"([{"action":"flag","id":"flag"}])");two["actors"].array[0]["then"].array[0]["id"]=two_flag;
    assert(!world_manifest::validate_world_packages({{"one",one},{"two",two}},why));
    assert(world_manifest::validate_world_packages({{"one",one}},why));
    v=example();auto& face=v["scenes"].array[0]["steps"].array[0];face=mods::json::parse(R"({"type":"face","actor":"guide","target_actor":"link"})");assert(parse_scenes(v,scenes,why));
    face["actor"]="link";assert(!parse_scenes(v,scenes,why));face["actor"]="notice";assert(!parse_scenes(v,scenes,why));
    v["actors"].array[1]["actor"]="chest";face["actor"]="notice";assert(!parse_scenes(v,scenes,why));
    face["actor"]="guide";face["target_actor"]="notice";assert(parse_scenes(v,scenes,why));
    auto mod=mods::json::parse(R"({"models":[{"id":"post","source":"models/post.glb"}],"actors":[{"id":"post","kind":"mod","model":"post","stage":"town","room":0,"position":{"x":0,"y":0,"z":0},"collision":{"radius":30,"height":120,"mode":"solid"},"attention":{"prompt":"check"},"lines":"Hello"}]})");
    assert(parse_actors(mod,actors,why)&&actors[0].kind=="mod"&&actors[0].model=="post"&&actors[0].collision_height==120&&!actors[0].pushable);
    auto valid_mod=mod;mod["actors"].array[0]["collision"]["mode"]="pushable";assert(parse_actors(mod,actors,why)&&actors[0].pushable);
    std::vector<ManifestModel> models;assert(parse_models(valid_mod,models,why)&&models[0].kind=="static");
    mod=valid_mod;mod["models"].array[0]["kind"]="skinned";assert(parse_models(mod,models,why)&&models[0].kind=="skinned");
    mod["models"].array[0]["kind"]="static";assert(parse_models(mod,models,why)&&models[0].kind=="static");
    for(const auto& kind:{"animated","", "native"}){mod["models"].array[0]["kind"]=kind;assert(!parse_models(mod,models,why));}
    mod["models"].array[0]["kind"]=mods::json::Value{};assert(!parse_models(mod,models,why));
    for(const auto& source:{"../post.glb","/post.glb","models//post.glb","models/./post.glb","models/post.bfres","C:post.glb"}){mod=valid_mod;mod["models"].array[0]["source"]=source;assert(!parse_actors(mod,actors,why));}
    mod=valid_mod;mod["actors"].array[0]["animation"]="wave";assert(!parse_actors(mod,actors,why));mod["models"].array[0]["kind"]="skinned";assert(parse_actors(mod,actors,why)&&actors[0].animation=="wave");mod["actors"].array[0]["animation"]="";assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0]["actor"]="sign";assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0]["model"]="missing";assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0]["params"]=0;assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0]["collision"]["radius"]=0;assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0]["collision"]["height"]=2001;assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0]["attention"]["prompt"]="attack";assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0].object.erase("kind");assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["models"].array.push_back(mod["models"].array[0]);assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0]["collision"]["radius"]=1e-300;assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0]["collision"]["height"]=1e-300;assert(!parse_actors(mod,actors,why));
    mod=example();mod["actors"].array[0]["actor"]="mod";assert(!parse_actors(mod,actors,why));
    auto moving=valid_mod;moving["models"].array[0]["kind"]="skinned";auto& ma=moving["actors"].array[0];
    ma["look_at"]=mods::json::parse(R"({"head":"Head","yaw_degrees":70,"pitch_degrees":30,"rate_degrees":180,"radius":500,"upper_body":{"bone":"Root","yaw_degrees":20,"fraction":0.3}})");
    ma["path"]=mods::json::parse(R"({"points":[{"x":0,"y":0,"z":0},{"x":100,"y":0,"z":0},{"x":100,"y":0,"z":100}],"speed":100,"mode":"loop"})");
    assert(parse_actors(moving,actors,why)&&actors[0].look_at.enabled&&actors[0].look_at.head=="Head"&&actors[0].look_at.upper_bone=="Root"&&actors[0].path.loop&&actors[0].path.points.size()==3);
    for(const auto& field:{"look_at","path"}){mod=moving;mod["models"].array[0]["kind"]="static";mod["actors"].array[0].object.erase(field==std::string("look_at")?"path":"look_at");assert(!parse_actors(mod,actors,why));mod=moving;mod["actors"].array[0][field]=Value{};assert(!parse_actors(mod,actors,why));}
    for(const auto& pair:{std::pair{"yaw_degrees",181.},std::pair{"pitch_degrees",91.},std::pair{"rate_degrees",0.},std::pair{"rate_degrees",1081.},std::pair{"rate_degrees",1e-300},std::pair{"radius",0.},std::pair{"radius",1000001.}}){mod=moving;mod["actors"].array[0]["look_at"][pair.first]=Value(pair.second);assert(!parse_actors(mod,actors,why));}
    for(const auto& bone:{"", "1Head", "Head/Bad", "Head Bad"}){mod=moving;mod["actors"].array[0]["look_at"]["head"]=bone;assert(!parse_actors(mod,actors,why));}
    mod=moving;mod["actors"].array[0]["look_at"]["upper_body"]["bone"]="Head";assert(!parse_actors(mod,actors,why));mod=moving;mod["actors"].array[0]["look_at"]["upper_body"]["fraction"]=1.01;assert(!parse_actors(mod,actors,why));mod=moving;mod["actors"].array[0]["look_at"]["upper_body"]=Value{};assert(!parse_actors(mod,actors,why));
    mod=moving;mod["actors"].array[0]["look_at"]["yaw_degrees"]=0;mod["actors"].array[0]["look_at"]["pitch_degrees"]=0;mod["actors"].array[0]["look_at"].object.erase("upper_body");assert(parse_actors(mod,actors,why)&&actors[0].look_at.upper_bone.empty());
    for(const auto& speed:{0.,-1.,1501.,1e-300}){mod=moving;mod["actors"].array[0]["path"]["speed"]=Value(speed);assert(!parse_actors(mod,actors,why));}
    mod=moving;mod["actors"].array[0]["path"]["mode"]="teleport";assert(!parse_actors(mod,actors,why));mod=moving;mod["actors"].array[0]["path"]["points"].array.resize(1);assert(!parse_actors(mod,actors,why));mod=moving;mod["actors"].array[0]["path"]["points"].array.resize(17);assert(!parse_actors(mod,actors,why));
    mod=moving;mod["actors"].array[0]["path"]["points"].array[1]=mod["actors"].array[0]["path"]["points"].array[0];assert(!parse_actors(mod,actors,why));mod=moving;mod["actors"].array[0]["path"]["points"].array.back()=mod["actors"].array[0]["path"]["points"].array.front();assert(!parse_actors(mod,actors,why));mod["actors"].array[0]["path"]["mode"]="once";assert(parse_actors(mod,actors,why));
    mod=moving;mod["actors"].array[0]["path"]["points"].array[0]["speed"]=1;assert(!parse_actors(mod,actors,why));mod=moving;mod["actors"].array[0]["look_at"]["extra"]=true;assert(!parse_actors(mod,actors,why));
    mod=example();mod["actors"].array[0]["look_at"]=ma.get("look_at");assert(!parse_actors(mod,actors,why));mod=example();mod["actors"].array[0]["path"]=ma.get("path");assert(!parse_actors(mod,actors,why));
    auto extended=moving;extended["scenes"]=mods::json::parse(R"([{"id":"extended","steps":[
      {"id":"opening","type":"camera_path","easing":"smoothstep","points":[{"eye":{"x":0,"y":100,"z":100},"target":{"x":0,"y":0,"z":0},"frames":30},{"eye":{"x":100,"y":100,"z":100},"target":{"x":0,"y":0,"z":0},"frames":60},{"eye":{"x":200,"y":100,"z":100},"target":{"x":0,"y":0,"z":0}}]},
      {"id":"follow","type":"camera_follow","actor":"post","eye_offset":{"x":0,"y":100,"z":100},"target_offset":{"x":0,"y":0,"z":0},"half_life":0.2,"frames":60},
      {"id":"animate","type":"animation","actor":"post","clip":"wave","blend_seconds":0.3,"loop":false},
      {"id":"walk","type":"walk","actor":"post","points":[{"x":0,"y":0,"z":0},{"x":100,"y":0,"z":0}],"speed":100,"frames":300},
      {"id":"question","type":"choice","text":"Which way?","options":[{"text":"Left","next":"left"},{"text":"Right","next":"right"}],"default":1,"frames":600},
      {"id":"left","type":"music","cue":"calm","action":"start","fade_frames":60,"next":"left-end"},{"id":"left-end","type":"end"},
      {"id":"right","type":"music","cue":"calm","action":"stop","next":"right-end"},{"id":"right-end","type":"end"}
    ]}])");
    assert(parse_scenes(extended,scenes,why)&&scenes[0].steps[0].frames==90&&scenes[0].steps[0].camera_keys.size()==3&&scenes[0].steps[4].successors==std::vector<size_t>({5,7})&&scenes[0].steps[4].choices[1].next_step==7&&scenes[0].steps[4].default_choice==1&&scenes[0].steps[5].successors==std::vector<size_t>({6}));
    auto bad_scene=[&](Value data){assert(!parse_scenes(data,scenes,why)&&scenes.empty()&&!why.empty());};
    v=extended;v["scenes"].array[0]["steps"].array[0]["points"].array.back()["frames"]=1;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[0]["points"].array[0]["frames"]=18000;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[0]["points"].array.resize(1);bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[0]["easing"]="bounce";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[0]["points"].array[1]["eye"]=mods::json::parse("{\"x\":0,\"y\":-100,\"z\":-100}");bad_scene(v); // crossing eye/target despite valid endpoints
    v=extended;v["scenes"].array[0]["steps"].array[1]["half_life"]=11;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[1]["target_offset"]=v["scenes"].array[0]["steps"].array[1]["eye_offset"];bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[2]["loop"]=1;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[2]["blend_seconds"]=11;bad_scene(v);
    v=extended;v["models"].array[0]["kind"]="static";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[3]["speed"]=1e-300;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[3]["quest_place"]="unknown";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[3].object.erase("points");v["scenes"].array[0]["steps"].array[3]["quest_place"]="destination";bad_scene(v);v["quests"]=mods::json::parse("[{\"id\":\"destination\"}]");assert(parse_scenes(v,scenes,why)&&scenes[0].steps[3].quest_place=="destination");
    v=extended;v["scenes"].array[0]["steps"].array[4]["options"].array[0]["text"]=std::string(33,'x');bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[4]["options"].array[0]["text"]="two\nlines";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[4]["default"]=2;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[4]["options"].array[1]["next"]="left";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[4]["options"].array[0]["next"]="opening";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[4]["next"]="left";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[0]["next"]="question";bad_scene(v); // skipped unreachable steps
    v=extended;v["scenes"].array[0]["steps"].array[1]["id"]="opening";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[5]["next"]="missing";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[6]["next"]="right";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[5]["fade_frames"]=601;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[5]["action"]="native-bgm-id";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[1]["eye_offset"]["extra"]=0;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[0]["next"]=Value{};bad_scene(v);
    v=moving;v["scenes"]=mods::json::parse(R"([{"id":"branch-duration","steps":[{"id":"choose","type":"choice","text":"Choose","frames":1,"options":[{"text":"A","next":"a"},{"text":"B","next":"b"}]},{"id":"a","type":"wait","frames":10000,"next":"a-end"},{"id":"a-end","type":"end"},{"id":"b","type":"wait","frames":10000,"next":"b-end"},{"id":"b-end","type":"end"}]}])");assert(parse_scenes(v,scenes,why));v["scenes"].array[0]["steps"].array[1]["frames"]=18000;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[1]["actor"]="link";assert(parse_scenes(v,scenes,why));
    v=moving;v["scenes"]=mods::json::parse(R"([{"id":"mod-move","steps":[{"type":"move","actor":"post","position":{"x":100,"y":0,"z":0},"frames":60},{"type":"face","actor":"post","target_actor":"link"},{"type":"end"}]}])");assert(parse_scenes(v,scenes,why));
    std::cout<<"story world manifest tests passed\n";
}
