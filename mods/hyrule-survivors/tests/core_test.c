/* SPDX-License-Identifier: MPL-2.0 */
#include "core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int state, kind;
    HsVec pos;
} Native;
static Native native[512];
static unsigned next_id, peak, spawn_calls, remove_calls;
static int reject_spawn, delay_creation;
static HsVec spawn_at;
static HsId spawn(int kind, int elite, HsVec *pos) {
    (void)elite;
    spawn_calls++;
    if (reject_spawn)
        return HS_NONE;
    assert(next_id + 1 < 512);
    unsigned id = ++next_id;
    *pos = spawn_at;
    native[id] = (Native){delay_creation ? 1 : 2, kind, *pos};
    unsigned count = 0;
    for (unsigned i = 1; i <= next_id; i++)
        count += native[i].state != 0;
    if (count > peak)
        peak = count;
    return id;
}
static int observe(HsId id, int kind, HsVec *pos) {
    assert(id <= next_id && kind == native[id].kind);
    *pos = native[id].pos;
    return native[id].state;
}
static void remove_enemy(HsId id) {
    remove_calls++;
    /* Native delete cannot remove a still-creating actor. */
    if (native[id].state != 1)
        native[id].state = 0;
}
static void burst(HsVec p, int kind) {
    (void)p;
    (void)kind;
}
static const HsBackend backend = {spawn, observe, remove_enemy, burst};
static void reset(HsRun *r) {
    memset(native, 0, sizeof(native));
    next_id = peak = spawn_calls = remove_calls = 0;
    reject_spawn = delay_creation = 0;
    spawn_at = (HsVec){100, 0, 0};
    hs_start(r, 123);
    r->phase = HS_RUN;
}
static void ticks(HsRun *r, int n) {
    while (n--)
        hs_tick(r, &backend, .05f);
}
static void suppress_spawning(HsRun *r) { r->spawn_timer = 10000; }
static void enemy(HsRun *r, int i, int hp, HsVec pos) {
    spawn_at = pos;
    HsId id = spawn(0, 0, &pos);
    r->enemies[i] = (HsEnemy){
        .id = id, .generation = ++r->serial, .state = HS_ALIVE, .kind = 0, .hp = hp, .max_hp = hp, .pos = pos};
}

static void test_pause_and_damage(void) {
    HsRun r, copy;
    reset(&r);
    suppress_spawning(&r);
    for (int phase = HS_MENU; phase <= HS_ERROR; phase++) {
        if (phase == HS_RUN)
            continue;
        r.phase = phase;
        copy = r;
        ticks(&r, 20);
        assert(r.elapsed == copy.elapsed && r.wave_time == copy.wave_time);
        hs_hurt(&r, 8);
        assert(r.hp == copy.hp);
    }
    r.phase = HS_RUN;
    copy = r;
    hs_tick(&r, &backend, NAN);
    hs_tick(&r, &backend, -1);
    hs_tick(&r, &backend, 0);
    hs_tick(&r, &backend, 1);
    assert(memcmp(&r, &copy, sizeof(r)) == 0);
    hs_hurt(&r, 4);
    assert(r.hp == 24); /* opening grace */
    r.grace = 0;
    hs_hurt(&r, 4);
    assert(r.hp == 20);
    hs_hurt(&r, 4);
    assert(r.hp == 20);
    ticks(&r, 14);
    hs_hurt(&r, 100);
    assert(r.hp == 0 && r.phase == HS_LOSE);
    ticks(&r, 2);
    assert(r.phase == HS_LOSE);
}

