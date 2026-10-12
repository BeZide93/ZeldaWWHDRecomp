/* Example guest mod: a story mod's chart. When the mod is on, Link gets the storyteller's chart in the
 * loaded save file, and the chart marks Windfall Island (square D2) on his sea chart, with the white
 * ring the game uses for the next story island. The save file remembers the chart; the marker is
 * placed again every session. A real quest would give the chart later (for example once its letter
 * was read, wwhd_mail_state) and move the marker as the quest goes on. */
#include "wwhd_guest.h"
#include "wwhd/functions.h"
#include "wwhd/save.h"

static u32 steps;
static s32 last = -100;

WWHD_HOOK(WWHD_ADDR_daPy_Execute, story_chart_step, (void* link)) {
    (void)link;
    if (++steps % 30) return;  /* twice a second is plenty */
    u16 max_life = dComIfGs_player_status_a.max_life;
    if (max_life < 12 || max_life > 80) return;  /* no save file loaded */
    /* Both calls are idempotent: the save file records the chart, mark only moves the marker. */
    wwhd_chart_give("tale");
    s32 state = wwhd_chart_mark("tale", "start", WWHD_CHART_COLUMN_X('D'), WWHD_CHART_ROW_Z(2),
                                WWHD_CHART_ICON_STORY, "Windfall Island: the storyteller");
    if (state != last) {
        last = state;
        wwhd_log_int("story-chart: chart state (1 owned, 2 finished)", state);
    }
}
