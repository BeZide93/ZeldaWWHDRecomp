/* SPDX-License-Identifier: MPL-2.0
 * Native Link attacks. Verified public WWHD layouts/entry points are described
 * in NATIVE_COMBAT.md. The scheduler starts an animation, never a fake hit.
 * Only native cc_at_check contacts against this run's enemies award damage.
 */
#include "game.h"
#include "combat.h"
#include "wwhd/actor.h"
#include "wwhd/link.h"
#include "wwhd/bindings.h"
#include "wwhd/data.h"

#define SHOTS 16
#define KEEP_EQUIP 0x6590
#define KEEP_THROW 0x6598
#define KEEP_GRAB 0x65A0
#define LOCK_ACTOR 0x65B0
#define BOMB_SPHERE 0x920
#define SPHERE_GEOMETRY 0x118
#define HERO_SWORD 0x38
#define SWORD_EQUIP 0x103
typedef struct {
    HsId id, target;
    u32 pointer;
    int weapon, damage, retiring;
    float reach, born;
    unsigned hit_mask, generations[HS_ENEMIES];
} NativeShot;
static NativeShot shots[SHOTS], spin;
static daPy_lk_c *player;
static int busy_weapon = -1;
static float started;
static u32 lookup_result;
static const u16 shot_profiles[] = {0, 0x1D8, 0x1B0, 0x126};

