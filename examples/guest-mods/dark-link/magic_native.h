/* Native MagicBar_00 proof: a separately constructed layout, green and full.
 * Game code draws every pane/material. No texture decoding or cached PNGs.
 * Evidence: ZeldaWWHDDecomp/wwhd b2811a3, hd_screen_magic_bar.cpp,
 * hd_parts.cpp, hd_ui_020021FC.cpp and hd_ui_02004A3C.cpp.
 * All addresses are USA identities relocated by the guest-mod translator. */
#pragma once
WWHD_GAME_FUNC(0x0273B050,u32,dl_ui_alloc,(u32,u32,u32));
WWHD_GAME_FUNC(0x0267516C,u32,dl_magic_construct,(u32));
WWHD_GAME_FUNC(0x02675294,u32,dl_magic_init,(u32,u32,u32,u32));
WWHD_GAME_FUNC(0x02674D84,void,dl_magic_destroy,(u32,u32));
WWHD_GAME_FUNC(0x026F8B00,void,dl_parts_finalize,(u32));
WWHD_GAME_FUNC(0x026F8CD0,void,dl_parts_calc,(u32));
WWHD_GAME_FUNC(0x026F8D08,void,dl_parts_draw,(u32,u32));
WWHD_GAME_FUNC(0x02674DF0,void,dl_magic_frame,(u32,u32,f32));
WWHD_GAME_FUNC(0x02004E04,u32,dl_anim_bind,(u32,u32,const u32*,const u32*));
WWHD_GAME_FUNC(0x026FBB24,u32,dl_layout_archive,(u32,const u32*));

static int dl_ui_span(u32 p,u32 n) {
    if(p&3u) return 0;
    return (p>=0x10000000u && p<0x50000000u && n<=0x50000000u-p) ||
           (p>=0xf4000000u && p<0xf6000000u && n<=0xf6000000u-p);
}
static int dl_str_equal(const char* a,const char* b,u32 n) {
    for(u32 i=0;i<n;++i) { if(a[i]!=b[i])return 0;if(!a[i])return 1; }return 0;
}
static f32 dl_abs(f32 v) { return v<0?-v:v; }
static f32 dl_gauge_fraction(void) {
    f32 v=dl_config.free_transform?1:dl_state.active?dl_state.remaining/dl_state.total:dl_state.charge/dl_config.capacity;
    return v<0?0:v>1?1:v;
}
static int dl_blob_span(u32 base,u32 size,u32 at,u32 n) {
    return at>=base && at-base<=size && n<=size-(at-base);
}
#include "magic_resources.h"

/* Separate records support both TV/GamePad HUD owners. Native allocations and
 * these guest records are restored together by full states: a HUD-image epoch
 * change must NOT discard or free still-owned native objects. */
#define DL_NATIVE_GAUGES 4
typedef struct {
    u32 owner,source_layout,source_root,copy,layout,root,ready;
    f32 box[4];
} DLNativeGauge;
static DLNativeGauge dl_native_gauges[DL_NATIVE_GAUGES];
static u32 dl_native_busy,dl_native_created,dl_native_draws,dl_native_error;
static unsigned long long dl_magic_epoch;
static int dl_magic_position_valid;

static u32 dl_native_root(u32 parts,u32* layout) {
    if(!dl_ui_span(parts,0x8c))return 0;
    *layout=dl_word((void*)parts,0x44);
    if(!dl_ui_span(*layout,0x110))return 0;
    u32 accessor=dl_word((void*)*layout,4);
    if(!dl_ui_span(accessor,0x34))return 0;
    u32 root=dl_word((void*)accessor,0xc);
    return dl_ui_span(root,0xa4)?root:0;
}
static u32 dl_native_entry(u32 layout,u32 index) {
    u32 set=dl_word((void*)layout,0xd4);
    if(!dl_ui_span(set,0x18) || dl_word((void*)set,4)!=7 || dl_word((void*)set,12)!=5)return 0;
    u32 entries=dl_word((void*)set,8);
    if(!dl_ui_span(entries,28))return 0;
    u32 entry=dl_word((void*)entries,index*4);
    return dl_ui_span(entry,0x3c) && dl_ui_span(dl_word((void*)entry,0x34),0x30)?entry:0;
}
static int dl_native_live(DLNativeGauge* g) {
    u32 layout=0;
    return dl_ui_span(g->copy,0x8c) && dl_ui_span(g->owner,0x8c) &&
        dl_word((void*)g->copy,4)==(u32)&WWHD_GAME_DATA(0x100F7548,u8) &&
        dl_word((void*)g->owner,4)==(u32)&WWHD_GAME_DATA(0x100F7548,u8) &&
        dl_native_root(g->owner,&layout)==g->source_root && layout==g->source_layout &&
        dl_native_root(g->copy,&layout)==g->root && layout==g->layout;
}
static void dl_native_release(DLNativeGauge* g) {
    u32 copy=g->copy;
    /* A recycled heap address is never ours to destroy. The normal lifetime
     * path runs before the original owner releases the backing UI resources. */
    int owned=dl_ui_span(copy,0x8c) &&
        dl_word((void*)copy,4)==(u32)&WWHD_GAME_DATA(0x100F7548,u8) &&
        (!g->layout || dl_word((void*)copy,0x44)==g->layout);
    memset(g,0,sizeof *g); /* remove ownership before native destructor hooks */
    if(owned) { ++dl_native_busy;dl_parts_finalize(copy);dl_magic_destroy(copy,1);--dl_native_busy; }
}
WWHD_HOOK(0x026F8B00,dl_native_finalize,(u32 parts)) {
    if(dl_native_busy)return;
    for(u32 i=0;i<DL_NATIVE_GAUGES;++i) if(dl_native_gauges[i].owner==parts)dl_native_release(&dl_native_gauges[i]);
}
WWHD_HOOK(0x02674D84,dl_native_destroy,(u32 parts,u32 flags)) {
    (void)flags;dl_native_finalize(parts);
}

