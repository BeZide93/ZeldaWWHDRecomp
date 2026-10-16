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
    std::cout<<"story world manifest tests passed\n";
}
