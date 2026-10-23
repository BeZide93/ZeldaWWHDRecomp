// Host-only file-select Quit UI. No guest state is changed.
#pragma once
#include "input.h"
#include <cstdint>
struct Cpu;
namespace file_select_quit {
// Input routing is kept independent of the renderer for regression tests.
struct Routing {
    bool x_held = false;
    bool filter(input::PadState &pad, bool visible) {
        const bool down = (pad.buttons & input::kX) != 0;
        const bool pressed = visible && down && !x_held;
        x_held = down;
        if (visible)
            pad.buttons &= ~input::kX;
        return pressed;
    }
};
void observe(Cpu *cpu); // guest draw thread, at the title background's draw
void filter(input::PadState &pad);
bool visible();
bool confirming();
bool pointer(float x, float y); // normalized TV coordinates
void frame(float width, float height);
void backend_destroyed();
bool image_texture(uint64_t id); // native layout images encoded as sRGB
} // namespace file_select_quit