/* Called after the original init; a NULL descriptor requests a fresh layout,
 * rather than binding the original Main_00 parts pane. Never memcpy UI objects:
 * their intrusive lists, materials and animation controllers must be distinct. */
WWHD_HOOK_RETURN(0x02675294,dl_native_init_done,(u32 owner,u32 resource,u32 option,u32 desc)) {
    (void)desc;if(dl_native_busy)return;
    u32 source_layout=0,source_root=dl_native_root(owner,&source_layout);
    if(!source_root || dl_word((void*)owner,4)!=(u32)&WWHD_GAME_DATA(0x100F7548,u8))return;
    DLNativeGauge* g=0;
    for(u32 i=0;i<DL_NATIVE_GAUGES;++i) if(dl_native_gauges[i].owner==owner) {
        dl_native_release(&dl_native_gauges[i]);g=&dl_native_gauges[i];break;
    }
    if(!g)for(u32 i=0;i<DL_NATIVE_GAUGES;++i)if(!dl_native_gauges[i].owner){g=&dl_native_gauges[i];break;}
    if(!g){dl_native_error=1;return;}
    g->owner=owner;g->source_layout=source_layout;g->source_root=source_root;
    u32 ui=WWHD_GAME_DATA(0x1018C404,u32);
    if(!dl_ui_span(ui,0x14)){dl_native_error=2;return;}
    u32 heap=dl_word((void*)ui,0x10);
    if(!dl_ui_span(heap,0x10)){dl_native_error=2;return;}
    ++dl_native_busy;
    u32 copy=dl_ui_alloc(0x8c,heap,4);
    if(copy)copy=dl_magic_construct(copy);
    if(!copy){--dl_native_busy;dl_native_error=3;return;}
    g->copy=copy;
    u32 ok=dl_magic_init(copy,resource,option,0);
    --dl_native_busy;
    g->root=dl_native_root(copy,&g->layout);
    if(!ok || !g->root || g->layout==source_layout || g->root==source_root ||
       dl_word((void*)g->root,0xc)) {
        /* Aliased objects are not ours to finalize. Detach their layout first. */
        if(g->layout==source_layout || (g->root && g->root==source_root)) {
            *(u32*)(copy+0x44)=0;g->layout=0;
        }
        dl_native_release(g);dl_native_error=4;return;
    }
    /* Standalone loads may omit the parent's two progress bindings. Resolve
     * their exact native BFLAN tags, then let the game bind/evaluate them. */
    DLNativeTags tags;
    if(!dl_native_tags(&tags)){dl_native_release(g);dl_native_error=5;return;}
    u32 set=dl_word((void*)g->layout,0xd4);
    if(!dl_ui_span(set,0x18) || dl_word((void*)set,4)!=7 || dl_word((void*)set,12)!=5 ||
       !dl_ui_span(dl_word((void*)set,8),28) || !dl_ui_span(dl_word((void*)set,16),20)) {
        dl_native_release(g);dl_native_error=6;return;
    }
    for(u32 i=0;i<2;++i)if(!dl_native_entry(g->layout,i)) {
        u32 name[2]={(u32)tags.progress[i],(u32)&WWHD_GAME_DATA(0x100F737C,u8)};
        u32 group[2]={(u32)"G_Bar_00",name[1]};dl_anim_bind(set,i,name,group);
    }
    for(u32 i=0;i<6;++i)if(!dl_native_entry(g->layout,i)) {
        dl_native_release(g);dl_native_error=6;return;
    }
    dl_magic_frame(copy,2,tags.in_end);
    dl_magic_frame(copy,3,0);dl_magic_frame(copy,4,0);dl_magic_frame(copy,5,0);
    ++dl_native_created;dl_native_error=0;
}

