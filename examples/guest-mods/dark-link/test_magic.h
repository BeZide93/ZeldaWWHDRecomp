/* Synthetic native call/lifetime tests: no Nintendo code or GPU is executed. */
enum { MAGIC_ARC=0x11400000,MAGIC_CTRL=0x11410000,MAGIC_PANE=0x11411000,
       AURA=0x11420000,AURA_MAT=0x11421000,AURA_RES=0x11422000,AURA_RS=0x11423000,
       AURA_ANM=0x11424000,AURA_VALUE=0x11425000,
       MAGIC_MAIN=0x11430000,MAGIC_UI=0x11431000,MAGIC_HEAP=0x11432000,
       MAGIC_COPIES=0x11600000,MAGIC_RESOURCE=0x11433000 };
static uint64_t magic_generation=1;
static unsigned native_allocs,native_frees,native_finalizes,native_calcs,native_draws,native_binds;
static uint32_t native_last_copy,native_last_info;
static int native_init_fail,native_alias,native_alloc_fail;
static float native_frames[7];
static void magic_epoch(Cpu* c) { c->r[3]=(uint32_t)(magic_generation>>32);c->r[4]=(uint32_t)magic_generation; }
static void magic_release(Cpu* c) { c->r[3]=1; }
static void magic_setting(Cpu* c) { c->r[3]=0; }
static void magic_write(Cpu* c) { (void)c;assert(!"native gauge must never write PNGs"); }
static void magic_texture(Cpu* c) {
    assert(c->r[3]==0 && !strcmp((char*)ppc_ptr(c->r[4]),"assets/dark_link.png"));c->r[3]=0;
}
static void magic_emit(uint32_t e) { (void)e;assert(!"no gauge images in the host overlay"); }
static void magic_init_hook(uint32_t owner,uint32_t resource,uint32_t option,uint32_t desc) {
    for(uint32_t i=0;i<module->hook_count;++i) {
        const WWHDGuestHook* h=&module->hooks[i];if(h->target!=0x02675294 || h->kind!=3)continue;
        Cpu c={0};c.r[1]=0x1201fff0;c.r[3]=owner;c.r[4]=resource;c.r[5]=option;c.r[6]=desc;
        for(uint32_t j=0;j<module->func_count;++j)if(module->funcs[j].addr==h->func){module->funcs[j].fn(&c);return;}
    }assert(!"native init hook missing");
}
static void native_objects(uint32_t p,int source) {
    memset(ppc_ptr(p),0,0x1000);
    uint32_t layout=p+0x100,accessor=p+0x300,root=p+0x500,set=p+0x700,entries=p+0x800;
    st32(p+4,0x100f7548);st32(p+0x44,layout);st32(layout+4,accessor);st32(accessor+0xc,root);
    st32(layout+0xd4,set);st32(set+4,7);st32(set+8,entries);st32(set+12,5);st32(set+16,p+0x840);
    for(unsigned i=0;i<7;++i)if(source || i>=2) {
        uint32_t e=p+0x900+i*0x40;st32(entries+i*4,e);st32(e+0x34,p+0xb00+i*0x30);
    }
    st32(root+0xc,source?MAGIC_MAIN:0);stf32(root+0x48,1);stf32(root+0x5c,1);stf32(root+0x70,1);
    stf32(root+0x54,-530);stf32(root+0x64,280);
    st32(p+0x58,root+0x100);st32(p+0x5c,MAGIC_PANE);st32(p+0x60,MAGIC_PANE);
}
static void fixture_magic(void) {
    magic_archive=MAGIC_ARC;memset(ppc_ptr(MAGIC_ARC),0,0x1000);
    st32(MAGIC_ARC,0x53415243);st16(MAGIC_ARC+4,0x14);st32(MAGIC_ARC+8,0x1000);st32(MAGIC_ARC+12,0x100);
    st32(MAGIC_ARC+0x14,0x53464154);st16(MAGIC_ARC+0x18,12);st16(MAGIC_ARC+0x1a,3);
    for(unsigned i=0;i<3;++i) {
        uint32_t node=MAGIC_ARC+0x20+i*16,f=MAGIC_ARC+0x100+i*0x100,pat=f+20,pai=f+116;
        st32(node+8,i*0x100);st32(node+12,i*0x100+136);
        st32(f,0x464c414e);st16(f+4,0xfeff);st16(f+6,20);st16(f+16,2);
        st32(pat,0x70617431);st32(pat+4,96);st16(pat+10,1);st32(pat+12,28);st32(pat+16,64);
        strcpy((char*)ppc_ptr(pat+28),i==0?"Full16":i==1?"Full32":"InAll");
        strcpy((char*)ppc_ptr(pat+64),i<2?"G_Bar_00":"G_All_00");
        st32(pai,0x70616931);st32(pai+4,20);st16(pai+8,i==0?16:i==1?32:12);
    }
    native_objects(MAGIC_CTRL,1);
    memset(ppc_ptr(MAGIC_PANE),0,0x100);stf32(MAGIC_PANE+0x3c,96);stf32(MAGIC_PANE+0x40,16);
    stf32(MAGIC_PANE+0x48,1);stf32(MAGIC_PANE+0x5c,1);stf32(MAGIC_PANE+0x54,-530);stf32(MAGIC_PANE+0x64,280);
    st32(0x1018c404,MAGIC_UI);st32(MAGIC_UI+0x10,MAGIC_HEAP);st32(0x101f7274,0x11434000);
    st32(MAGIC_MAIN+4,0x100f7a48);
    magic_init_hook(MAGIC_CTRL,MAGIC_RESOURCE,2,0x11435000);
}
static int magic_dispatch(Cpu* c) {
    uint32_t p=c->r[3];
    switch(c->pc) {
    case 0x0273b050:
        assert(p==0x8c && c->r[4]==MAGIC_HEAP && c->r[5]==4);
        if(native_alloc_fail){c->r[3]=0;return 1;}
        c->r[3]=MAGIC_COPIES+(++native_allocs)*0x10000;assert(native_allocs<100);return 1;
    case 0x0267516c:
        assert(p>=MAGIC_COPIES);memset(ppc_ptr(p),0,0x8c);st32(p+4,0x100f7548);return 1;
    case 0x02675294:
        assert(p>=MAGIC_COPIES && c->r[4]==MAGIC_RESOURCE && c->r[5]==2 && !c->r[6]);
        native_objects(p,0);if(native_alias)st32(p+0x44,MAGIC_CTRL+0x100);
        magic_init_hook(p,c->r[4],c->r[5],0); /* recursive native entry is ignored */
        native_last_copy=p;c->r[3]=!native_init_fail;return 1;
    case 0x02004e04: {
        unsigned i=c->r[4];assert(i<2);
        assert(!strcmp((char*)ppc_ptr(ld32(c->r[5])),i?"Full32":"Full16"));
        assert(!strcmp((char*)ppc_ptr(ld32(c->r[6])),"G_Bar_00"));
        assert(ld32(c->r[5]+4)==0x100f737c && ld32(c->r[6]+4)==0x100f737c);
        uint32_t object=p-0x700,e=object+0x900+i*0x40;
        st32(ld32(p+8)+i*4,e);st32(e+0x34,object+0xb00+i*0x30);++native_binds;c->r[3]=1;return 1;
    }
    case 0x02674df0:
        assert(p>=MAGIC_COPIES && c->r[4]<7);native_frames[c->r[4]]=c->f[1].ps0;return 1;
    case 0x026f8cd0:
        assert(p>=MAGIC_COPIES);++native_calcs;
        hook(0x026f8cd0,2,p,0);hook(0x026f8cd0,3,p,0);return 1;
    case 0x026f8d08:
        assert(p>=MAGIC_COPIES);++native_draws;native_last_copy=p;native_last_info=c->r[4];
        hook(0x026f8d08,3,p,c->r[4]); /* copy must not draw recursively */
        return 1;
    case 0x026f8b00:
        assert(p>=MAGIC_COPIES);++native_finalizes;hook(0x026f8b00,2,p,0);return 1;
    case 0x02674d84:
        assert(p>=MAGIC_COPIES && c->r[4]==1);++native_frees;hook(0x02674d84,2,p,1);return 1;
    default:return 0;
    }
}
static void native_calculate(void) { hook(0x026f8cd0,3,MAGIC_MAIN,0); }
static void test_magic(void) {
    fixture_magic();assert(native_allocs==1 && native_binds==2 && native_frames[2]==12);
    uint8_t original[0x1000],saved[0x40];memcpy(original,ppc_ptr(MAGIC_CTRL),sizeof original);memcpy(saved,ppc_ptr(SAVE),sizeof saved);
    st8(SAVE+0x33,16);native_calculate();uint32_t copy=native_last_copy;
    assert(native_frames[0]==16 && ldf32(copy+0x500+0x20)==256);
    unsigned draws=native_draws;hook(0x026f8d08,3,MAGIC_CTRL,0x12345678);
    assert(native_draws==draws+1 && native_last_info==0x12345678);
    st8(SAVE+0x33,32);native_calculate();assert(native_frames[1]==32);
    /* Original matrices, materials/animation pointers remain byte-identical. */
    assert(!memcmp(original,ppc_ptr(MAGIC_CTRL),sizeof original));
    st8(SAVE+0x33,saved[0x33]);assert(!memcmp(saved,ppc_ptr(SAVE),sizeof saved));
    draws=native_draws;read_hud();assert(native_draws==draws); /* overlay cannot call graphics */
    paused=1;hook(0x026f8d08,3,MAGIC_CTRL,1);paused=0;
    menu=1;hook(0x026f8d08,3,MAGIC_CTRL,1);menu=0;
    show_gauge=0;++revision;tick();hook(0x026f8d08,3,MAGIC_CTRL,1);assert(native_draws==draws);
    show_gauge=1;++revision;tick();
    unsigned allocated=native_allocs;++magic_generation;native_calculate();
    hook(0x026f8d08,3,MAGIC_CTRL,99);assert(native_allocs==allocated && native_draws==draws+1); /* state/image epoch */
    /* A second owner gets a distinct layout and its own draw context. */
    uint32_t owner2=MAGIC_CTRL+0x4000;native_objects(owner2,1);
    magic_init_hook(owner2,MAGIC_RESOURCE,2,1);uint32_t copy2=native_last_copy;assert(copy2!=copy);
    native_calculate();hook(0x026f8d08,3,owner2,77);assert(native_last_copy==copy2 && native_last_info==77);
    hook(0x026f8b00,2,owner2,0);unsigned freed=native_frees;
    hook(0x02674d84,2,owner2,1);assert(native_frees==freed); /* finalize then destructor: once */
    draws=native_draws;hook(0x026f8d08,3,owner2,77);assert(native_draws==draws);
    /* Failed allocation/load, aliased roots and malformed metadata never draw. */
    for(unsigned mode=0;mode<4;++mode) {
        hook(0x026f8b00,2,MAGIC_CTRL,0);
        native_alloc_fail=mode==0;native_init_fail=mode==1;native_alias=mode==2;
        if(mode==3)st32(MAGIC_ARC+0x100+20+12,0xfffffffc);
        magic_init_hook(MAGIC_CTRL,MAGIC_RESOURCE,2,1);native_calculate();draws=native_draws;
        hook(0x026f8d08,3,MAGIC_CTRL,42);assert(native_draws==draws);
        native_alloc_fail=native_init_fail=native_alias=0;
        assert(!memcmp(original,ppc_ptr(MAGIC_CTRL),sizeof original));
    }
    hook(0x026f8b00,2,MAGIC_CTRL,0);fixture_magic();native_calculate();
    st32(MAGIC_CTRL+0x500+0x48,0x7fc00000);native_calculate();draws=native_draws;
    hook(0x026f8d08,3,MAGIC_CTRL,42);assert(native_draws==draws);
    stf32(MAGIC_CTRL+0x500+0x48,1);native_calculate();
    puts("Dark Link native gauge: independent objects, bindings, draw context, visibility, lifetime and failure tests passed");
}
