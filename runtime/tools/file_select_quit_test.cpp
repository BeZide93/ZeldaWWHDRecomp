#include "file_select_quit.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main() {
    using namespace file_select_quit;
    for (bool setting : {false, true})
        for (bool idle : {false, true}) {
            Routing routing;
            input::PadState pad{
                input::kA | input::kB | input::kX | input::kY | input::kPlus, .25f, -.5f, .75f, 1, true, .4f, .6f};
            const auto original = pad;
            const bool visible = setting && idle;
            assert(routing.filter(pad, visible) == visible);
            auto expected = original;
            if (visible)
                expected.buttons &= ~input::kX;
            assert(pad.buttons == expected.buttons && pad.lx == expected.lx && pad.ly == expected.ly &&
                   pad.rx == expected.rx && pad.ry == expected.ry && pad.touch == expected.touch &&
                   pad.tx == expected.tx && pad.ty == expected.ty);
            pad = original;
            assert(!routing.filter(pad, visible));
            pad = {};
            assert(!routing.filter(pad, visible));
            pad = original;
            assert(routing.filter(pad, visible) == visible);
        }
    // A button held before the UI appears does not activate Quit.
    Routing routing;
    input::PadState pad{input::kX};
    assert(!routing.filter(pad, false));
    assert(pad.buttons == input::kX);
    assert(!routing.filter(pad, true));
    assert(pad.buttons == 0);
    // Turning the option off immediately restores every input, including X.
    pad.buttons = input::kX;
    assert(!routing.filter(pad, false));
    assert(pad.buttons == input::kX);
    puts("file_select_quit_test: passed");
}
