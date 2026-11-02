#include <assert.h>
#include <stdio.h>
#include "state.h"

int main(void) {
    DLConfig c = {100, 5, 20, 200, 0};
    DLState s = {0};
    dl_hit(&s, &c, 0); assert(s.charge == 0);
    for (int i = 0; i < 19; ++i) dl_hit(&s, &c, 1);
    dl_tick(&s, &c, 0, 1, 1); assert(!s.active && s.charge == 95);
    dl_hit(&s, &c, 1); assert(dl_ready(&s, &c));
    dl_hit(&s, &c, 1); assert(s.charge == 100);
    dl_tick(&s, &c, 0, 1, 0); assert(!s.active); /* paused input */
    dl_tick(&s, &c, 0, 1, 1); assert(s.active && s.remaining == 20);
    dl_hit(&s, &c, 1); assert(s.charge == 0); /* no charge while transformed */
    for (int i = 0; i < 900; ++i) dl_tick(&s, &c, 1.0f/30, 0, 0);
    assert(s.remaining == 20); /* menus/cutscenes don't drain */
    for (int fps = 30; fps <= 60; fps += 30) {
        s = (DLState){.active=1, .remaining=20};
        for (int i = 0; i < fps * 20 - 1; ++i) dl_tick(&s, &c, 1.0f/fps, 0, 1);
        assert(s.active);
        for (int i = 0; i < 2; ++i) dl_tick(&s, &c, 1.0f/fps, 0, 1);
        assert(!s.active && s.charge == 0);
    }
    c.free_transform = 1;
    dl_tick(&s, &c, 0, 1, 1); assert(s.active);
    for (int i = 0; i < 3000; ++i) dl_tick(&s, &c, 1.0f/30, 0, 1);
    assert(s.active && s.remaining == 20);
    dl_tick(&s, &c, 0, 1, 1); assert(!s.active); /* manual cancellation */
    c.free_transform = 0; c.capacity = 25;
    s.charge = 100; dl_tick(&s, &c, 0, 0, 1); assert(s.charge == 25);
    dl_reset(&s); assert(!s.active && !s.charge && !s.remaining);
    assert(dl_damage(0, 500) == 0);
    assert(dl_damage(3, 200) == 6);
    assert(dl_damage(3, 125) == 4);
    assert(dl_damage(100, 500) == 127);
    assert(dl_damage(128, 200) == 128); /* native sentinel stays intact */
    puts("Dark Link gameplay tests passed");
}
