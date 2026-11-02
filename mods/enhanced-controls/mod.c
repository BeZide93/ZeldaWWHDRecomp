/* SPDX-License-Identifier: MPL-2.0 */
#include "wwhd_guest.h"
#include "wwhd/functions.h"
#include "editor.h"

/* Only verified public game calls. Data bases use SDK relocations for USA/EU. */
WWHD_GAME_FUNC(0x023E6380, s32, spin_attack, (void*, s32));
WWHD_GAME_FUNC(0x023D794C, s32, equipping, (void*));
WWHD_GAME_FUNC(0x02443728, s32, guarding, (void*));
WWHD_GAME_FUNC(0x024FCBE8, u8, camera_line_check, (void*, void*, void*, u32));
#define PLAY ((u8*)&WWHD_GAME_DATA(0x1046F0B0,u8))
#define U8(p,o) (*(volatile u8*)((u8*)(p)+(o)))
#define U16(p,o) (*(volatile u16*)((u8*)(p)+(o)))
#define U32(p,o) (*(volatile u32*)((u8*)(p)+(o)))
#define F32(p,o) (*(volatile f32*)((u8*)(p)+(o)))

static EcState state;
static int registered, ready, mask, pinch_on, tap_on, claims_valid;
static EcRect rects[7], claimed[7];
static EcEditor editor;
static EcLayoutFile layout_file[2]; /* host services require mod-owned buffers */
static const char* const layout_names[]={"macro-layout-a.bin","macro-layout-b.bin"};
static int editor_visible=1, release_input;
static void load_layout(void) {
    editor.saved=ee_defaults(); editor.sequence=0; ee_close(&editor);
    for(int i=0;i<2;++i) {
        EcLayoutFile* f=&layout_file[i];
        if(wwhd_file_read(layout_names[i],f,sizeof(*f))==(s32)sizeof(*f) && ee_file_valid(f) &&
           (!editor.sequence || (s32)(f->sequence-editor.sequence)>0)) {
            editor.saved=f->layout; editor.sequence=f->sequence;
        }
    }
}
static int save_layout(void) {
    unsigned sequence=editor.sequence+1; if(!sequence) sequence=1;
    EcLayoutFile* f=&layout_file[sequence&1];
    *f=(EcLayoutFile){0x45434C59u,1,sequence,0,editor.draft}; f->checksum=ee_hash(f);
    if(wwhd_file_write(layout_names[sequence&1],f,sizeof(*f))!=(s32)sizeof(*f)) {
        editor.error=1; return 0;
    }
    editor.saved=editor.draft; editor.sequence=sequence; return 1;
}
static unsigned claimed_mask;
static unsigned long long last_step, hud_epoch;
static u32 player_id;
static wwhd_input_state sampled; /* service buffers must belong to the mod */
static wwhd_hud_element element;
static const char* const names[] = {"Jump", "Slash", "Spin", "Dodge"};

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
    for (u32 i=0;i<7;++i) wwhd_touch_claim(i,0,0,0,0);
    claims_valid=0; claimed_mask=0;
}
static void update_claims(void) {
    unsigned wanted=ready ? (editor.open?64u:((unsigned)mask|16u|(editor_visible?32u:0u))) : 0;
    if (!wanted) { clear_claims(); return; }
    int changed=!claims_valid || wanted!=claimed_mask;
    for (int i=0;i<7;++i) {
        EcRect a=rects[i], b=claimed[i];
        if ((wanted&(1u<<i)) && (a.x!=b.x || a.y!=b.y || a.w!=b.w || a.h!=b.h)) changed=1;
    }
    if (!changed) return; /* Do not steal overlapping claims back every frame. */
    ec_cancel(&state);
    for (u32 i=0;i<7;++i) {
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
            if(epoch!=hud_epoch) load_layout();
            else ee_close(&editor);
        } else ec_tick(&state,wwhd_logic_dt());
        last_step=step; hud_epoch=epoch;
        editor_visible=wwhd_config_bool("editor-visible",1);
        if(!editor_visible && editor.open) ee_close(&editor);
        rects[5]=(EcRect){number("editor-x",0,-570,570),number("editor-y",260,-320,320),96,48};
        rects[4]=(EcRect){420,120,260,140};
        rects[6]=(EcRect){0,0,1280,720};
    }
    int next=playable();
    u32 id=next ? U32(U32(PLAY,0x5B2C),4) : 0;
    if (!next || id!=player_id) { ec_cancel(&state); ee_close(&editor); }
    mask=0;
    for(int i=0;i<4;++i) {
        EcPlacement p=(editor.open?editor.draft:editor.saved).button[i];
        rects[i]=ee_rect(p); if(p.visible) mask|=1<<i;
    }
    player_id=id; ready=next;
    update_claims();
}
static void touch(const wwhd_touch_event* event) {
    refresh(); if(!ready) return;
    float x=event->x, y=event->y;
    if(editor.open) {
        int action=ee_touch(&editor,event->kind,x,y);
        if(action==EE_CANCEL) ee_close(&editor);
        else if(action==EE_SAVE) { if(save_layout()) ee_close(&editor); }
        else if(action>=0) ee_apply(&editor,action);
        ec_cancel(&state); refresh(); return;
    }
    if(event->kind==WWHD_TOUCH_DOWN && editor_visible && ec_inside(rects[5],x,y)) {
        ee_begin(&editor); release_input=1; ec_cancel(&state); refresh(); return;
    }
    switch(event->kind) {
        case WWHD_TOUCH_DOWN: {
            int hit=EC_NONE;
            /* Editable macros take priority over the fixed gesture pad. */
            for(int i=0;i<4;++i) if((mask&(1<<i)) && ec_inside(rects[i],x,y)) hit=i;
            if(hit==EC_NONE && (pinch_on||tap_on) && ec_inside(rects[4],x,y)) hit=4;
            ec_down(&state,hit,x,y);
            if(hit>=0 && hit<4) {
                wwhd_input_read(&sampled);
                /* Attack macros need a drawn sword; dodge works without it. */
                if(hit==EC_DODGE || U16(U32(PLAY,0x5B2C),0x69B0)==0x103) {
                    if(hit!=EC_SPIN || state.macro!=EC_SPIN || !ec_active(&state))
                        ec_start(&state,hit,sampled.lx);
                }
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
            if(pinch_on) { state.macro=EC_NONE; state.spin_pending=0; ec_pinch(&state,x); }
            break;
        case WWHD_TOUCH_PINCH_END:
            state.pinching=state.tap_valid=state.previous_tap=0; break;
    }
}
static void filter(wwhd_input_state* pad) {
    refresh(); if(!ready) return;
    if(editor.open || release_input) {
        int neutral=!pad->buttons && pad->lx>=-.15f && pad->lx<=.15f && pad->ly>=-.15f && pad->ly<=.15f &&
            pad->rx>=-.15f && pad->rx<=.15f && pad->ry>=-.15f && pad->ry<=.15f;
        if(!editor.open && neutral) release_input=0;
        pad->buttons=0; pad->lx=pad->ly=pad->rx=pad->ry=0; return;
    }
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
/* Package-owned images: four 1024x1024 RGBA icons fit the SDK's 16 MiB
 * per-mod pixel limit. Retry missing images only after a HUD epoch change. */
static const char* const icon_paths[] = {
    "assets/combat_jump_attack.png", "assets/combat_vertical_slash.png",
    "assets/combat_spin_attack.png", "assets/combat_dodge.png"
};
static u32 icons[4];
static unsigned icon_attempts;
static unsigned long long icon_epoch=~0ull;
static void sync_icons(void) {
    unsigned long long epoch=wwhd_hud_epoch();
    if(epoch!=icon_epoch) {
        for(int i=0;i<4;++i) icons[i]=0; /* old handles are already invalid */
        icon_attempts=0; icon_epoch=epoch;
    }
    for(int i=0;i<4;++i) {
        EcPlacement p=(editor.open?editor.draft:editor.saved).button[i];
        int needed=p.icons && (p.visible || editor.open);
        if(!needed) { if(icons[i]) wwhd_hud_release(icons[i]); icons[i]=0; icon_attempts&=~(1u<<i); }
        else if(!(icon_attempts&(1u<<i))) {
            icons[i]=wwhd_hud_texture(WWHD_HUD_PACKAGE,icon_paths[i]); icon_attempts|=1u<<i;
        }
    }
}
static void label(u32 list,float x,float y,const char* text,float font,u32 color) {
    unsigned n=0; while(text[n]) ++n;
    element=(wwhd_hud_element){.kind=WWHD_HUD_TEXT,.anchor=WWHD_HUD_CENTER,
        .x=640+x,.y=360-y,.size=font,.rgba=color,.text=text,.text_bytes=n};
    wwhd_hud_emit(list,&element);
}
static void outline(u32 list,EcRect r,u32 color) {
    element=(wwhd_hud_element){.kind=WWHD_HUD_RECT_OUTLINE,.anchor=WWHD_HUD_CENTER,
        .x=640+r.x-r.w/2,.y=360-r.y-r.h/2,.w=r.w,.h=r.h,.thickness=2,.rgba=color};
    wwhd_hud_emit(list,&element);
}
static void line(u32 list,float x,float y,float xx,float yy,u32 color) {
    element=(wwhd_hud_element){.kind=WWHD_HUD_LINE,.anchor=WWHD_HUD_CENTER,
        .x=640+x,.y=360-y,.w=xx-x,.h=y-yy,.thickness=1,.rgba=color};
    wwhd_hud_emit(list,&element);
}
static void macro_button(u32 list,int i) {
    EcPlacement p=(editor.open?editor.draft:editor.saved).button[i];
    EcRect r=rects[i]; float side=r.w<r.h?r.w:r.h;
    u32 alpha=(u32)(255*p.opacity/100);
    if(editor.open && alpha<128) alpha=128; /* editable even if hidden or faint */
    if(editor.open && !p.visible) alpha=96;
    u32 tint=(state.macro==i?0xB0D8FF00u:0xFFFFFF00u)|alpha;
    element=(wwhd_hud_element){.kind=p.shape?(p.filled?WWHD_HUD_RECT:WWHD_HUD_RECT_OUTLINE):
        (p.filled?WWHD_HUD_CIRCLE:WWHD_HUD_CIRCLE_OUTLINE),.anchor=WWHD_HUD_CENTER,
        .x=640+r.x-(p.shape?r.w/2:0),.y=360-r.y-(p.shape?r.h/2:0),
        .w=r.w,.h=r.h,.size=side/2,.thickness=2,.rgba=p.filled?(0x14222C00u|alpha):tint};
    wwhd_hud_emit(list,&element);
    if(icons[i] && p.icons) {
        element=(wwhd_hud_element){.kind=WWHD_HUD_IMAGE,.anchor=WWHD_HUD_CENTER,
            .x=640+r.x-side/2,.y=360-r.y-side/2,.w=side,.h=side,
            .u1=1,.v1=1,.rgba=tint,.image=icons[i]};
        wwhd_hud_emit(list,&element);
    } else label(list,r.x-r.w/2+8,r.y+9,names[i],18*p.size/100.f,tint);
    if(editor.open && editor.selected==i) outline(list,r,0xFFDB70FF);
}
static char editor_text[12][64];
static void editor_value(char* out,const char* prefix,int value,const char* suffix) {
    unsigned n=0; while(*prefix) out[n++]=*prefix++;
    if(value<0) { out[n++]='-'; value=-value; }
    if(value>=100) out[n++]='0'+value/100;
    if(value>=10) out[n++]='0'+(value/10)%10;
    out[n++]='0'+value%10;
    while(*suffix) out[n++]=*suffix++;
    out[n]=0;
}
static void editor_hud(u32 list) {
    box(list,(EcRect){0,245,1260,210},"",0x102332F0);
    if(editor.draft.grid) {
        for(int x=-620;x<=620;x+=20) line(list,x,-320,x,150,0xD0DCEC28);
        for(int y=-320;y<=140;y+=20) line(list,-620,y,620,y,0xD0DCEC28);
    }
    for(int i=0;i<4;++i) if(i!=editor.selected) macro_button(list,i);
    macro_button(list,editor.selected);
    for(int i=0;i<4;++i) box(list,ee_tab(i),names[i],editor.selected==i?0x436B8FFF:0x223C50FF);
    EcPlacement p=editor.draft.button[editor.selected];
    editor_value(editor_text[0],"Smaller ",p.size,"%");
    editor_value(editor_text[1],"Larger ",p.size,"%");
    editor_value(editor_text[2],"Opacity ",p.opacity,"%");
    const char* captions[EE_ACTIONS]={editor_text[0],editor_text[1],editor_text[2],
        p.shape?"Shape: square":"Shape: circle",p.filled?"Fill: on":"Fill: off",
        p.icons?"Style: icon":"Style: text",p.visible?"Visible: yes":"Visible: no",
        editor.draft.grid?"Grid: on":"Grid: off","Reset all","Cancel","Save / Done"};
    for(int i=0;i<EE_ACTIONS;++i) box(list,ee_action_rect(i),captions[i],i==EE_SAVE?0x26745AFF:0x29475FFF);
    label(list,-590,345,"MACRO EDITOR - select a button, then drag it",18,0xFFFFFFFF);
    box(list,(EcRect){0,-342,1280,36},"",0x102332F0);
    label(list,-590,-334,editor.error?"Save failed - retry Save or Cancel":
        "Only macro buttons are edited. The game keeps running; use a safe place.",16,editor.error?0xFF8888FF:0xFFFFFFFF);
}
static char zoom_label[]="Pinch / double tap 100%";
static void hud(u32 list) {
    refresh(); if(!ready) return;
    sync_icons();
    if(editor.open) { editor_hud(list); return; }
    for(int i=0;i<4;++i) if(mask&(1<<i)) macro_button(list,i);
    if(pinch_on||tap_on) {
        unsigned n=(unsigned)(state.distance*100+.5f);
        zoom_label[19]='0'+n/100; zoom_label[20]='0'+(n/10)%10; zoom_label[21]='0'+n%10;
        box(list,rects[4],zoom_label,0x14222c78);
    }
    if(editor_visible) box(list,rects[5],"Edit",0x23445CD0);
}
/* Public HD proc IDs: WAIT, MOVE, ATN_MOVE, ATN_ACTOR_WAIT/MOVE,
 * WAIT_TURN and MOVE_TURN. Never interrupt damage, jumps, rolls, another attack,
 * item use or scripted actions. Recheck at consumption, not in the pad callback.
 * procCutTurn_init(link, 1) is the native quick-spin path in checkNextAction. */
static int can_spin(void* link) {
    u32 proc=U32(link,0x65F0);
    if(proc!=4 && proc!=6 && proc!=7 && proc!=8 && proc!=9 && proc!=0x17 && proc!=0x18) return 0;
    if(U16(link,0x69B0)!=0x103 || U32(link,0x8260) ||
       U32(link,0x65A4) || U32(link,0x6594) || (U32(link,0x3B8)&0x20) ||
       WWHD_GAME_DATA(0x101CEF16,u16)==1) return 0;
    return !equipping(link) && !guarding(link);
}
WWHD_HOOK(WWHD_ADDR_daPy_Execute,enhanced_register,(void* link)) {
    if(!registered) {
        ec_init(&state); load_layout(); pinch_on=tap_on=1; last_step=~0ull;
        wwhd_touch_register(touch); wwhd_input_filter_register(filter);
        wwhd_hud_register(hud,WWHD_HUD_TV); registered=1;
        wwhd_log("Enhanced Controls: guest mod ready (experimental)");
    }
    refresh();
    /* Execute once on Link's game update, never from a touch/HUD/input callback.
     * A refused request is discarded instead of firing later after recovery. */
    if(ec_take_spin(&state)) {
        if(editor.open || !ready || (u32)link!=U32(PLAY,0x5B2C) || !can_spin(link) || !spin_attack(link,1))
            state.macro=EC_NONE;
    }
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
    if(editor.open || !ready || !pinch_on || state.distance==1 || (u32)camera!=U32(PLAY,0x5AF8)) return;
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
