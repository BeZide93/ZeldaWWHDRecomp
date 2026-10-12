#include "../src/interp_reticle.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace interp::reticle;

// A stationary world target traverses the screen as the camera turns. Logic
// projections provide the endpoints; hold projections see the preceding draw
// camera and deliberately supply a conflicting position here.
int main() {
    for (int frames : {1, 2, 4, 8}) {
        History h;
        h.sample({640, 360, 0}, 10, 1, 1);
        for (int step = 11; step < 20; ++step) {
            float before = 640 - float(step - 11) * 16;
            float exact = before - 16;
            for (int pass = 1; pass <= frames; ++pass) {
                float t = float(pass) / frames;
                Point p = h.sample({pass == 1 ? exact : exact + 32, 360, 0}, step, 1, t);
                assert(std::abs(p[0] - (before - t * 16)) < 0.0001f);
                assert(p[1] == 360 && p[2] == 0);
            }
        }
    }
    History h;
    Point a{1, 2, 3}, b{5, 6, 7};
    assert(h.sample(a, 1, 1, .5f) == a); // newly visible
    assert(h.sample(b, 2, 1, .25f) == (Point{2, 3, 4}));
    assert(h.sample(b, 2, 1, .75f) == (Point{4, 5, 6})); // no drift over holds
    assert(h.sample(b, 2, 1, 1) == b); // exact frame unchanged
    assert(h.sample(a, 4, 1, .5f) == a); // hidden for a step / skipped history
    assert(h.sample(b, 5, 2, .5f) == b); // target slot reused by another actor
    h = {};
    assert(h.sample(a, 5, 2, .5f) == a); // state load
    std::puts("interp_reticle: PASS");
}
