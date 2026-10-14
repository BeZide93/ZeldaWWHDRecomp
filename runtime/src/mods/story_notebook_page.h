// The story notebook's page: what it shows and how the player moves through it (docs/mod-sdk-v2.md,
// "The story notebook"). Pure code: input in, draw commands out (the port's HUD layer draws them on
// the TV and the GamePad); unit-tested in runtime/tools/story_notebook_test.cpp.
//
// The page looks and feels like the game's own menu windows (the item menu's mini windows, such as
// "Would you like to save?"): a window with a paper panel, text in the game's menu font, button
// plates with the game's A/B button pictures, the game's cursor (four corner brackets that breathe
// around what A acts on), and the menu's sounds. Coordinates are those of a 1280 x 720 screen,
// centered on wider screens.
//
//   List:   one row per quest (story_notebook_list.h order): the mod's emblem or the quest's portrait,
//           title and mod name, the next step, where, and the status (New, In progress, Not yet
//           available: condition, Done with a stamp). The tracked quest carries the chart ring.
//           Up/Down choose, A opens the quest, Left/Right the tabs (Quests, Archive), B closes.
//   Detail: the quest: next step and where (or "no place on the chart"), what is known so far, the
//           reward, and "Show on chart" (A): the one tracked quest; choosing it again untracks it.
//           B goes back to the list.
// Touch (and the mouse on the GamePad picture) taps rows, tabs and plates; the keyboard acts as the
// controller through the port's controls.
//
// Boxes are drawn as style markers (a Picture command without an image whose text names the box:
// "nb:window", "nb:panel", "nb:plate", "nb:plate-on", "nb:cursor", "nb:tab", "nb:tab-on", then
// "@<w>x<h>", the size it was laid out at):
// story_notebook_style.h turns them into the game's window art or into plain shapes. Text commands
// are drawn in the game's font (story_notebook_font.h); the button pictures are the Text of the
// game's icon characters (U+E000 A, U+E001 B).
//
// Feel (tick(dt), seconds): the window opens with a quick scale-and-fade, switching tabs or opening a
// quest turns the page (the content slides in from the side it comes from), the cursor glides to the
// new row and breathes, A/B press their plates and prompts down. Action::sound names the menu sound of
// what the input did (story_notebook.cpp plays the game's own). Without ticks the page is at rest.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "guest_hud.h"
#include "story_notebook_list.h"

