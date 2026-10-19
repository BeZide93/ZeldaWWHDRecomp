// SPDX-License-Identifier: MPL-2.0
// The native enemies added after the Rat, one row each: the game's class (profile, size, methods,
// vtable, colliders, enemyice/enemyfire helpers), the fixed parameter word and two pure checks: the
// fields that prove a created actor is the declared one, and the state in which its native drop is
// its defeat. story_enemies, story_enemy_ownership and the profiles read this table, so an enemy
// added here needs no change to their per-kind code. Addresses are canonical (GC/GD apply at use).
// The research behind each row (switches, save bits, children, effects on Link) is in its comment.
#pragma once
#include "story_enemy_ownership.h"
#include "ppc.h"
#include "guest_addr.h"
#include "story_native_canonical_code_id.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>

namespace story_enemies_more {
using story_enemy_ownership::Collider;
using story_enemy_ownership::Descriptor;
using story_enemy_ownership::Kind;
using story_native_code_identity::CANONICAL_CODE_ID;

// The private profile ids of the kinds added from the Kargaroc on, in one place: from 0x7FDF downward
// (the reserved range reaches down to 0x7FD0). Earlier rows keep the ids written in their rows.
inline constexpr uint16_t kKargarocProfile = 0x7FDF, kWizzrobeProfile = 0x7FDE, kWarshipProfile = 0x7FDD,
                          kGyorgProfile = 0x7FDC, kFloormasterProfile = 0x7FDB, kBigOctoProfile = 0x7FDA;

struct Native {
    Kind kind;
    const char* name;           // the manifest's actor
    Descriptor descriptor;      // private profile, native profile, class size, owned colliders
    uint32_t create, execute, draw, destroy;  // the class's methods (canonical code: GC applies at use)
    uint32_t (*vtable)();       // the class vtable at actor+0xB4 (mapped data)
    uint32_t params;            // the default variant's parameter word
    uint16_t home_z;            // home angle z
    uint32_t ice;               // the enemyice helper (0: none)
    uint32_t fire[2];           // the enemyfire helpers (0: none)
    bool offered;               // placeable by mods (false: adapter kept, not offered, see its comment)
    bool (*verify)(uint32_t actor, uint32_t params);  // pure bounded reads after its create
    bool (*terminal)(uint32_t actor);                 // its native drop (fopAcM_createDisappear) is its defeat
    bool (*liftable)(uint32_t actor);                 // Link may lift it now (nullptr: never)
    uint32_t action;            // its action field, for traces (u8; u16 when action16)
    bool action16;
};

inline int8_t s8(uint32_t a) { return int8_t(ld8(a)); }

// ---------------------------------------------------------------- Stalfos (d_a_st)
// Parameter 0xFFFFFF00: behaviour 0 (stands and fights; never the ambush or coffin ones, which read the
// ambush switch), sight byte 0xFF, ambush switch 0xFF; angle z 0: no death switch (never isSwitch,
// onSwitch or the boss status). setID 0xFFFF makes its onActor a no-op. It never writes Link.
// Children: its club (Boko 0x1CF, parameter 2) is an owned child (story_enemy_children) and is deleted
// with it (the game leaves it lying; owned, Link could never take it). A spin attack that empties its
// body would split it, creating a second native Stalfos (0xBE, 0xE: the upper body) with the game's own
// profile, drops and colliders; that create is refused and the Stalfos collapses as from any other
// finishing blow instead (the head must still be hit). Colliders on mStts 0x62C: head 0x668, body
// 0x794..0xD70, attack 0xE9C (Link and "other"; the pair policy keeps Link only); enemyice 0x21F0.
inline constexpr Collider kStalfosColliders[] = {
    {0x668, 0x62C, 0}, {0x794, 0x62C, 0}, {0x8C0, 0x62C, 0}, {0x9EC, 0x62C, 0}, {0xB18, 0x62C, 0},
    {0xC44, 0x62C, 0}, {0xD70, 0x62C, 0}, {0xE9C, 0x62C, 0}, {0x225C, 0x2220, 0x21F0}};
inline bool stalfos_verify(uint32_t a, uint32_t p) {
    const unsigned action = ld16(a + 0x3E2);
    return ld8(a + 0x3D0) == (p & 0xF) && ld8(a + 0x3D1) == ((p >> 8) & 0xFF) && ld8(a + 0x3D2) == ((p >> 16) & 0xFF) &&
           ld8(a + 0x3D3) == (p >> 24) && ld8(a + 0x3D4) == 0 && ld8(a + 0x25AD) == 0 &&
           (action == 0 || action == 5 || action == 29 || action == 30 || action == 31);
}
// the head knocked off after the collapse (head part state 8), or the collapse that timed out
inline bool stalfos_terminal(uint32_t a) { return ld8(a + 0x1648) == 8 || (ld16(a + 0x3E2) == 30 && ld16(a + 0x414) == 0); }
// the refused split: the body is down (LOWER_BODY with no upper body); it becomes the ordinary collapse
// (BREAK_WAIT 30, sub-state 0, parts command 1, lifetime 0, body health 0), like the other finishing blows
inline bool stalfos_refused_split(uint32_t a) { return ld16(a + 0x3E2) == 33 && ld32(a + 0x201C) == 0xFFFFFFFFu; }
inline void stalfos_collapse(uint32_t a) {
    st16(a + 0x3E2, 30); st16(a + 0x3E0, 0); st8(a + 0xFEF, 1); st16(a + 0x414, 0); st8(a + 0x20CC, 0);
}
inline constexpr uint16_t kStalfosClub = 0x1CF;     // the club's profile (Boko)
inline constexpr uint32_t kStalfosClubParams = 2;   // Type_STALFOS_MACE
inline constexpr uint16_t kStalfosUpperBody = 0xBE; // the split's second Stalfos (refused)

// ---------------------------------------------------------------- Redead (d_a_rd)
// Parameter 0xFFFFFFFE: standing (bit 0 clear), its own radius (0x7F: 650), the ordinary mode (byte 1
// non-zero: never the coffin, whose wait reads switch byte 3), switch 0xFF. No children, no save writes
// (setID 0xFFFF). Its scream and its bite are the stage's events DEFAULT_RD_CRY / DEFAULT_RD_ATTACK:
// while one runs Link is held in his demo state until the event ends. The game ends them only from the
// Redead's own execute, so story_enemies ends the event it holds when it is removed, stopped or left
// over (release_link). Every stage has both events except four (kNoRedeadStages): a Redead there is
// not created (it would scream without effect and wait in its attack forever). Light (the Mirror Shield
// beam, light volumes) paralyses it; Light Arrows destroy it. Colliders: body cylinder 0x738 on mStts
// 0x6FC (no attack), enemyice 0x8B4, enemyfire 0xC6C.
inline constexpr Collider kRedeadColliders[] = {{0x738, 0x6FC, 0}, {0x920, 0x8E4, 0x8B4}, {0xD48, 0xD0C, 0xC6C}};
inline bool redead_verify(uint32_t a, uint32_t p) {
    return ld32(a + 0x3D0) == (p & 1) && ld32(a + 0x3D8) == ((p >> 8) & 0xFF) && ld32(a + 0x3DC) == (p >> 24) && ld32(a + 0x3C8) <= 9;
}
// DEATH (mode 3) once its last timer ran out (the group already ENV): the one createDisappear
inline bool redead_terminal(uint32_t a) { return ld32(a + 0x3C8) == 3 && ld32(a + 0x4EC) == 0 && ld8(a + 0x2DA) == 3; }
inline bool redead_holds_link(uint32_t a) { return ld32(a + 0x3C8) == 5 || ld32(a + 0x3C8) == 7; }
inline constexpr const char* kNoRedeadStages[] = {"Ebesso", "kazan", "Msmoke", "tincle"};

// ---------------------------------------------------------------- Poe (d_a_pw)
// Parameter 0xFFFFFE00: behaviour 0 (seen, with its lantern), follows the ground, colour 0x7F (0),
// notice range 0xFF (1000), no room path (0xFF). Never the Jalhalla parts (3/4). No switches or save
// bits. Its lantern (Kantera 0xC1, a held lantern without switch) is created by the game and deleted
// with the Poe by story_enemies (the game leaves it floating). Without its lantern it possesses Link:
// a short event (its own potential event, admitted for an owned Poe; the camera stops), then Link's
// confusion flag (mNoResetFlg1 0x100) and the shared TORITUKI_ON. The game ends all of that only from
// the Poe; story_enemies ends it when the Poe is removed, stopped or left over (release_link).
// Outdoors (no light volumes) only Light Arrows defeat it. Colliders on mStts 0x708: body cylinder
// 0x744 (Link only), lantern sphere 0x874 (Link only); enemyice 0x9A0, enemyfire 0xD58.
inline constexpr Collider kPoeColliders[] = {{0x744, 0x708, 0}, {0x874, 0x708, 0}, {0xA0C, 0x9D0, 0x9A0}, {0xE34, 0xDF8, 0xD58}};
inline bool poe_verify(uint32_t a, uint32_t p) {
    return ld8(a + 0x454) == (p & 0xFF) && ld8(a + 0x455) == ((p >> 8) & 1) && ld8(a + 0x456) == ((p >> 16) & 0xFF) && ld8(a + 0x457) == 0 &&
           ld8(a + 0x475) == 0xFF && ld32(a + 0x464) == 0 && ld32(a + 0x488) == 0xFFFFFFFFu && ld16(a + 0x482) <= 4;
}
// mode 61 faded out without fleeing (m38A 0): the one createDisappear; a Poe that fled after its
// possession deletes itself without a drop (no defeat)
inline bool poe_terminal(uint32_t a) { return ld16(a + 0x482) == 2 && ld16(a + 0x484) == 61 && ld16(a + 0x4B6) == 0 && ld16(a + 0x4A6) == 0; }
inline uint32_t poe_possessing() { return GD(0x101CF0A4); }  // TORITUKI_ON (u8)
inline constexpr uint16_t kPoeLantern = 0xC1;

// ---------------------------------------------------------------- Mothula (d_a_gm)
// Parameters (story_enemy_variants "mothula"): 0xFF01FFFF ground (type 1, no wings), 0xFF02FFFF winged
// (type 2); never type 0 (the Forbidden Woods mini-boss with its cutscene, Link and camera writes).
// Byte 3 0xFF: no switch; angle z 0: no save switch; setID 0xFFFF: onActor is a no-op.
// Children: its moths are the game's own Morths (fopAcM_createChild 0xCD, parameters 4/5) with their
// native drops and their own cling statics: an owned Mothula's release is refused (story_enemies), so it
// fights alone (place Morths beside it instead). A winged one's lost wings are falling wing pieces
// (0xCC, no colliders, no save writes) that delete themselves. Its dive and wind cylinders are also given
// to the area manager (grass); that is skipped for an owned one, so they reach Link only through the
// pair policy. A winged one that dives far from where it was placed (2000) turns back home (dive state
// 37, the game's own retreat). Colliders on mStts 0x788; enemyice 0xCA4, enemyfire 0x105C.
inline constexpr Collider kMothulaColliders[] = {
    {0x7C4, 0x788, 0}, {0x8F4, 0x788, 0}, {0xA24, 0x788, 0}, {0xB54, 0x788, 0}, {0xD10, 0xCD4, 0xCA4}, {0x1138, 0x10FC, 0x105C}};
inline bool mothula_verify(uint32_t a, uint32_t p) {
    const unsigned action = ld8(a + 0x3EC);
    return ld8(a + 0x3E9) == 0 && ld8(a + 0x3EA) == ((p >> 16) & 0xFF) && ld8(a + 0x3EB) == 0xFF && ld8(a + 0x3F2) == 0 &&
           (action <= 5 || action == 10);
}
inline bool mothula_terminal(uint32_t a) { return ld8(a + 0x3EC) == 10 && ld16(a + 0x3F8) == 101; }
inline constexpr uint32_t kCreateChild = CANONICAL_CODE_ID(0x025D5A20).value(), kAreaManagerSet = CANONICAL_CODE_ID(0x02516C14).value();
inline constexpr uint16_t kMothulaMoth = 0xCD;
// a winged Mothula diving (dive action 3, states 34..36) farther than 2000 from its home (mHomePos 0x470)
inline bool mothula_strayed(uint32_t a) {
    const unsigned state = ld16(a + 0x3F8);
    if (ld8(a + 0x3EC) != 3 || state < 34 || state > 36) return false;
    const float dx = u32_as_f32(ld32(a + 0x314)) - u32_as_f32(ld32(a + 0x470)), dz = u32_as_f32(ld32(a + 0x31C)) - u32_as_f32(ld32(a + 0x478));
    return dx * dx + dz * dz > 2000.0f * 2000.0f;
}
inline void mothula_return(uint32_t a) { st16(a + 0x3F8, 37); }

// ---------------------------------------------------------------- Morth (d_a_ks)
// Parameter 0xFFFFFF03: type 3, the ordinary single Morth (never the spawners 0/1/6, which create
// unowned Morths, nor the Mothula child 4/5, which needs a parent). No switches, save bits or paths.
// Its death outside Ganon's Tower makes no drop: dead_eff_set (hooked in story_enemies) is its defeat.
// Clinging: an attached Morth (action 4, modes 40..41, joint matrix 0x7A4) counts itself in the shared
// statics KUTTUKU_ALL_COUNT / GORON_COUNT and bumps Link's per-frame counter (Link clears the heavy
// state himself once fewer than 5 cling). It never moves to the stage layer when owned (so it leaves
// with its room), and removed while attached it takes itself out of the statics as the game's own spin
// detach does (story_enemies). Colliders on mStts 0x7BC: body 0x7F8 (Link only); enemyice 0x924, enemyfire 0xCDC.
inline constexpr Collider kMorthColliders[] = {{0x7F8, 0x7BC, 0}, {0x990, 0x954, 0x924}, {0xDB8, 0xD7C, 0xCDC}};
inline bool morth_verify(uint32_t a, uint32_t p) {
    return ld8(a + 0x544) == (p & 0xFF) && ld8(a + 0x545) == ((p >> 8) & 0xFF) && ld8(a + 0x546) == 0 && ld32(a + 0x550) == 0 && ld8(a + 0x547) <= 4;
}
inline bool morth_dying(uint32_t a) { return ld8(a + 0x547) == 3 && (ld8(a + 0x548) == 31 || ld8(a + 0x548) == 32); }
inline bool morth_attached(uint32_t a) { return ld8(a + 0x547) == 4 && (ld8(a + 0x548) == 40 || ld8(a + 0x548) == 41) && ld32(a + 0x7A4) != 0; }
inline uint32_t morth_clinging() { return GD(0x101B8840); }   // KUTTUKU_ALL_COUNT
inline uint32_t morth_shake_off() { return GD(0x101B8848); }  // GORON_COUNT
inline constexpr uint32_t kMorthDeathEffect = CANONICAL_CODE_ID(0x021A7B4C).value(), kStageLayer = CANONICAL_CODE_ID(0x025D537C).value();

// ---------------------------------------------------------------- Magtail (d_a_mt)
// Parameter 0xFFFF0001: type 1 (goes for Link; odd: no per-frame lava drip particles), no snap turn,
// bit 15 clear (angle z is the death switch, and z is 0: no switch; never the respawn switch, whose
// delete would request a new untracked Magtail), no room path (0xFF), no appear switch (0xFF).
// No children: its 8 segments are internal (one mStts 0xF28; spheres 0xF64 + 0x12C*i, eye TG 0x18C4).
// No switch/save writes with this word (setID 0xFFFF makes onActor a no-op). It never writes Link.
// The curled ball is the game's own carry: Link may lift and throw it; a thrown ball's attack is
// limited to Link by the ownership pair policy. enemyice 0x1A1C (light arrows); no enemyfire.
inline constexpr Collider kMagtailColliders[] = {
    {0xF64, 0xF28, 0}, {0x1090, 0xF28, 0}, {0x11BC, 0xF28, 0}, {0x12E8, 0xF28, 0}, {0x1414, 0xF28, 0},
    {0x1540, 0xF28, 0}, {0x166C, 0xF28, 0}, {0x1798, 0xF28, 0}, {0x18C4, 0xF28, 0}, {0x1A88, 0x1A4C, 0x1A1C}};
inline bool magtail_verify(uint32_t a, uint32_t p) {
    const unsigned action = ld8(a + 0x570);
    return ld8(a + 0x3D0) == (p & 0xFF) && ld8(a + 0x3D1) == 0 && ld8(a + 0x3D2) == 0 && ld8(a + 0x3D3) == 0xFF &&
           ld8(a + 0x3D4) == 0xFF && ld8(a + 0x3D5) == 0 && ld8(a + 0x3D6) == 0 && ld8(a + 0x3D7) == 0 && ld8(a + 0x3D8) == 0 &&
           ld32(a + 0x3DC) == 0 && ld8(a + 0x1A1C + 0x1B1) == 0 && (action <= 3 || action == 6);
}
// bakuha's two callers: the execute tail (damage mode >= 2) and the parry pin's last frame
inline bool magtail_terminal(uint32_t a) {
    return ld8(a + 0x570) != 3 && (s8(a + 0x1A14) >= 2 || (ld8(a + 0x570) == 6 && ld16(a + 0x572) == 1));
}
// curled (ROLL), or the ball Link holds while it unrolls (the game's own release follows)
inline bool magtail_liftable(uint32_t a) {
    const unsigned action = ld8(a + 0x570);
    return action == 2 || ((ld32(a + 0x2E0) & 0x2000u) && action == 1);
}

// ---------------------------------------------------------------- Armos (d_a_am2)
// Parameter 0xFF00FF00: wake radius 600 (0xFF), active, no switch (0xFF: every isSwitch read is behind a
// 0xFF guard, and it never writes a switch). No children (the drop and a water pillar only), never
// writes Link. A paralysed Armos is the game's carry; it stays off (keep_owned clears the attention
// bit), so it never leaves its room (carried out, it would delete itself without a drop). In water or
// over an abyss it returns to its spawn point by itself. Defeated from behind; health 2.
inline constexpr Collider kArmosColliders[] = {
    {0x6BC, 0x680, 0}, {0x7EC, 0x680, 0}, {0x91C, 0x680, 0}, {0xA48, 0x680, 0}, {0xC14, 0xBD8, 0xBA8}};
inline bool armos_verify(uint32_t a, uint32_t p) {
    return ld8(a + 0x3E4) == (p & 0xFF) && ld8(a + 0x3E5) == ((p >> 8) & 0xFF) && ld8(a + 0x3E6) == 0 && ld8(a + 0x3E7) == 0xFF &&
           ld8(a + 0x3EC) == 0 && ld8(a + 0x3E8) <= 4;
}
// DEAD3 (ITAI mode 25) after its health ran out: the one createDisappear of daAM2_Execute
inline bool armos_terminal(uint32_t a) { return ld8(a + 0x3E8) == 2 && ld8(a + 0x3E9) == 25 && s8(a + 0x3A1) <= 0; }

// ---------------------------------------------------------------- Armos Knight (d_a_am)
// Parameter 0xFF00FF01: type 1 (it hops back to where it was placed beyond its radius), radius 400,
// active, no switch (0xFF; read only, never written). Only a bomb in its open mouth defeats it (or a
// Light Arrow): it holds Link's bomb by PID (0x400) while it swallows it and sets it off as it dies.
// Removed while it swallows (modes 44..46), the held bomb is deleted with it (story_enemies). Its spikes
// hit every group natively; the ownership pair policy limits them to Link. A fall far below its spawn
// point runs the death without a bomb: no defeat. No fire helper (never set by the game).
inline constexpr Collider kArmosKnightColliders[] = {
    {0x720, 0x6E4, 0}, {0x84C, 0x6E4, 0}, {0x978, 0x6E4, 0}, {0xAA8, 0x6E4, 0}, {0xC44, 0xC08, 0xBD8}};
inline bool armos_knight_verify(uint32_t a, uint32_t p) {
    return ld8(a + 0x3DC) == (p & 0xFF) && ld8(a + 0x3DD) == ((p >> 8) & 0xFF) && ld8(a + 0x3DE) == 0 && ld8(a + 0x3DF) == 0xFF &&
           ld8(a + 0x3E0) <= 3;
}
inline bool armos_knight_terminal(uint32_t a) { return ld8(a + 0x3E0) == 3 && ld8(a + 0x3E1) == 47 && ld32(a + 0x400) != 0xFFFFFFFFu; }
// the swallowed bomb's PID while its native death has not set it off yet (0: none)
inline uint32_t armos_knight_held_bomb(uint32_t a) {
    const unsigned mode = ld8(a + 0x3E1);
    return ld8(a + 0x3E0) == 3 && mode >= 44 && mode <= 46 && ld32(a + 0x400) != 0xFFFFFFFFu ? ld32(a + 0x400) : 0;
}

inline constexpr Native kNatives[] = {
    {Kind::Stalfos, "stalfos", {0x7FE0, 0xBE, 0x25B0, kStalfosColliders, std::size(kStalfosColliders)},
     CANONICAL_CODE_ID(0x02495C70).value(), CANONICAL_CODE_ID(0x02493FB0).value(), CANONICAL_CODE_ID(0x02491F34).value(),
     CANONICAL_CODE_ID(0x024957B4).value(), [] { return GD(0x1003D5BC); }, 0xFFFFFF00u, 0, 0x21F0, {0, 0}, true,
     stalfos_verify, stalfos_terminal, nullptr, 0x3E2, true},
    {Kind::Magtail, "magtail", {0x7FE4, 0xD8, 0x1DD8, kMagtailColliders, std::size(kMagtailColliders)},
     CANONICAL_CODE_ID(0x021DCE84).value(), CANONICAL_CODE_ID(0x021D87A0).value(), CANONICAL_CODE_ID(0x021D7FFC).value(),
     CANONICAL_CODE_ID(0x021DC634).value(), [] { return GD(0x1001565C); }, 0xFFFF0001u, 0, 0x1A1C, {0, 0}, true,
     magtail_verify, magtail_terminal, magtail_liftable, 0x570, false},
    {Kind::Morth, "morth", {0x7FE3, 0xCD, 0xF08, kMorthColliders, std::size(kMorthColliders)},
     CANONICAL_CODE_ID(0x021AA4C8).value(), CANONICAL_CODE_ID(0x021A7F94).value(), CANONICAL_CODE_ID(0x021A71EC).value(),
     CANONICAL_CODE_ID(0x021A9EC8).value(), [] { return GD(0x100136C0); }, 0xFFFFFF03u, 0, 0x924, {0xCDC, 0}, true,
     morth_verify, morth_dying, nullptr, 0x547, false},
    {Kind::Mothula, "mothula", {0x7FE2, 0xCC, 0x1290, kMothulaColliders, std::size(kMothulaColliders)},
     CANONICAL_CODE_ID(0x021507C4).value(), CANONICAL_CODE_ID(0x0214D128).value(), CANONICAL_CODE_ID(0x0214BF60).value(),
     CANONICAL_CODE_ID(0x0214FFD8).value(), [] { return GD(0x1000FDD4); }, 0xFF01FFFFu, 0, 0xCA4, {0x105C, 0}, true,
     mothula_verify, mothula_terminal, nullptr, 0x3EC, false},
    {Kind::Redead, "redead", {0x7FE1, 0xE0, 0xF1C, kRedeadColliders, std::size(kRedeadColliders)},
     CANONICAL_CODE_ID(0x0245B7B8).value(), CANONICAL_CODE_ID(0x0245C498).value(), CANONICAL_CODE_ID(0x0245C728).value(),
     CANONICAL_CODE_ID(0x0245B814).value(), [] { return GD(0x10038EE4); }, 0xFFFFFFFEu, 0, 0x8B4, {0xC6C, 0}, true,
     redead_verify, redead_terminal, nullptr, 0x3CB, false},
    {Kind::Poe, "poe", {0x7FE5, 0xD4, 0xF8C, kPoeColliders, std::size(kPoeColliders)},
     CANONICAL_CODE_ID(0x0244E398).value(), CANONICAL_CODE_ID(0x0244B3E4).value(), CANONICAL_CODE_ID(0x02449F80).value(),
     CANONICAL_CODE_ID(0x0244DD6C).value(), [] { return GD(0x1003841C); }, 0xFFFFFE00u, 0, 0x9A0, {0xD58, 0}, true,
     poe_verify, poe_terminal, nullptr, 0x482, true},
    {Kind::Armos, "armos", {0x7FE6, 0xCB, 0xF60, kArmosColliders, std::size(kArmosColliders)},
     CANONICAL_CODE_ID(0x0204FE84).value(), CANONICAL_CODE_ID(0x0204D3A4).value(), CANONICAL_CODE_ID(0x0204C5F8).value(),
     CANONICAL_CODE_ID(0x0204F9D0).value(), [] { return GD(0x1000702C); }, 0xFF00FF00u, 0, 0xBA8, {0, 0}, true,
     armos_verify, armos_terminal, nullptr, 0x3E8, false},
    {Kind::ArmosKnight, "armos_knight", {0x7FE7, 0xCA, 0x11BC, kArmosKnightColliders, std::size(kArmosKnightColliders)},
     CANONICAL_CODE_ID(0x0204B8E0).value(), CANONICAL_CODE_ID(0x0204938C).value(), CANONICAL_CODE_ID(0x02048B3C).value(),
     CANONICAL_CODE_ID(0x0204B430).value(), [] { return GD(0x10006EA4); }, 0xFF00FF01u, 0, 0xBD8, {0, 0}, true,
     armos_knight_verify, armos_knight_terminal, nullptr, 0x3E0, false},
};

inline const Native* find(Kind k) {
    for (const Native& n : kNatives)
        if (n.kind == k) return &n;
    return nullptr;
}
inline const Native* by_name(const std::string& name) {
    for (const Native& n : kNatives)
        if (name == n.name) return &n;
    return nullptr;
}
inline constexpr uint32_t params(Kind k) {
    for (const Native& n : kNatives)
        if (n.kind == k) return n.params;
    return 0;
}
inline constexpr uint16_t home_z(Kind k) {
    for (const Native& n : kNatives)
        if (n.kind == k) return n.home_z;
    return 0;
}
// the private profile ids of this table (0x7FD0..0x7FE7)
inline constexpr bool reserved(uint16_t profile) { return profile >= 0x7FD0 && profile <= 0x7FE7; }
}  // namespace story_enemies_more
