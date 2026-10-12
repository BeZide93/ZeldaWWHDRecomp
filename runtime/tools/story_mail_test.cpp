// Letters from story mods (runtime/src/mods/story_mail.*): the save record, the letter text, the
// services and the postbox hooks on a synthetic postbox, message object and save. The game's own
// functions are stubbed; no game files are needed.
#include "mods/story_mail.h"
#include "mods/story_mail_book.h"
#include "runtime.h"

#include <cassert>
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

using namespace story_mail;

void log_msg(const char* fmt, ...) {
    (void)fmt;
}

namespace {

// synthetic guest memory: the game's data page (letter table, save pointer, event name), the save,
// a postbox actor, a message object and the mod heap
constexpr uint32_t kData = 0x10030000, kGlobals = 0x101F0000, kArena = 0x20000000;
constexpr uint32_t kSave = kArena, kActor = kArena + 0x2000, kPlay = kArena + 0x4000, kMsg = kArena + 0x8000,
                   kSet = kArena + 0x9000, kHeap = kArena + 0x10000, kStaff = 5;
constexpr uint32_t kCarrier = 0x1234;  // the first table letter's message number
constexpr uint32_t kMsbt = 0x30001000, kIndex = 42, kGameText = 0x30002000;

void* map(uint32_t at, uint32_t size) {
#ifdef _WIN32
    return VirtualAlloc(mem::ptr(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    return mmap(mem::ptr(at), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
#endif
}

uint32_t heap_top = kHeap;
uint32_t alloc(uint32_t n) {
    uint32_t at = heap_top;
    heap_top += (n + 15) & ~15u;
    return at;
}

// the game's code
int game_readable = 0, game_waiting = 0;
std::vector<std::string> calls;
bool event_ended = false;
std::map<uint32_t, void (*)(Cpu*)> hooked;  // replaced functions: game calls go through the hooks
void original(Cpu* c, uint32_t fn) {
    switch (fn) {
    case 0x023A44A8: c->r[3] = uint32_t(game_readable); calls.push_back("readable"); break;
    case 0x023A51CC: c->r[3] = uint32_t(game_waiting); calls.push_back("count"); break;
    case 0x023A46E4: calls.push_back("disp"); break;
    case 0x023A4700: calls.push_back("present"); break;
    case 0x023A50CC: calls.push_back("demo"); break;
    case 0x023A55BC: c->r[3] = 16; calls.push_back("next"); break;
    case 0x025F50E8:  // getMessage(manager, out, name, label): fills the set and the label index
        st32(c->r[4] + 0x8, kSet);
        st32(c->r[4] + 0x118, kIndex);
        calls.push_back("message");
        break;
    case 0x0273A2E8: c->r[3] = kGameText; calls.push_back("text"); break;
    default: assert(!"unexpected original");
    }
}
uint32_t game_call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args) {
    std::vector<uint32_t> a(args);
    if (hooked.count(fn)) {
        for (size_t i = 0; i < a.size(); i++) c->r[3 + i] = a[i];
        hooked[fn](c);
        return c->r[3];
    }
    switch (fn) {
    case 0x025200D4: return kPlay;
    case 0x025D7DEC:  // the enclosure: a green rupee at the postbox
        assert(a.size() == 7 && a[0] == kActor + 0x314 && a[1] == 1 && a[3] == 0xFFFFFFFFu);
        calls.push_back("item");
        return 77;
    case 0x02543280: assert(a.size() == 2 && a[0] == kPlay + 0x52C4 && a[1] == kStaff); calls.push_back("cutEnd"); return 0;
    case 0x0254457C:
        assert(a.size() == 2 && a[0] == kPlay + 0x52C4 && a[1] == 0x10031464);
        return event_ended;
    case 0x023A40E0: assert(a.size() == 3 && a[0] == kActor); calls.push_back("modeProc" + std::to_string(a[2])); return 0;
    default: assert(!"unexpected game call"); return 0;
    }
}

std::u16string guest_u16(uint32_t at) {
    std::u16string s;
    for (; ld16(at); at += 2) s.push_back(char16_t(ld16(at)));
    return s;
}

void test_book() {
    uint8_t raw[kBookSize] = {};
    Book b(raw);
    assert(b.empty() && b.usable());
    const uint32_t k1 = letter_key("mod-a", "hello"), k2 = letter_key("mod-b", "hello");
    assert(k1 && k2 && k1 != k2 && k1 <= 0xFFFFFF && letter_key("mod-a", "hello") == k1);
    assert(letter_key("ab", "c") != letter_key("a", "bc"));  // the separator counts
    assert(b.state(k1) == kNone);
    assert(b.set(k1, kWaiting) && b.state(k1) == kWaiting && b.state(k2) == kNone);
    assert(raw[0] == kMagic && raw[1] == kVersion && raw[4] == uint8_t(k1 >> 16) && raw[7] == kWaiting);
    assert(b.set(k1, kRead) && b.state(k1) == kRead && raw[11] == 0);  // updated in place
    for (unsigned i = 1; i < kRecords; i++) assert(b.set(letter_key("m", "l" + std::to_string(i)), kWaiting));
    assert(!b.set(k2, kWaiting) && b.state(k2) == kNone);  // full
    assert(b.state(k1) == kRead);
    // data this port did not write is left alone
    uint8_t foreign[kBookSize] = {};
    foreign[10] = 7;
    Book f(foreign);
    assert(!f.usable() && !f.set(k1, kWaiting) && foreign[0] == 0 && f.state(k1) == kNone);
    foreign[0] = kMagic, foreign[1] = 9;
    assert(!Book(foreign).usable());
}

void test_text() {
    assert(valid_id("hello") && valid_id("chapter-1.part_2") && !valid_id("") && !valid_id("Hello") &&
           !valid_id("a b") && !valid_id(std::string(33, 'a')) && valid_id(std::string(32, 'a')));
    assert(compose("Me", "Hi\nthere\n\n") == u"Hi\nthere\n\nMe");
    assert(compose("Zoë", "Grüße ✉") == u"Grüße ✉\n\nZoë");
    assert(compose("A", "\xF0\x9F\x90\x9F") == u"\xD83D\xDC1F\n\nA");  // a surrogate pair
    assert(compose("", "text").empty() && compose("A", "").empty() && compose("A", "\n\n").empty());
    assert(compose("A\nB", "text").empty());                              // the sender is one line
    assert(compose("A", "tab\there").empty() && compose("A", "a\x0E" "b").empty());  // no control codes
    assert(compose("A", "a\rb").empty() && compose("A", "\xC2\x85").empty());
    assert(compose("A", "\xC0\xAF").empty() && compose("A", "\xED\xA0\x80").empty() && compose("A", "\xE2\x82").empty());
    assert(compose(std::string(kMaxSenderBytes + 1, 'a'), "text").empty());
    assert(compose("A", std::string(kMaxTextBytes + 1, 'a')).empty());
    assert(compose("A", std::string(kMaxUnits, 'a')).empty());  // too long with the signature
    assert(!compose("A", std::string(600, 'a')).empty());
}

void test_services() {
    assert(state("mod", "hello") == kResultNone);  // no save loaded yet
    assert(send("mod", "hello", "Me", "Text", alloc) == kResultNoRoom);
    st32(0x101F84DC, kSave);
    assert(send("mod", "hello", "Me", "Text", alloc) == kResultWaiting);
    assert(send("mod", "hello", "Me", "Text", alloc) == kResultWaiting);  // not twice
    assert(state("mod", "hello") == kResultWaiting && state("other", "hello") == kResultNone);
    assert(send("mod", "Bad id", "Me", "Text", alloc) == kResultInvalid);
    assert(send("mod", "bad-text", "Me", "a\x01", alloc) == kResultInvalid);
    assert(state("mod", "bad-text") == kResultNone);
    auto list = letters();
    assert(list.size() == 1 && list[0].mod == "mod" && list[0].id == "hello" && list[0].sender == "Me" &&
           list[0].state == kResultWaiting);
    Book b(mem::ptr(kSave + kBookOffset));
    assert(b.state(letter_key("mod", "hello")) == kWaiting);
}

void test_hooks() {
    // install: all or nothing
    set_game_calls(original, game_call);
    install([](uint32_t) { return std::string(); }, [](uint32_t, void (*)(Cpu*)) { assert(!"not imported"); });
    assert(!installed());
    note_import();
    install([](uint32_t a) { return a == 0x0273A2E8 ? std::string("other") : std::string(); },
            [](uint32_t, void (*)(Cpu*)) { assert(!"installed despite a conflict"); });
    assert(!installed());
    install([](uint32_t) { return std::string(); }, [](uint32_t a, void (*fn)(Cpu*)) { hooked[a] = fn; });
    assert(installed() && hooked.size() == 8);

    st32(0x10031504 + 12 + 4, kCarrier);
    Cpu c{};
    // a game letter goes first, every call passes through
    game_readable = 4;
    c.r[3] = kActor;
    hooks::readable(&c);
    assert(c.r[3] == 4);
    game_readable = 0;
    c.r[3] = kActor;
    hooks::readable(&c);
    assert(c.r[3] == 13);
    game_waiting = 2;
    hooks::receive_count(&c);
    assert(c.r[3] == 3);
    st32(kActor + 0xA04, 4);
    calls.clear();
    for (auto h : {hooks::disp_letter_start, hooks::present, hooks::receive_demo, hooks::next_msg}) {
        c.r[3] = kActor;
        h(&c);
    }
    assert((calls == std::vector<std::string>{"disp", "present", "demo", "next"}));

    // the mod letter (index 13)
    st32(kActor + 0xA04, 13);
    st32(kActor + 0xA08, 2);
    calls.clear();
    st32(kMsg, 0xCF7);  // "letter 2": handed out without postage
    c.r[3] = kActor, c.r[4] = kMsg;
    hooks::next_msg(&c);
    assert(c.r[3] == 16 && ld8(kActor + 0xA01) == 1 && calls.empty());
    c.r[3] = kActor, c.r[4] = kStaff;
    hooks::present(&c);
    assert((calls == std::vector<std::string>{"item", "cutEnd"}) && ld32(kPlay + 0x52A0) == 77);
    c.r[3] = kActor;
    hooks::disp_letter_start(&c);
    assert(ld32(kActor + 0x7C0) == kCarrier);
    // the message system looks the letter up; its text is the mod's
    st32(kSet, kMsbt);
    st32(kMsg + 0x11C, kCarrier);
    c.r[3] = 0x101F4AE8, c.r[4] = kMsg;
    hooks::get_message(&c);
    c.r[3] = kMsbt, c.r[4] = kIndex;
    hooks::get_text(&c);
    assert(guest_u16(c.r[3]) == u"Text\n\nMe");
    c.r[3] = kMsbt, c.r[4] = kIndex + 1;
    hooks::get_text(&c);
    assert(c.r[3] == kGameText);
    // the letter message ends the talk like a game letter
    st32(kMsg, kCarrier);
    c.r[3] = kActor, c.r[4] = kMsg;
    hooks::next_msg(&c);
    assert(c.r[3] == 14);
    // the event is still running: nothing happens
    calls.clear();
    c.r[3] = kActor;
    hooks::receive_demo(&c);
    assert(calls.empty() && state("mod", "hello") == kResultWaiting);
    // the event ends: read, no more letters
    event_ended = true;
    c.r[3] = kActor;
    hooks::receive_demo(&c);
    assert(state("mod", "hello") == kResultRead && ld32(kActor + 0xA04) == 0 && ld32(kActor + 0xA08) == 1);
    assert((ld16(kPlay + 0x52B8) & 8) && calls.back() == "modeProc0");
    // the text is no longer replaced, and the letter isn't sent again
    c.r[3] = kMsbt, c.r[4] = kIndex;
    hooks::get_text(&c);
    assert(c.r[3] == kGameText);
    assert(send("mod", "hello", "Me", "Text", alloc) == kResultRead);
    c.r[3] = kActor;
    hooks::readable(&c);
    assert(c.r[3] == 0);
    game_waiting = 0;
    hooks::receive_count(&c);
    assert(c.r[3] == 0);

    // two mod letters in a row: the demo goes on with the next one
    assert(send("mod", "second", "Me", "Two", alloc) == kResultWaiting);
    assert(send("mod", "third", "Me", "Three", alloc) == kResultWaiting);
    st32(kActor + 0xA04, 13);
    c.r[3] = kActor;
    hooks::disp_letter_start(&c);
    c.r[3] = kActor;
    hooks::receive_demo(&c);
    assert(state("mod", "second") == kResultRead && state("mod", "third") == kResultWaiting);
    assert(ld32(kActor + 0xA04) == 13 && ld8(kActor + 0xA0F) == 1 && ld8(kActor + 0xA03) == 1 && ld32(kActor + 0xA08) == 2);
    assert(calls.back() == "modeProc1");
    // a full state load in the middle of a letter: the first waiting one is marked read
    state_loaded();
    c.r[3] = kActor;
    hooks::receive_demo(&c);
    assert(state("mod", "third") == kResultRead);
}

}  // namespace

int main() {
    assert(map(kData, 0x10000) && map(kGlobals, 0x10000) && map(kArena, 0x20000));
    test_book();
    test_text();
    test_services();
    test_hooks();
    std::puts("story_mail_test: ok");
    return 0;
}
