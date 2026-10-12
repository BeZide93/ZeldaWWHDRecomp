/* Example guest mod: the story-mod letter. When the mod is on, a letter waits in the Rito postbox
 * of the loaded save file; the postbox hands it out like the game's own mail and the game shows
 * it in its letter window. The save file remembers it, so it arrives once per file.
 * The text is original to this example. */
#include "wwhd_guest.h"
#include "wwhd/functions.h"
#include "wwhd/save.h"

static const char kSender[] = "A wandering storyteller";
static const char kText[] =
    "Dear Link,\n"
    "a new tale has reached the Great Sea.\n"
    "If a mod like this one is on, look out\n"
    "for something new on your travels.\n"
    "Fair winds!";

static u32 steps;
static s32 last = -100;

WWHD_HOOK(WWHD_ADDR_daPy_Execute, rito_letter_step, (void* link)) {
    (void)link;
    if (++steps % 30) return;  /* twice a second is plenty */
    u16 max_life = dComIfGs_player_status_a.max_life;
    if (max_life < 12 || max_life > 80) return;  /* no save file loaded */
    /* Idempotent: the save file records the letter, so this never sends it twice. */
    s32 state = wwhd_mail_send("hello", kSender, kText);
    if (state != last) {
        last = state;
        wwhd_log_int("rito-letter: letter state (1 waiting, 2 read)", state);
    }
}
