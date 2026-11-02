/* Native-shaped Main_00 fixture: N_Default is a sibling, Shortcut owns a
 * separate nested layout. Tests final matrix values visible DURING draw. */
enum { SHORT_MAIN=0x11500000,SHORT_ROOT=0x11501000,SHORT_GROUP=0x11502000,
       SHORT_CHILD=0x11503000,SHORT_OTHER=0x11504000,SHORT_LAYOUT=0x11505000,
       SHORT_ACCESSOR=0x11506000,SHORT_NESTED=0x11507000,SHORT_NROOT=0x11508000 };
static void shortcut_pane(uint32_t p,const char* name,float y) {
    memset(ppc_ptr(p),0,0x100);strcpy((char*)ppc_ptr(p+0x80),name);
    st32(p+0x14,p+0x14);st32(p+0x18,p+0x14);stf32(p+0x64,y);stf32(p+0x20,y);
}
static void shortcut_fixture(float y,float scale) {
    (void)scale;
    memset(ppc_ptr(SHORT_MAIN),0,0x100);
    st32(SHORT_MAIN+4,0x100F7A48);st32(SHORT_MAIN+0x44,SHORT_LAYOUT);
    st32(SHORT_LAYOUT+4,SHORT_ACCESSOR);st32(SHORT_ACCESSOR+0xc,SHORT_ROOT);
    shortcut_pane(SHORT_ROOT,"RootPane",0);shortcut_pane(SHORT_OTHER,"N_Default_00",123);
    shortcut_pane(SHORT_GROUP,"L_Shortcut_00",y);shortcut_pane(SHORT_NROOT,"RootPane",y);
    shortcut_pane(SHORT_CHILD,"P_Icon_00",y-10);
    st32(SHORT_MAIN+0x50,SHORT_OTHER);
    st32(SHORT_ROOT+0x14,SHORT_OTHER);st32(SHORT_OTHER,SHORT_GROUP);st32(SHORT_GROUP,SHORT_ROOT+0x14);
    st32(SHORT_OTHER+0xc,SHORT_ROOT);st32(SHORT_GROUP+0xc,SHORT_ROOT);
    st32(SHORT_GROUP+8,0x1010BBC0);st32(SHORT_GROUP+0xac,SHORT_NESTED);st32(SHORT_NESTED+0xc,SHORT_NROOT);
    st32(SHORT_NROOT+0x14,SHORT_CHILD);st32(SHORT_CHILD,SHORT_NROOT+0x14);st32(SHORT_CHILD+0xc,SHORT_NROOT);
}
static void shortcut_calc(float expected) {
    uint32_t original=ld32(SHORT_GROUP+0x64),local=ld32(SHORT_GROUP+0x20);
    hook(0x026f8d08,2,SHORT_MAIN,0);
    assert(ldf32(SHORT_GROUP+0x64)==expected);
    assert(ldf32(SHORT_CHILD+0x64)==expected-10);
    assert(ldf32(SHORT_OTHER+0x64)==123);
    assert(ld32(SHORT_GROUP+0x20)==local);
    /* A nested parts draw must not undo the shift before the parent finishes. */
    hook(0x026f8d08,2,SHORT_OTHER,0);hook(0x026f8d08,3,SHORT_OTHER,0);
    assert(ldf32(SHORT_GROUP+0x64)==expected);
    hook(0x026f8d08,3,SHORT_MAIN,0);
    assert(ld32(SHORT_GROUP+0x64)==original);
    assert(ldf32(SHORT_CHILD+0x64)==ldf32(SHORT_GROUP+0x64)-10);
}
static void test_shortcut(void) {
    st8(SAVE+0x33,16);read_hud();hook(0x02675414,3,MAGIC_CTRL,0);
    shortcut_fixture(250,1);shortcut_calc(226);
    for(int i=0;i<100;++i) shortcut_calc(226);
    shortcut_fixture(220,1);shortcut_calc(196);
    st8(SAVE+0x33,32);shortcut_calc(196);
    st8(SAVE+0x33,0);shortcut_calc(172);
    st8(SAVE+0x33,16);shortcut_fixture(250,1);
    show_gauge=0;++revision;tick();shortcut_calc(250);
    show_gauge=1;++revision;tick();shortcut_calc(226);
    paused=1;shortcut_calc(250);paused=0;menu=1;shortcut_calc(250);menu=0;
    st8(PLAY+0x5292,1);shortcut_calc(250);st8(PLAY+0x5292,0);
    st8(PLAY+0x514c,1);shortcut_calc(250);st8(PLAY+0x514c,0);
    /* Different layout, malformed ring, missing layout and bad float: no writes. */
    st32(SHORT_MAIN+4,0x100EE5F8);shortcut_calc(250);st32(SHORT_MAIN+4,0x100F7A48);
    st32(SHORT_CHILD,SHORT_CHILD);shortcut_calc(250);st32(SHORT_CHILD,SHORT_NROOT+0x14);
    st32(SHORT_GROUP+0xac,3);shortcut_calc(250);st32(SHORT_GROUP+0xac,SHORT_NESTED);
    st32(SHORT_CHILD+0x64,0x7fc00000);hook(0x026f8d08,2,SHORT_MAIN,0);
    assert(ldf32(SHORT_GROUP+0x64)==250);hook(0x026f8d08,3,SHORT_MAIN,0);stf32(SHORT_CHILD+0x64,240);
    ++magic_generation;shortcut_calc(250);
    magic_archive=0;read_hud();shortcut_calc(250);
    fixture_magic();++magic_generation;read_hud();hook(0x02675414,3,MAGIC_CTRL,0);shortcut_calc(226);
    /* Respect another writer instead of overwriting its result. */
    hook(0x026f8d08,2,SHORT_MAIN,0);stf32(SHORT_GROUP+0x64,123);
    hook(0x026f8d08,3,SHORT_MAIN,0);assert(ldf32(SHORT_GROUP+0x64)==123);
    st8(SAVE+0x33,0);
    puts("Dark Link native Shortcut draw/restoration tests passed");
}
