// Unit tests for the traversal mods' maths (runtime/src/mods/traversal.h): faster ladders and
// sidling (#95). Pure functions only; the hooks are checked
// against a modelled guest in traversal_runtime_test.
//   make -C build/cmake traversal_test && ./build/cmake/traversal_test
#include "mods/traversal.h"
#include <cassert>
#include <cmath>
#include <limits>

using namespace mods::traversal;

static void test_clamp() {
    assert(clamp_factor(std::numeric_limits<float>::quiet_NaN()) == kDefaultFactor);
    assert(clamp_factor(std::numeric_limits<float>::infinity()) == kDefaultFactor);
    assert(clamp_factor(0.f) == 1.f && clamp_factor(.5f) == 1.f);
    assert(clamp_factor(10.f) == 3.f);
    assert(clamp_factor(2.25f) == 2.25f);
    assert(kDefaultFactor == 1.5f && kMinFactor == 1.f && kMaxFactor == 3.f);
}

static void test_off_is_stock() {
    // a mod that is off applies exactly 1, whatever its dial says
    for (float f : {1.f, 1.5f, 3.f, 100.f, std::numeric_limits<float>::quiet_NaN()}) assert(active_factor(false, f) == 1.f);
    assert(active_factor(true, 2.f) == 2.f);
    assert(active_factor(true, 9.f) == 3.f);
    // with both dials at 1 (mods off) no procedure's clip changes
    for (uint32_t proc = 0; proc < 256; ++proc) assert(anim_rate_factor(proc, 1.f, 1.f) == 1.f);
}

static void test_procedures() {
    // ladders: the four mount/dismount procedures and the rung; nothing else
    for (uint32_t proc = 0; proc < 256; ++proc) {
        const float f = anim_rate_factor(proc, 2.f, 3.f);
        if (proc >= 0x38 && proc <= 0x3C) assert(f == 2.f);
        else if (proc == 0x15) assert(f == 3.f);  // PROC_WHIDE_MOVE only: ready, wait, peep stay stock
        else assert(f == 1.f);
    }
    assert(is_ladder_proc(kProcLadderMove) && !is_ladder_proc(0x3D));  // CLIMB_UP_START is ivy, not a ladder
}

static void test_ladder_rewrite() {
    // the rung clip runs and Link stays on the ladder: the procedure stored the stock rate (scale it)
    assert(ladder_rate_rewritten(1.5f, kProcLadderMove, kProcLadderMove));
    assert(ladder_rate_rewritten(-1.5f, kProcLadderMove, kProcLadderMove));  // climbing down
    // the clip had ended: the procedure kept it or started the next rung (already scaled at init)
    assert(!ladder_rate_rewritten(0.f, kProcLadderMove, kProcLadderMove));
    assert(!ladder_rate_rewritten(.005f, kProcLadderMove, kProcLadderMove));
    // let go (procFall) or the ladder ended
    assert(!ladder_rate_rewritten(1.5f, kProcLadderMove, 0x27));
    assert(!ladder_rate_rewritten(1.5f, 0x39, 0x39));
}

static void test_ladder_rest() {
    // the footstep step: clip stopped, timer 1 -> 0, still on the ladder
    assert(ladder_rest_step(0.f, 1, 0, kProcLadderMove, kProcLadderMove));
    assert(!ladder_rest_step(0.f, 0, 0, kProcLadderMove, kProcLadderMove));     // the step after (the game moves on)
    assert(!ladder_rest_step(1.5f, 1, 1, kProcLadderMove, kProcLadderMove));    // clip running
    assert(!ladder_rest_step(0.f, 1, 0, kProcLadderMove, 0x27));                // let go
    assert(!ladder_rest_step(0.f, 2, 1, kProcLadderMove, kProcLadderMove));     // (not the last count)
    // steps per rung of the 10-frame clip, measured in game (Outset lookout ladder, stick held):
    // 8 at the stock rate 1.5, 5 with a 2x clip; merging the rest step makes it 4 = the factor
    assert(ladder_steps_per_rung(1.5f, false) == 8);
    assert(ladder_steps_per_rung(3.f, false) == 5);
    assert(ladder_steps_per_rung(3.f, true) == 4);
    assert(ladder_steps_per_rung(2.25f, true) == 5);  // 1.5x: 8/5 = 1.6
    assert(ladder_steps_per_rung(4.5f, true) == 3);   // 3x: 8/3 = 2.67
    for (float k : {1.25f, 1.5f, 2.f, 2.5f, 3.f}) {
        const float speedup = 8.f / ladder_steps_per_rung(1.5f * k, true);
        assert(speedup >= k * .85f && speedup <= k * 1.1f);
    }
}

static void test_ladder_timing() {
    // A rung clip of N frames played at rate r ends after ceil(N / r) logic steps; with the factor it
    // ends k times sooner, at 30 Hz and at true 60 (two half steps of dt = 0.5 per original step,
    // J3DFrameCtrl::update advancing rate * dt). The stored rate is the same on both passes.
    const float frames = 24.f;
    for (float k : {1.f, 1.5f, 2.f, 3.f})
        for (float stick_rate : {1.f, 1.5f, 2.f})
            for (bool true60 : {false, true}) {
                const float dt = true60 ? .5f : 1.f;
                const float rate = stick_rate * k;
                float frame = 0, steps = 0;
                while (frame < frames) frame += rate * dt, steps += dt;
                const float expected = std::ceil(frames / rate / dt) * dt;
                assert(std::fabs(steps - expected) < 1e-4f);
                // one second of climbing covers k times the stock rungs (to the step)
                if (!true60) assert(std::fabs(frames / rate - frames / stick_rate / k) < 1e-4f);
            }
}

int main() {
    test_clamp();
    test_off_is_stock();
    test_procedures();
    test_ladder_rewrite();
    test_ladder_rest();
    test_ladder_timing();
    return 0;
}
