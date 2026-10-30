/* SPDX-License-Identifier: MPL-2.0
 * Pure input/gesture policy. Timings adapted from the Enhanced fork and the
 * enhanced-on-main port; no host internals or game assets are required. */
#ifndef ENHANCED_CONTROLS_CORE_H
#define ENHANCED_CONTROLS_CORE_H

enum { EC_JUMP, EC_SLASH, EC_SPIN, EC_DODGE, EC_NONE = -1 };
enum { EC_A = 0x8000, EC_B = 0x4000, EC_ZL = 0x80 };
typedef struct { float x, y, w, h; } EcRect; /* centre, +y up */
typedef struct {
    int macro, contact, tap_valid, tap_moved, previous_tap, pinching;
    double now, macro_start, down_time, tap_time, center_until;
    float down_x, down_y, tap_x, tap_y, dodge_x, dodge_y;
    float distance, pinch_start;
} EcState;
static float ec_clamp(float v, float lo, float hi) {
    if (!(v >= lo)) return lo;
    return v > hi ? hi : v;
}
static float ec_d2(float ax, float ay, float bx, float by) {
    float x = ax - bx, y = ay - by; return x*x + y*y;
}
static int ec_inside(EcRect r, float x, float y) {
    return x >= r.x-r.w/2 && x <= r.x+r.w/2 && y >= r.y-r.h/2 && y <= r.y+r.h/2;
}
static void ec_cancel(EcState* s) {
    s->macro = EC_NONE; s->contact = EC_NONE;
    s->tap_valid = s->tap_moved = s->previous_tap = s->pinching = 0;
    s->center_until = 0;
}
static void ec_init(EcState* s) {
    *s = (EcState){0}; s->distance = s->pinch_start = 1; ec_cancel(s);
}
static int ec_active(EcState* s) {
    if (s->macro != EC_NONE && s->now-s->macro_start >= (s->macro==EC_SPIN ? 1.15 : .26))
        s->macro = EC_NONE;
    return s->macro != EC_NONE;
}
static void ec_tick(EcState* s, double dt) {
    if (dt > 0 && dt <= .25) s->now += dt;
    ec_active(s);
}
static void ec_start(EcState* s, int which, float stick_x) {
    s->macro = which; s->macro_start = s->now;
    s->dodge_x = stick_x > .35f ? 1 : stick_x < -.35f ? -1 : 0;
    s->dodge_y = s->dodge_x == 0 ? -1 : 0; /* VPAD +y forward */
    s->previous_tap = 0;
}
static unsigned ec_buttons(EcState* s) {
    unsigned bits = s->now < s->center_until ? EC_ZL : 0;
    if (ec_active(s)) {
        double t = s->now-s->macro_start;
        if (s->macro == EC_SPIN) { if (t < 1.1) bits |= EC_B; }
        else {
            bits |= EC_ZL;
            if (t >= .06 && t < .20) bits |= s->macro == EC_SLASH ? EC_B : EC_A;
        }
    }
    return bits;
}
static void ec_down(EcState* s, int contact, float x, float y) {
    s->contact = contact; s->down_time = s->now;
    s->down_x = x; s->down_y = y;
    s->tap_valid = contact == 4; s->tap_moved = 0;
}
static void ec_move(EcState* s, float x, float y) {
    if (ec_d2(x,y,s->down_x,s->down_y) > 24*24) s->tap_moved = 1;
}
static void ec_up(EcState* s, float x, float y, int double_tap) {
    ec_move(s,x,y);
    if (double_tap && s->tap_valid && !s->tap_moved && !s->pinching && s->now-s->down_time <= .22) {
        if (s->previous_tap && s->now-s->tap_time <= .32 && ec_d2(x,y,s->tap_x,s->tap_y) <= 48*48) {
            s->center_until = s->now+.08; s->previous_tap = 0;
        } else { s->previous_tap = 1; s->tap_time = s->now; s->tap_x = x; s->tap_y = y; }
    } else s->previous_tap = 0;
    s->contact = EC_NONE; s->tap_valid = 0;
}
static void ec_pinch(EcState* s, float ratio) {
    if (!(ratio > .001f && ratio < 1000)) return;
    if (!s->pinching) s->pinch_start = s->distance;
    s->pinching = 1; s->tap_valid = s->previous_tap = 0;
    s->distance = ec_clamp(s->pinch_start/ratio, .5f, 2.f);
}
/* The native camera collision query is supplied by the guest adapter. */
typedef int (*EcBlocked)(void*, float);
static float ec_collision_limit(float desired, EcBlocked blocked, void* context) {
    if (desired <= 1 || !blocked(context,desired)) return desired;
    float lo = 1, hi = desired;
    for (int i=0; i<6; ++i) {
        float mid=(lo+hi)*.5f;
        if (blocked(context,mid)) hi=mid; else lo=mid;
    }
    return 1+(lo-1)*.9f;
}
#endif