static int valid(u32 p, u32 size) { return !(p & 3) && p >= 0x10000000u && p <= 0x50000000u - size; }
static u32 word(void *p, u32 offset) { return *(u32 *)((u8 *)p + offset); }
static fopAc_ac_c *lookup(HsId id) {
    lookup_result = 0;
    wwhd_fopAcM_SearchByID(id, (u32)&lookup_result);
    if (!valid(lookup_result, 0x3AC) || word((void *)lookup_result, 4) != id)
        return 0;
    return (fopAc_ac_c *)lookup_result;
}
static fopAc_ac_c *kept(void *link, u32 keep) {
    u32 p = word(link, keep + 4);
    if (!valid(p, 0x3AC) || word((void *)p, 4) != word(link, keep))
        return 0;
    return (fopAc_ac_c *)p;
}
static NativeShot *shot_for(void *actor) {
    if (!actor)
        return 0;
    for (int i = 0; i < SHOTS; i++)
        if (shots[i].pointer == (u32)actor && shots[i].id == word(actor, 4) &&
            *(u16 *)((u8 *)actor + 0xE) == shot_profiles[shots[i].weapon])
            return &shots[i];
    return 0;
}
static fopAc_ac_c *target_for(HsId id) {
    fopAc_ac_c *target = lookup(id);
    int slot = hs_enemy_slot(target);
    return slot >= 0 && hs.enemies[slot].state == HS_ALIVE && hs.enemies[slot].hp > 0 ? target : 0;
}
static int walking(daPy_lk_c *link) {
    int p = link->mCurProc;
    return p == 4 || p == 6 || p == 7 || p == 8 || p == 9 || p == 0x17 || p == 0x18;
}
static u16 upper(daPy_lk_c *link) { return *(u16 *)&link->m_anm_heap_upper[2 * 0x10]; }
static int upper_done(daPy_lk_c *link) { return link->mFrameCtrlUpper[2].mRate < .01f; }
static void aim(fopAc_ac_c *actor, fopAc_ac_c *target) {
    actor->shape_angle.y = actor->current.angle.y =
        wwhd_cLib_targetAngleY_hd(&actor->current.pos, &target->current.pos);
}
static void clear_keep(void *link, u32 offset) {
    *(u32 *)((u8 *)link + offset) = HS_NONE;
    *(u32 *)((u8 *)link + offset + 4) = 0;
}
static void cancel_held(daPy_lk_c *link) {
    if (shot_for(kept(link, KEEP_EQUIP)) || link->mEquipItem == SWORD_EQUIP || link->mEquipItem == 0x27)
        wwhd_daPy_lk_c__deleteEquipItem(link, 0);
    fopAc_ac_c *grab = kept(link, KEEP_GRAB);
    if (shot_for(grab)) {
        wwhd_fopAcM_delete_id(word(grab, 4));
        clear_keep(link, KEEP_GRAB);
    }
}
void hs_combat_reset(void) {
    memset(shots, 0, sizeof(shots));
    memset(&spin, 0, sizeof(spin));
    player = 0;
    busy_weapon = -1;
    started = 0;
}
void hs_combat_provision(void) {
    /* These bytes are inside the existing pre-run serializer/return snapshot.
     * Temporary sword selection also supports Quest Logs without a sword. */
    u32 base = WWHD_GAME_DATA(WWHD_ADDR_dComIfG_save_info_pointer, u32);
    if (!valid(base, 0x100))
        return;
    u8 *save = (u8 *)base + 0x20;
    save[0x0E] = HERO_SWORD;
    save[0x69] = save[0x6A] = save[0x6F] = save[0x70] = 30;
}
void hs_combat_stop(void *link) {
    if (link && hs_in_arena()) {
        cancel_held(link);
        fopAc_ac_c *thrown = kept(link, KEEP_THROW);
        if (shot_for(thrown))
            clear_keep(link, KEEP_THROW);
        /* Stop pending run-ammo deltas from applying after story restoration.
         * dComIfG_play_c begins at gameinfo +0x12A0. */
        u8 *play = wwhd_play_get();
        *(s16 *)(play + 0x5B68) = 0;
        *(s16 *)(play + 0x5B6C) = 0;
    }
    for (int i = 0; i < SHOTS; i++)
        if (shots[i].pointer) {
            fopAc_ac_c *a = lookup(shots[i].id);
            if (a && shot_for(a) == &shots[i])
                wwhd_fopAcM_delete_id(shots[i].id);
            shots[i].retiring = 1;
        }
    memset(&spin, 0, sizeof(spin));
    busy_weapon = -1;
}
void hs_combat_step(void *link) {
    player = link;
    if (!hs_in_arena())
        return;
    for (int i = 0; i < SHOTS; i++)
        if (shots[i].pointer) {
            fopAc_ac_c *a = lookup(shots[i].id);
            if (!a || shot_for(a) != &shots[i]) {
                memset(&shots[i], 0, sizeof(shots[i]));
                continue;
            }
            if (shots[i].retiring || hs.elapsed - shots[i].born > 15) {
                shots[i].retiring = 1;
                wwhd_fopAcM_delete_id(shots[i].id);
            }
        }
    if (hs.phase == HS_WIN || hs.phase == HS_LOSE || hs.phase == HS_ERROR) {
        hs_combat_stop(link);
        return;
    }
    if (hs.phase != HS_RUN)
        return;
    hs_combat_provision();
    if (busy_weapon < 0)
        return;
    float age = hs.elapsed - started;
    if (age < .1f)
        return;
    u16 anim = upper(player);
    int done = 0;
    if (busy_weapon == HS_SWORD)
        done = player->mCurProc != 0x55;
    else if (busy_weapon == HS_BOW)
        done = (anim == 0xE && upper_done(player)) || (anim != 0xC && anim != 0x36 && anim != 0xE);
    else if (busy_weapon == HS_BOOMERANG)
        done = (anim != 0x34 && anim != 0x33) || upper_done(player);
    else
        done = player->mCurProc != 0x71;
    if (done || age > 4) {
        cancel_held(player);
        busy_weapon = -1;
        /* A spin has no independent actor; stop accepting its old contacts. */
        if (player->mCurProc != 0x55)
            memset(&spin, 0, sizeof(spin));
    }
}
int hs_combat_attack(int weapon, HsId target_id, int damage, float reach) {
    if (weapon < 0 || weapon >= HS_WEAPONS || damage <= 0)
        return 0;
    if (!hs_in_arena() || hs.phase != HS_RUN || !player || busy_weapon >= 0 || !walking(player))
        return 0;
    if (kept(player, KEEP_GRAB) || kept(player, KEEP_EQUIP))
        return 0;
    if (weapon == HS_BOOMERANG && kept(player, KEEP_THROW))
        return 0;
    fopAc_ac_c *target = target_for(target_id);
    if (!target)
        return 0;
    int free_slot = -1;
    for (int i = 0; i < SHOTS; i++)
        if (!shots[i].pointer) {
            free_slot = i;
            break;
        }
    if (weapon != HS_SWORD && free_slot < 0)
        return 0;
    /* Preserve rolling, damage, falls, item catches and every other non-walking
     * proc. No animation is interrupted merely because a cooldown expired. */
    wwhd_daPy_lk_c__deleteEquipItem(player, 0);
    aim((fopAc_ac_c *)player, target);
    hs_combat_provision();
    NativeShot info = {.target = target_id, .weapon = weapon, .damage = damage, .reach = reach, .born = hs.elapsed};
    fopAc_ac_c *actor = 0;
    if (weapon == HS_SWORD) {
        player->mEquipItem = SWORD_EQUIP;
        wwhd_daPy_lk_c__setSwordModel(player, 0);
        if (!wwhd_daPy_lk_c__procCutTurn_init(player, 1))
            return 0;
        player->m35A4 *= reach;
        info.id = word(player, 4);
        info.pointer = (u32)player;
        spin = info;
    } else if (weapon == HS_BOW) {
        player->mEquipItem = 0x27;
        wwhd_daPy_lk_c__makeItemType(player);
        wwhd_daPy_lk_c__setBowReloadAnime(player);
        wwhd_daPy_lk_c__makeArrow(player);
        player->m355E = 0;
        actor = kept(player, KEEP_EQUIP);
        /* The normal ready-action code releases the nocked arrow and plays
         * ARROWSHOOT after ARROWRELOAD/BOWWAIT. No first-person aim proc. */
    } else if (weapon == HS_BOOMERANG) {
        player->mEquipItem = 0x2D;
        wwhd_daPy_lk_c__makeItemType(player);
        actor = kept(player, KEEP_EQUIP);
        if (actor) {
            /* Native BOOMTHROW: checkItemAction releases the real actor at
             * animation frame 7 through throwBoomerang, then plays recovery. */
            wwhd_daPy_lk_c__setActAnimeUpper(player, 0x34, 2, 1.5f, 2.f, 10, 3.f);
            wwhd_daPy_lk_c__setPriTextureAnime(player, 0x48, 0);
            /* HD ready-action state: catch uses the saved throw direction. */
            *((u8 *)player + 0x827C) = 0;
            *(s16 *)((u8 *)player + 0x827E) = ((fopAc_ac_c *)player)->shape_angle.y;
        }
    } else {
        if (player->mActivePlayerBombs >= 3)
            return 0;
        player->mEquipItem = 0x31;
        wwhd_daPy_lk_c__makeItemType(player);
        actor = kept(player, KEEP_GRAB);
        if (actor)
            wwhd_daPy_lk_c__procGrabThrow_init(player, 0);
    }
    if (weapon != HS_SWORD) {
        if (!actor || *(u16 *)((u8 *)actor + 0xE) != shot_profiles[weapon]) {
            wwhd_daPy_lk_c__deleteEquipItem(player, 0);
            return 0;
        }
        info.id = word(actor, 4);
        info.pointer = (u32)actor;
        shots[free_slot] = info;
    }
    busy_weapon = weapon;
    started = hs.elapsed;
    return 1;
}

