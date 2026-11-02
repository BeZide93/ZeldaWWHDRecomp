/* Main_00 draw-time spacing. Evidence: ZeldaWWHDDecomp/wwhd b2811a3,
 * d/hd_parts.cpp (026F8D08), hd_screen_hud_main.cpp (vtable 100F7A48),
 * hd_parts_layout_owner.cpp (Parts vtable 1010BBC0), d/hd_parts_lyt.h.
 * N_Default_00 is a placement pane, NOT a reliable root/ancestor. Walk the
 * actual layout root and nested parts layouts after native matrix calculation.
 * Translate only Shortcut's final matrices for this draw, then restore exactly.
 * Native animation inputs and all other HUD elements remain untouched. */
#pragma once
#define DL_SHORTCUT_LIMIT 192
static struct { u32 pane,under,original,written; } dl_shortcut_nodes[DL_SHORTCUT_LIMIT];
static u32 dl_shortcut_count,dl_shortcut_owner,dl_shortcut_depth;
static int dl_shortcut_named(u32 pane,const char* name) {
    return dl_game_span(pane,0xa4) && dl_str_equal((const char*)(pane+0x80),name,24);
}
static int dl_shortcut_add(u32 pane,u32 under) {
    if(!dl_game_span(pane,0xa4) || dl_shortcut_count==DL_SHORTCUT_LIMIT) return 0;
    for(u32 i=0;i<dl_shortcut_count;++i) if(dl_shortcut_nodes[i].pane==pane) return 0;
    u32 n=dl_shortcut_count++;
    dl_shortcut_nodes[n].pane=pane;
    dl_shortcut_nodes[n].under=under || dl_shortcut_named(pane,"L_Shortcut_00");
    return 1;
}
WWHD_HOOK(0x026F8D08,dl_shortcut_draw_enter,(void* parts,void* info)) {
    (void)info;
    if(dl_shortcut_owner) { ++dl_shortcut_depth; return; }
    if(!dl_show_hud || !dl_owner || !dl_allowed(dl_owner) ||
       !dl_game_span((u32)parts,0x50) ||
       dl_word(parts,4)!=(u32)&WWHD_GAME_DATA(0x100F7A48,u8) ||
       wwhd_hud_epoch()!=dl_magic_epoch) return;
    u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    u32 capacity=save?*((u8*)save+0x33):0;
    wwhd_hud_element gauge;
    if(!dl_magic_element(capacity>16,0,&gauge) || !(gauge.rgba&255)) return;
    f32 row=(gauge.h+8)*(capacity?1:2);
    if(!(row>8 && row<320)) return;
    u32 layout=dl_word(parts,0x44);
    if(!dl_game_span(layout,8)) return;
    u32 accessor=dl_word((void*)layout,4);
    if(!dl_game_span(accessor,0x10)) return;
    dl_shortcut_count=0;
    if(!dl_shortcut_add(dl_word((void*)accessor,0xc),0)) return;
    /* First validate the whole graph: a broken link/cycle/oversize tree cannot
     * result in a partly moved D-pad. Iteration bounds also cover sibling rings. */
    for(u32 i=0;i<dl_shortcut_count;++i) {
        u32 p=dl_shortcut_nodes[i].pane,under=dl_shortcut_nodes[i].under;
        u32 head=p+0x14,node=dl_word((void*)head,0),steps=0;
        while(node!=head) {
            if(++steps>DL_SHORTCUT_LIMIT || !dl_shortcut_add(node,under)) return;
            node=dl_word((void*)node,0);
        }
        /* Main names Shortcut directly. Other nested HUD layouts (hearts,
         * counters, etc.) are outside the target and need not consume the bound. */
        u32 vtable=dl_word((void*)p,8);
        if(under && (vtable==(u32)&WWHD_GAME_DATA(0x1010BBC0,u8) ||
           vtable==(u32)&WWHD_GAME_DATA(0x10000610,u8))) {
            if(!dl_game_span(p,0xb4)) return;
            u32 nested=dl_word((void*)p,0xac);
            if(nested && (!dl_game_span(nested,0x10) ||
                !dl_shortcut_add(dl_word((void*)nested,0xc),under))) return;
        }
        if(under) {
            f32 y=*(f32*)(p+0x64),target=y-row;
            if(!(y>-10000 && y<10000 && target>-10000 && target<10000)) return;
            union {f32 value;u32 bits;} encoded={.value=target};
            dl_shortcut_nodes[i].original=dl_word((void*)p,0x64);
            dl_shortcut_nodes[i].written=encoded.bits;
        }
    }
    dl_shortcut_owner=(u32)parts; dl_shortcut_depth=1;
    for(u32 i=0;i<dl_shortcut_count;++i) if(dl_shortcut_nodes[i].under)
        *(u32*)(dl_shortcut_nodes[i].pane+0x64)=dl_shortcut_nodes[i].written;
}
WWHD_HOOK_RETURN(0x026F8D08,dl_shortcut_draw_leave,(void* parts,void* info)) {
    (void)info;
    if(!dl_shortcut_owner) return;
    if(dl_shortcut_depth>1) { --dl_shortcut_depth; return; }
    if((u32)parts!=dl_shortcut_owner) return;
    for(u32 i=0;i<dl_shortcut_count;++i) if(dl_shortcut_nodes[i].under) {
        u32 p=dl_shortcut_nodes[i].pane;
        if(dl_word((void*)p,0x64)==dl_shortcut_nodes[i].written)
            *(u32*)(p+0x64)=dl_shortcut_nodes[i].original;
    }
    dl_shortcut_owner=dl_shortcut_depth=dl_shortcut_count=0;
}
