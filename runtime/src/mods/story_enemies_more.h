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

inline constexpr Native kNatives[] = {
    {Kind::Magtail, "magtail", {0x7FE4, 0xD8, 0x1DD8, kMagtailColliders, std::size(kMagtailColliders)},
     CANONICAL_CODE_ID(0x021DCE84).value(), CANONICAL_CODE_ID(0x021D87A0).value(), CANONICAL_CODE_ID(0x021D7FFC).value(),
     CANONICAL_CODE_ID(0x021DC634).value(), [] { return GD(0x1001565C); }, 0xFFFF0001u, 0, 0x1A1C, {0, 0}, true,
     magtail_verify, magtail_terminal, magtail_liftable, 0x570, false},
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
// the private profile ids of this table (0x7FE0..0x7FE7)
inline constexpr bool reserved(uint16_t profile) { return profile >= 0x7FE0 && profile <= 0x7FE7; }
}  // namespace story_enemies_more
