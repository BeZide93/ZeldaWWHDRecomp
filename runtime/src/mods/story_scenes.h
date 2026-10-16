// SDK v3 preview scene runtime. Main game thread only; installed only for declared scenes.
#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>
#include "story_world_manifest.h"
#include "story_scenes_core.h"
struct Cpu;
namespace input {struct PadState;}
namespace story_scenes {
inline constexpr uint32_t kScratchBytes = 256;
using Resolve = std::function<uint32_t(Cpu*,const std::string&,const std::string&)>;
using Actions = std::function<bool(const std::string&,const std::vector<story_notebook::StoryAction>&)>;
void set_game_calls(uint32_t (*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>));
void set_scratch_memory(uint32_t guest);
void set_callbacks(Resolve resolve, Actions actions);
// False with a precise reason when a scene uses a currently unsupported effect.
bool validate(const std::vector<story_notebook::ManifestScene>& scenes,std::string& why);
bool declare(const std::string& mod,const std::vector<story_notebook::ManifestScene>& scenes,std::string& why);
// Requests wait for the current talk/game event to finish; bounded FIFO, one scene at a time.
bool request(const std::string& mod,const std::string& scene);
void trigger(const std::string& kind,const std::string& mod,const std::string& id,unsigned step=0);
void install(void (*original)(Cpu*,uint32_t),const std::function<std::string(uint32_t)>& replaced_by,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
// frames: 60 Hz timeline units; full 30 Hz base logic services pass 2.
void tick(Cpu* cpu,bool skip=false,unsigned frames=1);
void mute(input::PadState& pad);
void state_loaded();
void abort();
bool active();
size_t current_step();
} // namespace story_scenes
