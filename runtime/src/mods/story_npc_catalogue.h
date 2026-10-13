// The NPC catalogue: the game's characters a story mod may name in its manifest ("who": "windfall-kreeb"),
// one table per place, kept as data in runtime/assets/npc_catalogue/<place>.json and compiled in
// (cmake/EmbedNpcCatalogue.cmake). Adding a place adds a table, never a manifest change
// (docs/mod-sdk-v2.md, "The NPC catalogue"). Pure code, unit-tested in runtime/tools/story_notebook_test.cpp.
//
// Names are "<place>-<name>": the character's name as the game itself gives it (with the evidence in the
// entry), or a plain role marked "named": false where the game data doesn't tie a name to the actor. An
// entry lists the actors that are this character, generically: the actor class plus what identifies
// the actor in that class (for "people", daNpcPeople_c, its resident code "Um1"; for other classes the
// stage actor name), and where each stands (stage, room, the room layers it is in). A manifest naming a
// character this port doesn't know is refused when the mod is enabled; so is one whose "npc_catalogue"
// (the catalogue version it needs) is newer than this port's, and one naming a character the port
// can't give a quest's lines yet (only "people" actors can talk for a quest so far).
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

struct NpcActor {
    std::string actor_class, code, stage;  // code: the people resident code, or the stage actor name
    int room = -1;
    uint8_t people_number = 0xFF;          // class "people": daNpcPeople_c::mNpcNo
};
struct NpcEntry {
    std::string name, place, evidence, gate, unconfirmed;  // unconfirmed: what the static evidence leaves open
    bool named = false;
    unsigned since = 1;                 // the catalogue version that added it
    std::vector<NpcActor> actors;
    std::string where;                  // for the notebook
    bool has_spot = false;
    float x = 0, z = 0;                 // where it stands outside (by day if it is out by day): the chart's ring
    bool day = false, night = false;    // when it is out
    bool talks() const {                // the port can give it a quest's lines
        for (const NpcActor& a : actors)
            if (a.actor_class == "people") return true;
        return false;
    }
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

inline bool NpcCatalogue::add_table(const std::string& table, std::string& why) {
    using mods::json::Value;
    Value t;
    try {
        t = mods::json::parse(table);
    } catch (const std::exception& e) {
        return why = std::string("not JSON: ") + e.what(), false;
    }
    if (t.get("catalogue_format").type != Value::Number || t.get("catalogue_format").number != 2) return why = "unsupported catalogue_format", false;
    const std::string place = t.get("place").type == Value::String ? t.get("place").text : "";
    if (place.empty() || place.find_first_not_of("abcdefghijklmnopqrstuvwxyz-") != std::string::npos) return why = "invalid place", false;
    if (t.get("npcs").type != Value::Array) return why = place + ": no npcs", false;
    std::vector<NpcEntry> added;  // all or nothing
    unsigned newest = version;
    auto text = [](const Value& v) { return v.type == Value::String ? v.text : std::string(); };
    for (const Value& n : t.get("npcs").array) {
        NpcEntry e;
        e.place = place;
        e.name = text(n.get("name"));
        if (e.name.rfind(place + "-", 0) != 0 || e.name.size() == place.size() + 1 || e.name.size() > 48 ||
            e.name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos)
            return why = place + ": the name \"" + e.name + "\" is not <place>-<name>", false;
        bool taken = find(e.name) != kResidentNone;
        for (const NpcEntry& a : added) taken |= a.name == e.name;
        if (taken) return why = "duplicate name " + e.name, false;
        const Value& since = n.get("since");
        if (since.type != Value::Number || since.number < 1 || since.number > 1000 || std::floor(since.number) != since.number)
            return why = e.name + ": invalid since", false;
        e.since = unsigned(since.number);
        if (n.get("named").type != Value::Bool) return why = e.name + ": named must be true or false", false;
        e.named = n.get("named").boolean;
        e.evidence = text(n.get("evidence"));
        if (e.evidence.empty()) return why = e.name + ": no evidence", false;
        e.gate = text(n.get("gate"));
        e.unconfirmed = text(n.get("unconfirmed"));
        for (const Value& a : n.get("actors").array) {
            NpcActor x;
            x.actor_class = text(a.get("class"));
            x.stage = text(a.get("stage"));
            if (x.actor_class.empty()) return why = e.name + ": an actor without a class", false;
            if (x.stage.empty() || x.stage.size() > 7) return why = e.name + ": invalid stage", false;
            if (a.get("room").type == Value::Number) x.room = int(a.get("room").number);
            if (x.actor_class == "people") {
                x.code = text(a.get("code"));
                const int number = people_number_of(x.code);
                if (number < 0) return why = e.name + ": unknown people code", false;
                x.people_number = uint8_t(number);
            } else {
                x.code = text(a.get("actor"));
                if (x.code.empty() || x.code.size() > 8) return why = e.name + ": an actor of class " + x.actor_class + " needs its stage actor name", false;
            }
            e.actors.push_back(x);
        }
        if (e.actors.empty()) return why = e.name + ": no actors", false;
        e.where = text(n.get("where"));
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
