// SPDX-License-Identifier: MPL-2.0
// Distance and collision approach adapted from SSunnKing's Enhanced fork, MPL-2.0.
#include "camera_distance.h"
#include "enhanced_touch.h"
#include "play_context.h"
#include "guest_addr.h"
#include <algorithm>
#include <cmath>
#include <mutex>
namespace enhanced {
namespace {
std::atomic<float> distance{1};
std::mutex gesture_mutex;
float start = 1, spread = 0;
bool gesturing = false;
}
float camera_distance() { return distance.load(); }
void set_camera_distance(float v) { distance = std::isfinite(v) ? std::clamp(v, .5f, 2.f) : 1.f; }
bool distance_gesture(int phase, float pixels) {
    std::lock_guard<std::mutex> lock(gesture_mutex);
    if (!distance_pinch.get()) { gesturing = false; return false; }
    if (phase == 0) {
        gesturing = std::isfinite(pixels) && pixels > 1;
        start = camera_distance(); spread = pixels;
    } else if (gesturing && std::isfinite(pixels) && pixels > 1) {
        set_camera_distance(start * spread / pixels);
        if (phase == 2) gesturing = false;
    }
    return true;
}
std::array<float, 3> draw_camera_distance(Cpu* c, void (*draw)(Cpu*)) {
    const uint32_t cam = c->r[3];
    const float zoom = distance_pinch.get() ? camera_distance() : 1;
    if (zoom == 1) {
        std::array<float,3> eye;
        for (int i=0;i<3;i++) eye[i]=float(ldf32(cam+0xDC+4*i));
        draw(c); return eye;
    }
    struct RestoreEye {
        uint32_t cam; std::array<uint32_t, 3> words;
        ~RestoreEye() { for (int i=0;i<3;i++) st32(cam+0xDC+4*i, words[i]); }
    } restore{cam, {ld32(cam+0xDC), ld32(cam+0xE0), ld32(cam+0xE4)}};
    std::array<float,3> eye, center;
    float d2 = 0;
    for (int i=0;i<3;i++) {
        eye[i] = float(ldf32(cam+0xDC+4*i)); center[i] = float(ldf32(cam+0xE8+4*i));
        d2 += (eye[i]-center[i])*(eye[i]-center[i]);
    }
    static const bool wanted = [] { mods::play_context_want(true); return true; }();
    (void)wanted;
    const auto context = mods::play_context();
    if (zoom != 1 && std::isfinite(d2) && d2 >= 3600 && context.has(mods::playctx::kInPlay) && context.zoom_kind == 0) {
        float factor = zoom;
        if (zoom > 1) {
            static const uint32_t pts = mem::host_alloc(0x20, 0x10);
            struct RestoreCpu { Cpu* c; Cpu saved; ~RestoreCpu() { *c = saved; } } cpu{c, *c};
            for (int i=0;i<3;i++) stf32(pts+4*i, center[i]);
            auto blocked = [&](float f) {
                for (int i=0;i<3;i++) stf32(pts+0x10+4*i, center[i]+(eye[i]-center[i])*f);
                return guest_call(c, GC(0x024FCBE8), {cam+0x248, pts, pts+0x10, 0x7F}) != 0;
            };
            if (blocked(zoom)) {
                float lo=1, hi=zoom;
                for (int i=0;i<6;i++) { float mid=(lo+hi)*.5f; (blocked(mid)?hi:lo)=mid; }
                factor=1+(lo-1)*.9f;
            }
        }
        for (int i=0;i<3;i++) { eye[i]=center[i]+(eye[i]-center[i])*factor; stf32(cam+0xDC+4*i, eye[i]); }
    }
    draw(c);
    return eye;
}
}
