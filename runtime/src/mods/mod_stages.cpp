// SPDX-License-Identifier: MPL-2.0
// Mod stages in the running game: see mod_stages.h.
#include "mod_stages.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>

#include "../editor/stage_files.h"
#include "../platform/process.h"
#include "../savestate.h"
#include "guest_addr.h"
#include "runtime.h"
#include "story_store.h"
#include "story_world_placement.h"

extern "C" void f_020277AC_orig(Cpu* c);
extern "C" void f_025217F8_orig(Cpu* c);
extern "C" void f_02522198_orig(Cpu* c);
extern "C" void f_0273AA24_orig(Cpu* c);
extern "C" void f_025B9C9C_orig(Cpu* c);
extern "C" void f_025B9D24_orig(Cpu* c);
extern "C" void f_025B9174_orig(Cpu* c);

namespace mod_stages {
namespace {
namespace fs = std::filesystem;

struct Built {
    Info info;
    Stage stage;
    std::string dir;                          // the stage cache folder
    std::map<std::string, std::string> files;  // "<name>_Stage.szs", "<name>_Room<n>.szs" -> path
    uint16_t key = 0;                          // the mod save area key (save_key)
    uint16_t group_key = 0;                    // its save group's key (save_key of the group)
    unsigned group_index = 0;                  // the save group's number this session (its dungeon switches)
    std::map<std::string, std::string> names;  // the package's stage ids -> game names (its warps)
    std::string music_like;
    int music_id = -1;                         // spotNameToId's answer for the stage (-1: not looked up yet)
};
std::mutex g_mutex;
std::vector<Built> g_stages;  // appended at start-up only (before the game runs), read after
std::string g_game_dir;

// the game's play state (savestate.cpp kPlay): the start stage (name[8], point s16, room s8, layer s8) and the
// next stage (the same and enabled u8, wipe u8)
uint32_t play() { return GD(0x1046F0B0); }
uint32_t start_stage() { return play() + 0x5134; }
uint32_t next_stage() { return play() + 0x5140; }
const uint32_t kSavePtr = GD(0x101F84DC);  // the save area; dSv_player_return_place_c at +0x50 (name[8], room s8, point u8)
constexpr uint32_t kReturnPlace = 0x50;
// JAIZelBasic::spotNameToId's table (120 stage names; a stage's spot id is its index + 1) and the spot ids whose
// scene reads a dungeon's save bits (the boss rooms: mod_stages_manifest.h music_allowed)
const uint32_t kSpotNames = GD(0x1018E6D4);
constexpr uint32_t kSpots = 120;
constexpr uint32_t kStoryScenes[] = {0x02, 0x07, 0x18, 0x25, 0x2D};

std::string guest_text(uint32_t ea, size_t max) {
    if (ea < mem::kMem2Start || ea >= mem::kMem2End) return {};
    std::string s;
    for (size_t i = 0; i < max; i++) {
        const char ch = char(ld8(ea + uint32_t(i)));
        if (!ch) break;
        s.push_back(ch);
    }
    return s;
}
std::string current_stage() { return guest_text(start_stage(), 8); }

const Built* by_name(const std::string& name) {
    for (const auto& b : g_stages)
        if (b.info.name == name) return &b;
    return nullptr;
}

// the game's stages (room archives in Stage/ and the packs) and its Stage archive names, read once
const std::set<std::string>& game_stages() {
    static std::set<std::string> names;
    static bool read = false;
    if (!read) {
        read = true;
        for (const auto& [stage, rooms] : editor::game_stage_rooms(g_game_dir)) names.insert(stage);
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(fs::path(g_game_dir) / "content" / "Common" / "Stage", ec)) {
            const std::string f = e.path().filename().string();
            const size_t at = f.find('_');
            if (at != std::string::npos && at > 0) names.insert(f.substr(0, at));
        }
    }
    return names;
}
bool game_room(const std::string& stage, unsigned room) {
    for (const auto& [s, rooms] : editor::game_stage_rooms(g_game_dir))
        if (s == stage) return std::find(rooms.begin(), rooms.end(), int(room)) != rooms.end();
    return false;
}

std::string read_file(const fs::path& p, size_t limit) {
    std::ifstream f(p, std::ios::binary);
    std::string s(limit + 1, '\0');
    f.read(s.data(), std::streamsize(s.size()));
    s.resize(size_t(f.gcount()));
    return s.size() > limit ? std::string() : s;
}
bool write_file(const fs::path& p, const std::string& body) {
    const fs::path temp = p.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(body.data(), std::streamsize(body.size()));
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(temp, p, ec);
    return !ec;
}

// one stage: its recipe, the builder, the result checked and registered; false with a reason
bool build(const std::string& mod, const Stage& s, const std::string& name, const std::map<std::string, std::string>& names,
           const std::string& package_dir, const std::string& data_dir, const std::vector<std::string>& python, const std::string& tool,
           std::string& why) {
    // what the player's game files must have: the template's archives, every kit room, the entrance and the
    // game rooms the exits and entries name (a missing archive stops the game)
    if (!game_stages().count(s.template_stage)) return why = "the game has no stage " + s.template_stage, false;
    for (const auto& r : s.rooms)
        if (!game_room(r.kit_stage.empty() ? s.template_stage : r.kit_stage, r.kit))
            return why = "the kit room " + (r.kit_stage.empty() ? s.template_stage : r.kit_stage) + " room " + std::to_string(r.kit) + " is not in the game", false;
    if (!game_room(s.entrance.stage, s.entrance.room))
        return why = "the entrance " + s.entrance.stage + " room " + std::to_string(s.entrance.room) + " is not in the game", false;
    for (const auto& r : s.rooms)
        for (const auto& e : r.exits)
            if (!e.to.own && !game_room(e.to.stage, e.to.room))
                return why = "exit " + e.id + " leads to " + e.to.stage + " room " + std::to_string(e.to.room) + ", which is not in the game", false;
    for (const auto& w : s.warps)
        if (!w.to.own && !game_room(w.to.stage, w.to.room))
            return why = "warp " + w.id + " leads to " + w.to.stage + " room " + std::to_string(w.to.room) + ", which is not in the game", false;
    for (const auto& e : s.entries)
        if (!game_room(e.stage, e.room)) return why = "entry " + e.id + " is in " + e.stage + " room " + std::to_string(e.room) + ", which is not in the game", false;
    if (python.empty() || tool.empty() || data_dir.empty()) return why = "the stage builder is unavailable", false;
    const fs::path dir = fs::path(data_dir).parent_path() / ".mod-stages" / name;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec || fs::is_symlink(dir) || fs::is_symlink(dir.parent_path())) return why = "the stage cache folder is unavailable", false;
    std::string ground_error;
    auto ground = [&](const std::string& path) {
        fs::path p = package_dir;
        for (const auto& part : fs::path(path)) {
            p /= part;
            if (fs::is_symlink(p)) ground_error = "the ground model's path uses a symlink";
        }
        if (!fs::is_regular_file(p)) ground_error = "the ground model " + path + " is missing";
        return p.string();
    };
    const Value recipe_value = recipe(s, name, names, ground);
    if (!ground_error.empty()) return why = ground_error, false;
    const fs::path recipe_path = dir / "recipe.json";
    if (!write_file(recipe_path, mods::json::dump(recipe_value))) return why = "the stage recipe could not be written", false;
    auto command = python;
    command.insert(command.end(), {tool, "--game", g_game_dir, "--recipe", recipe_path.string(), "--out", dir.string()});
    const auto process = host::run_process(command);
    const auto end = process.output.find_last_not_of("\r\n");
    Value result;
    if (end != std::string::npos) {
        const auto line = process.output.rfind('\n', end);
        const auto begin = line == std::string::npos ? 0 : line + 1;
        try {
            result = mods::json::parse(process.output.substr(begin, end - begin + 1));
        } catch (...) {
        }
    }
    if (!process.error.empty() || result.get("ok").type != Value::Bool || !result.get("ok").boolean || process.code)
        return why = "the stage builder failed: " + result.get("error").string(process.error.empty() ? "no result" : process.error), false;
    Built b;
    b.info.mod = mod;
    b.info.id = s.id;
    b.info.name = name;
    b.info.entrance = s.entrance;
    b.stage = s;
    b.dir = dir.string();
    b.key = save_key(mod, s.id);
    b.group_key = save_key(mod, "group:" + s.save_group);
    b.names = names;
    {
        std::lock_guard lock(g_mutex);
        std::set<uint16_t> seen;
        for (const auto& o : g_stages) seen.insert(o.group_key);
        b.group_index = unsigned(seen.size());
        for (const auto& o : g_stages)
            if (o.group_key == b.group_key) b.group_index = o.group_index;
    }
    b.music_like = s.music_like;
    for (const auto& [file, digest] : result.get("files").object) {
        const bool ours = file == name + "_Stage.szs" || (file.rfind(name + "_Room", 0) == 0 && file.size() > 4 && file.substr(file.size() - 4) == ".szs");
        if (!ours || !fs::is_regular_file(dir / file) || fs::is_symlink(dir / file)) return why = "the stage builder returned an unexpected file " + file, false;
        b.files[file] = (dir / file).string();
        register_file(file, b.files[file]);
    }
    if (!b.files.count(name + "_Stage.szs")) return why = "the stage builder wrote no stage archive", false;
    for (const auto& r : s.rooms) {
        if (!b.files.count(name + "_Room" + std::to_string(r.room) + ".szs")) return why = "the stage builder wrote no archive of room " + std::to_string(r.room), false;
        b.info.rooms.push_back(r.room);
        for (const auto& sp : r.spawns) b.info.spawns.push_back({r.room, sp.point, sp.position, sp.angle});
    }
    LOG("[mod-stages] %s: stage %s is %s (%zu rooms, %s)", mod.c_str(), s.id.c_str(), name.c_str(), s.rooms.size(),
        result.get("cached").boolean ? "from the stage cache" : "built from the game files");
    for (const auto& room : result.get("rooms").array)
        LOG("[mod-stages] %s room %d: %d visible and %d collision triangles", name.c_str(), int(room.get("room").number),
            int(room.get("visible_triangles").number), int(room.get("collision_triangles").number));
    std::lock_guard lock(g_mutex);
    g_stages.push_back(std::move(b));
    return true;
}

