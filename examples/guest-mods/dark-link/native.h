/* WWHD USA-v0 identities; WWHD_GAME_FUNC/HOOK relocate these for EU at install.
 * Layout evidence: public ZeldaWWHDDecomp/wwhd (CC0), d_cc_uty.h, d_cc_d.h,
 * JSystem/J3DGraph.h and d_a_player_main_01.h. No game data is included. */
#pragma once
#include "wwhd_guest.h"
#include "wwhd/functions.h"
#include "wwhd/actor.h"
#include "wwhd/link.h"
#include "wwhd/save.h"

#define DL_AT_POWER 0x02518DB0
#define DL_CC_CHECK 0x025192A8
#define DL_MODEL_ENTRY 0x027F4F1C

WWHD_GAME_FUNC(0x027DF9B0, s32, dl_material_name_index, (void*, const char*));
/* The same two sead::SafeString names that Link's createHeap uses to find the
 * left/right eye materials. Use relocatable guest data, never copied strings. */
#define DL_EYE_MATERIAL_NAMES 0x1046CD2C
WWHD_GAME_FUNC(0x025986BC, int, dl_menu_open, (void));
WWHD_GAME_FUNC(WWHD_ADDR_dScnPly_isPause, int, dl_paused, (void));

typedef struct {
    u32 object, actor;
    u8 damage, dead, type, pad;
    s16 angle[3];
    u16 cut;
    u32 particle_position, hit_sound;
} DLAttack;
_Static_assert(sizeof(DLAttack) == 0x1c, "CcAtInfo size");
_Static_assert(__builtin_offsetof(DLAttack, damage) == 8, "CcAtInfo damage");

static u32 dl_word(const void* p, u32 offset) { return *(const u32*)((const u8*)p + offset); }
static u32 dl_actor_id(const void* p) { return dl_word(p, 4); }
static daPy_lk_c* dl_player(void) {
    u8* play = wwhd_play_get();
    return play ? (daPy_lk_c*)dl_word(play, WWHD_PLAY_PLAYER_OFFSET) : 0;
}
static int dl_sword(const DLAttack* hit, const daPy_lk_c* link) {
    if (!hit || !link || !hit->object || hit->actor != (u32)link || hit->type != 1) return 0;
    /* Require one of Link's actual three sword capsules, not a carried weapon,
     * hammer, bomb, arrow, enemy sword, or merely RESULT_SWORD classification. */
    u32 first = (u32)link + 0x7b1c;
    return (hit->object == first || hit->object == first + 0x138 ||
            hit->object == first + 2 * 0x138) &&
           (dl_word((void*)hit->object, 0x10) & 2);
}
