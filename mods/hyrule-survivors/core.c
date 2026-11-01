/* SPDX-License-Identifier: MPL-2.0
 * Engine-independent run rules. All capacities include pending native actors.
 * Time is simulation seconds; neither rendering nor a modal advances combat. */
#include "core.h"
const char *const hs_weapon_names[] = {"Spin Attack", "Hero's Bow", "Boomerang", "Bombs"};
const char *const hs_wave_names[] = {"Wings in the dark", "Emerald tide", "Little devils",
                                     "Bokoblin assault",  "Moblin guard", "The labyrinth champion"};
const char *const hs_upgrade_names[] = {"Spin Attack", "Hero's Bow", "Boomerang", "Bombs",           "Power",
                                        "Haste",       "Reach",      "Magnet",    "Heart Container", "Red Potion"};
const char *const hs_upgrade_details[] = {"Link's native spin attack. More damage per rank.",
                                          "Unlock auto-aimed arrows. Extra ranks add damage.",
                                          "Unlock a returning blade. Uses native flight and collision.",
                                          "Unlock timed bombs. Explosions hit every nearby enemy.",
                                          "+20% damage for every weapon.",
                                          "Shorter cooldowns for every weapon.",
                                          "+15% sword reach and explosion radius.",
                                          "Collect EXP from farther away.",
                                          "+1 maximum heart and restore 1 heart.",
                                          "Restore 3 hearts."};
