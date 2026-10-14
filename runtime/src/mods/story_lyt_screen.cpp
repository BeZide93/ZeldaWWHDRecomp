// Story UI layouts: our layout with the game's item screen (story_lyt_screen.h).
#include "story_lyt_screen.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>

#include "guest_addr.h"
#include "ppc.h"
#include "runtime.h"
#include "story_lyt_text.h"
#include "../input.h"

namespace story_lyt_screen {
namespace {
const uint32_t kLoadItemScreen = GC(0x02693650);   // hdIngameLoadItemScreen(screen, projection, matrix, flags)
const uint32_t kPartsDraw = GC(0x026F8B54);        // hdParts_c::draw(this, viewport)
const uint32_t kFinalizeScreens = GC(0x02715374);  // hdIngameUi_c::finalizeScreens(this)
const uint32_t kDrawPrimary = GC(0x02716AD0);      // hdIngameUi_c::drawPrimaryScreens(this, info): the item menu on the GamePad
const uint32_t kCursorDraw = GC(0x026538A0);       // hdCursorMgr_c::draw(this, info): the menus' selection cursor
const uint32_t kDrawHud = GC(0x02715CA0);          // hdIngameUi_c::drawHudScreens(this, info): the TV (the item menu there with the Pro Controller)
const uint32_t kResMgrCache = GC(0x026FB694);      // hdLayoutResMgr_c::cache(this, name, heap)
const uint32_t kNoControlConstruct = GC(0x02700ADC);  // NoControlPart_l::construct(0)
const uint32_t kAttachArchives = GC(0x026F90D8);   // hdParts_c::attachArchives(this, names, count)
const uint32_t kPartsNew = GC(0x0273B050);         // allocate(size, heap, alignment)
const uint32_t kLayoutConstruct = GC(0x026FADB0);  // hdPartsLayout_c::hdPartsLayout_c()
const uint32_t kBindGroup = GC(0x02004E04);        // animation set: bindGroup(entry, animation, group)
const uint32_t kAnimPlay = GC(0x020053E4);         // animation set: play(entry, slot, rate f1)
const uint32_t kAnimSetFrame = GC(0x0200552C);     // animation set: setFrame(entry, slot, frame f1)
const uint32_t kAnimIsEnd = GC(0x02005840);        // animation set: isEnd(entry)
const uint32_t kCalcLayout = GC(0x026F8CD0);       // hdParts_c::calcLayout(this)
const uint32_t kUiSystem = GD(0x1018C404);         // the UI system (its heap at +0x10)
const uint32_t kLayoutResMgr = GD(0x101F7274);     // the layout resource manager
const uint32_t kIngameUi = GD(0x101F8344);         // the in-game UI scene (item screen at +0x17C)
const uint32_t kSafeStringVtable = GD(0x1010A3FC); // a sead SafeString vtable of the parts framework
constexpr uint32_t kItemScreen = 0x17C;
// hdParts_c: vtable at +4 (delete +0x0C, finalize +0x54); its layout at +0x44, resource accessor +0x34;
// hdPartsLayout_c: vtable +0xE0 (vLoad +0x14), layout object +4, animation set +0xD4
constexpr uint32_t kPartsVtable = 4, kVtDelete = 0x0C, kVtFinalize = 0x54, kPartsLayout = 0x44, kPartsAccessor = 0x34;
constexpr uint32_t kLayoutVtable = 0xE0, kVtLoad = 0x14, kLayoutLyt = 4, kLayoutAnimSet = 0xD4;
constexpr uint32_t kPaneTranslate = 0x1C, kPaneSize = 0x3C, kPaneFlags = 0x44, kPaneAlpha = 0x45;

void (*g_original)(Cpu*, uint32_t) = nullptr;
uint32_t (*g_call)(Cpu*, uint32_t, std::initializer_list<uint32_t>) = nullptr;
Client g_client;
bool g_has_client = false;
uint32_t g_part = 0, g_layout = 0, g_set = 0;
bool g_failed = false;  // this item screen's build failed: not again until the next one
uint32_t g_part_vt = 0, g_layout_vt = 0;
bool g_check = false;  // a state was loaded: check still_there() before the next use
std::map<std::string, uint32_t> g_panes;
std::chrono::steady_clock::time_point g_covered{};  // when our layout last covered the item menu
uint32_t g_viewport = 0;  // the item screen's viewport, while drawPrimaryScreens runs (0: it didn't draw)

void drop() { g_part = g_layout = g_set = 0, g_panes.clear(); }

// After a state load: are our objects still in guest memory (a portable state, or a full state saved after
// the build)? Otherwise the handles are dropped without touching the memory.
bool still_there() {
    if (!g_part) return false;
    if (ld32(g_part + kPartsVtable) == g_part_vt && ld32(g_part + kPartsLayout) == g_layout &&
        ld32(g_layout + kLayoutVtable) == g_layout_vt && ld32(g_layout + kLayoutAnimSet) == g_set)
        return true;
    LOG("[lyt] %s: not in the loaded state: built again with the next item screen", g_client.layout.c_str());
    drop();
    return false;
}

int entry_of(const std::string& anim) {
    for (size_t i = 0; i < g_client.anims.size(); i++)
        if (g_client.anims[i].name == anim) return int(i);
    return -1;
}

void destroy(Cpu* c) {
    if (!still_there()) return;
    const uint32_t part = g_part;
    drop();
    const uint32_t vt = ld32(part + kPartsVtable);
    g_call(c, ld32(vt + kVtFinalize), {part});
    g_call(c, ld32(vt + kVtDelete), {part, 3});
    LOG("[lyt] %s: freed with the game's screens", g_client.layout.c_str());
}

void build(Cpu* c, uint32_t projection, uint32_t matrix) {
    if (g_part) destroy(c);  // a new item screen (should have been freed with the old one)
    g_failed = true;
    if (!g_has_client) return;
    if (FILE* f = fopen(g_client.archive.c_str(), "rb")) fclose(f);
    else return LOG("[lyt] %s: no archive %s", g_client.layout.c_str(), g_client.archive.c_str());
    const uint32_t name = safe_string(g_client.layout.c_str());
    const uint32_t mgr = ld32(kLayoutResMgr), ui = ld32(kUiSystem), heap = ui ? ld32(ui + 0x10) : 0;
    if (!mgr || !heap) return LOG("[lyt] %s: no layout manager / UI heap", g_client.layout.c_str());
    if (!g_call(c, kResMgrCache, {mgr, name, 0})) return LOG("[lyt] %s: the archive didn't load", g_client.layout.c_str());
    const uint32_t part = g_call(c, kNoControlConstruct, {0});
    if (!part) return LOG("[lyt] %s: no parts object", g_client.layout.c_str());
    g_call(c, kAttachArchives, {part, name, 1});
    uint32_t layout = g_call(c, kPartsNew, {0x110, heap, 4});
    if (layout) layout = g_call(c, kLayoutConstruct, {layout});
    if (!layout) {
        const uint32_t vt = ld32(part + kPartsVtable);
        g_call(c, ld32(vt + kVtDelete), {part, 3});
        return LOG("[lyt] %s: no layout object", g_client.layout.c_str());
    }
    st32(part + kPartsLayout, layout);
    g_part = part, g_layout = layout;  // from here destroy() frees both
    uint32_t slots = 1;
    for (const auto& a : g_client.anims) slots = std::max(slots, a.slot + 1);
    const uint32_t load = ld32(ld32(layout + kLayoutVtable) + kVtLoad);
    const uint32_t entries = uint32_t(g_client.anims.size());
    if (!g_call(c, load, {layout, name, ld32(part + kPartsAccessor), projection, matrix, entries, entries ? slots : 0, 0})) {
        LOG("[lyt] %s: the layout didn't load", g_client.layout.c_str());
        return destroy(c);
    }
    g_set = ld32(layout + kLayoutAnimSet);
    if (entries && !g_set) {
        LOG("[lyt] %s: no animation set", g_client.layout.c_str());
        return destroy(c);
    }
    for (uint32_t i = 0; i < entries; i++)
        g_call(c, kBindGroup, {g_set, i, safe_string(g_client.anims[i].name.c_str()), safe_string(g_client.anims[i].group.c_str())});
    g_failed = false;
    g_part_vt = ld32(part + kPartsVtable), g_layout_vt = ld32(layout + kLayoutVtable);
    LOG("[lyt] %s: built (parts %08X, layout %08X, %u animations)", g_client.layout.c_str(), part, layout, entries);
    if (g_client.built) g_client.built(c, ld32(g_layout + kLayoutLyt));
}

// Our calls into the game must not change what the hooked function's caller sees: the registers are
// put back as they were (the original's return values included).
struct Keep {
    Cpu* c;
    Cpu saved;
    explicit Keep(Cpu* cpu) : c(cpu), saved(*cpu) {}
    ~Keep() { *c = saved; }
};

namespace hooks {
void load_item_screen(Cpu* c) {
    const uint32_t projection = c->r[4], matrix = c->r[5];
    g_original(c, kLoadItemScreen);
    Keep keep(c);
    build(c, projection, matrix);
}

// the item screen draws: remember its viewport; ours is drawn after all of the item menu's screens
void parts_draw(Cpu* c) {
    const uint32_t self = c->r[3], viewport = c->r[4];
    g_original(c, kPartsDraw);
    if (!g_part) return;
    const uint32_t scene = ld32(kIngameUi);
    if (scene && self == ld32(scene + kItemScreen)) g_viewport = viewport;
}

// after a screen group was drawn: ours on top of it, if the item screen was among it
void draw_after(Cpu* c) {
    const uint32_t viewport = g_viewport;
    g_viewport = 0;
    if (!g_part || !viewport) return;
    Keep keep(c);
    if (g_check) {
        g_check = false;
        if (!still_there()) return;
    }
    if (g_client.frame && !g_client.frame(c, ld32(g_layout + kLayoutLyt))) return;
    if (!g_part) return;
    g_call(c, kCalcLayout, {g_part});
    g_call(c, kPartsDraw, {g_part, viewport});
    g_covered = std::chrono::steady_clock::now();
}

// The menus' selection cursor (four corner brackets) is drawn by a pass after ours on the TV: while our
// layout covers the item menu (drawn within the last 0.1 s) the cursor is not drawn; nothing of its state
// changes, it is drawn again as soon as our layout is gone.
void cursor_draw(Cpu* c) {
    if (g_part && std::chrono::steady_clock::now() - g_covered < std::chrono::milliseconds(100)) return;
    g_original(c, kCursorDraw);
}

// The item menu on the GamePad: its screens (item screen, sub window, menu buttons, cursor, messages) are
// drawn by drawPrimaryScreens; ours right after them.
void draw_primary(Cpu* c) {
    g_viewport = 0;
    g_original(c, kDrawPrimary);
    if (input::pro_controller()) g_viewport = 0;  // the menu is the TV's
    draw_after(c);
}

// With the Pro Controller the item menu is on the TV, drawn by drawHudScreens; ours right after it, in the
// render state that pass set up (a later pass, e.g. drawInputScreens with the menu cursor, draws the layout's
// pictures wrongly).
void draw_hud(Cpu* c) {
    g_viewport = 0;
    g_original(c, kDrawHud);
    if (!input::pro_controller()) g_viewport = 0;  // the menu is the GamePad's
    draw_after(c);
}

void finalize_screens(Cpu* c) {
    {
        Keep keep(c);  // the original still needs its `this` (r3)
        destroy(c);
    }
    g_failed = false;
    g_original(c, kFinalizeScreens);
}
}  // namespace hooks
}  // namespace

uint32_t safe_string(const char* text) {
    const uint32_t n = uint32_t(strlen(text)) + 1;
    const uint32_t buf = mem::host_alloc(n, 4);
    memcpy(mem::ptr(buf), text, n);
    const uint32_t s = mem::host_alloc(8, 4);
    st32(s, buf);
    st32(s + 4, kSafeStringVtable);
    return s;
}

void set_client(Client client) {
    g_client = std::move(client);
    g_has_client = !g_client.layout.empty() && !g_client.archive.empty();
}
bool has_client() { return g_has_client; }

void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>)) {
    g_original = original;
    g_call = call;
}