// the mod save area records of the stage a Quest Log was saved in (story_mail_book.h)
void clear_resume(story_mail::Book& book) {
    for (unsigned r = 0; r < story_block::kAreaRecords; ++r) {
        const uint8_t kind = book.record_kind(r);
        if (kind == story_mail::kRecordStageResume || kind == story_mail::kRecordStageResumePoint) book.erase(kind, book.record_key(r));
    }
}

void write_next_stage(const std::string& name, unsigned room, unsigned point) {
    char text[8] = {};
    memcpy(text, name.data(), std::min<size_t>(name.size(), 7));
    for (int i = 0; i < 8; i++) st8(next_stage() + i, uint8_t(text[i]));
    st16(next_stage() + 8, uint16_t(point));
    st8(next_stage() + 10, uint8_t(room));
}
}  // namespace

void declare(const std::string& mod, const Value& manifest, const std::string& package_dir, const std::string& data_dir,
             const std::vector<std::string>& python, const std::string& tool, const std::string& game_dir) {
    std::vector<Stage> list;
    std::string why;
    if (!parse(manifest, list, why)) exception_report::raise("Stages " + mod + ": " + why);
    if (list.empty()) return;
    g_game_dir = game_dir;
    static bool sources = false;
    if (!sources) {  // the editor's spawn points and the placement index read the stage cache too
        sources = true;
        editor::set_stage_archive_source(cached_file);
        story_world_placement::set_stage_archive_source(cached_file);
    }
    std::map<std::string, std::string> names;
    for (const auto& s : list) {
        const std::string name = allocate_name(mod, s.id, [&](const std::string& n) {
            if (game_stages().count(n)) return true;
            for (const auto& [id, other] : names)
                if (other == n) return true;
            std::lock_guard lock(g_mutex);
            return by_name(n) != nullptr;
        });
        if (name.empty()) exception_report::raise("Stages " + mod + ": no free stage name");
        names[s.id] = name;
    }
    for (const auto& s : list) {
        std::string failure;
        try {
            if (!build(mod, s, names[s.id], names, package_dir, data_dir, python, tool, failure) && failure.empty()) failure = "unknown";
        } catch (const std::exception& e) {
            failure = e.what();
        }
        if (!failure.empty()) LOG("[mod-stages] %s: stage %s is left out: %s", mod.c_str(), s.id.c_str(), failure.c_str());
    }
}