static const int quotas[] = {12, 16, 20, 24, 28, 13};
int hs_wave_quota(int w) { return w >= 0 && w < HS_WAVES ? quotas[w] : 0; }
int hs_xp_needed(int level) { return 8 + (level - 1) * 5; }
int hs_modal(int p) { return p != HS_RUN && p != HS_LOADING && p != HS_IDLE && p != HS_RETURNING; }
float hs_distance2(HsVec a, HsVec b) {
    float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return x * x + y * y + z * z;
}
static float root(float x) {
    if (x <= 0)
        return 0;
    float v = x > 1 ? x : 1;
    for (int n = 0; n < 16; n++)
        v = .5f * (v + x / v);
    return v;
}
static unsigned random_next(HsRun *r) {
    unsigned x = r->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return r->rng = x;
}
static void zero(void *p, unsigned n) {
    unsigned char *b = p;
    while (n--)
        *b++ = 0;
}
void hs_start(HsRun *r, unsigned seed) {
    zero(r, sizeof(*r));
    r->rng = seed ? seed : 0x73555256u;
    r->phase = HS_READY;
    r->hp = r->max_hp = 24;
    r->level = 1;
    r->weapon[HS_SWORD] = 1;
    r->grace = 2;
}
static int occupied(HsRun *r) {
    int n = 0;
    for (int i = 0; i < HS_ENEMIES; i++)
        n += r->enemies[i].state != HS_FREE;
    return n;
}
static void orb(HsRun *r, HsVec pos, int amount) {
    int nearest = 0;
    float best = 1e30f;
    for (int i = 0; i < HS_ORBS; i++) {
        if (!r->orbs[i].amount) {
            r->orbs[i] = (HsOrb){amount, pos};
            return;
        }
        float d = hs_distance2(pos, r->orbs[i].pos);
        if (d < best) {
            best = d;
            nearest = i;
        }
    }
    r->orbs[nearest].amount += amount; /* preserve EXP when the visual pool is full */
}
static void collect_all(HsRun *r) {
    for (int i = 0; i < HS_ORBS; i++) {
        r->xp += r->orbs[i].amount;
        r->total_xp += r->orbs[i].amount;
        r->orbs[i].amount = 0;
    }
}
static void defeat(HsRun *r, const HsBackend *b, int i) {
    HsEnemy *e = &r->enemies[i];
    if (e->state != HS_ALIVE)
        return;
    r->kills++;
    r->defeated++;
    orb(r, e->pos, e->elite ? 20 : 2 + e->kind);
    e->state = HS_RETIRING;
    e->age = 0;
    b->remove(e->id);
}
void hs_native_hit(HsRun *r, HsId id, int damage, int weapon) {
    if (r->phase != HS_RUN || damage <= 0 || weapon < 0 || weapon >= HS_WEAPONS)
        return;
    for (int i = 0; i < HS_ENEMIES; i++) {
        HsEnemy *e = &r->enemies[i];
        if (e->id != id || (e->state != HS_ALIVE && e->state != HS_PENDING) || e->hp <= 0)
            continue;
        /* A native contact proves that a pending creation has completed. */
        if (e->state == HS_PENDING) {
            e->state = HS_ALIVE;
            e->age = 0;
        }
        int actual = damage < e->hp ? damage : e->hp;
        e->hp -= actual;
        r->damage[weapon] += (unsigned)actual;
        return;
    }
}
void hs_stop(HsRun *r, const HsBackend *b, int phase) {
    r->phase = phase;
    for (int i = 0; i < HS_ENEMIES; i++)
        if (r->enemies[i].state != HS_FREE) {
            r->enemies[i].state = HS_RETIRING;
            b->remove(r->enemies[i].id);
        }
}
void hs_fail(HsRun *r, const HsBackend *b, const char *msg) {
    r->error = msg;
    hs_stop(r, b, HS_ERROR);
}
void hs_hurt(HsRun *r, int damage) {
    if (r->phase != HS_RUN || r->grace > 0 || damage <= 0)
        return;
    r->hp -= damage;
    r->grace = .65f;
    if (r->hp <= 0) {
        r->hp = 0;
        r->phase = HS_LOSE;
    }
}
static int eligible(HsRun *r, int u) {
    if (u < 4)
        return r->weapon[u] < 5;
    if (u == HS_UP_HEAL)
        return r->hp < r->max_hp;
    return r->passive[u - 4] < 5;
}
static void offer(HsRun *r) {
    int pool[HS_UP_COUNT], count = 0;
    for (int u = 0; u < HS_UP_COUNT; u++)
        if (eligible(r, u))
            pool[count++] = u;
    for (int i = 0; i < 3; i++) {
        if (count) {
            int j = (int)(random_next(r) % (unsigned)count);
            r->choices[i] = pool[j];
            pool[j] = pool[--count];
        } else
            r->choices[i] = HS_UP_HEAL;
    }
    /* Guarantee access to all four weapons over the first three level-ups. */
    if (r->level >= 2 && r->level <= 4) {
        int u = r->level - 1, found = 0;
        for (int i = 0; i < 3; i++)
            found |= r->choices[i] == u;
        if (!r->weapon[u] && !found)
            r->choices[0] = u;
    }
    r->selected = 0;
    r->phase = HS_UPGRADE;
}
void hs_choose(HsRun *r, int choice) {
    if (r->phase != HS_UPGRADE || choice < 0 || choice >= 3)
        return;
    int u = r->choices[choice];
    if (u < 4) {
        if (r->weapon[u] < 5)
            r->weapon[u]++;
        r->cooldown[u] = 0;
    } else if (u == HS_UP_HEAL)
        r->hp += 12;
    else if (r->passive[u - 4] < 5) {
        r->passive[u - 4]++;
        if (u == HS_UP_HEART) {
            r->max_hp += 4;
            r->hp += 4;
        }
    }
    if (r->hp > r->max_hp)
        r->hp = r->max_hp;
    r->phase = HS_RUN;
    r->grace = 1;
}
static int damage_for(HsRun *r, int w) {
    const int base[] = {5, 6, 4, 12};
    return (base[w] + (r->weapon[w] - 1) * 3) * (5 + r->passive[0]) / 5;
}
static float reach(HsRun *r) { return 1 + .15f * r->passive[2]; }
static int nearest(HsRun *r, float range) {
    int result = -1;
    float best = range * range;
    for (int i = 0; i < HS_ENEMIES; i++)
        if (r->enemies[i].state == HS_ALIVE) {
            float d = hs_distance2(r->player, r->enemies[i].pos);
            if (d < best) {
                best = d;
                result = i;
            }
        }
    return result;
}
void hs_tick(HsRun *r, const HsBackend *b, float dt) {
    if (!(dt > 0 && dt <= .1f))
        return;
    /* Retire even on result screens, but never reuse an unconfirmed process. */
    for (int i = 0; i < HS_ENEMIES; i++) {
        HsEnemy *e = &r->enemies[i];
        if (e->state == HS_FREE)
            continue;
        int observed = b->observe(e->id, e->kind, &e->pos);
        if (e->state == HS_RETIRING) {
            if (!observed)
                e->state = HS_FREE;
            else {
                if (observed != 1)
                    b->remove(e->id);
                e->age += dt;
                if (r->phase == HS_RUN && e->age > 10) {
                    hs_fail(r, b, "An enemy could not be removed. Retry the run.");
                    return;
                }
            }
            continue;
        }
        if (r->phase != HS_RUN)
            continue;
        e->age += dt;
        if (e->state == HS_PENDING) {
            if (observed >= 2) {
                e->state = HS_ALIVE;
                e->age = 0;
            } else if (!observed || e->age > 10) {
                hs_fail(r, b, "An enemy could not be created. Retry the run.");
                return;
            }
        } else if (observed == 3 || e->hp <= 0)
            defeat(r, b, i);
        else if (!observed) {
            hs_fail(r, b, "An enemy disappeared before its defeat. Retry the run.");
            return;
        }
    }
    if (r->phase == HS_LOSE) {
        hs_stop(r, b, HS_LOSE);
        return;
    }
    if (r->phase != HS_RUN)
        return;
    r->elapsed += dt;
    r->wave_time += dt;
    if (r->grace > 0)
        r->grace -= dt;
    for (int i = 0; i < HS_ORBS; i++)
        if (r->orbs[i].amount) {
            HsOrb *o = &r->orbs[i];
            float d = root(hs_distance2(o->pos, r->player));
            if (d < 100) {
                r->xp += o->amount;
                r->total_xp += o->amount;
                o->amount = 0;
            } else if (d < 350 + 150 * r->passive[3]) {
                float step = 650 * dt / d;
                o->pos.x += (r->player.x - o->pos.x) * step;
                o->pos.y += (r->player.y - o->pos.y) * step;
                o->pos.z += (r->player.z - o->pos.z) * step;
            }
        }
    if (r->defeated == hs_wave_quota(r->wave) && !occupied(r)) {
        collect_all(r);
        if (r->wave == HS_WAVES - 1) {
            hs_stop(r, b, HS_WIN);
            return;
        }
        r->wave++;
        r->spawned = r->defeated = 0;
        r->wave_time = 0;
        r->spawn_timer = 2;
        r->hp += 4;
        if (r->hp > r->max_hp)
            r->hp = r->max_hp;
    }
    if (r->xp >= hs_xp_needed(r->level)) {
        r->xp -= hs_xp_needed(r->level);
        r->level++;
        offer(r);
        return;
    }
    r->spawn_timer -= dt;
    if (r->spawn_timer <= 0 && r->spawned < hs_wave_quota(r->wave) && occupied(r) < HS_ENEMIES) {
        int slot = 0;
        while (slot < HS_ENEMIES && r->enemies[slot].state != HS_FREE)
            slot++;
        int kind = r->wave == 0   ? 0
                   : r->wave == 1 ? 1
                   : r->wave == 2 ? 2
                   : r->wave == 3 ? 3
                   : r->wave == 4 ? 4
                                  : r->spawned % 5;
        int elite = r->wave == HS_WAVES - 1 && r->spawned == hs_wave_quota(r->wave) - 1;
        if (elite)
            kind = 4;
        HsVec pos = r->player;
        HsId id = b->spawn(kind, elite, &pos);
        if (id != HS_NONE) {
            int hp = elite ? 160 : 8 + kind * 5 + r->wave * 2;
            r->enemies[slot] = (HsEnemy){.id = id,
                                         .generation = ++r->serial,
                                         .state = HS_PENDING,
                                         .kind = kind,
                                         .elite = elite,
                                         .hp = hp,
                                         .max_hp = hp,
                                         .pos = pos};
            r->spawned++;
            r->spawn_timer = .8f;
            r->spawn_failures = 0;
        } else {
            r->spawn_timer = .5f;
            if (++r->spawn_failures >= 40) {
                hs_fail(r, b, "No reachable enemy spawn was found.");
                return;
            }
        }
    }
    for (int w = 0; w < HS_WEAPONS; w++)
        r->cooldown[w] -= dt;
    /* Round-robin prevents the short spin cooldown from starving item attacks.
     * The adapter serializes Link's native animation state, including recovery. */
    for (int n = 0; n < HS_WEAPONS; n++) {
        int w = (r->next_weapon + n) % HS_WEAPONS;
        if (!r->weapon[w] || r->cooldown[w] > 0)
            continue;
        float range = w == HS_SWORD ? 180 * reach(r) : w == HS_BOMB ? 650 : 1500;
        int target = nearest(r, range);
        if (target < 0 || !b->attack(w, r->enemies[target].id, damage_for(r, w), reach(r)))
            continue;
        const float base[] = {.85f, 1.15f, 2.6f, 3.4f};
        r->cooldown[w] = base[w] / (1 + .12f * r->passive[1] + .06f * (r->weapon[w] - 1));
        r->next_weapon = (w + 1) % HS_WEAPONS;
        break;
    }
    /* Prevent unreachable/frozen enemies from trapping an endless prototype run. */
    for (int i = 0; i < HS_ENEMIES; i++)
        if (r->enemies[i].state == HS_ALIVE && r->enemies[i].age > 180) {
            hs_fail(r, b, "An enemy stayed unreachable. Retry the run.");
            return;
        }
}
