/* Original Dark Link gameplay state; independent of guest memory for host tests. */
#pragma once

typedef struct {
    int capacity, gain, duration, damage_percent, free_transform;
} DLConfig;
typedef struct {
    float charge, remaining, total;
    int active;
} DLState;

static void dl_reset(DLState* s) { *s = (DLState){0}; }
static void dl_hit(DLState* s, const DLConfig* c, int lost_health) {
    if (!lost_health || s->active || c->free_transform) return;
    s->charge += (float)c->gain;
    if (s->charge > c->capacity) s->charge = (float)c->capacity;
}
static int dl_ready(const DLState* s, const DLConfig* c) {
    return c->free_transform || s->charge >= c->capacity;
}
static void dl_tick(DLState* s, const DLConfig* c, float dt, int request, int allowed) {
    if (s->charge > c->capacity) s->charge = (float)c->capacity;
    if (!allowed) return;
    if (request) {
        if (s->active) { dl_reset(s); return; }
        if (dl_ready(s, c)) {
            s->active = 1;
            s->remaining = (float)c->duration;
            s->total = s->remaining;
            s->charge = 0;
            return;
        }
    }
    if (s->active && !c->free_transform && dt > 0.0f && dt <= 0.25f) {
        s->remaining -= dt;
        if (s->remaining <= 0.0001f) dl_reset(s);
    }
}
/* Native cc_at_check tests damage as signed 8-bit; values >=128 heal/wrap. */
static unsigned dl_damage(unsigned original, unsigned percent) {
    if (!original || original >= 128) return original;
    unsigned scaled = (original * percent + 99) / 100;
    return scaled > 127 ? 127 : scaled;
}
