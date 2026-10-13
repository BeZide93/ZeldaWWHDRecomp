// The story notebook (runtime/src/mods/story_notebook*): the quest list (order, tracking, state),
// the save record round trips, the services on a synthetic save, the page's navigation and drawing,
// the tracked quest's marker on the sea chart, and the item menu hooks on synthetic menu objects.
// The game's own functions are stubbed; no game files are needed.
#include "mods/story_notebook.h"
#include "mods/story_notebook_list.h"
#include "mods/story_notebook_page.h"
#include "mods/story_chart.h"
#include "mods/story_mail.h"
#include "mods/story_mail_book.h"
#include "input.h"
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

using namespace story_notebook;
using story_mail::Book;

void log_msg(const char* fmt, ...) {
    (void)fmt;
}

namespace {

constexpr uint32_t kGlobals = 0x101F0000, kArena = 0x20000000, kSave = kArena;

void* map(uint32_t at, uint32_t size) {
#ifdef _WIN32
    return VirtualAlloc(mem::ptr(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    return mmap(mem::ptr(at), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
#endif
}

Quest quest(const std::string& mod, const std::string& id, const std::string& condition = "") {
    Quest q;
    q.mod = mod, q.mod_name = "Mod " + mod, q.id = id, q.key = quest_key(mod, id), q.title = "Quest " + id, q.condition = condition;
    return q;
}
std::vector<std::string> order(const std::vector<Row>& rows) {
    std::vector<std::string> out;
    for (const Row& r : rows) out.push_back(r.quest->id);
    return out;
}

void test_list() {
    uint8_t raw[story_mail::kBookSize] = {};
    Book b(raw);
    std::vector<Quest> qs = {quest("a", "done"), quest("a", "locked", "after the Tower of the Gods"), quest("b", "fresh"),
                             quest("b", "going"), quest("a", "going2")};
    // a fresh save: nothing saved, nothing written by reading
    auto rows0 = rows(qs, b);
    assert((order(rows0) == std::vector<std::string>{"done", "fresh", "going", "going2", "locked"}));
    assert(rows0[0].status == Status::New && rows0[4].status == Status::NotAvailable);
    for (uint8_t v : raw) assert(v == 0);
    // steps: never back, done stays done
    assert(reach_step(b, qs[3].key, 2) == 2 && reach_step(b, qs[3].key, 1) == 2 && reach_step(b, qs[3].key, 0) == 2);
    assert(reach_step(b, qs[4].key, 1) == 1);
    assert(reach_step(b, qs[0].key, 3) == 3 && finish(b, qs[0].key) == kStepDone && reach_step(b, qs[0].key, 9) == kStepDone);
    assert(reach_step(b, qs[2].key, 255) == -1);  // 255 means done, not a step
    // a started quest with a condition is in progress (the save wins over the session's condition)
    assert(reach_step(b, qs[1].key, 1) == 1);
    auto rows1 = rows(qs, b);
    assert((order(rows1) == std::vector<std::string>{"locked", "going", "going2", "fresh", "done"}));
    assert(rows1[4].status == Status::Done && rows1[0].status == Status::InProgress && rows1[0].step == 1);
    // tracking: one quest at a time, first in the list; choosing it again untracks
    assert(toggle_tracked(b, qs[2].key) && tracked_key(b) == qs[2].key);
    assert((order(rows(qs, b)) == std::vector<std::string>{"fresh", "locked", "going", "going2", "done"}));
    assert(rows(qs, b)[0].tracked);
    assert(toggle_tracked(b, qs[3].key) && tracked_key(b) == qs[3].key);
    unsigned tracked_records = 0;
    for (unsigned r = 0; r < story_mail::kRecords; r++) tracked_records += b.record_kind(r) == story_mail::kRecordTracked;
    assert(tracked_records == 1);
    assert(!toggle_tracked(b, qs[3].key) && tracked_key(b) == 0);
    assert(!toggle_tracked(b, qs[0].key) && tracked_key(b) == 0);  // a done quest can't be tracked
    // the tracked quest's place: only with a place, gone when done
    assert(toggle_tracked(b, qs[3].key));
    assert(!tracked_place(qs, b));
    qs[3].has_place = true, qs[3].x = 100000, qs[3].z = -200000;
    assert(tracked_place(qs, b) == &qs[3]);
    assert(finish(b, qs[3].key) == kStepDone && tracked_key(b) == 0 && !tracked_place(qs, b));
    // a quest of a mod that is off this session: its records stay, it has no row
    const uint16_t gone = quest_key("off", "old");
    assert(reach_step(b, gone, 4) == 4 && toggle_tracked(b, gone));
    auto rows2 = rows(qs, b);
    assert(rows2.size() == qs.size() && !tracked_place(qs, b));
    for (const Row& r : rows2) assert(!r.tracked);
}

void test_record() {
    // a vanilla save (all-zero block) stays all zero while nothing is started, and a block written by
    // something else is never touched
    uint8_t vanilla[story_mail::kBookSize] = {};
    Book v(vanilla);
    assert(rows({quest("a", "x")}, v).size() == 1 && tracked_key(v) == 0);
    for (uint8_t x : vanilla) assert(x == 0);
    uint8_t foreign[story_mail::kBookSize] = {};
    foreign[5] = 0x77;
    uint8_t copy[story_mail::kBookSize];
    memcpy(copy, foreign, sizeof copy);
    Book f(foreign);
    assert(reach_step(f, quest_key("a", "x"), 1) == -1 && !toggle_tracked(f, quest_key("a", "x")) && finish(f, 1) == -1);
    assert(!memcmp(copy, foreign, sizeof copy));
    // the layout of the records (format 2, kinds 4 and 5) and a round trip through the save bytes
    uint8_t raw[story_mail::kBookSize] = {};
    Book b(raw);
    const uint16_t k = quest_key("mod", "tale");
    assert(b.set(story_mail::kRecordItem, story_mail::kItemNotebook, 1));
    assert(reach_step(b, k, 3) == 3 && toggle_tracked(b, k));
    assert(raw[0] == story_mail::kMagic && raw[1] == 2 && raw[2] == 0 && raw[3] == 0);
    assert(raw[8] == 4 && raw[9] == uint8_t(k >> 8) && raw[10] == uint8_t(k) && raw[11] == 3);
    assert(raw[12] == 5 && raw[13] == uint8_t(k >> 8) && raw[14] == uint8_t(k) && raw[15] == 1);
    uint8_t saved_copy[story_mail::kBookSize];
    memcpy(saved_copy, raw, sizeof raw);  // saved and loaded with the save file
    Book loaded(saved_copy);
    assert(saved_step(loaded, k) == 3 && tracked_key(loaded) == k && loaded.get(story_mail::kRecordItem, story_mail::kItemNotebook) == 1);
    // untracking frees the record; other records stay where they are
    assert(!toggle_tracked(loaded, k) && saved_copy[12] == 0 && saved_copy[15] == 0 && saved_copy[11] == 3);
    // a full record: a new step can't be saved, existing ones still change
    uint8_t full[story_mail::kBookSize] = {};
    Book fb(full);
    for (unsigned i = 0; i < story_mail::kRecords; i++) assert(fb.set(story_mail::kRecordLetter, uint16_t(100 + i), 2));
    assert(reach_step(fb, k, 1) == -1 && !toggle_tracked(fb, k));
    assert(fb.set(story_mail::kRecordLetter, 100, 3));
}

void test_text() {
    assert(fit("short", 20, 400) == "short");
    const std::string cut = fit(std::string(200, 'a'), 20, 80);  // 10 characters
    assert(cut.size() == 10 && cut.ends_with("..."));
    auto w = wrap("one two three four five six seven eight nine ten", 20, 120, 2);  // 15 characters a line
    assert(w.size() == 2 && w[0] == "one two three" && w[1].ends_with("..."));
    auto nl = wrap("first\nsecond", 20, 1000, 4);
    assert((nl == std::vector<std::string>{"first", "second"}));
    assert(wrap("", 20, 100, 2).size() == 1);
}

PadInput press(uint32_t buttons) {
    PadInput p;
    p.buttons = buttons;
    return p;
}

void test_page() {
    uint8_t raw[story_mail::kBookSize] = {};
    Book b(raw);
    std::vector<Quest> qs;
    for (int i = 0; i < 7; i++) qs.push_back(quest("m", "q" + std::to_string(i)));
    qs[2].has_place = true;
    auto rs = rows(qs, b);
    Page page;
    assert(!page.open() && page.draw(rs).empty());
    page.show();
    // the A that opened the page from the item menu is still held: nothing happens until released
    assert(page.input(press(kPadA), rs).kind == Page::Action::None && page.mode() == Page::Mode::List);
    assert(page.selected_key() == qs[0].key);
    page.input(press(0), rs);
    // down moves, and keeps the selection visible (5 rows shown)
    for (int i = 0; i < 6; i++) {
        page.input(press(kPadDown), rs);
        page.input(press(0), rs);
    }
    assert(page.selected_key() == qs[6].key && page.top() == 2);
    page.input(press(kPadDown), rs);  // the last row: stays
    page.input(press(0), rs);
    assert(page.selected_key() == qs[6].key);
    // the stick moves too, and held directions repeat
    PadInput stick;
    stick.ly = 0.9f;
    page.input(stick, rs);
    assert(page.selected_key() == qs[5].key);
    for (int i = 0; i < 18; i++) page.input(stick, rs);
    assert(page.selected_key() == qs[4].key);
    page.input(press(0), rs);
    // up to quest 2, open it, track it
    page.input(press(kPadUp), rs);
    page.input(press(0), rs);
    page.input(press(kPadUp), rs);
    page.input(press(0), rs);
    assert(page.selected_key() == qs[2].key);
    assert(page.input(press(kPadA), rs).kind == Page::Action::None && page.mode() == Page::Mode::Detail);
    page.input(press(0), rs);
    Page::Action a = page.input(press(kPadA), rs);
    assert(a.kind == Page::Action::Toggle && a.key == qs[2].key);
    page.input(press(0), rs);
    assert(toggle_tracked(b, a.key));
    rs = rows(qs, b);  // the tracked quest moves to the top; the selection follows the quest
    assert(rs[0].quest == &qs[2] && rs[0].tracked && page.selected_key() == qs[2].key);
    // detail drawing: the button says what A does now
    auto cmds = page.draw(rs);
    bool stop = false;
    for (const Command& c : cmds) stop |= c.kind == Command::Text && c.text == "Stop showing on chart";
    assert(stop);
    // B: back to the list, B again: the page closes
    page.input(press(kPadB), rs);
    assert(page.mode() == Page::Mode::List);
    page.input(press(0), rs);
    assert(page.input(press(kPadB), rs).kind == Page::Action::Close && !page.open());
    // touch: tap a row to open it, tap Show on chart, tap the close mark
    page.show();
    page.input(press(0), rs);
    PadInput tap;
    tap.touch = true, tap.x = kListX + 200, tap.y = kListY + kRowH * 1.5f;  // the second row
    page.input(tap, rs);
    assert(page.mode() == Page::Mode::Detail && page.selected_key() == rs[1].quest->key);
    page.input(press(0), rs);
    tap.x = kTrackX + 20, tap.y = kTrackY + 20;
    a = page.input(tap, rs);
    assert(a.kind == Page::Action::Toggle && a.key == rs[1].quest->key);
    page.input(press(0), rs);
    tap.x = kCloseX, tap.y = kCloseY;
    assert(page.input(tap, rs).kind == Page::Action::Close);
    // a done quest has no Show on chart; the list draws its stamp
    assert(finish(b, qs[0].key) == kStepDone);
    rs = rows(qs, b);
    page.show();
    page.input(press(0), rs);
    cmds = page.draw(rs);
    bool title = false;
    for (const Command& c : cmds) {
        title |= c.kind == Command::Text && c.text == "Story Notebook";
        assert(std::isfinite(c.x) && std::isfinite(c.y));
    }
    assert(title);
    for (size_t i = 0; i < rs.size(); i++) {
        if (rs[i].status != Status::Done) continue;
        for (size_t k = 0; k < i; k++) {
            page.input(press(kPadDown), rs);
            page.input(press(0), rs);
        }
    }
    page.input(press(kPadA), rs);
    page.input(press(0), rs);
    assert(page.mode() == Page::Mode::Detail);
    assert(page.input(press(kPadA), rs).kind == Page::Action::None);
    // an empty notebook
    Page empty;
    empty.show();
    std::vector<Row> none;
    empty.input(press(0), none);
    assert(empty.input(press(kPadA), none).kind == Page::Action::None && empty.mode() == Page::Mode::List);
    bool hint = false;
    for (const Command& c : empty.draw(none)) hint |= c.text == "No stories yet.";
    assert(hint);
}

void test_services() {
    // no save loaded
    assert(define("m", "My mod", "tale", "The tale", "A heart piece", "") == kResultNew);
    assert(step("m", "tale", 1, "Talk to the storyteller.", "Windfall Island") == kResultNoRoom);
    st32(0x101F84DC, kSave);
    // arguments
    assert(define("m", "My mod", "Tale", "x", "", "") == kResultInvalid);
    assert(define("m", "My mod", "t2", "", "", "") == kResultInvalid);                     // no title
    assert(define("m", "My mod", "t2", "two\nlines", "", "") == kResultInvalid);
    assert(define("m", "My mod", "t2", std::string(kMaxTitleBytes + 1, 'a'), "", "") == kResultInvalid);
    assert(step("m", "unknown", 1, "", "") == kResultInvalid);                             // define first
    assert(step("m", "tale", 255, "", "") == kResultInvalid);
    assert(place("m", "tale", 400000, 0) == kResultInvalid && place("m", "tale", NAN, 0) == kResultInvalid);
    assert(known("m", "tale", std::string(kMaxKnownBytes + 1, 'a')) == kResultInvalid);
    assert(portrait("m", "tale", "../x.png", "/nonexistent") == kResultInvalid);
    assert(portrait("m", "tale", "assets/missing.png", "/nonexistent") == kResultInvalid);
    // a quest's life: new, steps, done; the save keeps only the step
    Book b(mem::ptr(kSave + story_mail::kBookOffset));
    assert(state("m", "tale") == kResultNew && b.empty());
    assert(step("m", "tale", 0, "Read the letter.", "") == kResultNew && b.empty());  // texts only
    assert(step("m", "tale", 1, "Talk to the storyteller.", "Windfall Island") == 1);
    assert(step("m", "tale", 1, "Talk to the storyteller.", "Windfall Island") == 1);  // idempotent
    assert(place("m", "tale", 0, -200000) == 1 && known("m", "tale", "A storyteller came to Windfall.\nHe looks for Link.") == 1);
    assert(saved_step(b, quest_key("m", "tale")) == 1);
    assert(define("m", "My mod", "later", "Later", "", "Finish the tale first") == kResultNew);
    std::vector<Quest> storage;
    auto rs = rows_now(storage);
    assert(rs.size() == 2 && rs[0].quest->id == "tale" && rs[0].status == Status::InProgress && rs[1].status == Status::NotAvailable);
    assert(rs[0].quest->has_place && rs[0].quest->where == "Windfall Island" && rs[0].quest->mod_name == "My mod");
    // the tracked quest on the sea chart (story_chart's source)
    assert(toggle_tracked(b, quest_key("m", "tale")));
    assert(no_place("m", "tale") == 1);
    assert(step("m", "tale", 2, "Bring him a Skull Necklace.", "") == 2);
    assert(done("m", "tale") == kResultDone && done("m", "tale") == kResultDone && state("m", "tale") == kResultDone);
    assert(tracked_key(b) == 0 && step("m", "tale", 3, "", "") == kResultDone);
    // per-mod limit
    for (unsigned i = 2; i < kMaxQuestsPerMod; i++) assert(define("m", "My mod", "q" + std::to_string(i), "Q", "", "") == kResultNew);
    assert(define("m", "My mod", "one-more", "Q", "", "") == kResultNoRoom);
    assert(define("m", "My mod", "tale", "The tale, renamed", "", "") == kResultDone);  // an update is no new quest
}

// the game's code for the item menu hooks
uint8_t g_reserve[8] = {0x99, 0xFF, 0xFF, 0x9C, 0xFF, 0xFF, 0xFF, 0xFF};  // Note to Mom, -, -, Cabana Deed, ...
std::map<uint32_t, void (*)(Cpu*)> hooked;
std::vector<std::string> calls;
void original(Cpu* c, uint32_t fn) {
    switch (fn) {
    case 0x02632D94: c->r[3] = c->r[3] >= 0x30 && c->r[3] < 0x38 ? g_reserve[c->r[3] - 0x30] : 0x01; break;
    case 0x0262F880: c->r[3] = c->r[4] == 0x99 ? 0x26 : c->r[4] == 0x9C ? 0x24 : 0x37; break;
    case 0x0268F060: calls.push_back("caption"); break;
    case 0x02630A64: calls.push_back("pick up"); break;
    case 0x0268F420: c->r[3] = c->r[4]; break;  // the stub's receivers are the keys themselves
    case 0x02714B0C: calls.push_back("assign " + std::to_string(c->r[5])); break;
    default: assert(!"unexpected original");
    }
}
uint32_t call(uint32_t fn, uint32_t r3, uint32_t r4 = 0, uint32_t lr = 0, uint32_t r5 = 0) {
    Cpu c{};
    c.r[3] = r3, c.r[4] = r4, c.r[5] = r5, c.lr = lr;
    hooked.at(fn)(&c);
    return c.r[3];
}

void test_hooks() {
    constexpr uint32_t kMenu = kArena + 0x2000, kChildData = kArena + 0x3000, kChild = kArena + 0x4000;
    constexpr uint32_t kBuild = 0x02692548;  // the bag window's build
    // not installed without a quest service; all or nothing on a conflict
    set_game_calls(original);
    install([](uint32_t) { return std::string(); }, [](uint32_t, void (*)(Cpu*)) { assert(!"not imported"); });
    assert(!installed());
    note_import();
    install([](uint32_t a) { return a == 0x02714B0C ? std::string("other") : std::string(); },
            [](uint32_t, void (*)(Cpu*)) { assert(!"installed despite a conflict"); });
    assert(!installed());
    install([](uint32_t) { return std::string(); }, [](uint32_t a, void (*fn)(Cpu*)) { hooked[a] = fn; });
    assert(installed() && hooked.size() == 6);
    Book b(mem::ptr(kSave + story_mail::kBookOffset));
    // no notebook yet: the bag window shows the save as it is
    b.erase(story_mail::kRecordItem, story_mail::kItemNotebook);
    for (uint32_t k = 0x30; k < 0x38; k++) assert(call(0x02632D94, k, 0, kBuild) == g_reserve[k - 0x30]);
    // the notebook in the first empty place, for the bag window only (not for X/Y/R or other callers)
    assert(b.set(story_mail::kRecordItem, story_mail::kItemNotebook, 1));
    assert(call(0x02632D94, 0x31, 0, kBuild) == 0xA0 && call(0x02632D94, 0x31, 0, 0x02693C24) == 0xA0);
    assert(call(0x02632D94, 0x32, 0, kBuild) == 0xFF && call(0x02632D94, 0x30, 0, kBuild) == 0x99);
    assert(call(0x02632D94, 0x31, 0, 0x0266FDFC) == 0xFF);  // the X/Y/R strip
    assert(call(0x02632D94, 0x12, 0, kBuild) == 0x01);       // other keys untouched
    // its icon: the parchment; other items keep theirs
    assert(call(0x0262F880, 0, 0xA0) == 0x24 && call(0x0262F880, 0, 0x99) == 0x26 && call(0x0262F880, 0, 0xFF) == 0x37);
    // A on its place opens the page, not a caption; A on a game item shows the game's caption
    st32(kMenu + 0x48, 0x2E);
    st32(kMenu + 0x4C, kChildData);
    st32(kChildData + 4 * 0x15, kChild);
    st32(kChild + 0xEC, 0xA0);
    st32(kChildData + 4 * 0x14, kChild + 0x200);
    st32(kChild + 0x200 + 0xEC, 0x99);
    calls.clear();
    call(0x0268F060, kMenu, 0x14);
    assert(calls == std::vector<std::string>{"caption"} && !page_open());
    call(0x0268F060, kMenu, 0x15);
    assert(calls.size() == 1 && page_open());
    // while open the game's controller is idle, and the B that closes the page stays with the page
    input::PadState p;
    p.buttons = input::kA;
    pad(p);
    assert(p.buttons == 0 && page_open());
    p = {};
    pad(p);
    p.buttons = input::kB;
    pad(p);
    assert(!page_open() && p.buttons == 0);
    input::PadState pro;
    pro.buttons = input::kB | input::kY;
    mute(pro);
    assert(pro.buttons == input::kY);  // the Pro Controller read: the held B is kept back too
    p.buttons = input::kB;
    pad(p);
    assert(p.buttons == 0);
    p = {};
    pad(p);  // released
    p.buttons = input::kB;
    pad(p);
    assert(p.buttons == input::kB);  // a new press reaches the game
    // a touch on the notebook: the game's pick-up, then the page; on a game item only the pick-up
    calls.clear();
    call(0x02630A64, kChild + 0x200);
    assert(calls == std::vector<std::string>{"pick up"} && !page_open());
    call(0x02630A64, kChild);
    assert(calls.size() == 2 && page_open());
    close_page();
    p = {};
    pad(p);
    // the notebook can't go on X/Y/R
    calls.clear();
    assert(call(0x0268F420, kMenu, 0x31) == 0xFF && call(0x0268F420, kMenu, 0x30) == 0x30);
    call(0x02714B0C, 0, 1, 0, 0x31);
    call(0x02714B0C, 0, 1, 0, 0x30);
    assert(calls == std::vector<std::string>{"assign 48"});
    // a full bag: no place for the notebook until one is free
    uint8_t keep[8];
    memcpy(keep, g_reserve, 8);
    memset(g_reserve, 0x8C, 8);
    assert(call(0x02632D94, 0x31, 0, kBuild) == 0x8C);
    memcpy(g_reserve, keep, 8);
    // the tracked quest's marker reaches the sea chart layer
    assert(define("c", "Chart mod", "chart", "Charted", "", "") == kResultNew);
    assert(step("c", "chart", 1, "Sail to the island.", "Windfall Island") == 1 && place("c", "chart", 0, -200000) == 1);
    assert(story_chart::shown_markers().empty());
    assert(toggle_tracked(b, quest_key("c", "chart")));
    auto shown = story_chart::shown_markers();
    assert(shown.size() == 1 && shown[0].x == 0 && shown[0].z == -200000 && shown[0].label == "Charted");
    assert(place("c", "chart", 100000, 0) == 1 && story_chart::shown_markers()[0].x == 100000);  // moves with the quest
    assert(no_place("c", "chart") == 1 && story_chart::shown_markers().empty());               // a step without a place
    assert(place("c", "chart", 0, 0) == 1 && story_chart::shown_markers().size() == 1);
    assert(done("c", "chart") == kResultDone && story_chart::shown_markers().empty());        // gone when done
    // a state load closes the page
    call(0x0268F060, kMenu, 0x15);
    assert(page_open());
    state_loaded();
    assert(!page_open());
}

}  // namespace

int main() {
    assert(map(kGlobals, 0x10000) && map(kArena, 0x10000));
    test_list();
    test_record();
    test_text();
    test_page();
    test_services();
    test_hooks();
    std::puts("story_notebook_test: ok");
    return 0;
}
