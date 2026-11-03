/* Reserve the Dark Link row in Main_00's calculation INPUT, before NW4F
 * computes the embedded Shortcut layout. Never patch/restore draw matrices.
 *
 * Native evidence (ZeldaWWHDDecomp/wwhd b2811a3):
 * - hd_screen_hud_main.cpp: Main applies HeartPos/MagicPos, then vCalcLayout.
 * - hd_ui_020021FC.cpp: drawLayoutScreenWithInfo (02002D20) skips an
 *   accessor whose root +0x0c is nonzero (an attached pane's parent).
 * Thus counting Shortcut::draw calls does NOT prove that screen draws itself.
 * - hd_parts_lyt.h / runtime/src/aspect.cpp: pane links, SRT and dirty bit.
 * Walk the real Main root; N_Default_00 is not assumed to be an ancestor.
 */
#pragma once
#define DL_SHORTCUT_LIMIT 256
static u32 dl_shortcut_nodes[DL_SHORTCUT_LIMIT];
static u32 dl_shortcut_count,dl_shortcut_owner,dl_shortcut_depth,dl_shortcut_pane;
static u32 dl_shortcut_original[2],dl_shortcut_written[2];
static u32 dl_shortcut_calcs,dl_shortcut_applied,dl_shortcut_rejected;
static u32 dl_shortcut_seen,dl_shortcut_observed,dl_shortcut_shift;
static unsigned long long dl_shortcut_epoch;
static int dl_shortcut_add(u32 pane) {
    if(!dl_game_span(pane,0xa4) || dl_shortcut_count==DL_SHORTCUT_LIMIT) return 0;
    for(u32 i=0;i<dl_shortcut_count;++i) if(dl_shortcut_nodes[i]==pane) return 0;
    dl_shortcut_nodes[dl_shortcut_count++]=pane;
    return 1;
}
static int dl_shortcut_find(void* parts,u32* result) {
    u32 layout=dl_word(parts,0x44);
    if(!dl_game_span(layout,8)) return 0;
    u32 accessor=dl_word((void*)layout,4);
    if(!dl_game_span(accessor,0x10)) return 0;
    dl_shortcut_count=0;*result=0;
    if(!dl_shortcut_add(dl_word((void*)accessor,0xc))) return 0;
    for(u32 i=0;i<dl_shortcut_count;++i) {
        u32 p=dl_shortcut_nodes[i];
        if(dl_str_equal((const char*)(p+0x80),"L_Shortcut_00",24)) {
            if(*result) return 0; /* ambiguous placement: do not move either */
            *result=p;
        }
        /* Only Main's own pane tree. Nested parts resources are not needed to
         * identify the placement, and unrelated hearts/counters stay untouched. */
        u32 head=p+0x14,node=dl_word((void*)head,0),steps=0;
        while(node!=head) {
            if(++steps>DL_SHORTCUT_LIMIT || !dl_shortcut_add(node) ||
               dl_word((void*)node,0xc)!=p) return 0;
            node=dl_word((void*)node,0);
        }
    }
    return *result!=0;
}
WWHD_HOOK(0x026F8CD0,dl_shortcut_calc_enter,(void* parts)) {
    if(dl_shortcut_owner) { ++dl_shortcut_depth; return; }
    if(!dl_game_span((u32)parts,0x50) ||
       dl_word(parts,4)!=(u32)&WWHD_GAME_DATA(0x100F7A48,u8)) return;
    dl_probe_increment(&dl_shortcut_calcs);
    dl_shortcut_observed=dl_shortcut_shift=0;
    if(!dl_show_hud || !dl_owner || !dl_allowed(dl_owner) ||
       wwhd_hud_epoch()!=dl_magic_epoch) return;
    u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    u32 capacity=save?*((u8*)save+0x33):0;
    wwhd_hud_element gauge;
    if(!dl_magic_element(capacity>16,0,&gauge) || !(gauge.rgba&255)) return;
    f32 row=(gauge.h+8)*(capacity?1:2);
    if(!(row>8 && row<320)) return;
    u32 p;
    if(!dl_shortcut_find(parts,&p)) { dl_probe_increment(&dl_shortcut_rejected); return; }
    u32 parent=dl_word((void*)p,0xc);
    if(!dl_game_span(parent,0xa4)) { dl_probe_increment(&dl_shortcut_rejected); return; }
    /* Convert a downward layout-space vector through the parent's inverse 2D
     * basis. Scaling/rotation must not alter the reserved screen-space height. */
    f32 a=*(f32*)(parent+0x48),b=*(f32*)(parent+0x4c);
    f32 c=*(f32*)(parent+0x58),d=*(f32*)(parent+0x5c),det=a*d-b*c;
    f32 x=*(f32*)(p+0x1c),y=*(f32*)(p+0x20);
    if(!(dl_abs(det)>0.0001f && dl_abs(det)<10000 &&
         x>-10000 && x<10000 && y>-10000 && y<10000)) {
        dl_probe_increment(&dl_shortcut_rejected);return;
    }
    f32 nx=x+b*row/det,ny=y-a*row/det;
    if(!(nx>-10000 && nx<10000 && ny>-10000 && ny<10000)) {
        dl_probe_increment(&dl_shortcut_rejected);return;
    }
    dl_shortcut_owner=(u32)parts;dl_shortcut_depth=1;dl_shortcut_pane=p;
    union {f32 value;u32 bits;} encoded[2]={{.value=nx},{.value=ny}};
    for(u32 i=0;i<2;++i) {
        dl_shortcut_original[i]=dl_word((void*)p,0x1c+i*4);
        dl_shortcut_written[i]=encoded[i].bits;
        *(u32*)(p+0x1c+i*4)=encoded[i].bits;
    }
    *((u8*)p+0x44)|=0x10; /* native calculation must consume the new input */
    dl_shortcut_observed=p;dl_shortcut_epoch=dl_magic_epoch;dl_shortcut_shift=(u32)(row+0.5f);
    dl_probe_increment(&dl_shortcut_applied);
}
WWHD_HOOK_RETURN(0x026F8CD0,dl_shortcut_calc_leave,(void* parts)) {
    if(!dl_shortcut_owner) return;
    if(dl_shortcut_depth>1) { --dl_shortcut_depth;return; }
    if((u32)parts!=dl_shortcut_owner) return;
    u32 p=dl_shortcut_pane;
    for(u32 i=0;i<2;++i) if(dl_word((void*)p,0x1c+i*4)==dl_shortcut_written[i])
        *(u32*)(p+0x1c+i*4)=dl_shortcut_original[i];
    /* Keep the calculated result for drawing. Dirty again so disabling the
     * gauge recomputes native geometry even when there is no animation. */
    *((u8*)p+0x44)|=0x10;
    dl_shortcut_owner=dl_shortcut_depth=dl_shortcut_pane=0;
}
/* Read-only observation at the actual pane draw, not hdParts::draw (which can
 * skip embedded layouts). No cached address is dereferenced: walk only the
 * current draw's live ancestry and compare identities. */
WWHD_HOOK(0x02877100,dl_shortcut_pane_draw,(void* object,void* info)) {
    (void)info;
    if(!dl_diagnostics || !dl_shortcut_observed ||
       wwhd_hud_epoch()!=dl_shortcut_epoch) return;
    u32 p=(u32)object;
    for(u32 n=0;n<32 && dl_game_span(p,0xa4);++n) {
        if(p==dl_shortcut_observed) { dl_probe_increment(&dl_shortcut_seen);return; }
        p=dl_word((void*)p,0xc);
    }
}
