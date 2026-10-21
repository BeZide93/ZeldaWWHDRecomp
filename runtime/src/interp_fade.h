// Frame interpolation of the screen fade, without game state (interp.cpp, mDoGph_calcFade hook;
// runtime/tools/interp_fade_test.cpp, ctest interp_fade).
//
// mDoGph_calcFade (025F06A8) advances the full-screen fade (l_graphics: mFadeRate += mFadeSpeed,
// clamped to 0..1; mFade off once the rate drops below 0) and applies its colour. The game calls it
// once per drawn frame, so with frame interpolation every fade ran 2x/4x/8x too fast (at 120 fps the
// fade-in at a stage entry took 0.22 s instead of 0.87 s). Gated here: the fade advances once per
// logic step (the step's first call), and every call shows it at the pass's fraction of that step
// (the original update run from the step's start with the speed scaled by the fraction), so the
// fade takes the same time at 30, 60, 120 and 240 fps and changes on every drawn frame. The exact
// state of the step is put back after each call. A fade the game starts or changes between two
// calls (fadeIn/fadeOut set rate, speed and the flag) starts a new gate.
#pragma once
#include <cstdint>

namespace interp::fade {

struct State {
    float rate = 0, speed = 0;
    uint8_t fade = 0;  // 1: fading (rate/speed drive the colour); 0: the brightness path, nothing advances
    bool operator==(const State& o) const { return rate == o.rate && speed == o.speed && fade == o.fade; }
};
struct Gate {
    uint64_t step = ~0ull;
    State before, after;  // the guest state at the step's start and after its one advance
};

// One call on a pass at fraction `frac` (0 < frac <= 1) of logic step `step`. `s` is the guest
// state; orig(s) runs the game's update on it (and applies the colour).
template <class Orig>
void call(Gate& g, uint64_t step, float frac, State& s, Orig&& orig) {
    if (g.step != step || !(s == g.after)) {  // the step's first call, or the game started a fade
        g.step = step;
        g.before = s;
        orig(s);
        g.after = s;
        if (frac >= 1.0f || !g.before.fade) return;
    }
    if (g.before.fade) {  // shown at frac: from the step's start with the speed scaled
        s.rate = g.before.rate;
        s.speed = frac * g.before.speed;
        s.fade = 1;
    } else {
        s = g.before;
    }
    orig(s);
    s = g.after;  // the exact state of the step
}

// The HD display's fader (the stage fades: dOvlpFd_startFadeInHD 02728CB8 starts it, 26 frames)
// is advanced by the per-frame function's child 13 (02728A74), which runs on every drawn pass:
// +0 state (0 idle, 2/3 fading), +4 duration << 16 | frame counter, +8 alpha (0..255, read when the
// fader layer is painted). Gated the same way: the child advances it on the logic pass only (on the
// other passes its advance is taken back), and every pass shows the alpha at its fraction between
// the step's start and its end. A fader the game starts or changes between two passes (its logic
// runs before the child on the logic pass) is taken as it is.
struct Fader {
    uint32_t w[3] = {0, 0, 0};  // state, duration/counter, alpha
    bool operator==(const Fader& o) const { return w[0] == o.w[0] && w[1] == o.w[1] && w[2] == o.w[2]; }
};
struct FaderGate {
    uint64_t step = ~0ull;
    Fader before, after, shown;
    bool have = false;
};
// One run of the child on a pass at fraction `frac` of logic step `step` (`logic`: the step's logic
// pass). `f` is the guest fader; child(f) runs the child on it. Leaves the shown state in f.
template <class Child>
void fader_call(FaderGate& g, uint64_t step, bool logic, float frac, Fader& f, Child&& child) {
    if (g.have && f == g.shown) f = g.after;  // the step's exact state (nothing changed it since)
    if (logic || g.step != step || !(f == g.after)) {
        g.before = f;
        child(f);
        g.after = f;
        g.step = step;
        g.have = true;
    } else {
        child(f);     // (the child's other work)
        f = g.after;  // its advance taken back: once per step
    }
    g.shown = g.after;
    if (frac < 1.0f && (g.before.w[0] != 0 || g.after.w[0] != 0)) {
        const float a = (1.0f - frac) * (float)g.before.w[2] + frac * (float)g.after.w[2];
        g.shown.w[2] = (uint32_t)(a + 0.5f);
    }
    f = g.shown;
}

}  // namespace interp::fade
