// SPDX-License-Identifier: MPL-2.0
// Game pieces (story_game_pieces_catalogue.h, story_world_manifest.h, mod_actor_budget.h): the port's
// recipes, the recipe rules, what a manifest may say about a piece (catalogued ids only; id, stage,
// room, position and a turn only; the catalogue version gate), what a piece becomes (a static world
// object with the piece's model and collision), and the 8 MiB room budget for buildings, in manifests,
// across packages and at run time.
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "mods/mod_actor_budget.h"
#include "mods/story_world_manifest.h"

using namespace story_notebook;
using mods::json::Value;

static Value manifest(const std::string& actors, const std::string& requires_ = R"({"game_objects": 2})") {
    return mods::json::parse(R"({"requires": )" + requires_ + R"(, "actors": [)" + actors + "]}");
}
static std::string house(const std::string& id, const std::string& piece = "outset-house-small", const std::string& extra = "", int room = 12) {
    return R"({"id": ")" + id + R"(", "actor": "piece:)" + piece + R"(", "stage": "sea", "room": )" + std::to_string(room) +
           R"(, "position": {"x": 100, "y": 20, "z": -300})" + extra + "}";
}
static bool parses(const Value& m, std::string& why, std::vector<ManifestActor>* got = nullptr) {
    std::vector<ManifestActor> out;
    const bool ok = parse_actors(m, out, why);
    if (got) *got = out;
    return ok;
}

