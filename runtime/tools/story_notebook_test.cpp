// The story notebook (runtime/src/mods/story_notebook*): the quest list (order, tracking, state),
// the save record round trips, the services on a synthetic save, the page's navigation and drawing,
// the tracked quest's marker on the sea chart, and the item menu hooks on synthetic menu objects.
// The game's own functions are stubbed; no game files are needed.
#include "mods/story_notebook.h"
#include "mods/story_notebook_list.h"
#include "mods/story_notebook_page.h"
#include "mods/story_notebook_bag.h"
#include "mods/story_takeover.h"
#include "mods/story_manifest.h"
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
namespace input {
bool pro_controller() { return false; }  // the menu on the GamePad
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
    // the done quest is in the archive, not in the quests' list
    assert(page.tab() == 0 && page.shown(rs).size() == rs.size() - 1);
    for (const Row& r : page.shown(rs)) assert(r.status != Status::Done);
    page.input(press(kPadRight), rs);  // the archive
    page.input(press(0), rs);
    assert(page.tab() == 1 && page.shown(rs).size() == 1 && page.shown(rs)[0].quest == &qs[0]);
    assert(page.selected_key() == qs[0].key);
    bool archive = false;
    for (const Command& c : page.draw(rs)) archive |= c.text == "Archive (1)";
    assert(archive);
    page.input(press(kPadA), rs);
    page.input(press(0), rs);
    assert(page.mode() == Page::Mode::Detail);
    assert(page.input(press(kPadA), rs).kind == Page::Action::None);  // no Show on chart for a done quest
    page.input(press(kPadB), rs);
    page.input(press(0), rs);
    page.input(press(kPadLeft), rs);  // back to the quests
    page.input(press(0), rs);
    assert(page.tab() == 0 && page.mode() == Page::Mode::List);
    // a tap on a tab
    PadInput tab;
    tab.touch = true, tab.x = kTab1X + 10, tab.y = kTabY + 10;
    page.input(tab, rs);
    assert(page.tab() == 1);
    page.input(press(0), rs);
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

void test_bag_place() {
    // the icon pane at the item's root, the item in the bag window, the window on the screen root
    std::vector<PaneNode> chain = {{0, 0, 1, 1, false},       // the sentinel icon pane (hidden by the port)
                                   {-131, 130, 1, 1, true},   // the item's root pane
                                   {347, -92, 1, 1, true},    // the bag window
                                   {-73, -3, 1, 1, true}};    // the screen layout's root
    IconPlace at = place_icon(chain, 84, 255, 854, 480);
    assert(at.shown && at.x == 427 + 143 && at.y == 240 - 35 && at.size == 84 && at.alpha == 1);
    // the cursor's highlight scales the item: the icon grows around the same center
    chain[1].sx = chain[1].sy = 1.2f;
    at = place_icon(chain, 84, 255, 854, 480);
    assert(at.shown && at.x == 570 && at.y == 205 && std::fabs(at.size - 100.8f) < 0.01f);
    // a scaled parent scales the child's offset too (the window opening)
    chain[1].sx = chain[1].sy = 1;
    chain[2].sx = chain[2].sy = 0.5f;
    at = place_icon(chain, 84, 255, 854, 480);
    assert(at.x == 427 + 347 - 73 - 65.5f && at.y == 240 - (130 * 0.5f - 92 - 3) && at.size == 42);
    // the window slides
    chain[2].sx = chain[2].sy = 1;
    chain[2].tx += 100;
    assert(place_icon(chain, 84, 255, 854, 480).x == 670);
    // fades with the window, hides with it; NaN or zero scale while panes are built: not shown
    at = place_icon(chain, 84, 51, 854, 480);
    assert(at.shown && std::fabs(at.alpha - 0.2f) < 1e-6f);
    assert(!place_icon(chain, 84, 0, 854, 480).shown);
    chain[2].visible = false;
    assert(!place_icon(chain, 84, 255, 854, 480).shown);
    chain[2].visible = true;
    chain[3].sx = NAN;
    assert(!place_icon(chain, 84, 255, 854, 480).shown);
    chain[3].sx = 0;
    assert(!place_icon(chain, 84, 255, 854, 480).shown);
    assert(!place_icon({}, 84, 255, 854, 480).shown);
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
    case 0x0268FF50: calls.push_back("window"); break;
    case 0x026302B8: calls.push_back("float"); break;
    case 0x026333F0: {  // setVisible: the icon pane follows the item's visibility
        const uint32_t icon = ld32(ld32(c->r[3] + 0x8C + 4));
        st8(icon + 0x44, uint8_t((ld8(icon + 0x44) & ~1u) | (c->r[4] & 1)));
        calls.push_back("visible");
        break;
    }
    case 0x0268F420: c->r[3] = c->r[4]; break;  // the stub's receivers are the keys themselves
    case 0x02714B0C: calls.push_back("assign " + std::to_string(c->r[5])); break;
    case 0x022C6C68: calls.push_back("getMsg"); c->r[3] = 0x2CF1; break;  // the resident's own line
    case 0x022C36D0: calls.push_back("resident"); break;
    default: assert(!"unexpected original");
    }
}
constexpr uint32_t kPlayObj = 0x20030000, kItemActor = 0x2000A000;
uint32_t get_item_over = 0;
uint32_t game_call(Cpu*, uint32_t fn, std::initializer_list<uint32_t> args) {
    std::vector<uint32_t> a(args);
    switch (fn) {
    case 0x025200D4: return kPlayObj;
    case 0x025D7DEC: calls.push_back("present item " + std::to_string(a.at(1))); return 77;
    case 0x025D7B98: calls.push_back("order item event " + std::to_string(a.at(0) == kItemActor)); return 1;
    case 0x025D54C4: st32(a.at(1), a.at(0) == 77 ? kItemActor : 0); return 1;  // fopAcM_SearchByID
    case 0x0254457C: assert(a.at(0) == kPlayObj + 0x52C4 && a.at(1) == 0x10011EB8); return get_item_over;  // endCheck
    default: assert(!"unexpected game call"); return 0;
    }
}
uint32_t call(uint32_t fn, uint32_t r3, uint32_t r4 = 0, uint32_t lr = 0, uint32_t r5 = 0) {
    Cpu c{};
    c.r[1] = 0x2003F000;  // a stack in the synthetic arena
    c.r[3] = r3, c.r[4] = r4, c.r[5] = r5, c.lr = lr;
    hooked.at(fn)(&c);
    return c.r[3];
}