namespace story_notebook {

using guestmods::hud::Command;

// the controller as the page reads it (VPAD bits as input.h; touch in page coordinates)
struct PadInput {
    uint32_t buttons = 0;
    float ly = 0;
    bool touch = false;
    float x = 0, y = 0;
};
inline constexpr uint32_t kPadA = 0x8000, kPadB = 0x4000, kPadUp = 0x0200, kPadDown = 0x0100, kPadLeft = 0x0800, kPadRight = 0x0400;

// page geometry (1280 x 720)
inline constexpr float kPageX = 150, kPageY = 44, kPageW = 980, kPageH = 632;
inline constexpr float kListX = kPageX + 70, kListY = kPageY + 108, kRowH = 88, kListW = kPageW - 130;
inline constexpr unsigned kRowsShown = 5;
// plates of the detail view and the close mark
inline constexpr float kTrackX = kPageX + kPageW - 380, kTrackY = kPageY + kPageH - 92, kTrackW = 320, kTrackH = 58;
inline constexpr float kBackX = kPageX + 60, kBackY = kTrackY, kBackW = 190, kBackH = 58;
inline constexpr float kCloseX = kPageX + kPageW - 46, kCloseY = kPageY + 46, kCloseR = 22;
// the two tabs: the quests (active, new, not yet available) and the archive (done)
inline constexpr float kTabY = kPageY + 34, kTabH = 48, kTabW = 180, kTab0X = kPageX + kPageW - 490, kTab1X = kTab0X + kTabW + 12;

// colours (RGBA): the game's menu window (green paper, dark green ink) and the status marks
inline constexpr uint32_t kShade = 0x0A140A88, kInk = 0x003800FF, kInkSoft = 0x3C6B2EFF, kInkLight = 0x5E7F4AFF, kPlateInk = 0x0B3D06FF,
                          kNew = 0x2E8A2EFF, kProgress = 0x2C6CA8FF, kLocked = 0x7C8A6CFF, kStamp = 0xB2392BFF, kRing = 0xFFFFFFFF,
                          kRingBack = 0xD9822BFF, kRule = 0xA9D08AFF, kWhite = 0xFFFFFFFF, kSeal = 0xF2E4C2FF;

// Text measure: the game's font when it is loaded (story_notebook.cpp sets it), else an average
// advance of the overlay font (as a share of the size; generous).
inline constexpr float kAdvance = 0.40f;
inline float (*g_text_measure)(const std::string& text, float size) = nullptr;
// A text that starts with kBold is drawn in the game's bordered menu style (titles, tabs, plates;
// story_notebook_font.h), others in its plain letters.
// kMessage: in the game's message font (story_notebook_font.h).
inline const std::string kBold = "\xEE\x83\xB1", kMessage = "\xEE\x83\xB2";  // U+E0F1, U+E0F2
inline std::string bold(const std::string& s) { return kBold + s; }
inline std::string message(const std::string& s) { return kMessage + s; }
inline float text_width(const std::string& s, float size) {
    if (g_text_measure) return g_text_measure(s, size);
    size_t n = 0, skip = 0;
    while (s.compare(skip, 3, kBold) == 0 || s.compare(skip, 3, kMessage) == 0) skip += 3;
    for (size_t i = skip; i < s.size(); i++) n += (s[i] & 0xC0) != 0x80;
    return float(n) * size * kAdvance;
}
inline size_t char_bytes(unsigned char c) { return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; }

// One line of text cut to the width, with "...". `style`: the style code it is drawn with (kMessage:
// measured in that font).
inline std::string fit(const std::string& text, float size, float width, const std::string& style = "") {
    if (text_width(style + text, size) <= width) return text;
    std::string best;
    for (size_t i = 0; i < text.size();) {
        const size_t next = std::min(text.size(), i + char_bytes((unsigned char)text[i]));
        if (text_width(style + text.substr(0, next) + "...", size) > width) break;
        best = text.substr(0, next);
        i = next;
    }
    return best + "...";
}

// Text broken into lines of the width at spaces (and at the mod's own line breaks), at most `lines`.
inline std::vector<std::string> wrap(const std::string& text, float size, float width, unsigned lines, const std::string& style = "") {
    std::vector<std::string> out;
    std::string line;
    bool any = false;
    auto flush = [&] {
        out.push_back(line);
        line.clear();
        any = false;
    };
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find_first_of(" \n", start);
        if (end == std::string::npos) end = text.size();
        const std::string word = text.substr(start, end - start);
        if (any && text_width(style + line + " " + word, size) > width) flush();
        if (any) line += ' ';
        line += word;
        any = true;
        if (end < text.size() && text[end] == '\n') flush();
        start = end + 1;
    }
    if (any || out.empty()) flush();
    if (out.size() > lines) {
        out.resize(lines);
        out.back() = fit(out.back() + " ...........", size, width, style);
    }
    return out;
}

inline const char* status_text(Status s) {
    switch (s) {
    case Status::InProgress: return "In progress";
    case Status::New: return "New";
    case Status::NotAvailable: return "Not yet available";
    case Status::Done: return "Done";
    }
    return "";
}

// The mod's emblem colour: a muted ink, the same for all quests of a mod.
inline uint32_t emblem_colour(const std::string& mod) {
    static constexpr uint32_t kInks[] = {0x7D3B2EFF, 0x2E5E6EFF, 0x5B6B2EFF, 0x6A3F72FF, 0x8A5A1EFF, 0x34507EFF};
    return kInks[story_mail::letter_key(mod, "") % (sizeof kInks / sizeof kInks[0])];
}

// The screen that shows the game's item menu, where the page (and the bag icon) belong: the GamePad,
// or the TV when the Pro Controller has the game show its menus there. HUD screens: 0 TV, 1 GamePad.
inline uint32_t menu_screen(bool pro_controller) { return pro_controller ? 0u : 1u; }
// The page is laid out for 1280 x 720; the GamePad's HUD lists are 854 x 480.
inline void fit_to_screen(std::vector<Command>& cmds, uint32_t screen) {
    if (screen != 1) return;
    const float k = 854.0f / 1280.0f;
    for (Command& c : cmds) {
        c.x *= k, c.y *= k, c.size *= k, c.thickness *= k;
        if (c.kind != Command::Circle && c.kind != Command::CircleOutline) c.w *= k, c.h *= k;
    }
}

