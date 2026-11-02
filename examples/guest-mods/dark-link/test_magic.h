/* Synthetic layout resources and native aura objects; no game artwork. */
enum { MAGIC_ARC=0x11400000, MAGIC_FLYT=MAGIC_ARC+0x100, MAGIC_CTRL=0x11410000,
       MAGIC_PANE=0x11411000, AURA=0x11420000, AURA_MAT=0x11421000,
       AURA_RES=0x11422000, AURA_RS=0x11423000, AURA_ANM=0x11424000,
       AURA_VALUE=0x11425000 };
static uint64_t magic_generation=1;
static int magic_images, magic_draws, magic_textures, magic_authored;
static float magic_width, magic_x, magic_y;
static void magic_release(Cpu* c) { c->r[3]=1; }
static void magic_epoch(Cpu* c) { c->r[3]=(uint32_t)(magic_generation>>32); c->r[4]=(uint32_t)magic_generation; }
static void magic_texture(Cpu* c) {
    assert(c->r[3]==2);
    const char* path=(const char*)ppc_ptr(c->r[4]);
    assert(!strcmp(path,"MagicBar_00/frame#Frame") || !strcmp(path,"MagicBar_00/fill"));
    ++magic_textures; c->r[3]=++magic_images;
}
static void magic_setting(Cpu* c) {
    const char* key=(const char*)ppc_ptr(c->r[3]);
    if (magic_authored && !strcmp(key,"layout-pane:Main_00/L_MagicBar_00")) {
        uint32_t p=c->r[5]; memset(ppc_ptr(p),0,64); st32(p,1);
        stf32(p+8,-530); stf32(p+12,280); c->r[3]=64; return;
    }
    if (strncmp(key,"layout-pane:MagicBar_00/",24)) { c->r[3]=0; return; }
    assert(c->r[4]==6 && c->r[6]==64);
    const char* name=key+24;
    int fill=!strcmp(name,"P_Fill_00");
    if (!fill && strcmp(name,"P_MagicBase_00") && strcmp(name,"P_MagicBase_01")) { c->r[3]=0; return; }
    uint32_t p=c->r[5]; memset(ppc_ptr(p),0,64);
    st32(p,1); st32(p+4,3); stf32(p+8,0); stf32(p+12,0);
    stf32(p+16,fill ? 96:100); stf32(p+20,fill ? 12:16); st32(p+28,255);
    strcpy((char*)ppc_ptr(p+32),fill ? "fill":"frame"); c->r[3]=64;
}
static void magic_emit(uint32_t e) {
    ++magic_draws;
    assert(ld32(e+4)==(magic_authored ? 1u:0u)); /* follows native, already aspect-adjusted coordinates */
    if (ld32(e+56)==0xff2020ff) {
        magic_width=ldf32(e+20); magic_x=ldf32(e+12); magic_y=ldf32(e+16);
        float delta=(float)ldf32(e+48)-magic_width/96.0f; assert(delta>-0.00001f && delta<0.00001f);
    } else assert(ld32(e+56)==0xffffffff);
}
static void fixture_picture(uint32_t p,const char* name,unsigned mat) {
    st32(p,0x70696331); st32(p+4,0x88); strcpy((char*)ppc_ptr(p+12),name);
    st16(p+0x64,mat); st8(p+0x66,1);
    for(unsigned i=0;i<4;++i) st32(p+0x54+4*i,0xffffffff);
    const float uv[8]={0,0,1,0,0,1,1,1};
    for(unsigned i=0;i<8;++i) stf32(p+0x68+4*i,uv[i]);
}
static void fixture_magic(void) {
    uint32_t f=MAGIC_FLYT;
    st32(MAGIC_ARC,0x53415243); st16(MAGIC_ARC+4,0x14);
    st32(MAGIC_ARC+8,0x400); st32(MAGIC_ARC+12,0x100);
    st32(MAGIC_ARC+0x14,0x53464154); st16(MAGIC_ARC+0x18,12); st16(MAGIC_ARC+0x1a,1);
    st32(MAGIC_ARC+0x24,0x01000000); st32(MAGIC_ARC+0x28,0); st32(MAGIC_ARC+0x2c,0x1f0);
    st32(MAGIC_ARC+0x30,0x53464e54); st16(MAGIC_ARC+0x34,8);
    strcpy((char*)ppc_ptr(MAGIC_ARC+0x38),"blyt/MagicBar_00.bflyt");
    st32(f,0x464c5954); st16(f+4,0xfeff); st16(f+6,20); st16(f+16,4);
    uint32_t mat=f+20; st32(mat,0x6d617431); st32(mat+4,100); st16(mat+8,2);
    st32(mat+12,20); st32(mat+16,60);
    strcpy((char*)ppc_ptr(mat+20),"Frame"); st32(mat+20+32,0xffffffff);
    strcpy((char*)ppc_ptr(mat+60),"Fill"); st32(mat+60+32,0x20ff20ff);
    fixture_picture(f+120,"P_MagicBase_00",0);
    fixture_picture(f+256,"P_MagicBase_01",0);
    fixture_picture(f+392,"P_Fill_00",1);
    st32(MAGIC_ARC+0x2c,528); st32(MAGIC_ARC+8,0x100+528);
    st32(0x101f7274,0x11430000); magic_archive=MAGIC_ARC;
    st32(MAGIC_CTRL+0x5c,MAGIC_PANE); st32(MAGIC_CTRL+0x60,MAGIC_PANE);
    stf32(MAGIC_PANE+0x3c,100); stf32(MAGIC_PANE+0x40,16);
    stf32(MAGIC_PANE+0x48,1); stf32(MAGIC_PANE+0x5c,1);
    stf32(MAGIC_PANE+0x54,-530); stf32(MAGIC_PANE+0x64,280);
}
static void test_magic(void) {
    fixture_magic();
    /* Invalid resource is rejected with no texture handles or fake rectangle. */
    st32(MAGIC_FLYT+24,0xfffffffc); ++magic_generation;
    magic_draws=0; read_hud(); assert(!magic_draws);
    fixture_magic(); ++magic_generation; read_hud();
    hook(0x02675414,3,MAGIC_CTRL,0);
    magic_draws=0; read_hud(); assert(magic_draws==2);
    assert(magic_width>90 && magic_width<=96 && magic_x==62 && magic_y==98);
    int prior=magic_textures; read_hud(); assert(magic_textures==prior); /* reuse handles */
    /* The magic save values do not drive Dark Link's remaining duration. */
    st8(SAVE+0x33,32); st8(SAVE+0x34,0); read_hud(); assert(magic_width>90);
    for(int i=0;i<300;++i) tick();
    read_hud(); assert(magic_width>40 && magic_width<49);
    st8(SAVE+0x33,0);
    ++magic_generation; magic_draws=0; read_hud(); assert(!magic_draws); /* fresh native position required */
    hook(0x02675414,3,MAGIC_CTRL,0); read_hud(); assert(magic_draws==2);
    magic_authored=1; ++magic_generation; magic_draws=0; read_hud();
    assert(magic_draws==2 && magic_x==62 && magic_y==98); /* before magic unlock */
    magic_authored=0; hook(0x02675414,3,MAGIC_CTRL,0);
    st8(MAGIC_PANE+0x47,5); /* native origin top-left */
    hook(0x02675414,3,MAGIC_CTRL,0); read_hud(); assert(magic_x==112 && magic_y==106);
    st8(MAGIC_PANE+0x47,0); hook(0x02675414,3,MAGIC_CTRL,0);
    /* Return a full duration for the subsequent gameplay regression. */
    buttons(0); buttons(0x60000); tick(); buttons(0);
    for(int i=0;i<20;++i) hit(1);
    buttons(0x60000); tick(); assert(damage(3)==6);
}
static void test_aura(void) {
    st32(LINK+0x5700,AURA); st16(AURA+0x2a,1); st32(AURA+0x34,AURA_MAT);
    st32(AURA_MAT,AURA_RES); st32(AURA_RES,0x464d4154);
    st32(AURA_RES+0x20,AURA_RS-(AURA_RES+0x20));
    st32(AURA_RS,7); st32(AURA_RS+0x14,0xcc0100); st32(AURA_RS+0x1c,0x20010101);
    for(int i=0;i<2;++i) {
        uint32_t b=LINK+(i ? 0x5704:0x5614),a=AURA_ANM+i*0x100,v=AURA_VALUE+i*0x100;
        st32(b+0x10,a); st32(b+0x20,v); st16(b+0xa,30);
        stf32(a,0); stf32(b+4,0); stf32(v,0); st32(v+0x10,0x02000004);
    }
    uint8_t original_save[0x40]; memcpy(original_save,ppc_ptr(SAVE),sizeof original_save);
    uint32_t flags=ld32(LINK+0x3bc); unsigned timer=ld16(LINK+0x699e);
    hook(0x0240b7f4,2,LINK,0);
    assert((float)ldf32(AURA_ANM)>30 && (ld32(AURA_RS+0x1c)&0x1fff)==0x504);
    assert(ld32(LINK+0x3bc)==flags && ld16(LINK+0x699e)==timer);
    hook(0x0240b7f4,3,LINK,0); assert((float)ldf32(AURA_ANM)==0);
    hook(0x023dbbe4,2,LINK,0); assert((float)ldf32(AURA_ANM)>29);
    hook(0x023dbbe4,3,LINK,0); assert((float)ldf32(AURA_ANM)==0 && (float)ldf32(AURA_VALUE)==0);
    assert(aura_notifications>=6 && !memcmp(original_save,ppc_ptr(SAVE),sizeof original_save));
    st32(LINK+0x3bc,flags|1); stf32(AURA_ANM,12);
    hook(0x0240b7f4,2,LINK,0); assert((float)ldf32(AURA_ANM)==12);
    hook(0x0240b7f4,3,LINK,0); assert((float)ldf32(AURA_ANM)==12);
    st32(LINK+0x3bc,flags);
    st32(AURA_RS,ld32(AURA_RS)|0x400); /* another mod's unrelated flag */
    st32(AURA_RS+0x1c,ld32(AURA_RS+0x1c)|0x80000000u);
    enabled=0; ++revision; tick();
    assert(ld32(AURA_RS)==0x407 && ld32(AURA_RS+0x14)==0xcc0100 && ld32(AURA_RS+0x1c)==0xa0010101);
    hook(0x023dbbe4,2,LINK,0); assert((float)ldf32(AURA_ANM)==12);
    hook(0x023dbbe4,3,LINK,0);
    st32(LINK+0x5700,0); enabled=1; ++revision; tick();
    buttons(0); for(int i=0;i<20;++i) hit(1); buttons(0x60000); tick(); assert(damage(3)==6);
}
