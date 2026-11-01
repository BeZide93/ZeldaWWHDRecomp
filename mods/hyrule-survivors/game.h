/* SPDX-License-Identifier: MPL-2.0 */
#ifndef HS_GAME_H
#define HS_GAME_H
#include "wwhd_guest.h"
#include "core.h"
extern HsRun hs;
extern int hs_session, hs_can_open;
extern unsigned hs_best_kills, hs_wins;
void hs_draw(u32 list);
void hs_touch(const wwhd_touch_event *event);
void hs_action(int action);
/* UI actions share one path for touch and controller. */
enum {
    HS_ACTION_OPEN = 1,
    HS_ACTION_ACCEPT,
    HS_ACTION_BACK,
    HS_ACTION_LEFT,
    HS_ACTION_RIGHT,
    HS_ACTION_RETRY,
    HS_ACTION_EXIT,
    HS_ACTION_CHOICE0 = 16
};
#endif
