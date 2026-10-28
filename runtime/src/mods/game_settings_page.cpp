// The game-style settings page in the item menu: the driver (game_settings_page.h is the page).
//
// Where it runs: VPADRead (hle/system_stubs.cpp) hands every GamePad read to pad() after the story
// notebook and the Delivery Bag pages; KPADReadEx hands the Pro Controller's reads to mute(). While
// the page is open the game gets an idle controller (as with the notebook), and buttons held when it
// closes reach the game only after their release.
//
// When it is offered: the in-game UI (hdIngameUi_c, sIngameUi) is in its Menu state (the + menu
// stands still: not opening, not in a sub window, not saving) and no bag window is open. The page is
// drawn on the screen with the item menu (the GamePad, or the TV with the Pro Controller) through the
// HUD layer; its text and boxes use the game's menu font and window art, read from the running game
// (story_notebook_game.cpp's services), never stored.
//
// Prototype limits (p9 report): the entry is ZL or the arrow (not yet "left past the first column",
// which needs the item cursor), the page does not know the items page from the second page, and its
// look code is a copy of the notebook's (to be shared).
#include "game_settings_page.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>

#include "game_settings.h"
#include "guest_hud.h"
#include "story_notebook_game.h"
#include "story_notebook.h"
#include "story_notebook_page.h"  // menu_screen, fit_to_screen
#include "guest_addr.h"
#include "input.h"
#include "runtime.h"
#include "screenshot.h"

