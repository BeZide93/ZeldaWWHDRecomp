/* Example guest mod: one small story quest in the story notebook, "The storyteller's lantern".
 *
 * The quest is described as data in manifest.json ("quests"): its letter (which brings the notebook and
 * a sealed letter for the storyteller's old friend), the step with its place on the chart, the friend's
 * lines and the reward. The port does all of that. A mod's code only reports progress the port can't
 * see; this quest has none, so the code just logs the quest's state as it changes.
 * All texts are original to this example. */
#include "wwhd_guest.h"
#include "wwhd/functions.h"
#include "wwhd/save.h"

static u32 steps;
static s32 last = -100;

WWHD_HOOK(WWHD_ADDR_daPy_Execute, story_notebook_step, (void* link)) {
    (void)link;
    if (++steps % 30) return; /* twice a second is plenty */
    u16 max_life = dComIfGs_player_status_a.max_life;
    if (max_life < 12 || max_life > 80) return; /* no save file loaded */
    s32 quest = wwhd_quest_state("lantern");
    if (quest != last) {
        last = quest;
        wwhd_log_int("story-notebook: lantern quest state (0 new, 1.. step, 255 done)", quest);
    }
}