// Scale draw commands (from index `from`) around a point, move them and fade them (the animations).
inline void transform(std::vector<Command>& cmds, size_t from, float cx, float cy, float scale, float dx, float dy, float alpha) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    for (size_t i = from; i < cmds.size(); i++) {
        Command& c = cmds[i];
        c.x = cx + (c.x - cx) * scale + dx, c.y = cy + (c.y - cy) * scale + dy;
        c.size *= scale, c.thickness = std::max(0.01f, c.thickness * scale);
        if (c.kind != Command::Circle && c.kind != Command::CircleOutline) c.w *= scale, c.h *= scale;
        c.rgba = (c.rgba & 0xFFFFFF00u) | uint32_t(std::lround(float(c.rgba & 0xFF) * alpha));
    }
}

// The menu sound of an input (story_notebook.cpp plays the game's own: SE_MENU_CURSOR, ...)
enum class Sound : uint8_t { None, Cursor, Decide, Cancel, Tab, Open, Close, TrackOn, TrackOff };

class Page {
public:
    enum class Mode : uint8_t { Closed, List, Detail };
    struct Action {
        enum Kind : uint8_t { None, Close, Toggle } kind = None;
        uint16_t key = 0;  // Toggle: the quest
        Sound sound = Sound::None;
    };

    Mode mode() const { return mode_; }
    int tab() const { return tab_; }
    // the rows of the open tab: the quests (tracked first) or the archive (done quests)
    std::vector<Row> shown(const std::vector<Row>& rows) const {
        std::vector<Row> out;
        for (const Row& r : rows)
            if ((r.status == Status::Done) == (tab_ == 1)) out.push_back(r);
        return out;
    }
    bool open() const { return mode_ != Mode::Closed; }
    uint16_t selected_key() const { return selected_; }
    void show() {
        mode_ = Mode::List;
        tab_ = 0;
        selected_ = 0;
        top_ = 0;
        last_ = ~0u;  // buttons held when the page opens (the A that opened it) do nothing
        touched_ = true;
        opening_ = 0;  // the window opens (tick)
        turn_ = 1, turn_dir_ = 0;
        press_a_ = press_b_ = bump_ = glide_ = 1;
    }
    void hide() { mode_ = Mode::Closed; }

    // Time for the animations (seconds since the last call).
    void tick(float dt) {
        dt = std::clamp(dt, 0.0f, 0.1f);
        time_ += dt;
        opening_ = std::min(1.0f, opening_ + dt / kOpenTime);
        turn_ = std::min(1.0f, turn_ + dt / kTurnTime);
        press_a_ = std::min(1.0f, press_a_ + dt / kPressTime);
        press_b_ = std::min(1.0f, press_b_ + dt / kPressTime);
        bump_ = std::min(1.0f, bump_ + dt / kBumpTime);
        glide_ = std::min(1.0f, glide_ + dt / kGlideTime);
        ticked_ = true;
    }
    float opening() const { return ticked_ ? opening_ : 1.0f; }

