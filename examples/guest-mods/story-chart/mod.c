/* Example guest mod: a story mod's chart, treated like the game's treasure charts. When the mod is
 * on, Link gets the storyteller's chart in the loaded save file. Its marker is a spot on Windfall
 * Island (square D2): the top of the lighthouse. Like an owned treasure chart, it puts nothing on the
 * sea chart; once Link stands on the spot, the mod finishes the chart, and the game's red circle (the
 * mark of a salvaged treasure) marks the spot on the sea chart and in the zoomed square. The save
 * file remembers the chart and whether it is finished; the marker is placed again every session. */
#include "wwhd_guest.h"
#include "wwhd/actor.h"
#include "wwhd/functions.h"
#include "wwhd/save.h"

#define SPOT_X 1759.0f
#define SPOT_Z (-202572.0f)

static u32 steps;
static s32 last = -100;

WWHD_HOOK(WWHD_ADDR_daPy_Execute, story_chart_step, (void* link)) {
    if (++steps % 15) return;  /* four times a second is plenty */
    u16 max_life = dComIfGs_player_status_a.max_life;
    if (max_life < 12 || max_life > 80) return;  /* no save file loaded */
    /* Both calls are idempotent: the save file records the chart, mark only moves the marker. */
    wwhd_chart_give("tale");
    s32 state = wwhd_chart_mark("tale", "lighthouse", SPOT_X, SPOT_Z, WWHD_CHART_ICON_STORY,
                                "Windfall Island: the lighthouse");
    /* Link on the spot (the sea's world coordinates): the chart is done */
    const fopAc_ac_c* actor = (const fopAc_ac_c*)link;
    f32 dx = actor->current.pos.x - SPOT_X, dz = actor->current.pos.z - SPOT_Z;
    if (state == WWHD_CHART_OWNED && dx * dx + dz * dz < 600.0f * 600.0f) state = wwhd_chart_finish("tale");
    if (state != last) {
        last = state;
        wwhd_log_int("story-chart: chart state (1 owned, 2 finished)", state);
    }
}