bool declared() {
    std::lock_guard lock(g_mutex);
    return !g_stages.empty();
}

std::vector<Info> stages() {
    std::lock_guard lock(g_mutex);
    std::vector<Info> out;
    for (const auto& b : g_stages) out.push_back(b.info);
    return out;
}

bool find(const std::string& name, Info& out) {
    std::lock_guard lock(g_mutex);
    const Built* b = by_name(name);
    if (!b) return false;
    out = b->info;
    return true;
}

std::string resolve(const std::string& reference) {
    std::lock_guard lock(g_mutex);
    if (reference.rfind("stage:", 0) == 0) {
        const std::string rest = reference.substr(6);
        const size_t slash = rest.find('/');
        if (slash == std::string::npos) return {};
        for (const auto& b : g_stages)
            if (b.info.mod == rest.substr(0, slash) && b.info.id == rest.substr(slash + 1)) return b.info.name;
        return {};
    }
    return by_name(reference) ? reference : std::string();
}

bool missing(const std::string& stage) {
    if (!allocated_name(stage)) return false;
    {
        std::lock_guard lock(g_mutex);
        if (by_name(stage)) return false;
    }
    std::error_code ec;
    return g_game_dir.empty() || !fs::is_regular_file(fs::path(g_game_dir) / "content" / "Common" / "Stage" / (stage + "_Stage.szs"), ec);
}