void test_hooks() {
    constexpr uint32_t kMenu = kArena + 0x2000, kChildData = kArena + 0x3000, kChild = kArena + 0x4000;
    constexpr uint32_t kBuild = 0x02692548;  // the bag window's build
    // not installed without a quest service; all or nothing on a conflict
    set_game_calls(original, game_call);
    install([](uint32_t) { return std::string(); }, [](uint32_t, void (*)(Cpu*)) { assert(!"not imported"); });
    assert(!installed());
    note_import();
    install([](uint32_t a) { return a == 0x02714B0C ? std::string("other") : std::string(); },
            [](uint32_t, void (*)(Cpu*)) { assert(!"installed despite a conflict"); });
    assert(!installed());
    install([](uint32_t) { return std::string(); }, [](uint32_t a, void (*fn)(Cpu*)) { hooked[a] = fn; });
    assert(installed() && hooked.size() == 11);
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
    // setVisible: a game item's icon pane shows, the notebook's stays hidden (the port draws its own)
    const uint32_t kGroups = kArena + 0x5000, kIconA = kArena + 0x5100, kIconB = kArena + 0x5200;
    st32(kChild + 0x8C + 4, kGroups);
    st32(kGroups, kIconA);
    st32(kChild + 0x200 + 0x8C + 4, kGroups + 0x10);
    st32(kGroups + 0x10, kIconB);
    calls.clear();
    call(0x026333F0, kChild, 1);
    call(0x026333F0, kChild + 0x200, 1);
    assert(calls.size() == 2 && !(ld8(kIconA + 0x44) & 1) && (ld8(kIconB + 0x44) & 1));
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
    // ---- the notebook's icon over its bag place: only while the Delivery Bag window runs ----
    {
        // panes: the place's icon pane under its root pane, under the window, under the screen root
        const uint32_t kLayout = kArena + 0x6000, kLyt = kArena + 0x6100, kRoot = kArena + 0x6200, kWin = kArena + 0x6300,
                       kScreen = kArena + 0x6400;
        auto pane = [](uint32_t p, uint32_t parent, float x, float y) {
            st32(p + 0xC, parent);
            st32(p + 0x1C, f32_as_u32(x)), st32(p + 0x20, f32_as_u32(y));
            st32(p + 0x34, f32_as_u32(1)), st32(p + 0x38, f32_as_u32(1));
            st8(p + 0x44, 0x01), st8(p + 0x46, 255);
        };
        pane(kIconA, kRoot, 0, 0);
        pane(kRoot, kWin, -131, 130);
        pane(kWin, kScreen, 347, -92);
        pane(kScreen, 0, 0, 0);  // the screen root (1280 x 720 layout)
        st32(kChild + 0x44, kLayout), st32(kLayout + 4, kLyt), st32(kLyt + 0xC, kRoot);
        st8(kChild + 0xFC, 1), st8(kChild + 0x200 + 0xFC, 1);
        st32(kMenu + 0x54, 2);  // the Delivery Bag window
        // a floating icon (the menu's sMenuIcon) showing the notebook is not its place
        const uint32_t kFloat = kArena + 0x7000;
        st32(kFloat + 0xEC, 0xA0), st8(kFloat + 0xFC, 1), st32(kFloat + 0x8C + 4, kGroups + 0x20), st32(kGroups + 0x20, kArena + 0x7100);
        call(0x026333F0, kFloat, 1);
        next_frame();
        assert(!icon_place().shown && std::string(icon_reason()) == "no bag window yet");
        // the window runs: the icon is over the place, the sentinel icon hidden
        st8(kIconA + 0x44, 1);
        calls.clear();
        call(0x0268FF50, kMenu);
        assert(calls == std::vector<std::string>{"window"});
        next_frame();
        IconPlace at = icon_place();
        // (640 + 216, 360 - 38) in the 1280 x 720 layout, on the 854 x 480 GamePad picture
        assert(at.shown && std::fabs(at.x - 856.0f * 854 / 1280) < 0.01f && std::fabs(at.y - 322.0f * 854 / 1280) < 0.01f &&
               std::fabs(at.size - 90.0f * 854 / 1280) < 0.01f && !(ld8(kIconA + 0x44) & 1));
        for (int i = 0; i < 4; i++) assert(icon_place().shown);  // frames drawn within the step (60/120 fps) keep it
        // the notebook page open over the menu: no bag icon over it
        call(0x0268F060, kMenu, 0x15);
        assert(page_open() && !icon_place().shown && std::string(icon_reason()) == "the notebook page is open");
        close_page();
        assert(icon_place().shown);
        call(0x0268FF50, kMenu);
        next_frame();
        assert(icon_place().shown);
        // the window closes (Back, B, the menu closes, a tab switch): its state no longer runs, the icon goes
        next_frame();
        assert(!icon_place().shown && std::string(icon_reason()) == "the bag window is closed");
        // another bag's window: no icon
        st32(kMenu + 0x54, 0);
        call(0x0268FF50, kMenu);
        next_frame();
        assert(!icon_place().shown);
        st32(kMenu + 0x54, 2);
        // the window fading out (global alpha 0) or hidden
        call(0x0268FF50, kMenu);
        next_frame();
        st8(kRoot + 0x46, 0);
        assert(!icon_place().shown);
        st8(kRoot + 0x46, 255);
        st8(kWin + 0x44, 0);
        assert(!icon_place().shown);
        st8(kWin + 0x44, 1);
        assert(icon_place().shown);
        // X/Y/R on the notebook doesn't lift it; on a game item it does
        calls.clear();
        call(0x026302B8, kChild);
        call(0x026302B8, kChild + 0x200);
        assert(calls == std::vector<std::string>{"float"});
        // a state load: nothing until the window runs again
        state_loaded();
        next_frame();
        assert(!icon_place().shown);
        call(0x0268FF50, kMenu);
        next_frame();
        assert(icon_place().shown);
        // repeated opening by A and by touch, closing with B each time
        for (int i = 0; i < 3; i++) {
            call(0x0268F060, kMenu, 0x15);
            assert(page_open());
            input::PadState p2;
            pad(p2);  // the A that opened it was released
            p2.buttons = input::kB;
            pad(p2);
            assert(!page_open());
            p2 = {};
            pad(p2);
            call(0x02630A64, kChild);
            assert(page_open());
            pad(p2);  // the touch ended
            p2.buttons = input::kB;
            pad(p2);
            p2 = {};
            pad(p2);
            assert(!page_open());
        }
    }
    // a state load closes the page
    call(0x0268F060, kMenu, 0x15);
    assert(page_open());
    state_loaded();
    assert(!page_open());
}

