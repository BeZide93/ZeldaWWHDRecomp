// Story-mod letters delivered by the Rito postbox (story_mail.h, docs/mod-sdk-v2.md "Letters").
//
// How the game delivers mail (public HD decomp: d/d_letter.cpp, d/actor/d_a_obj_toripost.cpp):
//   - A letter is an event register of two bits in the save's event flags (save base + 0x644):
//     0 not sent, 1 sent, 2 waiting in the postbox (dLetter::stock), 3 read (dLetter::read).
//   - The postbox actor (daObjTpost_c) has a fixed table of 12 letters at 0x10031504 (entries 1..12
//     of 12 bytes: +0 needs postage, +4 message number, +8 item, +10 event register). Each step,
//     execute() sets mNumReadable (+0xA04) to the first waiting letter (getReadableLetterNum).
//   - Talking to it, nextMsg() walks the messages "you have mail" (0xCE5/0xCE6), "n letters" (0xCEB,
//     getReceiveLetterNum), "letter k" (0xCF7) and, for letters with postage, the payment (0xCF3...).
//     Then the receive event runs with three cuts: present (the letter's item), set animation, and
//     DispLetter, which shows the letter's message in the letter window (mCurrMsgNo, +0x7C0). When
//     the event ends, modeReceiveDemo() marks the letter read and goes on with the next one.
//
// A mod letter takes the place right after the game's own letters: index 13 (kModIndex), which the
// game's loops (1..12) never reach. The hooks below handle exactly that index and pass every other
// call to the game unchanged:
//   - getReadableLetterNum / getReceiveLetterNum also count waiting mod letters;
//   - for index 13 the table is not read: no postage, a green rupee as the enclosure, the letter
//     window of the game's first letter (its message number is read from the table at run time),
//     the read state goes to the mod letter record;
//   - the letter window shows the mod's text: when the message system looks up that message for the
//     letter (hd_msg_res_mgr getMessage 0x025F50E8), its MSBT set and label index are noted, and the
//     text lookup (0x0273A2E8, text of a label index in an MSBT) returns the mod's text for exactly
//     that entry while the letter is shown.
#include "story_mail.h"
#include "story_mail_book.h"

#include <cstdlib>
#include <map>
#include <mutex>

#include "guest_addr.h"
#include "runtime.h"

