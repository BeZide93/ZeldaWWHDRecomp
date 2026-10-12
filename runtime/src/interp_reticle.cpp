#include "runtime.h"
#include "interp_reticle.h"
#include "true60.h"
#include <cstdlib>
#include <unordered_map>

// Issue #163 investigation. The screen-position candidate is opt-in until
// headless before/after captures establish the cause and validate this path.
// WWHD_RETICLE_TRACE=1 logs scoped projections; WWHD_RETICLE_FIX=1 tests the candidate.

namespace interp {
float pass_t();
uint64_t logic_steps();
uint64_t pass_count();
bool enabled();
float reticle_t();
}
extern "C" {
void f_023D813C_orig(Cpu*);
void f_020CBB70_orig(Cpu*);
void f_025F1018_orig(Cpu*);
}
namespace {
thread_local uint32_t packet;
thread_local int marker;
std::unordered_map<uint64_t, interp::reticle::History> histories;
void scoped(Cpu* c, int slot, void (*original)(Cpu*)) {
    const auto old_packet = packet;
    const auto old_marker = marker;
    packet = c->r[3];
    marker = slot;
    original(c);
    packet = old_packet;
    marker = old_marker;
}
}
namespace interp {
void reticle_reset() { histories.clear(); }
}
extern "C" void hook_023D813C(Cpu* c) { scoped(c, -1, f_023D813C_orig); }
extern "C" void hook_020CBB70(Cpu* c) { scoped(c, int(c->r[5]), f_020CBB70_orig); }
extern "C" void hook_025F1018(Cpu* c) {
    if (!packet) {
        f_025F1018_orig(c);
        return;
    }
    const uint32_t src = c->r[3], dst = c->r[4];
    const float x = ldf32(src), y = ldf32(src + 4), z = ldf32(src + 8);
    static const bool fix = getenv("WWHD_RETICLE_FIX") && atoi(getenv("WWHD_RETICLE_FIX"));
    f_025F1018_orig(c);
    if (fix && packet && src != dst && interp::enabled()) {
        const uint64_t key = uint64_t(packet) * 8 + marker + 1;
        const uint32_t actor = marker < 0 ? packet - 0x58E8 : src - 0x37C;
        const float t = true60::runs_60(actor) ? 1.0f : interp::reticle_t();
        if (!true60::runs_60(actor)) {
            auto p = histories[key].sample({float(ldf32(dst)), float(ldf32(dst + 4)), float(ldf32(dst + 8))},
                                          interp::logic_steps(), src, t);
            for (int i = 0; i < 3; ++i) stf32(dst + 4 * i, p[i]);
        } else {
            histories.erase(key);
        }
        if (histories.size() > 128) {
            for (auto it = histories.begin(); it != histories.end();)
                if (it->second.step + 2 < interp::logic_steps()) it = histories.erase(it);
                else ++it;
        }
    }
    static const bool trace = getenv("WWHD_RETICLE_TRACE") != nullptr;
    if (packet && trace)
        LOG("[reticle] step %llu pass %llu t %.3f packet %08X slot %d world %.6f %.6f %.6f screen %.6f %.6f %.6f",
            (unsigned long long)interp::logic_steps(), (unsigned long long)interp::pass_count(),
            interp::pass_t(), packet, marker, x, y, z, ldf32(dst), ldf32(dst + 4), ldf32(dst + 8));
}