static void test_pending_and_failure(void) {
    HsRun r;
    reset(&r);
    delay_creation = 1;
    ticks(&r, 1);
    assert(r.enemies[0].state == HS_PENDING);
    hs_stop(&r, &backend, HS_PAUSED);
    ticks(&r, 5);
    assert(r.enemies[0].state == HS_RETIRING && native[1].state == 1);
    native[1].state = 2;
    ticks(&r, 2);
    assert(r.enemies[0].state == HS_FREE && native[1].state == 0);
    reset(&r);
    delay_creation = 1;
    ticks(&r, 205);
    assert(r.phase == HS_ERROR && r.kills == 0 && peak <= HS_ENEMIES);
    reset(&r);
    ticks(&r, 2);
    native[1].state = 0;
    ticks(&r, 1);
    assert(r.phase == HS_ERROR && r.kills == 0);
    reset(&r);
    ticks(&r, 1);
    reject_spawn = 1;
    /* Fail after partial progress as well as when the first spawn fails. */
    for (int i = 0; i < 500 && r.phase == HS_RUN; i++)
        ticks(&r, 1);
    assert(r.phase == HS_ERROR && r.spawned == 1);
}

static void test_pool_and_rewards(void) {
    HsRun r;
    reset(&r);
    suppress_spawning(&r);
    for (int i = 0; i < HS_ORBS; i++)
        r.orbs[i] = (HsOrb){1, {1000, 0, 0}};
    enemy(&r, 0, 1, (HsVec){100, 0, 0});
    ticks(&r, 1);
    int amount = 0;
    for (int i = 0; i < HS_ORBS; i++)
        amount += r.orbs[i].amount;
    assert(r.kills == 1 && amount == HS_ORBS + 2 && r.damage[HS_SWORD] == 1);
    ticks(&r, 5);
    assert(r.kills == 1 && r.defeated == 1); /* no duplicate reward */
    reset(&r);
    suppress_spawning(&r);
    enemy(&r, 0, 20, (HsVec){700, 0, 0});
    native[1].state = 3;
    ticks(&r, 3);
    assert(r.kills == 1);
    reset(&r);
    spawn_at = (HsVec){1000, 0, 0};
    r.wave = 4;
    ticks(&r, 500);
    assert(peak == HS_ENEMIES && r.spawned == HS_ENEMIES);
    hs_stop(&r, &backend, HS_LOSE);
    ticks(&r, 2);
    for (unsigned i = 1; i <= next_id; i++)
        assert(!native[i].state);
}

static void test_upgrades(void) {
    HsRun a, b;
    reset(&a);
    suppress_spawning(&a);
    a.xp = 8;
    b = a;
    ticks(&a, 1);
    ticks(&b, 1);
    assert(a.phase == HS_UPGRADE && a.level == 2 && a.xp == 0);
    assert(memcmp(a.choices, b.choices, sizeof(a.choices)) == 0);
    int bow = -1;
    for (int i = 0; i < 3; i++) {
        if (a.choices[i] == HS_UP_BOW)
            bow = i;
        for (int j = 0; j < i; j++)
            assert(a.choices[i] != a.choices[j]);
    }
    assert(bow >= 0);
    hs_choose(&a, -1);
    assert(a.phase == HS_UPGRADE);
    hs_choose(&a, bow);
    assert(a.weapon[HS_BOW] == 1 && a.phase == HS_RUN);
    a.phase = HS_UPGRADE;
    a.choices[0] = HS_UP_HEART;
    a.hp = 2;
    hs_choose(&a, 0);
    assert(a.hp == 6 && a.max_hp == 28);
    a.phase = HS_UPGRADE;
    a.choices[0] = HS_UP_HEAL;
    a.hp = 27;
    hs_choose(&a, 0);
    assert(a.hp == 28);
    for (int i = 0; i < 4; i++)
        a.weapon[i] = 5;
    for (int i = 0; i < 5; i++)
        a.passive[i] = 5;
    a.xp = 100;
    ticks(&a, 1);
    assert(a.phase == HS_UPGRADE);
    for (int i = 0; i < 3; i++)
        assert(a.choices[i] == HS_UP_HEAL);
}

