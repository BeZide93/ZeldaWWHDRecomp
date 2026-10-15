#pragma once
#include "story_world_manifest.h"
#include <functional>
#include <initializer_list>
#include <string>
struct Cpu;
namespace mod_actor {
inline constexpr uint16_t kProfile=0x7FFE;
inline constexpr uint32_t kScratchBytes=4096;
void configure(void(*original)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>),uint32_t scratch);
// Declaration tokens go into native create parameters; they remain stable across state loads.
uint32_t declare(const std::string& mod,const story_notebook::ManifestActor&,const std::string& archive,int resource_index,std::function<void()> talked,
                 story_notebook::WorldPoint minimum={-50,0,-50},story_notebook::WorldPoint maximum={50,200,50});
bool install(const std::function<std::string(uint32_t)>& replaced_by,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
void state_loaded();
bool talking(uint32_t actor);
bool failed(uint32_t declaration_token);
void retry(uint32_t declaration_token);
}
