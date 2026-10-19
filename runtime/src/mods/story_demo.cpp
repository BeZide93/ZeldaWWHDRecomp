// SPDX-License-Identifier: MPL-2.0
#include "story_demo.h"

#include "demo_event_image.h"
#include "guest_addr.h"
#include "runtime.h"
#include "story_mail.h"
#include "story_mail_book.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace story_demo {
namespace {
// game functions (HD US addresses; see docs/decomp-notes.md "Events, demos")
const uint32_t kPlay = GC(0x025200D4), kCamera = GC(0x024F8044), kStartCamera = GC(0x02514F38), kStopCamera = GC(0x02514F2C);
const uint32_t kClearBase = GC(0x0254273C), kSetData = GC(0x025443B4), kOrderEvent = GC(0x02544534);
const uint32_t kResLoad = GC(0x02520460), kResDelete = GC(0x025204C8);
const uint32_t kDemoRemove = GC(0x02528AC4), kSearchFromName = GC(0x025D9F38), kGetArchive = GC(0x026065A8);
const uint32_t kLookupId = GC(0x026067F4), kLookupName = GC(0x02606900);
// game data
const uint32_t kDemoMode = GD(0x101D6010), kDemoFile = GD(0x101D6004), kDemoControl = GD(0x101D5FE8), kResControl = GD(0x101F4F28);
const uint32_t kDemoArcName = GD(0x1047E6B8);  // dStage_roomControl_c::mDemoArcName, char[12]; mProcID follows at +0xC
const uint32_t kLiteralVtable = GD(0x1004BDC8);  // the literal-string vtable set_demo_data uses for its archive names
const uint32_t kIdTable = GD(0x100DE8F4), kIdTableCount = GD(0x100C45C0);  // archive CRC32 -> resource id order
constexpr uint32_t kManager = 0x52C4, kPlayer = 0x5B34, kFlagBytes = 1280;
// guest record (arena)
constexpr uint32_t kMarker = 0x444D4F31;  // 'DMO1'
constexpr uint32_t kDraining = 9;  // released; archives are given back after a few frames
enum : uint32_t {
    Marker = 0x000, Phase = 0x004, ManagerAt = 0x008, RetailWords = 0x00C, RetailCameraPlay = 0x02C, RetailCurrent = 0x030, Ticks = 0x034,
    SavedArc = 0x040, VirtualCount = 0x050, MessageIndex = 0x054, StbBytes = 0x058, CameraStarted = 0x05C, MessageCount = 0x060,
    Virtual = 0x080,        // 64 x {archive[12], id}
    Scratch = 0x480,        // request struct + names for the bridge
    Image = 0x500,          // event image (<= 0x100)
    RetailFlags = 0x600,    // manager+0x3C, 1280 bytes
    Archives = 0xB00,       // 16 x {name[12], phase request[8], held} archives this demo loads
    ArchiveCount = 0x064,
    Stb = 0xC80,            // STB (<= 0xC000)
};
uint32_t g_arena = 0;
Call g_call = nullptr;
void (*g_original)(Cpu*, uint32_t) = nullptr;
bool g_installed = false;
std::vector<demo_scene::Message> g_messages;  // host copy of the running demo's messages (the record keeps the index)
std::vector<std::u16string> g_texts;           // their text, kept alive while armed
uint32_t message_number() { static const uint32_t n = [] { const char* v = std::getenv("WWHD_DEMO_MESSAGE_NUMBER"); return v ? uint32_t(std::strtoul(v, nullptr, 0)) : 0x2CF3u; }(); return n; }
void arm(uint32_t index) { if (index < g_texts.size()) story_mail::say(message_number(), &g_texts[index]); }

uint32_t call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args) { Cpu saved = *c; const uint32_t r = g_call(c, fn, args); *c = saved; return r; }
std::string cstr(uint32_t p, size_t max = 64) { std::string s; if (!p) return s; for (size_t i = 0; i < max; ++i) { const char ch = char(ld8(p + uint32_t(i))); if (!ch) break; s += ch; } return s; }
bool recorded() { return g_arena && ld32(g_arena + Marker) == kMarker; }
uint32_t crc32(const std::string& s) {
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : s) { c ^= b; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
    return ~c;
}
// The game's id of a file in an archive: its position in the archive's row of the id table.
int32_t resource_id(const demo_scene::Resource& r) {
    const uint32_t hash = crc32(r.archive), rows = ld32(kIdTableCount);
    if (rows > 4096) return -1;
    for (uint32_t i = 0; i < rows; ++i) {
        const uint32_t row = kIdTable + i * 12;
        if (ld32(row) != hash) continue;
        const uint32_t names = ld32(row + 4), count = ld32(row + 8);
        for (uint32_t k = 0; k < count && k < 0x10000; ++k)
            if (cstr(ld32(names + k * 8), 40) == r.file) return int32_t(k);
        return -1;
    }
    return -1;
}
uint32_t text(uint32_t at, const std::string& s) { std::memset(mem::ptr(at), 0, 16); std::memcpy(mem::ptr(at), s.c_str(), std::min<size_t>(s.size(), 15)); return at; }
bool archive_loaded(Cpu* c, const std::string& archive) {
    const uint32_t req = g_arena + Scratch;
    st32(req, text(req + 0x10, archive)); st32(req + 4, kLiteralVtable);  // a {char*, vtable} literal name, as the game passes them
    return call(c, kGetArchive, {ld32(kResControl), req}) != 0;
}

