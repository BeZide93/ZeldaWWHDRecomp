// Frame interpolation of the screen fade: the same duration at 30, 60, 120 and 240 fps, a change on
// every drawn frame, and a fade started between two calls (runtime/src/interp_fade.h).
#include "../src/interp_fade.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace interp::fade;

namespace {
// mDoGph_calcFade's update (m_Do_graphic.cpp): the alpha it shows, or -1 on the brightness path
float g_shown = -1;
void calc_fade(State& s) {
    if (!s.fade) { g_shown = -1; return; }
    float rate = s.rate + s.speed;
    if (rate < 0.0f) { rate = 0.0f; s.fade = 0; }
    else if (rate > 1.0f) rate = 1.0f;
    s.rate = rate;
    g_shown = rate;
}
struct Run { int steps_to_end; std::vector<float> shown; };
// a fade-in (rate 1 -> 0 at speed -1/26) drawn with `frames` frames per logic step
Run fade_in(int frames) {
    Gate g;
    State s{1.0f, -1.0f / 26.0f, 1};
    Run r{-1, {}};
    for (uint64_t step = 1; step < 100 && r.steps_to_end < 0; step++)
        for (int k = 1; k <= frames; k++) {
            if (frames == 1) calc_fade(s);  // interpolation off: the hook calls the game directly
            else call(g, step, (float)k / frames, s, calc_fade);
            r.shown.push_back(g_shown);
            if (!s.fade && r.steps_to_end < 0) r.steps_to_end = (int)step;
        }
    return r;
}
}  // namespace

// the display fader's per-frame update (child 13): count up to the duration, alpha from the count
void fader_child(Fader& f) {
    if (f.w[0] == 0) return;
    const uint32_t dur = f.w[1] >> 16, n = (f.w[1] & 0xFFFF) + 1;
    f.w[1] = (dur << 16) | n;
    f.w[2] = n * 255 / dur;
    if (n >= dur) f.w[0] = 0;
}
// a stage fade (26 frames at 30 fps) drawn with `frames` frames per step: steps until done, alphas shown
Run fader_run(int frames) {
    FaderGate g;
    Fader f;
    f.w[0] = 3;
    f.w[1] = 26u << 16;
    Run r{-1, {}};
    for (uint64_t step = 1; step < 100 && r.steps_to_end < 0; step++)
        for (int k = 1; k <= frames; k++) {
            if (frames == 1) fader_child(f);
            else fader_call(g, step, k == 1, (float)k / frames, f, fader_child);
            r.shown.push_back((float)f.w[2]);
            if (k == frames && f.w[0] == 0 && r.steps_to_end < 0) r.steps_to_end = (int)step;
        }
    return r;
}

int main() {
    // the display fader: the same duration at every frame rate, a new alpha on every drawn frame
    {
        const Run a30 = fader_run(1), a60 = fader_run(2), a120 = fader_run(4), a240 = fader_run(8);
        assert(a30.steps_to_end == 26);
        assert(a60.steps_to_end == 26 && a120.steps_to_end == 26 && a240.steps_to_end == 26);
        for (size_t i = 0; i < a30.shown.size(); i++) assert(a120.shown[4 * i + 3] == a30.shown[i]);
        for (size_t i = 1; i < a120.shown.size(); i++) assert(a120.shown[i] > a120.shown[i - 1]);
        // ungated (the child on every pass), as before: 4 times as fast
        Fader f;
        f.w[0] = 3;
        f.w[1] = 26u << 16;
        int frames = 0;
        while (f.w[0]) { fader_child(f); frames++; }
        assert(frames == 26);  // 26 drawn frames: 0.22 s at 120 fps instead of 0.87 s
    }
    const Run r30 = fade_in(1), r60 = fade_in(2), r120 = fade_in(4), r240 = fade_in(8);
    // the same number of logic steps (the same time) at every frame rate
    assert(r30.steps_to_end > 20);
    assert(r60.steps_to_end == r30.steps_to_end && r120.steps_to_end == r30.steps_to_end && r240.steps_to_end == r30.steps_to_end);
    // the frame that ends each step shows the 30 fps value
    for (size_t i = 0; i < r30.shown.size(); i++) {
        assert(std::fabs(r120.shown[4 * i + 3] - r30.shown[i]) < 1e-6f);
        assert(std::fabs(r60.shown[2 * i + 1] - r30.shown[i]) < 1e-6f);
    }
    // at 120 fps the fade changes on every drawn frame (it used to run 4x as fast)
    for (size_t i = 1; i + 4 < r120.shown.size(); i++)
        if (r120.shown[i] > 0 && r120.shown[i - 1] > 0) assert(r120.shown[i] < r120.shown[i - 1]);
    // a fade the game starts between two calls of a step restarts the gate
    {
        Gate g;
        State s{0, 0, 0};
        call(g, 1, 0.25f, s, calc_fade);          // nothing fading
        s = State{0.0f, 0.1f, 1};                  // fadeOut started by the step's logic
        call(g, 1, 0.5f, s, calc_fade);
        assert(std::fabs(s.rate - 0.1f) < 1e-6f && s.fade == 1);  // advanced once, kept exact
        call(g, 1, 0.75f, s, calc_fade);
        assert(std::fabs(g_shown - 0.075f) < 1e-6f);                // shown at 3/4 of the step
        assert(std::fabs(s.rate - 0.1f) < 1e-6f);
        call(g, 2, 0.25f, s, calc_fade);
        assert(std::fabs(s.rate - 0.2f) < 1e-6f);                   // the next step advances once
        assert(std::fabs(g_shown - 0.125f) < 1e-6f);
    }
    printf("interp_fade: all tests passed (fade-in %d steps, stage fader 26 steps at 30/60/120/240 fps)\n", r30.steps_to_end);
    return 0;
}
