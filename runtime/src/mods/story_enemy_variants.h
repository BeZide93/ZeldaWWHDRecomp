// The declared variants of the game's enemies a mod can place (Mod SDK v3 "parameters": {"variant": ...}):
// one table shared by the manifest validator, the runtime (the actor parameters and their checks) and
// the world editor (list_actor_types "variants", add_actor options.variant). A mod never writes raw
// parameters: each variant is a fixed, audited parameter word. The first variant of a kind is its
// default (no "variant" in the manifest), so manifests from before keep their meaning.
//
// Bokoblin variants keep their weapon an owned child of the placed copy (story_enemy_children):
// the weapon a variant creates is the only one its child route admits.
//
// ChuChu (d_a_cc): parameters are behaviour (bits 0..7), colour (8..15), notice range (16..23) and
// the spawn switch (24..31). Every variant is the ground behaviour, the default notice range and no
// switch (0xFF: the game's switch bits are never read); only the colour differs. Colour logic is local
// to the actor (red and yellow jump at Link, blue and yellow spark when hit, black turns to stone);
// its native drops are replaced by the mod's like the green one's. Pure data, unit-tested in
// runtime/tools/story_world_manifest_test.cpp.
#pragma once
#include <cstdint>
#include <string>

namespace story_enemy_variants {

struct Variant {
    const char* kind;     // the manifest's actor: chuchu, bokoblin, darknut, ...
    const char* name;     // parameters.variant
    uint32_t params;      // the fixed actor parameters
    const char* label;    // the editor's name for it
    const char* help;     // plain words
    uint16_t angle_x = 0; // the placement's x rotation word, which some kinds read as a parameter
};

inline constexpr Variant kVariants[] = {
    {"chuchu", "green", 0xFF000000u, "Green ChuChu", "The ordinary green ChuChu."},
    {"chuchu", "red", 0xFF000100u, "Red ChuChu", "A red ChuChu: it jumps at Link when he comes close."},
    {"chuchu", "blue", 0xFF000200u, "Blue ChuChu", "A blue ChuChu: it sparks with electricity, so a sword hit shocks Link."},
    {"chuchu", "yellow", 0xFF000400u, "Yellow ChuChu", "A yellow ChuChu: it jumps at Link and sparks with electricity."},
    {"chuchu", "black", 0xFF000300u, "Black ChuChu", "A black ChuChu: swords bounce off; light turns it to stone, then Link can lift and throw it to break it (or use the hammer)."},
    // Bokoblin (d_a_bk): type 4 (the ordinary one that waits and searches), no path, no switches; bit 0x20
    // gives it a shield and 7 health instead of 5, bit 0x40 a machete (daBoko type 1) instead of the stick.
    // Rat hole (d_a_nzg): the hole itself is always the same; its three rats are ordinary rats, bomb
    // rats (each lights a bomb and chases Link) or both, one after the other.
    {"rat_hole", "rats", 0xFF010100u, "Rat hole", "A hole three rats come out of; they run at Link from behind and steal rupees."},
    {"rat_hole", "bomb_rats", 0xFF010100u, "Rat hole (bomb rats)", "A hole three bomb rats come out of: each lights a bomb, throws it at Link and runs back."},
    {"rat_hole", "mixed", 0xFF010100u, "Rat hole (mixed)", "A hole an ordinary rat, a bomb rat and another ordinary rat come out of."},
    {"bokoblin", "stick", 0xFFFFFF04u, "Bokoblin", "The ordinary Bokoblin with a stick."},
    {"bokoblin", "shield", 0xFFFFFF24u, "Bokoblin with a shield", "A Bokoblin with a stick and a shield: it blocks, and takes more hits."},
    {"bokoblin", "machete", 0xFFFFFF44u, "Bokoblin with a machete", "A Bokoblin with a machete instead of the stick."},
    // Darknut (d_a_tn): behaviour 4 (guards its place, then fights), sight range 150 or 250 (byte 1, tens of
    // units), no path (byte 2 0xFF), no spawn switch (byte 3 0xFF); bits 4..7 the armour colour. Its
    // equipment is the x rotation word's bits 5..7 (0 armour only, 2 a shield, 3 a helmet and a shield,
    // 5 a helmet, a shield and a cape: the Mighty Darknut); a helmet doubles its health (30 instead of
    // 15). The z rotation stays 0xFF: no defeat switch. Each pair is one the game itself places.
    {"darknut", "darknut", 0xFFFF9604u, "Darknut", "The armoured knight with a big sword.", 0x0000},
    {"darknut", "shield", 0xFFFF9614u, "Darknut with a shield", "A Darknut with a shield: it blocks Link's sword.", 0x0040},
    {"darknut", "helmet", 0xFFFF9654u, "Darknut with a helmet", "A Darknut with a helmet and a shield: it takes twice the hits.", 0x0060},
    {"darknut", "mighty", 0xFFFFFA54u, "Mighty Darknut", "The Mighty Darknut: helmet, shield and cape, and it notices Link from further away.", 0x00A0},
    // Keese (d_a_ki): action 0 (waits, then attacks), the nearest notice range, no path, no switch; bit 15 is
    // the Fire Keese (its own model, a burning attack, 2 damage, immune to fire). The game has no Ice Keese.
    {"keese", "normal", 0xFFFF0000u, "Keese", "The ordinary Keese."},
    {"keese", "fire", 0xFFFF8000u, "Fire Keese", "A Fire Keese: its bite sets Link on fire for a moment; fire doesn't hurt it."},
};

// the kind's variant `name` ("" is the default, the kind's first)
inline const Variant* find(const std::string& kind, const std::string& name) {
    for (const Variant& v : kVariants)
        if (kind == v.kind && (name.empty() || name == v.name)) return &v;
    return nullptr;
}
inline bool has_variants(const std::string& kind) {
    unsigned n = 0;
    for (const Variant& v : kVariants) n += kind == v.kind;
    return n > 1;
}
// the variant names of a kind, for messages: "green, red, blue, yellow or black"
inline std::string names(const std::string& kind) {
    std::string out;
    unsigned total = 0, at = 0;
    for (const Variant& v : kVariants) total += kind == v.kind;
    for (const Variant& v : kVariants) {
        if (kind != v.kind) continue;
        if (at) out += ++at == total ? " or " : ", ";
        else ++at;
        out += v.name;
    }
    return out;
}
// how many rats a rat hole has, and whether its rat `n` (0-based) carries a bomb
inline constexpr unsigned kRatsPerHole = 3;
inline bool rat_bomb(const std::string& variant, unsigned n) { return variant == "bomb_rats" || (variant == "mixed" && n == 1); }
// the weapon (daBoko parameter) a Bokoblin variant's parameter word creates: 1 the machete, 0 the stick
inline constexpr uint32_t bokoblin_weapon(uint32_t params) { return (params & 0x40u) ? 1u : 0u; }
// the x rotation word of the kind's variant with parameter word `params` (0 for none)
inline uint16_t angle_x(const std::string& kind, uint32_t params) {
    for (const Variant& v : kVariants)
        if (kind == v.kind && v.params == params) return v.angle_x;
    return 0;
}
// A patrolling Bokoblin: the variant's word with type 0 (it walks its route, d_a_bk jyunkai) and the route
// number kPatrolRoute (the port's route record, never a room path).
inline constexpr uint32_t kPatrolRoute = 0xFE;
inline constexpr uint32_t bokoblin_patrol(uint32_t params) { return (params & ~0x00FF000Fu) | (kPatrolRoute << 16); }
inline constexpr bool bokoblin_patrols(uint32_t params) { return ((params >> 16) & 0xFF) == kPatrolRoute; }
// A patrolling Moblin: its ordinary word (mode 0) with the route number kPatrolRoute (d_a_mo2 mPathIndex).
inline constexpr uint32_t moblin_patrol(uint32_t params) { return (params & ~0x00FF0000u) | (kPatrolRoute << 16); }
// whether a Bokoblin's or Moblin's word names the port's route
inline constexpr bool patrol_word(uint32_t params) { return ((params >> 16) & 0xFF) == kPatrolRoute; }
// whether `params` is one of the kind's declared parameter words (a Bokoblin's also as a patrol)
inline bool declared_params(const std::string& kind, uint32_t params) {
    for (const Variant& v : kVariants)
        if (kind == v.kind && (v.params == params || (kind == "bokoblin" && bokoblin_patrol(v.params) == params))) return true;
    return false;
}

}  // namespace story_enemy_variants