namespace story_mail {
namespace {

// canonical USA addresses (guest_addr.h translates them for other builds)
constexpr uint32_t kSavePtr = 0x101F84DC;          // save base pointer (d_com_inf_game)
constexpr uint32_t kLetterTable = 0x10031504;      // daObjTpost_c letter table, 12-byte entries
constexpr uint32_t kReceiveEventName = 0x10031464; // the receive event's name (end check)
constexpr uint32_t kPlay = 0x025200D4;             // the play object
constexpr uint32_t kCutEnd = 0x02543280;           // dEvent_manager_c::cutEnd(staff)
constexpr uint32_t kPresentItem = 0x025D7DEC;      // fopAcM_createItemForPresentDemo(pos, item, ...)
constexpr uint32_t kEndCheck = 0x0254457C;         // dEvent_manager_c::endCheck(name)
constexpr uint32_t kModeProc = 0x023A40E0;         // daObjTpost_c::modeProc(proc, mode)
constexpr uint32_t kReadable = 0x023A44A8;         // daObjTpost_c::getReadableLetterNum
constexpr uint32_t kReceiveCount = 0x023A51CC;     // daObjTpost_c::getReceiveLetterNum
constexpr uint32_t kDispLetterStart = 0x023A46E4;  // daObjTpost_c::cutDispLetterStart(staff)
constexpr uint32_t kPresent = 0x023A4700;          // daObjTpost_c::cutPresentProc(staff)
constexpr uint32_t kReceiveDemo = 0x023A50CC;      // daObjTpost_c::modeReceiveDemo
constexpr uint32_t kNextMsg = 0x023A55BC;          // daObjTpost_c::nextMsg(u32* msg)
constexpr uint32_t kGetMessage = 0x025F50E8;       // message manager: label -> set, index, entry
constexpr uint32_t kGetText = 0x0273A2E8;          // MSBT: text of a label index

// daObjTpost_c fields (d_a_obj_toripost.h) and play fields used by the receive demo
constexpr uint32_t kCurrMsgNo = 0x7C0, kReceivePending = 0xA01, kNextLetter = 0xA03, kNumReadable = 0xA04,
                   kLetterOrdinal = 0xA08, kEventIdx = 0xA0F;
constexpr uint32_t kPlayEventManager = 0x52C4, kPlayEventFlags = 0x52B8, kPlayItemPartner = 0x52A0;
constexpr uint32_t kActorPos = 0x314;  // fopAc_ac_c::current.pos
// message text (hd_msg_text): +8 set, +0x118 label index, +0x11C message number
constexpr uint32_t kMsgSet = 0x8, kMsgLabelIndex = 0x118, kMsgNumber = 0x11C;

constexpr int32_t kModIndex = 13;
// Every game letter encloses an item that Link takes after reading (rupees, a piece of heart...);
// the receive event waits for it. A mod letter encloses the smallest one: a green rupee.
constexpr uint32_t kEnclosedItem = 0x01;
constexpr uint32_t kMsgLetterNumber = 0xCF7;  // "letter k" before a letter is handed out

struct Letter {
    std::string mod, id, sender;
    uint32_t key = 0;
    uint32_t text = 0;  // UTF-16 in the mod's heap, NUL-terminated
};

std::mutex g_mu;
std::vector<Letter> g_letters;  // registered this session, in send order
bool g_imported = false, g_installed = false;

// the mod letter being handed out by a postbox (game thread only)
struct Showing {
    uint32_t key = 0, text = 0, carrier = 0;
    uint32_t msbt = 0, index = 0;
    bool keyed = false;
} g_show;

void (*g_original)(Cpu*, uint32_t) = nullptr;
uint32_t (*g_call)(Cpu*, uint32_t, std::initializer_list<uint32_t>) = nullptr;

bool trace() {
    static const bool on = [] { const char* e = getenv("WWHD_MAIL_TRACE"); return e && *e == '1'; }();
    return on;
}

uint32_t save_base() { return ld32(GD(kSavePtr)); }
Book book() {
    uint32_t base = save_base();
    return Book(base ? mem::ptr(base + kBookOffset) : nullptr);
}
bool have_save() { return save_base() != 0; }

int32_t result_of(State s) { return s == kWaiting ? kResultWaiting : s == kRead ? kResultRead : kResultNone; }

// the first waiting letter that a loaded mod registered this session (key 0: none)
struct Waiting {
    uint32_t key = 0, text = 0;
};
Waiting next_waiting() {
    if (!have_save()) return {};
    Book b = book();
    if (!b.usable()) return {};
    std::lock_guard lock(g_mu);
    for (const Letter& l : g_letters)
        if (b.state(l.key) == kWaiting) return {l.key, l.text};
    return {};
}
int waiting_count() {
    if (!have_save()) return 0;
    Book b = book();
    if (!b.usable()) return 0;
    std::lock_guard lock(g_mu);
    int n = 0;
    for (const Letter& l : g_letters) n += b.state(l.key) == kWaiting;
    return n;
}

uint32_t carrier_message() { return ld32(GD(kLetterTable) + 12 + 4); }  // the first letter's message
uint32_t play(Cpu* c) { return g_call(c, GC(kPlay), {}); }
bool mod_letter(uint32_t self) { return int32_t(ld32(self + kNumReadable)) == kModIndex; }

}  // namespace

int32_t send(const std::string& mod, const std::string& letter, const std::string& sender, const std::string& text,
             const std::function<uint32_t(uint32_t)>& alloc) {
    if (!valid_id(letter)) return kResultInvalid;
    const uint32_t key = letter_key(mod, letter);
    {
        std::lock_guard lock(g_mu);
        bool known = false;
        for (const Letter& l : g_letters) {
            if (l.key != key) continue;
            if (l.mod != mod || l.id != letter) return kResultInvalid;  // a 24-bit key collision
            known = true;
        }
        if (!known) {
            std::u16string body = compose(sender, text);
            if (body.empty()) return kResultInvalid;
            uint32_t at = alloc(uint32_t(2 * (body.size() + 1)));
            if (!at) return kResultInvalid;
            for (size_t i = 0; i < body.size(); i++) st16(at + uint32_t(2 * i), body[i]);
            st16(at + uint32_t(2 * body.size()), 0);
            g_letters.push_back({mod, letter, sender, key, at});
            LOG("[mail] %s: letter \"%s\" from \"%s\" registered", mod.c_str(), letter.c_str(), sender.c_str());
        }
    }
    if (!have_save()) return kResultNoRoom;
    Book b = book();
    State s = b.state(key);
    if (trace()) {  // each change of a letter's state, as the mod sees it
        static std::map<uint32_t, int> seen;
        auto [it, added] = seen.try_emplace(key, -1);
        if (it->second != int(s)) LOG("[mail] %s: letter \"%s\" state %d in this save file", mod.c_str(), letter.c_str(), result_of(s));
        it->second = int(s);
    }
    if (s != kNone) return result_of(s);
    if (!b.set(key, kWaiting)) {
        LOG("[mail] %s: no room for letter \"%s\" in this save file", mod.c_str(), letter.c_str());
        return kResultNoRoom;
    }
    LOG("[mail] %s: letter \"%s\" is waiting in the postbox", mod.c_str(), letter.c_str());
    return kResultWaiting;
}

int32_t state(const std::string& mod, const std::string& letter) {
    if (!valid_id(letter)) return kResultInvalid;
    if (!have_save()) return kResultNone;
    return result_of(book().state(letter_key(mod, letter)));
}

std::vector<LetterInfo> letters() {
    std::vector<LetterInfo> out;
    std::lock_guard lock(g_mu);
    const bool save = have_save();
    for (const Letter& l : g_letters)
        out.push_back({l.mod, l.id, l.sender, save ? result_of(book().state(l.key)) : kResultNone});
    return out;
}

void note_import() { g_imported = true; }
bool imported() { return g_imported; }
bool installed() { return g_installed; }
void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>)) {
    g_original = original;
    g_call = call;
}

