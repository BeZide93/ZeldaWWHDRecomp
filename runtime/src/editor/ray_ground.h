// The world editor's pick: the first ground a ray meets, found with the game's ground check only
// (`ground(x, y, z, h)`: the height `h` of the highest ground at x/z at or below y; false: none).
//
// The ray is walked in steps. For each step the ground is looked up below the top of that step (the
// higher of its two ends, plus a little): the ray has met the ground when its far end is at or below
// that ground. The crossing is then narrowed with the same lookup height, so a thin surface (a pier,
// a roof, a bridge) above lower ground is found as well as solid terrain, and the nearest one wins.
// Header-only so the contract test (editor_test) runs it against a made-up world.
#pragma once

#include <algorithm>
#include <cmath>

#include "../mods/story_world_manifest.h"

namespace editor {

template <class Ground>
bool ray_ground(const story_notebook::WorldPoint& from, const story_notebook::WorldPoint& d, float range, Ground&& ground,
                story_notebook::WorldPoint& hit, double step = 60) {
    if (!(range > 0) || !(step > 0)) return false;
    auto at = [&](double t, int axis) { return axis == 0 ? from.x + d.x * t : axis == 1 ? from.y + d.y * t : from.z + d.z * t; };
    // the ground at the ray's point t, looked up from `top`; true when the ray is at or below it there
    auto below = [&](double t, double top, float& h) {
        return ground(at(t, 0), top, at(t, 2), h) && at(t, 1) <= double(h);
    };
    double prev = 0;
    for (double t = step;; t += step) {
        const double tt = std::min<double>(t, range);
        const double top = std::max(at(prev, 1), at(tt, 1)) + 1;
        float h = 0;
        if (below(tt, top, h)) {
            double lo = prev, hi = tt;
            float hh = h;
            for (int i = 0; i < 20 && hi - lo > 0.05; i++) {
                const double mid = (lo + hi) / 2;
                float hm = 0;
                if (below(mid, top, hm)) hi = mid, hh = hm;
                else lo = mid;
            }
            hit = {float(at(hi, 0)), hh, float(at(hi, 2))};
            return true;
        }
        prev = tt;
        if (tt >= range) return false;
    }
}

}  // namespace editor
