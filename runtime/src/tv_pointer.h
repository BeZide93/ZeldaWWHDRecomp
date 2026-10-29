// The mouse (or a finger) on the TV picture for the port's own pages drawn there: the item menu's
// settings page with the Pro Controller, where the game shows its menu on the TV and the GamePad's
// touch screen is not where the player looks. A page says it wants the pointer (set_wanted, game
// thread, every controller read while it is on the TV); while it does, the hosts (AppKit
// gfx/display.mm, SDL gfx/vulkan/backend.cpp) hand left clicks and drags on the TV picture to it
// instead of the game's mouse handling (mouse camera capture). Positions: 0..1 of the TV picture as
// shown (from the top left) with that picture's width / height, so a page can map them onto its
// height-based canvas (guest_hud.h anchored()).
#pragma once
#include <atomic>

namespace tv_pointer {

struct State {
    bool down = false;
    float x = 0, y = 0;     // 0..1 of the TV picture
    float aspect = 16.0f / 9.0f;  // its width / height as shown
};

// Several parts may want the pointer, each by its own owner bit (the settings page, the talks by touch,
// mods/touch_talk.cpp); the hosts send it here while any does.
enum Owner : unsigned { kOwnerPage = 1, kOwnerTalk = 2 };
void set_wanted(bool on);  // the item menu's settings page (kOwnerPage): while true, clicks on the TV picture come here
void set_wanted(unsigned owner, bool on);
bool wanted();
bool wanted(unsigned owner);
void set(bool down, float x, float y, float aspect);  // hosts (main thread)
State read();                                         // any thread
// The mouse wheel over a code mod's claim on the TV picture (mods/touch_talk.cpp turns it into a pinch for that
// mod, guest_live.h): hosts add notches (+ away from the player: spread / nearer) with the pointer's place;
// take_wheel returns the notches since the last call and that place (false: none)
void add_wheel(float notches, float x, float y, float aspect);  // hosts (main thread)
bool take_wheel(float& notches, State& at);                     // game thread

// The point in a 1280 x 720 page centred on the picture (the HUD's centre anchor: a wider picture
// extends the canvas to both sides)
inline void to_page(const State& s, float& px, float& py) {
    const float width = 720.0f * (s.aspect > 0.1f ? s.aspect : 16.0f / 9.0f);
    px = s.x * width - (width - 1280.0f) * 0.5f, py = s.y * 720.0f;
}

}  // namespace tv_pointer
