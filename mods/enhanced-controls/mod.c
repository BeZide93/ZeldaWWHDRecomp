/* SPDX-License-Identifier: MPL-2.0 */
#include "wwhd_guest.h"
#include "wwhd/functions.h"
#include "core.h"

/* Only verified public game calls. Data bases use SDK relocations for USA/EU. */
WWHD_GAME_FUNC(0x024FCBE8, u8, camera_line_check, (void*, void*, void*, u32));
#define PLAY ((u8*)&WWHD_GAME_DATA(0x1046F0B0,u8))
#define U8(p,o) (*(volatile u8*)((u8*)(p)+(o)))
#define U16(p,o) (*(volatile u16*)((u8*)(p)+(o)))
#define U32(p,o) (*(volatile u32*)((u8*)(p)+(o)))
#define F32(p,o) (*(volatile f32*)((u8*)(p)+(o)))

static EcState state;
static int registered, ready, mask, pinch_on, tap_on, claims_valid;
static EcRect rects[5], claimed[5];
static unsigned claimed_mask;
static unsigned long long last_step, distance_revision, hud_epoch;
static u32 player_id;
static wwhd_input_state sampled; /* service buffers must belong to the mod */
static wwhd_hud_element element;
static float size = 1;
static const char* const names[] = {"Jump", "Slash", "Spin", "Dodge"};
static const char* const toggle_ids[] = {"jump", "slash", "spin", "dodge"};

