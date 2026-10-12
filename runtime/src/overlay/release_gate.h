#pragma once
#include <algorithm>
#include <iterator>

#include "input_map.h"

namespace overlay {

// Hands one controller back to the game after the settings overlay or the game's text prompt had the
// input (issue #153). The inputs the device holds while the overlay has the input (the button that
// confirmed a name, B or Home that closed the menu) stay away from the game until each one is released;
// every other input reaches the game at once.
//
// Before, one latch kept the whole game input until every input of every controller read below 0.3. An
// input that rests above that (a trigger or stick that does not return to zero, an input the game does
// not even use) kept the game without input until the controller was unplugged.
//
// The host's input pump calls filter() with each device's values (input_map pad order) on every poll,
// also while the overlay has the input, and gives the game the filtered values. A device that connects
// gets a new gate, so it holds nothing back.
class ReleaseGate {
public:
    // input_map::controller_state presses a game button above this (also the analog inputs it maps
    // to buttons): an input at or below it is released
    static constexpr float kHeld = 0.5f;

    // captured: the overlay or the text prompt has the input now (overlay::captures(), sampled before
    // and after the settings shortcut was polled, so the press that closes the menu counts as captured)
    void filter(float values[input_map::kPadCount], bool captured) {
        for (int p = 0; p < input_map::kPadCount; p++) {
            const bool held = values[p] > kHeld;
            if (captured) held_back_[p] = held_back_[p] || held;  // the overlay's input
            else held_back_[p] = held_back_[p] && held;           // released: the next press is the game's
            if (captured || held_back_[p]) values[p] = 0;
        }
    }
    // an input still kept from the game (diagnostics, tests)
    bool holds_back() const { return std::any_of(std::begin(held_back_), std::end(held_back_), [](bool b) { return b; }); }
    bool holds_back(int pad) const { return pad >= 0 && pad < input_map::kPadCount && held_back_[pad]; }

private:
    bool held_back_[input_map::kPadCount] = {};
};

}  // namespace overlay
