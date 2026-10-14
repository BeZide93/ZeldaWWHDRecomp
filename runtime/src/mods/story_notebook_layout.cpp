// The story notebook's page as a game layout (story_notebook_layout.h).
#include "story_notebook_layout.h"
#include "story_notebook_layout_view.h"

#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>

#include "lyt_build.h"
#include "ppc.h"
#include "runtime.h"
#include "story_lyt_screen.h"
#include "story_lyt_text.h"
#include "story_notebook_page.h"

namespace story_notebook_layout {
namespace {
namespace nb = story_notebook;
constexpr const char* kLayout = "StoryNotebook_00";

std::string g_description, g_data_dir;

// the page as the layout shows it (story_notebook_layout_view.h): written by update() on the controller read,
// read by the layout's draw
std::mutex g_mu;
View g_view;            // the latest (g_mu)
bool g_drawing = false;  // the layout draws the page (or its closing) now (g_mu)

// ---- the layout's draw (game thread, item screen draw) ----
struct Shown {
    int mode = 0, tab = -1, selected = -2;
    uint64_t serial = ~0ull;
    bool closing = false;
    float cursor_y = 0, cursor_from = 0;
    int glide = 0;
    std::map<std::string, std::string> texts;  // what each text pane holds
};
Shown g_shown;

float pane_f(uint32_t pane, uint32_t off) { return pane ? u32_as_f32(ld32(pane + off)) : 0.0f; }

void set_text(Cpu* c, const char* name, const std::string& s) {
    auto& now = g_shown.texts[name];
    if (now == s) return;
    now = s;
    if (const uint32_t p = story_lyt_screen::pane(c, name)) story_lyt_text::set_text(c, p, s);
}
void show(Cpu* c, const char* name, bool on) { story_lyt_screen::set_visible(story_lyt_screen::pane(c, name), on); }
std::string row_name(int i, const char* part) { return "N_Row" + std::to_string(i) + part; }
std::string row_pane(const char* kind, int i, const char* part) { return std::string(kind) + "_Row" + std::to_string(i) + part; }

// the text, cut with "..." to its pane's width (the page's own fit(), measured in the game's font)
std::string fitted(Cpu* c, const char* pane, const std::string& text, float size, bool bold) {
    const float w = pane_f(story_lyt_screen::pane(c, pane), 0x3C);
    return w > 0 ? nb::fit(text, size, w, bold ? nb::kBold : "") : text;
}

void apply(Cpu* c, const View& v) {
    for (int i = 0; i < 2; i++) {
        const bool on = i == v.tab;
        const std::string n = std::to_string(i);
        show(c, ("P_Tab" + n + "On").c_str(), on);
        show(c, ("P_Tab" + n + "Off").c_str(), !on);
        show(c, ("T_Tab" + n + "On").c_str(), on);
        show(c, ("T_Tab" + n + "Off").c_str(), !on);
        set_text(c, ("T_Tab" + n + "On").c_str(), v.tab_labels[i]);
        set_text(c, ("T_Tab" + n + "Off").c_str(), v.tab_labels[i]);
    }
    for (int i = 0; i < kRows; i++) {
        const bool has = i < int(v.rows.size());
        show(c, row_name(i, "").c_str(), has);
        if (!has) continue;
        const RowView& r = v.rows[size_t(i)];
        const std::string title = row_pane("T", i, r.dim ? "TitleDim" : "Title"), next = row_pane("T", i, r.dim ? "NextDim" : "Next");
        show(c, row_pane("T", i, "Title").c_str(), !r.dim);
        show(c, row_pane("T", i, "TitleDim").c_str(), r.dim);
        show(c, row_pane("T", i, "Next").c_str(), !r.dim);
        show(c, row_pane("T", i, "NextDim").c_str(), r.dim);
        set_text(c, title.c_str(), fitted(c, title.c_str(), r.title, 25, true));
        set_text(c, next.c_str(), fitted(c, next.c_str(), r.next, 19, false));
        set_text(c, row_pane("T", i, "Status").c_str(), r.status);
        set_text(c, row_pane("T", i, "Initial").c_str(), r.initial);
        show(c, row_pane("T", i, "Initial").c_str(), r.initial_shown);
        show(c, row_pane("P", i, "PillNew").c_str(), r.pill == 1);
        show(c, row_pane("P", i, "PillNotYet").c_str(), r.pill == 2);
        show(c, row_pane("P", i, "PillDone").c_str(), r.pill == 3);
        show(c, row_pane("P", i, "Ring").c_str(), r.tracked);
        show(c, row_pane("P", i, "Wash").c_str(), i == v.selected);
    }
    show(c, "T_Empty1", v.empty);
    show(c, "T_Empty2", v.empty && !v.empty2.empty());
    set_text(c, "T_Empty1", v.empty1);
    set_text(c, "T_Empty2", v.empty2);
    show(c, "T_Up", v.up);
    show(c, "T_Down", v.down);
    show(c, "N_PlateTrack", v.track_plate);
    show(c, "N_Cursor", v.selected >= 0);
}

// where the cursor (its null pane, in N_Content) is for row i: its built place (row 0), one row step down a row
float g_cursor_y0 = 0, g_row_step = 0;
float cursor_y_for(int i) { return g_cursor_y0 - g_row_step * float(i); }

bool frame(Cpu* c, uint32_t) {
    View v;
    bool drawing;
    {
        std::lock_guard lock(g_mu);
        v = g_view;
        drawing = g_drawing;
    }
    if (!drawing && !g_shown.closing) {
        if (v.mode == 2) g_shown.mode = 2;  // the detail view (the HUD's): coming back turns the page
        return false;
    }
    if (v.mode == 1 && g_shown.mode != 1) {  // the page opens (or comes back from the detail view)
        if (g_shown.mode == 0) {
            story_lyt_screen::play(c, "Open");
            story_lyt_screen::play(c, "Breathe");
        } else {
            story_lyt_screen::play(c, "TurnLeft");
        }
        g_shown.closing = false;
        g_shown.selected = -2;
    }
    if (v.mode == 0 && g_shown.mode == 1) {  // it closes: the closing animation, then nothing
        story_lyt_screen::play(c, "Close");
        g_shown.closing = true;
        g_shown.mode = 0;
    }
    if (v.mode == 0 && g_shown.closing && story_lyt_screen::ended(c, "Close")) {
        g_shown.closing = false;
        g_shown.mode = 0;
        std::lock_guard lock(g_mu);
        g_drawing = false;
        return false;
    }
    if (v.mode == 1) {
        if (g_shown.mode == 1 && v.tab != g_shown.tab) story_lyt_screen::play(c, v.tab > g_shown.tab ? "TurnRight" : "TurnLeft");
        if (v.serial != g_shown.serial) apply(c, v), g_shown.serial = v.serial;
        // the cursor glides to the selected row (0.1 s); the row bumps
        if (v.selected >= 0) {
            const uint32_t cursor = story_lyt_screen::pane(c, "N_Cursor");
            const float to = cursor_y_for(v.selected);
            if (v.selected != g_shown.selected) {
                if (g_shown.selected >= 0) story_lyt_screen::play(c, "Bump" + std::to_string(v.selected));
                g_shown.cursor_from = g_shown.selected >= 0 ? g_shown.cursor_y : to;
                g_shown.glide = 0;
                g_shown.selected = v.selected;
            }
            const int steps = 3;
            float t = std::min(1.0f, float(++g_shown.glide) / steps);
            t = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
            g_shown.cursor_y = g_shown.cursor_from + (to - g_shown.cursor_from) * t;
            story_lyt_screen::set_translate(cursor, pane_f(cursor, 0x1C), g_shown.cursor_y);
        }
        g_shown.tab = v.tab;
    }
    if (v.mode != 0) g_shown.mode = v.mode;
    return g_shown.mode == 1 || g_shown.closing;
}

void built(Cpu* c, uint32_t) {
    g_shown = Shown{};
    g_cursor_y0 = pane_f(story_lyt_screen::pane(c, "N_Cursor"), 0x20);
    g_row_step = pane_f(story_lyt_screen::pane(c, "N_Row0"), 0x20) - pane_f(story_lyt_screen::pane(c, "N_Row1"), 0x20);
    // the game sizes every text pane's buffer from its width when it builds the layout: the row texts
    // and labels get room for what the page puts there
    auto room = [&](const std::string& name, uint32_t chars) { story_lyt_text::set_capacity(c, story_lyt_screen::pane(c, name.c_str()), chars); };
    for (int i = 0; i < kRows; i++) {
        for (const char* part : {"Title", "TitleDim", "Next", "NextDim"}) room(row_pane("T", i, part), 160);
        room(row_pane("T", i, "Status"), 24);
        room(row_pane("T", i, "Initial"), 4);
    }
    for (const char* name : {"T_Tab0On", "T_Tab0Off", "T_Tab1On", "T_Tab1Off"}) room(name, 32);
    room("T_Empty1", 64), room("T_Empty2", 96);
}

}  // namespace

View view_of(const nb::Page& page, const std::vector<nb::Row>& all) {
    View v;
    v.mode = page.mode() == nb::Page::Mode::List ? 1 : page.mode() == nb::Page::Mode::Detail ? 2 : 0;
    v.tab = page.tab();
    size_t done = 0;
    for (const nb::Row& r : all) done += r.status == nb::Status::Done;
    v.tab_labels[0] = "Quests";
    v.tab_labels[1] = "Archive" + (done ? " (" + std::to_string(done) + ")" : std::string());
    const std::vector<nb::Row> rows = page.shown(all);
    const unsigned top = page.top();
    for (unsigned i = 0; i < unsigned(kRows) && top + i < rows.size(); i++) {
        const nb::Row& r = rows[top + i];
        const nb::Quest& q = *r.quest;
        RowView rv;
        rv.title = q.title;
        rv.next = r.status == nb::Status::Done ? "The story is told." : r.status == nb::Status::NotAvailable ? "Not yet available: " + q.condition : q.next;
        rv.dim = r.status == nb::Status::Done || r.status == nb::Status::NotAvailable;
        rv.status = r.status == nb::Status::NotAvailable ? "Not yet" : nb::status_text(r.status);
        rv.pill = r.status == nb::Status::New ? 1 : r.status == nb::Status::NotAvailable ? 2 : r.status == nb::Status::Done ? 3 : 0;
        rv.tracked = r.tracked;
        rv.initial_shown = q.portrait.empty();
        rv.initial = q.mod_name.empty() ? "?" : q.mod_name.substr(0, 1);
        if (q.key == page.selected_key()) v.selected = int(i);
        v.rows.push_back(std::move(rv));
    }
    v.up = top > 0;
    v.down = top + kRows < rows.size();
    v.empty = rows.empty();
    v.empty1 = v.tab == 0 ? "No stories yet." : "No finished stories yet.";
    v.empty2 = v.tab == 0 ? "When a story mod writes to you, its quests are kept here." : "";
    v.track_plate = !rows.empty();
    return v;
}

namespace {
bool same(const View& a, const View& b) {
    if (a.mode != b.mode || a.tab != b.tab || a.selected != b.selected || a.up != b.up || a.down != b.down || a.empty != b.empty ||
        a.track_plate != b.track_plate || a.tab_labels[1] != b.tab_labels[1] || a.rows.size() != b.rows.size())
        return false;
    for (size_t i = 0; i < a.rows.size(); i++) {
        const RowView &x = a.rows[i], &y = b.rows[i];
        if (x.title != y.title || x.next != y.next || x.status != y.status || x.initial != y.initial || x.dim != y.dim ||
            x.tracked != y.tracked || x.initial_shown != y.initial_shown || x.pill != y.pill)
            return false;
    }
    return true;
}
}  // namespace

bool configure() {
    if (const char* e = getenv("WWHD_NOTEBOOK_LAYOUT"); e && *e) {
        g_description = e;
        g_data_dir.clear();
    }
    if (g_description.empty()) return false;
    namespace fs = std::filesystem;
    const fs::path cache = g_data_dir.empty() ? fs::path(g_description).parent_path() / ".layout-cache" : fs::path(g_data_dir) / "layout-cache";
    const std::string archive = lyt_build::build_cached(g_description, cache);
    if (archive.empty()) {
        LOG("[notebook] the layout %s isn't usable: the page is drawn on the HUD", g_description.c_str());
        return false;
    }
    story_lyt_screen::Client client;
    client.layout = kLayout;
    client.archive = archive;
    client.owner = "the story notebook";
    // the animation entries: each with the slot of its group (animations on the same panes replace each other)
    const std::vector<std::pair<std::string, std::string>> anims = {
        {"Open", "G_Open"},       {"Close", "G_Open"},       {"TurnLeft", "G_Content"}, {"TurnRight", "G_Content"},
        {"Breathe", "G_Cursor"},  {"Bump0", "G_Row0"},       {"Bump1", "G_Row1"},       {"Bump2", "G_Row2"},
        {"Bump3", "G_Row3"},      {"Bump4", "G_Row4"},       {"PressBack", "G_Back"},   {"PressTrack", "G_Track"}};
    std::map<std::string, uint32_t> slots;
    for (const auto& [name, group] : anims) {
        const uint32_t slot = slots.emplace(group, uint32_t(slots.size())).first->second;
        client.anims.push_back({name, group, slot});
    }
    client.built = built;
    client.frame = frame;
    story_lyt_screen::set_client(std::move(client));
    LOG("[notebook] the page is drawn as the layout %s (%s)", kLayout, archive.c_str());
    return true;
}

bool update(const nb::Page& page, const std::vector<nb::Row>& all) {
    if (!story_lyt_screen::has_client() || !story_lyt_screen::ready()) return false;
    View v = view_of(page, all);
    std::lock_guard lock(g_mu);
    if (v.mode == 2) {  // version 1: the detail view is the HUD's
        g_view.mode = 2;
        g_drawing = false;
        return false;
    }
    if (!same(v, g_view)) v.serial = g_view.serial + 1, g_view = std::move(v);
    g_drawing = g_view.mode != 0 || g_drawing;
    return g_view.mode != 0;
}

bool drawing() {
    std::lock_guard lock(g_mu);
    return g_drawing;
}

bool update_closed() {
    if (!story_lyt_screen::has_client()) return false;
    std::lock_guard lock(g_mu);
    if (g_view.mode != 0) g_view.mode = 0, g_view.serial++;
    return g_drawing;
}

}  // namespace story_notebook_layout

namespace story_notebook {
void set_layout_description(const std::string& path, const std::string& data_dir) {
    story_notebook_layout::g_description = path;  // NOLINT: same translation unit
    story_notebook_layout::g_data_dir = data_dir;
}
}  // namespace story_notebook
