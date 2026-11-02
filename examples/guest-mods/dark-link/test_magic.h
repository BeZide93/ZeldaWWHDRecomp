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
static uint32_t magic_fill_handles[2];
static int magic_writes,magic_base_fill,magic_animated,magic_bad_animation,magic_long;
static void magic_write(Cpu* c) {
    const char* path=(const char*)ppc_ptr(c->r[3]);
    const uint8_t* png=ppc_ptr(c->r[4]);
    assert(c->r[5]>100 && !memcmp(png,"\x89PNG\r\n\x1a\n",8));
    /* First pixel of the uncompressed zlib stream: filter byte then RGBA.
     * Validate output pixels, not only a red tint in an emitted HUD command. */
    assert(png[24]==8 && png[25]==6 && png[48]==0);
    if(strstr(path,"fill")) { for(int i=0;i<4;++i) assert(png[49+i*4]==255 && png[50+i*4]==32 && png[51+i*4]==32);assert(png[52]==255); }
    if(magic_base_fill && strstr(path,"frame")) assert(png[49]==0&&png[50]==0&&png[51]==0);
    ++magic_writes;c->r[3]=c->r[5];
}
static void magic_texture(Cpu* c) {
    assert(c->r[3]==1); /* owned, recoloured native artwork in mod Data */
    const char* path=(const char*)ppc_ptr(c->r[4]);
    assert(!strncmp(path,"magic-",6));
    ++magic_textures; c->r[3]=++magic_images;
    if(strstr(path,"fill")) magic_fill_handles[path[6]=='1']=c->r[3];
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
    stf32(p+16,fill ? (magic_animated?48:96):(magic_long&&!strcmp(name,"P_MagicBase_01")?200:100)); stf32(p+20,fill ? 12:16); st32(p+28,255);
    strcpy((char*)ppc_ptr(p+32),fill ? "fill":"frame"); c->r[3]=64;
}
static void magic_emit(uint32_t e) {
    ++magic_draws;
    assert(ld32(e+4)==(magic_authored ? 1u:0u)); /* follows native, already aspect-adjusted coordinates */
    if (ld32(e+60)==magic_fill_handles[0] || ld32(e+60)==magic_fill_handles[1]) {
        magic_width=ldf32(e+20); magic_x=ldf32(e+12); magic_y=ldf32(e+16);
        assert(ld32(e+56)==0xffffffff); /* red pixels must not be tinted a second time */
        float end=magic_width/(magic_base_fill?100.0f:magic_animated&&magic_bad_animation?48.0f:magic_long?196.0f:96.0f);
        if(magic_animated && !magic_bad_animation) { assert(ldf32(e+40)==0.25f);end=0.25f+end*0.5f; }
        float delta=(float)ldf32(e+48)-end; assert(delta>-0.00001f && delta<0.00001f);
    } else assert(ld32(e+56)==0xffffffff);
}
static void fixture_picture(uint32_t p,const char* name,unsigned mat) {
    st32(p,0x70696331); st32(p+4,0x88); strcpy((char*)ppc_ptr(p+12),name);
    st16(p+0x64,mat); st8(p+0x66,1);
    stf32(p+0x44,1);stf32(p+0x48,1);
    stf32(p+0x4c,mat?(magic_animated?48:96):100);stf32(p+0x50,mat?12:16);
    for(unsigned i=0;i<4;++i) st32(p+0x54+4*i,0xffffffff);
    const float uv[8]={0,0,1,0,0,1,1,1};
    for(unsigned i=0;i<8;++i) stf32(p+0x68+4*i,uv[i]);
}
static void fixture_magic(void) {
    uint32_t f=MAGIC_FLYT;
    st32(MAGIC_ARC,0x53415243); st16(MAGIC_ARC+4,0x14);
    st32(MAGIC_ARC+8,0xc00); st32(MAGIC_ARC+12,0x100);
    st32(MAGIC_ARC+0x14,0x53464154); st16(MAGIC_ARC+0x18,12); st16(MAGIC_ARC+0x1a,3);
    st32(MAGIC_ARC+0x24,0x01000000); st32(MAGIC_ARC+0x28,0); st32(MAGIC_ARC+0x2c,0x1f0);
    st32(MAGIC_ARC+0x34,0x01000006);st32(MAGIC_ARC+0x38,0x300);st32(MAGIC_ARC+0x3c,0x728);
    st32(MAGIC_ARC+0x44,0x0100000b);st32(MAGIC_ARC+0x48,0x800);st32(MAGIC_ARC+0x4c,0xa28);
    st32(MAGIC_ARC+0x50,0x53464e54); st16(MAGIC_ARC+0x54,8);
    strcpy((char*)ppc_ptr(MAGIC_ARC+0x58),"blyt/MagicBar_00.bflyt");
    strcpy((char*)ppc_ptr(MAGIC_ARC+0x70),"timg/frame.bflim");
    strcpy((char*)ppc_ptr(MAGIC_ARC+0x84),"timg/fill.bflim");
    /* RGBA frame and BC4 alpha fill, both native linear tiling (pitch 64). */
    uint32_t frame=MAGIC_ARC+0x400,fill=MAGIC_ARC+0x900;
    memset(ppc_ptr(frame),255,1024);memset(ppc_ptr(fill),0,512);st8(fill,255);
    uint32_t trailers[2]={frame+1024,fill+512};
    for(int i=0;i<2;++i) {
        uint32_t t=trailers[i];st32(t,0x464c494d);st32(t+20,0x696d6167);
        st16(t+28,4);st16(t+30,4);st8(t+34,i?0x10:9);st8(t+35,1);st32(t+36,i?512:1024);
    }
    st32(f,0x464c5954); st16(f+4,0xfeff); st16(f+6,20); st16(f+16,4);
    uint32_t mat=f+20; st32(mat,0x6d617431); st32(mat+4,100); st16(mat+8,2);
    st32(mat+12,20); st32(mat+16,60);
    strcpy((char*)ppc_ptr(mat+20),"Frame"); st32(mat+20+32,0xffffffff);
    strcpy((char*)ppc_ptr(mat+60),"Fill"); st32(mat+60+32,0x20ff20ff);
    fixture_picture(f+120,"P_MagicBase_00",0);
    fixture_picture(f+256,"P_MagicBase_01",0);
    fixture_picture(f+392,"P_Fill_00",1);
    st32(MAGIC_ARC+0x2c,528); st32(MAGIC_ARC+8,0xc00);
    st32(0x101f7274,0x11430000); magic_archive=MAGIC_ARC;
    st32(MAGIC_CTRL+0x5c,MAGIC_PANE); st32(MAGIC_CTRL+0x60,MAGIC_PANE);
    stf32(MAGIC_PANE+0x3c,100); stf32(MAGIC_PANE+0x40,16);
    stf32(MAGIC_PANE+0x48,1); stf32(MAGIC_PANE+0x5c,1);
    stf32(MAGIC_PANE+0x54,-530); stf32(MAGIC_PANE+0x64,280);
}
static void fixture_animation(unsigned frames) {
    /* Fourth SARC node, preserving existing names after the SFNT moves. */
    memmove(ppc_ptr(MAGIC_ARC+0x60),ppc_ptr(MAGIC_ARC+0x50),0x48);
    st16(MAGIC_ARC+0x1a,4);st32(MAGIC_ARC+8,0x2000);
    st32(MAGIC_ARC+0x54,0);st32(MAGIC_ARC+0x58,0xb00);st32(MAGIC_ARC+0x5c,0xb00+280);
    uint32_t f=MAGIC_ARC+0xc00,pat=f+20,pai=f+84;
    memset(ppc_ptr(f),0,280);st32(f,0x464c414e);st16(f+4,0xfeff);st16(f+6,20);st16(f+16,2);
    st32(pat,0x70617431);st32(pat+4,64);st16(pat+10,1);st32(pat+12,28);st32(pat+16,36);
    st16(pat+22,frames);strcpy((char*)ppc_ptr(pat+28),"Full");strcpy((char*)ppc_ptr(pat+36),"G_Bar_00");
    st32(pai,0x70616931);st32(pai+4,196);st16(pai+8,frames);st16(pai+14,2);st32(pai+16,20);
    st32(pai+20,28);st32(pai+24,112);
    for(int i=0;i<2;++i) {
        uint32_t c=pai+28+i*84,t=c+36,e=t+12,k=e+12;
        strcpy((char*)ppc_ptr(c),i?"Fill":"P_Fill_00");st8(c+28,1);st8(c+29,i);st32(c+32,36);
        st32(t,i?0x464c5453:0x464c5041);st8(t+4,1);st32(t+8,12);
        st8(e+1,i?3:8);st8(e+2,2);st16(e+4,2);st32(e+8,12);
        stf32(k,0);stf32(k+4,i?1:48);stf32(k+8,i?-0.5f/frames:((frames==32?196.0f:96.0f)-48)/frames);
        stf32(k+12,frames);stf32(k+16,i?0.5f:frames==32?196:96);stf32(k+20,i?-0.5f/frames:((frames==32?196.0f:96.0f)-48)/frames);
    }
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
    /* A coloured MagicBase is the full-width fill, not the smaller highlight. */
    magic_base_fill=1;st32(MAGIC_FLYT+20+20+32,0x20ff20ff);++magic_generation;
    read_hud();hook(0x02675414,3,MAGIC_CTRL,0);read_hud();
    assert(magic_width>95 && magic_width<=100 && magic_x==60);
    magic_base_fill=0;fixture_magic();++magic_generation;
    read_hud();hook(0x02675414,3,MAGIC_CTRL,0);read_hud();
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
    /* Native authored half-width fill becomes full-width at frame 16;
     * its texture scale changes too. Free mode must cover the complete bar. */
    magic_animated=1;free_mode=1;++revision;tick();fixture_magic();fixture_animation(16);++magic_generation;
    read_hud();hook(0x02675414,3,MAGIC_CTRL,0);read_hud();assert(magic_width==96 && magic_x==62);
    expect_hud("Full=1/0");expect_hud("Fill=100%");
    /* Malformed keys cannot partially apply an animation or read outside it. */
    magic_bad_animation=1;st32(MAGIC_ARC+0xc00+84+28+36+12+8,0xfffffff0);++magic_generation;
    read_hud();hook(0x02675414,3,MAGIC_CTRL,0);read_hud();assert(magic_width==48);
    expect_hud("Full=0/0");
    magic_bad_animation=0;magic_long=1;st8(SAVE+0x33,32);fixture_magic();fixture_animation(32);
    stf32(MAGIC_FLYT+256+0x4c,200);stf32(MAGIC_PANE+0x3c,200);++magic_generation;
    read_hud();hook(0x02675414,3,MAGIC_CTRL,0);read_hud();assert(magic_width==196);
    expect_hud("Full=0/1");
    magic_long=0;st8(SAVE+0x33,0);
    magic_animated=magic_bad_animation=0;free_mode=0;++revision;tick();fixture_magic();++magic_generation;
    read_hud();hook(0x02675414,3,MAGIC_CTRL,0);read_hud();
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
        st32(b+0x10,a); st32(b+0x20,v); st16(b+0xa,0); /* native init(play=FALSE): no playback limit */
        st32(a+4,AURA_ANM+0x300);st32(AURA_ANM+0x314,0x02000008);
        stf32(a,0); stf32(b+4,0); stf32(v,0); st32(v+0x10,0x02000004);
    }
    uint8_t original_save[0x40]; memcpy(original_save,ppc_ptr(SAVE),sizeof original_save);
    uint32_t flags=ld32(LINK+0x3bc); unsigned timer=ld16(LINK+0x699e);
    hook(0x0240b7f4,2,LINK,0);
    assert((float)ldf32(AURA_ANM)>30 && (ld32(AURA_RS+0x1c)&0x1fff)==0x500);
    assert(ld32(LINK+0x3bc)==flags && ld16(LINK+0x699e)==timer);
    hook(0x0240b7f4,3,LINK,0); assert((float)ldf32(AURA_ANM)==0);
    hook(0x023dbbe4,2,LINK,0); assert((float)ldf32(AURA_ANM)>29);
    hook(0x023dbbe4,3,LINK,0); assert((float)ldf32(AURA_ANM)==0 && (float)ldf32(AURA_VALUE)==0);
    assert(aura_notifications>=6 && !memcmp(original_save,ppc_ptr(SAVE),sizeof original_save));
    st32(AURA_RS+0x1c,0x504); /* native non-separate alpha uses RGB equation */
    hook(0x0240b7f4,2,LINK,0);
    assert((ld32(AURA_RS+0x1c)&0x1fff)==0x500);
    assert((ld32(AURA_RS+0x1c)&(1u<<29)) && ((ld32(AURA_RS+0x1c)>>16)&0x1fff)==0x504);
    hook(0x0240b7f4,3,LINK,0);
    /* Return native separate-alpha fixture before restoration assertions. */
    st32(AURA_RS+0x1c,0x20010101);
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