void logic(Cpu*) {
    if (g_stages.empty()) return;  // appended only before the game runs
    static std::string last_room;
    static std::set<std::string> armed;  // entries Link was outside of since he came into the room
    const auto p = ss::link_place();
    if (!p.valid) return;
    const std::string room_key = p.stage + "/" + std::to_string(p.room);
    if (room_key != last_room) {
        last_room = room_key;
        armed.clear();  // arriving inside a box (back from the stage, say) takes nobody anywhere
    }
    for (const auto& b : g_stages)
        for (const auto& e : b.stage.entries) {
            if (e.stage != p.stage || int(e.room) != p.room) continue;
            const std::string key = b.info.name + "/" + e.id;
            if (!inside(e, p.pos[0], p.pos[1], p.pos[2])) {
                armed.insert(key);
                continue;
            }
            if (!armed.count(key) || !ss::control_problem().empty()) continue;
            armed.erase(key);
            write_next_stage(b.info.name, e.to_room, e.to_point);
            st8(next_stage() + 11, 0xFF);  // layer: the game's choice
            st8(next_stage() + 13, 0);     // wipe: fade
            st8(next_stage() + 12, 1);     // enabled
            LOG("[mod-stages] entry %s/%s: Link goes into %s (%s room %u point %u)", b.info.mod.c_str(), e.id.c_str(), b.info.id.c_str(),
                b.info.name.c_str(), e.to_room, e.to_point);
            return;
        }
    // the return warps of the stage Link is in
    for (const auto& b : g_stages) {
        if (b.info.name != p.stage) continue;
        for (const auto& w : b.stage.warps) {
            if (int(w.room) != p.room) continue;
            const std::string key = b.info.name + "/warp/" + w.id;
            if (!inside(w, p.pos[0], p.pos[1], p.pos[2])) {
                armed.insert(key);
                continue;
            }
            if (!armed.count(key) || !ss::control_problem().empty()) continue;
            armed.erase(key);
            const std::string to = !w.to.own ? w.to.stage : w.to.stage.empty() ? b.info.name : b.names.at(w.to.stage);
            write_next_stage(to, w.to.room, w.to.point);
            st8(next_stage() + 11, 0xFF);
            st8(next_stage() + 13, 9);  // wipe: the warp light (the game's own cave exits and its turn restart use it)
            st8(next_stage() + 12, 1);
            LOG("[mod-stages] warp %s/%s: Link goes to %s room %u point %u", b.info.id.c_str(), w.id.c_str(), to.c_str(), w.to.room, w.to.point);
            return;
        }
    }
}

}  // namespace mod_stages