/* Real arrow launch: select a direction before native velocity, capsule and
 * blur setup. Physics, wall hits, impact effects and lifetime remain native. */
WWHD_GAME_ORIGINAL(0x02053814, void, original_arrow_shoot, (void *));
WWHD_REPLACE(0x02053814, void, survivors_arrow_shoot, (void *actor)) {
    NativeShot *shot = hs_in_arena() ? shot_for(actor) : 0;
    if (shot) {
        fopAc_ac_c *target = target_for(shot->target), *arrow = actor;
        if (target) {
            cXyz point = target->eyePos;
            arrow->current.angle.y = wwhd_cLib_targetAngleY_hd(&arrow->current.pos, &point);
            arrow->current.angle.x = wwhd_cLib_targetAngleX_hd(&arrow->current.pos, &point);
            arrow->shape_angle.x = -arrow->current.angle.x;
            arrow->shape_angle.y = arrow->current.angle.y;
        }
    }
    original_arrow_shoot(actor);
}
WWHD_GAME_ORIGINAL(0x023FB10C, void, original_throw_boomerang, (void *));
WWHD_REPLACE(0x023FB10C, void, survivors_throw_boomerang, (void *link)) {
    NativeShot *shot = hs_in_arena() ? shot_for(kept(link, KEEP_EQUIP)) : 0;
    u32 previous = word(link, LOCK_ACTOR);
    if (shot) {
        fopAc_ac_c *target = target_for(shot->target);
        *(u32 *)((u8 *)link + LOCK_ACTOR) = (u32)target;
        if (target)
            aim(link, target);
    }
    original_throw_boomerang(link);
    if (shot)
        *(u32 *)((u8 *)link + LOCK_ACTOR) = previous;
}
/* This setter is shared; alter only the explosion sphere of a tracked bomb. */
WWHD_GAME_ORIGINAL(0x02018C8C, void, original_sphere_radius, (void *, f32));
WWHD_REPLACE(0x02018C8C, void, survivors_sphere_radius, (void *sphere, f32 radius)) {
    if (hs_in_arena() && radius == 200.f)
        for (int i = 0; i < SHOTS; i++) {
            NativeShot *shot = &shots[i];
            if (shot->pointer && shot->weapon == HS_BOMB &&
                (u32)sphere == shot->pointer + BOMB_SPHERE + SPHERE_GEOMETRY) {
                radius *= shot->reach;
                break;
            }
        }
    original_sphere_radius(sphere, radius);
}
/* Native projectile motion/fuses pause together with Link and owned enemies. */
#define PROJECTILE_EXEC(address, name)                                                                                 \
    WWHD_GAME_ORIGINAL(address, s32, original_##name, (void *));                                                       \
    WWHD_REPLACE(address, s32, survivors_##name, (void *actor)) {                                                      \
        if (hs_in_arena() && shot_for(actor) && hs.phase != HS_RUN)                                                    \
            return 1;                                                                                                  \
        return original_##name(actor);                                                                                 \
    }
PROJECTILE_EXEC(0x02053184, arrow)
PROJECTILE_EXEC(0x020CC454, boomerang)
PROJECTILE_EXEC(0x020C506C, bomb)

WWHD_GAME_ORIGINAL(0x025192A8, u32, original_contact, (void *, void *));
WWHD_REPLACE(0x025192A8, u32, survivors_contact, (void *target, void *at_info)) {
    int slot = hs_in_arena() ? hs_enemy_slot(target) : -1;
    if (slot < 0)
        return original_contact(target, at_info);
    u32 object = word(at_info, 0), source = 0;
    if (valid(object, 0x48)) {
        u32 status = word((void *)object, 0x44);
        if (valid(status, 0x10))
            source = word((void *)status, 0xC);
    }
    NativeShot *shot = valid(source, 0x3AC) ? shot_for((void *)source) : 0;
    if (!shot && source == spin.pointer && spin.pointer && word((void *)source, 4) == spin.id)
        shot = &spin;
    if (!shot)
        return original_contact(target, at_info);
    fopAc_ac_c *enemy = target;
    HsEnemy *run_enemy = &hs.enemies[slot];
    /* Native damage/reactions still execute, but their signed-byte health must
     * not kill a scaled enemy before the run HP says so (elite HP exceeds 127).
     * Native defenses decide whether this function is reached in the first place. */
    enemy->health = 100;
    u32 result = original_contact(target, at_info);
    if (hs.phase == HS_RUN && (run_enemy->state == HS_ALIVE || run_enemy->state == HS_PENDING) &&
        (!(shot->hit_mask & (1u << slot)) || shot->generations[slot] != run_enemy->generation)) {
        shot->hit_mask |= 1u << slot;
        shot->generations[slot] = run_enemy->generation;
        hs_native_hit(&hs, run_enemy->id, shot->damage, shot->weapon);
    }
    enemy->health = run_enemy->hp > 0 ? 100 : 0;
    if (!run_enemy->hp)
        *((u8 *)at_info + 9) = 1;
    return result;
}