static int object(u32 p, u32 bytes) { return !(p&3) && p >= 0x10000000 && p <= 0x50000000-bytes; }
static int is_menu(void) {
    if (WWHD_GAME_DATA(0x101EA069,u8) || WWHD_GAME_DATA(0x101F8348,u32)==2) return 1;
    u32 ui=WWHD_GAME_DATA(0x101F8344,u32);
    if (!object(ui,0x24c)) return 0;
    u32 current=U32(ui,0x18);
    u32 desc=object(current,12) ? U32(current,8) : U32(ui,0x14);
    u32 bag=U32(ui,0x17c);
    if (object(bag,0x58) && U32(bag,0x54)<3) return 1;
    /* Exact relocated scene descriptors from upstream gfx/auto_pip.h. */
#define MENU(a) if (desc==(u32)&WWHD_GAME_DATA(a,u8)) return 1
    MENU(0x1049EC84); MENU(0x1049ECB0); MENU(0x1049ECDC); MENU(0x1049ED08);
    MENU(0x1049ED34); MENU(0x1049ED60); MENU(0x1049ED8C); MENU(0x1049EDB8);
    MENU(0x1049EDE4); MENU(0x1049EE10); MENU(0x1049EE3C); MENU(0x1049EEEC);
    MENU(0x1049EF18); MENU(0x1049EF44); MENU(0x1049EF70); MENU(0x1049EF9C);
    MENU(0x1049EFC8); MENU(0x1049EFF4); MENU(0x1049F020); MENU(0x1049F04C);
    MENU(0x1049F078); MENU(0x1049F0A4); MENU(0x1049F0D0); MENU(0x1049F0FC);
    MENU(0x1049F1D8); MENU(0x1049F204); MENU(0x1049F288); MENU(0x1049F2B4);
    MENU(0x1049F2E0); MENU(0x1049F30C); MENU(0x1049F338);
    MENU(0x1049F498); MENU(0x1049F4C4); MENU(0x1049F4F0);
#undef MENU
    return 0;
}
static int playable(void) {
    u8* p=PLAY;
    u32 link=U32(p,0x5B2C);
    if (!object(link,0x8284) || !U32(link,0) || !U32(link,4) || U32(link,4)==0xffffffff) return 0;
    if (!p[0x5134] || (p[0x5134]=='s' && p[0x5135]=='e' && p[0x5136]=='a' && p[0x5137]=='_' && p[0x5138]=='T') ||
        (p[0x5134]=='N' && p[0x5135]=='a' && p[0x5136]=='m' && p[0x5137]=='e')) return 0;
    if (p[0x514C] || p[0x5292] || p[0x5BB2] || p[0x5BB3] || WWHD_GAME_DATA(0x101F36CC,u32) || is_menu()) return 0;
    u32 save=WWHD_GAME_DATA(0x101F84DC,u32);
    if (!object(save,0x24) || !U16(save,0x22)) return 0;
    /* Stop during first-person aiming, climbing, swimming, sailing and conducting. */
    const u32 special0=0x100|0x1000|0x2000|0x4000|0x10000|0x20000|0x80000|0x100000|0x200000|0x400000|0x800000|0x2000000|0x4000000|0x8000000;
    if (U32(p,0x5CD8)&special0 || U32(p,0x5CDC)&(1|8|0x20|0x400)) return 0;
    if (U32(link,0x6A70)&(0x20|0x200|0x800|0x2000|0x10000|0x40000|0x100000|0x200000|0x400000|0x800000|0x1000000|0x20000000)) return 0;
    return 1;
}
static void clear_claims(void) {
    if (!claims_valid) return;
    for (u32 i=0;i<5;++i) wwhd_touch_claim(i,0,0,0,0);
    claims_valid=0; claimed_mask=0;
}
static void update_claims(void) {
    unsigned wanted=ready ? (unsigned)mask | ((pinch_on||tap_on)?16u:0u) : 0;
    if (!wanted) { clear_claims(); return; }
    int changed=!claims_valid || wanted!=claimed_mask;
    for (int i=0;i<5;++i) {
        EcRect a=rects[i], b=claimed[i];
        if (a.x!=b.x || a.y!=b.y || a.w!=b.w || a.h!=b.h) changed=1;
    }
    if (!changed) return; /* Do not steal overlapping claims back every frame. */
    ec_cancel(&state);
    for (u32 i=0;i<5;++i) {
        EcRect r=rects[i]; claimed[i]=r;
        if (wanted&(1u<<i)) wwhd_touch_claim(i,r.x-r.w/2,r.y-r.h/2,r.x+r.w/2,r.y+r.h/2);
        else wwhd_touch_claim(i,0,0,0,0);
    }
    claims_valid=1; claimed_mask=wanted;
}
static float number(const char* id,float fallback,float lo,float hi) {
    return ec_clamp((float)wwhd_config_float(id,fallback),lo,hi);
}
static void refresh(void) {
    unsigned long long step=wwhd_logic_step(), epoch=wwhd_hud_epoch();
    if (step!=last_step || epoch!=hud_epoch) {
        if (epoch!=hud_epoch || step<last_step || step-last_step>1) {
            ec_cancel(&state); clear_claims();
        } else ec_tick(&state,wwhd_logic_dt());
        last_step=step; hud_epoch=epoch;
        int new_mask=0;
        for (int i=0;i<4;++i) if(wwhd_config_bool(toggle_ids[i],1)) new_mask|=1<<i;
        int new_pinch=wwhd_config_bool("pinch",1), new_tap=wwhd_config_bool("double-tap",1);
        if(new_mask!=mask || new_pinch!=pinch_on || new_tap!=tap_on) ec_cancel(&state);
        mask=new_mask; pinch_on=new_pinch; tap_on=new_tap;
        unsigned long long rev=wwhd_config_changed("distance");
        if (rev!=distance_revision) {
            state.distance=number("distance",100,50,200)/100;
            state.pinching=state.tap_valid=state.previous_tap=0; distance_revision=rev;
        }
        size=number("button-size",100,60,140)/100;
        float x=number("buttons-x",0,-350,350), y=number("buttons-y",-180,-270,270);
        for (int i=0;i<4;++i) rects[i]=(EcRect){x+(i-1.5f)*100*size,y,88*size,60*size};
        rects[4]=(EcRect){number("gesture-x",420,150,480),number("gesture-y",120,-200,250),260,140};
    }
    int next=playable();
    u32 id=next ? U32(U32(PLAY,0x5B2C),4) : 0;
    if (!next || id!=player_id) ec_cancel(&state);
    player_id=id; ready=next;
    update_claims();
}
static void touch(const wwhd_touch_event* event) {
    refresh(); if(!ready) return;
    float x=event->x, y=event->y;
    switch(event->kind) {
        case WWHD_TOUCH_DOWN: {
            int hit=EC_NONE;
            /* Claim slot 4 is newest and wins an overlap, matching the host. */
            if ((pinch_on||tap_on) && ec_inside(rects[4],x,y)) hit=4;
            else for(int i=0;i<4;++i) if((mask&(1<<i)) && ec_inside(rects[i],x,y)) hit=i;
            ec_down(&state,hit,x,y);
            if(hit>=0 && hit<4) {
                wwhd_input_read(&sampled);
                /* Attack macros need a drawn sword; dodge works without it. */
                if(hit==EC_DODGE || U16(U32(PLAY,0x5B2C),0x69B0)==0x103) ec_start(&state,hit,sampled.lx);
            }
            break;
        }
        case WWHD_TOUCH_MOVE: ec_move(&state,x,y); break;
        case WWHD_TOUCH_UP: ec_up(&state,x,y,tap_on); break;
        case WWHD_TOUCH_CANCEL: ec_cancel(&state); break;
        case WWHD_TOUCH_PINCH:
            /* Wheel events have no preceding DOWN; all pinch locations owned
             * by this mod support zoom, including the macro buttons. */
            state.tap_valid=state.previous_tap=0;
            if(pinch_on) { state.macro=EC_NONE; ec_pinch(&state,x); }
            break;
        case WWHD_TOUCH_PINCH_END:
            state.pinching=state.tap_valid=state.previous_tap=0; break;
    }
}
static void filter(wwhd_input_state* pad) {
    refresh(); if(!ready) return;
    /* Always preserve real buttons. The macro only supplies additional holds. */
    pad->buttons |= ec_buttons(&state);
    ec_stick(&state,&pad->lx,&pad->ly);
}
static void box(u32 list,EcRect r,const char* label,u32 color) {
    element=(wwhd_hud_element){.kind=WWHD_HUD_RECT,.anchor=WWHD_HUD_CENTER,
        .x=640+r.x-r.w/2,.y=360-r.y-r.h/2,.w=r.w,.h=r.h,
        .thickness=1,.u1=1,.v1=1,.rgba=color};
    wwhd_hud_emit(list,&element);
    unsigned n=0; while(label[n]) ++n;
    element.kind=WWHD_HUD_TEXT; element.x+=10; element.y+=r.h/2-9;
    element.size=18; element.rgba=0xffffffff; element.text=label; element.text_bytes=n;
    wwhd_hud_emit(list,&element);
}
static char zoom_label[]="Pinch / double tap 100%";
static void hud(u32 list) {
    refresh(); if(!ready) return;
    for(int i=0;i<4;++i) if(mask&(1<<i)) box(list,rects[i],names[i],state.macro==i?0x397a96df:0x14222cb8);
    if(pinch_on||tap_on) {
        unsigned n=(unsigned)(state.distance*100+.5f);
        zoom_label[19]='0'+n/100; zoom_label[20]='0'+(n/10)%10; zoom_label[21]='0'+n%10;
        box(list,rects[4],zoom_label,0x14222c78);
    }
}
WWHD_HOOK(WWHD_ADDR_daPy_Execute,enhanced_register,(void* link)) {
    (void)link;
    if(!registered) {
        ec_init(&state); last_step=~0ull;
        wwhd_touch_register(touch); wwhd_input_filter_register(filter);
        wwhd_hud_register(hud,WWHD_HUD_TV); registered=1;
        wwhd_log("Enhanced Controls: guest mod ready (experimental)");
    }
    refresh();
}