namespace game_settings_page {
namespace {

namespace font = story_notebook::font;
namespace style = story_notebook::style;
using guestmods::hud::Handle;

const std::string kOwner = "port:game-settings";
const uint32_t kIngameUi = GD(0x101F8344);       // sIngameUi (hdIngameUi_c*)
const uint32_t kStateMenu = GD(0x1049ECB0);      // l_ingameMenuState (::StateID_Menu)
const uint32_t kPlay = GD(0x1046F0B0);           // g_dComIfG_gameInfo.play
constexpr uint32_t kUiCurrentState = 0x18, kUiPendingState = 0x14, kUiItemScreen = 0x17C, kMenuWindowMode = 0x54;
constexpr uint32_t kPlayEventRun = 0x5292;

bool trace() {
    static const bool on = getenv("WWHD_GAME_SETTINGS_TRACE") != nullptr;
    return on;
}
bool object(uint32_t p, uint32_t size) { return (p & 3) == 0 && p >= mem::kMem2Start && p <= mem::kMem2End - size; }

struct Menu {
    uint32_t ui = 0, descriptor = 0, items = 0, window = ~0u;
    bool items_page = false;  // the + menu stands still with no bag window
};
Menu read_menu() {
    Menu m;
    m.ui = ld32(kIngameUi);
    if (!object(m.ui, 0x24C)) return m;
    const uint32_t current = ld32(m.ui + kUiCurrentState);
    m.descriptor = object(current, 12) ? ld32(current + 8) : ld32(m.ui + kUiPendingState);
    m.items = ld32(m.ui + kUiItemScreen);
    m.window = object(m.items, 0x58) ? ld32(m.items + kMenuWindowMode) : ~0u;
    m.items_page = m.descriptor == kStateMenu && m.window >= 3 && m.window != ~0u;
    return m;
}

// ---- the look: the game's fonts and menu art (a copy of the notebook's, story_notebook.cpp) ----
struct Look {
    font::Face main, fallback, icons;
    bool font_failed = false, icons_probed = false;
    font::Atlas atlas;
    Handle atlas_image = 0;
    uint64_t atlas_generation = 0, store_generation = 0, fonts_key = 0;
    style::Art art;
    uint32_t art_menu = 0;
    bool art_ok = false;
    struct Box { Handle handle = 0; uint64_t generation = 0; std::shared_ptr<guestmods::hud::Image> image; };
    std::map<std::string, Box> boxes;
    font::Prompts prompts;
} g_look;
constexpr float kFontScale = 1.05f, kAtlasScale = 0.62f, kBoxResolution = 0.75f;

float game_measure(const std::string& text, float size) {
    const int h = g_look.main.cell_height;
    return h ? float(font::Atlas::line_width(text, g_look.main, &g_look.fallback, h)) * size * kFontScale / float(h) : 0.0f;
}

void load_glyphs(Cpu* c, const std::vector<std::string>& lines) {
    const auto& ui = story_notebook::game_ui();
    if (!ui.glyphs || !c || g_look.font_failed) return;
    std::vector<uint32_t> want;
    for (const std::string& l : lines)
        for (uint32_t cp : font::codes(l))
            if (!font::Atlas::button(cp) && !font::style_code(cp) && !g_look.main.glyphs.count(cp) && !g_look.main.missing.count(cp) &&
                std::find(want.begin(), want.end(), cp) == want.end())
                want.push_back(cp);
    if (!g_look.main.cell_height) want.push_back('?');
    if (!want.empty()) {
        const size_t before = g_look.main.glyphs.size();
        if (!ui.glyphs(c, 0, want, g_look.main) || !g_look.main.ready()) {
            if (trace()) LOG("[game-settings] the game's menu font is not readable yet");
            return;  // (the font loads with the game's UI; tried again next frame)
        }
        std::vector<uint32_t> lacking;
        for (uint32_t cp : want)
            if (g_look.main.missing.count(cp) && !g_look.fallback.glyphs.count(cp) && !g_look.fallback.missing.count(cp)) lacking.push_back(cp);
        if (!lacking.empty()) ui.glyphs(c, 1, lacking, g_look.fallback);  // the message font for what the menu font lacks
        if (g_look.main.glyphs.size() != before || !lacking.empty()) g_look.fonts_key++;
        if (before == 0) LOG("[game-settings] the page uses the game's menu font (cell height %d)", g_look.main.cell_height);
    }
    if (!g_look.icons_probed) {
        g_look.icons_probed = true;
        for (int slot : {1, 4, 0}) {
            font::Face f;
            if (ui.glyphs(c, slot, {font::kButtonA, font::kButtonB, font::kButtonA + 2, font::kButtonA + 3}, f) && f.glyphs.count(font::kButtonA) &&
                f.glyphs.count(font::kButtonB)) {
                g_look.icons = std::move(f);
                g_look.fonts_key++;
                break;
            }
        }
    }
}

void load_art(uint32_t menu) {
    const auto& ui = story_notebook::game_ui();
    if (!ui.art || !menu || menu == g_look.art_menu) return;
    g_look.art_menu = menu;  // once per item screen (the game builds a new one on a stage change)
    std::string why;
    g_look.art_ok = ui.art(menu, g_look.art, why);
    if (g_look.art_ok) style::trim_plates(g_look.art);
    g_look.boxes.clear();
    LOG("[game-settings] menu art %s%s%s", g_look.art_ok ? "read" : "not read (plain boxes)", why.empty() ? "" : ": ", why.c_str());
}

std::vector<Command> render(std::vector<Command> cmds, std::vector<Handle>& images, uint32_t screen) {
    auto& store = guestmods::hud::store();
    const uint64_t generation = store.state_generation();
    cmds = style::resolve(cmds, g_look.art_ok ? &g_look.art : nullptr);
    const bool have_font = g_look.main.ready();
    const float pixels = store.pixels(screen) * (screen == 1 ? 854.0f / 1280.0f : 1.0f);
    if (have_font) {
        const std::vector<std::string> lines = font::lines_of(cmds, kFontScale, pixels);
        if (!g_look.atlas.holds(lines, g_look.prompts, g_look.fonts_key)) {
            g_look.atlas.build(lines, g_look.main, &g_look.fallback, g_look.icons.ready() ? &g_look.icons : nullptr, g_look.prompts, g_look.fonts_key,
                               kAtlasScale);
            if (g_look.atlas_image) store.release(kOwner, g_look.atlas_image);
            g_look.atlas_image = 0;
        }
        if (!g_look.atlas_image || g_look.store_generation != generation) {
            const auto img = g_look.atlas.image();
            g_look.atlas_image = img ? store.create_image(kOwner, img->width, img->height, img->width * 4, img->rgba.data(), img->rgba.size()) : 0;
            g_look.store_generation = generation;
        }
    }
    cmds = font::to_pictures(cmds, have_font && g_look.atlas_image ? &g_look.atlas : nullptr, kFontScale, pixels);
    images.assign(cmds.size(), 0);
    for (size_t i = 0; i < cmds.size(); i++) {
        Command& c = cmds[i];
        if (c.kind != Command::Picture || c.image) continue;
        if (c.text.rfind("art:", 0) != 0) {
            images[i] = g_look.atlas_image;
            c.text.clear();
            continue;
        }
        int bw, bh;
        style::box_size(c.text, bw, bh);
        const std::string name = "nb:" + style::box_name(c.text).substr(4);
        const int w = int(std::lround(bw * kBoxResolution)), h = int(std::lround(bh * kBoxResolution));
        const std::string key = name + ":" + std::to_string(w) + "x" + std::to_string(h);
        Look::Box& box = g_look.boxes[key];
        if (!box.image) box.image = style::compose(g_look.art, name, w, h);
        if (box.image && (!box.handle || box.generation != generation)) {
            box.handle = store.create_image(kOwner, box.image->width, box.image->height, box.image->width * 4, box.image->rgba.data(),
                                            box.image->rgba.size());
            box.generation = generation;
        }
        images[i] = box.handle;
        c.text.clear();
    }
    return cmds;
}

void publish(std::vector<Command> cmds, Cpu* c) {
    auto& store = guestmods::hud::store();
    if (cmds.empty()) {
        store.drop(kOwner);
        return;
    }
    load_glyphs(c, font::texts_of(cmds));
    const uint32_t screen = story_notebook::menu_screen(input::pro_controller());
    const Handle list = store.begin(kOwner, screen);
    if (!list) return;
    std::vector<Handle> images;
    cmds = render(std::move(cmds), images, screen);
    story_notebook::fit_to_screen(cmds, screen);
    for (size_t i = 0; i < cmds.size(); i++) {
        if (cmds[i].kind == Command::Picture) {
            if (images[i]) store.picture(kOwner, list, images[i], std::move(cmds[i]));
            continue;
        }
        store.append(kOwner, list, std::move(cmds[i]));
    }
    store.commit(kOwner, list);
}

const char* sound_label(Sound s) {
    switch (s) {
    case Sound::Cursor: return "SE_MENU_CURSOR";
    case Sound::Change: return "SE_MENU_OPTION_SWITCH";
    case Sound::Decide: return "SE_MENU_DECIDE_S";
    case Sound::Cancel: case Sound::Blocked: return "SE_MENU_CANCEL";
    case Sound::Tab: return "SE_MENU_TAB_SWITCH";
    case Sound::Open: return "SE_MENU_OPTION_IN";
    case Sound::Close: return "SE_MENU_OPTION_OUT";
    case Sound::None: break;
    }
    return nullptr;
}
void play(Cpu* c, Sound s) {
    const char* label = sound_label(s);
    const auto& ui = story_notebook::game_ui();
    if (!label || !c || !ui.sound || ld8(kPlay + kPlayEventRun)) return;
    if (trace()) LOG("[game-settings] sound %s", label);
    ui.sound(c, label);
}

Page g_page;
std::atomic<bool> g_has_controller{false};  // the page (or its slide out) has the controller
std::atomic<uint32_t> g_swallow{0};

float frame_seconds() {
    using clock = std::chrono::steady_clock;
    static clock::time_point last{};
    const clock::time_point now = clock::now();
    const float dt = last == clock::time_point{} ? 0.0f : std::chrono::duration<float>(now - last).count();
    last = now;
    return std::min(dt, 0.1f);
}

std::vector<Row> rows_of(const std::string& tab) {
    std::vector<Row> rows;
    for (const game_settings::Item* it : game_settings::items(tab)) {
        Row r;
        r.id = it->id, r.label = it->label, r.help = it->help, r.value = game_settings::value_text(*it);
        r.number = it->kind == game_settings::Kind::Number, r.toggle = it->kind == game_settings::Kind::Toggle;
        r.on = r.toggle && r.value == "On";
        r.enabled = !it->enabled || it->enabled();
        rows.push_back(std::move(r));
    }
    return rows;
}

// Test aid WWHD_TEST_GAME_SETTINGS_SHOTS=1: a screenshot (screenshot.h; the GamePad picture too with
// WWHD_SCREENSHOT_GAMEPAD=1) a few reads after the entry arrow first shows, the page opens and each change
void shot_after(int reads) {
    static const bool on = getenv("WWHD_TEST_GAME_SETTINGS_SHOTS") != nullptr;
    static int countdown = -1;
    if (!on) return;
    if (reads > 0) { countdown = reads; return; }
    if (countdown > 0 && --countdown == 0) {
        screenshot::request();
        LOG("[game-settings] test screenshot requested");
    }
}

}  // namespace

bool page_open() { return g_has_controller.load(std::memory_order_relaxed); }

void mute(input::PadState& p) {
    if (g_has_controller.load(std::memory_order_relaxed)) {
        p = input::PadState{};
        return;
    }
    if (const uint32_t s = g_swallow.load(std::memory_order_relaxed)) p.buttons &= ~s;
}

void pad(input::PadState& p, Cpu* c) {
    static bool disabled = getenv("WWHD_GAME_SETTINGS") && *getenv("WWHD_GAME_SETTINGS") == '0';
    if (disabled) return;
    game_settings::register_builtin();
    g_page.measure = g_look.main.ready() ? game_measure : nullptr;
    const float dt = frame_seconds();
    shot_after(0);
    const Menu m = read_menu();
    if (trace()) {
        static uint32_t last_descriptor = ~0u, last_window = ~1u;
        if (m.descriptor != last_descriptor || m.window != last_window)
            LOG("[game-settings] in-game UI %08X: state %08X%s, bag window %d -> %s", m.ui, m.descriptor, m.descriptor == kStateMenu ? " (Menu)" : "",
                int(m.window), m.items_page ? "the page is offered" : "not offered");
        last_descriptor = m.descriptor, last_window = m.window;
    }
    const bool other_page = story_notebook::page_open();
    const std::vector<std::string> tabs = game_settings::tabs();
    std::vector<std::string> labels;
    for (const std::string& t : tabs) labels.push_back(game_settings::tab_label(t));
    Input in;
    in.buttons = p.buttons, in.lx = p.lx, in.ly = p.ly, in.touch = p.touch, in.dt = dt;
    in.x = p.tx * 1280.0f, in.y = p.ty * 720.0f;  // the GamePad picture as the page's 1280 x 720
    if (!g_page.open()) {
        g_page.tick(dt);
        if (const uint32_t s = g_swallow.load(std::memory_order_relaxed)) {
            g_swallow.store(s & p.buttons, std::memory_order_relaxed);
            p.buttons &= ~s;
        }
        if (g_page.shown()) {  // sliding out
            const std::string tab = tabs.empty() ? "" : tabs[size_t(std::min<int>(g_page.tab(), int(tabs.size()) - 1))];
            publish(g_page.draw(rows_of(tab), labels), c);
            p = input::PadState{};
            return;
        }
        g_has_controller.store(false, std::memory_order_relaxed);
        if (!m.items_page || other_page || tabs.empty()) {
            g_page.entry(in);  // (follows the buttons: a ZL held into the menu does not open it)
            publish({}, c);
            return;
        }
        load_art(m.items);
        if (story_notebook::game_ui().prompts) g_look.prompts = story_notebook::game_ui().prompts();
        if (g_page.entry(in)) {
            g_page.show(int(tabs.size()));
            g_has_controller.store(true, std::memory_order_relaxed);
            LOG("[game-settings] page opened (%s)", (p.buttons & kPadZL) ? "ZL" : "the arrow");
            play(c, Sound::Open);
            shot_after(20);
            g_swallow.store(0, std::memory_order_relaxed);
            p = input::PadState{};
        } else {
            static bool shown_once = false;
            if (!shown_once) shown_once = true, shot_after(20);
            publish(g_page.draw_entry(), c);
        }
        return;
    }
    // open: the page has the controller
    g_has_controller.store(true, std::memory_order_relaxed);
    if (!m.items_page && m.descriptor != kStateMenu) {  // the menu closed under the page (a state load, a cutscene)
        g_page.hide();
        LOG("[game-settings] page closed: the menu is gone");
    }
    const std::string tab = tabs.empty() ? "" : tabs[size_t(std::min<int>(g_page.tab(), int(tabs.size()) - 1))];
    std::vector<Row> rows = rows_of(tab);
    const Action act = g_page.input(in, rows, int(tabs.size()));
    g_page.tick(dt);
    if (!act.id.empty())
        if (const game_settings::Item* it = game_settings::find(act.id)) {
            const float v = game_settings::apply(*it, act.direction);
            LOG("[game-settings] %s -> %g", act.id.c_str(), double(v));
            rows = rows_of(tab);
            shot_after(10);
        }
    play(c, act.sound);
    if (act.closed) {
        g_swallow.store(p.buttons, std::memory_order_relaxed);  // the B that closed it doesn't reach the game
        LOG("[game-settings] page closed");
    }
    publish(g_page.draw(rows, labels), c);
    p = input::PadState{};
}

}  // namespace game_settings_page
