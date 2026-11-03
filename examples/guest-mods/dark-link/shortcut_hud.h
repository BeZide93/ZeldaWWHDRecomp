/* Shortcut_00 owns a separately drawn hdParts layout, not just Main_00's
 * L_Shortcut_00 placement pane. Native order: hdParts_c::draw (026F8B54)
 * calls vDrawLayout (026F8D08) THEN vDrawChildren (026F9020). The previous
 * Main drawLayout scope ended before the Shortcut child was drawn.
 *
 * Scope this change to hdScreenItemShortcut_c::draw and its owned layouts.
 * Shift final matrices only while those layouts draw; restore exact bits.
 * Evidence: ZeldaWWHDDecomp/wwhd b2811a3ce838d1f875bc8e68536c1950af5d7ffc,
 * d/hd_parts.cpp, d/hd_screen_item_shortcut.cpp, include/d/hd_parts{,_lyt}.h.
 */
#pragma once
#define DL_SHORTCUT_LIMIT 192
#define DL_SHORTCUT_PARTS 16
static struct { u32 pane,original,written; } dl_shortcut_nodes[DL_SHORTCUT_LIMIT];
static u32 dl_shortcut_parts[DL_SHORTCUT_PARTS];
static u32 dl_shortcut_count,dl_shortcut_owner,dl_shortcut_depth;
static u32 dl_shortcut_draws,dl_shortcut_applied,dl_shortcut_rejected;
static int dl_shortcut_add(u32 pane) {
    if(!dl_game_span(pane,0xa4)) return 0;
    /* An owned parts screen and a nested parts pane can reference one layout.
     * Record each matrix once, even when reached by both ownership routes. */
    for(u32 i=0;i<dl_shortcut_count;++i) if(dl_shortcut_nodes[i].pane==pane) return 1;
    if(dl_shortcut_count==DL_SHORTCUT_LIMIT) return 0;
    dl_shortcut_nodes[dl_shortcut_count++].pane=pane;
    return 1;
}
static int dl_shortcut_layout(u32 layout) {
    if(!dl_game_span(layout,8)) return 0;
    u32 accessor=dl_word((void*)layout,4);
    return dl_game_span(accessor,0x10) && dl_shortcut_add(dl_word((void*)accessor,0xc));
}
static int dl_shortcut_collect(u32 shortcut,f32 row) {
    dl_shortcut_count=0;
    dl_shortcut_parts[0]=shortcut;
    u32 parts_count=1;
    for(u32 i=0;i<parts_count;++i) {
        u32 parts=dl_shortcut_parts[i];
        if(!dl_game_span(parts,0x50)) return 0;
        u32 layout=dl_word((void*)parts,0x44);
        /* An empty child may have no layout yet. The Shortcut itself must. */
        if(layout ? !dl_shortcut_layout(layout) : !i) return 0;
        u32 count=dl_word((void*)parts,0x48),children=dl_word((void*)parts,0x4c);
        if(count>DL_SHORTCUT_PARTS || (count && !dl_game_span(children,count*4))) return 0;
        for(u32 n=0;n<count;++n) {
            u32 child=dl_word((void*)children,n*4);
            if(!child) continue;
            for(u32 k=0;k<parts_count;++k) if(dl_shortcut_parts[k]==child) return 0;
            if(parts_count==DL_SHORTCUT_PARTS) return 0;
            dl_shortcut_parts[parts_count++]=child;
        }
    }
    for(u32 i=0;i<dl_shortcut_count;++i) {
        u32 p=dl_shortcut_nodes[i].pane;
        u32 head=p+0x14,node=dl_word((void*)head,0),steps=0;
        while(node!=head) {
            if(++steps>DL_SHORTCUT_LIMIT || !dl_shortcut_add(node)) return 0;
            node=dl_word((void*)node,0);
        }
        u32 vtable=dl_word((void*)p,8);
        if(vtable==(u32)&WWHD_GAME_DATA(0x1010BBC0,u8) ||
           vtable==(u32)&WWHD_GAME_DATA(0x10000610,u8)) {
            if(!dl_game_span(p,0xb4)) return 0;
            u32 nested=dl_word((void*)p,0xac);
            if(nested && (!dl_game_span(nested,0x10) ||
                !dl_shortcut_add(dl_word((void*)nested,0xc)))) return 0;
        }
        f32 y=*(f32*)(p+0x64),target=y-row;
        if(!(y>-10000 && y<10000 && target>-10000 && target<10000)) return 0;
        union {f32 value;u32 bits;} encoded={.value=target};
        dl_shortcut_nodes[i].original=dl_word((void*)p,0x64);
        dl_shortcut_nodes[i].written=encoded.bits;
    }
    return 1;
}
WWHD_HOOK(0x026F8B54,dl_shortcut_draw_enter,(void* parts,void* info)) {
    (void)info;
    if(dl_shortcut_owner) { ++dl_shortcut_depth; return; }
    if(!dl_game_span((u32)parts,0x50) ||
       dl_word(parts,4)!=(u32)&WWHD_GAME_DATA(0x10104B88,u8)) return;
    dl_probe_increment(&dl_shortcut_draws);
    if(!dl_show_hud || !dl_owner || !dl_allowed(dl_owner) ||
       wwhd_hud_epoch()!=dl_magic_epoch) return;
    u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    u32 capacity=save?*((u8*)save+0x33):0;
    wwhd_hud_element gauge;
    if(!dl_magic_element(capacity>16,0,&gauge) || !(gauge.rgba&255)) return;
    f32 row=(gauge.h+8)*(capacity?1:2);
    if(!(row>8 && row<320)) return;
    if(!dl_shortcut_collect((u32)parts,row)) { dl_probe_increment(&dl_shortcut_rejected); return; }
    dl_shortcut_owner=(u32)parts; dl_shortcut_depth=1;
    for(u32 i=0;i<dl_shortcut_count;++i)
        *(u32*)(dl_shortcut_nodes[i].pane+0x64)=dl_shortcut_nodes[i].written;
    dl_probe_increment(&dl_shortcut_applied);
}
WWHD_HOOK_RETURN(0x026F8B54,dl_shortcut_draw_leave,(void* parts,void* info)) {
    (void)info;
    if(!dl_shortcut_owner) return;
    if(dl_shortcut_depth>1) { --dl_shortcut_depth; return; }
    if((u32)parts!=dl_shortcut_owner) return;
    for(u32 i=0;i<dl_shortcut_count;++i) {
        u32 p=dl_shortcut_nodes[i].pane;
        if(dl_word((void*)p,0x64)==dl_shortcut_nodes[i].written)
            *(u32*)(p+0x64)=dl_shortcut_nodes[i].original;
    }
    dl_shortcut_owner=dl_shortcut_depth=dl_shortcut_count=0;
}