using namespace mod_stages;

// JAIZelBasic::spotNameToId(self, name) -> the stage's spot id (1..120), 0 for an unknown stage. A mod stage
// answers as its music_like stage, unless that stage's scene reads a dungeon's save bits.
extern "C" void hook_020277AC(Cpu* c) {
    const uint32_t name_ea = c->r[4];
    f_020277AC_orig(c);
    if (g_stages.empty() || c->r[3] != 0) return;
    const std::string name = guest_text(name_ea, 8);
    for (auto& b : g_stages) {
        if (b.info.name != name) continue;
        if (b.music_id < 0) {
            b.music_id = 0;
            for (uint32_t i = 0; i < kSpots && !b.music_like.empty(); i++)
                if (guest_text(ld32(kSpotNames + i * 4), 16) == b.music_like) b.music_id = int(i + 1);
            if (std::find(std::begin(kStoryScenes), std::end(kStoryScenes), uint32_t(b.music_id)) != std::end(kStoryScenes)) {
                LOG("[mod-stages] %s: music_like %s follows a dungeon's state; the stage has no music", b.info.id.c_str(), b.music_like.c_str());
                b.music_id = 0;
            } else if (!b.music_like.empty() && !b.music_id) {
                LOG("[mod-stages] %s: music_like %s has no music of its own", b.info.id.c_str(), b.music_like.c_str());
            }
            LOG("[mod-stages] %s (%s): music and scene sounds of %s (spot %d)", b.info.id.c_str(), name.c_str(),
                b.music_like.empty() ? "nothing" : b.music_like.c_str(), b.music_id);
        }
        c->r[3] = uint32_t(b.music_id);
        return;
    }
}

// dComIfGs_setGameStartStage: where the save file continues. In a mod stage: the stage's entrance (a place of
// the game, so the file loads without the package), and the mod save area keeps where in the stage the save
// was made. Anywhere else: the game's own choice, and no record of a mod stage.
extern "C" void hook_025217F8(Cpu* c) {
    const std::string stage = current_stage();
    const Built* b = g_stages.empty() ? nullptr : by_name(stage);
    if (!b) {
        f_025217F8_orig(c);
        if (story_store::ready()) {
            auto book = story_store::book();
            clear_resume(book);
        }
        return;
    }
    const uint32_t save = ld32(kSavePtr);
    if (save) {
        char text[8] = {};
        memcpy(text, b->info.entrance.stage.data(), std::min<size_t>(b->info.entrance.stage.size(), 7));
        for (int i = 0; i < 8; i++) st8(save + kReturnPlace + i, uint8_t(text[i]));
        st8(save + kReturnPlace + 8, uint8_t(b->info.entrance.room));
        st8(save + kReturnPlace + 9, uint8_t(b->info.entrance.point));
    }
    // where Link came into the stage: a spawn of it, else the stage's first
    unsigned room = ld8(start_stage() + 10) & 0x3F, point = ld16(start_stage() + 8);
    const bool spawn = std::any_of(b->info.spawns.begin(), b->info.spawns.end(), [&](const Info::SpawnPoint& s) { return s.room == room && s.point == point; });
    if (!spawn) room = b->info.spawns.front().room, point = b->info.spawns.front().point;
    if (story_store::ready()) {
        auto book = story_store::book();
        clear_resume(book);
        const bool kept = book.set(story_mail::kRecordStageResume, b->key, uint8_t(room)) && book.set(story_mail::kRecordStageResumePoint, b->key, uint8_t(point));
        LOG("[mod-stages] return place in %s (%s): the save file continues at %s room %u point %u; the mod save area %s room %u point %u", b->info.id.c_str(),
            stage.c_str(), b->info.entrance.stage.c_str(), b->info.entrance.room, b->info.entrance.point, kept ? "continues in the stage at" : "is full; not kept:", room,
            point);
    } else {
        LOG("[mod-stages] return place in %s (%s): the save file continues at %s room %u point %u", b->info.id.c_str(), stage.c_str(), b->info.entrance.stage.c_str(),
            b->info.entrance.room, b->info.entrance.point);
    }
}

