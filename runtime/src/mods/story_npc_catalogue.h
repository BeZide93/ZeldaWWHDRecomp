// The NPC catalogue: the game's residents a story mod may name in its manifest ("who":
// "windfall-light-keeper"), one table per place, kept as data in runtime/assets/npc_catalogue/<place>.json
// and compiled in (cmake/EmbedNpcCatalogue.cmake). Adding a place adds a table, never a manifest change
// (docs/mod-sdk-v2.md, "The NPC catalogue"). Pure code, unit-tested in runtime/tools/story_notebook_test.cpp.
//
// Names are "<place>-<role>". An entry identifies the actor generically: its class plus the fields that
// class needs (for "people", daNpcPeople_c, the resident code: "Um1" ...), and where it stands (stage,
// room). A manifest naming an NPC this port doesn't know is refused when the mod is enabled; so is one
// whose "npc_catalogue" (the catalogue version it needs) is newer than this port's.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "mod_json.h"

namespace story_notebook {

// a resident: 0 none, else 1 + its index in the catalogue
using Resident = uint16_t;
inline constexpr Resident kResidentNone = 0;

struct NpcEntry {
    std::string name, place;
    unsigned since = 1;                 // the catalogue version that added it
    std::string actor_class, code, stage;
    int room = -1;
    uint8_t people_number = 0xFF;       // class "people": daNpcPeople_c::mNpcNo
    std::string where;                  // for the notebook
    bool has_spot = false;
    float x = 0, z = 0;                 // where it stands (by day if it is out by day): the chart's ring
    bool day = false, night = false;    // when it is out there
};

struct NpcCatalogue {
    unsigned version = 0;  // the highest "since" of its entries
    std::vector<NpcEntry> entries;

    Resident find(const std::string& name) const {
        for (size_t i = 0; i < entries.size(); i++)
            if (entries[i].name == name) return Resident(i + 1);
        return kResidentNone;
    }
    const NpcEntry* entry(Resident r) const { return r && r <= entries.size() ? &entries[r - 1] : nullptr; }

    // adds one place's table; false with a reason for anything it doesn't accept
    bool add_table(const std::string& text, std::string& why);
};

// "people" (daNpcPeople_c): the resident codes in the game's order (l_npc_staff_id), mNpcNo is the index
inline int people_number_of(const std::string& code) {
    static const char* const kCodes[] = {"Uo1", "Uo2", "Uo3", "Ub1", "Ub2", "Ub3", "Ub4", "Uw1", "Uw2", "Um1",
                                         "Um2", "Um3", "Sa1", "Sa2", "Sa3", "Sa4", "Sa5", "Ug1", "Ug2"};
    for (int i = 0; i < int(sizeof kCodes / sizeof *kCodes); i++)
        if (code == kCodes[i]) return i;
    return -1;
}

inline bool NpcCatalogue::add_table(const std::string& text, std::string& why) {
    using mods::json::Value;
    Value t;
    try {
        t = mods::json::parse(text);
    } catch (const std::exception& e) {
        return why = std::string("not JSON: ") + e.what(), false;
    }
    if (t.get("catalogue_format").type != Value::Number || t.get("catalogue_format").number != 1) return why = "unsupported catalogue_format", false;
    const std::string place = t.get("place").type == Value::String ? t.get("place").text : "";
    if (place.empty() || place.find_first_not_of("abcdefghijklmnopqrstuvwxyz-") != std::string::npos) return why = "invalid place", false;
    if (t.get("npcs").type != Value::Array) return why = place + ": no npcs", false;
    std::vector<NpcEntry> added;  // all or nothing
    unsigned newest = version;
    for (const Value& n : t.get("npcs").array) {
        NpcEntry e;
        e.place = place;
        e.name = n.get("name").type == Value::String ? n.get("name").text : "";
        if (e.name.rfind(place + "-", 0) != 0 || e.name.size() == place.size() + 1 || e.name.size() > 48 ||
            e.name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos)
            return why = place + ": the name \"" + e.name + "\" is not <place>-<role>", false;
        bool taken = find(e.name) != kResidentNone;
        for (const NpcEntry& a : added) taken |= a.name == e.name;
        if (taken) return why = "duplicate name " + e.name, false;
        const Value& since = n.get("since");
        if (since.type != Value::Number || since.number < 1 || since.number > 1000 || std::floor(since.number) != since.number)
            return why = e.name + ": invalid since", false;
        e.since = unsigned(since.number);
        const Value& actor = n.get("actor");
        e.actor_class = actor.get("class").type == Value::String ? actor.get("class").text : "";
        e.stage = actor.get("stage").type == Value::String ? actor.get("stage").text : "";
        if (e.stage.empty() || e.stage.size() > 7) return why = e.name + ": invalid stage", false;
        if (actor.get("room").type == Value::Number) e.room = int(actor.get("room").number);
        if (e.actor_class == "people") {
            e.code = actor.get("code").type == Value::String ? actor.get("code").text : "";
            const int number = people_number_of(e.code);
            if (number < 0) return why = e.name + ": unknown people code", false;
            e.people_number = uint8_t(number);
        } else {
            return why = e.name + ": unsupported actor class \"" + e.actor_class + "\"", false;
        }
        e.where = n.get("where").type == Value::String ? n.get("where").text : "";
        if (e.where.empty() || e.where.size() > 64) return why = e.name + ": invalid where", false;
        const Value& spot = n.get("spot");
        if (spot.type == Value::Object) {
            if (spot.get("x").type != Value::Number || spot.get("z").type != Value::Number) return why = e.name + ": invalid spot", false;
            e.has_spot = true, e.x = float(spot.get("x").number), e.z = float(spot.get("z").number);
        }
        for (const Value& p : n.get("present").array) {
            if (p.type == Value::String && p.text == "day") e.day = true;
            else if (p.type == Value::String && p.text == "night") e.night = true;
            else return why = e.name + ": present lists day and/or night", false;
        }
        if (!e.day && !e.night) return why = e.name + ": never present", false;
        if (e.since > newest) newest = e.since;
        added.push_back(std::move(e));
    }
    for (NpcEntry& e : added) entries.push_back(std::move(e));
    version = newest;
    return true;
}

// the port's catalogue, built once from the compiled-in tables (generated; see cmake/EmbedNpcCatalogue.cmake)
extern "C" const char* const wwhd_npc_catalogue_tables[];
extern "C" const unsigned wwhd_npc_catalogue_table_count;
inline const NpcCatalogue& npc_catalogue() {
    static const NpcCatalogue c = [] {
        NpcCatalogue built;
        for (unsigned i = 0; i < wwhd_npc_catalogue_table_count; i++) {
            std::string why;
            if (!built.add_table(wwhd_npc_catalogue_tables[i], why)) {
                fprintf(stderr, "[notebook] NPC catalogue table %u not used: %s\n", i, why.c_str());
            }
        }
        return built;
    }();
    return c;
}

}  // namespace story_notebook
