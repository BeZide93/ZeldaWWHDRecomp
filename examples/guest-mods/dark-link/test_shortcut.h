/* Synthetic native-memory regression. Model Main calculation, retained render
 * geometry and both embedded/standalone draw routes. hdParts::draw can run
 * without drawing its layout: 02002D20 checks root.parent first. No game code
 * or GPU is executed; this test does not establish device rendering. */
enum { SHORT_MAIN=0x11500000,SHORT_ROOT=0x11501000,SHORT_GROUP=0x11502000,
       SHORT_CHILD=0x11503000,SHORT_OTHER=0x11504000,SHORT_LAYOUT=0x11505000,
       SHORT_ACCESSOR=0x11506000,SHORT_NESTED=0x11507000,SHORT_NROOT=0x11508000,
       SHORT_SCREEN=0x11509000,SHORT_LIST=0x1150a000,SHORT_UNREAD=0x1150b000,
       SHORT_ULAYOUT=0x1150c000,SHORT_UACCESSOR=0x1150d000,SHORT_UPANE=0x1150e000 };
static void shortcut_hook(uint32_t address,uint32_t kind,uint32_t object) {
    for(uint32_t i=0;i<module->hook_count;++i) {
        const WWHDGuestHook* h=&module->hooks[i];
        if(h->target==address && h->kind==kind) execute(h->func,object,0);
    }
}
static void shortcut_pane(uint32_t p,const char* name,float y) {
    memset(ppc_ptr(p),0,0x100);strcpy((char*)ppc_ptr(p+0x80),name);
    st32(p+0x14,p+0x14);st32(p+0x18,p+0x14);
    stf32(p+0x20,y);stf32(p+0x48,1);stf32(p+0x5c,1);stf32(p+0x64,y);
}
static void shortcut_fixture(float y,int embedded) {
    memset(ppc_ptr(SHORT_MAIN),0,0x100);memset(ppc_ptr(SHORT_SCREEN),0,0x100);
    memset(ppc_ptr(SHORT_UNREAD),0,0x100);
    st32(SHORT_MAIN+4,0x100F7A48);st32(SHORT_MAIN+0x44,SHORT_LAYOUT);
    st32(SHORT_LAYOUT+4,SHORT_ACCESSOR);st32(SHORT_ACCESSOR+0xc,SHORT_ROOT);
    shortcut_pane(SHORT_ROOT,"RootPane",0);shortcut_pane(SHORT_OTHER,"N_Default_00",123);
    shortcut_pane(SHORT_GROUP,"L_Shortcut_00",y);shortcut_pane(SHORT_NROOT,"RootPane",0);
    shortcut_pane(SHORT_CHILD,"P_Cross_00",-10);shortcut_pane(SHORT_UPANE,"P_Unread_00",-20);
    st32(SHORT_MAIN+0x50,SHORT_OTHER); /* deliberately NOT Shortcut's ancestor */
    st32(SHORT_ROOT+0x14,SHORT_OTHER);st32(SHORT_OTHER,SHORT_GROUP);st32(SHORT_GROUP,SHORT_ROOT+0x14);
    st32(SHORT_OTHER+0xc,SHORT_ROOT);st32(SHORT_GROUP+0xc,SHORT_ROOT);
    st32(SHORT_GROUP+8,0x1010BBC0);st32(SHORT_GROUP+0xac,SHORT_NESTED);st32(SHORT_NESTED+0xc,SHORT_NROOT);
    st32(SHORT_NROOT+0xc,embedded?SHORT_GROUP:0);
    if(embedded) { st32(SHORT_GROUP+0x14,SHORT_NROOT);st32(SHORT_NROOT,SHORT_GROUP+0x14); }
    st32(SHORT_NROOT+0x14,SHORT_CHILD);st32(SHORT_CHILD,SHORT_NROOT+0x14);st32(SHORT_CHILD+0xc,SHORT_NROOT);
    st32(SHORT_SCREEN+4,0x10104B88);st32(SHORT_SCREEN+0x44,SHORT_SCREEN+0x80);
    st32(SHORT_SCREEN+0x84,SHORT_NESTED);
    st32(SHORT_SCREEN+0x48,1);st32(SHORT_SCREEN+0x4c,SHORT_LIST);st32(SHORT_LIST,SHORT_UNREAD);
    st32(SHORT_UNREAD+0x44,SHORT_ULAYOUT);st32(SHORT_ULAYOUT+4,SHORT_UACCESSOR);st32(SHORT_UACCESSOR+0xc,SHORT_UPANE);
}
static void shortcut_frame(float expected) {
    uint32_t x=ld32(SHORT_GROUP+0x1c),y=ld32(SHORT_GROUP+0x20);
    shortcut_hook(0x026f8cd0,2,SHORT_MAIN);
    /* A nested parts calculation must not restore the outer input early. */
    shortcut_hook(0x026f8cd0,2,SHORT_SCREEN);shortcut_hook(0x026f8cd0,3,SHORT_SCREEN);
    float px=ldf32(SHORT_GROUP+0x1c),py=ldf32(SHORT_GROUP+0x20);
    float gx=ldf32(SHORT_ROOT+0x48)*px+ldf32(SHORT_ROOT+0x4c)*py;
    float gy=ldf32(SHORT_ROOT+0x58)*px+ldf32(SHORT_ROOT+0x5c)*py;
    if(ld32(SHORT_GROUP+0x20)!=y || ld32(SHORT_GROUP+0x1c)!=x) assert(ld8(SHORT_GROUP+0x44)&0x10);
    /* Stand-in for the native matrix/geometry calculation and its cache. */
    stf32(SHORT_GROUP+0x54,gx);stf32(SHORT_GROUP+0x64,gy);
    stf32(SHORT_NROOT+0x64,gy);stf32(SHORT_CHILD+0x64,gy-10);stf32(SHORT_UPANE+0x64,gy-20);
    st8(SHORT_GROUP+0x44,ld8(SHORT_GROUP+0x44)&~0x10);
    const float prepared_y=gy;
    shortcut_hook(0x026f8cd0,3,SHORT_MAIN);
    assert(ld32(SHORT_GROUP+0x1c)==x && ld32(SHORT_GROUP+0x20)==y); /* authored locals restored */
    assert(ldf32(SHORT_OTHER+0x64)==123);
    shortcut_hook(0x026f8b54,2,SHORT_MAIN);shortcut_hook(0x026f8d08,2,SHORT_MAIN);
    float visible_y=0;
    if(ld32(SHORT_NROOT+0xc)) { /* embedded layout is rendered through Main */
        shortcut_hook(0x02877100,2,SHORT_CHILD);
        visible_y=prepared_y;
    }
    shortcut_hook(0x026f8d08,3,SHORT_MAIN);
    shortcut_hook(0x026f8b54,2,SHORT_SCREEN);shortcut_hook(0x026f8d08,2,SHORT_SCREEN);
    if(!ld32(SHORT_NROOT+0xc)) visible_y=prepared_y; /* otherwise its own draw */
    shortcut_hook(0x026f8d08,3,SHORT_SCREEN);shortcut_hook(0x026f8b54,3,SHORT_SCREEN);
    shortcut_hook(0x026f8b54,3,SHORT_MAIN);
    assert(visible_y==expected); /* 0.1.18 fails here: it changes only draw-time memory */
    assert(ldf32(SHORT_NROOT+0x64)==expected); /* result survives the whole draw */
    assert(ldf32(SHORT_CHILD+0x64)==expected-10 && ldf32(SHORT_UPANE+0x64)==expected-20);
}
static void shortcut_rejected(void) {
    uint32_t x=ld32(SHORT_GROUP+0x1c),y=ld32(SHORT_GROUP+0x20);
    shortcut_hook(0x026f8cd0,2,SHORT_MAIN);
    assert(ld32(SHORT_GROUP+0x1c)==x && ld32(SHORT_GROUP+0x20)==y);
    shortcut_hook(0x026f8cd0,3,SHORT_MAIN);
}
static void test_shortcut(void) {
    st8(SAVE+0x33,16);read_hud();hook(0x02675414,3,MAGIC_CTRL,0);
    for(int embedded=0;embedded<2;++embedded) {
        shortcut_fixture(250,embedded);shortcut_frame(226);
        for(int i=0;i<100;++i) shortcut_frame(226); /* no drift or draw-time undo */
    }
    read_hud(); const char* seen=strstr(hud_text,"Seen=");
    assert(seen && strtoul(seen+5,0,10)>0); expect_hud("Shift=24");
    assert(ld8(SHORT_GROUP+0x44)&0x10); /* force native recomputation on removal */
    shortcut_fixture(220,1);shortcut_frame(196); /* second heart row */
    st8(SAVE+0x33,32);shortcut_frame(196);st8(SAVE+0x33,0);shortcut_frame(172);
    st8(SAVE+0x33,16);shortcut_fixture(250,1);
    stf32(SHORT_ROOT+0x5c,2);shortcut_frame(476); /* parent scale: still exactly 24 down */
    stf32(SHORT_ROOT+0x48,0);stf32(SHORT_ROOT+0x4c,-1);stf32(SHORT_ROOT+0x58,1);stf32(SHORT_ROOT+0x5c,0);
    stf32(SHORT_GROUP+0x1c,250);shortcut_frame(226); /* rotated basis */
    shortcut_fixture(250,1);
    show_gauge=0;++revision;tick();shortcut_frame(250);
    show_gauge=1;++revision;tick();shortcut_frame(226);
    paused=1;shortcut_frame(250);paused=0;menu=1;shortcut_frame(250);menu=0;
    st8(PLAY+0x5292,1);shortcut_frame(250);st8(PLAY+0x5292,0);
    st8(PLAY+0x514c,1);shortcut_frame(250);st8(PLAY+0x514c,0);
    st32(SHORT_MAIN+4,0x100EE5F8);shortcut_frame(250);st32(SHORT_MAIN+4,0x100F7A48);
    st32(SHORT_OTHER,SHORT_OTHER);shortcut_rejected();st32(SHORT_OTHER,SHORT_GROUP);
    st32(SHORT_GROUP+0xc,SHORT_GROUP);shortcut_rejected();st32(SHORT_GROUP+0xc,SHORT_ROOT);
    st32(SHORT_MAIN+0x44,3);shortcut_rejected();st32(SHORT_MAIN+0x44,SHORT_LAYOUT);
    strcpy((char*)ppc_ptr(SHORT_OTHER+0x80),"L_Shortcut_00");shortcut_rejected();
    strcpy((char*)ppc_ptr(SHORT_OTHER+0x80),"N_Default_00");
    strcpy((char*)ppc_ptr(SHORT_GROUP+0x80),"Missing");shortcut_rejected();
    strcpy((char*)ppc_ptr(SHORT_GROUP+0x80),"L_Shortcut_00");
    st32(SHORT_GROUP+0x20,0x7fc00000);shortcut_rejected();stf32(SHORT_GROUP+0x20,250);
    stf32(SHORT_ROOT+0x5c,0);shortcut_rejected();stf32(SHORT_ROOT+0x5c,1);
    ++magic_generation;shortcut_frame(250);
    magic_archive=0;read_hud();shortcut_frame(250);
    fixture_magic();++magic_generation;read_hud();hook(0x02675414,3,MAGIC_CTRL,0);shortcut_frame(226);
    shortcut_hook(0x026f8cd0,2,SHORT_MAIN);stf32(SHORT_GROUP+0x20,123);
    shortcut_hook(0x026f8cd0,3,SHORT_MAIN);assert(ldf32(SHORT_GROUP+0x20)==123); /* other writer */
    st8(SAVE+0x33,0);
    puts("Dark Link calculation-input, embedded/standalone draw, parent transform and restoration tests passed");
}