// dComIfGs_gameStart: a loaded Quest Log starts at the save's return place. When the mod save area says it was
// saved in a mod stage that is installed, it continues there.
extern "C" void hook_02522198(Cpu* c) {
    f_02522198_orig(c);
    if (!story_store::ready()) return;
    auto book = story_store::book();
    for (unsigned r = 0; r < story_block::kAreaRecords; ++r) {
        if (book.record_kind(r) != story_mail::kRecordStageResume) continue;
        const uint16_t key = book.record_key(r);
        const unsigned room = book.record_value(r), point = book.get(story_mail::kRecordStageResumePoint, key);
        for (const auto& b : g_stages) {
            if (b.key != key) continue;
            const bool spawn = std::any_of(b.info.spawns.begin(), b.info.spawns.end(), [&](const Info::SpawnPoint& s) { return s.room == room && s.point == point; });
            if (!spawn) break;
            write_next_stage(b.info.name, room, point);
            static std::string logged;  // the game asks again on following frames while the stage loads
            const std::string line = b.info.name + "/" + std::to_string(room) + "/" + std::to_string(point);
            if (logged == line) return;
            logged = line;
            LOG("[mod-stages] the Quest Log was saved in %s/%s: it continues there (%s room %u point %u)", b.info.mod.c_str(), b.info.id.c_str(), b.info.name.c_str(),
                room, point);
            return;
        }
        LOG("[mod-stages] the Quest Log was saved in a mod stage that is not installed: it continues at the stage's entrance");
        return;
    }
}

// JUTAssertion (file, line, message): logged with the stage while a mod stage is loaded
extern "C" void hook_0273AA24(Cpu* c) {
    if (!g_stages.empty()) {
        const std::string stage = current_stage();
        if (by_name(stage))
            LOG("[mod-stages] game assertion in %s: %s:%u %s", stage.c_str(), guest_text(c->r[3], 96).c_str(), c->r[4], guest_text(c->r[5], 96).c_str());
    }
    f_0273AA24_orig(c);
}

// ---- the stage record ---------------------------------------------------------------------------------------
// A stage's save record (dSv_memBit_c, 0x24 bytes: chests, switches, items, visited rooms, keys, dungeon items)
// lives in dSv_info_c's working copy (+0x778) while the stage is loaded; the game copies it from and back to the
// save's table of 16 (getSave, putSave). A mod stage's is its save group's own, kept in the mod save area
// (story_store; without one, for this session only), and the save file's slot 15 is never written.
namespace {
constexpr uint32_t kWorkingRecord = 0x778, kRecordBytes = 0x24;
std::map<uint16_t, std::array<uint8_t, kRecordBytes>> g_session_records;  // without a mod save area
const Built* g_loaded_record = nullptr;  // the mod stage whose record is in the working copy

// the stage being set up: the start stage once the game has taken the next stage over, else the next stage
const Built* loading_stage() {
    if (const Built* b = by_name(current_stage())) return b;
    return by_name(guest_text(next_stage(), 8));
}
unsigned group_slot(story_mail::Book& book, uint16_t group, bool make) {
    if (const uint8_t slot = book.get(story_mail::kRecordStageGroup, group)) return slot;
    if (!make) return 0;
    std::set<uint8_t> used;
    for (unsigned r = 0; r < story_block::kAreaRecords; ++r)
        if (book.record_kind(r) == story_mail::kRecordStageGroup) used.insert(book.record_value(r));
    for (unsigned slot = 1; slot <= 250; ++slot)
        if (!used.count(uint8_t(slot))) return book.set(story_mail::kRecordStageGroup, group, uint8_t(slot)) ? slot : 0;
    return 0;
}
std::array<uint8_t, kRecordBytes> read_record(const Built& b) {
    std::array<uint8_t, kRecordBytes> out{};
    if (!story_store::ready()) {
        auto it = g_session_records.find(b.group_key);
        if (it != g_session_records.end()) out = it->second;
        return out;
    }
    auto book = story_store::book();
    const unsigned slot = group_slot(book, b.group_key, false);
    if (slot)
        for (unsigned i = 0; i < kRecordBytes; ++i) out[i] = book.get(story_mail::kRecordStageMemory, uint16_t(slot << 8 | i));
    return out;
}
bool write_record(const Built& b, const std::array<uint8_t, kRecordBytes>& bytes) {
    if (!story_store::ready()) {
        g_session_records[b.group_key] = bytes;
        return true;
    }
    auto book = story_store::book();
    const bool any = std::any_of(bytes.begin(), bytes.end(), [](uint8_t v) { return v != 0; });
    const unsigned slot = group_slot(book, b.group_key, any);
    if (!slot) return !any;
    bool ok = true;
    for (unsigned i = 0; i < kRecordBytes; ++i) {
        const uint16_t key = uint16_t(slot << 8 | i);
        if (bytes[i]) ok &= book.set(story_mail::kRecordStageMemory, key, bytes[i]);
        else book.erase(story_mail::kRecordStageMemory, key);
    }
    return ok;
}
}  // namespace

