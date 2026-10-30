/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../core.h"

static int wall(void* limit,float factor) { return factor > *(float*)limit; }
int main(void) {
    EcState s; ec_init(&s);
    ec_start(&s,EC_JUMP,0);
    assert(ec_buttons(&s)==EC_ZL);
    ec_tick(&s,.07); assert(ec_buttons(&s)==(EC_ZL|EC_A));
    ec_tick(&s,.14); assert(ec_buttons(&s)==EC_ZL);
    ec_tick(&s,.06); assert(!ec_buttons(&s));
    ec_start(&s,EC_SLASH,0); ec_tick(&s,.07); assert(ec_buttons(&s)==(EC_ZL|EC_B));
    ec_start(&s,EC_SPIN,0); assert(ec_buttons(&s)==EC_B);
    for(int i=0;i<11;++i) ec_tick(&s,.1);
    ec_tick(&s,.01); assert(!ec_buttons(&s) && ec_active(&s));
    ec_tick(&s,.05); assert(!ec_active(&s));
    ec_start(&s,EC_DODGE,-.8f); assert(s.dodge_x==-1 && s.dodge_y==0);
    ec_start(&s,EC_DODGE,0); assert(s.dodge_x==0 && s.dodge_y==-1);
    ec_cancel(&s); assert(!ec_buttons(&s) && s.contact==EC_NONE);
    ec_down(&s,4,0,0); ec_tick(&s,.05); ec_up(&s,0,0,1);
    ec_tick(&s,.1); ec_down(&s,4,0,0); ec_tick(&s,.05); ec_up(&s,0,0,1);
    assert(ec_buttons(&s)==EC_ZL); ec_tick(&s,.09); assert(!ec_buttons(&s));
    /* Swipes, long holds, distant taps and pinches must not recenter. */
    ec_cancel(&s); ec_down(&s,4,0,0); ec_move(&s,30,0); ec_up(&s,0,0,1); assert(!s.previous_tap);
    ec_down(&s,4,0,0); ec_tick(&s,.25); ec_up(&s,0,0,1); assert(!s.previous_tap);
    ec_down(&s,4,0,0); ec_up(&s,0,0,1); ec_down(&s,4,100,0); ec_up(&s,100,0,1); assert(!ec_buttons(&s));
    ec_pinch(&s,2); assert(s.distance==.5f && !s.previous_tap);
    ec_pinch(&s,.5f); assert(s.distance==2); /* ratios stay relative to start */
    ec_pinch(&s,0); assert(s.distance==2);
    ec_pinch(&s,NAN); assert(s.distance==2);
    s.pinching=0; ec_pinch(&s,2); assert(s.distance==1);
    ec_up(&s,0,0,1); assert(!ec_buttons(&s));
    float limit=1.4f;
    float f=ec_collision_limit(2,wall,&limit); assert(f>1 && f<limit);
    assert(ec_collision_limit(.5f,wall,&limit)==.5f);
    limit=1; assert(ec_collision_limit(2,wall,&limit)==1);
    EcRect r={420,120,260,140}; assert(ec_inside(r,420,120)); assert(!ec_inside(r,0,0));
    puts("Enhanced core: macro timing, gesture rejection, zoom and collision tests passed");
}
