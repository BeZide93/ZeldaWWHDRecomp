// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_enemy_ownership.h"
#include "story_enemies_more.h"
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
// Rat 0xFFFF0001: a hole-born rat (arg0 1, created as the child of its owned hole), type 0; the bomb rat is
// its variant 0xFFFF0101 (type 1). Rat hole 0xFF010100: no player range, one rat, bomb mode 1 (so the hole
// never creates the rat shop keeper NpcNz), no path: the port spawns its rats (story_actors).
// Peahat 0xFFFFFF00: type 0 (Peahat, never the Seahat and its kill-counter register), default ranges.
// Boko Baba 0xFFFFFF00: kind 0 (head and stem), variant 0 (never a Baba Bud); no switch, path or range fields.
// Octorok 0xFFFFFF00: type 0 (the small one that spits rocks; never the big ones with bombs and the kill counter).
// Miniblin 0xFFFF0A01: behaviour 1 (never respawns), notice range 10 (1000 units), no spawn switches
// Darknut 0xFFFF9604: behaviour 4 (guards its place), sight 150, no path, no spawn switch; its variants
// (story_enemy_variants.h) add a shield, a helmet or a cape through the x rotation word (home_x)
constexpr uint32_t parameters(Kind k){return k==Kind::ChuChu?0xFF000000u:k==Kind::Bokoblin?0xFFFFFF04u:(k==Kind::Keese||k==Kind::Moblin)?0xFFFF0000u:k==Kind::Miniblin?0xFFFF0A01u:k==Kind::Peahat?0xFFFFFF00u:k==Kind::Rat?0xFFFF0001u:k==Kind::Darknut?0xFFFF9604u:k==Kind::RatHole?0xFF010100u:k==Kind::BokoBaba?0xFFFFFF00u:k==Kind::Bubble?0xFFFFFF00u:k==Kind::Octorok?0xFFFFFF00u:story_enemies_more::params(k);}
// the manifest's actor name of a kind ("chuchu", ...)
const char* kind_name(Kind);
// the parameters of a kind's variant ("" the default); 0 for a variant the kind doesn't declare
uint32_t parameters(Kind,const std::string& variant);
// a declared enemy's parameters (its variant's), 0 for an unknown token
uint32_t parameters(Token);
// The z rotation word: 0xFF for the kinds that read a switch from it (Bokoblin, Moblin, Darknut: none).
constexpr uint16_t home_z(Kind k){return k==Kind::Bokoblin||k==Kind::Moblin||k==Kind::Darknut?0xFF:story_enemies_more::home_z(k);}
// The x rotation word a declared enemy is placed with: the Darknut's equipment, 0 for the other kinds.
uint16_t home_x(Token);
// The kinds whose weapons (and the Darknut's cape; the Stalfos's club) are owned children (story_enemy_children).
constexpr bool has_children(Kind k){return k==Kind::Bokoblin||k==Kind::Moblin||k==Kind::Darknut||k==Kind::Stalfos||k==Kind::Wizzrobe;}
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
// BK/MO/TN remain closed until their weapon-child ownership routes are admitted.
bool supported(Kind);
Token declare(const Spec&,std::string& why);
size_t checkpoint();void rollback(size_t) noexcept;
void bind(Token,uint32_t pid,bool restored=false);
bool available(Token);void quarantine(Token);
// the declaration's defeat marker is committed (its defeat counted)
bool is_defeated(Token);bool settle(Token);bool begin_epoch(Token,uint32_t epoch);
void state_loaded();
// Pure bounded native reads: suitable for the ownership core's mandatory callback.
bool verify_actor(Token,Kind,uint32_t actor);
// Compose with the shared player lift dispatcher; no competing player hook.
bool owns_actor(uint32_t actor);
// fopAcM_create filter (story_enemy_children::set_create_filter): an owned Boko Baba creates no actors (its flying
// head, which drops a Boko stick, the stem left by a light arrow, a Baba Bud); every other create passes.
bool deny_create(uint32_t profile,uint32_t parameters);
bool deny_carry(uint32_t actor);
void configure(void(*original)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>),const std::array<uint32_t,story_enemy_ownership::kKindCount>& profile_storage,std::function<void(Token,uint32_t)> retired={});
bool install(const std::function<std::string(uint32_t)>& owner,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
// Patrolling Bokoblins (story_enemy_variants::bokoblin_patrol): while an owned Bokoblin runs, the game's
// room path lookup (dPath_GetRoomPath) for the port's route number answers with that enemy's route record
// (`route` gives it, 0: none); every other lookup is untouched. Installed only with patrols or the editor.
void set_route_provider(std::function<uint32_t(Token)> route);
bool install_routes(const std::function<std::string(uint32_t)>& owner,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
uint32_t room_path_function();
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
