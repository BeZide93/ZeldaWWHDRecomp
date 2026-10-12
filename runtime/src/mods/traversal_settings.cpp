// Traversal mods: the live switches and factors (see traversal.cpp for the hooks). No game code,
// so the mod manager tests link this file as it is.
//
// Start-up / test switches (all off by default, also switchable from the Gameplay menu):
//   WWHD_MOD_LADDER_SPEED=1    faster ladders   (WWHD_MOD_LADDER_FACTOR=1.5)
//   WWHD_MOD_SIDLE_SPEED=1     faster sidling   (WWHD_MOD_SIDLE_FACTOR=1.5)
#include "mods.h"
#include "traversal.h"

#include <atomic>
#include <cstdlib>

namespace mods {
namespace {
bool env_on(const char* n) {
    const char* e = getenv(n);
    return e && atoi(e) != 0;
}
float env_factor(const char* n) {
    const char* e = getenv(n);
    return traversal::clamp_factor(e ? (float)atof(e) : traversal::kDefaultFactor);
}
std::atomic<bool> g_ladder{env_on("WWHD_MOD_LADDER_SPEED")};
std::atomic<float> g_ladder_factor{env_factor("WWHD_MOD_LADDER_FACTOR")};
std::atomic<bool> g_sidle{env_on("WWHD_MOD_SIDLE_SPEED")};
std::atomic<float> g_sidle_factor{env_factor("WWHD_MOD_SIDLE_FACTOR")};
constexpr auto kRelaxed = std::memory_order_relaxed;
}  // namespace

bool ladder_speed() { return g_ladder.load(kRelaxed); }
void set_ladder_speed(bool on) { g_ladder = on; }
float ladder_speed_factor() { return g_ladder_factor.load(kRelaxed); }
void set_ladder_speed_factor(float f) { g_ladder_factor = traversal::clamp_factor(f); }
bool sidle_speed() { return g_sidle.load(kRelaxed); }
void set_sidle_speed(bool on) { g_sidle = on; }
float sidle_speed_factor() { return g_sidle_factor.load(kRelaxed); }
void set_sidle_speed_factor(float f) { g_sidle_factor = traversal::clamp_factor(f); }

bool traversal_anim() { return ladder_speed() || sidle_speed(); }
float traversal_anim_factor(uint32_t proc) {
    return traversal::anim_rate_factor(proc, traversal::active_factor(ladder_speed(), ladder_speed_factor()),
                                       traversal::active_factor(sidle_speed(), sidle_speed_factor()));
}

}  // namespace mods
