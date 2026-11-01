/* SPDX-License-Identifier: MPL-2.0
 * Engine-independent run rules. All capacities include pending native actors.
 * Time is simulation seconds; neither rendering nor a modal advances combat. */
#include "core.h"
const char *const hs_weapon_names[] = {"Spirit Sword", "Hero's Bow", "Boomerang", "Bombs"};
const char *const hs_wave_names[] = {"Wings in the dark", "Emerald tide", "Little devils",
                                     "Bokoblin assault",  "Moblin guard", "The labyrinth champion"};
const char *const hs_upgrade_names[] = {"Spirit Sword", "Hero's Bow", "Boomerang", "Bombs",           "Power",
                                        "Haste",        "Reach",      "Magnet",    "Heart Container", "Red Potion"};
const char *const hs_upgrade_details[] = {"A sweeping pulse around Link. More damage per rank.",
                                          "Unlock auto-aimed arrows. Extra ranks add damage and piercing.",
                                          "Unlock a returning blade. Hits enemies on both journeys.",
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
static void effect(HsRun *r, HsVec p, int kind, float radius) {
    int slot = -1;
    float shortest = 100;
    for (int i = 0; i < HS_EFFECTS; i++)
        if (r->effects[i].remaining < shortest) {
            slot = i;
            shortest = r->effects[i].remaining;
        }
    if (slot >= 0)
        r->effects[slot] = (HsEffect){.remaining = .35f, .duration = .35f, .radius = radius, .kind = kind, .pos = p};
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
    effect(r, e->pos, 4, 90);
    b->burst(e->pos, 4);
    e->state = HS_RETIRING;
    e->age = 0;
    b->remove(e->id);
}
static void hit(HsRun *r, const HsBackend *b, int i, int damage, int weapon) {
    HsEnemy *e = &r->enemies[i];
    if (e->state != HS_ALIVE || damage <= 0)
        return;
    int actual = damage < e->hp ? damage : e->hp;
    r->damage[weapon] += (unsigned)actual;
    e->hp -= damage;
    effect(r, e->pos, weapon, 35);
    if (e->hp <= 0)
        defeat(r, b, i);
}
void hs_stop(HsRun *r, const HsBackend *b, int phase) {
    r->phase = phase;
    for (int i = 0; i < HS_PROJECTILES; i++)
        r->projectiles[i].active = 0;
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
static void shoot(HsRun *r, int weapon, int target) {
    int slot = -1;
    for (int i = 0; i < HS_PROJECTILES; i++)
        if (!r->projectiles[i].active) {
            slot = i;
            break;
        }
    if (slot < 0)
        return;
    HsProjectile *p = &r->projectiles[slot];
    zero(p, sizeof(*p));
    p->active = 1;
    p->kind = weapon;
    p->damage = damage_for(r, weapon);
    p->pos = r->player;
    p->pos.y += 50;
    p->origin = p->pos;
    HsVec aim = r->enemies[target].pos;
    aim.y += 40;
    HsVec d = {aim.x - p->pos.x, aim.y - p->pos.y, aim.z - p->pos.z};
    float length = root(d.x * d.x + d.y * d.y + d.z * d.z);
    if (length < 1)
        length = 1;
    float speed = weapon == HS_BOW ? 1500 : weapon == HS_BOOMERANG ? 800 : 0;
    p->velocity = (HsVec){speed * d.x / length, speed * d.y / length, speed * d.z / length};
    p->lifetime = weapon == HS_BOW ? 1.6f : weapon == HS_BOOMERANG ? 2.2f : 1.2f;
    p->radius = weapon == HS_BOMB ? 260 * reach(r) : 60;
    if (weapon == HS_BOMB) {
        p->velocity = (HsVec){(aim.x - p->pos.x) / 1.2f, 0, (aim.z - p->pos.z) / 1.2f};
    }
}
static float segment_distance2(HsVec p, HsVec a, HsVec b) {
    HsVec d = {b.x - a.x, b.y - a.y, b.z - a.z};
    float length = d.x * d.x + d.y * d.y + d.z * d.z;
    float t = length > 0 ? ((p.x - a.x) * d.x + (p.y - a.y) * d.y + (p.z - a.z) * d.z) / length : 0;
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    HsVec q = {a.x + t * d.x, a.y + t * d.y, a.z + t * d.z};
    return hs_distance2(p, q);
}
static void projectiles(HsRun *r, const HsBackend *b, float dt) {
    for (int j = 0; j < HS_PROJECTILES; j++) {
        HsProjectile *p = &r->projectiles[j];
        if (!p->active)
            continue;
        HsVec old = p->pos;
        float before = p->age;
        p->age += dt;
        if (p->kind == HS_BOOMERANG && p->age >= 1.1f) {
            if (before < 1.1f)
                p->hit_mask = 0;
            HsVec target = r->player;
            target.y += 50;
            float d = root(hs_distance2(p->pos, target));
            if (d < 40) {
                p->active = 0;
                continue;
            }
            p->velocity = (HsVec){900 * (target.x - p->pos.x) / d, 900 * (target.y - p->pos.y) / d,
                                  900 * (target.z - p->pos.z) / d};
        }
        p->pos.x += p->velocity.x * dt;
        p->pos.y += p->velocity.y * dt;
        p->pos.z += p->velocity.z * dt;
        if (p->kind == HS_BOMB) {
            float t = p->age / 1.2f;
            p->pos.y = p->origin.y + 300 * t * (1 - t);
            if (p->age >= p->lifetime) {
                p->pos.y = p->origin.y;
                effect(r, p->pos, HS_BOMB, p->radius);
                b->burst(p->pos, HS_BOMB);
                for (int i = 0; i < HS_ENEMIES; i++)
                    if (hs_distance2(p->pos, r->enemies[i].pos) < p->radius * p->radius)
                        hit(r, b, i, p->damage, p->kind);
                p->active = 0;
            }
            continue;
        }
        for (int i = 0; i < HS_ENEMIES && p->active; i++) {
            HsEnemy *e = &r->enemies[i];
            if (e->state != HS_ALIVE)
                continue;
            if ((p->hit_mask & (1u << i)) && p->generations[i] == e->generation)
                continue;
            HsVec center = e->pos;
            center.y += 40;
            if (segment_distance2(center, old, p->pos) > p->radius * p->radius)
                continue;
            p->hit_mask |= 1u << i;
            p->generations[i] = e->generation;
            hit(r, b, i, p->damage, p->kind);
            if (p->kind == HS_BOW) {
                int hits = 0;
                for (int k = 0; k < HS_ENEMIES; k++)
                    hits += (p->hit_mask >> k) & 1u;
                if (hits >= 1 + (r->weapon[HS_BOW] - 1) / 2)
                    p->active = 0;
            }
        }
        if (p->age >= p->lifetime)
            p->active = 0;
    }
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
        } else if (observed == 3)
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
    for (int i = 0; i < HS_EFFECTS; i++)
        if (r->effects[i].remaining > 0)
            r->effects[i].remaining -= dt;
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
        if (r->weapon[w]) {
            r->cooldown[w] -= dt;
            if (r->cooldown[w] > 0)
                continue;
            float radius = 230 * reach(r);
            int target = nearest(r, w == HS_SWORD ? radius : 1500);
            if (target < 0)
                continue;
            if (w == HS_SWORD) {
                effect(r, r->player, w, radius);
                for (int i = 0; i < HS_ENEMIES; i++)
                    if (hs_distance2(r->player, r->enemies[i].pos) < radius * radius)
                        hit(r, b, i, damage_for(r, w), w);
            } else
                shoot(r, w, target);
            const float base[] = {.85f, 1.15f, 2.6f, 3.4f};
            r->cooldown[w] = base[w] / (1 + .12f * r->passive[1] + .06f * (r->weapon[w] - 1));
        }
    projectiles(r, b, dt);
    /* Prevent unreachable/frozen enemies from trapping an endless prototype run. */
    for (int i = 0; i < HS_ENEMIES; i++)
        if (r->enemies[i].state == HS_ALIVE && r->enemies[i].age > 180) {
            hs_fail(r, b, "An enemy stayed unreachable. Retry the run.");
            return;
        }
}
