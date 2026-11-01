/* SPDX-License-Identifier: MPL-2.0
 * WWHD adapter. USA addresses are SDK relocations, including on EU installs.
 * The package borrows Cave09/Room1 through the host's stage builder; no game
 * models, textures, maps or code are included in the package.
 */
#include "game.h"
#include "combat.h"
#include "wwhd/actor.h"
#include "wwhd/objects.h"
#include "wwhd/data.h"
#include "wwhd/bindings.h"
#include "stage_name.h"

#define BTN_A 0x8000u
#define BTN_B 0x4000u
#define BTN_LEFT 0x0800u
#define BTN_RIGHT 0x0400u
#define BTN_PLUS 0x0008u
#define BTN_MINUS 0x0004u
#define OPEN_CHORD (0x20u | 0x10u | BTN_MINUS)
#define SAVE_BYTES 0x778u
#define GRID 13
#define CELLS (GRID * GRID)

HsRun hs;
int hs_session, hs_can_open;
unsigned hs_best_kills, hs_wins;
static int registered, settled, returning, retrying, action, was_arena, result_saved;
static int snapshot_slot = -1, stick_latched, pending_open;
static unsigned previous_buttons, pressed, launch_serial, spawn_cursor;
static unsigned long long option_revision, last_step = ~0ull;
static float loading_time, modal_age;
static u8 snapshot[SAVE_BYTES], temporary_save[SAVE_BYTES];
static char return_stage[8];
static s16 return_point;
static s8 return_room, return_layer;
static HsVec return_position;
static s16 return_angle;
static unsigned records[5];
static struct {
    u32 id, parent;
} children[128];
static struct {
    float floor;
    int valid, connected;
} grid[CELLS];
static int queue[CELLS], spawn_cells[CELLS], spawn_count;
static HsVec ground_query;
static u32 actor_result;
static const u16 profiles[] = {0xD7, 0xCE, 0xF7, 0xBD, 0xBC};
/* Native ordinary variants, no defeat switch/path. See story_enemies.h. */
static const u32 parameters[] = {0xFFFF0000u, 0xFF000000u, 0xFFFF0A01u, 0xFFFF9604u, 0xFFFF0000u};