void test_page_screen() {
    // the page goes where the game's item menu is: the GamePad, or the TV with the Pro Controller
    assert(menu_screen(false) == 1 && menu_screen(true) == 0);
    Page page;
    page.show();
    std::vector<Row> none;
    std::vector<Command> tv = page.draw(none), pad = page.draw(none);
    fit_to_screen(tv, 0);
    fit_to_screen(pad, 1);
    assert(tv.size() == pad.size());
    for (size_t i = 0; i < tv.size(); i++) {
        assert(std::fabs(pad[i].x - tv[i].x * 854 / 1280) < 0.01f && std::fabs(pad[i].size - tv[i].size * 854 / 1280) < 0.01f);
        if (tv[i].kind == Command::Rect) assert(std::fabs(pad[i].w - tv[i].w * 854 / 1280) < 0.01f);
    }
}

void test_takeover_rules() {
    uint8_t raw[story_mail::kBookSize] = {};
    Book b(raw);
    const uint16_t a = quest_key("mod-a", "q"), bq = quest_key("mod-b", "q");
    std::vector<Takeover> all = {{a, 1, kResidentWindfallHarbour, true, "A", 6}, {bq, 1, kResidentWindfallHarbour, true, "B", 0}};
    // nothing tracked: nobody speaks for the mods
    assert(reach_step(b, a, 1) == 1 && reach_step(b, bq, 1) == 1);
    assert(b.set(kRecordCarried, a, 1) && b.set(kRecordCarried, bq, 1));
    assert(!live_takeover(all, b, kResidentWindfallHarbour));
    // two mods declare the same resident: only the tracked quest's takeover is live
    assert(toggle_tracked(b, bq));
    const Takeover* t = live_takeover(all, b, kResidentWindfallHarbour);
    assert(t && t->quest == bq && t->lines == "B");
    assert(toggle_tracked(b, a));
    t = live_takeover(all, b, kResidentWindfallHarbour);
    assert(t && t->quest == a);
    // another step, no letter, another resident: not live
    assert(!live_takeover(all, b, kResidentNone));
    b.erase(kRecordCarried, a);
    assert(!live_takeover(all, b, kResidentWindfallHarbour));
    assert(b.set(kRecordCarried, a, 1));
    assert(reach_step(b, a, 2) == 2 && !live_takeover(all, b, kResidentWindfallHarbour));
    // completing hands the letter over and finishes the quest (it leaves the tracked list)
    uint8_t raw2[story_mail::kBookSize] = {};
    Book c(raw2);
    assert(reach_step(c, a, 1) == 1 && c.set(kRecordCarried, a, 1) && toggle_tracked(c, a));
    assert(complete(c, all[0]) && saved_step(c, a) == kStepDone && !carries(c, a) && tracked_key(c) == 0);
    assert(resident_by_id("windfall-harbour") == kResidentWindfallHarbour && resident_by_id("nobody") == kResidentNone);
}

