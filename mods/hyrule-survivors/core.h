/* SPDX-License-Identifier: MPL-2.0 */
#ifndef HS_CORE_H
#define HS_CORE_H
#define HS_ENEMIES 24
#define HS_PROJECTILES 48
#define HS_ORBS 64
#define HS_EFFECTS 32
#define HS_WEAPONS 4
#define HS_WAVES 6
#define HS_NONE 0xffffffffu
typedef unsigned int HsId;
typedef struct {
    float x, y, z;
} HsVec;
enum HsPhase {
    HS_IDLE,
    HS_MENU,
    HS_LOADING,
    HS_READY,
    HS_RUN,
    HS_UPGRADE,
    HS_PAUSED,
    HS_WIN,
    HS_LOSE,
    HS_ERROR,
    HS_RETURNING
};
enum HsWeapon { HS_SWORD, HS_BOW, HS_BOOMERANG, HS_BOMB };
enum HsUpgrade {
    HS_UP_SWORD,
    HS_UP_BOW,
    HS_UP_BOOMERANG,
    HS_UP_BOMB,
    HS_UP_POWER,
    HS_UP_HASTE,
    HS_UP_REACH,
    HS_UP_MAGNET,
    HS_UP_HEART,
    HS_UP_HEAL,
    HS_UP_COUNT
};
enum HsEnemyState { HS_FREE, HS_PENDING, HS_ALIVE, HS_RETIRING };
typedef struct {
    HsId id;
    unsigned generation;
    int state, kind, elite, hp, max_hp;
    float age;
    HsVec pos;
} HsEnemy;
typedef struct {
    int active, kind, damage;
    HsVec pos, velocity, origin;
    float age, lifetime, radius;
    unsigned hit_mask;
    unsigned generations[HS_ENEMIES];
} HsProjectile;
typedef struct {
    int amount;
    HsVec pos;
} HsOrb;
typedef struct {
    float remaining, duration, radius;
    int kind;
    HsVec pos;
} HsEffect;
typedef struct {
    int phase, wave, spawned, defeated, kills, level, xp, total_xp, hp, max_hp, spawn_failures;
    int weapon[HS_WEAPONS], passive[5], choices[3], selected;
    unsigned rng, serial;
    float elapsed, wave_time, spawn_timer, cooldown[HS_WEAPONS], grace;
    unsigned damage[HS_WEAPONS];
    int last_damage_weapon;
    HsVec player;
    HsEnemy enemies[HS_ENEMIES];
    HsProjectile projectiles[HS_PROJECTILES];
    HsOrb orbs[HS_ORBS];
    HsEffect effects[HS_EFFECTS];
    const char *error;
} HsRun;
/* The adapter owns native process lifetime. observe: 0 gone, 1 creating, 2 alive,
 * 3 native defeat. Removal must be retried while a process is still creating. */
typedef struct {
    HsId (*spawn)(int kind, int elite, HsVec *position);
    int (*observe)(HsId id, int kind, HsVec *position);
    void (*remove)(HsId id);
    void (*burst)(HsVec position, int kind);
} HsBackend;
extern const char *const hs_weapon_names[HS_WEAPONS];
extern const char *const hs_upgrade_names[HS_UP_COUNT];
extern const char *const hs_upgrade_details[HS_UP_COUNT];
extern const char *const hs_wave_names[HS_WAVES];
int hs_wave_quota(int wave);
int hs_xp_needed(int level);
int hs_modal(int phase);
float hs_distance2(HsVec a, HsVec b);
void hs_start(HsRun *run, unsigned seed);
void hs_tick(HsRun *run, const HsBackend *backend, float dt);
void hs_choose(HsRun *run, int choice);
void hs_hurt(HsRun *run, int damage);
void hs_stop(HsRun *run, const HsBackend *backend, int phase);
void hs_fail(HsRun *run, const HsBackend *backend, const char *message);
#endif