void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    if (!g_has_client || !g_original || !g_call) return;
    // all three or nothing: a layout that is built but never freed (or never drawn) must not happen
    for (uint32_t fn : {kLoadItemScreen, kPartsDraw, kFinalizeScreens, kDrawPrimary, kDrawHud, kCursorDraw})
        if (const std::string by = replaced_by(fn); !by.empty()) {
            LOG("[lyt] %s: %08X is replaced by %s: no layout", g_client.layout.c_str(), fn, by.c_str());
            g_has_client = false;
            return;
        }
    replace(kLoadItemScreen, hooks::load_item_screen);
    replace(kPartsDraw, hooks::parts_draw);
    replace(kFinalizeScreens, hooks::finalize_screens);
    replace(kDrawPrimary, hooks::draw_primary);
    replace(kDrawHud, hooks::draw_hud);
    replace(kCursorDraw, hooks::cursor_draw);
    LOG("[lyt] %s (%s): from %s", g_client.layout.c_str(), g_client.owner, g_client.archive.c_str());
}

void state_loaded() {
    g_panes.clear();
    g_check = g_part != 0;
}

std::string archive_for(const std::string& guest) {
    if (!g_has_client) return "";
    const std::string want = "layout/" + g_client.layout + ".szs";
    if (guest.size() < want.size()) return "";
    const std::string tail = guest.substr(guest.size() - want.size());
    for (size_t i = 0; i < want.size(); i++)
        if (std::tolower((unsigned char)tail[i]) != std::tolower((unsigned char)want[i])) return "";
    if (guest.size() > want.size() && guest[guest.size() - want.size() - 1] != '/') return "";
    return g_client.archive;
}

