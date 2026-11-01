/* SPDX-License-Identifier: MPL-2.0
 * Executes the actual translated PowerPC module against synthetic host services.
 * This checks adapter state/save/input/HUD plumbing, not native game behavior.
 */
#define _GNU_SOURCE
#include "wwhd_guest_abi.h"
#include "core.h"
#include "stage_name.h"
#include "test_addresses.h"
#include <assert.h>
#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

enum {
    PLAY = 0x11000000,
    LINK = 0x12000000,
    SAVE = 0x13000000,
    STACK = 0x14000000,
    INPUT = 0x15000000,
    ENEMY = 0x16000000,
    SHOT_BASE = 0x17000000,
    AT_INFO = 0x18000000,
    AT_OBJ = 0x18000100,
    AT_STATUS = 0x18000300,
    SAVE_BYTES = 0x778
};
static const WWHDGuestModuleV1 *mod;
static uint64_t logic_step;
static uint32_t hud_callback, input_callback;
static unsigned hud_elements, original_damage, serializations;
static uint8_t expected_save[SAVE_BYTES];
static volatile int preempt[3];
static Cpu cpu;
static unsigned shot_count, native_spin, native_reload, native_throw, native_anim, native_exec, contacts;
static unsigned shot_alive[32], fail_item;
static float set_radius;
static void clear_native_keep(uint32_t offset) {
    st32(LINK + offset, HS_NONE);
    st32(LINK + offset + 4, 0);
}
static uint32_t make_native_item(unsigned profile, unsigned keep) {
    if (fail_item) {
        clear_native_keep(keep);
        return 0;
    }
    assert(shot_count < 32);
    unsigned i = shot_count++;
    uint32_t p = SHOT_BASE + i * 0x2000;
    memset(ppc_ptr(p), 0, 0x2000);
    st32(p + 4, 100 + i);
    st16(p + 0xE, profile);
    shot_alive[i] = 1;
    st32(LINK + keep, 100 + i);
    st32(LINK + keep + 4, p);
    return p;
}
static void kill_native_item(unsigned id) {
    if (id >= 100 && id < 100 + shot_count)
        shot_alive[id - 100] = 0;
}

