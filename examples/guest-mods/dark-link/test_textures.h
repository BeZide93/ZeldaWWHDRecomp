/* Runs through the translated PowerPC hooks, using synthetic FMAT/FTEX data.
 * This verifies actual binding lifetime and descriptor contents, not a game shader. */
enum { TEX_MATS=0x11100000, TEX_RES=0x11110000, TEX_REFS=0x11120000,
       TEX_ORIGINAL=0x11130000, TEX_NEXT=0x11131000, TEX_PIXELS=0x11140000 };
static uint32_t texture_slot(int material) {
    return TEX_REFS+(MATERIAL_COUNT-1-material)*0x20+4; /* _a0 is slot ONE */
}
static void texture_refresh(void) {
    hook(0x027f50f8,2,MODEL,0);
    hook(0x027f50f8,3,MODEL,0);
}
static void texture_originals(void) {
    for (int m=0;m<MATERIAL_COUNT;++m) assert(ld32(texture_slot(m))==TEX_ORIGINAL);
}
static void texture_view_check(uint32_t view, uint32_t original, int eye) {
    assert(view!=original && view>=module->mem_base && view+0xa0<=module->mem_base+module->mem_size);
    for (int i=0;i<0xa0;i+=4) if (i!=0x88 && i!=0x94)
        assert(ld32(view+i)==ld32(original+i));
    uint32_t comp=ld32(view+0x88), reg=ld32(view+0x94), source=ld32(original+0x94);
    assert((comp&0xff)==(ld32(original+0x88)&0xff));
    assert((reg&~(0x1ffu<<16))==(source&~(0x1ffu<<16)));
    assert(((reg>>25)&7)==((source>>25)&7)); /* alpha selector */
    /* Independently decode the hardware component selectors (R,G,B,A,0,1).
     * RGB must not depend on the source pixel, including a bright source. */
    float samples[][6]={{.3f,.8f,.2f,.6f,0,1},{1,1,1,0,0,1}};
    for (int p=0;p<2;++p) for (int c=0;c<3;++c) {
        unsigned selector=(reg>>(16+3*c))&7;
        assert(selector<6 && selector==((comp>>(24-8*c))&7));
        assert(samples[p][selector]==(eye && !c ? 1.0f : 0.0f));
    }
}
static void texture_tinted(void) {
    for (int m=0;m<MATERIAL_COUNT;++m) {
        uint32_t slot=texture_slot(m);
        assert(ld32(slot-4)==TEX_ORIGINAL); /* toon sampler stays original */
        texture_view_check(ld32(slot),TEX_ORIGINAL,m>=1 && m<=6);
    }
}
static void test_textures(void) {
    uint8_t asset[0xa0], old_view[0xa0];
    for (int i=0;i<0xa0;i+=4) st32(TEX_ORIGINAL+i,0x12340000u+i);
    st32(TEX_ORIGINAL,0x46544558); st32(TEX_ORIGINAL+8,128); st32(TEX_ORIGINAL+12,128);
    st32(TEX_ORIGINAL+0x28,TEX_PIXELS);
    st32(TEX_ORIGINAL+0x88,0x02010000); /* alpha comes from R, not A */
    st32(TEX_ORIGINAL+0x94,0xa1000a55); /* W selector zero */
    memcpy(asset,ppc_ptr(TEX_ORIGINAL),sizeof asset);
    memcpy(ppc_ptr(TEX_NEXT),asset,sizeof asset);
    st32(TEX_NEXT+0x28,TEX_PIXELS+0x1000);
    st32(TEX_NEXT+0x88,0x00010203);
    st32(TEX_NEXT+0x94,0xa6000a55); /* next animation frame: alpha from A */
    st16(MODEL+0x2a,MATERIAL_COUNT); st32(MODEL+0x34,TEX_MATS);
    for (int m=0;m<MATERIAL_COUNT;++m) {
        unsigned index=MATERIAL_COUNT-1-m;
        uint32_t res=TEX_RES+m*0x200, native=TEX_MATS+index*0x3c;
        st32(MATERIALS+m*0x39c,res);
        st32(res,0x464d4154); st16(res+0xc,index); st8(res+0x10,2); st8(res+0x11,2);
        st32(res+0x2c,0x80-0x2c); /* sampler array */
        st32(res+0x80+0xc,0xc0-(0x80+0xc));
        st32(res+0x98+0xc,0xc4-(0x98+0xc));
        memcpy(ppc_ptr(res+0xc0),"_a1\0_a0",8);
        st32(native,res); st32(native+0x2c,TEX_REFS+index*0x20);
        st32(texture_slot(m)-4,TEX_ORIGINAL); st32(texture_slot(m),TEX_ORIGINAL);
    }
    texture_refresh(); texture_tinted();
    uint32_t body_view=ld32(texture_slot(0)), eye_view=ld32(texture_slot(1));
    assert(body_view!=eye_view); /* the same input texture needs two distinct views */
    memcpy(old_view,ppc_ptr(eye_view),sizeof old_view);
    /* Returning from the outer submission must NOT remove queued bindings. */
    hook(0x025e2e5c,2,MODEL,0); hook(0x025e2e5c,3,MODEL,0); texture_tinted();
    hook(0x027f50f8,2,MODEL,0); texture_originals();
    st32(texture_slot(1),TEX_NEXT); /* native BTP refresh changes the eye frame */
    hook(0x027f50f8,3,MODEL,0);
    texture_view_check(ld32(texture_slot(1)),TEX_NEXT,1);
    assert(!memcmp(old_view,ppc_ptr(eye_view),sizeof old_view)); /* published view immutable */
    hook(0x027f50f8,2,MODEL,0);
    assert(ld32(texture_slot(1))==TEX_NEXT);
    st32(texture_slot(1),TEX_ORIGINAL);
    hook(0x027f50f8,3,MODEL,0); texture_tinted();
    assert(ld32(texture_slot(0))==body_view && ld32(texture_slot(1))==eye_view); /* cache reuse */
    /* Cancel via actual input/config lifecycle; no further draw is needed. */
    free_mode=1; ++revision; tick();
    buttons(0); buttons(0x60000); tick(); assert(damage(3)==3); texture_originals();
    texture_refresh(); texture_originals();
    buttons(0); buttons(0x60000); tick(); assert(damage(3)==6);
    texture_refresh(); texture_tinted();
    /* Do not overwrite a more recent binding installed by native code/another mod. */
    st32(texture_slot(2),TEX_NEXT);
    enabled=0; ++revision; tick();
    assert(ld32(texture_slot(2))==TEX_NEXT);
    st32(texture_slot(2),TEX_ORIGINAL); texture_originals();
    enabled=1; ++revision; tick(); buttons(0); buttons(0x60000); tick(); assert(damage(3)==6);
    /* Missing albedo, non-FTEX input, mismatched native resource and bad index
     * refuse the affected material without writing elsewhere. */
    st8(TEX_RES+0xc5,'x');
    st32(texture_slot(1),TEX_PIXELS);
    st32(TEX_MATS+(MATERIAL_COUNT-1-2)*0x3c,TEX_RES);
    st16(TEX_RES+3*0x200+0xc,96);
    texture_refresh();
    assert(ld32(texture_slot(0))==TEX_ORIGINAL && ld32(texture_slot(1))==TEX_PIXELS);
    assert(ld32(texture_slot(2))==TEX_ORIGINAL && ld32(texture_slot(3))==TEX_ORIGINAL);
    texture_view_check(ld32(texture_slot(4)),TEX_ORIGINAL,1);
    hook(0x027f50f8,2,MODEL,0);
    st8(TEX_RES+0xc5,'a'); st32(texture_slot(1),TEX_ORIGINAL);
    st32(TEX_MATS+(MATERIAL_COUNT-1-2)*0x3c,TEX_RES+2*0x200);
    st16(TEX_RES+3*0x200+0xc,MATERIAL_COUNT-1-3);
    /* Hands and hair are black even if their synthetic materials are named eyes. */
    for (int part=0;part<2;++part) {
        uint32_t model=MODEL+0x400+part*0x400, field=LINK+(part ? 0x44c : 0xcf4);
        st32(field,model); st32(model+0xac,DATA); st16(model+0x2a,MATERIAL_COUNT); st32(model+0x34,TEX_MATS);
        hook(0x027f50f8,3,model,0);
        for (int m=0;m<MATERIAL_COUNT;++m) texture_view_check(ld32(texture_slot(m)),TEX_ORIGINAL,0);
        hook(0x027f50f8,2,model,0); texture_originals(); st32(field,0);
    }
    st32(LINK+0x3bc,0x800); texture_refresh(); texture_originals(); st32(LINK+0x3bc,0);
    texture_refresh(); texture_tinted();
    st8(PLAY+0x514c,1); tick(); texture_originals(); st8(PLAY+0x514c,0);
    buttons(0); buttons(0x60000); tick(); texture_refresh(); texture_tinted();
    st16(SAVE+0x22,0); tick(); texture_originals(); st16(SAVE+0x22,80);
    buttons(0); buttons(0x60000); tick(); texture_refresh(); texture_tinted();
    hook(0x0240ee28,2,LINK,0); texture_originals();
    tick(); buttons(0); buttons(0x60000); tick(); assert(damage(3)==6);
    /* Changing the material-array identity means old pointers may be freed.
     * Reset must discard that ledger without writing its stale slot addresses. */
    texture_refresh(); st32(MODEL+0x34,TEX_MATS+0x1000);
    st32(texture_slot(0),0xdeadbeef);
    enabled=0; ++revision; tick(); assert(ld32(texture_slot(0))==0xdeadbeef);
    for (int m=0;m<MATERIAL_COUNT;++m) st32(texture_slot(m),TEX_ORIGINAL);
    st32(MODEL+0x34,TEX_MATS);
    enabled=1; ++revision; tick(); buttons(0); buttons(0x60000); tick(); assert(damage(3)==6);
    assert(!memcmp(asset,ppc_ptr(TEX_ORIGINAL),sizeof asset));
    for (int m=0;m<MATERIAL_COUNT;++m) st32(MATERIALS+m*0x39c,0);
    st32(MODEL+0x34,0); st16(MODEL+0x2a,0);
    free_mode=0; ++revision; tick(); restored();
    puts("Dark Link animated albedo binding tests passed");
}