void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    if (!g_imported || g_installed || !g_original || !g_call) return;
    const std::pair<uint32_t, void (*)(Cpu*)> hooks[] = {
        {kReadable, hooks::readable},     {kReceiveCount, hooks::receive_count},
        {kDispLetterStart, hooks::disp_letter_start}, {kPresent, hooks::present},
        {kReceiveDemo, hooks::receive_demo}, {kNextMsg, hooks::next_msg},
        {kGetMessage, hooks::get_message}, {kGetText, hooks::get_text},
    };
    // all or nothing: a postbox with only some of the hooks would read past the game's letter table
    for (const auto& [addr, fn] : hooks) {
        std::string other = replaced_by(GC(addr));
        if (!other.empty()) {
            LOG("[mail] %s replaces %08X: letters from mods are not delivered this session", other.c_str(), addr);
            return;
        }
    }
    for (const auto& [addr, fn] : hooks) replace(GC(addr), fn);
    g_installed = true;
    LOG("[mail] postbox delivers letters from mods");
    if (trace())
        for (uint32_t i = 1; i <= 12; i++) {
            const uint32_t e = GD(kLetterTable) + 12 * i;
            LOG("[mail] game letter %u: postage %u message %X item %02X register %04X", i, ld8(e), ld32(e + 4), ld8(e + 8), ld16(e + 10));
        }
}

void state_loaded() { g_show = {}; }