void test_takeover_hooks() {
    constexpr uint32_t kNpc = kArena + 0x8000, kLink = kArena + 0x9000;
    st32(kPlayObj + 0x5B2C, kLink);
    Book b(mem::ptr(kSave + story_mail::kBookOffset));
    // the lantern quest of one mod, the same resident in another mod's quest
    assert(define("lantern", "Lantern", "tale", "The storyteller's lantern", "100 rupees", "") == kResultNew);
    assert(define("other", "Other", "tale", "Another tale", "", "") == kResultNew);
    assert(talk("lantern", "tale", 1, "windfall-harbour", "A letter from him?\nThank you!", 6) == kResultNew);
    assert(talk("other", "tale", 1, "windfall-harbour", "Not me.", 0) == kResultNew);
    assert(talk("lantern", "tale", 1, "nobody", "x", 0) == kResultInvalid);
    assert(talk("lantern", "tale", 1, "windfall-harbour", "1\n2\n3\n4", 0) == kResultInvalid);  // one box: three lines
    assert(talk("lantern", "tale", 1, "windfall-harbour", "x", 0x0F) == kResultInvalid);        // rupees only
    assert(step("lantern", "tale", 1, "Bring the letter to his old friend.", "Windfall Island") == 1);
    assert(step("other", "tale", 1, "", "") == 1);
    assert(carry("lantern", "tale", true) == 1 && carry("other", "tale", true) == 1);
    // the carried letter shows in the next empty bag place after the notebook, with a letter's icon underneath
    assert(b.get(story_mail::kRecordItem, story_mail::kItemNotebook) == 1);
    assert(call(0x02632D94, 0x31, 0, 0x02692548) == 0xA0 && call(0x02632D94, 0x32, 0, 0x02692548) == 0xA1);
    assert(call(0x02632D94, 0x34, 0, 0x02692548) == 0xFF);
    assert(call(0x0262F880, 0, 0xA1) == 0x25);
    assert(call(0x0268F420, 0, 0x32) == 0xFF);  // it can't go on X/Y/R
    st8(kNpc + 0x905, 5);  // the harbour man
    st32(kNpc + 0x8A8, 0x1234), st32(kNpc + 0x8AC, 0);
    // the event flags of the save, to prove nothing is set
    std::vector<uint8_t> flags(mem::ptr(kSave + 0x644), mem::ptr(kSave + 0x644) + 0x100);
    // untracked: the resident's own line
    while (tracked_key(b)) toggle_tracked(b, tracked_key(b));
    calls.clear();
    assert(call(0x022C6C68, kNpc) == 0x2CF1 && calls == std::vector<std::string>{"getMsg"});
    // the hint in the notebook says to track it
    std::vector<Quest> storage;
    for (const Row& r : rows_now(storage))
        if (r.quest->mod == "lantern") assert(r.quest->hint.find("track it") != std::string::npos);
    // the OTHER mod's quest tracked: its line, never the lantern's
    assert(toggle_tracked(b, quest_key("other", "tale")));
    calls.clear();
    assert(call(0x022C6C68, kNpc) == 0x2CF3 && calls.empty());
    assert(ld32(kNpc + 0x8A8) == 0);  // the talk ends after one box
    // the talk runs, then ends: the other quest is done, no reward
    st16(kNpc + 0xF8, 1);
    calls.clear();
    call(0x022C36D0, kNpc);
    assert(calls == std::vector<std::string>{"resident"} && state("other", "tale") == 1);
    st16(kNpc + 0xF8, 0);
    call(0x022C36D0, kNpc);
    assert(state("other", "tale") == kResultDone && !carries(b, quest_key("other", "tale")));
    // now the lantern quest tracked: its line, then the reward through the get-item event
    assert(toggle_tracked(b, quest_key("lantern", "tale")));
    calls.clear();
    assert(call(0x022C6C68, kNpc) == 0x2CF3 && calls.empty());
    assert(call(0x022C6C68, kNpc) == 0x2CF1);  // a second talk while ours runs: the game's
    st16(kNpc + 0xF8, 1);
    call(0x022C36D0, kNpc);
    st16(kNpc + 0xF8, 0);
    calls.clear();
    call(0x022C36D0, kNpc);
    assert(state("lantern", "tale") == kResultDone && !carries(b, quest_key("lantern", "tale")));
    // the get-item event is ordered by the resident who hands the reward over, as the game's items do
    assert((calls == std::vector<std::string>{"resident", "present item 6", "order item event 0"}) && (ld16(kNpc + 0xFA) & 8));
    calls.clear();
    call(0x022C36D0, kNpc);  // not started yet: ordered again, the item created once
    assert((calls == std::vector<std::string>{"resident", "order item event 0"}));
    st16(kNpc + 0xF8, 4);  // the get-item event runs: its item is ours
    call(0x022C36D0, kNpc);
    assert(ld32(kPlayObj + 0x52A0) == 77);
    // the one who ordered the event ends it once the event manager says it is over
    st16(kPlayObj + 0x52B8, 0);
    call(0x022C36D0, kNpc);
    assert(!(ld16(kPlayObj + 0x52B8) & 8));
    get_item_over = 1;
    call(0x022C36D0, kNpc);
    assert(ld16(kPlayObj + 0x52B8) & 8);
    get_item_over = 0;
    st16(kNpc + 0xF8, 0);
    calls.clear();
    call(0x022C36D0, kNpc);
    assert(calls == std::vector<std::string>{"resident"});
    // delivered: the letter has left the bag
    assert(call(0x02632D94, 0x32, 0, 0x02692548) == 0xFF);
    // done: the resident is the game's again
    calls.clear();
    assert(call(0x022C6C68, kNpc) == 0x2CF1);
    // no game flag was set by any of it
    assert(std::equal(flags.begin(), flags.end(), mem::ptr(kSave + 0x644)));
}

