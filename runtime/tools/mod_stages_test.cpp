// SPDX-License-Identifier: MPL-2.0
// Mod stages: the manifest's "stages" section, the stage names and the builder's recipe (mod_stages_manifest.h).
#include <cassert>
#include <cstdio>
#include <set>
#include <string>

#include "mods/mod_stages_manifest.h"

using mods::json::Value;
using namespace mod_stages;

namespace {
Value parse_json(const std::string& s) { return mods::json::parse(s); }

const char* kValid = R"({
  "stages": [{
    "id": "chamber", "template": "Cave01", "kind": "cave", "music_like": "Cave01",
    "entrance": {"stage": "sea", "room": 34, "point": 1},
    "rooms": [
      {"room": 0, "kit": 0, "ground": "assets/room.glb", "material": "rock", "materials": {"wall": "wall_v"},
       "spawns": [{"point": 0, "position": {"x": -1000, "y": 0, "z": 0}, "angle": 90}],
       "exits": [{"id": "to-sea", "box": {"min": {"x": 900, "y": -100, "z": -800}, "max": {"x": 1200, "y": 100, "z": 800}},
                  "to": {"stage": "sea", "room": 34, "point": 1}},
                 {"id": "to-back", "box": {"min": {"x": -900, "y": -100, "z": -800}, "max": {"x": -800, "y": 100, "z": 800}},
                  "to": {"room": 1, "point": 3}}]},
      {"room": 1, "ground": "assets/back.glb",
       "spawns": [{"point": 3, "position": {"x": 0, "y": 0, "z": 0}, "start": "fall"}],
       "exits": [{"id": "to-other", "box": {"min": {"x": 0, "y": 0, "z": 0}, "max": {"x": 1, "y": 1, "z": 1}}, "to": {"stage": "stage:other", "room": 0, "point": 0}}]}
    ],
    "doors": [{"position": {"x": -1000, "y": 0, "z": 0}, "angle": 270, "rooms": [0, 1]}],
    "entries": [{"id": "passage", "stage": "Cave01", "room": 0, "position": {"x": -1300, "y": -200, "z": -900},
                 "size": {"x": 200, "y": 200, "z": 200}, "to": {"room": 0, "point": 0}}]
  }, {
    "id": "other", "template": "Cave01", "entrance": {"stage": "sea", "room": 34, "point": 1},
    "rooms": [{"room": 0, "ground": "assets/other.glb", "spawns": [{"point": 0, "position": {"x": 0, "y": 0, "z": 0}}]}]
  }]
})";

// the valid manifest with one change (a JSON text replacement)
bool fails(const std::string& from, const std::string& to, const std::string& reason_part) {
    std::string text = kValid;
    const size_t at = text.find(from);
    assert(at != std::string::npos);
    text.replace(at, from.size(), to);
    std::vector<Stage> out;
    std::string why;
    const bool ok = parse(parse_json(text), out, why);
    if (ok || why.find(reason_part) == std::string::npos) {
        fprintf(stderr, "expected a failure with \"%s\" for %s -> %s, got %s \"%s\"\n", reason_part.c_str(), from.c_str(), to.c_str(), ok ? "success" : "failure",
                why.c_str());
        return false;
    }
    return true;
}
}  // namespace