static int same(const char *a, const char *b) {
    for (int i = 0; i < 8; i++) {
        if (a[i] != b[i])
            return 0;
        if (!a[i])
            return 1;
    }
    return 1;
}
static int valid_ptr(u32 p, u32 n) { return !(p & 3) && p >= 0x10000000u && p <= 0x50000000u - n; }
static u8 *save_base(void) {
    u32 p = WWHD_GAME_DATA(WWHD_ADDR_dComIfG_save_info_pointer, u32);
    return valid_ptr(p, 0x12C0) ? (u8 *)p : 0;
}
static int slot(void) {
    u8 *p = save_base();
    return p ? p[0x20 + 0x1290] : -1;
}
static int same_slot(void) { return snapshot_slot >= 0 && snapshot_slot == slot(); }
static int arena(u8 *play) { return play && same((char *)play + WWHD_PLAY_START_STAGE_NAME_OFFSET, HS_STAGE_NAME); }
static int event_busy(u8 *p) {
    return p[WWHD_PLAY_EVENT_RUNNING_OFFSET] || p[0x5BB2] || p[0x5BB3] || WWHD_GAME_DATA(0x101EA069, u8) ||
           WWHD_GAME_DATA(0x101F8348, u32) == 2;
}
static void restore_save(void) {
    u8 *p = save_base();
    if (hs_session && p && same_slot())
        memcpy(p + 0x20, snapshot, SAVE_BYTES);
}
static fopAc_ac_c *find_actor(HsId id, int kind, int *creating) {
    actor_result = 0;
    int found = wwhd_fopAcM_SearchByID(id, (u32)&actor_result);
    *creating = found && !actor_result;
    if (!valid_ptr(actor_result, 0x3AC))
        return 0;
    fopAc_ac_c *a = (fopAc_ac_c *)actor_result;
    if (*(u32 *)((u8 *)a + 4) != id || *(u16 *)((u8 *)a + 0xE) != profiles[kind] ||
        a->mParameters != parameters[kind] || a->current.roomNo != 0)
        return 0;
    return a;
}
static int observe(HsId id, int kind, HsVec *pos) {
    int creating = 0;
    fopAc_ac_c *a = find_actor(id, kind, &creating);
    if (!a)
        return creating ? 1 : 0;
    pos->x = a->current.pos.x;
    pos->y = a->current.pos.y;
    pos->z = a->current.pos.z;
    /* SearchByID does not expose a partially constructed actor; the create
     * request remains in the creating list until its phase is complete. */
    return a->health <= 0 ? 3 : 2;
}
static void remove_actor(HsId id) { wwhd_fopAcM_delete_id(id); }
static float ground(float x, float z, float from) {
    ground_query = (HsVec){x, from, z};
    return wwhd_dBgS_ObjGndChk_Func((u32)&ground_query);
}
static int finite_floor(float y) { return y == y && y > -5000 && y < 5000; }
static float absf(float x) { return x < 0 ? -x : x; }
static HsVec cell_position(int c) {
    return (HsVec){(float)(c % GRID - 6) * 250, grid[c].floor + 12, (float)(c / GRID - 6) * 250};
}
static int scan_arena(fopAc_ac_c *link) {
    /* Select a real, broad floor patch, never the spawn height or a stair lip.
     * Connected grid cells with matching floor height form this room's bounded
     * spawn pool. Four extra footprint samples reject holes and ledges. */
    int seed = -1;
    float best = 1e30f;
    for (int i = 0; i < CELLS; i++) {
        float x = (float)(i % GRID - 6) * 250, z = (float)(i / GRID - 6) * 250;
        float y = ground(x, z, 6000);
        int okay = finite_floor(y);
        const float dx[] = {65, -65, 0, 0}, dz[] = {0, 0, 65, -65};
        for (int k = 0; k < 4 && okay; k++) {
            float g = ground(x + dx[k], z + dz[k], y + 120);
            okay = finite_floor(g) && absf(g - y) < 50;
        }
        grid[i].floor = y;
        grid[i].valid = okay;
        grid[i].connected = 0;
        float score = (x - 650) * (x - 650) + z * z;
        if (okay && score < best) {
            best = score;
            seed = i;
        }
    }
    if (seed < 0)
        return 0;
    int begin = 0, end = 1;
    queue[0] = seed;
    grid[seed].connected = 1;
    while (begin < end) {
        int c = queue[begin++];
        int neighbors[] = {c - 1, c + 1, c - GRID, c + GRID};
        for (int k = 0; k < 4; k++) {
            int n = neighbors[k];
            if (n < 0 || n >= CELLS || (k < 2 && n / GRID != c / GRID) || grid[n].connected || !grid[n].valid ||
                absf(grid[n].floor - grid[c].floor) > 50)
                continue;
            grid[n].connected = 1;
            queue[end++] = n;
        }
    }
    spawn_count = 0;
    for (int i = 0; i < CELLS; i++)
        if (grid[i].connected)
            spawn_cells[spawn_count++] = i;
    if (spawn_count < 12)
        return 0;
    HsVec p = cell_position(seed);
    link->current.pos = (cXyz){p.x, p.y, p.z};
    link->old.pos = link->current.pos;
    link->speed = (cXyz){0, 0, 0};
    link->speedF = 0;
    hs.player = p;
    wwhd_log_int("arena connected floor samples", spawn_count);
    return 1;
}
static HsId spawn_enemy(int kind, int elite, HsVec *position) {
    (void)elite;
    if (!spawn_count)
        return HS_NONE;
    for (int tries = 0; tries < spawn_count; tries++) {
        int c = spawn_cells[spawn_cursor++ % (unsigned)spawn_count];
        HsVec p = cell_position(c);
        if (hs_distance2(p, hs.player) < 450 * 450)
            continue;
        int clear = 1;
        for (int j = 0; j < HS_ENEMIES; j++)
            if (hs.enemies[j].state != HS_FREE && hs_distance2(p, hs.enemies[j].pos) < 120 * 120)
                clear = 0;
        if (!clear)
            continue;
        if (kind == 0)
            p.y += 150;
        csXyz angle = {0, 0, (s16)(kind >= 3 ? 0xFF : 0)};
        u32 id = wwhd_fopAcM_create(profiles[kind], parameters[kind], (u32)&p, 0, (u32)&angle, 0, 0xFF, 0);
        if (id != HS_NONE) {
            *position = p;
            return id;
        }
        return HS_NONE;
    }
    return HS_NONE;
}
static const HsBackend backend = {spawn_enemy, observe, remove_actor, hs_combat_attack};

