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
static uint32_t magic_fill_handles[2],magic_border_handles[2];
static int magic_window;
static int magic_require_core_alpha;
static int magic_native_hidden;
static int magic_picture_layers,magic_distinct_fill;
static uint32_t magic_detail_handle;
static unsigned magic_border_alpha=255;
static unsigned magic_frame_count=1;
static int magic_writes,magic_base_fill,magic_animated,magic_bad_animation,magic_long,magic_bad_geometry;
static void magic_write(Cpu* c) {
    const char* path=(const char*)ppc_ptr(c->r[3]);
    const uint8_t* png=ppc_ptr(c->r[4]);
    assert(c->r[5]>100 && !memcmp(png,"\x89PNG\r\n\x1a\n",8));
    /* First pixel of the uncompressed zlib stream: filter byte then RGBA.
     * Validate output pixels, not only a red tint in an emitted HUD command. */
    assert(png[24]==8 && png[25]==6 && png[48]==0);
    if(strstr(path,"fill")) { for(int i=0;i<4;++i) assert(png[49+i*4]==255 && png[50+i*4]==99 && png[51+i*4]==99);assert(png[52]==255); }
    if(magic_base_fill && strstr(path,"frame")) assert(png[49]==99&&png[50]==99&&png[51]==99);
    if(magic_require_core_alpha && !strstr(path,"border")) assert(png[52]==255);
    if(strstr(path,"border")) {
        assert(png[19]==108 && png[23]==24);
        assert(png[49]<png[49+3*4] && png[52]==magic_border_alpha); /* contrast and pane coverage retained */
        assert(png[49+12*(108*4+1)+54*4+3]==0); /* frame never fills the centre */
    }
    if(strstr(path,"detail")) {
        assert(png[49]==png[50] && png[50]==png[51] && png[52]==255);
        assert(png[49+1*17+1*4+3]==0); /* native picture's open centre survives */
    }
    ++magic_writes;c->r[3]=c->r[5];
}
static void magic_texture(Cpu* c) {
    if(c->r[3]==0) { assert(!strcmp((const char*)ppc_ptr(c->r[4]),"assets/dark_link.png")); c->r[3]=0; return; } /* fallback tested here; combined test loads the icon */
    assert(c->r[3]==1); /* owned, recoloured native artwork in mod Data */
    const char* path=(const char*)ppc_ptr(c->r[4]);
    assert(!strncmp(path,"magic-",6));
    ++magic_textures; c->r[3]=++magic_images;
    if(strstr(path,"border")) magic_border_handles[path[6]=='1']=c->r[3];
    if(strstr(path,"fill")) magic_fill_handles[path[6]=='1']=c->r[3];
    if(strstr(path,"detail")) magic_detail_handle=c->r[3];
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
    if(magic_picture_layers && (!strcmp(name,"P_Outline_00") || !strcmp(name,"P_White_00"))) {
        uint32_t p=c->r[5];memset(ppc_ptr(p),0,64);st32(p,1);st32(p+4,3);
        stf32(p+16,108);stf32(p+20,24);st32(p+28,255);
        strcpy((char*)ppc_ptr(p+32),!strcmp(name,"P_White_00")?"fill":"frame");c->r[3]=64;return;
    }
    if(magic_window && !strcmp(name,"W_Frame_00")) {
        uint32_t p=c->r[5];memset(ppc_ptr(p),0,64);st32(p,1);st32(p+4,1);
        stf32(p+16,108);stf32(p+20,24);st32(p+28,255);c->r[3]=64;return;
    }
    int fill=!strcmp(name,"P_Fill_00");
    if (!fill && strcmp(name,"P_MagicBase_00") && strcmp(name,"P_MagicBase_01")) { c->r[3]=0; return; }
    uint32_t p=c->r[5]; memset(ppc_ptr(p),0,64);
    st32(p,1); st32(p+4,magic_native_hidden?2:3); stf32(p+8,0); stf32(p+12,0);
    stf32(p+16,fill ? (magic_animated?48:96):(magic_long&&!strcmp(name,"P_MagicBase_01")?200:100)); stf32(p+20,fill ? 12:16); st32(p+28,magic_native_hidden?0:255);
    if(fill && magic_bad_geometry) stf32(p+8,100000);
    strcpy((char*)ppc_ptr(p+32),fill ? "fill":"frame"); c->r[3]=64;
}
static void magic_emit(uint32_t e) {
    ++magic_draws;
    assert(ld32(e+4)==(magic_authored ? 1u:0u)); /* follows native, already aspect-adjusted coordinates */
    if(ld32(e+60)==magic_border_handles[0] || ld32(e+60)==magic_border_handles[1]) {
        assert(ldf32(e+20)==108 && ldf32(e+24)==24);assert(ld32(e+56)==0xffffffff);return;
    }
    if (ld32(e+60)==magic_fill_handles[0] || ld32(e+60)==magic_fill_handles[1]) {
        magic_width=ldf32(e+20); magic_x=ldf32(e+12); magic_y=ldf32(e+16);
        assert(ld32(e+56)==0xffffffff); /* red pixels must not be tinted a second time */
        float end=magic_width/(magic_base_fill&&!magic_distinct_fill?100.0f:magic_animated&&magic_bad_animation?48.0f:magic_long?196.0f:96.0f);
        assert(ldf32(e+40)==0 && ldf32(e+44)==0 && ldf32(e+52)==1);
        float delta=(float)ldf32(e+48)-end; assert(delta>-0.00001f && delta<0.00001f);
    } else assert(ld32(e+56)==0xffffffff);
}
static void fixture_picture(uint32_t p,const char* name,unsigned mat) {
    st32(p,0x70696331); st32(p+4,0x88); strcpy((char*)ppc_ptr(p+12),name);
    st8(p+8,1);st8(p+10,255);
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
static void fixture_window(void) {
    /* Separate archive location avoids overwriting either source texture. */
    uint32_t f=MAGIC_ARC+0x3000,mat=f+20,txl=f+576,wnd=f+608;
    memset(ppc_ptr(f),0,800);memcpy(ppc_ptr(f),ppc_ptr(MAGIC_FLYT),20);st16(f+16,6);
    st32(mat,0x6d617431);st32(mat+4,148);st16(mat+8,3);
    st32(mat+12,24);st32(mat+16,64);st32(mat+20,104);
    memcpy(ppc_ptr(mat+24),ppc_ptr(MAGIC_FLYT+40),40);
    memcpy(ppc_ptr(mat+64),ppc_ptr(MAGIC_FLYT+80),40);
    strcpy((char*)ppc_ptr(mat+104),"Border");st32(mat+136,0xffffffff);st32(mat+140,1);
    memcpy(ppc_ptr(f+168),ppc_ptr(MAGIC_FLYT+120),408);
    st32(txl,0x74786c31);st32(txl+4,32);st16(txl+8,1);st32(txl+12,4);
    strcpy((char*)ppc_ptr(txl+16),"frame.bflim");
    st32(wnd,0x776e6431);st32(wnd+4,112+8*magic_frame_count);strcpy((char*)ppc_ptr(wnd+12),"W_Frame_00");
    st8(wnd+8,1);st8(wnd+10,255);
    stf32(wnd+0x4c,108);stf32(wnd+0x50,24);
    for(int i=0;i<4;++i) st16(wnd+0x5c+i*2,4);
    st8(wnd+0x64,magic_frame_count);st8(wnd+0x65,0x11);st32(wnd+0x6c,112);
    for(unsigned i=0;i<magic_frame_count;++i) {
        unsigned e=112+4*magic_frame_count+4*i;st32(wnd+112+4*i,e);st16(wnd+e,2);
        const unsigned flips[8]={0,1,2,4,0,1,0,2};st8(wnd+e+2,flips[i]);
    }
    for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
        uint32_t pixel=MAGIC_ARC+0x400+(y*64+x)*4;
        st8(pixel,32+x*32);st8(pixel+1,32+y*32);st8(pixel+2,64);st8(pixel+3,255);
    }
    st32(MAGIC_ARC+0x28,0x2f00);st32(MAGIC_ARC+0x2c,0x2f00+608+112+8*magic_frame_count);st32(MAGIC_ARC+8,0x4000);
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
static uint32_t fixture_style_animation(const char* tag,const char* group,const char* name,
        unsigned kind,uint32_t type,unsigned target,unsigned channels,float first,float last) {
    unsigned nodes=ld16(MAGIC_ARC+0x1a);assert(nodes<8);
    uint32_t node=MAGIC_ARC+0x20+nodes*16;
    memmove(ppc_ptr(node+16),ppc_ptr(node),0x48);memset(ppc_ptr(node),0,16);
    st16(MAGIC_ARC+0x1a,nodes+1);
    uint32_t f=MAGIC_ARC+0x5000+(nodes-3)*0x400,pat=f+20,pai=f+116,c=pai+24,t=c+36;
    unsigned bytes=116+24+36+8+channels*40;
    memset(ppc_ptr(f),0,bytes);st32(f,0x464c414e);st16(f+4,0xfeff);st16(f+6,20);st16(f+16,2);
    st32(pat,0x70617431);st32(pat+4,96);st16(pat+10,1);st32(pat+12,28);st32(pat+16,64);
    st16(pat+22,20);strcpy((char*)ppc_ptr(pat+28),tag);strcpy((char*)ppc_ptr(pat+64),group);
    st32(pai,0x70616931);st32(pai+4,bytes-116);st16(pai+8,20);st16(pai+14,1);st32(pai+16,20);st32(pai+20,24);
    strcpy((char*)ppc_ptr(c),name);st8(c+28,1);st8(c+29,kind);st32(c+32,36);
    st32(t,type);st8(t+4,channels);
    for(unsigned i=0;i<channels;++i) {
        uint32_t e=t+8+channels*4+i*36,k=e+12;st32(t+8+i*4,e-t);
        st8(e+1,target+i);st8(e+2,2);st16(e+4,2);st32(e+8,12);
        stf32(k,0);stf32(k+4,first);stf32(k+8,(last-first)/20);
        stf32(k+12,20);stf32(k+16,last);stf32(k+20,(last-first)/20);
    }
    st32(node+8,f-MAGIC_ARC-0x100);st32(node+12,f-MAGIC_ARC-0x100+bytes);st32(MAGIC_ARC+8,0x7000);
    return t;
}
static void magic_expect_draws(unsigned count) {
    ++magic_generation;read_hud();if(!magic_authored) hook(0x02675414,3,MAGIC_CTRL,0);
    magic_draws=0;read_hud();assert((unsigned)magic_draws==count);
}
static void test_magic_hidden_native(void) {
    /* Dark Link must remain visible with no native magic gauge. The PNG
     * assertion checks actual alpha, not only submitted image handles. */
    magic_window=0;magic_require_core_alpha=1;fixture_magic();
    const unsigned pictures[3]={120,256,392};
    for(unsigned i=0;i<3;++i) {
        st8(MAGIC_FLYT+pictures[i]+8,0);st8(MAGIC_FLYT+pictures[i]+10,0);
    }
    st8(SAVE+0x33,0);magic_authored=magic_native_hidden=1;magic_expect_draws(2);assert(magic_width>0);
    /* When base == fill, 0.1.22 submits handles but bakes alpha zero. */
    magic_base_fill=1;st32(MAGIC_FLYT+20+20+32,0x20ff20ff);magic_expect_draws(2);
    magic_base_fill=0;
    /* Shared native fade parent, without an InAll resource to override it. */
    fixture_magic();
    uint32_t f=MAGIC_ARC+0x3000,parent=f+120;
    memset(ppc_ptr(f),0,628);memcpy(ppc_ptr(f),ppc_ptr(MAGIC_FLYT),120);
    st16(f+16,7);st32(parent,0x70616e31);st32(parent+4,84);
    strcpy((char*)ppc_ptr(parent+12),"N_All_00");st32(parent+84,0x70617331);st32(parent+88,8);
    memcpy(ppc_ptr(parent+92),ppc_ptr(MAGIC_FLYT+120),408);
    st32(f+620,0x70616531);st32(f+624,8);
    st32(MAGIC_ARC+0x28,0x2f00);st32(MAGIC_ARC+0x2c,0x2f00+628);st32(MAGIC_ARC+8,0x4000);
    const unsigned capacities[3]={0,16,32};
    for(unsigned i=0;i<3;++i) {
        st8(SAVE+0x33,capacities[i]);magic_authored=magic_native_hidden=i==0;
        magic_expect_draws(2);assert(magic_width>0);
    }
    int writes=magic_writes;read_hud();read_hud();assert(magic_writes==writes);
    /* Optional hierarchy metadata must not veto independently valid images. */
    st32(f+620,0x70617331);magic_expect_draws(2);
    magic_authored=0;magic_require_core_alpha=0;st8(SAVE+0x33,0);fixture_magic();
    puts("Dark Link hidden native HUD: visible cached pixels at 0/16/32 magic passed");
}
static void test_magic_picture_layers(void) {
    magic_window=0;magic_picture_layers=1;magic_frame_count=1;
    fixture_magic();fixture_window();
    uint32_t f=MAGIC_ARC+0x3000,pic=f+728;
    fixture_picture(pic,"P_Outline_00",2);fixture_picture(pic+136,"P_White_00",2);
    for(unsigned i=0;i<2;++i) { stf32(pic+i*136+0x4c,108);stf32(pic+i*136+0x50,24); }
    st16(f+16,8);st32(MAGIC_ARC+0x2c,0x2f00+1000);
    for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
        uint32_t pixel=MAGIC_ARC+0x400+(y*64+x)*4;
        st8(pixel,128);st8(pixel+1,128);st8(pixel+2,128);
        st8(pixel+3,(x==0 || x==3 || y==0 || y==3)?255:0);
    }
    /* Copy a real pic1 outline; reject the similarly sized opaque white pane. */
    magic_detail_handle=0;magic_expect_draws(3);assert(magic_detail_handle);
    int writes=magic_writes;read_hud();assert(magic_writes==writes);
    magic_picture_layers=0;fixture_magic();
    /* A green background must not override the separate animated fill. */
    magic_animated=magic_base_fill=magic_distinct_fill=1;free_mode=1;++revision;tick();
    fixture_magic();fixture_animation(16);st32(MAGIC_FLYT+20+20+32,0x20ff20ff);
    magic_expect_draws(2);assert(magic_width==96 && magic_x==62);
    magic_animated=magic_base_fill=magic_distinct_fill=0;free_mode=0;++revision;tick();fixture_magic();
    puts("Dark Link separate animated fill and native picture outline tests passed");
}
static void test_magic_style(void) {
    magic_frame_count=1;fixture_magic();fixture_window();
    uint32_t f=MAGIC_ARC+0x3000,wnd=f+608;
    /* Published 0.1.21 draws this invisible white window as an opaque frame. */
    st8(wnd+10,0);magic_expect_draws(2);
    st8(wnd+10,255);st8(wnd+8,0);magic_expect_draws(2);
    st8(wnd+8,1);
    /* Outer box alone matches, but thick slices would hide almost all fill. */
    st16(wnd+0x5c,50);st16(wnd+0x5e,50);st16(wnd+0x60,11);st16(wnd+0x62,11);
    magic_expect_draws(2);
    fixture_window();st8(wnd+10,128);magic_border_alpha=128;magic_expect_draws(3);
    magic_border_alpha=255;
    /* InAll at end reveals it; Flash at zero hides it again. SARC order is
     * deliberately reversed so the loader must use native animation order. */
    fixture_magic();fixture_window();
    fixture_style_animation("Flash","G_Flash_00","W_Frame_00",0,0x464c5643,16,1,0,255);
    fixture_style_animation("InAll","G_All_00","W_Frame_00",0,0x464c5643,16,1,0,255);
    magic_expect_draws(2);
    /* Warning-group panes are excluded even if Flash resources are missing. */
    fixture_magic();fixture_window();
    uint32_t group=wnd+120;st32(group,0x67727031);st32(group+4,60);
    strcpy((char*)ppc_ptr(group+8),"G_Flash_00");st16(group+32,1);
    strcpy((char*)ppc_ptr(group+36),"W_Frame_00");st16(f+16,7);st32(MAGIC_ARC+0x2c,0x2f00+788);
    magic_expect_draws(2);
    /* Hidden parent propagates to its window child. */
    fixture_magic();fixture_window();
    memmove(ppc_ptr(wnd+92),ppc_ptr(wnd),120);memset(ppc_ptr(wnd),0,92);
    st32(wnd,0x70616e31);st32(wnd+4,84);st8(wnd+8,3);st8(wnd+10,0);
    strcpy((char*)ppc_ptr(wnd+12),"N_Flash");st32(wnd+84,0x70617331);st32(wnd+88,8);
    st32(wnd+212,0x70616531);st32(wnd+216,8);st16(f+16,9);st32(MAGIC_ARC+0x2c,0x2f00+828);
    magic_expect_draws(2);
    /* Copy the displayed style, not the white authoring colour or an initial
     * alpha of zero. ColorChange frame zero and InAll end both matter. */
    fixture_magic();st32(MAGIC_FLYT+20+60+32,0xffffffff);st8(MAGIC_FLYT+392+10,0);
    uint32_t tracks=fixture_style_animation("ColorChange","G_Change_00","Fill",1,0x464c4d43,4,3,32,255);
    uint32_t green=tracks+8+12+36+12;stf32(green+4,255);stf32(green+8,0);
    fixture_style_animation("InAll","G_All_00","P_Fill_00",0,0x464c5643,16,1,0,255);
    magic_expect_draws(2);assert(magic_width>0);
    fixture_magic();fixture_window();
    puts("Dark Link displayed style, flash exclusion, frame opening and alpha tests passed");
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
    /* Native FLTS scales may reverse UV order or extend past [0,1].
     * Replay these actual commands through the real HUD Store after this test. */
    uint32_t scale_key=MAGIC_ARC+0xc00+84+112+36+12+12+16;
    const float scales[]={-2,4,0};
    for(unsigned i=0;i<sizeof scales/sizeof scales[0];++i) {
        stf32(scale_key,scales[i]);++magic_generation;
        read_hud();hook(0x02675414,3,MAGIC_CTRL,0);magic_draws=0;read_hud();
        assert(magic_draws==2 && magic_width==96);expect_hud("Dark Link");
    }
    /* Unsupported rotation and non-finite UVs omit only the gauge. */
    uint32_t scale_entry=MAGIC_ARC+0xc00+84+112+36+12;
    st8(scale_entry+1,2);stf32(scale_key,45);++magic_generation;
    read_hud();hook(0x02675414,3,MAGIC_CTRL,0);magic_draws=0;read_hud();
    assert(!magic_draws);expect_hud("Dark Link");
    fixture_magic();fixture_animation(16);
    st32(MAGIC_FLYT+392+0x68,0x7fc00000);++magic_generation;
    magic_draws=0;read_hud();assert(!magic_draws);expect_hud("Dark Link");
    fixture_magic();fixture_animation(16);
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
    magic_bad_geometry=1;++magic_generation;read_hud();hook(0x02675414,3,MAGIC_CTRL,0);
    magic_draws=0;read_hud();assert(magic_draws==1);expect_hud("Dark Link");
    magic_bad_geometry=0;++magic_generation;read_hud();hook(0x02675414,3,MAGIC_CTRL,0);read_hud();
    magic_window=1;fixture_window();++magic_generation;read_hud();hook(0x02675414,3,MAGIC_CTRL,0);
    magic_draws=0;read_hud();assert(magic_draws==3);
    expect_hud("pic/wnd=3/1");expect_hud("BORDER");
    /* Diagnostic toggling does not rebuild images or modify source resources. */
    uint8_t source_before[0x5000];memcpy(source_before,ppc_ptr(MAGIC_ARC),sizeof source_before);
    int writes_before=magic_writes;diagnostics=0;++revision;tick();read_hud();
    assert(!strstr(hud_text,"Gauge ") && magic_writes==writes_before);
    diagnostics=1;++revision;tick();read_hud();
    assert(strstr(hud_text,"Gauge ") && magic_writes==writes_before);
    assert(!memcmp(source_before,ppc_ptr(MAGIC_ARC),sizeof source_before));
    for(unsigned count=4;count<=8;count+=4) {
        magic_frame_count=count;fixture_window();++magic_generation;read_hud();hook(0x02675414,3,MAGIC_CTRL,0);
        magic_draws=0;read_hud();assert(magic_draws==3);
    }
    /* A malformed frame is omitted without invalidating the gauge/button. */
    st32(MAGIC_ARC+0x3000+608+0x6c,0xfffffff0);++magic_generation;
    read_hud();hook(0x02675414,3,MAGIC_CTRL,0);magic_draws=0;read_hud();assert(magic_draws==2);
    expect_hud("wnd-image-failed");
    test_magic_style();
    test_magic_hidden_native();
    test_magic_picture_layers();
    magic_window=0;fixture_magic();++magic_generation;read_hud();
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
