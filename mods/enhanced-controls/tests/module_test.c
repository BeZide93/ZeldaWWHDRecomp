/* SPDX-License-Identifier: MPL-2.0
 * Executes the real PPC-translated module against synthetic guest memory and
 * host services. No game instructions or assets are used. Linux test runner. */
#define _GNU_SOURCE
#include "wwhd_guest_abi.h"
#include "fixture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

extern const WWHDGuestModuleV1* wwhd_guest_module_v1(const WWHDGuestHostV1*);
static const WWHDGuestModuleV1* mod;
static Cpu cpu;
static uint32_t touch_fn, filter_fn, hud_fn;
static uint64_t step=1, epoch=1, revision=1;
static int pinch_enabled=1, jump_enabled=1, claim_count, hud_count, rays;
static float distance_option=100, wall_z=100000;
static float claim[5][4];
static volatile int preempt_flags[3];
static const uint32_t LINK=0x18000000, SAVE=0x18010000, CAMERA=0x18020000,
                      SCRATCH=0x30040000;
static const char* string(uint32_t a) { return (const char*)ppc_ptr(a); }
static int own(uint32_t a, uint32_t n) { return a>=mod->mem_base && (uint64_t)a+n<=mod->mem_base+mod->mem_size; }
static void call(uint32_t fn, uint32_t arg) {
    cpu.r[1]=0x30020000; cpu.r[3]=arg; cpu.pc=fn;
    for(uint32_t i=0;i<mod->func_count;++i) if(mod->funcs[i].addr==fn) { mod->funcs[i].fn(&cpu); return; }
    fprintf(stderr,"unknown callback %08x\n",fn); abort();
}
static void dispatch(Cpu* c) {
    if(c->pc==COLLISION) {
        ++rays;
        assert(c->r[3]==CAMERA+0x248 && c->r[6]==0x7f);
        c->r[3]=ldf32(c->r[5]+8)>wall_z;
        return;
    }
    for(uint32_t i=0;i<mod->func_count;++i) if(mod->funcs[i].addr==c->pc) { mod->funcs[i].fn(c); return; }
    fprintf(stderr,"unexpected dispatch %08x\n",c->pc); abort();
}
static void unavailable(Cpu* c,uint32_t a,uint32_t insn) { (void)c; fprintf(stderr,"unsupported %08x %08x\n",a,insn); abort(); }
static void trap(Cpu* c,uint32_t a) { unavailable(c,a,0); }
static uint64_t timebase(void) { return step; }
static double reciprocal(double x) { return 1/x; }
static double rsqrt(double x) { return 1/sqrt(x); }
static void preempt(Cpu* c) { (void)c; abort(); }
static void original(Cpu* c,uint32_t a) { unavailable(c,a,0); }
static void s_memset(Cpu* c) { memset(ppc_ptr(c->r[3]),c->r[4],c->r[5]); }
static void s_touch(Cpu* c) { touch_fn=c->r[3]; c->r[3]=1; }
static void s_filter(Cpu* c) { filter_fn=c->r[3]; c->r[3]=1; }
static void s_hud(Cpu* c) { hud_fn=c->r[3]; assert(c->r[4]==0); c->r[3]=1; }
static void s_log(Cpu* c) { assert(own(c->r[3],1)); }
static void s_read(Cpu* c) { assert(own(c->r[3],32)); memset(ppc_ptr(c->r[3]),0,32); }
static void s_emit(Cpu* c) {
    uint32_t a=c->r[4]; assert(own(a,72)); ++hud_count;
    if(ld32(a)==1) { uint32_t text=ld32(a+64), bytes=ld32(a+68); assert(own(text,bytes)); }
    c->r[3]=1;
}
static void u64(Cpu* c,uint64_t v) { c->r[3]=v>>32; c->r[4]=(uint32_t)v; }
static void s_step(Cpu* c) { u64(c,step); }
static void s_epoch(Cpu* c) { u64(c,epoch); }
static void s_dt(Cpu* c) { c->f[1].ps0=1./30; }
static void s_bool(Cpu* c) {
    const char* name=string(c->r[3]); assert(own(c->r[3],1));
    c->r[3]=!strcmp(name,"pinch")?pinch_enabled:!strcmp(name,"jump")?jump_enabled:c->r[4];
}
static void s_changed(Cpu* c) { assert(!strcmp(string(c->r[3]),"distance")); u64(c,revision); }
static void s_number(Cpu* c) {
    assert(own(c->r[3],1)); if(!strcmp(string(c->r[3]),"distance")) c->f[1].ps0=distance_option;
}
static void s_claim(Cpu* c) {
    unsigned slot=c->r[3]; assert(slot<5); ++claim_count;
    for(int i=0;i<4;++i) claim[slot][i]=(float)c->f[i+1].ps0;
    c->r[3]=1;
}
static PpcFunc service(const char* name) {
    struct { const char* name; PpcFunc fn; } all[]={
        {"memset",s_memset},{"wwhd_touch_register",s_touch},{"wwhd_input_filter_register",s_filter},
        {"wwhd_hud_register",s_hud},{"wwhd_log",s_log},{"wwhd_input_read",s_read},
        {"wwhd_hud_emit",s_emit},{"wwhd_logic_step",s_step},{"wwhd_hud_epoch",s_epoch},
        {"wwhd_logic_dt",s_dt},{"wwhd_config_bool",s_bool},{"wwhd_config_changed",s_changed},
        {"wwhd_config_float",s_number},{"wwhd_touch_claim",s_claim}};
    for(unsigned i=0;i<sizeof(all)/sizeof(all[0]);++i) if(!strcmp(name,all[i].name)) return all[i].fn;
    fprintf(stderr,"unexpected service %s\n",name); abort();
}
static void hook(uint32_t target,uint32_t arg) {
    for(uint32_t i=0;i<mod->hook_count;++i) if(mod->hooks[i].target==target) { call(mod->hooks[i].func,arg); return; }
    abort();
}
static void touch(unsigned kind,float x,float y) {
    st32(SCRATCH,kind); stf32(SCRATCH+4,x); stf32(SCRATCH+8,y); call(touch_fn,SCRATCH);
}
static unsigned pad(unsigned real) {
    memset(ppc_ptr(SCRATCH),0,32); st32(SCRATCH,real); call(filter_fn,SCRATCH); return ld32(SCRATCH);
}
static void next(void) { ++step; (void)pad(0); }
static void camera(float z) { stf32(CAMERA+0xE4,z); hook(STORE,CAMERA); }
int main(void) {
    void* memory=mmap(PPC_MEM_BASE,1ull<<32,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_FIXED_NOREPLACE,-1,0);
    assert(memory==PPC_MEM_BASE);
    WWHDGuestHostV1 host={sizeof(host),WWHD_GUEST_ABI_VERSION,dispatch,unavailable,trap,timebase,
        reciprocal,rsqrt,preempt,preempt_flags,original,service};
    mod=wwhd_guest_module_v1(&host); assert(mod && mod->hook_count==2);
    memcpy(ppc_ptr(mod->mem_base),mod->image,mod->image_size);
    memcpy(ppc_ptr(PLAY+0x5134),"sea",4);
    st32(PLAY+0x5B2C,LINK); st32(LINK,1); st32(LINK+4,7); st16(LINK+0x69B0,0x103);
    st32(SAVE_PTR,SAVE); st16(SAVE+0x20,12); st16(SAVE+0x22,12);
    st32(PLAY+0x5AF8,CAMERA); st8(CAMERA+0x248+4,1);
    hook(LINK_EXECUTE,LINK); assert(touch_fn && filter_fn && hud_fn && claim_count==5);
    assert(claim[4][0]==290 && claim[4][2]==550);
    call(hud_fn,1); assert(hud_count==10);
    int n=claim_count; next(); call(hud_fn,1); assert(claim_count==n);
    /* Real Y survives the macro; repeated reads do not advance its clock. */
    touch(0,-150,-180); assert(pad(0x1000)==(0x1000|0x80));
    next(); next(); assert(pad(0)==(0x80|0x8000)); assert(pad(0)==(0x80|0x8000));
    touch(2,-150,-180); assert(pad(0)==(0x80|0x8000));
    for(int i=0;i<7;++i) next(); assert(pad(0x1000)==0x1000);
    touch(0,50,-180); assert(pad(0)==0x4000);
    st8(MENU_FLAG,1); assert(pad(0)==0); assert(claim[0][0]==claim[0][2]);
    st8(MENU_FLAG,0); next(); assert(pad(0)==0);
    /* Disable a live option during an attack. */
    touch(0,-150,-180); assert(pad(0)==0x80); jump_enabled=0; next(); assert(!pad(0));
    jump_enabled=1; next();
    /* A real double tap; pinch clears all tap recognition. */
    touch(0,420,120); next(); touch(2,420,120); next(); touch(0,420,120); next(); touch(2,420,120);
    assert(pad(0)==0x80); for(int i=0;i<3;++i)next(); assert(!pad(0));
    touch(0,420,120); touch(4,2,0); touch(5,0,0); touch(2,420,120); assert(!pad(0));
    camera(100); assert(fabs(ldf32(CAMERA+0xE4)-50)<.01);
    /* store rewrites the native view each step: no multiplying an old zoom. */
    camera(100); assert(fabs(ldf32(CAMERA+0xE4)-50)<.01);
    distance_option=200; ++revision; next(); wall_z=140;
    camera(100); assert(ldf32(CAMERA+0xE4)>100 && ldf32(CAMERA+0xE4)<140 && rays==7);
    st8(PLAY+0x5292,1); camera(100); assert(ldf32(CAMERA+0xE4)==100);
    st8(PLAY+0x5292,0); st32(PLAY+0x5CD8,0x2000); camera(100); assert(ldf32(CAMERA+0xE4)==100);
    st32(PLAY+0x5CD8,0); pinch_enabled=0; next(); camera(100); assert(ldf32(CAMERA+0xE4)==100);
    touch(0,50,-180); assert(pad(0)==0x4000); ++epoch; assert(!pad(0));
    puts("Translated module: registration, claims, HUD, input, lifecycle and camera fixture passed");
    assert(munmap(memory,1ull<<32)==0);
}