bool ready() {
    if (g_check) g_check = false, still_there();
    return g_part != 0;
}
uint32_t lyt() { return g_layout ? ld32(g_layout + kLayoutLyt) : 0; }
uint32_t anim_set() { return g_set; }

uint32_t pane(Cpu* c, const char* name) {
    if (!g_layout) return 0;
    auto it = g_panes.find(name);
    if (it != g_panes.end()) return it->second;
    const uint32_t p = story_lyt_text::find_pane(c, lyt(), name);
    g_panes[name] = p;
    if (!p) LOG("[lyt] %s: no pane %s", g_client.layout.c_str(), name);
    return p;
}

void set_visible(uint32_t p, bool visible) {
    if (p) st8(p + kPaneFlags, uint8_t((ld8(p + kPaneFlags) & 0xFE) | (visible ? 1 : 0)));
}
void set_alpha(uint32_t p, uint8_t alpha) {
    if (p) st8(p + kPaneAlpha, alpha);
}
void set_translate(uint32_t p, float x, float y) {
    if (!p) return;
    st32(p + kPaneTranslate, f32_as_u32(x));
    st32(p + kPaneTranslate + 4, f32_as_u32(y));
    st8(p + kPaneFlags, uint8_t(ld8(p + kPaneFlags) | 0x10));
}
void set_size(uint32_t p, float w, float h) {
    if (!p) return;
    st32(p + kPaneSize, f32_as_u32(w));
    st32(p + kPaneSize + 4, f32_as_u32(h));
    st8(p + kPaneFlags, uint8_t(ld8(p + kPaneFlags) | 0x10));
}

void play(Cpu* c, const std::string& anim, float rate) {
    const int e = entry_of(anim);
    if (!g_set || e < 0) return;
    c->f[1].ps0 = rate, c->f[1].ps1 = rate;
    g_call(c, kAnimPlay, {g_set, uint32_t(e), g_client.anims[e].slot});
}
void set_frame(Cpu* c, const std::string& anim, float frame) {
    const int e = entry_of(anim);
    if (!g_set || e < 0) return;
    c->f[1].ps0 = frame, c->f[1].ps1 = frame;
    g_call(c, kAnimSetFrame, {g_set, uint32_t(e), g_client.anims[e].slot});
}
bool ended(Cpu* c, const std::string& anim) {
    const int e = entry_of(anim);
    return !g_set || e < 0 || g_call(c, kAnimIsEnd, {g_set, uint32_t(e)}) != 0;
}

}  // namespace story_lyt_screen
