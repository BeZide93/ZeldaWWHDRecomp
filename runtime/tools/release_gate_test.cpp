// overlay/release_gate.h (issue #153): what the game gets back after the settings overlay or the
// text prompt had the input. No SDL, overlay or game involved.
#include <cassert>
#include <cstdio>
#include "input_map.h"
#include "overlay/release_gate.h"

using namespace input_map;

namespace {
struct Device {
    float v[kPadCount] = {};
    overlay::ReleaseGate gate;
    // what the game gets this poll
    float get(int p, bool captured) {
        float out[kPadCount];
        std::copy(std::begin(v), std::end(v), out);
        gate.filter(out, captured);
        return out[p];
    }
};
}  // namespace

int main() {
    // nothing captured, nothing held back
    {
        Device pad;
        pad.v[kPadA] = 1;
        assert(pad.get(kPadA, false) == 1);
        assert(!pad.gate.holds_back());
    }
    // issue #153: inputs that rest above the old 0.3 release threshold (a trigger, a drifting stick, a
    // stuck input the game does not use) must not keep the game's input after the overlay closes
    {
        Device pad;
        pad.v[kPadRT] = 0.4f;
        pad.v[kPadLSRight] = 0.35f;
        pad.v[kPadHome] = 0.45f;
        for (int i = 0; i < 3; i++) assert(pad.get(kPadA, true) == 0);  // the overlay is open
        pad.v[kPadA] = 1;
        assert(pad.get(kPadA, true) == 0);
        pad.v[kPadA] = 0;
        assert(pad.get(kPadA, true) == 0);
        assert(pad.get(kPadA, false) == 0);  // closed (F1 / Esc)
        assert(!pad.gate.holds_back());
        pad.v[kPadA] = 1;
        assert(pad.get(kPadA, false) == 1);  // the next press is the game's
        assert(pad.get(kPadRT, false) == 0.4f && pad.get(kPadLSRight, false) == 0.35f);  // analog values pass
    }
    // the button that confirmed / closed is held across the close: it alone waits for its release
    {
        Device pad;
        pad.v[kPadA] = 1;
        assert(pad.get(kPadA, true) == 0);
        assert(pad.get(kPadA, false) == 0);
        assert(pad.gate.holds_back(kPadA));
        pad.v[kPadB] = 1;
        assert(pad.get(kPadB, false) == 1);  // another button at once
        pad.v[kPadA] = 0.5f;                 // at the game's threshold: released
        assert(pad.get(kPadA, false) == 0.5f);
        assert(!pad.gate.holds_back(kPadA));
        pad.v[kPadA] = 1;
        assert(pad.get(kPadA, false) == 1);
    }
    // a press and release inside the overlay leaves nothing behind
    {
        Device pad;
        pad.v[kPadMenu] = 1;
        pad.get(kPadMenu, true);
        pad.v[kPadMenu] = 0;
        pad.get(kPadMenu, true);
        assert(pad.get(kPadMenu, false) == 0 && !pad.gate.holds_back());
    }
    // a stick pushed while the overlay closes stays out until it returns, then moves the game again
    {
        Device pad;
        pad.v[kPadLSUp] = 1;
        pad.get(kPadLSUp, true);
        assert(pad.get(kPadLSUp, false) == 0);
        pad.v[kPadLSUp] = 0.2f;
        assert(pad.get(kPadLSUp, false) == 0.2f);
        pad.v[kPadLSUp] = 1;
        assert(pad.get(kPadLSUp, false) == 1);
    }
    // each device has its own gate: a second pad's same button is not held back by the first one's
    {
        Device one, two;
        one.v[kPadA] = 1;
        one.get(kPadA, true);
        two.get(kPadA, true);
        assert(one.get(kPadA, false) == 0);
        two.v[kPadA] = 1;
        assert(two.get(kPadA, false) == 1);
    }
    // opened and closed again while still held: still waits for that one release
    {
        Device pad;
        pad.v[kPadOptions] = 1;
        pad.get(kPadOptions, true);
        assert(pad.get(kPadOptions, false) == 0);
        assert(pad.get(kPadOptions, true) == 0);
        assert(pad.get(kPadOptions, false) == 0);
        pad.v[kPadOptions] = 0;
        assert(pad.get(kPadOptions, false) == 0);
        pad.v[kPadOptions] = 1;
        assert(pad.get(kPadOptions, false) == 1);
    }
    puts("release_gate_test: passed");
}
