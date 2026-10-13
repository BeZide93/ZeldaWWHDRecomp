// Mod charts and their markers on the sea chart (story_chart.h, docs/mod-sdk-v2.md "Charts and markers").
//
// How the game marks story destinations (HD decomp MAP-sea_chart#1, hd_screen_sea_chart_menu.cpp;
// GameCube: dMenu_Fmap_c::checkMarkCheck1/2/3):
//   - The sea chart menu (hdSeaChartMenu_c, the MAP screen of the GamePad, also shown on the TV) has
//     eight story-mark panes (mpMenuPanes, +0x50): null panes holding the white ring the game puts on
//     the island to go to next (Dragon Roost, Forest Haven, Greatfish, the three triangle islands, the
//     Earth and Wind Temples). Their places are fixed in the layout; the game never moves them.
//   - updateProgressIcons() (02678B74) shows the ones the story asks for, from event flags, every
//     update. While the screen is busy (opening, zooming, closing), it calls hideMenuPanes()
//     (02678B00) instead, which hides all eight.
//   - The chart area is the main pane (+0x70): the 7 x 7 squares, square D4 at its position, one
//     square (100000 world units) = its size / 7, y up (the game places Link's marker the same way,
//     updateMarkerPanePosition).
//   - Treasure and other charts are save bits (dSv_collect_map: owned, opened/complete); their marks
//     are separate per-square panes. A mod chart has no game item: its ownership is a record of the
//     story-mod book (story_mail_book.h).
//
// The marker layer: after the game's updateProgressIcons, every story-mark pane the game is not
// showing may carry a mod marker: it is moved to the marker's place and shown. Before the game's next
// update the pane goes back to its place and is hidden again, so the game always finds its panes as it
// left them and the game's own marks win. The panes are the game's own, drawn by the game's layout, so
// the markers look like the game's story marks and follow the screen wherever the chart is shown.
// Full save states never contain a moved pane (capture_begin/capture_end).
#include "story_chart.h"
#include "story_mail_book.h"

#include <cstdlib>
#include <map>
#include <mutex>

#include "guest_addr.h"
#include "runtime.h"

