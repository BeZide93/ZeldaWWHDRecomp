// The story notebook's page: what it shows and how the player moves through it (docs/mod-sdk-v2.md,
// "The story notebook"). Pure code: input in, draw commands out (the port's HUD layer draws them on
// the TV and the GamePad); unit-tested in runtime/tools/story_notebook_test.cpp.
//
// The page is a sheet of the notebook, drawn like the game's paper items (the letters, the charts):
// a parchment page with a dark-brown ink, a leather binding, one row per quest, and a stamp for the
// quests that are done. Coordinates are those of a 1280 x 720 screen, centered on wider screens.
//
//   List:   one row per quest (story_notebook_list.h order): the mod's emblem or the quest's portrait,
//           title and mod name, the next step, where, and the status (New, In progress, Not yet
//           available: condition, Done with a stamp). The tracked quest carries the chart ring.
//           Up/Down choose, A opens the quest, B closes the notebook.
//   Detail: the quest: next step and where (or "no place on the chart"), what is known so far, the
//           reward, and "Show on chart" (A): the one tracked quest; choosing it again untracks it.
//           B goes back to the list.
// Touch (and the mouse on the GamePad picture) taps rows and buttons; the keyboard acts as the
// controller through the port's controls.
#pragma once
#include <algorithm>
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
inline constexpr uint32_t kPadA = 0x8000, kPadB = 0x4000, kPadUp = 0x0200, kPadDown = 0x0100;

// page geometry (1280 x 720)
inline constexpr float kPageX = 150, kPageY = 44, kPageW = 980, kPageH = 632;
inline constexpr float kListX = kPageX + 70, kListY = kPageY + 92, kRowH = 98, kListW = kPageW - 110;
inline constexpr unsigned kRowsShown = 5;
// buttons of the detail view and the close mark
inline constexpr float kTrackX = kPageX + kPageW - 330, kTrackY = kPageY + kPageH - 92, kTrackW = 270, kTrackH = 52;
inline constexpr float kBackX = kPageX + 70, kBackY = kTrackY, kBackW = 150, kBackH = 52;
inline constexpr float kCloseX = kPageX + kPageW - 64, kCloseY = kPageY + 22, kCloseR = 20;

// colours (RGBA): parchment and ink
inline constexpr uint32_t kShade = 0x0A0C1499, kPaper = 0xF2E4C2FF, kPaperDark = 0xE3CF9EFF, kPaperEdge = 0x8B6A3EFF,
                          kBinding = 0x6E4526FF, kStitch = 0xC9A35EFF, kInk = 0x3F2A14FF, kInkSoft = 0x7A5E3AFF,
                          kSelect = 0xE8D09AFF, kSelectEdge = 0xB5893BFF, kNew = 0x3D7B3AFF, kProgress = 0x2C5C8CFF,
                          kLocked = 0x8A7F6CFF, kStamp = 0xB2392BFF, kRing = 0xFFFFFFFF, kRingBack = 0xD9822BFF;

// The overlay font's average advance, as a share of the size (measured on the frame dumps; generous).
inline constexpr float kAdvance = 0.40f;
inline float text_width(const std::string& s, float size) {
    size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return float(n) * size * kAdvance;
}

// One line of text cut to the width, with "...".
inline std::string fit(const std::string& text, float size, float width) {
    const size_t max_chars = size_t(std::max(1.0f, width / (size * kAdvance)));
    size_t chars = 0, i = 0, cut = std::string::npos;
    for (; i < text.size(); chars++) {
        if (chars + 3 == max_chars) cut = i;
        if (chars == max_chars) return text.substr(0, cut) + "...";
        const unsigned char c = (unsigned char)text[i];
        i += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    }
    return text;
}