int main() {
    std::string why;
    // ---- the port's recipes
    const auto& c = story_game_pieces::catalogue();
    assert(c.version >= 2);
    const char* outset[] = {"outset-house-ladder", "outset-house-lookout", "outset-house-terrace", "outset-house-small", "outset-lookout-tower", "outset-well-shelter"};
    for (const char* id : outset) {
        const auto* p = c.find(id);
        assert(p && p->set == "outset" && p->category == "Buildings" && !p->label.empty() && !p->description.empty());
        assert(!p->parts.empty() && p->parts[0].model == 0 && p->parts[0].light == 1 && p->parts[0].nodes == std::vector<std::string>{"ie"});
        assert(p->instance_bytes > 0 && p->collision_triangles > 0 && !p->collision_groups.empty());
    }
    for (const char* id : {"outset-house-ladder", "outset-house-lookout", "outset-house-terrace", "outset-house-small"}) {
        const auto* p = c.find(id);
        assert(p->parts.size() == 2 && p->parts[1].model == 3 && p->parts[1].light == 4);  // the window glow: BG3
    }
    const auto* set = c.set("outset");
    assert(set && set->archive == "sea_Room44.szs" && set->room == 44 && set->pieces.size() == 6);
    assert(set->archive_bytes > 1000000 && set->archive_bytes < story_game_pieces::kRoomBudgetBytes);
    for (const auto& p : c.pieces) assert(story_game_pieces::neutral_id(p.id) && c.set(p.set));
    assert(story_game_pieces::reference("piece:outset-house-small") == "outset-house-small" && story_game_pieces::reference("game:palm").empty());
    assert(story_game_pieces::model_key("x") == "piece:x");
    assert(story_game_pieces::light_type("bg0") == 1 && story_game_pieces::light_type("bg3") == 4 && !story_game_pieces::light_type("bg4") &&
           !story_game_pieces::light_type("actor"));

    // ---- recipe rules: all or nothing per set
    const std::string good = R"({"game_pieces_format": 1, "set": "test", "label": "Test", "source": {"archive": "sea_Room12.szs", "room": 12},
        "memory": {"archive_bytes": 3000000},
        "pieces": [{"id": "hut", "since": 3, "category": "Buildings", "label": "Hut", "description": "a hut",
          "volume": [-100, 0, -100, 100, 200, 100], "parts": [{"model": 0, "nodes": ["ie"], "light": "bg0"}],
          "collision": {"groups": ["hut"]}, "expect": {"triangles": [12], "collision_triangles": 4}, "memory": {"instance_bytes": 40000}}]})";
    {
        story_game_pieces::Catalogue t;
        assert(t.add_set(good, why) && t.version == 3 && t.find("hut") && t.set("test")->recipe == good);
        assert(!t.add_set(good, why) && why.find("duplicate") != std::string::npos && t.pieces.size() == 1);
        auto bad = [&](const std::string& from, const std::string& to) {
            std::string text = good;
            const auto at = text.find(from);
            assert(at != std::string::npos);
            text.replace(at, from.size(), to);
            story_game_pieces::Catalogue u;
            const bool ok = u.add_set(text, why);
            assert(!ok && u.pieces.empty() && u.sets.empty());
        };
        bad(R"("game_pieces_format": 1)", R"("game_pieces_format": 2)");
        bad(R"("set": "test")", R"("set": "Test")");
        bad(R"("archive": "sea_Room12.szs")", R"("archive": "sea_Room13.szs")");  // not the room's archive
        bad(R"("archive": "sea_Room12.szs")", R"("archive": "../sea_Room12.szs")");
        bad(R"("room": 12})", R"("room": 50})");
        bad(R"("archive_bytes": 3000000)", R"("archive_bytes": 9000000)");  // over the room budget alone
        bad(R"("id": "hut")", R"("id": "Hut")");
        bad(R"("volume": [-100, 0, -100, 100, 200, 100])", R"("volume": [100, 0, -100, -100, 200, 100])");
        bad(R"("volume": [-100, 0, -100, 100, 200, 100])", R"("volume": [-100, 0, -100, 100, 200])");
        bad(R"("model": 0)", R"("model": 4)");
        bad(R"("light": "bg0")", R"("light": "actor")");
        bad(R"("nodes": ["ie"])", R"("nodes": ["../ie"])");
        bad(R"("triangles": [12])", R"("triangles": [12, 4])");  // one count per part
        bad(R"("groups": ["hut"])", R"("groups": []})");           // groups and their count go together
        bad(R"("instance_bytes": 40000)", R"("instance_bytes": 0)");
    }

    // ---- manifests
    {
        std::vector<ManifestActor> got;
        assert(parses(manifest(house("a")), why, &got));
        const auto& a = got.at(0);
        assert(a.game_piece() && a.piece == "outset-house-small" && a.actor == "piece:outset-house-small" && !a.game_object());
        assert(a.kind == "mod" && a.model == "piece:outset-house-small" && a.collision_mesh == a.model && a.behaviour == "static" && a.world_object());
        assert(a.scale.x == 1 && a.position.x == 100 && a.room == 12);
        assert(parses(manifest(house("b", "outset-lookout-tower", R"(, "rotation": {"x": 0, "y": 90, "z": 0})")), why, &got) && got[0].rotation.y == 90);
    }
    assert(!parses(manifest(house("a", "outset-house-big")), why) && why.find("not in the game pieces catalogue") != std::string::npos);
    assert(!parses(manifest(house("a"), "null"), why) && why.find("requires") != std::string::npos);
    assert(!parses(manifest(house("a"), R"({"game_objects": 1})"), why) && why.find("version 2") != std::string::npos);
    assert(!parses(manifest(house("a"), R"({"game_objects": 99})"), why) && why.find("update the port") != std::string::npos);
    for (const char* extra : {R"(, "scale": {"x": 2, "y": 2, "z": 2})", R"(, "lines": "hello")", R"(, "flag": "f")", R"(, "params": 1)",
                              R"(, "collision": {"radius": 1})", R"(, "model": "m")", R"(, "variant": "v")", R"(, "then": [])"})
        assert(!parses(manifest(house("a", "outset-house-small", extra)), why) && why.find("a game piece takes only") != std::string::npos);
    assert(!parses(manifest(house("a", "outset-house-small", R"(, "rotation": {"x": 10, "y": 0, "z": 0})")), why) && why.find("vertical axis") != std::string::npos);
    assert(!parses(manifest(house("a") + "," + house("a")), why) && why.find("duplicate") != std::string::npos);
    // the room budget: one set's archive once and every placement (the Outset set leaves room for its own pieces)
    {
        std::string many;
        for (int i = 0; i < 12; ++i) many += (i ? "," : "") + house("h" + std::to_string(i), outset[i % 6], "", 12 + i / 6);
        assert(parses(manifest(many), why));
    }
    {
        // a second set in the same room goes over 8 MiB
        story_game_pieces::Catalogue t = c;
        std::string other = good;
        other.replace(other.find("3000000"), 7, "3500000");
        assert(t.add_set(other, why));
        std::vector<ManifestActor> list(2);
        list[0].piece = "outset-house-small", list[0].stage = "sea", list[0].room = 12;
        list[1].piece = "hut", list[1].stage = "sea", list[1].room = 12;
        assert(!world_manifest::game_piece_budget(list, why, t) && why.find("memory budget for buildings") != std::string::npos);
        list[1].room = 13;
        assert(world_manifest::game_piece_budget(list, why, t));
    }
    // across packages: each package within budget, together over it
    {
        story_game_pieces::Catalogue t = c;
        // (the port's sets fit; the cross-package check reuses game_piece_budget over every enabled package's pieces)
        std::vector<std::pair<std::string, Value>> packages{{"one", manifest(house("a"))}, {"two", manifest(house("b", "outset-well-shelter"))}};
        assert(world_manifest::validate_world_packages(packages, why));
    }

    // ---- the run-time budget (mod_actor_budget.h): a piece set may take up to 8 MiB, a model 2 MiB
    {
        using namespace mod_actor::budget;
        Usage u;
        std::vector<Record> r{{"m", "gp-set", 12, 5400000, 300000, 30000, 4400, true, true, true}};
        assert(check(r, 12, u, why) && u.pieces > 5700000);
        r[0].piece = false;
        assert(!check(r, 12, u, why) && why.find("2 MiB") != std::string::npos);
        r[0].piece = true;
        r.push_back({"m", "gp-other", 12, 2600000, 200000, 30000, 4400, true, true, true});
        assert(!check(r, 12, u, why) && why.find("game pieces in this room exceed 8 MiB") != std::string::npos);
        r[1].room = 13, r[1].active = false, r[1].loaded = false;
        assert(check(r, 12, u, why));
    }
    std::puts("story_game_pieces_test: ok");
    return 0;
}