int main() {
    std::vector<Stage> stages;
    std::string why;
    assert(parse(parse_json(kValid), stages, why) && why.empty());
    assert(stages.size() == 2);
    const Stage s = stages[0];  // a copy: stages is parsed into again below
    assert(s.id == "chamber" && s.template_stage == "Cave01" && s.kind == "cave" && s.music_like == "Cave01");
    assert(s.entrance.stage == "sea" && !s.entrance.own && s.entrance.room == 34 && s.entrance.point == 1);
    assert(s.rooms.size() == 2 && s.rooms[1].kit == 1);  // kit: the room's own number when left out
    assert(s.rooms[0].exits[1].to.own && s.rooms[0].exits[1].to.stage.empty());
    assert(s.rooms[1].exits[0].to.own && s.rooms[1].exits[0].to.stage == "other");
    assert(s.rooms[1].spawns[0].start == "fall");
    assert(s.doors.size() == 1 && s.doors[0].a == 0 && s.doors[0].b == 1);
    assert(s.entries.size() == 1 && s.entries[0].stage == "Cave01");
    assert(inside(s.entries[0], -1300, -200, -900) && inside(s.entries[0], -1250, -100, -850) && !inside(s.entries[0], -1300, -200, -1100) &&
           !inside(s.entries[0], -1300, 50, -900));

    // the section is optional
    assert(parse(parse_json("{}"), stages, why) && stages.empty());

    // what Check refuses
    int bad = 0;
    bad += !fails("\"room\": 1, \"ground\"", "\"room\": 2, \"ground\"", "numbered 0, 1");
    bad += !fails("\"point\": 3, \"position\"", "\"point\": 0, \"position\"", "used twice");
    bad += !fails("\"to\": {\"room\": 1, \"point\": 3}", "\"to\": {\"room\": 1, \"point\": 4}", "spawn its stage does not have");
    bad += !fails("\"stage:other\"", "\"stage:nowhere\"", "does not declare");
    bad += !fails("\"rooms\": [0, 1]", "\"rooms\": [1, 1]", "two different rooms");
    bad += !fails("\"rooms\": [0, 1]", "\"rooms\": [0, 5]", "two different rooms");
    bad += !fails("\"entrance\": {\"stage\": \"sea\", \"room\": 34, \"point\": 1},\n    \"rooms\": [\n      {\"room\": 0",
                  "\"entrance\": {\"stage\": \"stage:other\", \"room\": 0, \"point\": 0},\n    \"rooms\": [\n      {\"room\": 0", "entrance");
    bad += !fails("\"music_like\": \"Cave01\"", "\"music_like\": \"kinBOSS\"", "music_like");
    bad += !fails("\"assets/room.glb\"", "\"../room.glb\"", "ground");
    bad += !fails("\"assets/room.glb\"", "\"assets/room.png\"", "ground");
    bad += !fails("\"kind\": \"cave\"", "\"kind\": \"boss\"", "kind");
    bad += !fails("\"template\": \"Cave01\", \"kind\"", "\"template\": \"Cave0123\", \"kind\"", "template");
    bad += !fails("\"to\": {\"room\": 0, \"point\": 0}}]", "\"to\": {\"room\": 0, \"point\": 7}}]", "entry passage leads to a spawn");
    bad += !fails("\"size\": {\"x\": 200", "\"size\": {\"x\": 2", "size");
    bad += !fails("\"start\": \"fall\"", "\"start\": \"fly\"", "stand or fall");
    bad += !fails("\"id\": \"other\"", "\"id\": \"chamber\"", "duplicate stage id");
    bad += !fails("\"min\": {\"x\": 900", "\"min\": {\"x\": 1300", "min below max");
    bad += !fails("\"music_like\"", "\"musik\"", "unknown stage field");
    assert(!bad);

    // names: "Z" and six letters or digits, the same every time, never one that is taken
    std::set<std::string> taken;
    const std::string a = allocate_name("mod-a", "chamber", [&](const std::string& n) { return taken.count(n) > 0; });
    assert(allocated_name(a) && a == allocate_name("mod-a", "chamber", [](const std::string&) { return false; }));
    assert(a != allocate_name("mod-b", "chamber", [](const std::string&) { return false; }));
    taken.insert(a);
    const std::string b = allocate_name("mod-a", "chamber", [&](const std::string& n) { return taken.count(n) > 0; });
    assert(allocated_name(b) && b != a);
    assert(!allocated_name("Cave01") && !allocated_name("ZABCDEF") && allocated_name("Zab12cd"));
    assert(save_key("mod-a", "chamber") == save_key("mod-a", "chamber") && save_key("mod-a", "chamber") != save_key("mod-a", "other"));

    // the recipe: own places resolved to game names, ground paths to files
    std::map<std::string, std::string> names = {{"chamber", "Zaaaaaa"}, {"other", "Zbbbbbb"}};
    const Value r = recipe(s, "Zaaaaaa", names, [](const std::string& p) { return "/pkg/" + p; });
    assert(r.get("format").number == 1 && r.get("name").string() == "Zaaaaaa" && r.get("template").string() == "Cave01");
    assert(r.get("entrance").get("stage").string() == "sea" && r.get("entrance").get("room").number == 34);
    const auto& rooms = r.get("rooms").array;
    assert(rooms.size() == 2 && rooms[0].get("ground").string() == "/pkg/assets/room.glb" && rooms[1].get("kit").get("room").number == 1 &&
           rooms[1].get("kit").get("stage").string() == "Cave01");
    assert(rooms[0].get("exits").array[0].get("to").get("stage").string() == "sea");
    assert(rooms[0].get("exits").array[1].get("to").get("stage").string() == "Zaaaaaa");
    assert(rooms[1].get("exits").array[0].get("to").get("stage").string() == "Zbbbbbb");
    assert(rooms[0].get("materials").get("wall").string() == "wall_v" && rooms[0].get("material").string() == "rock");
    assert(rooms[0].get("exits").array[0].get("min").array[0].number == 900);
    assert(rooms[1].get("spawns").array[0].get("start").string() == "fall");
    assert(r.get("doors").array.size() == 1 && r.get("doors").array[0].get("rooms").array[1].number == 1);
    assert(r.get("entries").type == Value::Null);  // the entries are the runtime's, not the builder's

    // phase 2: kits from any stage, a kit room as it is, actors, locked doors, joins, warps, save groups
    {
        const char* dungeon = R"({"stages": [{
          "id": "keep", "template": "Cave01", "kind": "dungeon", "save_group": "keep",
          "entrance": {"stage": "sea", "room": 34, "point": 1},
          "rooms": [
            {"room": 0, "ground": "kit", "kit": {"stage": "M_NewD2", "room": 3},
             "spawns": [{"point": 0, "position": {"x": 0, "y": 0, "z": 0}}],
             "actors": [{"id": "key", "actor": "chest", "item": "small_key", "position": {"x": 1, "y": 0, "z": 2}, "angle": 90},
                        {"id": "boko", "actor": "bokoblin", "variant": "shield", "position": {"x": 3, "y": 0, "z": 0}},
                        {"id": "pot", "actor": "pot", "position": {"x": 5, "y": 0, "z": 0}}]},
            {"room": 1, "ground": "assets/b.glb", "spawns": [{"point": 1, "position": {"x": 0, "y": 0, "z": 0}}],
             "actors": [{"id": "big", "actor": "chest", "item": "boss_key", "position": {"x": 0, "y": 0, "z": 0}}]},
            {"room": 2, "ground": "assets/c.glb", "spawns": [{"point": 2, "position": {"x": 0, "y": 0, "z": 0}}]},
            {"room": 3, "ground": "assets/d.glb", "spawns": [{"point": 3, "position": {"x": 0, "y": 0, "z": 0}}]}
          ],
          "doors": [{"position": {"x": 0, "y": 0, "z": 0}, "rooms": [0, 1], "lock": "small_key"},
                    {"position": {"x": 0, "y": 0, "z": 0}, "rooms": [1, 2], "lock": "boss"},
                    {"position": {"x": 0, "y": 0, "z": 0}, "rooms": [0, 2], "lock": "shutter", "switch": 5}],
          "joins": [[2, 3]],
          "warps": [{"id": "home", "room": 3, "position": {"x": 0, "y": 0, "z": 0}, "size": {"x": 100, "y": 100, "z": 100}}]
        }]})";
        std::vector<Stage> d;
        assert(parse(parse_json(dungeon), d, why) && why.empty());
        const Stage& k = d[0];
        assert(k.keys && k.rooms[0].kit_ground && k.rooms[0].kit_stage == "M_NewD2" && k.rooms[0].kit == 3);
        assert(k.rooms[0].actors[0].flag == 0 && k.rooms[1].actors[0].flag == 1);  // chests numbered in order
        assert(k.doors[0].switch_no == 0x7F && k.doors[1].switch_no == 0x7E && k.doors[2].switch_no == 5);
        assert(k.warps[0].to == k.entrance && k.joins.size() == 1);
        const Value kr = recipe(k, "Zkkkkkk", {{"keep", "Zkkkkkk"}}, [](const std::string& p) { return p; });
        assert(kr.get("keys").boolean);
        const auto& r0 = kr.get("rooms").array[0];
        assert(r0.get("kit_ground").boolean && r0.get("ground").string().empty() && r0.get("kit").get("stage").string() == "M_NewD2");
        const auto& acts = r0.get("actors").array;
        assert(acts.size() == 3 && acts[0].get("tag").string() == "TRES" && acts[0].get("name").string() == "takara2");
        assert(uint32_t(acts[0].get("params").number) == 0xFF000000u && uint32_t(acts[0].get("angle_z").number) == 0x15FF);
        assert(acts[1].get("tag").string() == "ACTR" && acts[1].get("name").string() == "Bk" && uint32_t(acts[1].get("params").number) == 0xFFFFFF24u);
        assert(acts[2].get("name").string() == "kotubo");
        const auto& big = kr.get("rooms").array[1].get("actors").array[0];
        assert(big.get("name").string() == "takara4" && uint32_t(big.get("params").number) == (0xFF000000u | 3u << 20 | 1u << 7) && uint32_t(big.get("angle_z").number) == 0x4EFF);
        const auto& doors = kr.get("doors").array;
        assert(uint32_t(doors[0].get("params").number) == 0x0FFFF47Fu && doors[0].get("name").string() == "door10");
        assert(uint32_t(doors[1].get("params").number) == 0x0FFFF17Eu && doors[1].get("name").string() == "door20");
        assert(uint32_t(doors[2].get("params").number) == 0x0FFFF005u);
        assert(kr.get("joins").array.size() == 1);
        assert(inside(k.warps[0], 0, 50, 0) && !inside(k.warps[0], 0, 150, 0));

        auto refuse = [&](const std::string& from, const std::string& to, const std::string& part) {
            std::string text = dungeon;
            const size_t at = text.find(from);
            assert(at != std::string::npos);
            text.replace(at, from.size(), to);
            std::vector<Stage> out;
            std::string w;
            const bool ok = parse(parse_json(text), out, w);
            if (ok || w.find(part) == std::string::npos) {
                fprintf(stderr, "expected \"%s\" for %s -> %s, got %s \"%s\"\n", part.c_str(), from.c_str(), to.c_str(), ok ? "success" : "failure", w.c_str());
                return false;
            }
            return true;
        };
        int bad2 = 0;
        bad2 += !refuse("\"item\": \"small_key\"", "\"item\": \"map\"", "small-key door needs a small key");
        bad2 += !refuse("\"item\": \"boss_key\"", "\"item\": \"compass\"", "boss door needs a boss key");
        bad2 += !refuse("\"actor\": \"bokoblin\", \"variant\": \"shield\"", "\"actor\": \"bokoblin\", \"variant\": \"axe\"", "unknown variant");
        bad2 += !refuse("\"actor\": \"pot\"", "\"actor\": \"dragon\"", "not an actor a stage's room can hold");
        bad2 += !refuse("\"lock\": \"shutter\", \"switch\": 5", "\"lock\": \"shutter\"", "shutter opens on a switch");
        bad2 += !refuse("\"lock\": \"shutter\", \"switch\": 5", "\"lock\": \"shutter\", \"switch\": 100", "shutter opens on a switch");
        bad2 += !refuse("\"lock\": \"boss\"}", "\"lock\": \"golden\"}", "lock is none");
        bad2 += !refuse("\"joins\": [[2, 3]]", "\"joins\": []", "room 3 can't be reached");
        bad2 += !refuse("\"joins\": [[2, 3]]", "\"joins\": [[2, 2]]", "two different rooms");
        bad2 += !refuse("\"kit\": {\"stage\": \"M_NewD2\", \"room\": 3}", "\"kit\": {\"stage\": \"M_NewD2\"}", "kit is a room");
        bad2 += !refuse("\"id\": \"pot\", \"actor\": \"pot\"", "\"id\": \"pot\", \"actor\": \"pot\", \"item\": \"map\"", "only a chest holds an item");
        bad2 += !refuse("\"id\": \"pot\"", "\"id\": \"key\"", "duplicate actor id");
        assert(!bad2);
    }

    printf("mod_stages_test: ok\n");
    return 0;
}
