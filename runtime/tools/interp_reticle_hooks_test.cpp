// Exercise the actual hooks with guest-memory fixtures and a projection spy.
// The spy supplies independently chosen logic/hold screen coordinates; the
// test checks hook scope, original call/CPU semantics, isolation and bypasses.
#include "runtime.h"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <sys/mman.h>
#include <vector>

extern "C" void hook_023D813C(Cpu*);
extern "C" void hook_020CBB70(Cpu*);
extern "C" void hook_025F1018(Cpu*);
namespace interp { void reticle_reset(); }
namespace {
constexpr uint32_t base = 0x10000000, bytes = 0x20000;
constexpr uint32_t sight = base + 0x6000, boom = base + 0x8000;
constexpr uint32_t target = base + 0x10000, output = base + 0x18000;
bool active = true, at60 = false;
uint64_t step = 1;
float fraction = 1;
std::array<float, 3> raw{};
int calls = 0;
}
namespace interp {
bool enabled() { return active; }
uint64_t logic_steps() { return step; }
uint64_t pass_count() { return step * 8; }
float pass_t() { return fraction; }
float reticle_t() { return fraction; }
}
namespace true60 { bool runs_60(uint32_t) { return at60; } }
void log_msg(const char*, ...) {}
extern "C" void f_025F1018_orig(Cpu* c) {
    ++calls;
    for (int i = 0; i < 3; ++i) stf32(c->r[4] + 4 * i, raw[i]);
    // The hook must leave original callee effects in place, rather than
    // restoring the entry CPU to recover argument registers.
    c->r[3] = 0x11223344;
    c->f[1].ps0 = 19.25;
    c->cr[7] = 1;
}
extern "C" void f_023D813C_orig(Cpu* c) {
    c->r[3] += 8;
    c->r[4] = output;
    hook_025F1018(c);
}
extern "C" void f_020CBB70_orig(Cpu* c) {
    c->r[3] = c->r[4];
    c->r[4] = output;
    hook_025F1018(c);
}

static void draw(bool boomerang, int slot, uint32_t source,
                 std::array<float, 3> computed, std::array<float, 3> expected,
                 bool scoped = true) {
    raw = computed;
    auto* memory = static_cast<unsigned char*>(ppc_ptr(base));
    std::vector<unsigned char> before(memory, memory + bytes);
    Cpu c{};
    c.r[3] = scoped ? (boomerang ? boom : sight) : source;
    c.r[4] = scoped ? source : output;
    c.r[5] = slot;
    int count = calls;
    if (!scoped) hook_025F1018(&c);
    else if (boomerang) hook_020CBB70(&c);
    else hook_023D813C(&c);
    assert(calls == count + 1);
    assert(c.r[3] == 0x11223344 && c.f[1].ps0 == 19.25 && c.cr[7] == 1);
    for (int i = 0; i < 3; ++i) {
        assert(ldf32(output + 4 * i) == expected[i]);
        // Exclude only the projection result; source positions and all other
        // guest bytes (standing in for the remaining HUD) must be unchanged.
        std::memcpy(before.data() + output - base + 4 * i,
                    ppc_ptr(output + 4 * i), 4);
    }
    assert(std::memcmp(before.data(), memory, bytes) == 0);
}

int main() {
    setenv("WWHD_RETICLE_FIX", "1", 1);
    unsetenv("WWHD_RETICLE_TRACE");
    void* mapped = mmap(ppc_ptr(base), bytes, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    assert(mapped == ppc_ptr(base));
    std::memset(mapped, 0xA5, bytes);
    for (uint32_t p : {sight + 8, target + 0x37C, target + 0x13FC})
        for (int i = 0; i < 3; ++i) stf32(p + 4 * i, 100.f + i);

    for (int frames : {2, 4, 8}) {
        interp::reticle_reset();
        step = 1; fraction = 1;
        draw(false, 0, sight + 8, {640, 360, 0}, {640, 360, 0});
        ++step;
        for (int pass = 1; pass <= frames; ++pass) {
            fraction = float(pass) / frames;
            draw(false, 0, sight + 8, {pass == 1 ? 624.f : 656.f, 360, 0},
                 {640 - fraction * 16, 360, 0});
            // The scoped marker must not affect a later HUD projection.
            draw(false, 0, target, {100, 200, 0}, {100, 200, 0}, false);
        }
    }
    interp::reticle_reset();
    step = 10; fraction = 1;
    for (int slot = 0; slot < 5; ++slot)
        draw(true, slot, target + 0x37C, {float(slot * 100), 300, 0}, {float(slot * 100), 300, 0});
    ++step; fraction = .5f;
    for (int slot = 0; slot < 5; ++slot)
        draw(true, slot, target + 0x37C, {float(slot * 100 + 20), 300, 0}, {float(slot * 100 + 10), 300, 0});
    // A different actor in a reused lock slot must not blend from the old target.
    draw(true, 0, target + 0x13FC, {900, 100, 0}, {900, 100, 0});
    at60 = true;
    draw(true, 1, target + 0x37C, {700, 100, 0}, {700, 100, 0});
    at60 = false; ++step;
    draw(true, 1, target + 0x37C, {800, 100, 0}, {800, 100, 0});
    // No interpolation: verify exact original floats, including signed zero.
    active = false;
    draw(false, 0, sight + 8, {-0.f, 311.375f, -0.125f}, {-0.f, 311.375f, -0.125f});
    assert(ld32(output) == 0x80000000u);
    active = true; interp::reticle_reset(); ++step;
    draw(false, 0, sight + 8, {500, 300, 0}, {500, 300, 0});
    munmap(mapped, bytes);
    std::puts("interp_reticle_hooks: PASS");
}