// --- the virtual archive bridge (dComIfG_getObjectIDRes / getObjectRes in HD) ---
bool virtual_name(uint32_t request) { return request && recorded() && cstr(ld32(request), 16) == kVirtualArchive; }
void lookup(Cpu* c, uint32_t fn) {
    const uint32_t request = c->r[4], id = c->r[5];
    if (!virtual_name(request)) { g_original(c, fn); return; }
    const uint32_t n = id & 0xFFFF, count = ld32(g_arena + VirtualCount);
    if (n >= count) { LOG("[demo] virtual archive id %u is not mapped", n); c->r[3] = 0; return; }
    const uint32_t entry = g_arena + Virtual + n * 16, req = g_arena + Scratch + 0x40;
    st32(req, entry); st32(req + 4, ld32(request + 4));
    c->r[4] = req; c->r[5] = ld32(entry + 12);
    g_original(c, fn);
    if (!c->r[3]) LOG("[demo] virtual id %u -> %s id 0x%X: not found", n, cstr(entry, 12).c_str(), ld32(entry + 12));
}
void lookup_id(Cpu* c) { lookup(c, kLookupId); }
// getObjectRes(control, {archive}, {name}): the PACKAGE staff's STB by name
void lookup_name(Cpu* c) {
    const uint32_t archive = c->r[4], name = c->r[5];
    if (!virtual_name(archive)) { g_original(c, kLookupName); return; }
    const std::string file = cstr(name ? ld32(name) : 0, 32);
    c->r[3] = file == "moddemo.stb" && ld32(g_arena + StbBytes) ? g_arena + Stb : 0;
    LOG("[demo] PACKAGE loads %s from the virtual archive: %08X", file.c_str(), c->r[3]);
}

void restore_manager(Cpu* c) {
    const uint32_t manager = ld32(g_arena + ManagerAt);
    if (!manager) return;
    call(c, kClearBase, {manager});
    for (unsigned i = 0; i < 8; ++i) st32(manager + i * 4, ld32(g_arena + RetailWords + i * 4));
    st32(manager + 0x20, ld32(g_arena + RetailCameraPlay));
    st32(manager + 0x53C, ld32(g_arena + RetailCurrent));
    std::memcpy(mem::ptr(manager + 0x3C), mem::ptr(g_arena + RetailFlags), kFlagBytes);
}
}  // namespace

void configure(uint32_t arena, Call call_fn, void (*original)(Cpu*, uint32_t)) {
    g_arena = arena; g_call = call_fn; g_original = original;
    if (g_arena) std::memset(mem::ptr(g_arena), 0, kArenaBytes);
}
bool configured() { return g_arena && g_call && g_original; }