static void test_projectiles(void) {
    HsRun r;
    reset(&r);
    suppress_spawning(&r);
    r.weapon[0] = 0;
    r.weapon[1] = 1;
    r.cooldown[1] = 100;
    enemy(&r, 0, 20, (HsVec){100, 0, 0});
    /* A fast arrow crosses the whole target between updates. */
    r.projectiles[0] = (HsProjectile){.active = 1,
                                      .kind = HS_BOW,
                                      .damage = 6,
                                      .pos = {0, 40, 0},
                                      .velocity = {2000, 0, 0},
                                      .lifetime = 2,
                                      .radius = 20};
    hs_tick(&r, &backend, .1f);
    assert(r.enemies[0].hp == 14 && !r.projectiles[0].active);
    /* Returning boomerang can hit the same actor again on its return leg. */
    r.player = (HsVec){-1000, 0, 0};
    r.projectiles[0] = (HsProjectile){.active = 1,
                                      .kind = HS_BOOMERANG,
                                      .damage = 4,
                                      .pos = {145, 40, 0},
                                      .age = 1.09f,
                                      .lifetime = 2.2f,
                                      .radius = 60,
                                      .hit_mask = 1};
    r.projectiles[0].generations[0] = r.enemies[0].generation;
    ticks(&r, 1);
    assert(r.enemies[0].hp == 10);
    /* Reused slots are new targets even when an old projectile remembers them. */
    r.enemies[0].generation++;
    r.projectiles[0].pos = (HsVec){145, 40, 0};
    ticks(&r, 1);
    assert(r.enemies[0].hp == 6);
    r.projectiles[0] = (HsProjectile){.active = 1,
                                      .kind = HS_BOMB,
                                      .damage = 12,
                                      .pos = {100, 50, 0},
                                      .origin = {100, 50, 0},
                                      .age = 1.19f,
                                      .lifetime = 1.2f,
                                      .radius = 260};
    enemy(&r, 1, 10, (HsVec){200, 0, 0});
    ticks(&r, 1);
    assert(r.kills == 2 && r.damage[HS_BOMB] == 16);
}

static void test_complete_run(void) {
    HsRun r;
    reset(&r);
    int expected = 0, seen[6] = {0}, elite = 0;
    for (int w = 0; w < HS_WAVES; w++)
        expected += hs_wave_quota(w);
    for (int frame = 0; frame < 20000 && r.phase != HS_WIN; frame++) {
        assert(r.phase != HS_ERROR && r.phase != HS_LOSE);
        if (r.phase == HS_UPGRADE)
            hs_choose(&r, 0);
        for (int w = 0; w < HS_WEAPONS; w++)
            r.weapon[w] = 0;
        for (int i = 0; i < HS_ENEMIES; i++)
            if (r.enemies[i].state == HS_ALIVE) {
                seen[r.wave]++;
                if (r.enemies[i].elite) {
                    elite++;
                    assert(r.wave == 5 && r.enemies[i].max_hp == 160);
                }
                native[r.enemies[i].id].state = 3;
            }
        ticks(&r, 1);
    }
    assert(r.phase == HS_WIN && r.kills == expected && expected == 113 && elite == 1);
    for (int w = 0; w < 6; w++)
        assert(seen[w] == hs_wave_quota(w));
    assert(peak <= HS_ENEMIES && r.total_xp > 0);
    int kills = r.kills;
    float elapsed = r.elapsed;
    ticks(&r, 100);
    assert(r.kills == kills && r.elapsed == elapsed);
    hs_start(&r, 123);
    assert(r.phase == HS_READY && r.kills == 0 && r.weapon[1] == 0 && r.hp == 24);
}
int main(void) {
    test_pause_and_damage();
    test_pending_and_failure();
    test_pool_and_rewards();
    test_upgrades();
    test_projectiles();
    test_complete_run();
    puts("Core: pause, damage, native lifetime, capacity, EXP, upgrades, projectiles, six-wave victory and restart "
         "passed");
}
