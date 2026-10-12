// Exercises the traversal mods' hooks (runtime/src/mods/traversal.cpp) against a modelled guest:
// with each mod off a hook must leave guest memory and the registers exactly as the original code
// alone does (no extra writes, no guest calls); with it on, only the intended values change.
#include "mods/mods.h"
#include "mods/traversal.h"
#include "runtime.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace {
constexpr uint32_t kLink = 0x11000000;
constexpr uint32_t kProc = 0x65F0, kRate = 0x5898, kMax = 0x3C4, kTimer = 0x6916, kSide = 0x69C0;
constexpr uint32_t kChangeLadderFn = 0x0242757C;
void map(uint32_t address) {
#ifdef _WIN32
    auto p = VirtualAlloc(mem::ptr(address), 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    auto p = mmap(mem::ptr(address), 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(p == mem::ptr(address));
}
float f32(uint32_t a) { return u32_as_f32(ld32(a)); }
void setf(uint32_t a, float v) { st32(a, f32_as_u32(v)); }

// guest calls the hooks make
unsigned ladder_calls = 0;

// ---- modelled original code ----
// procLadderMove: rate stopped -> start the next rung (procLadderMove_init: setFrameCtrl with the
// stock rate times whatever setFrameCtrl's hook applies, modelled here as `init_rate`); else the stick
// rate is written, keeping the sign. `let_go` models doTrigger (procFall_init).
float stick_rate = 1.5f, init_rate = 1.5f;
bool let_go = false;
float max_seen = 0;  // mMaxNormalSpeed as setBlendWHideMoveAnime saw it
// the guest storage the original code itself touches, compared byte for byte
std::vector<uint8_t> snapshot(uint32_t a, size_t n) { return {mem::ptr(a), mem::ptr(a) + n}; }
}  // namespace

unsigned rungs = 0;
void next_rung(uint32_t link) {  // changeLadderMoveProc -> procLadderMove_init
    setf(link + kRate, init_rate);
    st16(link + kTimer, 1);
    ++rungs;
}
void log_msg(const char*, ...) {}
uint32_t guest_call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args) {
    const std::vector<uint32_t> v(args);
    assert(fn == kChangeLadderFn && v.size() == 2 && v[0] == kLink && v[1] == ld32(kLink + kSide));
    ++ladder_calls;
    next_rung(kLink);
    c->r[31] = 0xDEADBEEF;
    return 0;
}
extern "C" void f_02427CA8_orig(Cpu* c) {
    const uint32_t link = c->r[3];
    if (let_go) { st32(link + kProc, 0x27); c->r[3] = 1; return; }
    const float rate = f32(link + kRate);
    if (std::fabs(rate) < 0.01f) {
        const int16_t t = (int16_t)ld16(link + kTimer);
        if (t > 0) st16(link + kTimer, (uint16_t)(t - 1));  // the footstep
        else next_rung(link);
    } else {
        setf(link + kRate, rate < 0 ? -stick_rate : stick_rate);
    }
    c->r[3] = 1;
    c->r[4] = 0x77;  // volatile registers need not keep their values
}
extern "C" void f_0242A218_orig(Cpu* c) {
    max_seen = f32(c->r[3] + kMax);
    setf(c->r[3] + 0x6A14, 3.f);  // the blend writes some of Link's fields
    c->f[1].ps0 = 0;
}
extern "C" void hook_02427CA8(Cpu*);
extern "C" void hook_0242A218(Cpu*);

static void all_off() {
    mods::set_ladder_speed(false);
    mods::set_sidle_speed(false);
}

// with the mod off, hook == original alone (memory and registers), and no guest calls
static void test_off_identical() {
    all_off();
    const unsigned ladder0 = ladder_calls;
    for (int timer : {0, 1})
    for (float rate : {0.f, 1.5f, -1.5f}) {
        st32(kLink + kProc, 0x3C);
        st16(kLink + kTimer, (uint16_t)timer);
        setf(kLink + kRate, rate);
        Cpu a{}; a.r[3] = kLink;
        const auto before = snapshot(kLink, 0x8000);
        f_02427CA8_orig(&a);
        const auto stock = snapshot(kLink, 0x8000);
        memcpy(mem::ptr(kLink), before.data(), before.size());
        Cpu b{}; b.r[3] = kLink;
        hook_02427CA8(&b);
        assert(snapshot(kLink, 0x8000) == stock && memcmp(&a, &b, sizeof a) == 0);
    }
    st32(kLink + kProc, 0x15);
    setf(kLink + kMax, 17.f);
    {
        Cpu a{}; a.r[3] = kLink; a.f[1].ps0 = -1.0;
        const auto before = snapshot(kLink, 0x8000);
        f_0242A218_orig(&a);
        const auto stock = snapshot(kLink, 0x8000);
        memcpy(mem::ptr(kLink), before.data(), before.size());
        Cpu b{}; b.r[3] = kLink; b.f[1].ps0 = -1.0;
        hook_0242A218(&b);
        assert(snapshot(kLink, 0x8000) == stock && memcmp(&a, &b, sizeof a) == 0 && max_seen == 17.f);
        Cpu c{}; c.r[3] = kLink; c.f[1].ps0 = 2.0; c.f[3].ps0 = 2.0; c.f[4].ps0 = .25;
        const Cpu c0 = c;
        const auto scope = mods::sidle_scope_begin(&c, true);
        assert(!scope.link && memcmp(&c, &c0, sizeof c) == 0 && f32(kLink + kMax) == 17.f);
        mods::sidle_scope_end(scope);
        assert(f32(kLink + kMax) == 17.f);
    }
    assert(ladder_calls == ladder0);
    // the setFrameCtrl hook (mods.cpp) asks traversal_anim(): off for every procedure
    assert(!mods::traversal_anim());
    for (uint32_t proc = 0; proc < 256; ++proc) assert(mods::traversal_anim_factor(proc) == 1.f);
}