typedef struct { void* body; float eye[3],center[3],end[3]; } Ray;
static int blocked(void* data,float factor) {
    Ray* r=data;
    for(int i=0;i<3;++i) r->end[i]=r->center[i]+(r->eye[i]-r->center[i])*factor;
    return camera_line_check(r->body,r->center,r->end,0x7f)!=0;
}
/* store(camera_process_class*) copies the body's newly computed view into the
 * process view before view_setup. Adjust that output, leaving the camera body's
 * simulation untouched. Upstream interpolation/culling then see the same view.
 * Entry/draw patches would miss upstream's cached camera and risk zoom feedback. */
WWHD_HOOK_RETURN(WWHD_ADDR_store_024F8430,enhanced_camera_store,(void* camera)) {
    if(!registered) return;
    refresh();
    if(!ready || !pinch_on || state.distance==1 || (u32)camera!=U32(PLAY,0x5AF8)) return;
    u8* body=(u8*)camera+0x248;
    if(!U8(body,4) || U8(body,5)) return;
    u32 param=U32(body,0x8A8);
    if(object(param,8) && U32(param,4)==11) return; /* event camera */
    Ray ray; ray.body=body;
    float d2=0;
    for(int i=0;i<3;++i) {
        ray.eye[i]=F32(camera,0xDC+4*i); ray.center[i]=F32(camera,0xE8+4*i);
        float d=ray.eye[i]-ray.center[i]; d2+=d*d;
    }
    if(!(d2>=3600 && d2<1.e12f)) return;
    float factor=ec_collision_limit(state.distance,blocked,&ray);
    for(int i=0;i<3;++i) F32(camera,0xDC+4*i)=ray.center[i]+(ray.eye[i]-ray.center[i])*factor;
}