static void warp(u8 *p, const char *name, s16 point, s8 room, s8 layer) {
    u8 *n = p + 0x5140;
    memset(n, 0, 14);
    memcpy(n, name, 8);
    *(s16 *)(n + 8) = point;
    n[10] = (u8)room;
    n[11] = (u8)layer;
    n[13] = 0;
    n[12] = 1;
}
static void begin_run(u8 *p, fopAc_ac_c *link) {
    u8 *save = save_base();
    if (!save || slot() < 0 || slot() > 2)
        return;
    snapshot_slot = slot();
    memcpy(snapshot, save + 0x20, SAVE_BYTES);
    hs_session = 1;
    hs_combat_reset();
    hs_combat_provision();
    memset(children, 0, sizeof(children));
    spawn_cursor = 0;
    memcpy(return_stage, p + 0x5134, 8);
    return_point = *(s16 *)(p + 0x513C);
    return_room = link->current.roomNo;
    return_layer = (s8)p[0x513F];
    return_position = (HsVec){link->current.pos.x, link->current.pos.y, link->current.pos.z};
    return_angle = link->shape_angle.y;
    hs_start(&hs, ++launch_serial * 0x9e3779b9u);
    hs.phase = HS_LOADING;
    settled = 0;
    returning = retrying = 0;
    loading_time = 0;
    result_saved = 0;
    warp(p, HS_STAGE_NAME, 0, 0, -1);
    wwhd_log("entering Savage Labyrinth prototype");
}
static void exit_run(u8 *p) {
    hs_combat_stop((void *)*(u32 *)(p + 0x5B2C));
    hs_stop(&hs, &backend, HS_RETURNING);
    restore_save();
    returning = 1;
    loading_time = 0;
    warp(p, return_stage, return_point, return_room, return_layer);
}
static void retry_run(u8 *p) {
    hs_combat_stop((void *)*(u32 *)(p + 0x5B2C));
    hs_stop(&hs, &backend, HS_LOADING);
    retrying = 1;
    settled = 0;
    loading_time = 0;
    result_saved = 0;
    warp(p, HS_STAGE_NAME, 0, 0, -1);
}
static void record_result(void) {
    if (result_saved)
        return;
    result_saved = 1;
    if ((unsigned)hs.kills > hs_best_kills)
        hs_best_kills = (unsigned)hs.kills;
    if (hs.phase == HS_WIN)
        hs_wins++;
    records[0] = 0x48535731u;
    records[1] = 1;
    records[2] = hs_best_kills;
    records[3] = hs_wins;
    records[4] = records[0] ^ records[1] ^ records[2] ^ records[3];
    if (wwhd_file_write("records-v1.bin", records, sizeof(records)) != (int)sizeof(records))
        wwhd_log("could not persist run records");
}
void hs_action(int a) { action = a; }
static void input_filter(wwhd_input_state *s) {
    unsigned changed = s->buttons & ~previous_buttons;
    previous_buttons = s->buttons;
    pressed |= changed;
    if (hs.phase == HS_UPGRADE && !stick_latched && (s->lx < -.65f || s->lx > .65f)) {
        action = s->lx < 0 ? HS_ACTION_LEFT : HS_ACTION_RIGHT;
        stick_latched = 1;
    }
    if (s->lx > -.25f && s->lx < .25f)
        stick_latched = 0;
    if (hs_session || hs.phase == HS_MENU) {
        /* Movement, camera, roll and lock-on stay native. Items, the story pause
         * menu and manual sword attacks are disabled for the automatic loadout. */
        s->buttons &= 0x8000u | 0x0080u;
        if (hs_modal(hs.phase) || hs.phase == HS_LOADING || hs.phase == HS_RETURNING) {
            s->buttons = 0;
            s->lx = s->ly = s->rx = s->ry = 0;
        }
    } else if (hs_can_open && (previous_buttons & OPEN_CHORD) == OPEN_CHORD)
        s->buttons &= ~OPEN_CHORD;
}
static void clear_session(void) {
    hs_combat_reset();
    hs_session = 0;
    snapshot_slot = -1;
    hs.phase = HS_IDLE;
    returning = retrying = settled = 0;
    was_arena = 0;
    spawn_count = 0;
    action = 0;
    memset(children, 0, sizeof(children));
    pending_open = 0;
}
static void touch_claims(void) {
    int claim = 0;
    static int previous_claim = -1;
    if (hs.phase == HS_IDLE && hs_can_open && wwhd_config_bool("show_entry", 1))
        claim = 1;
    else if (hs.phase == HS_RUN)
        claim = 2;
    else if (hs.phase != HS_IDLE && hs.phase != HS_LOADING && hs.phase != HS_RETURNING)
        claim = 3;
    if (claim == previous_claim)
        return;
    previous_claim = claim;
    if (claim == 1)
        wwhd_touch_claim(0, 390, 288, 610, 336);
    else if (claim == 2)
        wwhd_touch_claim(0, 475, 288, 615, 336);
    else if (claim == 3)
        wwhd_touch_claim(0, -540, -255, 540, 255);
    else
        wwhd_touch_claim(0, 0, 0, 0, 0);
}
static void retire_children(void) {
    for (int i = 0; i < 128; i++)
        if (children[i].id) {
            actor_result = 0;
            int found = wwhd_fopAcM_SearchByID(children[i].parent, (u32)&actor_result);
            if (found)
                continue;
            actor_result = 0;
            found = wwhd_fopAcM_SearchByID(children[i].id, (u32)&actor_result);
            if (!found)
                children[i].id = 0;
            else if (actor_result)
                wwhd_fopAcM_delete_id(children[i].id);
        }
}
int hs_enemy_slot(void *actor) {
    if (!hs_session || !actor)
        return -1;
    u32 id = *(u32 *)((u8 *)actor + 4);
    for (int i = 0; i < HS_ENEMIES; i++)
        if (hs.enemies[i].state != HS_FREE && hs.enemies[i].id == id &&
            *(u16 *)((u8 *)actor + 0xE) == profiles[hs.enemies[i].kind] &&
            ((fopAc_ac_c *)actor)->mParameters == parameters[hs.enemies[i].kind])
            return i;
    return -1;
}
static int owned(void *actor) { return hs_enemy_slot(actor) >= 0; }
int hs_in_arena(void) { return hs_session && same_slot() && arena(wwhd_play_get()); }
static void step(fopAc_ac_c *link) {
    u8 *p = wwhd_play_get();
    if (!p || !link)
        return;
    int here = arena(p), transition = p[WWHD_PLAY_ENABLE_NEXT_STAGE_OFFSET];
    int busy = event_busy(p);
    hs_can_open = !hs_session && !busy && !transition && slot() >= 0 && slot() < 3 &&
                  !same((char *)p + 0x5134, "sea_T") && !same((char *)p + 0x5134, "Name") &&
                  !(*(u32 *)((u8 *)link + 0x6A70) &
                    (0x2u | 0x20u | 0x800u | 0x2000u | 0x10000u | 0x40000u | 0x100000u | 0x400000u));
    if (!registered) {
        registered = 1;
        wwhd_hud_register(hs_draw, WWHD_HUD_BOTH);
        wwhd_input_filter_register(input_filter);
        wwhd_touch_register(hs_touch);
        if (wwhd_file_read("records-v1.bin", records, sizeof(records)) == (int)sizeof(records) &&
            records[0] == 0x48535731u && records[1] == 1 &&
            records[4] == (records[0] ^ records[1] ^ records[2] ^ records[3])) {
            hs_best_kills = records[2];
            hs_wins = records[3];
        }
    }
    unsigned long long revision = wwhd_config_changed("open_menu");
    if (revision != option_revision) {
        option_revision = revision;
        pending_open = wwhd_config_bool("open_menu", 0) && !hs_session;
    }
    if (pending_open && hs_can_open) {
        action = HS_ACTION_OPEN;
        pending_open = 0;
    }
    if (hs_can_open && (previous_buttons & OPEN_CHORD) == OPEN_CHORD && (pressed & OPEN_CHORD))
        action = HS_ACTION_OPEN;
    unsigned keys = pressed;
    pressed = 0;
    if (keys & BTN_LEFT)
        action = HS_ACTION_LEFT;
    if (keys & BTN_RIGHT)
        action = HS_ACTION_RIGHT;
    if (keys & BTN_A)
        action = HS_ACTION_ACCEPT;
    if (keys & BTN_B)
        action = HS_ACTION_BACK;
    if (hs_session && (keys & BTN_PLUS) && hs.phase == HS_RUN) {
        hs.phase = HS_PAUSED;
        modal_age = 0;
        action = 0;
    }
    float dt = wwhd_logic_dt();
    if (!(dt > 0 && dt <= .1f))
        dt = 1.f / 30;
    modal_age += dt;
    if (!hs_session) {
        if (action == HS_ACTION_OPEN && hs_can_open) {
            hs.phase = HS_MENU;
            modal_age = 0;
        } else if (hs.phase == HS_MENU && action == HS_ACTION_BACK)
            hs.phase = HS_IDLE;
        else if (hs.phase == HS_MENU && action == HS_ACTION_ACCEPT && modal_age > .25f && hs_can_open)
            begin_run(p, link);
        action = 0;
        return;
    }
    if (!same_slot()) {
        clear_session();
        return;
    } /* never restore over another Quest Log */
    if (returning) {
        if (!transition && same((char *)p + 0x5134, return_stage)) {
            restore_save();
            link->current.pos = (cXyz){return_position.x, return_position.y, return_position.z};
            link->old.pos = link->current.pos;
            link->shape_angle.y = link->current.angle.y = return_angle;
            link->speed = (cXyz){0, 0, 0};
            link->speedF = 0;
            clear_session();
            wwhd_log("returned; story inventory and health restored");
        }
        action = 0;
        return;
    }
    if (!here) {
        if (was_arena) {
            restore_save();
            clear_session();
            return;
        }
        loading_time += dt;
        if (loading_time > 20 && !transition) {
            hs.error = "The arena could not be loaded. Check mod setup.";
            hs.phase = HS_ERROR;
            if (action == HS_ACTION_BACK || action == HS_ACTION_EXIT) {
                restore_save();
                clear_session();
            }
        }
        action = 0;
        return;
    }
    was_arena = 1;
    if (transition) {
        action = 0;
        return;
    }
    if (retrying) {
        hs_combat_reset();
        hs_start(&hs, ++launch_serial * 0x9e3779b9u);
        hs.phase = HS_LOADING;
        retrying = 0;
        memset(children, 0, sizeof(children));
    }
    if (!settled) {
        if (!scan_arena(link)) {
            hs_fail(&hs, &backend, "Arena floor validation failed. Return and check the log.");
            settled = 1;
        } else {
            hs.phase = HS_READY;
            settled = 1;
            modal_age = 0;
            wwhd_log("arena ready");
        }
    }
    hs.player = (HsVec){link->current.pos.x, link->current.pos.y, link->current.pos.z};
    if (action == HS_ACTION_EXIT || (action == HS_ACTION_BACK && (hs.phase == HS_WIN || hs.phase == HS_LOSE ||
                                                                  hs.phase == HS_ERROR || hs.phase == HS_READY))) {
        exit_run(p);
        action = 0;
        return;
    }
    if ((action == HS_ACTION_RETRY ||
         (action == HS_ACTION_ACCEPT && (hs.phase == HS_WIN || hs.phase == HS_LOSE || hs.phase == HS_ERROR))) &&
        modal_age > .35f) {
        retry_run(p);
        action = 0;
        return;
    }
    if (hs.phase == HS_UPGRADE) {
        if (action == HS_ACTION_LEFT)
            hs.selected = (hs.selected + 2) % 3;
        if (action == HS_ACTION_RIGHT)
            hs.selected = (hs.selected + 1) % 3;
        if (modal_age > .25f) {
            if (action == HS_ACTION_ACCEPT)
                hs_choose(&hs, hs.selected);
            else if (action >= HS_ACTION_CHOICE0 && action < HS_ACTION_CHOICE0 + 3)
                hs_choose(&hs, action - HS_ACTION_CHOICE0);
        }
    } else if ((hs.phase == HS_READY || hs.phase == HS_PAUSED) && modal_age > .25f &&
               (action == HS_ACTION_ACCEPT || (hs.phase == HS_PAUSED && action == HS_ACTION_BACK)))
        hs.phase = HS_RUN;
    action = 0;
    if (busy)
        return;
    hs_combat_step(link);
    int old_phase = hs.phase;
    hs_tick(&hs, &backend, dt);
    retire_children();
    if (hs.phase != old_phase)
        modal_age = 0;
    if (hs.phase == HS_WIN || hs.phase == HS_LOSE)
        record_result();
    /* Catch the arena's holes without touching native scene/save recovery. */
    if (hs.phase == HS_RUN && link->current.pos.y < -4500)
        hs_fail(&hs, &backend, "Link left the arena floor. Retry the run.");
}