    // One game frame of input. Returns what the notebook has to do (Close: the page closed itself)
    // and the menu sound of what happened.
    Action input(const PadInput& pad, const std::vector<Row>& all) {
        Action act;
        if (mode_ == Mode::Closed) return act;
        const std::vector<Row> rows = shown(all);
        uint32_t held = pad.buttons;
        if (pad.ly > 0.5f) held |= kPadUp;
        if (pad.ly < -0.5f) held |= kPadDown;
        if (last_ == ~0u) last_ = held;
        const uint32_t pressed = held & ~last_;
        // Up/Down repeat while held, like the game's cursors
        uint32_t moved = pressed & (kPadUp | kPadDown);
        if (held & (kPadUp | kPadDown) && !(pressed & (kPadUp | kPadDown))) {
            if (++repeat_ >= 18 && (repeat_ - 18) % 5 == 0) moved = held & (kPadUp | kPadDown);
        } else {
            repeat_ = 0;
        }
        last_ = held;
        const bool tap = pad.touch && !touched_;
        touched_ = pad.touch;
        if (pressed & kPadA) press_a_ = 0;
        if (pressed & kPadB) press_b_ = 0;
        int index = index_of(rows);
        if (mode_ == Mode::List) {
            // Left/Right or a tap on a tab: the quests or the archive
            int want = tab_;
            if (pressed & kPadLeft) want = 0;
            if (pressed & kPadRight) want = 1;
            if (tap && inside(pad.x, pad.y, kTab0X, kTabY, kTabW, kTabH)) want = 0;
            if (tap && inside(pad.x, pad.y, kTab1X, kTabY, kTabW, kTabH)) want = 1;
            if (want != tab_) {
                turn(want > tab_ ? 1 : -1);
                tab_ = want;
                top_ = 0;
                const std::vector<Row> now = shown(all);
                selected_ = now.empty() ? 0 : now[0].quest->key;
                act.sound = Sound::Tab;
                return act;
            }
            if (rows.empty()) index = -1;
            else if (index < 0) index = 0;
            const int before = index;
            const float before_y = row_y(before);
            if (index >= 0 && (moved & kPadUp) && index > 0) index--;
            if (index >= 0 && (moved & kPadDown) && index + 1 < int(rows.size())) index++;
            if (tap && hit_close(pad.x, pad.y)) {
                hide();
                act.kind = Action::Close;
                act.sound = Sound::Close;
                return act;
            }
            int tapped = tap ? row_at(pad.x, pad.y, rows.size()) : -1;
            if (tapped >= 0) index = tapped;
            if (index >= 0) {
                selected_ = rows[size_t(index)].quest->key;
                if (unsigned(index) < top_) top_ = unsigned(index);
                if (unsigned(index) >= top_ + kRowsShown) top_ = unsigned(index) - kRowsShown + 1;
            }
            if (index != before && before >= 0) {
                act.sound = Sound::Cursor;
                bump_ = 0, glide_ = 0, glide_from_ = before_y;
            }
            if (index >= 0 && ((pressed & kPadA) || tapped >= 0)) {
                mode_ = Mode::Detail;
                turn(1);
                if (tapped >= 0) press_a_ = 0;
                act.sound = Sound::Decide;
            } else if (pressed & kPadB) {
                hide();
                act.kind = Action::Close;
                act.sound = Sound::Close;
            }
        } else {  // Detail
            if (index < 0) {  // the quest is gone (its mod stopped registering it)
                mode_ = Mode::List;
                return act;
            }
            const Row& r = rows[size_t(index)];
            const bool trackable = r.status != Status::Done;
            const bool back_tap = tap && inside(pad.x, pad.y, kBackX, kBackY, kBackW, kBackH);
            const bool track_tap = tap && inside(pad.x, pad.y, kTrackX, kTrackY, kTrackW, kTrackH);
            if (tap && hit_close(pad.x, pad.y)) {
                hide();
                act.kind = Action::Close;
                act.sound = Sound::Close;
            } else if ((pressed & kPadB) || back_tap) {
                mode_ = Mode::List;
                turn(-1);
                if (back_tap) press_b_ = 0;
                act.sound = Sound::Cancel;
            } else if (trackable && ((pressed & kPadA) || track_tap)) {
                act.kind = Action::Toggle;
                act.key = r.quest->key;
                if (track_tap) press_a_ = 0;
                act.sound = r.tracked ? Sound::TrackOff : Sound::TrackOn;
            }
        }
        return act;
    }

    // The page as draw commands (empty when closed). The caller draws the portraits with its own
    // picture commands at emblem_place() (moved by place_portrait()).
    std::vector<Command> draw(const std::vector<Row>& all) const {
        std::vector<Command> out;
        if (mode_ == Mode::Closed) return out;
        const float fade = std::min(1.0f, opening() * 1.6f);
        out.push_back(rect(-400, -200, 2080, 1120, (kShade & 0xFFFFFF00u) | uint32_t(float(kShade & 0xFF) * fade)));  // the game behind, dimmed
        const size_t window_from = out.size();
        const std::vector<Row> rows = shown(all);
        sheet(out);
        if (mode_ == Mode::List) tabs(out, all);
        // the content turns like a page: it slides in from the side it comes from
        const size_t content_from = out.size();
        const int index = index_of(rows);
        if (mode_ == Mode::Detail && index >= 0) detail(out, rows[size_t(index)]);
        else list(out, rows, index);
        const float t = turned();
        if (t < 1.0f) transform(out, content_from, 0, 0, 1.0f, float(turn_dir_) * (1.0f - t) * kTurnSlide, 0, t);
        // the window opens: scales up from 92 % with a little overshoot, fading in
        transform(out, window_from, kPageX + kPageW / 2, kPageY + kPageH / 2, window_scale(), 0, 0, fade);
        return out;
    }