void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    if (g_installed || !configured()) return;
    for (uint32_t fn : {kLookupId, kLookupName})
        if (!replaced_by(fn).empty()) { LOG("[demo] %s owns resource lookup %08X; demo steps disabled", replaced_by(fn).c_str(), fn); return; }
    replace(kLookupId, lookup_id);
    replace(kLookupName, lookup_name);
    g_installed = true;
    LOG("[demo] installed: virtual demo archive %s", kVirtualArchive);
}

namespace {
demo_scene::Demo g_demo;  // host copy of the requested demo (the record says where it is)
bool g_camera_stopped = false;
// Phase 0: the archives the demo's files come from are loaded (and held) like an actor would.
bool load_archives(Cpu* c, bool& ready, std::string& why) {
    ready = true;
    const uint32_t count = ld32(g_arena + ArchiveCount);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t entry = g_arena + Archives + i * 24;
        const uint32_t state = call(c, kResLoad, {entry + 12, entry});
        if (ld32(g_arena + Ticks) < 4) LOG("[demo] archive %s load state %u (loaded %u)", cstr(entry, 12).c_str(), state, unsigned(archive_loaded(c, cstr(entry, 12))));
        if (state == 4) { st32(entry + 20, 1); continue; }
        if (state >= 5) return why = "archive " + cstr(entry, 12) + " could not be loaded", false;
        ready = false;
    }
    return true;
}
void drop_archives(Cpu* c) {
    const uint32_t count = ld32(g_arena + ArchiveCount);
    for (uint32_t i = 0; i < count && i < 16; ++i) {
        const uint32_t entry = g_arena + Archives + i * 24;
        if (ld32(entry + 20)) call(c, kResDelete, {entry + 12, entry});
        st32(entry + 20, 0);
    }
    st32(g_arena + ArchiveCount, 0);
}
// Phase 1: participants present, then the demo's event list replaces the stage's until the end.
bool begin(Cpu* c, std::string& why) {
    const uint32_t play = call(c, kPlay, {}), link = play ? ld32(play + kPlayer) : 0;
    if (!play || !link) return why = "no game play scene", false;
    if (ld32(kDemoMode) || ld32(kDemoFile)) return why = "a game demo is running", false;
    demo_scene::Compiled compiled;
    if (!demo_scene::compile(g_demo, resource_id, compiled, why, message_number())) return false;
    // every participant must exist: the game refuses a whole demo when one named actor is missing
    for (const auto& name : compiled.participants) {
        std::memset(mem::ptr(g_arena + Scratch), 0, 16);
        std::memcpy(mem::ptr(g_arena + Scratch), name.c_str(), name.size());
        if (!call(c, kSearchFromName, {g_arena + Scratch, 0, 0})) return why = "actor " + name + " is not in this room", false;
    }
    if (compiled.stb.size() > kArenaBytes - Stb) return why = "demo data too large", false;
    demo_event_image::Spec spec;
    if (g_demo.origin_link) spec.origin = demo_event_image::Origin{u32_as_f32(ld32(link + 0x314)), u32_as_f32(ld32(link + 0x318)), u32_as_f32(ld32(link + 0x31C)), g_demo.angle};
    else if (g_demo.origin) spec.origin = demo_event_image::Origin{g_demo.origin->x, g_demo.origin->y, g_demo.origin->z, g_demo.angle};
    std::vector<uint8_t> image;
    if (!demo_event_image::make(spec, image, why)) return false;
    const uint32_t manager = play + kManager;
    st32(g_arena + ManagerAt, manager);
    for (unsigned i = 0; i < 8; ++i) st32(g_arena + RetailWords + i * 4, ld32(manager + i * 4));
    st32(g_arena + RetailCameraPlay, ld32(manager + 0x20));
    st32(g_arena + RetailCurrent, ld32(manager + 0x53C));
    std::memcpy(mem::ptr(g_arena + RetailFlags), mem::ptr(manager + 0x3C), kFlagBytes);
    std::memcpy(mem::ptr(g_arena + SavedArc), mem::ptr(kDemoArcName), 12);
    st32(g_arena + VirtualCount, uint32_t(compiled.virtual_ids.size()));
    for (size_t i = 0; i < compiled.virtual_ids.size(); ++i) {
        const uint32_t entry = g_arena + Virtual + uint32_t(i) * 16;
        text(entry, compiled.virtual_ids[i].first.archive);
        st32(entry + 12, compiled.virtual_ids[i].second);
    }
    std::memcpy(mem::ptr(g_arena + Image), image.data(), image.size());
    std::memcpy(mem::ptr(g_arena + Stb), compiled.stb.data(), compiled.stb.size());
    st32(g_arena + StbBytes, uint32_t(compiled.stb.size()));
    st32(g_arena + MessageCount, uint32_t(compiled.messages.size()));
    g_messages = compiled.messages;
    g_texts.clear();
    for (const auto& m : g_messages) {
        std::u16string t;
        if (!story_mail::can_say() || !story_mail::append_utf16(m.text, true, t)) return why = "demo messages need the story system's message hooks", false;
        g_texts.push_back(std::move(t));
    }
    st32(g_arena + MessageIndex, 0);
    arm(0);
    std::memset(mem::ptr(kDemoArcName), 0, 12);
    std::memcpy(mem::ptr(kDemoArcName), kVirtualArchive, sizeof kVirtualArchive - 1);
    st32(g_arena + Phase, 1);
    st32(g_arena + Ticks, 0);
    call(c, kClearBase, {manager});
    call(c, kSetData, {manager, g_arena + Image});
    if (!call(c, kOrderEvent, {manager, 0})) return why = "the game refused the demo event", false;
    if (g_camera_stopped) { const uint32_t camera = call(c, kCamera, {}); if (camera) { call(c, kStartCamera, {camera}); st32(g_arena + CameraStarted, 1); } }
    LOG("[demo] started: %zu bytes STB, %zu archive files, %zu participants, %zu messages, origin %s", compiled.stb.size(), compiled.virtual_ids.size(), compiled.participants.size(),
        compiled.messages.size(), spec.origin ? "set" : "none");
    return true;
}
}  // namespace