WWHD_GAME_ORIGINAL(0x0240EBB0, s32, original_link, (void *));
WWHD_REPLACE(0x0240EBB0, s32, survivors_link, (void *actor)) {
    unsigned long long now = wwhd_logic_step();
    if (now != last_step) {
        last_step = now;
        step((fopAc_ac_c *)actor);
        touch_claims();
    }
    if (hs_session && arena(wwhd_play_get()) && hs_modal(hs.phase))
        return 1;
    return original_link(actor);
}
/* Pause only our live processes, not any of the game's ordinary enemies. */
#define ENEMY_EXEC(address, name)                                                                                      \
    WWHD_GAME_ORIGINAL(address, s32, original_##name, (void *));                                                       \
    WWHD_REPLACE(address, s32, survivors_##name, (void *a)) {                                                          \
        if (owned(a) && hs.phase != HS_RUN)                                                                            \
            return 1;                                                                                                  \
        return original_##name(a);                                                                                     \
    }
ENEMY_EXEC(0x0219AAE8, keese)
ENEMY_EXEC(0x0210DD3C, chuchu)
ENEMY_EXEC(0x02446374, miniblin)
ENEMY_EXEC(0x0209BD1C, bokoblin)
ENEMY_EXEC(0x021C9B1C, moblin)

/* Ordinary Bokoblin/Moblin weapons are child actors. Retire orphaned weapons
 * after their owner is gone, and keep pending children until lookup resolves. */
WWHD_GAME_ORIGINAL(0x025D5A20, u32, original_child, (u32, u32, u32, u32, s32, u32, u32, u32));
WWHD_REPLACE(0x025D5A20, u32, survivors_child,
             (u32 profile, u32 parent, u32 params, u32 pos, s32 room, u32 angle, u32 scale, u32 argument)) {
    int track = 0;
    for (int i = 0; i < HS_ENEMIES && hs_session; i++)
        if (hs.enemies[i].state != HS_FREE && hs.enemies[i].id == parent)
            track = 1;
    int free_slot = -1;
    if (track) {
        for (int i = 0; i < 128; i++)
            if (!children[i].id) {
                free_slot = i;
                break;
            }
        if (free_slot < 0)
            return HS_NONE;
    }
    u32 id = original_child(profile, parent, params, pos, room, angle, scale, argument);
    if (track && id != HS_NONE) {
        children[free_slot].id = id;
        children[free_slot].parent = parent;
    }
    return id;
}

WWHD_GAME_ORIGINAL(0x023F51D0, s32, original_damage, (void *, f32));
WWHD_REPLACE(0x023F51D0, s32, survivors_damage, (void *link, f32 amount)) {
    if (hs_session && arena(wwhd_play_get())) {
        if (amount < 0 && amount > -1000) {
            int d = (int)(-amount + .999f);
            hs_hurt(&hs, d);
        }
        return 1;
    }
    return original_damage(link, amount);
}
/* Even an external save request serializes the pre-run story payload. This
 * wraps the serializer, not the asynchronous writer, and restores live memory
 * before returning. The normal game serializer/slot format remains in charge. */
WWHD_GAME_ORIGINAL(0x025BA9FC, s32, original_serialize, (u32, u32, s32));
WWHD_REPLACE(0x025BA9FC, s32, survivors_serialize, (u32 self, u32 card, s32 target_slot)) {
    u8 *save = save_base();
    if (!hs_session || !save || !same_slot())
        return original_serialize(self, card, target_slot);
    memcpy(temporary_save, save + 0x20, SAVE_BYTES);
    memcpy(save + 0x20, snapshot, SAVE_BYTES);
    s32 result = original_serialize(self, card, target_slot);
    memcpy(save + 0x20, temporary_save, SAVE_BYTES);
    return result;
}
/* A loaded Quest Log supersedes the run, including when the same slot is
 * loaded. Do not overwrite that newly loaded data with our earlier snapshot. */
WWHD_HOOK(0x02721908, survivors_load_card, (u32 self, u32 selected, u32 reset)) {
    (void)self;
    (void)selected;
    (void)reset;
    clear_session();
}
