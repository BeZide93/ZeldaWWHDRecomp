/* SPDX-License-Identifier: MPL-2.0
 * Vector HUD: no packaged textures. Screen-projected spirit weapons are part
 * of the prototype; native enemy models/animations remain in the 3D world. */
#include "game.h"
static wwhd_hud_element element;
static char strings[48][160];
static int string_count;
static f32 world[3], projected[2];
static const u32 gold = 0xEBC779FF, ink = 0xF1F4F3FF, muted = 0xA3B7BAFF, teal = 0x52D5B4FF;
static const u32 weapon_colors[] = {0x8DEBD5FF, 0xFFE5A2FF, 0x8AC3FFFF, 0xFF9B70FF};
static unsigned length(const char *s) {
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}
static char *append(char *d, const char *s) {
    while (*s)
        *d++ = *s++;
    *d = 0;
    return d;
}
static char *number(char *d, unsigned n) {
    char buf[12];
    int k = 0;
    do {
        buf[k++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (k)
        *d++ = buf[--k];
    *d = 0;
    return d;
}
static char *line(void) {
    if (string_count >= 48)
        return strings[47];
    strings[string_count][0] = 0;
    return strings[string_count++];
}
static const char *stat(const char *name, unsigned value, const char *suffix) {
    char *s = line();
    char *p = append(s, name);
    p = number(p, value);
    append(p, suffix);
    return s;
}
static void box(u32 list, float x, float y, float w, float h, u32 color, int outline) {
    element = (wwhd_hud_element){.kind = outline ? WWHD_HUD_RECT_OUTLINE : WWHD_HUD_RECT,
                                 .anchor = WWHD_HUD_CENTER,
                                 .x = x,
                                 .y = y,
                                 .w = w,
                                 .h = h,
                                 .thickness = outline ? 2 : 1,
                                 .u1 = 1,
                                 .v1 = 1,
                                 .rgba = color};
    wwhd_hud_emit(list, &element);
}
static void text(u32 list, float x, float y, float size, u32 color, const char *s) {
    element = (wwhd_hud_element){.kind = WWHD_HUD_TEXT,
                                 .anchor = WWHD_HUD_CENTER,
                                 .x = x,
                                 .y = y,
                                 .size = size,
                                 .thickness = 1,
                                 .u1 = 1,
                                 .v1 = 1,
                                 .rgba = color,
                                 .text = s,
                                 .text_bytes = length(s)};
    wwhd_hud_emit(list, &element);
}
static void circle(u32 list, float x, float y, float radius, u32 color, int outline) {
    element = (wwhd_hud_element){.kind = outline ? WWHD_HUD_CIRCLE_OUTLINE : WWHD_HUD_CIRCLE,
                                 .anchor = WWHD_HUD_CENTER,
                                 .x = x,
                                 .y = y,
                                 .size = radius,
                                 .thickness = 2,
                                 .u1 = 1,
                                 .v1 = 1,
                                 .rgba = color};
    wwhd_hud_emit(list, &element);
}
static void segment(u32 list, float x, float y, float dx, float dy, u32 color) {
    element = (wwhd_hud_element){.kind = WWHD_HUD_LINE,
                                 .anchor = WWHD_HUD_CENTER,
                                 .x = x,
                                 .y = y,
                                 .w = dx,
                                 .h = dy,
                                 .thickness = 3,
                                 .u1 = 1,
                                 .v1 = 1,
                                 .rgba = color};
    wwhd_hud_emit(list, &element);
}
static int project(HsVec p, float *x, float *y) {
    world[0] = p.x;
    world[1] = p.y;
    world[2] = p.z;
    if (!wwhd_screen_point(world, projected))
        return 0;
    *x = 640 + projected[0];
    *y = 360 - projected[1];
    return *x > -100 && *x < 1380 && *y > -100 && *y < 820;
}
static void button(u32 list, float x, float y, float w, const char *label, int selected) {
    box(list, x, y, w, 48, selected ? 0x284C46FF : 0x192E35FF, 0);
    box(list, x, y, w, 48, selected ? teal : 0x496069FF, 1);
    text(list, x + 18, y + 13, 20, selected ? ink : muted, label);
}
static void header(u32 list, const char *eyebrow, const char *title, const char *sub) {
    box(list, 0, 0, 1280, 720, 0x031018DB, 0);
    box(list, 100, 105, 1080, 510, 0x10232AF8, 0);
    box(list, 100, 105, 1080, 3, gold, 0);
    text(list, 140, 138, 16, gold, eyebrow);
    text(list, 140, 177, 38, ink, title);
    text(list, 140, 235, 18, muted, sub);
}
static void effects(u32 list) {
    for (int i = 0; i < HS_ORBS; i++)
        if (hs.orbs[i].amount) {
            HsVec p = hs.orbs[i].pos;
            p.y += 30;
            float x, y;
            if (project(p, &x, &y)) {
                circle(list, x, y, hs.orbs[i].amount > 8 ? 7 : 4, hs.orbs[i].amount > 8 ? gold : teal, 0);
            }
        }
    for (int i = 0; i < HS_PROJECTILES; i++)
        if (hs.projectiles[i].active) {
            HsProjectile *p = &hs.projectiles[i];
            float x, y;
            if (!project(p->pos, &x, &y))
                continue;
            u32 color = weapon_colors[p->kind];
            if (p->kind == HS_BOMB) {
                circle(list, x, y, 8, color, 0);
                segment(list, x + 3, y - 8, 4, -6, gold);
            } else if (p->kind == HS_BOOMERANG) {
                segment(list, x - 9, y + 5, 9, -12, color);
                segment(list, x, y - 7, 9, 12, color);
            } else {
                HsVec tail = {p->pos.x - p->velocity.x * .025f, p->pos.y - p->velocity.y * .025f,
                              p->pos.z - p->velocity.z * .025f};
                float a, b;
                if (project(tail, &a, &b))
                    segment(list, a, b, x - a, y - b, color);
                circle(list, x, y, 3, color, 0);
            }
        }
    const float sx[] = {1, .7071f, 0, -.7071f, -1, -.7071f, 0, .7071f, 1};
    const float sz[] = {0, .7071f, 1, .7071f, 0, -.7071f, -1, -.7071f, 0};
    for (int i = 0; i < HS_EFFECTS; i++) {
        HsEffect *e = &hs.effects[i];
        if (e->remaining <= 0)
            continue;
        float scale = 1 - e->remaining / e->duration;
        unsigned alpha = (unsigned)(220 * (1 - scale));
        u32 color = ((e->kind < 4 ? weapon_colors[e->kind] : teal) & 0xFFFFFF00u) | alpha;
        float radius = e->radius * (.5f + .5f * scale);
        for (int k = 0; k < 8; k++) {
            HsVec a = {e->pos.x + sx[k] * radius, e->pos.y + 30, e->pos.z + sz[k] * radius};
            HsVec b = {e->pos.x + sx[k + 1] * radius, e->pos.y + 30, e->pos.z + sz[k + 1] * radius};
            float ax, ay, bx, by;
            if (project(a, &ax, &ay) && project(b, &bx, &by))
                segment(list, ax, ay, bx - ax, by - ay, color);
        }
    }
}
void hs_draw(u32 list) {
    string_count = 0;
    if (hs.phase == HS_IDLE) {
        if (hs_can_open && wwhd_config_bool("show_entry", 1))
            button(list, 1030, 24, 220, "HYRULE SURVIVORS", 0);
        return;
    }
    if (hs.phase == HS_MENU) {
        header(list, "HYRULE SURVIVORS / FIRST EXPEDITION", "The Savage Labyrinth",
               "Six waves. Four spirit weapons. One champion.");
        text(list, 140, 296, 22, teal, "MOVE TO SURVIVE. YOUR WEAPONS ATTACK AUTOMATICALLY.");
        text(list, 140, 345, 20, ink, "Gather green EXP, choose upgrades, and clear every wave.");
        text(list, 140, 381, 18, muted, "Start with six hearts and Spirit Sword. Unlock the bow, boomerang and bombs.");
        text(list, 140, 419, 18, muted, "A separate arena and run loadout. Return to where you started.");
        button(list, 140, 520, 280, "A  Begin expedition", 1);
        button(list, 450, 520, 220, "B  Back", 0);
        return;
    }
    if (hs.phase == HS_LOADING || hs.phase == HS_RETURNING) {
        box(list, 340, 275, 600, 150, 0x10232AED, 0);
        text(list, 380, 311, 28, gold,
             hs.phase == HS_LOADING ? "Entering the labyrinth..." : "Returning to your journey...");
        text(list, 380, 364, 18, muted, "HYRULE SURVIVORS");
        return;
    }
    effects(list);
    box(list, 24, 22, 335, 95, 0x0B1C25EF, 0);
    text(list, 42, 36, 15, gold, "HYRULE SURVIVORS");
    text(list, 42, 64, 21, ink, stat("HEALTH  ", (unsigned)hs.hp, " / "));
    text(list, 221, 64, 21, ink, stat("", (unsigned)hs.max_hp, ""));
    box(list, 42, 96, 298, 7, 0x34434CFF, 0);
    box(list, 42, 96, 298 * (float)hs.hp / (float)hs.max_hp, 7, 0xEE8275FF, 0);
    box(list, 390, 22, 500, 95, 0x0B1C25EF, 0);
    char *time = line();
    char *p = number(time, (unsigned)hs.elapsed / 60);
    p = append(p, ":");
    unsigned sec = (unsigned)hs.elapsed % 60;
    *p++ = (char)('0' + sec / 10);
    *p++ = (char)('0' + sec % 10);
    *p = 0;
    text(list, 416, 37, 24, gold, time);
    text(list, 525, 39, 19, ink, stat("WAVE ", (unsigned)hs.wave + 1, " / 6"));
    text(list, 416, 80, 18, muted, hs_wave_names[hs.wave]);
    button(list, 1115, 24, 140, "Pause", 0);
    int alive = 0;
    for (int i = 0; i < HS_ENEMIES; i++)
        if (hs.enemies[i].state == HS_ALIVE || hs.enemies[i].state == HS_PENDING)
            alive++;
    text(list, 920, 86, 17, ink, stat("ENEMIES ", (unsigned)alive, ""));
    text(list, 1105, 86, 17, muted, stat("KILLS ", (unsigned)hs.kills, ""));
    for (int i = 0; i < HS_ENEMIES; i++)
        if (hs.enemies[i].elite && hs.enemies[i].state == HS_ALIVE) {
            HsEnemy *e = &hs.enemies[i];
            text(list, 430, 133, 18, gold, "LABYRINTH CHAMPION");
            box(list, 390, 160, 500, 10, 0x34434CFF, 0);
            box(list, 390, 160, 500 * (float)e->hp / e->max_hp, 10, 0xDDA653FF, 0);
        }
    box(list, 310, 615, 660, 80, 0x0B1C25EF, 0);
    for (int w = 0; w < 4; w++) {
        float x = 325 + 160 * w;
        box(list, x, 624, 150, 36, hs.weapon[w] ? 0x234038FF : 0x17252BFF, 0);
        text(list, x + 8, 629, 14, hs.weapon[w] ? weapon_colors[w] : muted, hs_weapon_names[w]);
        text(list, x + 8, 646, 12, muted, hs.weapon[w] ? stat("Rank ", (unsigned)hs.weapon[w], "") : "Find an upgrade");
    }
    text(list, 325, 674, 14, teal, stat("LEVEL ", (unsigned)hs.level, ""));
    float progress = (float)hs.xp / hs_xp_needed(hs.level);
    if (progress > 1)
        progress = 1;
    box(list, 430, 676, 380, 8, 0x34434CFF, 0);
    box(list, 430, 676, 380 * progress, 8, teal, 0);
    text(list, 830, 672, 14, muted, stat("EXP ", (unsigned)hs.xp, ""));
    if (hs.phase == HS_READY || hs.phase == HS_PAUSED) {
        header(list, "SAVAGE LABYRINTH / FLOOR 1",
               hs.phase == HS_READY ? "Your expedition is ready" : "Expedition paused",
               hs.phase == HS_READY ? "Clear all six waves to win. Collect EXP to build your loadout."
                                    : "Your run waits here. Choose when to continue.");
        text(list, 140, 310, 22, teal, "Spirit Sword is ready.");
        text(list, 140, 357, 20, ink, "Move with the left stick. Camera and roll use the normal controls.");
        text(list, 140, 397, 20, ink, "At level-up: left / right to select, A to confirm. Touch also works.");
        button(list, 140, 520, 280, hs.phase == HS_READY ? "A  Start wave 1" : "A  Resume", 1);
        button(list, 450, 520, 280, "Return to journey", 0);
        return;
    }
    if (hs.phase == HS_UPGRADE) {
        header(list, "LEVEL UP / CHOOSE ONE", "Shape your expedition",
               "Weapons and enemies are paused while you choose.");
        for (int i = 0; i < 3; i++) {
            int u = hs.choices[i];
            float x = 140 + i * 330;
            box(list, x, 290, 310, 205, i == hs.selected ? 0x234038FF : 0x182E36FF, 0);
            box(list, x, 290, 310, 205, i == hs.selected ? teal : 0x496069FF, 1);
            text(list, x + 18, 309, 15, gold,
                 u < 4 ? (hs.weapon[u] ? "WEAPON UPGRADE" : "WEAPON UNLOCK") : "SURVIVAL UPGRADE");
            text(list, x + 18, 344, 25, ink, hs_upgrade_names[u]);
            /* Fixed short lines fit the cards without depending on font metrics. */
            const char *a[] = {"Damage in a circle around Link.",  "Arrows seek the nearest foe.",
                               "A blade that flies out and back.", "Timed area explosions.",
                               "All weapon damage +20%.",          "Reduce weapon cooldowns.",
                               "Larger sword and bomb radius.",    "Pick up EXP from farther away.",
                               "One extra heart; heal one.",       "Restore three hearts."};
            text(list, x + 18, 393, 16, muted, a[u]);
            text(list, x + 18, 447, 17, teal,
                 u < 4 ? stat("Next rank: ", (unsigned)hs.weapon[u] + 1, "") : "Applies immediately");
        }
        text(list, 140, 543, 19, ink, "Left / Right: choose     A: confirm     Or tap a card");
        return;
    }
    if (hs.phase == HS_WIN || hs.phase == HS_LOSE || hs.phase == HS_ERROR) {
        header(list, "EXPEDITION RESULTS",
               hs.phase == HS_WIN    ? "VICTORY"
               : hs.phase == HS_LOSE ? "DEFEAT"
                                     : "Expedition interrupted",
               hs.phase == HS_WIN    ? "The champion has fallen. The labyrinth is yours."
               : hs.phase == HS_LOSE ? "A new expedition brings a new loadout."
               : hs.error            ? hs.error
                                     : "Return and try again.");
        text(list, 140, 291, 23, ink, stat("Defeated: ", (unsigned)hs.kills, ""));
        text(list, 440, 291, 23, ink, stat("Level: ", (unsigned)hs.level, ""));
        text(list, 650, 291, 23, ink, stat("EXP: ", (unsigned)hs.total_xp, ""));
        text(list, 860, 291, 23, gold, time);
        for (int w = 0; w < 4; w++) {
            text(list, 140, 345 + 34 * w, 19, weapon_colors[w], hs_weapon_names[w]);
            text(list, 420, 345 + 34 * w, 19, ink, stat("", hs.damage[w], " damage"));
        }
        text(list, 740, 365, 19, muted, stat("Best defeated: ", hs_best_kills, ""));
        text(list, 740, 402, 19, muted, stat("Victories: ", hs_wins, ""));
        button(list, 140, 540, 280, "A  Retry expedition", 1);
        button(list, 450, 540, 280, "B  Return to journey", 0);
    }
}
static int inside(float x, float y, float left, float top, float width, float height) {
    return x >= left && x <= left + width && y >= top && y <= top + height;
}
void hs_touch(const wwhd_touch_event *e) {
    if (e->kind != WWHD_TOUCH_DOWN)
        return;
    float x = e->x + 640, y = 360 - e->y;
    if (hs.phase == HS_IDLE) {
        hs_action(HS_ACTION_OPEN);
        return;
    }
    if (hs.phase == HS_RUN) {
        hs.phase = HS_PAUSED;
        return;
    }
    if (hs.phase == HS_UPGRADE) {
        for (int i = 0; i < 3; i++)
            if (inside(x, y, 140 + i * 330, 290, 310, 205))
                hs_action(HS_ACTION_CHOICE0 + i);
        return;
    }
    if (hs.phase == HS_MENU) {
        if (inside(x, y, 140, 520, 280, 48))
            hs_action(HS_ACTION_ACCEPT);
        else if (inside(x, y, 450, 520, 220, 48))
            hs_action(HS_ACTION_BACK);
        return;
    }
    if (hs.phase == HS_READY || hs.phase == HS_PAUSED) {
        if (inside(x, y, 140, 520, 280, 48))
            hs_action(HS_ACTION_ACCEPT);
        else if (inside(x, y, 450, 520, 280, 48))
            hs_action(HS_ACTION_EXIT);
        return;
    }
    if (hs.phase == HS_WIN || hs.phase == HS_LOSE || hs.phase == HS_ERROR) {
        if (inside(x, y, 140, 540, 280, 48))
            hs_action(HS_ACTION_RETRY);
        else if (inside(x, y, 450, 540, 280, 48))
            hs_action(HS_ACTION_EXIT);
    }
}