namespace story_chart {
namespace {

// canonical USA addresses, translated for the running build (guest_addr.h)
const uint32_t kSavePtr = GD(0x101F84DC);     // save base pointer (d_com_inf_game)
const uint32_t kProgress = GC(0x02678B74);    // hdSeaChartMenu_c::updateProgressIcons
const uint32_t kHidePanes = GC(0x02678B00);   // hdSeaChartMenu_c::hideMenuPanes
const uint32_t kDestroy = GC(0x02677B90);     // hdSeaChartMenu_c::destroy(flags)

// hdSeaChartMenu_c (hd_screen_sea_chart_menu.h) and library pane fields (hdPartsPaneSRT_c)
constexpr uint32_t kMenuPanes = 0x50, kMainPane = 0x70;
constexpr unsigned kStoryPanes = 8;
constexpr uint32_t kPaneTranslate = 0x1C, kPaneSize = 0x3C, kPaneFlags = 0x44;
constexpr uint8_t kVisible = 0x01, kMatrixDirty = 0x10;
constexpr float kSquaresPerSide = 7.0f;

struct Marker {
    std::string mod, chart, id, label;
    uint16_t key;  // the chart's record key
    float x, z;
    uint32_t icon;
};
// a story-mark pane carrying a marker
struct Applied {
    uint32_t pane;
    unsigned index;        // in the menu's story-mark panes
    uint32_t tx, ty;       // the game's place (raw bits)
    uint32_t mx, my;       // the marker's place (raw bits)
};

std::mutex g_mu;
std::vector<Marker> g_markers;  // placement order
std::map<uint32_t, std::vector<Applied>> g_applied;  // per menu (game thread)
bool g_imported = false, g_installed = false;
bool g_hidden = false;  // hideMenuPanes ran during the game's update
TrackedSource g_tracked = nullptr;
void (*g_original)(Cpu*, uint32_t) = nullptr;

bool trace() {
    static const bool on = [] { const char* e = getenv("WWHD_CHART_TRACE"); return e && *e == '1'; }();
    return on;
}

uint32_t save_base() { return ld32(kSavePtr); }
bool have_save() { return save_base() != 0; }
story_mail::Book book() { return story_mail::Book(mem::ptr(save_base() + story_mail::kBookOffset)); }
// the chart's record (kind kRecordMarker): 1 owned, 2 finished, 0 none (story_chart.h)
uint8_t chart_value(const story_mail::Book& b, uint16_t key) { return b.get(story_mail::kRecordMarker, key); }
int32_t result_of(uint8_t v) { return v == kChartOwned ? kResultOwned : v == kChartDone ? kResultDone : kResultNone; }
bool valid_label(const std::string& label) {
    std::u16string ignored;
    return label.size() <= kMaxLabelBytes && story_mail::append_utf16(label, false, ignored);
}

// the panes of one menu back as the game left them: in place and hidden
void revert(uint32_t menu, const std::vector<Applied>& applied) {
    for (const Applied& a : applied) {
        if (ld32(menu + kMenuPanes + 4 * a.index) != a.pane) continue;  // not that menu's pane any more
        st32(a.pane + kPaneTranslate, a.tx);
        st32(a.pane + kPaneTranslate + 4, a.ty);
        st8(a.pane + kPaneFlags, uint8_t((ld8(a.pane + kPaneFlags) & ~kVisible) | kMatrixDirty));
    }
}
void show(const Applied& a) {
    st32(a.pane + kPaneTranslate, a.mx);
    st32(a.pane + kPaneTranslate + 4, a.my);
    st8(a.pane + kPaneFlags, uint8_t(ld8(a.pane + kPaneFlags) | kVisible | kMatrixDirty));
}

// the markers on the story-mark panes the game left hidden
void apply(uint32_t menu) {
    std::vector<Applied>& applied = g_applied[menu];
    applied.clear();
    const uint32_t main = ld32(menu + kMainPane);
    if (!main || !have_save()) return;
    story_mail::Book b = book();
    if (!b.usable()) return;
    struct Place {
        float x, z;
    };
    std::vector<Place> shown;
    if (Place t{}; g_tracked) {  // the notebook's tracked quest comes first
        std::string label;
        if (g_tracked(t.x, t.z, label)) shown.push_back(t);
    }
    std::lock_guard lock(g_mu);
    for (const Marker& m : g_markers)
        if (chart_value(b, m.key) == kChartOwned) shown.push_back({m.x, m.z});
    if (shown.empty()) return;
    const float cx = float(ldf32(main + kPaneTranslate)), cy = float(ldf32(main + kPaneTranslate + 4));
    const float sx = float(ldf32(main + kPaneSize)) / kSquaresPerSide, sy = float(ldf32(main + kPaneSize + 4)) / kSquaresPerSide;
    size_t next = 0;
    for (unsigned i = 0; i < kStoryPanes && next < shown.size(); i++) {
        const uint32_t pane = ld32(menu + kMenuPanes + 4 * i);
        if (!pane || (ld8(pane + kPaneFlags) & kVisible)) continue;  // the game's own mark
        const Place& m = shown[next++];
        Applied a{pane, i, ld32(pane + kPaneTranslate), ld32(pane + kPaneTranslate + 4),
                  f32_as_u32(cx + m.x / kSquare * sx), f32_as_u32(cy - m.z / kSquare * sy)};
        show(a);
        applied.push_back(a);
    }
    if (trace()) {
        static std::map<uint32_t, std::string> last;
        std::string now;
        for (const Applied& a : applied) {
            char line[96];
            snprintf(line, sizeof line, " pane %u at %.1f,%.1f;", a.index, u32_as_f32(a.mx), u32_as_f32(a.my));
            now += line;
        }
        if (next < shown.size()) now += " " + std::to_string(shown.size() - next) + " waiting for a free pane";
        if (last[menu] != now) LOG("[chart] menu %08X:%s", menu, now.empty() ? " no markers" : now.c_str());
        last[menu] = now;
    }
}

Marker* find(const std::string& mod, const std::string& chart, const std::string& id) {
    for (Marker& m : g_markers)
        if (m.mod == mod && m.chart == chart && m.id == id) return &m;
    return nullptr;
}

}  // namespace

// the same 16-bit hash as the letters' keys; kinds keep the two apart
uint16_t chart_key(const std::string& mod, const std::string& chart) { return story_mail::letter_key(mod, chart); }

int32_t give(const std::string& mod, const std::string& chart) {
    if (!story_mail::valid_id(chart)) return kResultInvalid;
    if (!have_save()) return kResultNoRoom;
    story_mail::Book b = book();
    const uint16_t key = chart_key(mod, chart);
    const uint8_t v = chart_value(b, key);
    if (v) return result_of(v);
    if (!b.set(story_mail::kRecordMarker, key, kChartOwned)) {
        LOG("[chart] %s: no room for chart \"%s\" in this save file", mod.c_str(), chart.c_str());
        return kResultNoRoom;
    }
    LOG("[chart] %s: Link got chart \"%s\"", mod.c_str(), chart.c_str());
    return kResultOwned;
}

int32_t finish(const std::string& mod, const std::string& chart) {
    if (!story_mail::valid_id(chart)) return kResultInvalid;
    if (!have_save()) return kResultNoRoom;
    story_mail::Book b = book();
    const uint16_t key = chart_key(mod, chart);
    const uint8_t v = chart_value(b, key);
    if (v == kChartDone) return kResultDone;
    if (v && v != kChartOwned) return kResultInvalid;  // a value this version doesn't know
    if (!b.set(story_mail::kRecordMarker, key, kChartDone)) return kResultNoRoom;
    LOG("[chart] %s: chart \"%s\" finished", mod.c_str(), chart.c_str());
    return kResultDone;
}

int32_t state(const std::string& mod, const std::string& chart) {
    if (!story_mail::valid_id(chart)) return kResultInvalid;
    if (!have_save()) return kResultNone;
    return result_of(chart_value(book(), chart_key(mod, chart)));
}

int32_t mark(const std::string& mod, const std::string& chart, const std::string& marker, float x, float z,
             uint32_t icon, const std::string& label) {
    if (!story_mail::valid_id(chart) || !story_mail::valid_id(marker) || icon != kIconStory || !valid_label(label))
        return kResultInvalid;
    if (!(x >= -kSeaHalf && x <= kSeaHalf && z >= -kSeaHalf && z <= kSeaHalf)) return kResultInvalid;  // NaN too
    {
        std::lock_guard lock(g_mu);
        if (Marker* m = find(mod, chart, marker)) {
            m->x = x, m->z = z, m->label = label;
        } else {
            unsigned mine = 0;
            for (const Marker& m : g_markers) mine += m.mod == mod;
            if (mine >= kMaxMarkersPerMod || g_markers.size() >= kMaxMarkers) return kResultNoRoom;
            g_markers.push_back({mod, chart, marker, label, chart_key(mod, chart), x, z, icon});
            if (trace()) LOG("[chart] %s: marker \"%s\" of chart \"%s\" at %.0f, %.0f", mod.c_str(), marker.c_str(), chart.c_str(), x, z);
        }
    }
    return state(mod, chart);
}

int32_t unmark(const std::string& mod, const std::string& chart, const std::string& marker) {
    if (!story_mail::valid_id(chart) || (!marker.empty() && !story_mail::valid_id(marker))) return kResultInvalid;
    {
        std::lock_guard lock(g_mu);
        std::erase_if(g_markers, [&](const Marker& m) { return m.mod == mod && m.chart == chart && (marker.empty() || m.id == marker); });
    }
    return state(mod, chart);
}

void set_tracked_source(TrackedSource source) { g_tracked = source; }

std::vector<MarkerView> shown_markers() {
    std::vector<MarkerView> out;
    if (!have_save()) return out;
    if (float x, z; g_tracked) {
        std::string label;
        if (g_tracked(x, z, label)) out.push_back({"", "notebook", "tracked", label, x, z, kIconStory});
    }
    story_mail::Book b = book();
    std::lock_guard lock(g_mu);
    for (const Marker& m : g_markers)
        if (chart_value(b, m.key) == kChartOwned) out.push_back({m.mod, m.chart, m.id, m.label, m.x, m.z, m.icon});
    return out;
}

void note_import() { g_imported = true; }
bool imported() { return g_imported; }
bool installed() { return g_installed; }
void set_game_calls(void (*original)(Cpu*, uint32_t)) { g_original = original; }

void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    if (!g_imported || g_installed || !g_original) return;
    const std::pair<uint32_t, void (*)(Cpu*)> hooks[] = {
        {kProgress, hooks::progress_icons}, {kHidePanes, hooks::hide_panes}, {kDestroy, hooks::destroy}};
    // all or nothing: a pane moved without the hook that puts it back would stay moved
    for (const auto& [addr, fn] : hooks) {
        std::string other = replaced_by(addr);
        if (!other.empty()) {
            LOG("[chart] %s replaces %08X: mod chart markers are not shown this session", other.c_str(), addr);
            return;
        }
    }
    for (const auto& [addr, fn] : hooks) replace(addr, fn);
    g_installed = true;
    LOG("[chart] the sea chart shows markers of mod charts");
}

void capture_begin() {
    if (!g_installed) return;
    for (const auto& [menu, applied] : g_applied) revert(menu, applied);
}
void capture_end() {
    if (!g_installed) return;
    for (const auto& [menu, applied] : g_applied)
        for (const Applied& a : applied)
            if (ld32(menu + kMenuPanes + 4 * a.index) == a.pane) show(a);
}
void state_loaded() { g_applied.clear(); }

namespace hooks {

// updateProgressIcons(): the game's story marks, then the mod markers on the panes left free
void progress_icons(Cpu* c) {
    const uint32_t menu = c->r[3];
    auto it = g_applied.find(menu);
    if (it != g_applied.end()) {
        revert(menu, it->second);
        it->second.clear();
    }
    g_hidden = false;
    g_original(c, kProgress);
    if (!g_hidden) apply(menu);  // busy: the game hid every story mark, the markers stay hidden too
}

// hideMenuPanes(): the screen is busy
void hide_panes(Cpu* c) {
    g_hidden = true;
    g_original(c, kHidePanes);
}

// destroy(flags): forget the menu's panes
void destroy(Cpu* c) {
    g_applied.erase(c->r[3]);
    g_original(c, kDestroy);
}

}  // namespace hooks
}  // namespace story_chart
