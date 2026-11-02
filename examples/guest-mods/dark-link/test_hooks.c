/* Execute the translated guest module against synthetic memory and host services.
 * Linux-only integration driver; no game code/data or renderer is involved. */
#include "wwhd_guest_abi.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

enum { PLAY=0x11000000, LINK=0x11010000, SAVE=0x11020000, ENEMY=0x11030000,
       HIT=0x11040000, INPUT=0x11050000, MODEL=0x11060000, DATA=0x11070000,
       MATERIALS=0x11080000, EYE_ANIM=0x11090000, RESOURCE=0x110a0000,
       NAMES=0x110b0000, PE=0x110c0000, FOG=0x110d0000, VTABLE=0x110e0000 };
static const WWHDGuestModuleV1* module;
static uint32_t filter, hud, touch, step=1, revision=1;
static int free_mode, enabled=1, paused, menu, touch_visible=1, claim_active, elements;
static int eye_indices[2]={1,2}, name_lookups;

static void execute(uint32_t fn, uint32_t a, uint32_t b) {
    Cpu c = {0}; c.r[1] = 0x1201fff0; c.r[3] = a; c.r[4] = b;
    for (uint32_t i=0; i<module->func_count; ++i) if (module->funcs[i].addr == fn) {
        module->funcs[i].fn(&c); return;
    }
    assert(!"missing callback");
}
static void hook(uint32_t address, uint32_t kind, uint32_t a, uint32_t b) {
    for (uint32_t i=0; i<module->hook_count; ++i) {
        const WWHDGuestHook* h = &module->hooks[i];
        if (h->target == address && h->kind == kind) { execute(h->func, a, b); return; }
    }
    assert(!"missing hook");
}
static const char* arg_string(Cpu* c) { return (const char*)ppc_ptr(c->r[3]); }
static void config_bool(Cpu* c) {
    const char* key=arg_string(c);
    c->r[3] = !strcmp(key,"free_transform") ? free_mode : !strcmp(key,"enabled") ? enabled :
        !strcmp(key,"touch_button") ? touch_visible : c->r[4];
}
static void config_int(Cpu* c) { c->r[3]=c->r[4]; }
static void config_revision(Cpu* c) { c->r[3]=0; c->r[4]=revision; }
static void logic_step(Cpu* c) { c->r[3]=0; c->r[4]=step; }
static void logic_dt(Cpu* c) { c->f[1].ps0=1.0/30; }
static void reg_filter(Cpu* c) { filter=c->r[3]; c->r[3]=1; }
static void reg_hud(Cpu* c) { hud=c->r[3]; c->r[3]=1; }
static void reg_touch(Cpu* c) { touch=c->r[3]; c->r[3]=1; }
static void claim(Cpu* c) { claim_active=c->f[3].ps0>c->f[1].ps0 && c->f[4].ps0>c->f[2].ps0; c->r[3]=1; }
static void emit(Cpu* c) { ++elements; c->r[3]=1; }
static void copy(Cpu* c) { memcpy(ppc_ptr(c->r[3]),ppc_ptr(c->r[4]),c->r[5]); }
static void clear(Cpu* c) { memset(ppc_ptr(c->r[3]),c->r[4],c->r[5]); }
static PpcFunc service(const char* name) {
    if (!strcmp(name,"wwhd_config_bool")) return config_bool;
    if (!strcmp(name,"wwhd_config_int")) return config_int;
    if (!strcmp(name,"wwhd_config_changed")) return config_revision;
    if (!strcmp(name,"wwhd_logic_step")) return logic_step;
    if (!strcmp(name,"wwhd_logic_dt")) return logic_dt;
    if (!strcmp(name,"wwhd_input_filter_register")) return reg_filter;
    if (!strcmp(name,"wwhd_hud_register")) return reg_hud;
    if (!strcmp(name,"wwhd_touch_register")) return reg_touch;
    if (!strcmp(name,"wwhd_touch_claim")) return claim;
    if (!strcmp(name,"wwhd_hud_emit")) return emit;
    if (!strcmp(name,"memcpy")) return copy;
    if (!strcmp(name,"memset")) return clear;
    fprintf(stderr,"Unexpected service: %s\n",name); return 0;
}
static void dispatch(Cpu* c) {
    switch (c->pc) {
        case 0x025200d4: c->r[3]=PLAY; return;
        case 0x025986bc: c->r[3]=menu; return;
        case 0x025af2a4: c->r[3]=paused; return;
        /* Synthetic virtual getFogInfo; the real function is read from PE's vtable. */
        case 0x02001000:
            assert(c->r[3]>=PE && c->r[3]<PE+3*0x40);
            c->r[3]=ld32(c->r[3]+4); return;
        case 0x027df9b0:
            assert(c->r[3]==RESOURCE+0x100); ++name_lookups;
            assert(c->r[4]==NAMES || c->r[4]==NAMES+32);
            c->r[3]=eye_indices[c->r[4]==NAMES+32]; return;
        default: fprintf(stderr,"Unexpected native call: %08x\n",c->pc); abort();
    }
}
static void tick(void) { ++step; hook(0x0240ebb0,2,LINK,0); }
static void buttons(uint32_t value) { st32(INPUT,value); execute(filter,INPUT,0); }
static unsigned damage(unsigned power) {
    st8(HIT+8,power); hook(0x02518db0,3,HIT,0); return ld8(HIT+8);
}
static void hit(int lose_hp) {
    st8(ENEMY+0x3a1,10); st8(HIT+8,1);
    hook(0x025192a8,2,ENEMY,HIT);
    if (lose_hp) st8(ENEMY+0x3a1,9);
    hook(0x025192a8,3,ENEMY,HIT);
}
static uint32_t color(int material, int index) { return MATERIALS+material*0x39c+0xa4+index*16; }
static uint32_t fog(int material) { return FOG+material*0x40; }
static uint8_t original_materials[3*0x39c], original_fog[3*0x40];
static void restored(void) {
    assert(!memcmp(ppc_ptr(MATERIALS),original_materials,sizeof original_materials));
    assert(!memcmp(ppc_ptr(FOG),original_fog,sizeof original_fog));
}
static void prepare(int active) {
    hook(0x027f4f1c,2,MODEL,0);
    if (active) {
        for (int m=0;m<3;++m) {
            assert(ld8(fog(m))==2);
            assert((float)ldf32(fog(m)+4)==-10000.0f);
            assert((float)ldf32(fog(m)+8)==-9999.0f);
            assert(ld8(fog(m)+0x14)==(m ? 255 : 2));
            assert(ld8(fog(m)+0x15)==(m ? 0 : 2));
            assert(ld8(fog(m)+0x16)==(m ? 0 : 3));
            assert(ld8(fog(m)+0x17)==255);
            assert((float)ldf32(fog(m)+12)==1.0f && (float)ldf32(fog(m)+16)==100000.0f);
        }
        /* No lighting registers, override flags, texture/alpha data touched. */
        assert(!memcmp(ppc_ptr(MATERIALS),original_materials,sizeof original_materials));
    } else restored();
    hook(0x027f4f1c,3,MODEL,0);
    restored();
}
int main(int argc, char** argv) {
    assert(argc==2);
    void* memory=mmap(PPC_MEM_BASE,0x100000000ull,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE|MAP_NORESERVE,-1,0);
    assert(memory==PPC_MEM_BASE);
    void* library=dlopen(argv[1],RTLD_NOW);
    if (!library) { fprintf(stderr,"%s\n",dlerror()); return 1; }
    WWHDGuestInitV1 init=(WWHDGuestInitV1)dlsym(library,WWHD_GUEST_INIT_SYMBOL); assert(init);
    volatile int preempt[3]={0};
    WWHDGuestHostV1 host={0}; host.size=sizeof host; host.abi_version=WWHD_GUEST_ABI_VERSION;
    host.core_preempt=preempt; host.service=service; host.dispatch=dispatch;
    module=init(&host); assert(module);
    memcpy(ppc_ptr(module->mem_base),module->image,module->image_size);
    for (unsigned i=0; i<module->hook_count; ++i) {
        assert(module->hooks[i].kind!=WWHD_GUEST_REPLACE);
        assert(module->hooks[i].target!=0x027f55fc && module->hooks[i].target!=0x027f583c);
    }
    st32(PLAY+0x5b2c,LINK); st32(LINK+4,42); st32(0x101f84dc,SAVE);
    st16(SAVE+0x20,80); st16(SAVE+0x22,80); st8(ENEMY+0x2da,2);
    st32(HIT,LINK+0x7b1c); st32(HIT+4,LINK); st8(HIT+10,1); st32(LINK+0x7b1c+0x10,2);
    tick(); assert(filter && hud && touch);
    buttons(0); assert(claim_active);
    elements=0; execute(hud,1,0); assert(elements>=5);
    for (int i=0; i<20; ++i) hit(0);
    buttons(0x60000); tick(); assert(damage(3)==3); /* blocked hits give no charge */
    buttons(0);
    for (int i=0; i<19; ++i) hit(1);
    buttons(0x60000); tick(); assert(damage(3)==3); /* need twenty */
    buttons(0); hit(1); buttons(0x60000); tick(); assert(damage(3)==6);
    buttons(0x60000); tick(); assert(damage(3)==6); /* held combo doesn't toggle again */
    assert(ld32(INPUT)==0); assert(damage(3)==6); /* no cumulative scaling */
    st32(HIT,LINK+0x200); assert(damage(3)==3); st32(HIT,LINK+0x7b1c);
    st32(HIT+4,ENEMY); assert(damage(3)==3); st32(HIT+4,LINK);
    assert(damage(100)==127); assert(damage(0)==0);
    st8(LINK+0x3ac,8); st8(LINK+0x69e8,1); assert(damage(100)==63);
    st8(LINK+0x3ac,0); st8(LINK+0x69e8,0);

    st32(LINK+0x448,MODEL);
    st32(MODEL+0xac,DATA); st32(DATA+0x0c,3); st32(DATA+0x10,MATERIALS);
    st32(DATA,RESOURCE); st32(RESOURCE+0x18,0x100-0x18);
    st32(0x1046cd2c,NAMES); st32(0x1046cd34,NAMES+32);
    /* No eye controller pointers: name resolution must identify BOTH eyes. */
    for (int m=0; m<3; ++m) for (int k=0; k<11; ++k) {
        stf32(color(m,k),0.25f+k*0.01f); stf32(color(m,k)+4,0.5f);
        stf32(color(m,k)+8,0.75f); stf32(color(m,k)+12,0.4f+m*0.1f);
    }
    /* Use nonzero override masks and sentinel bytes. The old code left 0x7ff
     * set permanently; these assertions check the exact original material. */
    st32(VTABLE+0x14,0x02001000);
    for (int m=0;m<3;++m) {
        st32(MATERIALS+m*0x39c+0x20,PE+m*0x40);
        st32(PE+m*0x40,VTABLE); st32(PE+m*0x40+4,fog(m));
        st32(MATERIALS+m*0x39c+0xa0,0xa0000090u+m);
        st32(fog(m),0x02aabbcc); /* includes padding, restored bit for bit */
        stf32(fog(m)+4,500.0f); stf32(fog(m)+8,5000.0f);
        stf32(fog(m)+12,1.0f); stf32(fog(m)+16,100000.0f);
        st32(fog(m)+20,0xabcdef80);
    }
    memcpy(original_materials,ppc_ptr(MATERIALS),sizeof original_materials);
    memcpy(original_fog,ppc_ptr(FOG),sizeof original_fog);
    prepare(1); assert(name_lookups==2);
    /* Nested entry/update returns restore the outer scope exactly once. */
    hook(0x027f4f1c,2,MODEL,0);
    hook(0x027f4fe4,2,MODEL,0); hook(0x027f4fe4,3,MODEL,0);
    assert(ld8(FOG+0x14)==2 && ld8(fog(1)+0x14)==255);
    hook(0x027f4f1c,3,MODEL,0); restored();
    hook(0x027f4f1c,2,MODEL+0x400,0); restored();
    hook(0x027f4f1c,3,MODEL+0x400,0); restored();
    for (int i=0;i<20;++i) hook(0x027f4f1c,2,MODEL,0);
    for (int i=0;i<20;++i) hook(0x027f4f1c,3,MODEL,0);
    restored(); /* bounded scope overflow */
    eye_indices[0]=-1; eye_indices[1]=3;
    hook(0x027f4f1c,2,MODEL,0);
    for (int m=0;m<3;++m) assert(ld8(fog(m)+0x14)==2);
    hook(0x027f4f1c,3,MODEL,0); restored();
    st32(LINK+0x604,EYE_ANIM); st32(MATERIALS+0x39c+0x24,EYE_ANIM);
    hook(0x027f4f1c,2,MODEL,0); assert(ld8(fog(1)+0x14)==255); /* controller fallback */
    hook(0x027f4f1c,3,MODEL,0);
    st32(LINK+0x604,0); st32(MATERIALS+0x39c+0x24,0); restored();
    eye_indices[0]=1; eye_indices[1]=2;
    st8(LINK+0x3be,8); prepare(0); st8(LINK+0x3be,0); /* frozen */
    st32(DATA+0x0c,97); prepare(0); st32(DATA+0x0c,3); /* count bound */
    /* No fog info, no PE block, and unsupported fog modes fail closed. */
    st32(PE+4,0); st32(MATERIALS+0x39c+0x20,0); st8(fog(2),4);
    hook(0x027f4f1c,2,MODEL,0);
    assert(ld32(FOG+20)==0xabcdef80 && ld32(fog(1)+20)==0xabcdef80 && ld8(fog(2))==4);
    hook(0x027f4f1c,3,MODEL,0);
    st32(PE+4,FOG); st32(MATERIALS+0x39c+0x20,PE+0x40); st8(fog(2),2); restored();
    /* Multiple activations/cancellations: BOTH gameplay and next material pass
     * must be normal after the second button edge (including free mode below). */
    buttons(0); buttons(0x60000); tick(); assert(damage(3)==3); prepare(0);
    buttons(0); for (int i=0;i<20;++i) hit(1);
    buttons(0x60000); tick(); assert(damage(3)==6); prepare(1);

    paused=1; buttons(0); assert(!claim_active); elements=0; execute(hud,1,0); assert(!elements);
    for (int i=0;i<900;++i) tick();
    paused=0; tick(); assert(damage(3)==6); /* timer suspended */
    st8(PLAY+0x5292,1); buttons(0); assert(!claim_active);
    for (int i=0;i<900;++i) tick();
    st8(PLAY+0x5292,0); tick(); assert(damage(3)==6);
    st8(PLAY+0x514c,1); tick(); assert(damage(3)==3); prepare(0); /* stage transition resets */
    st8(PLAY+0x514c,0); free_mode=1; ++revision; tick();
    buttons(0); buttons(0x60000); tick(); assert(damage(3)==6);
    prepare(1);
    buttons(0); buttons(0x60000); tick(); assert(damage(3)==3); prepare(0);
    buttons(0); buttons(0x60000); tick(); assert(damage(3)==6); prepare(1);
    /* DARK touch cancellation, move/up do not toggle, second tap activates. */
    st32(INPUT+0x100,0); execute(touch,INPUT+0x100,0); tick();
    assert(damage(3)==3); prepare(0);
    st32(INPUT+0x100,1); execute(touch,INPUT+0x100,0); tick();
    st32(INPUT+0x100,2); execute(touch,INPUT+0x100,0); tick();
    assert(damage(3)==3); prepare(0);
    st32(INPUT+0x100,0); execute(touch,INPUT+0x100,0); tick();
    assert(damage(3)==6); prepare(1);
    touch_visible=0; ++revision; tick(); buttons(0); assert(!claim_active);
    st16(SAVE+0x22,0); tick(); assert(damage(3)==3); prepare(0);
    st16(SAVE+0x22,80); tick(); buttons(0x60000); tick(); assert(damage(3)==6);
    hook(0x027f4f1c,2,MODEL,0);
    enabled=0; ++revision; tick(); assert(damage(3)==3);
    /* Return must restore even when active/enabled changed inside the scope. */
    hook(0x027f4f1c,3,MODEL,0); restored(); prepare(0);
    enabled=1; ++revision; tick(); buttons(0); buttons(0x60000); tick(); assert(damage(3)==6);
    hook(0x0240ee28,2,LINK,0); assert(damage(3)==3 && !claim_active); prepare(0);
    puts("Dark Link translated guest-hook tests passed");
    dlclose(library); munmap(memory,0x100000000ull);
}
