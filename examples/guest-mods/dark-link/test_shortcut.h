/* Native Main_00/Pane snapshots, entirely synthetic. */
enum { SHORT_MAIN=0x11500000,SHORT_ROOT=0x11501000,SHORT_GROUP=0x11502000,
       SHORT_CHILD=0x11503000,SHORT_OTHER=0x11504000 };
static void shortcut_fixture(float y,float parent_scale) {
    memset(ppc_ptr(SHORT_MAIN),0,0x100);memset(ppc_ptr(SHORT_ROOT),0,0x100);
    memset(ppc_ptr(SHORT_GROUP),0,0x100);memset(ppc_ptr(SHORT_CHILD),0,0x100);
    st32(SHORT_MAIN+0x50,SHORT_ROOT);strcpy((char*)ppc_ptr(SHORT_ROOT+0x80),"N_Default_00");
    st32(SHORT_GROUP+0xc,SHORT_ROOT);strcpy((char*)ppc_ptr(SHORT_GROUP+0x80),"L_Shortcut_00");
    st32(SHORT_CHILD+0xc,SHORT_GROUP);strcpy((char*)ppc_ptr(SHORT_CHILD+0x80),"P_Icon_00");
    stf32(SHORT_ROOT+0x5c,parent_scale);stf32(SHORT_GROUP+0x20,y);st8(SHORT_GROUP+0x44,3);
}
static void shortcut_calc(float expected) {
    uint32_t original=ld32(SHORT_GROUP+0x20);
    hook(0x02676674,2,SHORT_MAIN,0);
    hook(0x028766cc,2,SHORT_GROUP,0);
    assert(ldf32(SHORT_GROUP+0x20)==expected);
    /* The native calculation consumes the modified local Y. Child callbacks
     * must not restore the parent's input halfway through that traversal. */
    hook(0x028766cc,2,SHORT_CHILD,0);hook(0x028766cc,3,SHORT_CHILD,0);
    assert(ldf32(SHORT_GROUP+0x20)==expected);
    stf32(SHORT_GROUP+0x64,expected*ldf32(SHORT_ROOT+0x5c));
    stf32(SHORT_CHILD+0x64,ldf32(SHORT_GROUP+0x64)-10);
    st8(SHORT_GROUP+0x44,ld8(SHORT_GROUP+0x44)&~0x10);
    hook(0x028766cc,3,SHORT_GROUP,0);
    assert(ld32(SHORT_GROUP+0x20)==original); /* exact restoration, no drift */
    assert(ldf32(SHORT_CHILD+0x64)==expected*ldf32(SHORT_ROOT+0x5c)-10);
    hook(0x02676674,3,SHORT_MAIN,0);
}
static void test_shortcut(void) {
    st8(SAVE+0x33,16);read_hud();hook(0x02675414,3,MAGIC_CTRL,0);
    shortcut_fixture(250,1);shortcut_calc(226);assert(ld8(SHORT_GROUP+0x44)==0x13);
    for(int i=0;i<100;++i) shortcut_calc(226); /* never accumulate offset */
    shortcut_fixture(220,1);shortcut_calc(196); /* native second heart row */
    st8(SAVE+0x33,32);shortcut_calc(196); /* doubled capacity, same height */
    st8(SAVE+0x33,0);shortcut_calc(172); /* native magic row is not reserved */
    st8(SAVE+0x33,16);shortcut_fixture(250,2);shortcut_calc(238);
    shortcut_fixture(250,1);
    show_gauge=0;++revision;tick();shortcut_calc(250);
    show_gauge=1;++revision;tick();shortcut_calc(226);
    paused=1;shortcut_calc(250);paused=0;
    menu=1;shortcut_calc(250);menu=0;
    st8(PLAY+0x5292,1);shortcut_calc(250);st8(PLAY+0x5292,0);
    st8(PLAY+0x514c,1);shortcut_calc(250);st8(PLAY+0x514c,0);
    /* Same name in a different layout cannot be moved. */
    st32(SHORT_GROUP+0xc,SHORT_OTHER);shortcut_calc(250);st32(SHORT_GROUP+0xc,SHORT_ROOT);
    hook(0x028766cc,2,SHORT_GROUP,0);assert(ldf32(SHORT_GROUP+0x20)==250);
    hook(0x028766cc,3,SHORT_GROUP,0); /* outside Main_00 execute */
    /* State load / missing resources must not leave a reserved blank row. */
    ++magic_generation;shortcut_calc(250);
    magic_archive=0;read_hud();shortcut_calc(250);
    fixture_magic();++magic_generation;read_hud();hook(0x02675414,3,MAGIC_CTRL,0);shortcut_calc(226);
    /* Respect another writer between entry/return, rather than overwriting it. */
    hook(0x02676674,2,SHORT_MAIN,0);hook(0x028766cc,2,SHORT_GROUP,0);
    stf32(SHORT_GROUP+0x20,123);hook(0x028766cc,3,SHORT_GROUP,0);assert(ldf32(SHORT_GROUP+0x20)==123);
    hook(0x02676674,3,SHORT_MAIN,0);
    st8(SAVE+0x33,0);
    puts("Dark Link native shortcut spacing tests passed");
}