// Text broken into lines of the width at spaces (and at the mod's own line breaks), at most `lines`.
inline std::vector<std::string> wrap(const std::string& text, float size, float width, unsigned lines) {
    const size_t max_chars = size_t(std::max(4.0f, width / (size * kAdvance)));
    std::vector<std::string> out;
    std::string line;
    size_t line_chars = 0;
    auto chars_of = [](const std::string& s) {
        size_t n = 0;
        for (unsigned char c : s) n += (c & 0xC0) != 0x80;
        return n;
    };
    auto flush = [&] {
        out.push_back(line);
        line.clear();
        line_chars = 0;
    };
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find_first_of(" \n", start);
        if (end == std::string::npos) end = text.size();
        const std::string word = text.substr(start, end - start);
        const size_t n = chars_of(word);
        if (line_chars && line_chars + 1 + n > max_chars) flush();
        if (line_chars) line += ' ', line_chars++;
        line += word;
        line_chars += n;
        if (end < text.size() && text[end] == '\n') flush();
        start = end + 1;
    }
    if (line_chars || out.empty()) flush();
    if (out.size() > lines) {
        out.resize(lines);
        out.back() = fit(out.back() + " ...........", size, width);
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

class Page {
public:
    enum class Mode : uint8_t { Closed, List, Detail };
    struct Action {
        enum Kind : uint8_t { None, Close, Toggle } kind = None;
        uint16_t key = 0;  // Toggle: the quest
    };

    Mode mode() const { return mode_; }
    bool open() const { return mode_ != Mode::Closed; }
    uint16_t selected_key() const { return selected_; }
    void show() {
        mode_ = Mode::List;
        selected_ = 0;
        top_ = 0;
        last_ = ~0u;  // buttons held when the page opens (the A that opened it) do nothing
        touched_ = true;
    }
    void hide() { mode_ = Mode::Closed; }

    // One game frame of input. Returns what the notebook has to do (Close: the page closed itself).
    Action input(const PadInput& pad, const std::vector<Row>& rows) {
        Action act;
        if (mode_ == Mode::Closed) return act;
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
        int index = index_of(rows);
        if (mode_ == Mode::List) {
            if (rows.empty()) index = -1;
            else if (index < 0) index = 0;
            if (index >= 0 && (moved & kPadUp) && index > 0) index--;
            if (index >= 0 && (moved & kPadDown) && index + 1 < int(rows.size())) index++;
            if (tap && hit_close(pad.x, pad.y)) {
                hide();
                act.kind = Action::Close;
                return act;
            }
            int tapped = tap ? row_at(pad.x, pad.y, rows.size()) : -1;
            if (tapped >= 0) index = tapped;
            if (index >= 0) {
                selected_ = rows[size_t(index)].quest->key;
                if (unsigned(index) < top_) top_ = unsigned(index);
                if (unsigned(index) >= top_ + kRowsShown) top_ = unsigned(index) - kRowsShown + 1;
            }
            if (index >= 0 && ((pressed & kPadA) || tapped >= 0)) {
                mode_ = Mode::Detail;
            } else if (pressed & kPadB) {
                hide();
                act.kind = Action::Close;
            }
        } else {  // Detail
            if (index < 0) {  // the quest is gone (its mod stopped registering it)
                mode_ = Mode::List;
                return act;
            }
            const Row& r = rows[size_t(index)];
            const bool trackable = r.status != Status::Done;
            if (tap && hit_close(pad.x, pad.y)) {
                hide();
                act.kind = Action::Close;
            } else if ((pressed & kPadB) || (tap && inside(pad.x, pad.y, kBackX, kBackY, kBackW, kBackH))) {
                mode_ = Mode::List;
            } else if (trackable && ((pressed & kPadA) || (tap && inside(pad.x, pad.y, kTrackX, kTrackY, kTrackW, kTrackH)))) {
                act.kind = Action::Toggle;
                act.key = r.quest->key;
            }
        }
        return act;
    }

    // The page as draw commands (empty when closed). `portrait(quest)`: the quest's portrait image
    // is drawn by the caller's picture commands; here only its place is kept free when has_portrait.
    std::vector<Command> draw(const std::vector<Row>& rows) const {
        std::vector<Command> out;
        if (mode_ == Mode::Closed) return out;
        sheet(out);
        const int index = index_of(rows);
        if (mode_ == Mode::Detail && index >= 0) detail(out, rows[size_t(index)]);
        else list(out, rows, index);
        return out;
    }

    // where a row's emblem goes (for the caller's portrait pictures): center and radius
    static void emblem_place(unsigned shown_index, float& cx, float& cy, float& r) {
        cx = kListX + 44;
        cy = kListY + kRowH * float(shown_index) + kRowH * 0.5f;
        r = 34;
    }
    unsigned top() const { return top_; }

private:
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
    static Command outline(float x, float y, float w, float h, uint32_t rgba, float t) {
        Command c = rect(x, y, w, h, rgba);
        c.kind = Command::RectOutline, c.thickness = t;
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
    // a button hint: the button's letter in a ring, then its action
    static void hint(std::vector<Command>& out, float x, float y, const char* button, const char* what) {
        out.push_back(circle(x + 13, y + 13, 13, kInk, true));
        out.push_back(text(x + 7, y + 3, 19, kPaper, button));
        out.push_back(text(x + 32, y + 2, 20, kInk, what));
    }
    // the chart ring the tracked quest carries (the white story ring of the sea chart)
    static void ring(std::vector<Command>& out, float cx, float cy, float r) {
        out.push_back(circle(cx, cy, r, kRingBack, true));
        out.push_back(circle(cx, cy, r * 0.62f, kRing, false, 3));
    }
    static void stamp(std::vector<Command>& out, float cx, float cy) {
        out.push_back(circle(cx, cy, 30, kStamp, false, 3));
        out.push_back(circle(cx, cy, 24, kStamp, false, 1.5f));
        out.push_back(text(cx - 22, cy - 10, 18, kStamp, "DONE"));
    }

    void sheet(std::vector<Command>& out) const {
        out.push_back(rect(-400, -200, 2080, 1120, kShade));  // the game behind, dimmed
        out.push_back(rect(kPageX + 6, kPageY + 8, kPageW, kPageH, 0x00000055));  // shadow
        out.push_back(rect(kPageX, kPageY, kPageW, kPageH, kPaper));
        out.push_back(rect(kPageX, kPageY + kPageH - 20, kPageW, 20, kPaperDark));
        out.push_back(outline(kPageX, kPageY, kPageW, kPageH, kPaperEdge, 4));
        out.push_back(outline(kPageX + 10, kPageY + 10, kPageW - 20, kPageH - 20, kPaperDark, 2));
        // the binding on the left, with its stitches
        out.push_back(rect(kPageX - 6, kPageY - 4, 40, kPageH + 8, kBinding));
        for (float y = kPageY + 24; y < kPageY + kPageH - 16; y += 36) out.push_back(line(kPageX + 8, y, kPageX + 22, y + 14, kStitch, 2));
        out.push_back(text(kPageX + 70, kPageY + 26, 34, kInk, "Story Notebook"));
        out.push_back(line(kPageX + 70, kPageY + 74, kPageX + kPageW - 40, kPageY + 74, kInkSoft, 2));
        // the close mark (B)
        out.push_back(circle(kCloseX, kCloseY + 4, kCloseR, kInkSoft, false, 2));
        out.push_back(line(kCloseX - 8, kCloseY - 4, kCloseX + 8, kCloseY + 12, kInkSoft, 2.5f));
        out.push_back(line(kCloseX + 8, kCloseY - 4, kCloseX - 8, kCloseY + 12, kInkSoft, 2.5f));
    }

    void list(std::vector<Command>& out, const std::vector<Row>& rows, int index) const {
        if (rows.empty()) {
            out.push_back(text(kListX + 20, kListY + 60, 24, kInkSoft, "No stories yet."));
            out.push_back(text(kListX + 20, kListY + 96, 20, kInkSoft, "When a story mod writes to you, its quests are kept here."));
        }
        for (unsigned i = 0; i < kRowsShown && top_ + i < rows.size(); i++) {
            const Row& r = rows[top_ + i];
            const Quest& q = *r.quest;
            const float y = kListY + kRowH * float(i);
            if (int(top_ + i) == index) {
                out.push_back(rect(kListX - 10, y + 3, kListW + 10, kRowH - 6, kSelect));
                out.push_back(outline(kListX - 10, y + 3, kListW + 10, kRowH - 6, kSelectEdge, 2));
                out.push_back(line(kListX - 30, y + kRowH * 0.5f - 10, kListX - 16, y + kRowH * 0.5f, kSelectEdge, 4));
                out.push_back(line(kListX - 30, y + kRowH * 0.5f + 10, kListX - 16, y + kRowH * 0.5f, kSelectEdge, 4));
            }
            if (i) out.push_back(line(kListX, y, kListX + kListW - 10, y, kPaperDark, 1.5f));
            float cx, cy, rad;
            emblem_place(i, cx, cy, rad);
            if (q.portrait.empty()) {  // the mod's emblem: its initial on a seal
                out.push_back(circle(cx, cy, rad, emblem_colour(q.mod), true));
                out.push_back(circle(cx, cy, rad - 5, kPaper, false, 1.5f));
                std::string initial = q.mod_name.empty() ? "?" : q.mod_name.substr(0, 1);
                out.push_back(text(cx - 9, cy - 17, 30, kPaper, initial));
            }
            const float tx = kListX + 96, tw = kListW - 96 - 230;
            const bool dim = r.status == Status::Done || r.status == Status::NotAvailable;
            const std::string title = fit(q.title, 24, tw * 0.62f);
            out.push_back(text(tx, y + 10, 24, dim ? kInkSoft : kInk, title));
            const float after = tx + std::min(tw * 0.62f, text_width(title, 24) * 0.85f) + 18;
            out.push_back(text(after, y + 16, 17, kInkSoft, fit("- " + q.mod_name, 17, tx + tw - after)));
            std::string next = r.status == Status::Done ? "The story is told." : r.status == Status::NotAvailable ? "Not yet available: " + q.condition : q.next;
            out.push_back(text(tx, y + 42, 19, dim ? kInkSoft : kInk, fit(next, 19, tw)));
            std::string where = q.where.empty() ? std::string(q.has_place ? "On the sea chart" : "No place on the chart for this step")
                                                : "Where: " + q.where;
            if (r.status == Status::New || r.status == Status::InProgress) out.push_back(text(tx, y + 68, 16, kInkSoft, fit(where, 16, tw)));
            status(out, r, kListX + kListW - 210, y);
        }
        if (top_ > 0) out.push_back(text(kListX + kListW / 2, kListY - 22, 18, kInkSoft, "^"));
        if (top_ + kRowsShown < rows.size()) out.push_back(text(kListX + kListW / 2, kListY + kRowH * kRowsShown - 6, 18, kInkSoft, "v"));
        const float hy = kPageY + kPageH - 56;
        if (!rows.empty()) hint(out, kPageX + kPageW - 430, hy, "A", "Open");
        hint(out, kPageX + kPageW - 250, hy, "B", "Close");
    }

    static void status(std::vector<Command>& out, const Row& r, float x, float y) {
        if (r.tracked) {
            ring(out, x + 16, y + 26, 14);
            out.push_back(text(x + 38, y + 14, 17, kInk, "On the chart"));
        }
        const float sy = y + (r.tracked ? 48 : 30);
        switch (r.status) {
        case Status::Done: stamp(out, x + 120, y + kRowH * 0.5f); break;
        case Status::New:
        case Status::InProgress: {  // a label in the colour of its state, as wide as its word
            const char* word = status_text(r.status);
            out.push_back(rect(x, sy, text_width(word, 19) * 0.85f + 22, 28, r.status == Status::New ? kNew : kProgress));
            out.push_back(text(x + 11, sy + 3, 19, kPaper, word));
            break;
        }
        case Status::NotAvailable:
            out.push_back(outline(x, sy, text_width("Not yet available", 17) * 0.85f + 20, 28, kLocked, 2));
            out.push_back(text(x + 10, sy + 4, 17, kLocked, "Not yet available"));
            break;
        }
    }

    void detail(std::vector<Command>& out, const Row& r) const {
        const Quest& q = *r.quest;
        const float x = kPageX + 70, w = kPageW - 140;
        float y = kPageY + 96;
        out.push_back(text(x, y, 30, kInk, fit(q.title, 30, w - 260)));
        out.push_back(text(x, y + 38, 18, kInkSoft, fit("from " + q.mod_name, 18, w - 260)));
        status(out, r, x + w - 220, y - 6);
        y += 84;
        auto section = [&](const char* head, const std::string& body, unsigned lines) {
            out.push_back(text(x, y, 18, kInkSoft, head));
            y += 26;
            for (const std::string& l : wrap(body, 21, w, lines)) {
                out.push_back(text(x + 12, y, 21, kInk, l));
                y += 28;
            }
            y += 10;
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
        if (!q.known.empty()) section("What is known so far", q.known, 5);
        if (!q.reward.empty()) section("Reward", q.reward, 1);
        // buttons: Back (B) and Show on chart (A)
        out.push_back(outline(kBackX, kBackY, kBackW, kBackH, kInkSoft, 2));
        hint(out, kBackX + 16, kBackY + 13, "B", "Back");
        if (r.status != Status::Done) {
            out.push_back(rect(kTrackX, kTrackY, kTrackW, kTrackH, r.tracked ? kPaperDark : kSelect));
            out.push_back(outline(kTrackX, kTrackY, kTrackW, kTrackH, kSelectEdge, 2.5f));
            hint(out, kTrackX + 14, kTrackY + 13, "A", r.tracked ? "Stop showing on chart" : "Show on chart");
            if (r.tracked && !q.has_place)
                out.push_back(text(kTrackX - 10, kTrackY - 28, 16, kInkSoft, "No place on the chart for this step"));
        }
    }

    Mode mode_ = Mode::Closed;
    uint16_t selected_ = 0;
    unsigned top_ = 0, repeat_ = 0;
    uint32_t last_ = 0;
    bool touched_ = false;
};

}  // namespace story_notebook
