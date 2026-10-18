// The game's villagers a mod can place (Mod SDK v3 "resident"): one table shared by the manifest
// validator, the runtime and the world editor (add form, list_actor_types, MCP descriptions).
//
// Every entry but ym1 is the Windfall townspeople class daNpcPeople_c (profile 0x175); its stage
// argument picks the character (mNpcNo). A placed copy always gets the same fixed ordinary
// parameters: no rail (it stands where the mod puts it) and, for the two whose appearance follows
// their story (uw2, um2), the argument that skips that story gate. Everything the class does for a
// character's quests (its messages, item-show and photo callbacks, event orders and event cuts,
// figurine registration, retail name searches) is intercepted per placed copy in story_actors.cpp,
// whatever the character, so a placed copy only talks with the mod's text and flags.
//
// Characters whose ordinary behaviour itself reads or writes the game's own progress, or moves other
// retail actors, are refused with the reason (kRefused). Pure data, unit-tested in
// runtime/tools/story_world_manifest_test.cpp.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace story_resident_types {

inline constexpr uint8_t kSeatedYm1 = 19;       // ManifestActor::resident for the seated ym1 (another class)
inline constexpr uint16_t kPeopleProfile = 0x175;
inline constexpr uint16_t kYm1Profile = 0x13B;

struct Type {
    const char* code;    // the native object code, lower case: what a manifest names
    uint8_t subtype;     // daNpcPeople_c::mNpcNo (the stage argument); kSeatedYm1 for ym1
    uint32_t params;     // the fixed actor parameters of a placed copy
    const char* label;   // the editor's name for it
    const char* help;    // plain words: who this looks like and what to know
};

// The order is the editor's picker order (Windfall's people first, the seated ym1 last).
inline constexpr Type kTypes[] = {
    {"uo2", 1, 0x00FF0000u, "Man from the bar", "The man from the Windfall bar."},
    {"um1", 9, 0x00FF0000u, "Windmill keeper", "The man from up on the Windfall windmill."},
    {"uo1", 0, 0x00FF0000u, "Old man on the bench", "The old man from the sea-view bench at the Windfall cliff."},
    {"uo3", 2, 0x00FF0000u, "Well-dressed man", "A well-dressed man; a placed copy stands still."},
    {"ub1", 3, 0x00FF0000u, "Chatting woman (first)", "One of the two women chatting on the Windfall steps."},
    {"ub2", 4, 0x00FF0000u, "Chatting woman (second)", "The other of the two women chatting on the Windfall steps."},
    {"ub3", 5, 0x00FF0000u, "Woman from the pier", "The woman on the Windfall harbour pier."},
    {"ub4", 6, 0x00FF0000u, "Woman by the gate", "The woman by the Windfall town gate (not in the photo studio, Ocmera)."},
    {"uw1", 7, 0x00FF0000u, "Bar worker", "The young woman working in the Windfall bar."},
    {"uw2", 8, 0x00FF00FFu, "Young woman", "The young woman of the Windfall town square; a placed copy is always there."},
    {"um2", 10, 0x00FF00FFu, "Young man", "The young man of the Windfall town square; a placed copy is always there."},
    {"sa1", 12, 0x00FF0000u, "Sailor from the bar", "A sailor who drinks in the Windfall bar at night."},
    {"sa2", 13, 0x00FF0000u, "Lookout sailor", "The sailor on the Windfall high lookout."},
    {"sa3", 14, 0x00FF0000u, "Sailor from the dock", "The sailor on the Windfall stone dock."},
    {"sa4", 15, 0x00FF0000u, "Sailor from the pier", "The sailor at the end of the Windfall wooden pier."},
    {"ym1", kSeatedYm1, 1u, "Seated man (ym1)", "A seated man; author his height, he doesn't walk or turn in scenes."},
};

struct Refused {const char* code;const char* why;};
// The rest of daNpcPeople_c, with the reason a placed copy would not be safe.
inline constexpr Refused kRefused[] = {
    {"um3", "resident um3 is not supported: his telescope talk at night sets the game's own progress bits"},
    {"sa5", "resident sa5 is not supported: he moves the game's pigs and writes the pig game's registers"},
    {"ug1", "resident ug1 is not supported: the two by the flowers follow the game's week and its hide-and-seek event"},
    {"ug2", "resident ug2 is not supported: the two by the flowers follow the game's week and its hide-and-seek event"},
};

inline const Type* find(const std::string& code) {
    for (const Type& t : kTypes)
        if (code == t.code) return &t;
    return nullptr;
}
inline const Type* by_subtype(uint8_t subtype) {
    for (const Type& t : kTypes)
        if (t.subtype == subtype) return &t;
    return nullptr;
}
inline bool supported(uint8_t subtype) { return by_subtype(subtype) != nullptr; }
inline bool people(uint8_t subtype) { return supported(subtype) && subtype != kSeatedYm1; }
inline uint16_t profile(uint8_t subtype) { return subtype == kSeatedYm1 ? kYm1Profile : kPeopleProfile; }
inline uint32_t params(uint8_t subtype) {
    const Type* t = by_subtype(subtype);
    return t ? t->params : 0x00FF0000u;
}
// "uo2, um1, ..., or the seated ym1": the list the validator and the editor quote
inline std::string codes() {
    std::string out;
    for (const Type& t : kTypes) {
        if (!out.empty()) out += ", ";
        out += t.code;
    }
    return out;
}
inline const char* refused(const std::string& code) {
    for (const Refused& r : kRefused)
        if (code == r.code) return r.why;
    return nullptr;
}
// Places a placed copy can't stand: the native create logic moves or hides the character there.
inline const char* place_problem(uint8_t subtype, const std::string& stage, float x, float z) {
    if (subtype == kSeatedYm1 && std::fabs(x + 202066.f) < 10 && std::fabs(z - 320123.f) < 10)
        return "ym1 position intersects native relocation area";
    if (subtype == 6 && stage == "Ocmera") return "resident ub4 cannot be placed in the Ocmera stage: the game decides there whether she appears";
    return nullptr;
}

}  // namespace story_resident_types
