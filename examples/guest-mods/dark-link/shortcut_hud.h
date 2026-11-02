/* Main_00 layout evidence (USA, relocated for EU by the guest SDK):
 * ZeldaWWHDDecomp/wwhd b2811a3, d/hd_screen_hud_main.{cpp,h}:
 * execute 02676674 applies HeartPos/MagicPos before vCalcLayout; mPanes[0]
 * at +50 is N_Default_00 and the item/D-pad child is L_Shortcut_00.
 * Pane SRT/name/global fields: d/hd_parts_lyt.h and runtime/src/aspect.cpp.
 * Change only the calculation input; native animation/local state stays owned
 * by the game. Its freshly calculated descendants inherit the extra offset. */
#pragma once
static u32 dl_shortcut_root,dl_shortcut_pane,dl_shortcut_original,dl_shortcut_written;
static int dl_shortcut_named(u32 pane,const char* name) {
    return dl_game_span(pane,0xa4) && dl_str_equal((const char*)(pane+0x80),name,24);
}
WWHD_HOOK(0x02676674, dl_shortcut_main_enter, (void* main)) {
    dl_shortcut_root=0;
    if(!dl_game_span((u32)main,0xac)) return;
    u32 root=dl_word(main,0x50);
    if(dl_shortcut_named(root,"N_Default_00")) dl_shortcut_root=root;
}
WWHD_HOOK_RETURN(0x02676674, dl_shortcut_main_leave, (void* main)) {
    (void)main;dl_shortcut_root=0;
}
WWHD_HOOK(0x028766CC, dl_shortcut_calc_enter, (void* object,u32 info,u32 force)) {
    (void)info;(void)force;
    u32 pane=(u32)object;
    if(!dl_shortcut_root || dl_shortcut_pane || !dl_show_hud || !dl_owner ||
       !dl_shortcut_named(pane,"L_Shortcut_00") || !dl_allowed(dl_owner)) return;
    /* Scope to this Main_00 tree, never an identically named menu pane. */
    u32 parent=dl_word(object,0xc),ancestor=parent;
    for(u32 n=0;n<16 && ancestor!=dl_shortcut_root;++n) {
        if(!dl_game_span(ancestor,0xa4)) return;
        ancestor=dl_word((void*)ancestor,0xc);
    }
    if(ancestor!=dl_shortcut_root || !dl_game_span(parent,0xa4) ||
       wwhd_hud_epoch()!=dl_magic_epoch) return;
    u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    u32 capacity=save?*((u8*)save+0x33):0;
    wwhd_hud_element gauge;
    if(!dl_magic_element(capacity>16,0,&gauge) || !(gauge.rgba&255)) return;
    f32 row=gauge.h+8,sy=*(f32*)(parent+0x5c),y=*(f32*)(pane+0x20);
    if(!(row>8 && row<160 && dl_abs(sy)>0.01f && dl_abs(sy)<100 && y>-2000 && y<2000)) return;
    /* MagicPos already reserves the native magic row when unlocked. */
    f32 target=y-row*(capacity?1:2)/sy;
    if(!(target>-4000 && target<4000)) return;
    dl_shortcut_pane=pane;dl_shortcut_original=dl_word(object,0x20);
    union { f32 value;u32 bits; } encoded={.value=target};
    dl_shortcut_written=encoded.bits;*(u32*)(pane+0x20)=encoded.bits;
    *((u8*)pane+0x44)|=0x10; /* force this subtree to recalculate */
}
WWHD_HOOK_RETURN(0x028766CC, dl_shortcut_calc_leave, (void* object,u32 info,u32 force)) {
    (void)info;(void)force;
    u32 pane=(u32)object;
    if(!dl_shortcut_pane || pane!=dl_shortcut_pane) return;
    if(dl_word(object,0x20)==dl_shortcut_written) *(u32*)(pane+0x20)=dl_shortcut_original;
    *((u8*)pane+0x44)|=0x10; /* removing/hiding the gauge restores native matrices next pass */
    dl_shortcut_pane=0;
}
