// Feasibility prototype (local only, never published): a layout of our own (StoryLyt_00.szs, written by
// tools/story_mods/story_lyt.py) loaded and drawn by the game's own UI framework while the item menu is
// open. Enabled with WWHD_STORY_LYT=<path to StoryLyt_00.szs>. The calls are the ones the game's own screens
// make (HD decomp, FW-layout_mgr / FW-parts_*):
//   hdLayoutResMgr_c::cache(name)             loads "Layout/<name>.szs" into the layout resource manager
//   NoControlPart_l::construct                a parts object (UI parts base, no player control)
//   hdParts_c::attachArchives(name, 1)        the archive's resource accessor for the parts
//   hdPartsLayout_c construct + vLoad          the parts' layout screen from "<name>.bflyt", 1 animation slot
//   animation set bindGroup(0, InAll, G_All_00) the fade (anim/<name>_InAll.bflan) on the group
//   hdAnimSet_play / hdParts_c::calcLayout / hdParts_c::draw   every frame the item screen draws
#include "runtime.h"
#include "ppc.h"
#include "guest_addr.h"
#include "story_lyt_states.h"
#include "story_lyt_text.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>

namespace story_lyt {
namespace {
const uint32_t kLoadItemScreen = GC(0x02693650);  // hdIngameLoadItemScreen(screen, projection, matrix, flags)
const uint32_t kPartsDraw = GC(0x026F8B54);       // hdParts_c::draw(this, viewport): the item screen's draw
const uint32_t kResMgrCache = GC(0x026FB694);     // hdLayoutResMgr_c::cache(this, name, heap)
const uint32_t kNoControlConstruct = GC(0x02700ADC);
const uint32_t kAttachArchives = GC(0x026F90D8);  // hdParts_c::attachArchives(this, names, count)
const uint32_t kPartsNew = GC(0x0273B050);        // allocate(size, heap, alignment)
const uint32_t kLayoutConstruct = GC(0x026FADB0); // hdPartsLayout_c::hdPartsLayout_c()
const uint32_t kBindGroup = GC(0x02004E04);       // animation set: bind animation `entry` to its pane group
const uint32_t kAnimPlay = GC(0x020053E4);        // animation set: play(entry, slot, rate)
const uint32_t kCalcLayout = GC(0x026F8CD0);      // hdParts_c::calcLayout(this)
const uint32_t kUiSystem = 0x1018C404;            // the UI system pointer (its heap at +0x10)
const uint32_t kLayoutResMgr = 0x101F7274;        // the layout resource manager pointer
const uint32_t kIngameUi = 0x101F8344;            // the in-game UI scene (item screen at +0x17C)
const uint32_t kSafeStringVtable = 0x1010A3FC;    // a sead SafeString vtable of the parts framework

void (*g_original)(Cpu*, uint32_t) = nullptr;
uint32_t (*g_call)(Cpu*, uint32_t, std::initializer_list<uint32_t>) = nullptr;
std::string g_file;
uint32_t g_part = 0, g_layout = 0, g_anim = 0;
std::chrono::steady_clock::time_point g_last_draw{};
bool g_failed = false;
uint32_t g_draws = 0, g_replay = 0;  // WWHD_STORY_LYT_REPLAY=N: restart InAll every N item-screen draws (testing)
bool g_text_demo = false;  // WWHD_STORY_LYT_TEXT_DEMO=1: fill the text demo layout's panes (story_lyt_text.cpp)

// a sead SafeString in guest memory: { text pointer, vtable }
uint32_t safe_string(const char* text) {
    const uint32_t n = uint32_t(strlen(text)) + 1;
    const uint32_t buf = mem::host_alloc(n, 4);
    memcpy(mem::ptr(buf), text, n);
    const uint32_t s = mem::host_alloc(8, 4);
    st32(s, buf);
    st32(s + 4, kSafeStringVtable);
    return s;
}

void create(Cpu* c, uint32_t projection, uint32_t matrix) {
    if (g_part || g_failed) return;
    const uint32_t name = safe_string("StoryLyt_00");
    const uint32_t mgr = ld32(kLayoutResMgr);
    const uint32_t ui = ld32(kUiSystem);
    const uint32_t heap = ui ? ld32(ui + 0x10) : 0;
    if (!mgr || !heap) { LOG("[story-lyt] no layout manager / UI heap yet"); g_failed = true; return; }
    const uint32_t cached = g_call(c, kResMgrCache, {mgr, name, 0});
    LOG("[story-lyt] cache(StoryLyt_00) -> %u", cached);
    if (!cached) { g_failed = true; return; }
    const uint32_t part = g_call(c, kNoControlConstruct, {0});
    if (!part) { LOG("[story-lyt] NoControlPart_l::construct failed"); g_failed = true; return; }
    g_call(c, kAttachArchives, {part, name, 1});
    const uint32_t accessor = ld32(part + 0x34);
    uint32_t layout = g_call(c, kPartsNew, {0x110, heap, 4});
    if (layout) layout = g_call(c, kLayoutConstruct, {layout});
    if (!layout) { LOG("[story-lyt] layout allocation failed"); g_failed = true; return; }
    st32(part + 0x44, layout);
    const uint32_t vtable = ld32(layout + 0xE0);
    const uint32_t load = ld32(vtable + 0x14);
    // animation entries and play slots (WWHD_STORY_LYT_ANIMS: several, e.g. button states; else InAll only)
    const bool many = story_lyt_states::active();
    const uint32_t entries = many ? story_lyt_states::entry_count() : 1, slots = many ? story_lyt_states::slot_count() : 1;
    const uint32_t ok = g_call(c, load, {layout, name, accessor, projection, matrix, entries, slots, 0});
    const uint32_t set = ld32(layout + 0xD4);
    LOG("[story-lyt] parts %08X layout %08X accessor %08X vLoad -> %u, lyt %08X, animation set %08X", part, layout, accessor,
        ok, ld32(layout + 4), set);
    if (set && many) {
        story_lyt_states::bind(c, g_call, set, safe_string);
        g_anim = set;
    } else if (set) {
        const uint32_t anim = safe_string("InAll"), group = safe_string("G_All_00");
        g_call(c, kBindGroup, {set, 0, anim, group});
        g_anim = set;
    }
    g_part = part, g_layout = layout;
    if (g_text_demo && ok) story_lyt_text::demo(c, ld32(layout + 4), 0);
}

namespace hooks {
void load_item_screen(Cpu* c) {
    const uint32_t projection = c->r[4], matrix = c->r[5];
    g_original(c, kLoadItemScreen);
    create(c, projection, matrix);
}

void parts_draw(Cpu* c) {
    const uint32_t self = c->r[3], viewport = c->r[4];
    g_original(c, kPartsDraw);
    const uint32_t scene = ld32(kIngameUi);
    if (!g_part || !scene || self != ld32(scene + 0x17C)) return;
    const auto now = std::chrono::steady_clock::now();
    ++g_draws;
    const bool reopened = now - g_last_draw > std::chrono::milliseconds(500);  // the item screen opens (again)
    if (g_anim && (reopened || (g_replay && g_draws % g_replay == 0))) {           // fade in
        if (story_lyt_states::active()) {
            story_lyt_states::play_named(c, g_call, g_anim, "InAll");
        } else {
            c->f[1].ps0 = 1.0;
            c->f[1].ps1 = 1.0;
            g_call(c, kAnimPlay, {g_anim, 0, 0});
        }
        LOG("[story-lyt] InAll starts (item screen draw %u)", g_draws);
    }
    g_last_draw = now;
    if (g_anim) story_lyt_states::tick(c, g_call, g_anim, g_draws);
    if (g_text_demo) story_lyt_text::demo(c, ld32(g_layout + 4), g_draws);
    g_call(c, kCalcLayout, {g_part});
    g_call(c, kPartsDraw, {g_part, viewport});
}
}  // namespace hooks
}  // namespace

void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>)) {
    g_original = original;
    g_call = call;
}

void install(const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    const char* file = getenv("WWHD_STORY_LYT");
    if (!file || !*file || !g_original || !g_call) return;
    g_file = file;
    if (const char* r = getenv("WWHD_STORY_LYT_REPLAY")) g_replay = uint32_t(atoi(r));
    if (const char* t = getenv("WWHD_STORY_LYT_TEXT_DEMO")) g_text_demo = *t == '1';
    replace(kLoadItemScreen, hooks::load_item_screen);
    replace(kPartsDraw, hooks::parts_draw);
    LOG("[story-lyt] prototype layout from %s", file);
}
}  // namespace story_lyt
