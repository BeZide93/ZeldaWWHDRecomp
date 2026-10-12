// Story-mod letters: guest mods send a letter that the Rito postbox delivers like the game's own mail
// (docs/mod-sdk-v2.md, "Letters"). The record of received letters is kept in the save data
// (story_mail_book.h); the letter text stays in the sending mod.
//
// Nothing of this runs unless a loaded guest mod imports a wwhd_mail_* service: only then are the
// postbox and message hooks installed (install()).
#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

struct Cpu;

namespace story_mail {

// Results of the guest services (wwhd_guest.h: WWHD_MAIL_*)
inline constexpr int32_t kResultNone = 0, kResultWaiting = 1, kResultRead = 2;
inline constexpr int32_t kResultInvalid = -1, kResultNoRoom = -2;

// wwhd_mail_send: registers the letter's text for this session and, if this save file never got
// the letter, puts it in the postbox. alloc reserves guest memory for the text in the mod's heap.
int32_t send(const std::string& mod, const std::string& letter, const std::string& sender,
             const std::string& text, const std::function<uint32_t(uint32_t)>& alloc);
// wwhd_mail_state: the letter's state in the current save file
int32_t state(const std::string& mod, const std::string& letter);

// Letters of the loaded mods and their state in the current save file (for lists of received letters).
struct LetterInfo {
    std::string mod, id, sender;
    int32_t state;
};
std::vector<LetterInfo> letters();

void note_import();  // a module imports a mail service: hooks are needed
bool imported();
// After all guest modules are loaded. replaced_by(addr) names a mod that replaces that game function
// (empty: none); replace(addr, fn) installs a host replacement. Nothing is installed on a conflict.
void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace);
bool installed();
void state_loaded();  // a full state was loaded: forget the letter being shown

// game code called from the hooks (guestmods::call_original / guest_call in the game)
void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>));

// The hooks, exposed for runtime/tools/story_mail_test.cpp
namespace hooks {
void readable(Cpu* c);
void receive_count(Cpu* c);
void disp_letter_start(Cpu* c);
void present(Cpu* c);
void receive_demo(Cpu* c);
void next_msg(Cpu* c);
void get_message(Cpu* c);
void get_text(Cpu* c);
}  // namespace hooks

}  // namespace story_mail