/* Update on the game thread after Main_00's matrices are available. The native
 * draw hook submits the prepared copy with the SAME DrawInfo as its owner.
 * No game functions/graphics commands run from the host overlay callback. */
static void dl_native_calculate(u32 main) {
    if(dl_native_busy || !dl_ui_span(main,0x50) ||
       dl_word((void*)main,4)!=(u32)&WWHD_GAME_DATA(0x100F7A48,u8))return;
    dl_magic_epoch=wwhd_hud_epoch();dl_magic_position_valid=0;
    u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32),v=save && *((u8*)save+0x33)>16;
    for(u32 i=0;i<DL_NATIVE_GAUGES;++i) {
        DLNativeGauge* g=&dl_native_gauges[i];g->ready=0;
        if(!dl_native_live(g))continue;
        u32 base=dl_word((void*)g->owner,0x5c+v*4),r=g->source_root;
        if(!dl_ui_span(base,0xa4))continue;
        f32 w=dl_abs(*(f32*)(base+0x48)**(f32*)(base+0x3c));
        f32 h=dl_abs(*(f32*)(base+0x5c)**(f32*)(base+0x40));
        f32 x=*(f32*)(base+0x54),y=*(f32*)(base+0x64);
        f32 sx=*(f32*)(r+0x48),sy=*(f32*)(r+0x5c);
        f32 rx=*(f32*)(r+0x54),ry=*(f32*)(r+0x64),rz=*(f32*)(r+0x74);
        /* HUD roots are axis-aligned. Unsupported/corrupt matrices never reach
         * native draw. No modification of the original root or its materials. */
        if(!(w>8 && w<600 && h>2 && h<150 && x>-2000 && x<2000 && y>-1000 && y<1000 &&
             sx>0 && sx<10 && sy>0 && sy<10 && rx>-3000 && rx<3000 && ry>-2000 && ry<2000 && rz>-2000 && rz<2000) ||
           *(f32*)(r+0x4c)!=0 || *(f32*)(r+0x50)!=0 || *(f32*)(r+0x58)!=0 || *(f32*)(r+0x60)!=0)continue;
        ++dl_native_busy;
        dl_magic_frame(g->copy,v,v?32:16); /* visual proof: always full, native green */
        *(f32*)(g->root+0x1c)=rx;*(f32*)(g->root+0x20)=ry-h-8;*(f32*)(g->root+0x24)=rz;
        for(u32 j=0;j<3;++j)*(f32*)(g->root+0x28+j*4)=0;
        *(f32*)(g->root+0x34)=sx;*(f32*)(g->root+0x38)=sy;
        *((u8*)g->root+0x44)|=0x11;*((u8*)g->root+0x45)=255;
        dl_parts_calc(g->copy);
        --dl_native_busy;
        u32 origin=*((u8*)base+0x47),bh=origin&3,bv=(origin>>2)&3;
        g->box[0]=640+x+(bh==1?0:bh==2?-w:-w/2);
        g->box[1]=360-y+(bv==1?h:bv==2?0:h/2)+8;
        g->box[2]=w;g->box[3]=h;g->ready=1;dl_magic_position_valid=1;
    }
}
WWHD_HOOK_RETURN(0x026F8D08,dl_native_draw,(u32 parts,u32 info)) {
    if(dl_native_busy || !dl_show_hud || !dl_owner || !dl_allowed(dl_owner))return;
    for(u32 i=0;i<DL_NATIVE_GAUGES;++i) {
        DLNativeGauge* g=&dl_native_gauges[i];
        if(g->owner==parts && g->ready && dl_native_live(g)) {
            /* Per-owner draw context handles embedded/standalone and TV/DRC.
             * Drawing the copy re-enters this hook, but its pointer is never
             * an owner. Avoid a process-global render-thread recursion guard. */
            dl_parts_draw(g->copy,info);++dl_native_draws;return;
        }
    }
}
/* Geometry only, for the confirmed shortcut placement fix. Never emitted as
 * an overlay rectangle/image. Native drawing owns the gauge. */
static int dl_magic_element(u32 variant,u32 layer,wwhd_hud_element* out) {
    (void)variant;(void)layer;
    for(u32 i=0;i<DL_NATIVE_GAUGES;++i) {
        DLNativeGauge* g=&dl_native_gauges[i];if(!g->ready || !dl_native_live(g))continue;
        *out=(wwhd_hud_element){.x=g->box[0],.y=g->box[1],.w=g->box[2],.h=g->box[3],.rgba=0xffffffff};return 1;
    }return 0;
}
