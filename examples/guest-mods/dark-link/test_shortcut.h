/* Reproduce native hdParts::draw ordering: Main layout RETURNS before child
 * Shortcut::draw begins. Shortcut and its unread marker own separate layouts.
 * The old 0.1.17 Main drawLayout patch fails the visible-child assertion below.
 * All memory and values are synthetic, no game assets. */
enum { SHORT_MAIN=0x11500000,SHORT_ROOT=0x11501000,SHORT_GROUP=0x11502000,
       SHORT_CHILD=0x11503000,SHORT_OTHER=0x11504000,SHORT_LAYOUT=0x11505000,
       SHORT_ACCESSOR=0x11506000,SHORT_NESTED=0x11507000,SHORT_NROOT=0x11508000,
       SHORT_SCREEN=0x11509000,SHORT_LIST=0x1150a000,SHORT_UNREAD=0x1150b000,
       SHORT_ULAYOUT=0x1150c000,SHORT_UACCESSOR=0x1150d000,SHORT_UPANE=0x1150e000 };
/* Native functions without installed hooks simply continue. This permits the
 * same regression to execute both the old and new translated modules. */
static void shortcut_hook(uint32_t address,uint32_t kind,uint32_t object) {
    for(uint32_t i=0;i<module->hook_count;++i) {
        const WWHDGuestHook* h=&module->hooks[i];
        if(h->target==address && h->kind==kind) execute(h->func,object,0);
    }
}
static void shortcut_pane(uint32_t p,const char* name,float y) {
    memset(ppc_ptr(p),0,0x100);strcpy((char*)ppc_ptr(p+0x80),name);
    st32(p+0x14,p+0x14);st32(p+0x18,p+0x14);stf32(p+0x64,y);stf32(p+0x20,y);
}
static void shortcut_fixture(float y) {
    memset(ppc_ptr(SHORT_MAIN),0,0x100);memset(ppc_ptr(SHORT_SCREEN),0,0x100);
    memset(ppc_ptr(SHORT_UNREAD),0,0x100);
    st32(SHORT_MAIN+4,0x100F7A48);st32(SHORT_MAIN+0x44,SHORT_LAYOUT);
    st32(SHORT_LAYOUT+4,SHORT_ACCESSOR);st32(SHORT_ACCESSOR+0xc,SHORT_ROOT);
    shortcut_pane(SHORT_ROOT,"RootPane",0);shortcut_pane(SHORT_OTHER,"N_Default_00",123);
    shortcut_pane(SHORT_GROUP,"L_Shortcut_00",y);shortcut_pane(SHORT_NROOT,"RootPane",y);
    shortcut_pane(SHORT_CHILD,"P_Cross_00",y-10);shortcut_pane(SHORT_UPANE,"P_Unread_00",y-20);
    st32(SHORT_MAIN+0x50,SHORT_OTHER);
    st32(SHORT_ROOT+0x14,SHORT_OTHER);st32(SHORT_OTHER,SHORT_GROUP);st32(SHORT_GROUP,SHORT_ROOT+0x14);
    st32(SHORT_OTHER+0xc,SHORT_ROOT);st32(SHORT_GROUP+0xc,SHORT_ROOT);
    st32(SHORT_GROUP+8,0x1010BBC0);st32(SHORT_GROUP+0xac,SHORT_NESTED);st32(SHORT_NESTED+0xc,SHORT_NROOT);
    st32(SHORT_NROOT+0x14,SHORT_CHILD);st32(SHORT_CHILD,SHORT_NROOT+0x14);st32(SHORT_CHILD+0xc,SHORT_NROOT);
    st32(SHORT_SCREEN+4,0x10104B88);st32(SHORT_SCREEN+0x44,SHORT_SCREEN+0x80);
    st32(SHORT_SCREEN+0x84,SHORT_NESTED);
    st32(SHORT_SCREEN+0x48,1);st32(SHORT_SCREEN+0x4c,SHORT_LIST);st32(SHORT_LIST,SHORT_UNREAD);
    st32(SHORT_UNREAD+0x44,SHORT_ULAYOUT);st32(SHORT_ULAYOUT+4,SHORT_UACCESSOR);st32(SHORT_UACCESSOR+0xc,SHORT_UPANE);
}
static void shortcut_calc(float expected) {
    uint32_t original=ld32(SHORT_NROOT+0x64),local=ld32(SHORT_NROOT+0x20);
    /* Main::draw -> drawLayout returns -> drawChildren -> Shortcut::draw. */
    shortcut_hook(0x026f8b54,2,SHORT_MAIN);
    shortcut_hook(0x026f8d08,2,SHORT_MAIN);shortcut_hook(0x026f8d08,3,SHORT_MAIN);
    shortcut_hook(0x026f8b54,2,SHORT_SCREEN);
    shortcut_hook(0x026f8d08,2,SHORT_SCREEN);
    /* Values the renderer consumes for the actual visible D-pad. */
    assert(ldf32(SHORT_NROOT+0x64)==expected);
    assert(ldf32(SHORT_CHILD+0x64)==expected-10);
    assert(ldf32(SHORT_OTHER+0x64)==123); /* unrelated HUD unchanged */
    assert(ld32(SHORT_GROUP+0x64)==original); /* placement input unchanged */
    assert(ld32(SHORT_NROOT+0x20)==local);
    shortcut_hook(0x026f8d08,3,SHORT_SCREEN);
    /* The child unread marker draws AFTER Shortcut's own drawLayout returns. */
    shortcut_hook(0x026f8b54,2,SHORT_UNREAD);shortcut_hook(0x026f8d08,2,SHORT_UNREAD);
    assert(ldf32(SHORT_UPANE+0x64)==expected-20);
    shortcut_hook(0x026f8d08,3,SHORT_UNREAD);shortcut_hook(0x026f8b54,3,SHORT_UNREAD);
    assert(ldf32(SHORT_NROOT+0x64)==expected);
    shortcut_hook(0x026f8b54,3,SHORT_SCREEN);
    assert(ld32(SHORT_NROOT+0x64)==original);
    assert(ldf32(SHORT_CHILD+0x64)==ldf32(SHORT_NROOT+0x64)-10);
    assert(ldf32(SHORT_UPANE+0x64)==ldf32(SHORT_NROOT+0x64)-20);
    shortcut_hook(0x026f8b54,3,SHORT_MAIN);
}
static void test_shortcut(void) {
    st8(SAVE+0x33,16);read_hud();hook(0x02675414,3,MAGIC_CTRL,0);
    shortcut_fixture(250);shortcut_calc(226);
    for(int i=0;i<100;++i) shortcut_calc(226); /* TV/DRC/repeated draws: no drift */
    shortcut_fixture(220);shortcut_calc(196); /* second heart row */
    st8(SAVE+0x33,32);shortcut_calc(196); /* doubled magic */
    st8(SAVE+0x33,0);shortcut_calc(172); /* native magic row hidden */
    st8(SAVE+0x33,16);shortcut_fixture(250);
    show_gauge=0;++revision;tick();shortcut_calc(250);
    show_gauge=1;++revision;tick();shortcut_calc(226);
    paused=1;shortcut_calc(250);paused=0;menu=1;shortcut_calc(250);menu=0;
    st8(PLAY+0x5292,1);shortcut_calc(250);st8(PLAY+0x5292,0);
    st8(PLAY+0x514c,1);shortcut_calc(250);st8(PLAY+0x514c,0);
    st32(SHORT_SCREEN+4,0x100EE5F8);shortcut_calc(250);st32(SHORT_SCREEN+4,0x10104B88);
    st32(SHORT_CHILD,SHORT_CHILD);shortcut_calc(250);st32(SHORT_CHILD,SHORT_NROOT+0x14);
    st32(SHORT_SCREEN+0x44,3);shortcut_calc(250);st32(SHORT_SCREEN+0x44,SHORT_SCREEN+0x80);
    st32(SHORT_LIST,SHORT_SCREEN);shortcut_calc(250);st32(SHORT_LIST,SHORT_UNREAD); /* ownership cycle */
    st32(SHORT_SCREEN+0x48,17);shortcut_calc(250);st32(SHORT_SCREEN+0x48,1);
    st32(SHORT_CHILD+0x64,0x7fc00000);shortcut_hook(0x026f8b54,2,SHORT_SCREEN);
    assert(ldf32(SHORT_NROOT+0x64)==250);shortcut_hook(0x026f8b54,3,SHORT_SCREEN);stf32(SHORT_CHILD+0x64,240);
    /* Same root reachable through a parts pane AND a child screen: shift once. */
    st32(SHORT_CHILD+8,0x1010BBC0);st32(SHORT_CHILD+0xac,SHORT_UACCESSOR);shortcut_calc(226);
    st32(SHORT_CHILD+8,0);st32(SHORT_CHILD+0xac,0);
    ++magic_generation;shortcut_calc(250);
    magic_archive=0;read_hud();shortcut_calc(250);
    fixture_magic();++magic_generation;read_hud();hook(0x02675414,3,MAGIC_CTRL,0);shortcut_calc(226);
    shortcut_hook(0x026f8b54,2,SHORT_SCREEN);stf32(SHORT_NROOT+0x64,123);
    shortcut_hook(0x026f8b54,3,SHORT_SCREEN);assert(ldf32(SHORT_NROOT+0x64)==123);
    st8(SAVE+0x33,0);
    puts("Dark Link actual child-draw ordering, Shortcut ownership and exact restoration tests passed");
}