static void test_ladder() {
    all_off();
    mods::set_ladder_speed(true);
    mods::set_ladder_speed_factor(2.f);
    assert(mods::traversal_anim() && mods::traversal_anim_factor(0x3C) == 2.f && mods::traversal_anim_factor(0x38) == 2.f);
    assert(mods::traversal_anim_factor(0x15) == 1.f && mods::traversal_anim_factor(0x06) == 1.f);
    st32(kLink + kProc, 0x3C);
    // a running rung clip: the stick rate the procedure wrote is doubled, climbing up and down
    for (float sign : {1.f, -1.f}) {
        setf(kLink + kRate, sign * 3.f);  // last step's (scaled) rate
        Cpu c{}; c.r[3] = kLink;
        hook_02427CA8(&c);
        assert(f32(kLink + kRate) == sign * stick_rate * 2.f);
    }
    // the clip ended: the footstep step starts the next rung at once (the game's own call), its init
    // rate came through setFrameCtrl and is kept as is
    setf(kLink + kRate, 0.f);
    st16(kLink + kTimer, 1);
    st32(kLink + kSide, 1);
    unsigned before = ladder_calls;
    { Cpu c{}; c.r[3] = kLink; c.r[31] = 0x1234; hook_02427CA8(&c); assert(c.r[31] == 0x1234 && c.r[3] == 1); }
    assert(ladder_calls == before + 1 && f32(kLink + kRate) == init_rate && ld16(kLink + kTimer) == 1);
    // with the stick released the game itself keeps calling changeLadderMoveProc (timer 0): no extra call
    setf(kLink + kRate, 0.f);
    st16(kLink + kTimer, 0);
    before = ladder_calls;
    { Cpu c{}; c.r[3] = kLink; hook_02427CA8(&c); }
    assert(ladder_calls == before && rungs > 0);
    // a whole climb: steps per rung with the 2x clip (the stub's clip: 10 frames)
    st32(kLink + kProc, 0x3C);
    setf(kLink + kRate, 0.f);
    st16(kLink + kTimer, 1);
    init_rate = 3.f;
    float frame = 9.999f;
    const unsigned r0 = rungs;
    int steps = 0;
    for (; steps < 400 && rungs - r0 < 50; ++steps) {
        if (f32(kLink + kRate) != 0.f) {  // the clip advances (J3DFrameCtrl::update), then the procedure runs
            frame = std::min(frame + f32(kLink + kRate), 9.999f);
            if (frame >= 9.999f) setf(kLink + kRate, 0.f);
        }
        Cpu c{}; c.r[3] = kLink;
        const unsigned rb = rungs;
        hook_02427CA8(&c);
        if (rungs != rb) frame = 0;
    }
    assert(steps == 1 + 49 * 4);  // the first step starts rung 1; then 4 steps per rung (8 at 1x)
    init_rate = 1.5f;
    // letting go: the procedure changed, nothing is scaled
    let_go = true;
    setf(kLink + kRate, 1.5f);
    { Cpu c{}; c.r[3] = kLink; hook_02427CA8(&c); }
    assert(ld32(kLink + kProc) == 0x27 && f32(kLink + kRate) == 1.5f);
    let_go = false;
    // the rate written does not compound over steps
    st32(kLink + kProc, 0x3C);
    for (int i = 0; i < 10; ++i) { Cpu c{}; c.r[3] = kLink; hook_02427CA8(&c); }
    assert(f32(kLink + kRate) == stick_rate * 2.f);
}

static void test_sidle() {
    all_off();
    mods::set_sidle_speed(true);
    mods::set_sidle_speed_factor(1.5f);
    assert(mods::traversal_anim_factor(0x15) == 1.5f && mods::traversal_anim_factor(0x3C) == 1.f);
    st32(kLink + kProc, 0x15);
    setf(kLink + kMax, 18.f);
    // setBlendWHideMoveAnime sees the scaled maximum, and it goes back afterwards
    { Cpu c{}; c.r[3] = kLink; hook_0242A218(&c); }
    assert(max_seen == 27.f && f32(kLink + kMax) == 18.f);
    // setNormalSpeedF: maximum and the accel/step arguments scaled, the ratio argument kept
    Cpu c{}; c.r[3] = kLink; c.f[1].ps0 = 2.0; c.f[2].ps0 = .5; c.f[3].ps0 = 2.0; c.f[4].ps0 = .25;
    auto scope = mods::sidle_scope_begin(&c, true);
    assert(f32(kLink + kMax) == 27.f && c.f[1].ps0 == 3.0 && c.f[2].ps0 == .5 && c.f[3].ps0 == 3.0 && c.f[4].ps0 == .375);
    mods::sidle_scope_end(scope);
    assert(f32(kLink + kMax) == 18.f);
    // the game set its own maximum meanwhile (checkNextMode when he leaves the wall): it stays
    scope = mods::sidle_scope_begin(&c, true);
    setf(kLink + kMax, 21.f);
    mods::sidle_scope_end(scope);
    assert(f32(kLink + kMax) == 21.f);
    // other procedures: untouched
    st32(kLink + kProc, 0x14);
    setf(kLink + kMax, 18.f);
    { Cpu d{}; d.r[3] = kLink; hook_0242A218(&d); }
    assert(max_seen == 18.f && f32(kLink + kMax) == 18.f);
}

int main() {
    map(kLink);
    test_off_identical();
    test_ladder();
    test_sidle();
    test_off_identical();  // switching off again goes back to the stock path
    return 0;
}