// dSv_info_c::getSave(info, stage number): the stage's record into the working copy
extern "C" void hook_025B9C9C(Cpu* c) {
    const uint32_t info = c->r[3], number = c->r[4];
    f_025B9C9C_orig(c);
    if (g_stages.empty() || number != 15) return;
    const Built* b = loading_stage();
    g_loaded_record = b;
    if (!b) return;
    const auto bytes = read_record(*b);
    for (unsigned i = 0; i < kRecordBytes; ++i) st8(info + kWorkingRecord + i, bytes[i]);
    LOG("[mod-stages] %s: record of save group %s loaded (chests %02X%02X%02X%02X, keys %u, dungeon items %02X)%s", b->info.id.c_str(), b->stage.save_group.c_str(),
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[0x20], bytes[0x21], story_store::ready() ? "" : " (no mod save area: kept for this session only)");
}

// dSv_info_c::putSave(info, stage number): the working copy back into the stage's record
extern "C" void hook_025B9D24(Cpu* c) {
    const uint32_t info = c->r[3], number = c->r[4];
    const Built* b = g_stages.empty() || number != 15 ? nullptr : by_name(current_stage());
    if (!b || b != g_loaded_record) {
        if (b && g_loaded_record && b->group_key == g_loaded_record->group_key) b = g_loaded_record;
        else if (b) {
            // the working copy isn't this stage's (a save state loaded elsewhere, say): nothing to keep, and the save
            // file's slot 15 stays as it is
            LOG("[mod-stages] %s: the working record is not the stage's; not kept", b->info.id.c_str());
            return;
        } else {
            f_025B9D24_orig(c);
            return;
        }
    }
    std::array<uint8_t, kRecordBytes> bytes{};
    for (unsigned i = 0; i < kRecordBytes; ++i) bytes[i] = ld8(info + kWorkingRecord + i);
    if (!write_record(*b, bytes)) LOG("[mod-stages] %s: the mod save area is full; the stage's record is not kept", b->info.id.c_str());
    else
        LOG("[mod-stages] %s: record of save group %s kept (chests %02X%02X%02X%02X, keys %u, dungeon items %02X)", b->info.id.c_str(), b->stage.save_group.c_str(),
            bytes[0], bytes[1], bytes[2], bytes[3], bytes[0x20], bytes[0x21]);
}

// dSv_danBit_c::init(dan, stage number): the dungeon switches (0x80..0xBF) start fresh when the stage number
// changes; each save group of mod stages counts as a stage number of its own (0x20 and up)
extern "C" void hook_025B9174(Cpu* c) {
    if (!g_stages.empty() && c->r[4] == 15)
        if (const Built* b = loading_stage()) c->r[4] = 0x20 + b->group_index;
    f_025B9174_orig(c);
}