bool start(Cpu* c, const demo_scene::Demo& demo, bool camera_stopped, std::string& why) {
    if (!g_installed || !configured()) return why = "demo steps are not installed", false;
    if (recorded()) return why = ld32(g_arena + Phase) == kDraining ? "the previous demo is still being cleared" : "another demo is running", false;
    // resolve every file now (static tables): a wrong archive or file name refuses before anything changes
    demo_scene::Compiled compiled;
    if (!demo_scene::compile(demo, resource_id, compiled, why)) return false;
    std::vector<std::string> archives;
    for (const auto& [r, id] : compiled.virtual_ids)
        if (std::find(archives.begin(), archives.end(), r.archive) == archives.end()) archives.push_back(r.archive);
    if (archives.size() > 16) return why = "demo uses more than 16 archives", false;
    std::memset(mem::ptr(g_arena), 0, Stb);
    for (size_t i = 0; i < archives.size(); ++i) text(g_arena + Archives + uint32_t(i) * 24, archives[i]);
    st32(g_arena + ArchiveCount, uint32_t(archives.size()));
    g_demo = demo;
    g_camera_stopped = camera_stopped;
    (void)c;
    st32(g_arena + Phase, 0);
    st32(g_arena + Marker, kMarker);
    LOG("[demo] requested: %zu archives to load", archives.size());
    return true;
}

