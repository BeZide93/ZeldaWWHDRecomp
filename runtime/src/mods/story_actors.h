#pragma once
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>
#include "story_world_manifest.h"
#include "story_actors_core.h"
struct Cpu;
namespace story_actors {
inline constexpr uint32_t kModScratchOffset=1280;
inline constexpr uint32_t kScratchBytes=5376;
bool load(const std::string& mod,const std::vector<story_notebook::ManifestActor>& actors,std::string& why);
bool declared();
bool mod_declared();
// Prepared player-local model archive; register before install.
bool register_model(const std::string& mod,const std::string& model,const std::string& archive,int resource_index,std::string& why,
                    story_notebook::WorldPoint minimum={-50,0,-50},story_notebook::WorldPoint maximum={50,200,50});
bool register_flags(const std::string& mod,const std::vector<std::string>& flags,std::string& why);
void service(Cpu*);
void set_game_calls(void (*original)(Cpu*,uint32_t),uint32_t (*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>));
void set_scratch_memory(uint32_t guest);
void set_scene_start(std::function<bool(const std::string&,const std::string&)> start);
void install(const std::function<std::string(uint32_t)>& replaced_by,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
void room_loaded(Cpu*,int room);
// Call once after native room actor setup (including the shared treasure hook).
void native_room_loaded(Cpu*,int room);
void state_loaded();
uint32_t actor_address(Cpu*,const std::string& mod,const std::string& id);
bool position(Cpu*,const std::string& mod,const std::string& id,story_notebook::WorldPoint& out);
bool actions(const std::string& mod,const std::vector<story_notebook::StoryAction>& actions);
// Notebook hooks delegate only actors created by this service. Returns true when handled.
bool people_message(Cpu*);
bool people_next_message(Cpu*);
bool people_execute(Cpu*,bool before);
}