#define FIELD(field) (ADDR_HS + offsetof(HsRun, field))
#define VALUE(field) ld32(FIELD(field))
static void dispatch(Cpu *c);
static void call(uint32_t address, uint32_t arg) {
    cpu.pc = address;
    cpu.r[3] = arg;
    dispatch(&cpu);
    assert(cpu.r[1] == STACK + 0xFF000);
}
static void hook(uint32_t address, uint32_t arg) {
    for (unsigned i = 0; i < mod->hook_count; i++)
        if (mod->hooks[i].target == address) {
            cpu.r[3] = arg;
            mod->hooks[i].fn(&cpu);
            assert(cpu.r[1] == STACK + 0xFF000);
            return;
        }
    assert(!"missing hook");
}
static void dispatch(Cpu *c) {
    for (unsigned i = 0; i < mod->func_count; i++)
        if (mod->funcs[i].addr == c->pc) {
            mod->funcs[i].fn(c);
            return;
        }
    if (c->pc == GAME_PLAY) {
        c->r[3] = PLAY;
        return;
    }
    if (c->pc == GAME_GROUND) {
        c->f[1].ps0 = 0;
        return;
    }
    if (c->pc == GAME_SEARCH) {
        unsigned id = c->r[3];
        uint32_t p = 0;
        if (id == 20)
            p = ENEMY;
        if (id >= 100 && id < 100 + shot_count && shot_alive[id - 100])
            p = SHOT_BASE + (id - 100) * 0x2000;
        st32(c->r[4], p);
        c->r[3] = p != 0;
        return;
    }
    if (c->pc == GAME_DELETE) {
        kill_native_item(c->r[3]);
        c->r[3] = 1;
        return;
    }
    if (c->pc == GAME_CREATE) {
        c->r[3] = HS_NONE;
        return;
    }
    if (c->pc == GAME_DELETE_EQUIP) {
        kill_native_item(ld32(LINK + 0x6590));
        clear_native_keep(0x6590);
        st16(LINK + 0x69B0, 0x100);
        return;
    }
    if (c->pc == GAME_ANGLE_Y || c->pc == GAME_ANGLE_X) {
        c->r[3] = c->pc == GAME_ANGLE_Y ? 0x2000 : 0x1000;
        return;
    }
    if (c->pc == GAME_SWORD_MODEL) {
        assert(ld16(LINK + 0x69B0) == 0x103);
        return;
    }
    if (c->pc == GAME_SPIN) {
        native_spin++;
        st32(LINK + 0x65F0, 0x55);
        stf32(LINK + 0x69FC, 180);
        c->r[3] = 1;
        return;
    }
    if (c->pc == GAME_MAKE_ITEM) {
        unsigned item = ld16(LINK + 0x69B0);
        if (item == 0x2D)
            make_native_item(0x1B0, 0x6590);
        else if (item == 0x31) {
            make_native_item(0x126, 0x65A0);
            st16(LINK + 0x69B0, 0x100);
        } else
            assert(item == 0x27);
        return;
    }
    if (c->pc == GAME_MAKE_ARROW) {
        make_native_item(0x1D8, 0x6590);
        return;
    }
    if (c->pc == GAME_BOW_RELOAD) {
        native_reload++;
        st16(LINK + 0x5888, 0xC);
        stf32(LINK + 0x58D8, 1);
        return;
    }
    if (c->pc == GAME_UPPER_ANIM) {
        assert(c->r[4] == 0x34 && c->r[5] == 2);
        assert(c->f[1].ps0 == 1.5 && c->f[2].ps0 == 2);
        native_anim++;
        st16(LINK + 0x5888, 0x34);
        stf32(LINK + 0x58D8, 1);
        return;
    }
    if (c->pc == GAME_TEXTURE_ANIM)
        return;
    if (c->pc == GAME_THROW_BOMB) {
        assert(ld32(LINK + 0x65A4));
        native_throw++;
        st32(LINK + 0x65F0, 0x71);
        c->r[3] = 1;
        return;
    }
    fprintf(stderr, "unexpected game call %08x\n", c->pc);
    abort();
}
static void original(Cpu *c, uint32_t address) {
    if (address == GAME_SERIALIZE) {
        assert(!memcmp(ppc_ptr(SAVE + 0x20), expected_save, SAVE_BYTES));
        serializations++;
    } else if (address == GAME_DAMAGE)
        original_damage++;
    else if (address == GAME_CONTACT) {
        contacts++;
        assert(ld8(c->r[3] + 0x3A1) == 100 || c->r[3] != ENEMY);
        st8(c->r[3] + 0x3A1, 96);
        st8(c->r[4] + 8, 4);
        c->r[3] = ld32(AT_STATUS + 0xC);
        return;
    } else if (address == GAME_ARROW_EXEC || address == GAME_BOOM_EXEC || address == GAME_BOMB_EXEC)
        native_exec++;
    else if (address == GAME_ARROW_SHOOT) {
        assert(ld16(c->r[3] + 0x320) == 0x1000 && ld16(c->r[3] + 0x322) == 0x2000);
    } else if (address == GAME_RADIUS) {
        set_radius = (float)c->f[1].ps0;
    } else if (address == GAME_THROW_BOOM) {
        assert(ld32(LINK + 0x65B0) == ENEMY);
        st32(LINK + 0x6598, ld32(LINK + 0x6590));
        st32(LINK + 0x659C, ld32(LINK + 0x6594));
        clear_native_keep(0x6590);
        st16(LINK + 0x69B0, 0x100);
    } else
        assert(address == GAME_LINK);
    c->r[3] = 1;
}
static void svc_step(Cpu *c) {
    c->r[3] = (uint32_t)(logic_step >> 32);
    c->r[4] = (uint32_t)logic_step;
}
static void svc_revision(Cpu *c) { c->r[3] = c->r[4] = 0; }
static void svc_dt(Cpu *c) { c->f[1].ps0 = 1.0 / 30; }
static void svc_bool(Cpu *c) { c->r[3] = c->r[4]; }
static void svc_zero(Cpu *c) { c->r[3] = 0; }
static void svc_one(Cpu *c) { c->r[3] = 1; }
static void svc_hud(Cpu *c) {
    hud_callback = c->r[3];
    c->r[3] = 1;
}
static void svc_input(Cpu *c) {
    input_callback = c->r[3];
    c->r[3] = 1;
}
static void svc_copy(Cpu *c) {
    assert(c->r[5] < 1000000);
    memcpy(ppc_ptr(c->r[3]), ppc_ptr(c->r[4]), c->r[5]);
}
static void svc_set(Cpu *c) {
    assert(c->r[5] < 1000000);
    memset(ppc_ptr(c->r[3]), c->r[4], c->r[5]);
}
static void svc_emit(Cpu *c) {
    uint32_t p = c->r[4];
    assert(p >= mod->mem_base && p + 72 <= mod->mem_base + mod->mem_size);
    for (unsigned offset = 12; offset <= 32; offset += 4)
        assert(isfinite(ldf32(p + offset)));
    if (ld32(p) == 1) { /* text buffers must belong to the mod, not guest stack */
        uint32_t str = ld32(p + 64), n = ld32(p + 68);
        assert(n < 160 && str >= mod->mem_base && str + n <= mod->mem_base + mod->mem_size);
    }
    hud_elements++;
    c->r[3] = 1;
}
static PpcFunc service(const char *name) {
    if (!strcmp(name, "wwhd_logic_step"))
        return svc_step;
    if (!strcmp(name, "wwhd_logic_dt"))
        return svc_dt;
    if (!strcmp(name, "wwhd_config_changed"))
        return svc_revision;
    if (!strcmp(name, "wwhd_config_bool"))
        return svc_bool;
    if (!strcmp(name, "wwhd_hud_register"))
        return svc_hud;
    if (!strcmp(name, "wwhd_input_filter_register"))
        return svc_input;
    if (!strcmp(name, "wwhd_hud_emit"))
        return svc_emit;
    if (!strcmp(name, "memcpy"))
        return svc_copy;
    if (!strcmp(name, "memset"))
        return svc_set;
    if (!strcmp(name, "wwhd_file_read") || !strcmp(name, "wwhd_screen_point"))
        return svc_zero;
    if (!strcmp(name, "wwhd_touch_register") || !strcmp(name, "wwhd_touch_claim") || !strcmp(name, "wwhd_log") ||
        !strcmp(name, "wwhd_log_int") || !strcmp(name, "wwhd_particle_at") || !strcmp(name, "wwhd_file_write"))
        return svc_one;
    fprintf(stderr, "unexpected service %s\n", name);
    abort();
}
static void unimplemented(Cpu *c, uint32_t a, uint32_t insn) {
    (void)c;
    fprintf(stderr, "unimplemented %08x %08x\n", a, insn);
    abort();
}
static void trap(Cpu *c, uint32_t a) { unimplemented(c, a, 0); }
static void frame(void) {
    logic_step++;
    hook(GAME_LINK, LINK);
}
static void frames(int n) {
    while (n--)
        frame();
}
static void action(int a) {
    call(ADDR_ACTION, (uint32_t)a);
    frame();
}
static void set_stage(const char *stage) {
    memcpy(ppc_ptr(PLAY + 0x5134), stage, 8);
    st8(PLAY + 0x514C, 0);
}
static void setup(void) {
    memset(ppc_ptr(mod->mem_base), 0, mod->mem_size);
    memcpy(ppc_ptr(mod->mem_base), mod->image, mod->image_size);
    memset(ppc_ptr(PLAY), 0, 0x10000);
    memset(ppc_ptr(LINK), 0, 0x10000);
    memset(ppc_ptr(SAVE), 0, 0x2000);
    memset(&cpu, 0, sizeof(cpu));
    cpu.r[1] = STACK + 0xFF000;
    shot_count = native_spin = native_reload = native_throw = native_anim = native_exec = contacts = fail_item = 0;
    memset(shot_alive, 0, sizeof(shot_alive));
    st32(PLAY + 0x5B2C, LINK);
    st32(LINK + 4, 10);
    st32(LINK + 0x65F0, 4);
    st16(LINK + 0x69B0, 0x100);
    clear_native_keep(0x6590);
    clear_native_keep(0x6598);
    clear_native_keep(0x65A0);
    st32(DATA_SAVE, SAVE);
    st8(SAVE + 0x20 + 0x1290, 0);
    set_stage("sea\0\0\0\0");
    st16(PLAY + 0x513C, 1);
    st8(PLAY + 0x513E, 44);
    st8(PLAY + 0x513F, 0xFF);
    st8(LINK + 0x326, 44);
    stf32(LINK + 0x314, 100);
    stf32(LINK + 0x31C, 200);
    for (unsigned i = 0; i < SAVE_BYTES; i++)
        expected_save[i] = (uint8_t)(i * 17 + 3);
    memcpy(ppc_ptr(SAVE + 0x20), expected_save, SAVE_BYTES);
    frame();
    assert(VALUE(phase) == HS_IDLE && hud_callback && input_callback);
}
static void enter(void) {
    action(1);
    assert(VALUE(phase) == HS_MENU);
    frames(10);
    action(2);
    assert(VALUE(phase) == HS_LOADING && ld32(ADDR_SESSION) == 1);
    assert(!strcmp((char *)ppc_ptr(PLAY + 0x5140), HS_STAGE_NAME));
    set_stage(HS_STAGE_NAME);
    st8(LINK + 0x326, 0);
    frame();
    assert(VALUE(phase) == HS_READY);
}
static void check_hud(void) {
    /* Draw every valid screen using actual translated callbacks. */
    const int phases[] = {HS_READY, HS_RUN, HS_PAUSED, HS_UPGRADE, HS_WIN, HS_LOSE, HS_ERROR};
    for (unsigned i = 0; i < sizeof(phases) / sizeof(phases[0]); i++) {
        st32(FIELD(phase), phases[i]);
        hud_elements = 0;
        call(hud_callback, 1);
        assert(hud_elements > 10 && VALUE(phase) == (unsigned)phases[i]);
    }
    st32(FIELD(phase), HS_READY);
}
static void native_fixture(void) {
    setup();
    enter();
    st32(FIELD(phase), HS_RUN);
    stf32(FIELD(spawn_timer), 10000);
    memset(ppc_ptr(ENEMY), 0, 0x1000);
    st32(ENEMY + 4, 20);
    st16(ENEMY + 0xE, 0xBC);
    st32(ENEMY + 0xB0, 0xFFFF0000);
    st8(ENEMY + 0x3A1, 10);
    stf32(ENEMY + 0x314, 100);
    stf32(ENEMY + 0x37C, 100);
    uint32_t e = FIELD(enemies);
    st32(e + offsetof(HsEnemy, id), 20);
    st32(e + offsetof(HsEnemy, generation), 1);
    st32(e + offsetof(HsEnemy, state), HS_ALIVE);
    st32(e + offsetof(HsEnemy, kind), 4);
    st32(e + offsetof(HsEnemy, hp), 160);
    st32(e + offsetof(HsEnemy, max_hp), 160);
    call(COMBAT_STEP, LINK);
}
static int launch(int weapon) {
    cpu.r[4] = 20;
    cpu.r[5] = 17;
    cpu.f[1].ps0 = 1.5;
    call(COMBAT_ATTACK, weapon);
    return cpu.r[3] != 0;
}
static void contact(uint32_t source) {
    st32(AT_INFO, AT_OBJ);
    st32(AT_OBJ + 0x44, AT_STATUS);
    st32(AT_STATUS + 0xC, source);
    cpu.r[4] = AT_INFO;
    hook(GAME_CONTACT, ENEMY);
}
static void test_native_combat(void) {
    for (int weapon = 0; weapon < 4; weapon++) {
        native_fixture();
        /* Rolling/damage/falls must not be overwritten by automatic attacks. */
        st32(LINK + 0x65F0, 0x27);
        assert(!launch(weapon));
        st32(LINK + 0x65F0, 4);
        assert(launch(weapon));
        assert(!launch((weapon + 1) % 4));
        uint32_t source = weapon == HS_SWORD ? LINK : SHOT_BASE;
        uint32_t e = FIELD(enemies);
        assert(ld32(e + offsetof(HsEnemy, hp)) == 160); /* no animation-only damage */
        if (weapon == HS_SWORD) {
            assert(native_spin == 1);
            assert(ldf32(LINK + 0x69FC) == 270);
        }
        if (weapon == HS_BOW) {
            assert(native_reload == 1 && ld16(source + 0xE) == 0x1D8);
            hook(GAME_ARROW_SHOOT, source);
            /* Arrow released by the native animation, independent of Link. */
            clear_native_keep(0x6590);
            st16(LINK + 0x5888, 0xE);
            stf32(LINK + 0x58D8, 0);
        }
        if (weapon == HS_BOOMERANG) {
            assert(native_anim == 1);
            assert(!ld8(LINK + 0x827C) && ld16(LINK + 0x827E) == 0x2000);
            st32(LINK + 0x65B0, 0x19990000);
            hook(GAME_THROW_BOOM, LINK);
            assert(ld32(LINK + 0x65B0) == 0x19990000);
            assert(ld32(LINK + 0x659C) == source);
            stf32(LINK + 0x58D8, 0);
        }
        if (weapon == HS_BOMB) {
            assert(native_throw == 1);
            clear_native_keep(0x65A0);
            cpu.f[1].ps0 = 200;
            hook(GAME_RADIUS, source + 0x920 + 0x118);
            assert(set_radius == 300);
            cpu.f[1].ps0 = 200;
            hook(GAME_RADIUS, source + 0x100);
            assert(set_radius == 200);
        }
        contact(source);
        assert(ld32(e + offsetof(HsEnemy, hp)) == 143 && ld8(ENEMY + 0x3A1) == 100);
        contact(source);
        assert(ld32(e + offsetof(HsEnemy, hp)) == 143); /* one hit per cast/target */
        st32(e + offsetof(HsEnemy, generation), 2);
        contact(source);
        assert(ld32(e + offsetof(HsEnemy, hp)) == 126);
        st32(FIELD(phase), HS_UPGRADE);
        st32(e + offsetof(HsEnemy, generation), 3);
        contact(source);
        assert(ld32(e + offsetof(HsEnemy, hp)) == 126);
        if (weapon) {
            unsigned exec = weapon == HS_BOW         ? GAME_ARROW_EXEC
                            : weapon == HS_BOOMERANG ? GAME_BOOM_EXEC
                                                     : GAME_BOMB_EXEC;
            hook(exec, source);
            assert(native_exec == 0); /* motion/fuse pause */
            hook(exec, source + 0x1000);
            assert(native_exec == 1); /* unrelated actor untouched */
            st32(FIELD(phase), HS_RUN);
            hook(exec, source);
            assert(native_exec == 2);
        }
        st32(FIELD(phase), HS_RUN);
        st32(e + offsetof(HsEnemy, hp), 5);
        contact(source);
        assert(ld32(e + offsetof(HsEnemy, hp)) == 0 && ld8(ENEMY + 0x3A1) == 0 && ld8(AT_INFO + 9) == 1);
        assert(ld32(FIELD(damage) + (unsigned)weapon * 4) == 39); /* 17+17+5; no overkill inflation */
        stf32(FIELD(elapsed), 1);
        st32(LINK + 0x65F0, 4);
        call(COMBAT_STEP, LINK);
        st16(PLAY + 0x5B68, 0xFFFF);
        st16(PLAY + 0x5B6C, 0xFFFF);
        call(COMBAT_STOP, LINK);
        assert(ld16(PLAY + 0x5B68) == 0 && ld16(PLAY + 0x5B6C) == 0);
        for (unsigned i = 0; i < shot_count; i++)
            assert(!shot_alive[i]);
    }
    native_fixture();
    fail_item = 1;
    assert(!launch(HS_BOW));
    assert(!launch(HS_BOOMERANG));
    assert(!launch(HS_BOMB));
    assert(native_throw == 0); /* never dereference a failed bomb creation */
    puts("Native combat: animation dispatch, busy gates, launch aim, real-hit damage, deduplication, radius, pause and "
         "cleanup passed");
}
int main(int argc, char **argv) {
    assert(argc == 2);
    void *memory = mmap(PPC_MEM_BASE, 0x80000000ull, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(memory == PPC_MEM_BASE);
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        puts(dlerror());
        return 1;
    }
    WWHDGuestInitV1 init = (WWHDGuestInitV1)dlsym(lib, WWHD_GUEST_INIT_SYMBOL);
    assert(init);
    const WWHDGuestHostV1 host = {.size = sizeof(host),
                                  .abi_version = WWHD_GUEST_ABI_VERSION,
                                  .dispatch = dispatch,
                                  .unimplemented = unimplemented,
                                  .trap = trap,
                                  .core_preempt = preempt,
                                  .call_original = original,
                                  .service = service};
    mod = init(&host);
    assert(mod);
    setup();
    enter();
    check_hud();
    memset(ppc_ptr(INPUT), 0, 32);
    st32(INPUT, 0x2000);
    call(input_callback, INPUT);
    assert(ld32(INPUT) == 0); /* modal input is captured */
    frames(10);
    action(2);
    assert(VALUE(phase) == HS_RUN);
    stf32(FIELD(grace), 0);
    cpu.f[1].ps0 = -4;
    hook(GAME_DAMAGE, LINK);
    assert(VALUE(hp) == 20 && original_damage == 0);
    memset(ppc_ptr(SAVE + 0x20), 0x55, SAVE_BYTES);
    hook(GAME_SERIALIZE, 0);
    assert(serializations == 1);
    for (unsigned i = 0; i < SAVE_BYTES; i++)
        assert(ld8(SAVE + 0x20 + i) == 0x55);
    action(7);
    assert(VALUE(phase) == HS_RETURNING);
    set_stage("sea\0\0\0\0");
    frame();
    assert(VALUE(phase) == HS_IDLE && !ld32(ADDR_SESSION));
    assert(!memcmp(ppc_ptr(SAVE + 0x20), expected_save, SAVE_BYTES));
    assert(ldf32(LINK + 0x314) == 100 && ldf32(LINK + 0x31C) == 200);
    /* A newly loaded save, even in the same slot, supersedes the snapshot. */
    setup();
    enter();
    memset(ppc_ptr(SAVE + 0x20), 0x66, SAVE_BYTES);
    hook(GAME_LOAD, 0);
    assert(!ld32(ADDR_SESSION));
    frame();
    assert(ld8(SAVE + 0x20) == 0x66);
    setup();
    enter();
    memset(ppc_ptr(SAVE + 0x20), 0x77, SAVE_BYTES);
    st8(SAVE + 0x20 + 0x1290, 1);
    frame();
    assert(!ld32(ADDR_SESSION) && ld8(SAVE + 0x20) == 0x77);
    puts("Translated adapter: entry, floor gate, input, HUD, damage, serialization, return and Quest Log isolation "
         "passed");
    test_native_combat();
    dlclose(lib);
    munmap(memory, 0x80000000ull);
}
