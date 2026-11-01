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
    SAVE_BYTES = 0x778
};
static const WWHDGuestModuleV1 *mod;
static uint64_t logic_step;
static uint32_t hud_callback, input_callback;
static unsigned hud_elements, original_damage, serializations;
static uint8_t expected_save[SAVE_BYTES];
static volatile int preempt[3];
static Cpu cpu;
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
        st32(c->r[4], 0);
        c->r[3] = 0;
        return;
    }
    if (c->pc == GAME_DELETE) {
        c->r[3] = 1;
        return;
    }
    if (c->pc == GAME_CREATE) {
        c->r[3] = HS_NONE;
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
    else
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
    dlclose(lib);
    munmap(memory, 0x80000000ull);
}