namespace hooks {

// s32 getReadableLetterNum(): the game's first waiting letter, else 13 for a waiting mod letter
void readable(Cpu* c) {
    g_original(c, GC(kReadable));
    if (c->r[3] == 0 && next_waiting().key) c->r[3] = kModIndex;
}

// s16 getReceiveLetterNum(): the game's waiting letters plus the mod letters
void receive_count(Cpu* c) {
    g_original(c, GC(kReceiveCount));
    c->r[3] = uint32_t(int32_t(int16_t(c->r[3] + waiting_count())));
}

// cutDispLetterStart(staff): the letter window with the mod's text
void disp_letter_start(Cpu* c) {
    const uint32_t self = c->r[3];
    if (!mod_letter(self)) {
        g_original(c, GC(kDispLetterStart));
        return;
    }
    const Waiting l = next_waiting();
    g_show = {};
    g_show.carrier = carrier_message();
    if (l.key) g_show.key = l.key, g_show.text = l.text;  // (none: unreachable, the window stays the game's)
    st32(self + kCurrMsgNo, g_show.carrier);
    if (trace()) LOG("[mail] showing letter %06X as message %X", l.key, g_show.carrier);
}

// cutPresentProc(staff): the game's code with the mod letter's enclosure instead of a table entry
void present(Cpu* c) {
    const uint32_t self = c->r[3], staff = c->r[4];
    if (!mod_letter(self)) {
        g_original(c, GC(kPresent));
        return;
    }
    const uint32_t id = g_call(c, GC(kPresentItem), {self + kActorPos, kEnclosedItem, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0});
    const uint32_t p = play(c);
    if (id != 0xFFFFFFFFu) st32(p + kPlayItemPartner, id);
    g_call(c, GC(kCutEnd), {p + kPlayEventManager, staff});
}

// modeReceiveDemo(): after the receive event, mark the mod letter read and go on
void receive_demo(Cpu* c) {
    const uint32_t self = c->r[3];
    if (!mod_letter(self)) {
        g_original(c, GC(kReceiveDemo));
        return;
    }
    if (!g_call(c, GC(kEndCheck), {play(c) + kPlayEventManager, GD(kReceiveEventName)})) return;
    uint32_t key = g_show.key;
    if (!key) key = next_waiting().key;  // a state was loaded while the letter was shown
    if (key && have_save()) book().set(key, kRead);
    if (trace()) LOG("[mail] letter %06X read", key);
    g_show = {};
    const uint32_t next = g_call(c, GC(kReadable), {self});
    st32(self + kNumReadable, next);
    const uint32_t p = play(c);
    st16(p + kPlayEventFlags, uint16_t(ld16(p + kPlayEventFlags) | 8));
    if (next) {
        g_call(c, GC(kModeProc), {self, 0, 1});
        st8(self + kEventIdx, 1);
        st8(self + kNextLetter, 1);
        st32(self + kLetterOrdinal, ld32(self + kLetterOrdinal) + 1);
    } else {
        g_call(c, GC(kModeProc), {self, 0, 0});
        st32(self + kLetterOrdinal, 1);
    }
}

// u16 nextMsg(u32* msg): a mod letter needs no postage and ends like a game letter
void next_msg(Cpu* c) {
    const uint32_t self = c->r[3], msg = c->r[4];
    if (!mod_letter(self)) {
        g_original(c, GC(kNextMsg));
        return;
    }
    if (ld32(msg) == kMsgLetterNumber) {
        st8(self + kReceivePending, 1);
        c->r[3] = 16;
        return;
    }
    g_original(c, GC(kNextMsg));
    // the game compares the shown message with the (nonexistent) table entry 13
    const bool letter_shown = g_show.carrier && ld32(self + kCurrMsgNo) == g_show.carrier;
    const uint32_t status = c->r[3] & 0xFFFF;
    if (status == 16 && letter_shown) c->r[3] = 14;
    else if (status == 14 && !letter_shown) c->r[3] = 16;
}

// getMessage(manager, out, set name, label): note the MSBT entry of the letter being shown
void get_message(Cpu* c) {
    const uint32_t out = c->r[4];
    g_original(c, GC(kGetMessage));
    if (!g_show.text || g_show.keyed || ld32(out + kMsgNumber) != g_show.carrier) return;
    const uint32_t set = ld32(out + kMsgSet);
    if (!set) return;
    g_show.msbt = ld32(set);
    g_show.index = ld32(out + kMsgLabelIndex);
    g_show.keyed = true;
    if (trace()) LOG("[mail] letter text replaces MSBT %08X entry %u", g_show.msbt, g_show.index);
}

// text(msbt, index): the mod's text for the noted entry while the letter is shown
void get_text(Cpu* c) {
    if (g_show.keyed && c->r[3] == g_show.msbt && c->r[4] == g_show.index) {
        c->r[3] = g_show.text;
        return;
    }
    g_original(c, GC(kGetText));
}

}  // namespace hooks
}  // namespace story_mail
