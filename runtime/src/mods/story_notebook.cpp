// The story notebook (story_notebook.h, docs/mod-sdk-v2.md "The story notebook"): the services, the
// page driver and the item menu entry.
#include "story_notebook.h"
#include "story_notebook_page.h"
#include "story_notebook_bag.h"
#include "story_chart.h"
#include "story_mail.h"
#include "guest_hud.h"
#include "guest_png.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>

#include "guest_addr.h"
#include "input.h"
#include "runtime.h"

// the icon's PNG, compiled in (runtime/assets/story_notebook/notebook_icon_192.png, cmake/EmbedBytes.cmake)
extern const unsigned char story_notebook_icon_png[];
extern const unsigned long story_notebook_icon_png_size;

namespace story_notebook {
namespace {

const uint32_t kSavePtr = GD(0x101F84DC);  // save base pointer (d_com_inf_game)
const std::string kOwner = "port:story-notebook";  // the HUD list's owner (no mod ID has a colon)

// The item menu (HD decomp MENU-menu_main, hd_screen_menu_main.cpp / hd_screen_menu_item.cpp at
// decomp-readable 02a56a87). The notebook is kept in the Delivery Bag window: its eight places show
// the save's eight delivery-bag entries (keys 0x30..0x37 = dSv_player_bag_item_c::mReserve[0..7], 0xFF
// empty). When the window is built, the first empty place shows the notebook: the save read for that
// key returns kNotebookItem (an item number no game item uses, so no game code knows it), whose icon
// is a letter's. Nothing is written to the save: removing the mods leaves the place empty again.
const uint32_t kInventoryItem = GC(0x02632D94);   // u32 hdMenuItem_inventoryItem(key): the save's item for a menu key
const uint32_t kWindowBuild = GC(0x02692548);     // its call in executeSaveChildren (the bag window opens)
const uint32_t kWindowInit = GC(0x02693C24);      // its call in initializeSaveChildren (the menu opens on a window)
const uint32_t kIconFrame = GC(0x0262F880);       // u32 itemForSlot(item, r4 = item number): the icon frame
const uint32_t kOpenChild = GC(0x0268F060);       // hdMenuMain_c::openSelectedChild(index): A on a place: the item's caption
const uint32_t kTouchSelect = GC(0x02630A64);     // hdMenuItem_c::selectTouchEnter(): a place is touched (an icon is picked up)
const uint32_t kSetVisible = GC(0x026333F0);      // hdMenuItem_c::setVisible(visible): shows the item's icon pane
const uint32_t kExecuteWindow = GC(0x0268FF50);   // hdMenuMain_c::executeWindow(): the bag window's state, every frame it is open
const uint32_t kCursorFloat = GC(0x026302B8);     // hdMenuItem_c::beginCursorSelection(point): X/Y/R lifts the item off its place
const uint32_t kSelectedValue = GC(0x0268F420);   // hdMenuMain_c::getSelectedItemValue(receiver): the key X/Y/R would take
const uint32_t kAssignButton = GC(0x02714B0C);    // game system: put a menu key on X/Y/R (controller, button, key)
constexpr uint32_t kFirstDeliveryKey = 0x30, kDeliveryPlaces = 8, kEmpty = 0xFF;
constexpr uint32_t kNotebookItem = 0xA0;          // dItemNo SALVAGE_ITEM_2: unused by the game, no menu mapping
constexpr uint32_t kNotebookIcon = 0x24;          // the icon frame of the Cabana Deed (a sheet of parchment, the get-item stand-in)
constexpr uint32_t kMenuChildren = 0x48, kItemNumber = 0xEC;  // hdParts_c::mChildren {count, data}; hdMenuItem_c::mSlot
// The notebook's own icon (story_notebook_bag.h): drawn by the port over the bag place, the game's
// sentinel icon pane (hdMenuItem_c::mPaneGroups[0][0], N_ItemIcon_00) kept hidden underneath.
constexpr uint32_t kItemVisible = 0xFC, kItemPaneGroups = 0x8C, kItemLayout = 0x44;  // hdMenuItem_c / hdParts_c
constexpr uint32_t kLayoutLyt = 0x4, kLytRootPane = 0xC;                            // hdPartsLayout_c / hdPartsLyt_c
constexpr uint32_t kPaneParent = 0xC, kPaneTranslate = 0x1C, kPaneScale = 0x34, kPaneFlags = 0x44, kPaneGlobalAlpha = 0x46;
constexpr float kIconSize = 84.0f;  // GamePad pixels at scale 1: the size of the game's item icons in the bag
const std::string kIconOwner = "port:story-notebook-icon";
constexpr uint32_t kMenuWindowMode = 0x54, kDeliveryWindow = 2;           // hdMenuMain_c::mWindowMode: 2 the Delivery Bag
constexpr uint32_t kFirstWindowChild = 0x14, kWindowChildren = 8;          // the bag window's eight places (mChildren 0x14..0x1B)
// The notebook's bag place: a child of the menu's open Delivery Bag window, found every frame the
// window runs (executeWindow). Only that place carries the icon: the menu's floating icon (the item
// the cursor or a touch lifts, sMenuIcon) is another item that may show the notebook's number too.
uint32_t g_menu = 0, g_bag_item = 0;  // game thread
uint64_t g_frame = 0, g_window_frame = 0;  // pad() counts frames; the frame the bag window last ran
const char* g_icon_reason = "";       // why the icon is (not) shown, for the trace

std::mutex g_mu;
std::vector<Quest> g_quests;  // registered this session, in registration order
// portraits: package images per quest key, created in the HUD store on demand
struct Portrait {
    guestmods::hud::Image pixels;
    guestmods::hud::Handle handle = 0;
    uint64_t generation = 0;
};
std::map<uint16_t, Portrait> g_portraits;
bool g_imported = false, g_installed = false;
std::atomic<bool> g_open{false};
Page g_page;  // game thread
void (*g_original)(Cpu*, uint32_t) = nullptr;

bool trace() {
    static const bool on = [] { const char* e = getenv("WWHD_NOTEBOOK_TRACE"); return e && *e == '1'; }();
    return on;
}

uint32_t save_base() { return ld32(kSavePtr); }
bool have_save() { return save_base() != 0; }
story_mail::Book book() { return story_mail::Book(mem::ptr(save_base() + story_mail::kBookOffset)); }

bool one_line(const std::string& s, size_t max) {
    std::u16string ignored;
    return s.size() <= max && story_mail::append_utf16(s, false, ignored);
}
bool lines(const std::string& s, size_t max) {
    std::u16string ignored;
    return s.size() <= max && story_mail::append_utf16(s, true, ignored);
}

Quest* find(const std::string& mod, const std::string& quest) {
    for (Quest& q : g_quests)
        if (q.mod == mod && q.id == quest) return &q;
    return nullptr;
}
int32_t saved(uint16_t key) {
    if (!have_save()) return kResultNew;
    story_mail::Book b = book();
    return b.usable() ? saved_step(b, key) : kResultNew;
}

// story_chart's source of the tracked quest's marker (game thread)
bool tracked_marker(float& x, float& z, std::string& label) {
    if (!have_save()) return false;
    story_mail::Book b = book();
    std::lock_guard lock(g_mu);
    const Quest* q = tracked_place(g_quests, b);
    if (!q) return false;
    x = q->x, z = q->z, label = q->title;
    return true;
}

// the save's delivery-bag entry of a key (the game's own read)
uint32_t reserve(Cpu* c, uint32_t key) {
    Cpu probe = *c;
    probe.r[3] = key;
    g_original(&probe, kInventoryItem);
    return probe.r[3] & 0xFF;
}
// The key whose place shows the notebook now: the first empty delivery place (0: none)
uint32_t notebook_key(Cpu* c) {
    if (!have_save() || !notebook_owned()) return 0;
    for (uint32_t k = kFirstDeliveryKey; k < kFirstDeliveryKey + kDeliveryPlaces; k++)
        if (reserve(c, k) == kEmpty) return k;
    return 0;  // all eight hold game items: the notebook waits for a free place
}

// The sentinel's icon pane of a bag place, or 0
uint32_t icon_pane(uint32_t item) {
    const uint32_t data = ld32(item + kItemPaneGroups + 4);
    return data ? ld32(data) : 0;
}

// The notebook's icon this frame: only while the Delivery Bag window runs (this frame or the one
// before: the window's state runs after the controller read), over the notebook's own bag place.
// Keeps the game's sentinel icon of that place hidden.
IconPlace compute_icon_place() {
    IconPlace at;
    const uint32_t item = g_bag_item;
    if (!item || !g_menu) return g_icon_reason = "no bag window yet", at;
    if (g_window_frame + 1 < g_frame) return g_icon_reason = "the bag window is closed", at;
    if (ld32(g_menu + kMenuWindowMode) != kDeliveryWindow) return g_icon_reason = "another bag is open", at;
    if (ld32(item + kItemNumber) != kNotebookItem || !ld8(item + kItemVisible)) return g_icon_reason = "the place shows a game item", at;
    if (!notebook_owned()) return g_icon_reason = "no notebook in this save file", at;
    const uint32_t icon = icon_pane(item);
    const uint32_t layout = ld32(item + kItemLayout), lyt = layout ? ld32(layout + kLayoutLyt) : 0;
    const uint32_t root = lyt ? ld32(lyt + kLytRootPane) : 0;
    if (!icon || !root) return g_icon_reason = "the place has no panes", at;
    st8(icon + kPaneFlags, uint8_t(ld8(icon + kPaneFlags) & ~1u));  // the game's sentinel icon stays hidden
    std::vector<PaneNode> chain;
    for (uint32_t pane = icon; pane && chain.size() < 32; pane = ld32(pane + kPaneParent))
        chain.push_back({u32_as_f32(ld32(pane + kPaneTranslate)), u32_as_f32(ld32(pane + kPaneTranslate + 4)),
                         u32_as_f32(ld32(pane + kPaneScale)), u32_as_f32(ld32(pane + kPaneScale + 4)),
                         (ld8(pane + kPaneFlags) & 1) != 0});
    at = place_icon(chain, kIconSize, ld8(root + kPaneGlobalAlpha), 854.0f, 480.0f);
    g_icon_reason = at.shown ? "shown over the notebook's bag place" : "the window is hidden or fading";
    return at;
}

// Every frame (game thread): the notebook's icon over its bag place, or nothing.
void bag_icon() {
    auto& store = guestmods::hud::store();
    static bool published = false;
    static guestmods::hud::Handle image = 0;
    static uint64_t generation = 0;
    const IconPlace at = compute_icon_place();
    if (trace()) {
        static const char* last = nullptr;
        if (g_icon_reason != last) LOG("[notebook] bag icon: %s", g_icon_reason);
        last = g_icon_reason;
    }
    if (!at.shown) {
        if (published) store.drop(kIconOwner);
        published = false;
        return;
    }
    if (!image || generation != store.state_generation()) {  // the icon image, again after a state load
        generation = store.state_generation();
        try {
            const auto png = guestmods::hud::decode_png({story_notebook_icon_png, size_t(story_notebook_icon_png_size)});
            image = store.create_image(kIconOwner, png.width, png.height, png.width * 4, png.rgba.data(), png.rgba.size());
        } catch (const std::exception&) {
            image = 0;
        }
        if (!image) return;
    }
    // The menu is on the GamePad; with the Pro Controller the game shows it on the TV (the same
    // 854 x 480 layout, scaled to the TV's 1280 x 720).
    const bool tv = input::pro_controller();
    const float k = tv ? 1280.0f / 854.0f : 1.0f;
    const guestmods::hud::Handle list = store.begin(kIconOwner, tv ? 0 : 1);
    if (!list) return;
    Command c;
    c.x = (at.x - at.size * 0.5f) * k, c.y = (at.y - at.size * 0.5f) * k, c.w = c.h = at.size * k;
    c.rgba = 0xFFFFFF00u | uint32_t(at.alpha * 255.0f + 0.5f);
    store.picture(kIconOwner, list, image, c);
    store.commit(kIconOwner, list);
    published = true;
}

void publish(const std::vector<Row>& rows) {
    auto& store = guestmods::hud::store();
    if (!g_page.open()) {
        store.drop(kOwner);
        return;
    }
    const guestmods::hud::Handle list = store.begin(kOwner, 2);  // TV and GamePad
    if (!list) return;
    for (Command& c : g_page.draw(rows)) store.append(kOwner, list, std::move(c));
    if (g_page.mode() == Page::Mode::List) {  // portraits in the rows that show them
        const uint64_t generation = store.state_generation();
        for (unsigned i = 0; i < kRowsShown && g_page.top() + i < rows.size(); i++) {
            const Quest& q = *rows[g_page.top() + i].quest;
            auto it = g_portraits.find(q.key);
            if (q.portrait.empty() || it == g_portraits.end()) continue;
            Portrait& p = it->second;
            if (!p.handle || p.generation != generation) {
                p.handle = store.create_image(kOwner, p.pixels.width, p.pixels.height, p.pixels.width * 4, p.pixels.rgba.data(),
                                              p.pixels.rgba.size());
                p.generation = generation;
            }
            if (!p.handle) continue;
            float cx, cy, r;
            Page::emblem_place(i, cx, cy, r);
            Command c;
            c.x = cx - r, c.y = cy - r, c.w = 2 * r, c.h = 2 * r;
            store.picture(kOwner, list, p.handle, c);
        }
    }
    store.commit(kOwner, list);
}

}  // namespace

IconPlace icon_place() { return compute_icon_place(); }
const char* icon_reason() { return g_icon_reason; }
void next_frame() { g_frame++; }

int32_t define(const std::string& mod, const std::string& mod_name, const std::string& quest, const std::string& title,
               const std::string& reward, const std::string& condition) {
    if (!story_mail::valid_id(quest) || title.empty() || !one_line(title, kMaxTitleBytes) || !one_line(reward, kMaxRewardBytes) ||
        !one_line(condition, kMaxConditionBytes))
        return kResultInvalid;
    const uint16_t key = quest_key(mod, quest);
    {
        std::lock_guard lock(g_mu);
        if (Quest* known_quest = find(mod, quest)) {
            known_quest->title = title, known_quest->reward = reward, known_quest->condition = condition;
        } else {
            unsigned mine = 0;
            for (const Quest& q : g_quests) {
                if (q.key == key) return kResultInvalid;  // a 16-bit key collision with another quest
                mine += q.mod == mod;
            }
            if (mine >= kMaxQuestsPerMod || g_quests.size() >= kMaxQuests) return kResultNoRoom;
            Quest q;
            q.mod = mod, q.mod_name = mod_name.empty() ? mod : mod_name, q.id = quest, q.key = key;
            q.title = title, q.reward = reward, q.condition = condition;
            g_quests.push_back(std::move(q));
            LOG("[notebook] %s: quest \"%s\" registered", mod.c_str(), quest.c_str());
        }
    }
    return saved(key);
}

int32_t step(const std::string& mod, const std::string& quest, uint32_t n, const std::string& next, const std::string& where) {
    if (!story_mail::valid_id(quest) || n > kMaxStep || !one_line(next, kMaxNextBytes) || !one_line(where, kMaxWhereBytes))
        return kResultInvalid;
    uint16_t key;
    {
        std::lock_guard lock(g_mu);
        Quest* q = find(mod, quest);
        if (!q) return kResultInvalid;  // wwhd_quest_define first
        q->next = next, q->where = where;
        key = q->key;
    }
    if (!n) return saved(key);
    if (!have_save()) return kResultNoRoom;
    story_mail::Book b = book();
    const int before = saved_step(b, key);
    const int now = reach_step(b, key, n);
    if (now < 0) {
        LOG("[notebook] %s: no room for quest \"%s\" in this save file", mod.c_str(), quest.c_str());
        return kResultNoRoom;
    }
    if (now != before) LOG("[notebook] %s: quest \"%s\" step %d reached", mod.c_str(), quest.c_str(), now);
    return now;
}

int32_t place(const std::string& mod, const std::string& quest, float x, float z) {
    if (!story_mail::valid_id(quest)) return kResultInvalid;
    if (!(x >= -story_chart::kSeaHalf && x <= story_chart::kSeaHalf && z >= -story_chart::kSeaHalf && z <= story_chart::kSeaHalf))
        return kResultInvalid;  // NaN too
    std::lock_guard lock(g_mu);
    Quest* q = find(mod, quest);
    if (!q) return kResultInvalid;
    q->has_place = true, q->x = x, q->z = z;
    return saved(q->key);
}

int32_t no_place(const std::string& mod, const std::string& quest) {
    if (!story_mail::valid_id(quest)) return kResultInvalid;
    std::lock_guard lock(g_mu);
    Quest* q = find(mod, quest);
    if (!q) return kResultInvalid;
    q->has_place = false;
    return saved(q->key);
}

int32_t known(const std::string& mod, const std::string& quest, const std::string& text) {
    if (!story_mail::valid_id(quest) || !lines(text, kMaxKnownBytes)) return kResultInvalid;
    std::lock_guard lock(g_mu);
    Quest* q = find(mod, quest);
    if (!q) return kResultInvalid;
    q->known = text;
    return saved(q->key);
}

int32_t portrait(const std::string& mod, const std::string& quest, const std::string& path, const std::string& package_dir) {
    if (!story_mail::valid_id(quest) || path.size() > kMaxPortraitBytes || (!path.starts_with("assets/") && !path.starts_with("textures/")) ||
        path.find("..") != std::string::npos)
        return kResultInvalid;
    guestmods::hud::Image pixels;
    try {
        auto png = guestmods::hud::load_png(package_dir, path);
        if (png.width > 256 || png.height > 256) return kResultInvalid;
        pixels.width = png.width, pixels.height = png.height, pixels.rgba = std::move(png.rgba);
    } catch (const std::exception&) {
        return kResultInvalid;
    }
    std::lock_guard lock(g_mu);
    Quest* q = find(mod, quest);
    if (!q) return kResultInvalid;
    if (q->portrait != path) {
        q->portrait = path;
        g_portraits[q->key] = {std::move(pixels), 0, 0};
    }
    return saved(q->key);
}

int32_t done(const std::string& mod, const std::string& quest) {
    if (!story_mail::valid_id(quest)) return kResultInvalid;
    uint16_t key;
    {
        std::lock_guard lock(g_mu);
        Quest* q = find(mod, quest);
        if (!q) return kResultInvalid;
        key = q->key;
    }
    if (!have_save()) return kResultNoRoom;
    story_mail::Book b = book();
    if (saved_step(b, key) == kStepDone) return kResultDone;
    if (finish(b, key) < 0) return kResultNoRoom;
    LOG("[notebook] %s: quest \"%s\" done", mod.c_str(), quest.c_str());
    return kResultDone;
}

int32_t state(const std::string& mod, const std::string& quest) {
    if (!story_mail::valid_id(quest)) return kResultInvalid;
    return saved(quest_key(mod, quest));
}

std::vector<Quest> quests() {
    std::lock_guard lock(g_mu);
    return g_quests;
}

std::vector<Row> rows_now(std::vector<Quest>& storage) {
    storage = quests();
    if (!have_save()) return {};
    return rows(storage, book());
}

bool notebook_owned() { return story_mail::notebook_owned(); }

bool open_page(const char* how) {
    const char* refused = !g_installed ? "the notebook is not installed" : !have_save() ? "no save file" :
                          !notebook_owned() ? "no notebook in this save file" : g_open ? "the page is already open" : nullptr;
    if (refused) {
        LOG("[notebook] page not opened (%s): %s", how, refused);
        return false;
    }
    g_page.show();
    g_open = true;
    LOG("[notebook] page opened (%s)", how);
    return true;
}
void close_page() {
    if (g_open) LOG("[notebook] page closed (state load or shutdown)");
    g_page.hide();
    g_open = false;
    guestmods::hud::store().drop(kOwner);
}
bool page_open() { return g_open.load(std::memory_order_relaxed); }

// buttons held when the page closed: the game gets them only once they are released
std::atomic<uint32_t> g_swallow{0};

void mute(input::PadState& p) {
    if (g_open.load(std::memory_order_relaxed)) {
        p = input::PadState{};
        return;
    }
    if (const uint32_t s = g_swallow.load(std::memory_order_relaxed)) p.buttons &= ~s;
}

void pad(input::PadState& p) {
    if (!g_installed) return;  // no quest mod: nothing of the notebook runs
    next_frame();
    bag_icon();
    if (!g_open.load(std::memory_order_relaxed)) {
        if (const uint32_t s = g_swallow.load(std::memory_order_relaxed)) {
            g_swallow.store(s & p.buttons, std::memory_order_relaxed);
            p.buttons &= ~s;
        }
        return;
    }
    std::vector<Quest> storage;
    std::vector<Row> rows = rows_now(storage);
    PadInput in;
    in.buttons = p.buttons, in.ly = p.ly, in.touch = p.touch;
    in.x = p.tx * 1280.0f, in.y = p.ty * 720.0f;  // the GamePad picture (854 x 480) as the page's 1280 x 720
    const Page::Action act = g_page.input(in, rows);
    if (act.kind == Page::Action::Toggle && have_save()) {
        story_mail::Book b = book();
        const bool on = toggle_tracked(b, act.key);
        LOG("[notebook] quest %04X %s", act.key, on ? "tracked: shown on the sea chart" : "no longer tracked");
        rows = rows_now(storage);
    }
    if (trace()) {
        static int last_mode = -1;
        static uint16_t last_key = 0;
        if (int(g_page.mode()) != last_mode || g_page.selected_key() != last_key)
            LOG("[notebook] page mode %d, quest %04X selected", int(g_page.mode()), g_page.selected_key());
        last_mode = int(g_page.mode()), last_key = g_page.selected_key();
    }
    publish(rows);
    if (!g_page.open()) {
        g_open = false;
        g_swallow.store(p.buttons, std::memory_order_relaxed);  // the B that closed it doesn't reach the game
        LOG("[notebook] page closed (B or the close mark)");
    }
    p = input::PadState{};  // the game sees an idle controller while the page is open
}

void note_import() { g_imported = true; }
bool imported() { return g_imported; }
bool installed() { return g_installed; }
void set_game_calls(void (*original)(Cpu*, uint32_t)) { g_original = original; }

namespace hooks {

// inventoryItem(key): the notebook in the first empty delivery place, for the bag window only
void inventory_item(Cpu* c) {
    const uint32_t key = c->r[3], lr = c->lr;
    g_original(c, kInventoryItem);
    if ((lr != kWindowBuild && lr != kWindowInit) || key < kFirstDeliveryKey || key >= kFirstDeliveryKey + kDeliveryPlaces) return;
    if ((c->r[3] & 0xFF) != kEmpty || notebook_key(c) != key) return;
    c->r[3] = kNotebookItem;
    static uint32_t last = 0;
    if (trace() && key != last) LOG("[notebook] the delivery bag shows the notebook in place %u", key - kFirstDeliveryKey);
    last = key;
}

// itemForSlot(this, item): the notebook's icon
void icon_frame(Cpu* c) {
    if (c->r[4] == kNotebookItem) {
        c->r[3] = kNotebookIcon;
        return;
    }
    g_original(c, kIconFrame);
}

// openSelectedChild(this, index): A on the notebook opens its page instead of an item caption
void open_child(Cpu* c) {
    const uint32_t menu = c->r[3], index = c->r[4];
    const uint32_t count = ld32(menu + kMenuChildren), data = ld32(menu + kMenuChildren + 4);
    const uint32_t child = data ? ld32(data + 4 * (index < count ? index : 0)) : 0;
    if (trace()) LOG("[notebook] item menu: A on place %u (item %02X)", index, child ? ld32(child + kItemNumber) : 0xFFu);
    if (child && ld32(child + kItemNumber) == kNotebookItem && open_page("A on its bag place")) return;
    g_original(c, kOpenChild);
}

// setVisible(this, visible): the game shows a place's icon pane; the notebook's own icon replaces it
void set_visible(Cpu* c) {
    const uint32_t item = c->r[3];
    g_original(c, kSetVisible);
    if (ld32(item + kItemNumber) != kNotebookItem) return;
    if (const uint32_t icon = icon_pane(item)) st8(icon + kPaneFlags, uint8_t(ld8(icon + kPaneFlags) & ~1u));
}

// executeWindow(this): the bag window runs this frame; find the notebook's place among its children
void execute_window(Cpu* c) {
    const uint32_t menu = c->r[3];
    g_original(c, kExecuteWindow);
    g_menu = menu;
    g_window_frame = g_frame;
    g_bag_item = 0;
    if (ld32(menu + kMenuWindowMode) != kDeliveryWindow) return;
    const uint32_t count = ld32(menu + kMenuChildren), data = ld32(menu + kMenuChildren + 4);
    for (uint32_t i = kFirstWindowChild; i < kFirstWindowChild + kWindowChildren && i < count && data; i++) {
        const uint32_t child = ld32(data + 4 * i);
        if (child && ld32(child + kItemNumber) == kNotebookItem) g_bag_item = child;
    }
}

// beginCursorSelection(this, point): X/Y/R on the notebook would lift it off its place towards the
// button, which it can't go on: it stays where it is
void cursor_float(Cpu* c) {
    if (ld32(c->r[3] + kItemNumber) == kNotebookItem) {
        if (trace()) LOG("[notebook] item menu: the notebook can't go on X, Y or R");
        return;
    }
    g_original(c, kCursorFloat);
}

// selectTouchEnter(this): touching the notebook opens its page (the GamePad has no A on its items);
// the game's own pick-up runs first and is put back as the touch ends (the page keeps the touch)
void touch_select(Cpu* c) {
    const uint32_t item = c->r[3];
    g_original(c, kTouchSelect);
    if (trace()) LOG("[notebook] item menu: touch on item %02X", ld32(item + kItemNumber));
    if (ld32(item + kItemNumber) == kNotebookItem) open_page("a touch on its bag place");
}

// getSelectedItemValue(this, receiver): the notebook can't go on X, Y or R
void selected_value(Cpu* c) {
    g_original(c, kSelectedValue);
    const uint32_t key = c->r[3];
    if (key >= kFirstDeliveryKey && key < kFirstDeliveryKey + kDeliveryPlaces && notebook_key(c) == key) c->r[3] = kEmpty;
}

// the game system's X/Y/R assignment (controller, button, key): never the notebook's place
void assign_button(Cpu* c) {
    const uint32_t key = c->r[5];
    if (key >= kFirstDeliveryKey && key < kFirstDeliveryKey + kDeliveryPlaces && notebook_key(c) == key) return;
    g_original(c, kAssignButton);
}

}  // namespace hooks

void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    if (!g_imported || g_installed || !g_original) return;
    const std::pair<uint32_t, void (*)(Cpu*)> list[] = {
        {kInventoryItem, hooks::inventory_item}, {kIconFrame, hooks::icon_frame},     {kOpenChild, hooks::open_child},
        {kTouchSelect, hooks::touch_select},     {kSelectedValue, hooks::selected_value}, {kAssignButton, hooks::assign_button},
        {kSetVisible, hooks::set_visible},       {kExecuteWindow, hooks::execute_window}, {kCursorFloat, hooks::cursor_float}};
    // all or nothing: a notebook that shows but can't be opened, or can be put on a button, is worse than none
    for (const auto& [addr, fn] : list) {
        std::string other = replaced_by(addr);
        if (!other.empty()) {
            LOG("[notebook] %s replaces %08X: the story notebook is not in the item menu this session", other.c_str(), addr);
            return;
        }
    }
    for (const auto& [addr, fn] : list) replace(addr, fn);
    story_chart::set_tracked_source(tracked_marker);
    g_installed = true;
    LOG("[notebook] the story notebook is in the Delivery Bag");
}

void state_loaded() {
    close_page();
    g_bag_item = 0, g_menu = 0, g_window_frame = 0;
    std::lock_guard lock(g_mu);
    for (auto& [key, p] : g_portraits) p.handle = 0;
}

}  // namespace story_notebook
