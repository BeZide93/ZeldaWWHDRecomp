// Story UI layouts: one layout of our own (Layout/<name>.szs) shown with the game's item screen, built,
// animated, drawn and freed by the game's own UI framework (HD decomp FW-layout_mgr / FW-parts_*):
//
//   built   when the game builds its item screen (hdIngameLoadItemScreen, with the item screen's
//           projection and matrix): hdLayoutResMgr_c::cache(name), NoControlPart_l, attachArchives,
//           hdPartsLayout_c + vLoad, then the client's animations bound (bindGroup); then the client's
//           built() (text pane capacities and first texts, story_lyt_text.h)
//   drawn   every time the item menu's screens are drawn and the item screen was among them
//           (hdIngameUi_c::drawPrimaryScreens on the GamePad; drawHudScreens on the TV, where the item menu
//           is with the Pro Controller; the game's menu cursor is drawn by a later pass there, over ours): after all of them (sub window, menu
//           buttons, cursor, messages), with the item screen's viewport; the client's frame() decides (and
//           sets panes / plays animations), then hdParts_c::calcLayout + draw of our parts
//   freed   when the game frees its screens (hdIngameUi_c::finalizeScreens): our parts' finalize + delete,
//           as the game does for its own screens. The archive stays in the layout resource cache (the
//           game's own screens' archives do too) and is reused when the item screen is built again.
//   state load  before their next use the handles are checked against the loaded memory (our objects'
//           vtables and links as built): kept when the objects are there (a portable state, or a full state
//           saved after the build), else dropped without touching the memory; the layout is then built
//           again with the next item screen and until then nothing is drawn (the client falls back).
//
// The archive file: archive_for() serves the client's archive for the game's read of
// Layout/<name>.szs (hle/fs.cpp). Nothing is hooked without a client.
#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

struct Cpu;

namespace story_lyt_screen {

struct Anim {
    std::string name, group;  // anim/<layout>_<name>.bflan, bound to the group's panes
    uint32_t slot = 0;        // play slot: animations of one slot replace each other
};

struct Client {
    std::string layout;        // the layout's name (Layout/<layout>.szs, blyt/<layout>.bflyt)
    std::string archive;       // the archive file on the host
    std::vector<Anim> anims;   // the animation set's entries, in this order
    std::function<void(Cpu*, uint32_t lyt)> built;   // right after the build (lyt: the layout object)
    std::function<bool(Cpu*, uint32_t lyt)> frame;   // every item screen draw: true draws the layout
    const char* owner = "story layout";              // for the log and hook conflicts
};

void set_client(Client client);  // before install()
bool has_client();
void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>));
// The hooks (only with a client, and only when none of the five game functions is replaced already:
// replaced_by(addr) names who replaces it, "" if nobody).
void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace);
void state_loaded();

// the host file for a game read of `guest` (Layout/<layout>.szs of the client), else ""
std::string archive_for(const std::string& guest);

// ---- for the client's callbacks (game thread, layout built) ----
bool ready();                                // the layout is built
uint32_t lyt();                              // the layout object (hdPartsLayout_c +4), 0 if not built
uint32_t anim_set();                         // its animation set (entries in Client::anims order), 0 if none
uint32_t pane(Cpu* c, const char* name);     // a pane by name (cached), 0 if there is none
// pane fields (HD decomp hdPartsPaneSRT_c: translate 0x1C, size 0x3C, flags 0x44 bit 0 visible, alpha
// 0x45; a changed translate / size sets flag 0x10, the game's own way of asking for a new matrix)
void set_visible(uint32_t pane, bool visible);
void set_alpha(uint32_t pane, uint8_t alpha);
void set_translate(uint32_t pane, float x, float y);
void set_size(uint32_t pane, float w, float h);
// animations by name (Client::anims): play from the start at `rate`; jump to a frame; finished?
void play(Cpu* c, const std::string& anim, float rate = 1.0f);
void set_frame(Cpu* c, const std::string& anim, float frame);
bool ended(Cpu* c, const std::string& anim);

uint32_t safe_string(const char* text);  // a sead SafeString in guest memory (kept for the session)

}  // namespace story_lyt_screen
