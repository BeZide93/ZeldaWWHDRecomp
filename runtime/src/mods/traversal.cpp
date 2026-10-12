// Traversal mods (Gameplay menu, all off by default): faster ladders and sidling (#95). Settings:
// traversal_settings.cpp; the maths: traversal.h (traversal_test). GameCube source:
// tww/src/d/actor/d_a_player_ladder.inc, d_a_player_whide.inc.
//
// Ladders: the rung, mount and dismount clips move Link through their root motion (posMove with
//   m34C2 = 5), so playing them faster climbs faster with hands and feet in step. setFrameCtrl
//   (mods.cpp's hook) scales the rate of every clip started in a ladder procedure (0x38..0x3C);
//   procLadderMove (02427CA8) rewrites the rung clip's rate from the stick every step, so that
//   write is scaled here. Sounds come from the clips' frames (setLadderFootSe when a rung clip ends,
//   checkPass on the mount clips), so they keep their place in the motion. The procedures' own
//   end checks (|rate| < 0.01, LineCross for the ladder's end) are untouched. The stock procedure
//   rests one step between rungs (the footstep, then the next rung on the next step); with the mod
//   on the next rung starts in the footstep's step (the game's own changeLadderMoveProc, called
//   right after the procedure), so the climb speeds up by about the factor (traversal.h).
// Sidling: speed = mNormalSpeed * (1 - m3598) + foot motion * m3598 (posMoveFromFootPos). Both
//   parts grow by the factor: the walk clip plays faster (setFrameCtrl in PROC_WHIDE_MOVE) and the
//   speed target is scaled by raising mMaxNormalSpeed for the length of setNormalSpeedF and
//   setBlendWHideMoveAnime (whose blend reads mNormalSpeed / mMaxNormalSpeed, so it keeps the stock
//   blend). The field goes back right after each call, so leaving the wall (0.7 * mMaxNormalSpeed in
//   changeWHideEndProc) and every other procedure see the stock value.
// Off means identical: every hook checks its switch first and then runs only the original code.
// 60 fps: all of this runs inside the game's own procedures, once per execute of Link,
//   and writes only rates and targets (never per-step amounts), so interpolation (60/120/240) and
//   true 60 (J3DFrameCtrl::update and cLib_addCalc scale the advance by dt) compose with it. In true
//   60 the ladder and sidle procedures run at 30 Hz (not in true60.cpp's kLinkGroups). The test
//   setting WWHD_TRUE60_LINK=audited runs the ladder procedures on every pass; that set is not
//   checked with these mods.
#include "mods/mods.h"
#include "mods/traversal.h"

#include "guest_addr.h"
#include "runtime.h"

extern "C" {
void f_02427CA8_orig(Cpu* c);  // daPy_lk_c::procLadderMove
void f_0242A218_orig(Cpu* c);  // daPy_lk_c::setBlendWHideMoveAnime(f32)
}

namespace {
using namespace mods::traversal;
// daPy_lk_c (WWHD offsets; docs/decomp-notes.md)
constexpr uint32_t kCurProc = 0x65F0;          // mCurProc
constexpr uint32_t kFrameCtrlUnder0 = 0x5898;  // mFrameCtrlUnder[0]: rate at +0
constexpr uint32_t kMaxNormalSpeed = 0x3C4;    // daPy_py_c::mMaxNormalSpeed (GameCube 0x2A8)
constexpr uint32_t kProcTimer = 0x6916;        // mProcVar0.m34D0 (s16; GameCube 0x34D0)
constexpr uint32_t kLadderSide = 0x69C0;       // mProcVar6.m3570 (GameCube 0x3570): which hand is up
const uint32_t kChangeLadderMoveProc = GC(0x0242757C);  // daPy_lk_c::changeLadderMoveProc(int)

float ladder_factor() { return active_factor(mods::ladder_speed(), mods::ladder_speed_factor()); }
float sidle_factor() { return active_factor(mods::sidle_speed(), mods::sidle_speed_factor()); }

}  // namespace

namespace mods {
SidleScope sidle_scope_begin(Cpu* c, bool speed_steps) {
    if (!sidle_speed()) return {};
    const uint32_t link = c->r[3];
    if (ld32(link + kCurProc) != kProcWHideMove) return {};
    const float k = sidle_factor();
    if (k == 1.f) return {};
    const uint32_t stock = ld32(link + kMaxNormalSpeed);
    const uint32_t scaled = f32_as_u32(u32_as_f32(stock) * k);
    st32(link + kMaxNormalSpeed, scaled);
    // setNormalSpeedF(accel f1, ratio f2, max step f3, min step f4): the steps grow with the target,
    // so reaching and losing the faster speed takes as long as the stock one
    if (speed_steps)
        for (int r : {1, 3, 4}) c->f[r].ps0 = (float)c->f[r].ps0 * k;
    return {link, stock, scaled};
}
void sidle_scope_end(const SidleScope& s) {
    // put the stock value back unless the game set its own meanwhile (checkNextMode does)
    if (s.link && ld32(s.link + kMaxNormalSpeed) == s.scaled) st32(s.link + kMaxNormalSpeed, s.stock);
}
}  // namespace mods

// ---- daPy_lk_c::procLadderMove (02427CA8): the rung clip's rate, rewritten from the stick ----
extern "C" void hook_02427CA8(Cpu* c) {
    if (!mods::ladder_speed()) {
        f_02427CA8_orig(c);
        return;
    }
    const uint32_t link = c->r[3];
    const uint32_t before = ld32(link + kCurProc);
    const float entry = u32_as_f32(ld32(link + kFrameCtrlUnder0));
    const int16_t timer = (int16_t)ld16(link + kProcTimer);
    f_02427CA8_orig(c);
    const float k = ladder_factor();
    if (k == 1.f) return;
    const uint32_t after = ld32(link + kCurProc);
    if (ladder_rate_rewritten(entry, before, after)) {
        st32(link + kFrameCtrlUnder0, f32_as_u32(u32_as_f32(ld32(link + kFrameCtrlUnder0)) * k));
    } else if (ladder_rest_step(entry, timer, (int16_t)ld16(link + kProcTimer), before, after)) {
        // the footstep was just played: start the next rung now (or the dismount at the ladder's end)
        const Cpu saved = *c;
        guest_call(c, kChangeLadderMoveProc, {link, ld32(link + kLadderSide)});
        *c = saved;
    }
}

// ---- daPy_lk_c::setBlendWHideMoveAnime(f32) (0242A218): the sidle blend reads
// mNormalSpeed / mMaxNormalSpeed; with the scaled maximum it keeps the stock blend ----
extern "C" void hook_0242A218(Cpu* c) {
    const mods::SidleScope scope = mods::sidle_scope_begin(c, false);
    f_0242A218_orig(c);
    mods::sidle_scope_end(scope);
}