Poll poll(Cpu* c) {
    Poll out;
    if (!recorded() || ld32(g_arena + Phase) == kDraining) return out;
    const uint32_t ticks = ld32(g_arena + Ticks) + 1;
    st32(g_arena + Ticks, ticks);
    if (ld32(g_arena + Phase) == 0) {
        bool ready = false;
        std::string why;
        if (ticks > 300) why = "archives did not load in 10 seconds";
        else if (load_archives(c, ready, why) && ready && !begin(c, why) && why.empty()) why = "demo could not start";
        if (!why.empty()) { release(c, why.c_str()); out.state = Poll::State::Failed; out.why = why; return out; }
        out.state = Poll::State::Running;
        return out;
    }
    const uint32_t mode = ld32(kDemoMode), file = ld32(kDemoFile), state = ld32(g_arena + Image + demo_event_image::kEventStateOffset);
    const bool ours = file == g_arena + Stb;
    if (ld32(g_arena + Phase) == 1) {
        if (ours && mode == 1) { st32(g_arena + Phase, 2); LOG("[demo] playing (event state %u)", state); }
        else if (ticks > 90) {  // the PACKAGE never started the demo (refused data): never leave the event hanging
            release(c, "demo did not start");
            out.state = Poll::State::Failed; out.why = "the game did not start the demo data";
            return out;
        }
        out.state = Poll::State::Running;
        return out;
    }
    // suspended at a message point: the next message in frame order
    const uint32_t control = ld32(kDemoControl), index = ld32(g_arena + MessageIndex);
    if (ticks % 30 == 0 && std::getenv("WWHD_DEMO_TRACE"))
        LOG("[demo] trace tick %u mode %u frame %u ours %d state %u control %08X +38 %08X +50 %08X msgstatus %u msgctl %08X", ticks, mode, ld32(GD(0x101D6008)), int(ours), state, control,
            control ? ld32(control + 0x38) : 0, control ? ld32(control + 0x50) : 0, call(c, GC(0x025F795C), {ld32(GD(0x101F4B5C))}), ld32(GD(0x104756C8)));
    // a message point passed and the game resumed the demo (its close decrements the suspend count):
    // the next message's text is armed before its point
    if (ours && mode == 1 && control && index < ld32(g_arena + MessageCount) && index < g_messages.size() &&
        ld32(GD(0x101D6008)) > g_messages[index].frame + 1 && int32_t(ld32(control + 0x38)) <= 0) {
        story_mail::unsay();
        st32(g_arena + MessageIndex, index + 1);
        arm(index + 1);
        LOG("[demo] message %u closed", index);
    }
    if (!ours && mode == 0) {
        // the game's PACKAGE cut removed the demo when its data ended (its event stays running in the
        // compulsory event the scene owns): the step is done
        LOG("[demo] finished by the game at tick %u (event state %u)", ticks, state);
        release(c, "finished");
        out.state = Poll::State::Done;
        return out;
    }
    out.state = Poll::State::Running;
    return out;
}

void release(Cpu* c, const char* reason) {
    if (!recorded() || ld32(g_arena + Phase) == kDraining) return;
    const bool ours = ld32(kDemoFile) == g_arena + Stb && ld32(g_arena + StbBytes);
    if (ours && ld32(kDemoMode)) call(c, kDemoRemove, {});
    if (ld32(g_arena + Phase) >= 1) {
        restore_manager(c);
        std::memcpy(mem::ptr(kDemoArcName), mem::ptr(g_arena + SavedArc), 12);
    }
    if (ld32(g_arena + CameraStarted)) { const uint32_t camera = call(c, kCamera, {}); if (camera) call(c, kStopCamera, {camera}); }
    if (ld32(g_arena + MessageCount)) story_mail::unsay();
    LOG("[demo] released (%s)%s", reason, ours ? ": demo data removed" : "");
    // the demo's model actors delete themselves on their next execute; the archives they draw from
    // are given back a few frames later (tick())
    st32(g_arena + Phase, kDraining);
    st32(g_arena + Ticks, 0);
    st32(g_arena + StbBytes, 0);
    g_messages.clear();
    g_texts.clear();
}

void tick(Cpu* c) {
    if (!recorded() || ld32(g_arena + Phase) != kDraining) return;
    const uint32_t ticks = ld32(g_arena + Ticks) + 1;
    st32(g_arena + Ticks, ticks);
    if (ticks < 10) return;
    drop_archives(c);
    st32(g_arena + Marker, 0);
    st32(g_arena + Phase, 0);
}
bool active() { return recorded() && ld32(g_arena + Phase) != kDraining; }
}  // namespace story_demo
