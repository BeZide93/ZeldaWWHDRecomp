/* Example guest mod: a small story mod that fills the story notebook. Its letter arrives by Rito mail
 * (the first letter from a mod brings the notebook, kept in the Delivery Bag); once the letter is read
 * the quest starts, its next step points at Windfall Island, and the player can show it on the sea
 * chart from the notebook. A second quest waits for a condition; a third is done at once.
 * All texts are original to this example. */
#include "wwhd_guest.h"
#include "wwhd/functions.h"
#include "wwhd/save.h"

static const char kSender[] = "The storyteller";
static const char kText[] =
    "Dear Link,\n"
    "I lost my lantern on my way to Windfall.\n"
    "Without it, no story can be told at night.\n"
    "I have sent you a notebook: keep my tale\n"
    "in it, and come and see me.";

static u32 steps;
static s32 last = -100;

WWHD_HOOK(WWHD_ADDR_daPy_Execute, story_notebook_step, (void* link)) {
    (void)link;
    if (++steps % 30) return;  /* twice a second is plenty */
    u16 max_life = dComIfGs_player_status_a.max_life;
    if (max_life < 12 || max_life > 80) return;  /* no save file loaded */
    /* every call is idempotent: the save file records the letter and the steps */
    s32 letter = wwhd_mail_send("lantern", kSender, kText);

    s32 quest = wwhd_quest_define("lantern", "The storyteller's lantern", "A story for the long nights", 0);
    if (letter == WWHD_MAIL_READ && quest == WWHD_QUEST_NEW) quest = wwhd_quest_step("lantern", 1, 0, 0);
    if (quest == WWHD_QUEST_NEW) {
        wwhd_quest_step("lantern", 0, "Check a Rito postbox for a letter.", 0);
        wwhd_quest_no_place("lantern");
    } else if (quest == 1) {
        wwhd_quest_step("lantern", 1, "Ask the storyteller on Windfall about his lantern.", "Windfall Island");
        wwhd_quest_place("lantern", WWHD_CHART_COLUMN_X('D'), WWHD_CHART_ROW_Z(2));
        wwhd_quest_known("lantern", "A storyteller lost his lantern on his way to Windfall.\n"
                                    "He sent you this notebook by Rito mail.");
    }

    wwhd_quest_define("tower", "Lights over the tower", "A glimpse of the old kingdom",
                      "after the Tower of the Gods rises");
    wwhd_quest_define("welcome", "A letter by Rito mail", 0, 0);
    if (letter == WWHD_MAIL_READ) wwhd_quest_done("welcome");
    else wwhd_quest_step("welcome", 0, "Open the letter the postbox keeps for you.", 0);

    if (quest != last) {
        last = quest;
        wwhd_log_int("story-notebook: lantern quest state (0 new, 1.. step, 255 done)", quest);
    }
}