    // where a row's emblem goes (for the caller's portrait pictures): center and radius
    static void emblem_place(unsigned shown_index, float& cx, float& cy, float& r) {
        cx = kListX + 50;
        cy = kListY + kRowH * float(shown_index) + kRowH * 0.5f;
        r = 32;
    }
    unsigned top() const { return top_; }
    // a portrait at (x, y, size) follows the page's animation: the content's slide and the window's scale
    void place_portrait(float& x, float& y, float& size, float& alpha) const {
        const float t = turned();
        x += float(turn_dir_) * (1.0f - t) * kTurnSlide;
        const float s = window_scale(), cx = kPageX + kPageW / 2, cy = kPageY + kPageH / 2;
        x = cx + (x - cx) * s, y = cy + (y - cy) * s, size *= s;
        alpha = std::min(1.0f, opening() * 1.6f) * t;
    }

private:
    static constexpr float kOpenTime = 0.22f, kTurnTime = 0.20f, kPressTime = 0.16f, kBumpTime = 0.18f, kGlideTime = 0.10f, kTurnSlide = 70.0f;
    static float ease_out(float t) { return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
    static float ease_out_back(float t) {
        const float c = 1.6f, u = t - 1.0f;
        return 1.0f + (c + 1.0f) * u * u * u + c * u * u;
    }
    float window_scale() const { return 0.92f + 0.08f * ease_out_back(opening()); }
    float turned() const { return ticked_ ? ease_out(turn_) : 1.0f; }
    // a pressed plate or prompt: down to 90 % and back
    float press_scale(float t) const { return ticked_ ? 1.0f - 0.10f * std::sin(std::clamp(t, 0.0f, 1.0f) * 3.14159265f) : 1.0f; }
    void turn(int dir) { turn_ = 0, turn_dir_ = dir; }
    float row_y(int index) const { return kListY + kRowH * float(index - int(top_)) + 4; }

    int index_of(const std::vector<Row>& rows) const {
        for (size_t i = 0; i < rows.size(); i++)
            if (rows[i].quest->key == selected_) return int(i);
        return -1;
    }
    static bool inside(float x, float y, float rx, float ry, float rw, float rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }
    static bool hit_close(float x, float y) { return (x - kCloseX) * (x - kCloseX) + (y - kCloseY) * (y - kCloseY) <= (kCloseR + 8) * (kCloseR + 8); }
    int row_at(float x, float y, size_t count) const {
        if (!inside(x, y, kListX, kListY, kListW, kRowH * kRowsShown)) return -1;
        const unsigned i = top_ + unsigned((y - kListY) / kRowH);
        return i < count ? int(i) : -1;
    }

    static Command rect(float x, float y, float w, float h, uint32_t rgba) {
        Command c;
        c.kind = Command::Rect, c.x = x, c.y = y, c.w = w, c.h = h, c.rgba = rgba;
        return c;
    }
    static Command text(float x, float y, float size, uint32_t rgba, std::string s) {
        Command c;
        c.kind = Command::Text, c.x = x, c.y = y, c.size = size, c.rgba = rgba, c.text = std::move(s);
        return c;
    }
    static Command circle(float x, float y, float r, uint32_t rgba, bool filled, float t = 2) {
        Command c;
        c.kind = filled ? Command::Circle : Command::CircleOutline, c.x = x, c.y = y, c.size = r, c.rgba = rgba, c.thickness = t;
        return c;
    }
    static Command line(float x0, float y0, float x1, float y1, uint32_t rgba, float t) {
        Command c;
        c.kind = Command::Line, c.x = x0, c.y = y0, c.w = x1 - x0, c.h = y1 - y0, c.rgba = rgba, c.thickness = t;
        return c;
    }
    // a style marker: a box the style draws (story_notebook_style.h)
    static Command box(const char* name, float x, float y, float w, float h) {
        Command c;
        // the box's name and its size as laid out (the style composes it once at that size; the
        // animations only scale the picture)
        c.kind = Command::Picture, c.x = x, c.y = y, c.w = w, c.h = h, c.rgba = 0xFFFFFFFF;
        c.text = std::string(name) + "@" + std::to_string(int(std::lround(w))) + "x" + std::to_string(int(std::lround(h)));
        return c;
    }
    // a pressed plate or prompt: its commands scaled around their centre
    static void press(std::vector<Command>& out, size_t from, float cx, float cy, float s) {
        if (s != 1.0f) transform(out, from, cx, cy, s, 0, 0, 1.0f);
    }
    // a button prompt: the game's button picture (A or B), then what it does; returns its width
    float prompt(std::vector<Command>& out, float x, float y, bool a, const char* what, float pressed, float size = 36) const {
        const size_t from = out.size();
        out.push_back(text(x, y, size, kWhite, a ? std::string("\xEE\x80\x80") : std::string("\xEE\x80\x81")));
        out.push_back(text(x + size + 6, y + size * 0.1f, size * 0.8f, kInk, bold(what)));
        press(out, from, x + size / 2, y + size / 2, press_scale(pressed));
        return size + 6 + text_width(bold(what), size * 0.8f);
    }
    static float prompt_width(const char* what, float size = 36) { return size + 6 + text_width(bold(what), size * 0.8f); }
    // a plate the player can press (A on it, or a tap): the game's button plate with its prompt
    void plate(std::vector<Command>& out, float x, float y, float w, float h, bool on, bool a, const char* what) const {
        const size_t from = out.size();
        out.push_back(box(on ? "nb:plate-on" : "nb:plate", x, y, w, h));
        const float size = 34, width = prompt_width(what, size);
        prompt(out, x + (w - width) / 2, y + (h - size) / 2, a, what, 1.0f, size);
        press(out, from, x + w / 2, y + h / 2, press_scale(a ? press_a_ : press_b_));
        if (on) cursor(out, x, y, w, h);
    }
    // the game's cursor: four corner brackets around what A acts on, breathing in and out
    void cursor(std::vector<Command>& out, float x, float y, float w, float h) const {
        const float breathe = ticked_ ? 2.0f + 2.0f * std::sin(time_ * 6.2831853f * 0.9f) : 2.0f;
        const float b = 4 + breathe;
        out.push_back(box("nb:cursor", x - b, y - b, w + 2 * b, h + 2 * b));
    }
    // the chart ring the tracked quest carries (the white story ring of the sea chart)
    static void ring(std::vector<Command>& out, float cx, float cy, float r) {
        out.push_back(circle(cx, cy, r, kRingBack, true));
        out.push_back(circle(cx, cy, r * 0.62f, kRing, false, 3));
    }
    static void stamp(std::vector<Command>& out, float cx, float cy) {
        out.push_back(circle(cx, cy, 30, kStamp, false, 3));
        out.push_back(circle(cx, cy, 24, kStamp, false, 1.5f));
        out.push_back(text(cx - text_width("DONE", 19) / 2, cy - 10, 19, kStamp, "DONE"));
    }

    void sheet(std::vector<Command>& out) const {
        out.push_back(box("nb:window", kPageX, kPageY, kPageW, kPageH));
        out.push_back(text(kPageX + 66, kPageY + 36, 42, kInk, bold("Story Notebook")));
        // the close mark (a tap closes the page, as B does)
        out.push_back(circle(kCloseX, kCloseY, kCloseR, 0xFFFFFFC8, true));
        out.push_back(circle(kCloseX, kCloseY, kCloseR, kInkSoft, false, 2.5f));
        out.push_back(line(kCloseX - 8, kCloseY - 8, kCloseX + 8, kCloseY + 8, kInkSoft, 3));
        out.push_back(line(kCloseX + 8, kCloseY - 8, kCloseX - 8, kCloseY + 8, kInkSoft, 3));
    }

    void list(std::vector<Command>& out, const std::vector<Row>& rows, int index) const {
        out.push_back(box("nb:panel", kListX - 22, kListY - 14, kListW + 34, kRowH * kRowsShown + 26));
        if (rows.empty() && tab_ == 0) {
            out.push_back(text(kListX + 20, kListY + 60, 30, kInkSoft, "No stories yet."));
            out.push_back(text(kListX + 20, kListY + 104, 25, kInkSoft, "When a story mod writes to you, its quests are kept here."));
        } else if (rows.empty()) {
            out.push_back(text(kListX + 20, kListY + 60, 30, kInkSoft, "No finished stories yet."));
        }
        for (unsigned i = 0; i < kRowsShown && top_ + i < rows.size(); i++) {
            const Row& r = rows[top_ + i];
            const Quest& q = *r.quest;
            const float y = kListY + kRowH * float(i);
            const size_t from = out.size();
            const bool selected = int(top_ + i) == index;
            if (selected) out.push_back(box("nb:plate-on", kListX - 6, y + 4, kListW + 2, kRowH - 8));
            else if (i && int(top_ + i) != index + 1) out.push_back(line(kListX + 10, y, kListX + kListW - 20, y, kRule, 2));
            float cx, cy, rad;
            emblem_place(i, cx, cy, rad);
            if (q.portrait.empty()) {  // the mod's emblem: its initial on a seal
                out.push_back(circle(cx, cy, rad, emblem_colour(q.mod), true));
                out.push_back(circle(cx, cy, rad - 5, kSeal, false, 1.5f));
                std::string initial = q.mod_name.empty() ? "?" : q.mod_name.substr(0, 1);
                out.push_back(text(cx - text_width(initial, 32) / 2, cy - 16, 32, kSeal, initial));
            }
            const float tx = kListX + 100, tw = kListW - 100 - 230;
            const bool dim = r.status == Status::Done || r.status == Status::NotAvailable;
            const std::string title = fit(q.title, 27, tw * 0.64f);
            out.push_back(text(tx, y + 9, 27, dim ? kInkSoft : kInk, bold(title)));
            const float after = tx + std::min(tw * 0.64f, text_width(title, 27)) + 16;
            out.push_back(text(after, y + 14, 19, kInkLight, fit("- " + q.mod_name, 19, tx + tw - after)));
            std::string next = r.status == Status::Done ? "The story is told." : r.status == Status::NotAvailable ? "Not yet available: " + q.condition : q.next;
            out.push_back(text(tx, y + 38, 22, dim ? kInkSoft : kInk, fit(next, 22, tw)));
            std::string where = q.where.empty() ? std::string(q.has_place ? "On the sea chart" : "No place on the chart for this step")
                                                : "Where: " + q.where;
            if (r.status == Status::New || r.status == Status::InProgress) out.push_back(text(tx, y + 61, 18, kInkSoft, fit(where, 18, tw)));
            status(out, r, kListX + kListW - 216, y);
            // the selected row: a little bounce as the cursor arrives
            if (selected && ticked_ && bump_ < 1.0f) press(out, from, kListX + kListW / 2, y + kRowH / 2, 1.0f + 0.025f * std::sin(bump_ * 3.14159265f));
        }
        // the cursor: around the selected row, gliding there from the row before
        if (index >= int(top_) && index < int(top_ + kRowsShown)) {
            float y = row_y(index);
            if (ticked_ && glide_ < 1.0f) y = glide_from_ + (y - glide_from_) * ease_out(glide_);
            cursor(out, kListX - 6, y, kListW + 2, kRowH - 8);
        }
        if (top_ > 0) out.push_back(text(kListX + kListW / 2, kListY - 34, 24, kInkSoft, "^"));
        if (top_ + kRowsShown < rows.size()) out.push_back(text(kListX + kListW / 2, kListY + kRowH * kRowsShown + 6, 24, kInkSoft, "v"));
        // prompts along the bottom, as the game's menus show them
        const float hy = kPageY + kPageH - 66;
        out.push_back(text(kPageX + 66, hy + 6, 23, kInkSoft, tab_ == 0 ? "Right: Archive" : "Left: Quests"));
        float x = kPageX + kPageW - 66 - prompt_width("Close");
        prompt(out, x, hy, false, "Close", press_b_);
        if (!rows.empty()) {
            x -= 36 + prompt_width("Open");
            prompt(out, x, hy, true, "Open", press_a_);
        }
    }

    void tabs(std::vector<Command>& out, const std::vector<Row>& all) const {
        size_t done = 0;
        for (const Row& r : all) done += r.status == Status::Done;
        const std::string labels[2] = {"Quests", "Archive" + (done ? " (" + std::to_string(done) + ")" : std::string())};
        const float xs[2] = {kTab0X, kTab1X};
        for (int i = 0; i < 2; i++) {
            const bool on = i == tab_;
            const float lift = on ? -3 : 0;
            out.push_back(box(on ? "nb:tab-on" : "nb:tab", xs[i], kTabY + lift, kTabW, kTabH));
            const float size = 26, w = text_width(labels[i], size);
            out.push_back(text(xs[i] + (kTabW - w) / 2, kTabY + lift + (kTabH - size) / 2, size, on ? kPlateInk : kInkSoft, bold(labels[i])));
        }
    }

    static void status(std::vector<Command>& out, const Row& r, float x, float y) {
        if (r.tracked) {
            ring(out, x + 16, y + 24, 14);
            out.push_back(text(x + 38, y + 12, 21, kInk, "On the chart"));
        }
        const float sy = y + (r.tracked ? 46 : 28);
        switch (r.status) {
        case Status::Done: stamp(out, x + 120, y + kRowH * 0.5f); break;
        case Status::New:
        case Status::InProgress: {  // a label in the colour of its state, as wide as its word
            const char* word = status_text(r.status);
            out.push_back(rect(x, sy, text_width(word, 21) + 22, 30, r.status == Status::New ? kNew : kProgress));
            out.push_back(text(x + 11, sy + 4, 21, kWhite, bold(word)));
            break;
        }
        case Status::NotAvailable: {
            Command c = rect(x, sy, text_width("Not yet available", 19) + 20, 30, kLocked);
            c.kind = Command::RectOutline, c.thickness = 2;
            out.push_back(c);
            out.push_back(text(x + 10, sy + 5, 19, kLocked, "Not yet available"));
            break;
        }
        }
    }

    void detail(std::vector<Command>& out, const Row& r) const {
        const Quest& q = *r.quest;
        const float x = kPageX + 80, w = kPageW - 160;
        float y = kPageY + 108;
        out.push_back(box("nb:panel", x - 22, y - 14, w + 44, kTrackY - y - 4));
        out.push_back(text(x, y, 34, kInk, bold(fit(q.title, 34, w - 260))));
        out.push_back(text(x, y + 40, 21, kInkLight, fit("from " + q.mod_name, 21, w - 260)));
        status(out, r, x + w - 220, y - 6);
        y += 80;
        auto section = [&](const char* head, const std::string& body, unsigned lines) {
            out.push_back(text(x, y, 21, kInkSoft, head));
            y += 25;
            for (const std::string& l : wrap(body, 24, w - 12, lines)) {
                out.push_back(text(x + 12, y, 24, kInk, l));
                y += 28;
            }
            y += 7;
        };
        if (r.status == Status::Done) {
            section("The story", "Done. The story is told.", 1);
        } else if (r.status == Status::NotAvailable) {
            section("Not yet available", q.condition, 2);
        } else {
            section("Next step", q.next.empty() ? std::string("(not written down yet)") : q.next, 2);
            section("Where", q.has_place ? (q.where.empty() ? std::string("Marked on the sea chart when you show it there.") : q.where)
                                         : (q.where.empty() ? std::string("This step has no place on the chart.")
                                                            : q.where + " (this step has no place on the chart)"),
                    1);
        }
        if (!q.hint.empty() && r.status != Status::Done) section("To continue", q.hint, 1);
        if (!q.known.empty()) section("What is known so far", q.known, 4);
        if (!q.reward.empty()) section("Reward", q.reward, 1);
        // plates: Back (B) and Show on chart (A); the cursor is on what A does
        plate(out, kBackX, kBackY, kBackW, kBackH, false, false, "Back");
        if (r.status != Status::Done) {
            plate(out, kTrackX, kTrackY, kTrackW, kTrackH, true, true, r.tracked ? "Stop showing on chart" : "Show on chart");
            if (r.tracked && !q.has_place)
                out.push_back(text(kTrackX, kTrackY - 30, 19, kInkSoft, "No place on the chart for this step"));
        }
    }

    Mode mode_ = Mode::Closed;
    int tab_ = 0;
    uint16_t selected_ = 0;
    unsigned top_ = 0, repeat_ = 0;
    uint32_t last_ = 0;
    bool touched_ = false;
    // animations (tick)
    bool ticked_ = false;
    float time_ = 0, opening_ = 1, turn_ = 1, press_a_ = 1, press_b_ = 1, bump_ = 1, glide_ = 1, glide_from_ = 0;
    int turn_dir_ = 0;
};

}  // namespace story_notebook
