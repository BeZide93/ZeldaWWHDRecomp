// Mod charts and their sea chart markers (runtime/src/mods/story_chart.*): the shared save record, the
// services and the sea chart menu hooks on a synthetic menu with synthetic panes and save. The game's
// own functions are stubbed; no game files are needed.
#include "mods/story_chart.h"
#include "mods/story_mail_book.h"
#include "mods/story_store.h"
#include "runtime.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

using namespace story_chart;

void log_msg(const char* fmt, ...) {
    (void)fmt;
}

namespace {

// synthetic guest memory: the save pointer, the save, a sea chart menu and its panes
constexpr uint32_t kGlobals = 0x101F0000, kArena = 0x20000000;
constexpr uint32_t kSave = kArena, kMenu = kArena + 0x2000, kMain = kArena + 0x3000, kPanes = kArena + 0x4000;
constexpr uint32_t kPaneStride = 0x100;

void* map(uint32_t at, uint32_t size) {
#ifdef _WIN32
    return VirtualAlloc(mem::ptr(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    return mmap(mem::ptr(at), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
#endif
}

uint32_t pane(unsigned i) { return kPanes + i * kPaneStride; }
float tx(unsigned i) { return u32_as_f32(ld32(pane(i) + 0x1C)); }
float ty(unsigned i) { return u32_as_f32(ld32(pane(i) + 0x20)); }
bool visible(unsigned i) { return ld8(pane(i) + 0x44) & 1; }
bool dirty(unsigned i) { return ld8(pane(i) + 0x44) & 0x10; }
void clear_dirty() {
    for (unsigned i = 0; i < 8; i++) st8(pane(i) + 0x44, ld8(pane(i) + 0x44) & ~0x10);
}
// the layout's places of the story marks (made up)
float home_x(unsigned i) { return 10.0f * float(i + 1); }
float home_y(unsigned i) { return -5.0f * float(i + 1); }
void reset_pane(unsigned i) {  // the pane as the layout made it
    st32(pane(i) + 0x1C, f32_as_u32(home_x(i)));
    st32(pane(i) + 0x20, f32_as_u32(home_y(i)));
    st8(pane(i) + 0x44, 0x02);
}
void build_menu() {
    for (unsigned i = 0; i < 8; i++) {
        st32(kMenu + 0x50 + 4 * i, pane(i));
        st32(pane(i) + 0x1C, f32_as_u32(home_x(i)));
        st32(pane(i) + 0x20, f32_as_u32(home_y(i)));
        st8(pane(i) + 0x44, 0x02);  // hidden, "influenced alpha" as the game's
    }
    st32(kMenu + 0x70, kMain);
    st32(kMain + 0x1C, f32_as_u32(71.0f));    // the chart's center (square D4)
    st32(kMain + 0x20, f32_as_u32(-44.0f));
    st32(kMain + 0x3C, f32_as_u32(511.0f));   // 7 squares of 73
    st32(kMain + 0x40, f32_as_u32(511.0f));
}

// the game's code
std::vector<unsigned> game_shows;  // story marks the game's update shows
bool game_busy = false;
std::vector<std::string> calls;
std::map<uint32_t, void (*)(Cpu*)> hooked;
void original(Cpu* c, uint32_t fn) {
    switch (fn) {
    case 0x02678B74:  // updateProgressIcons: busy -> hideMenuPanes, else the story's marks
        assert(c->r[3] == kMenu);
        // the game always finds its panes in place: a pane is either the game's (shown) or hidden
        for (unsigned i = 0; i < 8; i++) assert(tx(i) == home_x(i) && ty(i) == home_y(i));
        calls.push_back("progress");
        if (game_busy) {
            hooked.at(0x02678B00)(c);
            break;
        }
        for (unsigned i : game_shows) st8(pane(i) + 0x44, ld8(pane(i) + 0x44) | 1);
        break;
    case 0x02678B00:
        calls.push_back("hide");
        for (unsigned i = 0; i < 8; i++) st8(pane(i) + 0x44, ld8(pane(i) + 0x44) & ~1);
        break;
    case 0x02677B90: calls.push_back("destroy"); break;
    default: assert(!"unexpected original");
    }
}
void update() {
    Cpu c{};
    c.r[3] = kMenu;
    hooked.at(0x02678B74)(&c);
}
// a story mark of the game hides when its step is done (the game's own visibility writes)
void game_hides(unsigned i) {
    std::erase(game_shows, i);
    st8(pane(i) + 0x44, ld8(pane(i) + 0x44) & ~1);
}
bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

void test_record() {
    uint8_t raw[story_mail::kBookSize] = {};
    story_mail::Book b(raw);
    const uint16_t chart = chart_key("mod", "tale"), letter = story_mail::letter_key("mod", "tale");
    assert(chart && chart_key("mod", "tale") == chart && chart_key("other", "tale") != chart);
    // letters and charts share the book; the record kind keeps them apart (same key, different kinds)
    assert(letter == chart);
    assert(b.set_state(letter, story_mail::kWaiting) && b.set(story_mail::kRecordMarker, chart, kChartOwned));
    assert(b.state(letter) == story_mail::kWaiting && b.get(story_mail::kRecordMarker, chart) == kChartOwned);
    assert(raw[0] == story_mail::kMagic && raw[1] == 2);
    // second record: kind 3, the key big-endian, value 1
    assert(raw[8] == 3 && raw[9] == uint8_t(chart >> 8) && raw[10] == uint8_t(chart) && raw[11] == 1);
    assert(b.set(story_mail::kRecordMarker, chart, kChartDone) && raw[11] == 2 && b.state(letter) == story_mail::kWaiting);
}

void test_services() {
    assert(state("mod", "tale") == kResultNone && give("mod", "tale") == kResultNoRoom);  // no save loaded
    st32(0x101F84DC, kSave);
    assert(give("mod", "Tale") == kResultInvalid && give("mod", "") == kResultInvalid);
    assert(give("mod", "tale") == kResultOwned && give("mod", "tale") == kResultOwned);  // idempotent
    assert(state("mod", "tale") == kResultOwned && state("other", "tale") == kResultNone);
    story_mail::Book b = story_store::book();
    assert(b.get(story_mail::kRecordMarker, chart_key("mod", "tale")) == kChartOwned);
    // markers: checked arguments, add or move, per-mod limit
    assert(mark("mod", "tale", "start", 0, -200000, kIconStory, "Windfall") == kResultOwned);
    assert(mark("mod", "tale", "Start", 0, 0, kIconStory, "") == kResultInvalid);
    assert(mark("mod", "tale", "x", 0, 0, 7, "") == kResultInvalid);                    // unknown icon
    assert(mark("mod", "tale", "x", 400000, 0, kIconStory, "") == kResultInvalid);      // off the sea
    assert(mark("mod", "tale", "x", NAN, 0, kIconStory, "") == kResultInvalid);
    assert(mark("mod", "tale", "x", 0, 0, kIconStory, "two\nlines") == kResultInvalid);
    assert(mark("mod", "tale", "x", 0, 0, kIconStory, std::string(kMaxLabelBytes + 1, 'a')) == kResultInvalid);
    assert(mark("mod", "later", "x", 100000, 0, kIconStory, "") == kResultNone);  // a chart not given yet
    for (unsigned i = 2; i < kMaxMarkersPerMod; i++)
        assert(mark("mod", "later", "m" + std::to_string(i), 0, 0, kIconStory, "") == kResultNone);
    assert(mark("mod", "later", "one-more", 0, 0, kIconStory, "") == kResultNoRoom);
    assert(mark("mod", "tale", "start", 0, -200000, kIconStory, "Windfall Island") == kResultOwned);  // a move is no new marker
    auto shown = shown_markers();
    assert(shown.size() == 1 && shown[0].mod == "mod" && shown[0].marker == "start" && shown[0].label == "Windfall Island" &&
           shown[0].x == 0 && shown[0].z == -200000);
    assert(unmark("mod", "later", "") == kResultNone);  // all of a chart
    assert(unmark("mod", "tale", "nothing") == kResultOwned && shown_markers().size() == 1);
}

void test_hooks() {
    // install: all or nothing
    set_game_calls(original);
    install([](uint32_t) { return std::string(); }, [](uint32_t, void (*)(Cpu*)) { assert(!"not imported"); });
    assert(!installed());
    note_import();
    install([](uint32_t a) { return a == 0x02678B00 ? std::string("other") : std::string(); },
            [](uint32_t, void (*)(Cpu*)) { assert(!"installed despite a conflict"); });
    assert(!installed());
    install([](uint32_t) { return std::string(); }, [](uint32_t a, void (*fn)(Cpu*)) { hooked[a] = fn; });
    assert(installed() && hooked.size() == 3);

    build_menu();
    // the game shows its own mark on pane 6; the marker takes the first pane the game left hidden
    game_shows = {6};
    update();
    assert(visible(6) && near(tx(6), home_x(6)));
    assert(visible(0) && dirty(0) && near(tx(0), 71.0f) && near(ty(0), -44.0f + 2 * 73.0f));  // square D2
    for (unsigned i = 1; i < 8; i++) assert(i == 6 || !visible(i));
    // every update puts the pane back first (checked in the stub), then shows the marker again
    clear_dirty();
    update();
    assert(visible(0) && dirty(0) && near(ty(0), 102.0f));
    // the game's own mark wins: the story now asks for pane 0
    game_shows = {0, 6};
    update();
    assert(visible(0) && near(tx(0), home_x(0)) && visible(1) && near(tx(1), 71.0f) && near(ty(1), 102.0f));
    game_hides(0);
    game_hides(6);
    // a moved marker, a second marker in placement order
    assert(mark("mod", "tale", "start", 100000, 0, kIconStory, "") == kResultOwned);
    assert(mark("mod", "tale", "next", -300000, 300000, kIconStory, "") == kResultOwned);
    update();
    assert(near(tx(0), 71.0f + 73.0f) && near(ty(0), -44.0f) && near(tx(1), 71.0f - 3 * 73.0f) && near(ty(1), -44.0f - 3 * 73.0f));
    assert(visible(0) && visible(1) && !visible(2));
    // busy screen: the game hides its marks and so do the markers
    game_busy = true;
    update();
    for (unsigned i = 0; i < 8; i++) assert(!visible(i) && near(tx(i), home_x(i)));
    game_busy = false;
    update();
    assert(visible(0) && visible(1));
    // only as many markers as there are free panes: the rest wait
    game_shows = {0, 1, 2, 3, 4, 5, 6};
    update();
    assert(visible(7) && near(tx(7), 71.0f + 73.0f));
    for (unsigned i = 0; i < 7; i++) assert(near(tx(i), home_x(i)));
    for (unsigned i = 0; i < 7; i++) game_hides(i);
    // a full state is captured with the panes as the game left them
    update();
    capture_begin();
    for (unsigned i = 0; i < 8; i++) assert(!visible(i) && near(tx(i), home_x(i)));
    capture_end();
    assert(visible(0) && near(tx(0), 71.0f + 73.0f) && visible(1));
    // a finished chart: its markers go
    assert(finish("mod", "tale") == kResultDone && finish("mod", "tale") == kResultDone);
    assert(give("mod", "tale") == kResultDone && shown_markers().empty());
    update();
    for (unsigned i = 0; i < 8; i++) assert(!visible(i) && near(tx(i), home_x(i)));
    // a state load replaces guest memory: nothing is put back into it
    assert(give("mod", "other") == kResultOwned && mark("mod", "other", "a", 0, 0, kIconStory, "") == kResultOwned);
    update();
    assert(visible(0));
    const uint32_t moved = ld32(pane(0) + 0x1C);
    state_loaded();
    capture_begin();
    assert(ld32(pane(0) + 0x1C) == moved);  // untouched: not ours any more
    reset_pane(0);  // (the loaded state's own pane)
    update();
    assert(visible(0) && near(tx(0), 71.0f));
    // a destroyed menu: its panes are forgotten
    Cpu c{};
    c.r[3] = kMenu;
    calls.clear();
    hooked.at(0x02677B90)(&c);
    assert(calls == std::vector<std::string>{"destroy"});
    capture_begin();
    assert(visible(0) && near(tx(0), 71.0f));  // nothing written into the freed menu
    // removed markers are no longer shown
    reset_pane(0);
    assert(unmark("mod", "other", "a") == kResultOwned);
    update();
    for (unsigned i = 0; i < 8; i++) assert(!visible(i));
}

}  // namespace

int main() {
    assert(map(kGlobals, 0x10000) && map(kArena, 0x10000));
    story_store::set_memory(kArena + 0xE000);
    test_record();
    test_services();
    test_hooks();
    std::puts("story_chart_test: ok");
    return 0;
}
