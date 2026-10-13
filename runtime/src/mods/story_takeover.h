// Resident takeovers: a story mod's quest step that has a game resident say the mod's lines (and hand
// out a reward) instead of the resident's own line, for one talk (docs/mod-sdk-v2.md, "Talking to
// residents"). Pure code, unit-tested in runtime/tools/story_notebook_test.cpp.
//
// The rules (user decisions 2026-10-13), enforced here and not by the mods:
// - a takeover is only live for the TRACKED quest, at its current step, while Link carries the quest's
//   letter (if the step needs one); untracked, the resident is the game's;
// - at most one takeover is live for a resident: the tracked quest is one quest;
// - the port never sets or clears a game flag for it. The resident's own line selection does not run
//   for that talk, so a first-talk introduction it would have given is not lost but comes later.
// Mods can't hook a resident's dialogue themselves; they declare the step (wwhd_quest_talk).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "story_notebook_list.h"

namespace story_notebook {

// Residents the port can hand to a quest. Each is identified in the game by its actor class and its
// resident number, and only exists where the game places it.
enum Resident : uint8_t {
    kResidentNone = 0,
    kResidentWindfallHarbour = 1,     // Windfall's harbour (daNpcPeople_c resident 5, "Ub3"): by day, not in the Endless Night
    kResidentWindfallLightKeeper = 2, // Windfall's light keeper (daNpcPeople_c resident 9, "Um1"): always out
};
struct ResidentInfo {
    const char* id;        // the SDK name
    const char* where;     // for the notebook
    uint8_t people_number; // daNpcPeople_c::mNpcNo
    float x, z;            // where the game places it (Windfall room 11 layer 0, user's files)
};
inline const ResidentInfo* resident_info(Resident r) {
    static const ResidentInfo kWindfallHarbour{"windfall-harbour", "Windfall Island, the harbour (by day)", 5, 3827.0f, -202189.0f};
    static const ResidentInfo kWindfallLightKeeper{"windfall-light-keeper", "Windfall Island, up by the light", 9, 1670.0f, -202585.0f};
    return r == kResidentWindfallHarbour ? &kWindfallHarbour : r == kResidentWindfallLightKeeper ? &kWindfallLightKeeper : nullptr;
}
inline Resident resident_by_id(const std::string& id) {
    return id == "windfall-harbour" ? kResidentWindfallHarbour : id == "windfall-light-keeper" ? kResidentWindfallLightKeeper : kResidentNone;
}
inline constexpr Resident kAllResidents[] = {kResidentWindfallHarbour, kResidentWindfallLightKeeper};

// A step's prerequisites (manifest "available"), checked read-only from the save: event flags the game
// set (or didn't) and the time of day. While they don't hold, the notebook says "Not yet possible:
// <text>", the step has no ring on the chart and its resident talk is not live.
struct Availability {
    std::vector<uint16_t> events, not_events;  // the game's event flag numbers (high byte: byte, low byte: mask)
    int time = 0;                              // 0 any, 1 day (6:00-18:00), 2 night
    std::string text;
    bool any() const { return !events.empty() || !not_events.empty() || time; }
};
// event_flags: the save's event flag block (0x100 bytes); hour: the game's hour of the day
inline bool event_flag(const uint8_t* event_flags, uint16_t flag) { return (event_flags[flag >> 8] & (flag & 0xFF)) != 0; }
inline bool available(const Availability& a, const uint8_t* event_flags, int hour) {
    for (uint16_t f : a.events)
        if (!event_flag(event_flags, f)) return false;
    for (uint16_t f : a.not_events)
        if (event_flag(event_flags, f)) return false;
    const bool day = hour >= 6 && hour < 18;
    if (a.time == 1 && !day) return false;
    if (a.time == 2 && day) return false;
    return true;
}

inline constexpr size_t kMaxLinesBytes = 240;  // one message box: up to three lines
using story_mail::kRecordCarried;  // Link carries the quest's letter: key quest_key, value 1

struct Takeover {
    uint16_t quest = 0;  // quest_key
    uint8_t step = 0;    // the step it belongs to (1..254)
    Resident resident = kResidentNone;
    bool needs_letter = true;
    std::string lines;   // UTF-8, '\n' between lines
    uint8_t reward = 0;  // a rupee item (0x01..0x06) or 0
    Availability when;   // the step's prerequisites (checked by the caller with the save's state)
};

inline bool carries(const story_mail::Book& b, uint16_t quest) { return b.get(kRecordCarried, quest) == 1; }

// The live takeover for this resident, or null: the tracked quest's, at its current step, with its
// letter carried. Several mods may declare the same resident; only the tracked quest's can be live.
inline const Takeover* live_takeover(const std::vector<Takeover>& all, const story_mail::Book& b, Resident who,
                                     const uint8_t* event_flags = nullptr, int hour = 12) {
    if (!b.usable()) return nullptr;
    const uint16_t tracked = tracked_key(b);
    if (!tracked) return nullptr;
    const uint8_t step = saved_step(b, tracked);
    if (!step || step == kStepDone) return nullptr;
    for (const Takeover& t : all)
        if (t.quest == tracked && t.resident == who && t.step == step && (!t.needs_letter || carries(b, tracked)) &&
            (!event_flags || available(t.when, event_flags, hour)))
            return &t;
    return nullptr;
}

// After the talk: the letter is handed over and the quest is done (it moves to the notebook's archive).
inline bool complete(story_mail::Book& b, const Takeover& t) {
    b.erase(kRecordCarried, t.quest);
    return finish(b, t.quest) == kStepDone;
}

}  // namespace story_notebook
