// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_enemy_ownership.h"
#include "story_items.h"
#include <array>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>
struct Cpu;
namespace story_enemies {
using Kind=story_enemy_ownership::Kind;
using Token=story_enemy_ownership::Token;
// params: the actor parameters of the declared variant (story_enemy_variants.h); 0 is the kind's default
struct Spec {Kind kind;std::string mod,id,defeated_flag;std::vector<story_items::PropDrop> drops;story_enemy_ownership::Fingerprint fingerprint{};uint32_t epoch=0;uint32_t params=0;};
// The fixed native parameters. Bokoblin: type 4 (stands, then fights); byte 1 is its sight and
// territory range in tens of units, 0xFF the game's own (500 to notice Link, 750 to give up); 0 made
// it blind (it never noticed Link). Byte 2 no path, byte 3 no switch.
constexpr uint32_t parameters(Kind k){return k==Kind::ChuChu?0xFF000000u:k==Kind::Bokoblin?0xFFFFFF04u:(k==Kind::Keese||k==Kind::Moblin)?0xFFFF0000u:0u;}
// the manifest's actor name of a kind ("chuchu", ...)
const char* kind_name(Kind);
// the parameters of a kind's variant ("" the default); 0 for a variant the kind doesn't declare
uint32_t parameters(Kind,const std::string& variant);
// a declared enemy's parameters (its variant's), 0 for an unknown token
uint32_t parameters(Token);
constexpr uint16_t home_z(Kind k){return k==Kind::Bokoblin||k==Kind::Moblin?0xFF:0;}
struct ChildBridge {
 std::function<bool(Token,Kind,uint32_t parent,uint32_t epoch)> enter;
 std::function<void()> leave;
 std::function<bool(Token,uint32_t parent)> ready;
 std::function<void(Token,uint32_t parent)> retire;
};
// Root enables this only after the child module and persisted provenance gate pass.
// Empty/default bridge keeps BK/MO declaration and execution closed.
bool configure_children(ChildBridge,bool provenance_verified=false);
// Normalize only the currently selected retired child of the exact live generation.
// Preserve native AI action and Moblin innate/pending-spawn state; never award defeat.
void child_retired(const story_enemy_ownership::LineageInfo&);
// BK/MO remain closed until their weapon-child ownership routes are admitted.
bool supported(Kind);
Token declare(const Spec&,std::string& why);
size_t checkpoint();void rollback(size_t) noexcept;
void bind(Token,uint32_t pid,bool restored=false);
bool available(Token);void quarantine(Token);bool begin_epoch(Token,uint32_t epoch);
void state_loaded();
// Pure bounded native reads: suitable for the ownership core's mandatory callback.
bool verify_actor(Token,Kind,uint32_t actor);
// Compose with the shared player lift dispatcher; no competing player hook.
bool owns_actor(uint32_t actor);
bool deny_carry(uint32_t actor);
void configure(void(*original)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>),const std::array<uint32_t,4>& profile_storage,std::function<void(Token,uint32_t)> retired={});
bool install(const std::function<std::string(uint32_t)>& owner,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
// World editor (--editor only, main game thread). editor_enable before install: the profiles install
// even without declarations, so enemies placed while the game runs appear at once (live()).
// editor_declare adds a declaration then (same checks as declare). editor_redeclare gives a declaration
// a changed spec of the same kind (drops, defeat flag, fingerprint) while no copy of it exists.
void editor_enable();
bool live();
Token editor_declare(const Spec&,std::string& why);
bool editor_redeclare(Token,const Spec&,std::string& why);
// A copy moved in place: the declaration's fingerprint follows (its ownership record too).
bool editor_refingerprint(Token,const story_enemy_ownership::Fingerprint&);
}