const char* const kLanternManifest = R"({
  "format_version": 1, "id": "lantern-mod", "name": "Lantern", "version": "1.0.0", "kind": "guest",
  "quests_format": 1,
  "quests": [{
    "id": "lantern", "title": "The storyteller's lantern", "journal": "on_enable",
    "start": {"text": "Check a Rito postbox for a letter."},
    "letter": {"id": "lantern", "sender": "The storyteller", "text": "Dear Link,\nplease help.", "encloses": ["notebook", "letter:friend"]},
    "reward": {"text": "100 rupees", "item": "rupee_orange"},
    "steps": [{"n": 1, "text": "Bring the letter to his friend.", "where": "Windfall Island", "place": {"cell": "D2", "x": 3827, "z": -202189},
      "npc": {"who": {"class": "people", "code": "UB3"}, "when": {"carries": "letter:friend"},
              "lines": ["One.", "Two."], "then": {"give": "rupee_orange", "take": "letter:friend", "complete": true}}}]
  }, {"id": "secret", "title": "A secret", "journal": "on_trigger"}]
})";

void test_manifest() {
    std::vector<ManifestQuest> qs;
    std::string why;
    assert(parse_quests(mods::json::parse(kLanternManifest), qs, why) && qs.size() == 2);
    const ManifestQuest& q = qs[0];
    assert(q.id == "lantern" && q.start_text == "Check a Rito postbox for a letter." && q.reward_item == 6 && q.has_letter &&
           q.letter_encloses_letter && q.steps.size() == 1);
    assert(q.steps[0].has_place && q.steps[0].x == 3827 && q.steps[0].has_talk && q.steps[0].resident == kResidentWindfallHarbour &&
           q.steps[0].needs_letter && q.steps[0].lines == "One.\nTwo." && q.steps[0].reward == 6);
    // what is refused
    auto refused = [&](const std::string& from, const std::string& to) {
        std::string text = kLanternManifest;
        const size_t at = text.find(from);
        assert(at != std::string::npos);
        text.replace(at, from.size(), to);
        std::vector<ManifestQuest> out;
        std::string reason;
        return !parse_quests(mods::json::parse(text), out, reason) && !reason.empty();
    };
    assert(refused(R"("code": "UB3")", R"("code": "XX9")"));                         // a resident the port doesn't know
    assert(refused(R"(["One.", "Two."])", R"(["1", "2", "3", "4"])"));              // more than one box
    assert(refused(R"("give": "rupee_orange")", R"("give": "master_sword")"));      // only rupees
    assert(refused(R"("quests_format": 1)", R"("quests_format": 2)"));
    assert(refused(R"("id": "lantern", "title")", R"("id": "Lantern!", "title")"));
    assert(refused(R"("journal": "on_enable")", R"("journal": "sometimes")"));
    std::vector<ManifestQuest> none;
    assert(parse_quests(mods::json::parse(R"({"format_version": 1})"), none, why) && none.empty());

    // declared: in the notebook at once (the secret one not), its letter sent, started when read
    Book b(mem::ptr(kSave + story_mail::kBookOffset));
    assert(declare("lantern-mod", "Lantern", mods::json::parse(kLanternManifest)));
    std::vector<Quest> storage;
    bool found = false;
    for (const Row& r : rows_now(storage)) {
        assert(r.quest->id != "secret");
        if (r.quest->id == "lantern") {
            found = true;
            assert(r.status == Status::New && r.quest->next == "Check a Rito postbox for a letter.");
        }
    }
    assert(found);
    declared_tick();
    const uint16_t letter = story_mail::letter_key("lantern-mod", "lantern"), key = quest_key("lantern-mod", "lantern");
    assert(b.state(letter) == story_mail::kWaiting && saved_step(b, key) == 0);
    declared_tick();
    assert(saved_step(b, key) == 0);  // waiting: nothing yet
    assert(b.set_state(letter, story_mail::kRead));  // read at a postbox
    declared_tick();
    assert(saved_step(b, key) == 1 && carries(b, key));
    declared_tick();
    assert(saved_step(b, key) == 1);
    for (const Row& r : rows_now(storage))
        if (r.quest->id == "lantern")
            assert(r.quest->next == "Bring the letter to his friend." && r.quest->has_place && r.quest->x == 3827 &&
                   r.quest->hint.find("track it") != std::string::npos);
}

