#include "gfx/display_modes.h"
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <limits>
void log_msg(const char *, ...) {}
namespace input {
void set_touch(bool, float, float) {}
} // namespace input
namespace aspect {
void set_window_aspect(float) {}
} // namespace aspect
static bool near(float a, float b) { return std::fabs(a - b) < 0.002f; }
int main() {
    using namespace gfx;
    const char *old[] = {"tl", "tr", "bl", "br"};
    const int migrated[] = {kPipTL, kPipTR, kPipBL, kPipBR};
    for (int i = 0; i < 4; ++i)
        assert(pip_load_anchor(nullptr, old[i]) == migrated[i]);
    assert(pip_load_anchor("cc", "br") == kPipCC);
    assert(pip_load_anchor("invalid", "br") == kPipBR);
    assert(pip_load_anchor(nullptr, nullptr) == kPipBR);
    assert(pip_clamp(NAN, 0.1f, 0.5f, 0.25f) == 0.25f);
    assert(pip_clamp(-1, 0.2f, 1, 1) == 0.2f);
    assert(pip_clamp(2, 0.2f, 1, 1) == 1);
    assert(pip_place({}, 1, 0.25f, 0, 0).anchor == kPipCustom);
    for (float aspect : {16.0f / 9, 4.0f / 3, 32.0f / 9}) {
        const Box tv{31, 47, 1440, 1440 / aspect};
        for (float size : {-10.0f, 0.1f, 0.25f, 0.5f, 10.0f, NAN})
            for (int a = 0; a <= kPipCustom; ++a) {
                auto b = pip_box(tv, 854.0f / 480, size, {a, 0.73f, 0.62f});
                assert(b.x >= tv.x && b.y >= tv.y);
                assert(b.x + b.w <= tv.x + tv.w + 0.002f && b.y + b.h <= tv.y + tv.h + 0.002f);
                if (a == kPipBR)
                    assert(b.y + b.h <= tv.y + tv.h * kPipRupeeBottom + 0.002f);
                if (a == kPipTL) assert(b.y >= tv.y + tv.h * kPipLeftHudBottom - 0.002f);
                if (a == kPipTR) assert(b.y >= tv.y + tv.h * kPipRightHudBottom - 0.002f);
                const float x = (b.x - tv.x + b.w / 2) / tv.w, y = (b.y - tv.y + b.h / 2) / tv.h;
                auto mapped = pip_place(tv, 854.0f / 480, size, x, y, false);
                auto back = pip_box(tv, 854.0f / 480, size, mapped);
                assert(near(b.x, back.x) && near(b.y, back.y));
                if (a < kPipCustom) {
                    auto snapped = pip_place(tv, 854.0f / 480, size, x + 0.001f, y + 0.001f);
                    auto q = pip_box(tv, 854.0f / 480, size, snapped);
                    assert(near(q.x, b.x) && near(q.y, b.y));
                }
            }
        for (float x : {-10.0f, 0.0f, 0.5f, 1.0f, 10.0f, NAN}) {
            auto b = pip_box(tv, 854.0f / 480, 0.5f, {kPipCustom, x, x});
            assert(b.x >= tv.x && b.y >= tv.y && b.x + b.w <= tv.x + tv.w + 0.002f &&
                   b.y + b.h <= tv.y + tv.h + 0.002f);
        }
        auto p = pip_place(tv, 854.0f / 480, 0.25f, 0.41f, 0.37f);
        assert(p.anchor == kPipCustom);
        g_pip_anchor = kPipBR;
        auto l = layout(1440, 1440 / aspect, 1440, 1440 / aspect, 854, 480, true);
        assert(l.pip_on && l.pip.y + l.pip.h <= l.tv.y + l.tv.h * kPipRupeeBottom + 0.002f);
    }
    g_mode = kDrcOff;
    g_shown = false;
    g_pip_preview = true;
    auto p = display_plan(true, 1280, 720, true, 854, 480, 1920, 1080, 1);
    assert(p.pip_on && !p.drc_only);
    g_pip_preview = false;
    assert(!display_plan(true, 1280, 720, true, 854, 480, 1920, 1080, 2).pip_on);
}
