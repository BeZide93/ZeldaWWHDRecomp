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
    const char* kind;     // the manifest's actor: chuchu, bokoblin, moblin, keese
    const char* name;     // parameters.variant
    uint32_t params;      // the fixed actor parameters
    const char* label;    // the editor's name for it
    const char* help;     // plain words
};

inline constexpr Variant kVariants[] = {
    {"chuchu", "green", 0xFF000000u, "Green ChuChu", "The ordinary green ChuChu."},
    {"chuchu", "red", 0xFF000100u, "Red ChuChu", "A red ChuChu: it jumps at Link when he comes close."},
    {"chuchu", "blue", 0xFF000200u, "Blue ChuChu", "A blue ChuChu: it sparks with electricity, so a sword hit shocks Link."},
    {"chuchu", "yellow", 0xFF000400u, "Yellow ChuChu", "A yellow ChuChu: it jumps at Link and sparks with electricity."},
    // Bokoblin (d_a_bk): type 4 (the ordinary one that waits and searches), no path, no switches; bit 0x20
    // gives it a shield and 7 health instead of 5, bit 0x40 a machete (daBoko type 1) instead of the stick.
    {"bokoblin", "stick", 0xFFFFFF04u, "Bokoblin", "The ordinary Bokoblin with a stick."},
    {"bokoblin", "shield", 0xFFFFFF24u, "Bokoblin with a shield", "A Bokoblin with a stick and a shield: it blocks, and takes more hits."},
    {"bokoblin", "machete", 0xFFFFFF44u, "Bokoblin with a machete", "A Bokoblin with a machete instead of the stick."},
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
// the variant names of a kind, for messages: "green, red, blue or yellow"
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
// the weapon (daBoko parameter) a Bokoblin variant's parameter word creates: 1 the machete, 0 the stick
inline constexpr uint32_t bokoblin_weapon(uint32_t params) { return (params & 0x40u) ? 1u : 0u; }
// whether `params` is one of the kind's declared parameter words
inline bool declared_params(const std::string& kind, uint32_t params) {
    for (const Variant& v : kVariants)
        if (kind == v.kind && v.params == params) return true;
    return false;
}

}  // namespace story_enemy_variants