void test_available() {
    // the evaluator: event flags (byte, mask) and the time of day
    uint8_t flags[0x100] = {};
    Availability a;
    a.events = {0x2D01}, a.not_events = {0x0A02}, a.time = 1, a.text = "after the pearl, by day";
    assert(!available(a, flags, 12));
    flags[0x2D] |= 0x01;
    assert(available(a, flags, 12) && !available(a, flags, 20) && !available(a, flags, 5));
    flags[0x0A] |= 0x02;  // the Endless Night
    assert(!available(a, flags, 12));
    Availability none;
    assert(!none.any() && available(none, flags, 3));
    // the manifest form, and what it refuses
    const char* m = R"({"quests_format": 1, "quests": [{"id": "q", "title": "Q", "steps": [{"n": 1, "text": "Go.",
        "available": {"event": "0x2D01", "not_event": ["0x0A02"], "time": "day", "text": "after Nayru's pearl, by day"},
        "npc": {"who": {"class": "people", "code": "UM1"}, "lines": ["Hi."]}}]}]})";
    std::vector<ManifestQuest> qs;
    std::string why;
    assert(parse_quests(mods::json::parse(m), qs, why));
    const ManifestStep& st = qs[0].steps[0];
    assert(st.when.events == std::vector<uint16_t>{0x2D01} && st.when.not_events == std::vector<uint16_t>{0x0A02} && st.when.time == 1 &&
           st.when.text == "after Nayru's pearl, by day" && st.resident == kResidentWindfallLightKeeper && !st.needs_letter);
    auto refused = [](std::string text, const std::string& from, const std::string& to) {
        text.replace(text.find(from), from.size(), to);
        std::vector<ManifestQuest> out;
        std::string reason;
        return !parse_quests(mods::json::parse(text), out, reason);
    };
    assert(refused(m, "\"0x2D01\"", "\"2D01\""));
    assert(refused(m, "\"day\"", "\"noon\""));
    assert(refused(m, ", \"text\": \"after Nayru's pearl, by day\"", ""));
    // a talk whose step isn't available yet is not live
    uint8_t raw[story_mail::kBookSize] = {};
    Book b(raw);
    const uint16_t k = quest_key("m", "q");
    assert(reach_step(b, k, 1) == 1 && toggle_tracked(b, k));
    std::vector<Takeover> all = {{k, 1, kResidentWindfallLightKeeper, false, "Hi.", 0, st.when}};
    uint8_t game[0x100] = {};
    assert(!live_takeover(all, b, kResidentWindfallLightKeeper, game, 12));
    game[0x2D] = 0x01;
    assert(live_takeover(all, b, kResidentWindfallLightKeeper, game, 12) && !live_takeover(all, b, kResidentWindfallLightKeeper, game, 22));
    // in the notebook: "Not yet possible: ..." and no place until it holds (the save's flags and time)
    assert(declare("avail-mod", "Avail", mods::json::parse(m)));
    Book save(mem::ptr(kSave + story_mail::kBookOffset));
    assert(reach_step(save, quest_key("avail-mod", "q"), 1) == 1);
    st32(kSave + 0x44, f32_as_u32(180.0f));  // noon
    std::vector<Quest> storage;
    auto row = [&]() -> const Quest* {
        static std::vector<Quest> keep;
        for (const Row& r : rows_now(keep))
            if (r.quest->mod == "avail-mod") return r.quest;
        return nullptr;
    };
    assert(row() && row()->hint == "Not yet possible: after Nayru's pearl, by day" && !row()->has_place);
    st8(kSave + 0x644 + 0x2D, 0x01);  // the game set the flag
    assert(row() && row()->hint.find("Not yet possible") == std::string::npos);
    st8(kSave + 0x644 + 0x2D, 0);
    (void)storage;
}

}  // namespace

int main() {
    assert(map(kGlobals, 0x10000) && map(kArena, 0x40000));
    test_list();
    test_record();
    test_text();
    test_page();
    test_bag_place();
    test_services();
    test_hooks();
    test_page_screen();
    test_takeover_rules();
    test_takeover_hooks();
    test_manifest();
    test_available();
    std::puts("story_notebook_test: ok");
    return 0;
}
