/* A copy of MagicBar_00's authored picture art, loaded from the player's game.
 * No extracted artwork is packaged. Native HD magic controller: 02675414,
 * panes +58 N_All_00, +5c P_MagicBase_00, +60 P_MagicBase_01.
 * SARC/BFLYT layouts: tools/story_mods/lyt and runtime/src/mods/game_layouts. */
#pragma once
WWHD_GAME_FUNC(0x026FBB24, u32, dl_layout_archive, (u32, const u32*));
static int dl_game_span(u32, u32);
#include "magic_art.h"
#define DL_MAGIC_PICTURES 32
static struct { char name[24], material[29]; u32 fill, black, white, vertex[4]; f32 uv[4], tex_srt[5], pane[10], original[10]; } dl_magic_pictures[DL_MAGIC_PICTURES];
static u32 dl_magic_picture_count, dl_magic_loaded, dl_magic_retry;
static unsigned long long dl_magic_epoch;
static wwhd_game_pane dl_magic_query, dl_magic_base[2], dl_magic_fill[2];
static u32 dl_magic_images[2][2], dl_magic_tint[2][2], dl_magic_archive;
static f32 dl_magic_position[4];
static f32 dl_magic_authored[2];
static u32 dl_magic_authored_valid;
static int dl_magic_position_valid;
static char dl_magic_key[64], dl_magic_path[96];
static const char dl_magic_name[]="MagicBar_00";
static u32 dl_magic_string[2];
static int dl_str_equal(const char* a,const char* b,u32 maximum) {
    for (u32 i=0;i<maximum;++i) { if (a[i]!=b[i]) return 0; if (!a[i]) return 1; }
    return 0;
}
static int dl_copy_string(char* dst,u32 capacity,const char* src,u32 maximum) {
    for (u32 i=0;i<maximum && i<capacity;++i) { dst[i]=src[i]; if (!src[i]) return 1; }
    dst[0]=0; return 0;
}
/* Material identifiers may be padded with spaces rather than a terminator. */
static int dl_material_string(char out[29],const char* source) {
    u32 n=0; while(n<28 && source[n]) { out[n]=source[n]; ++n; }
    while(n && out[n-1]==' ') --n;
    out[n]=0; return n!=0;
}
static u32 dl_append(char* dst,u32 at,u32 capacity,const char* src) {
    while (*src && at+1<capacity) dst[at++]=*src++;
    dst[at]=0; return at;
}
static int dl_green(u32 c) {
    u32 r=c>>24,g=(c>>16)&255,b=(c>>8)&255;
    return g>r+16 && g>b+8;
}
static f32 dl_abs(f32 x) { return x<0 ? -x:x; }
static f32 dl_gauge_fraction(void) {
    f32 v=dl_config.free_transform ? 1 : dl_state.active ? dl_state.remaining/dl_state.total : dl_state.charge/dl_config.capacity;
    return v<0 ? 0 : v>1 ? 1 : v;
}
/* All archive offsets are range checked before dereference, including strings. */
static int dl_blob_span(u32 base,u32 size,u32 at,u32 bytes) {
    return at>=base && at-base<=size && bytes<=size-(at-base);
}
static u32 dl_magic_file(u32 arc,const char* wanted,u32* bytes) {
    u32 length=0;while(wanted[length] && length<95) ++length;
    if(length==95) return 0;
    if (!dl_game_span(arc,0x20) || *(u32*)arc!=0x53415243) return 0;
    u32 size=dl_word((void*)arc,8), data=dl_word((void*)arc,0xc);
    if (size>16*1024*1024 || !dl_game_span(arc,size) || data>=size) return 0;
    u32 fat=arc+*(u16*)(arc+4);
    if (!dl_blob_span(arc,size,fat,12) || *(u32*)fat!=0x53464154) return 0;
    u32 n=*(u16*)(fat+6), nodes=fat+*(u16*)(fat+4);
    if (!n || n>4096 || !dl_blob_span(arc,size,nodes,n*16+8)) return 0;
    u32 names=nodes+n*16;
    if (*(u32*)names!=0x53464e54) return 0;
    names+=*(u16*)(names+4);
    for(u32 i=0;i<n;++i) {
        u32 node=nodes+16*i, attr=dl_word((void*)node,4);
        if (!(attr>>24)) continue;
        u32 name=names+(attr&0xffffff)*4;
        if (!dl_blob_span(arc,size,name,length+1) || !dl_str_equal((char*)name,wanted,length+1)) continue;
        u32 start=dl_word((void*)node,8), end=dl_word((void*)node,12);
        if (end<=start || end>size-data) return 0;
        *bytes=end-start; return arc+data+start;
    }
    return 0;
}
static int dl_magic_parse(u32 f,u32 size) {
    if (size<20 || *(u32*)f!=0x464c5954 || *(u16*)(f+4)!=0xfeff) return 0;
    u32 at=f+*(u16*)(f+6), mat=0, mat_bytes=0, count=*(u16*)(f+16);
    if (count>1024) return 0;
    dl_magic_picture_count=0;
    /* Material table can occur before or after pictures. */
    for(u32 i=0;i<count;++i) {
        if (!dl_blob_span(f,size,at,8)) return 0;
        u32 n=dl_word((void*)at,4);
        if (n<8 || (n&3) || !dl_blob_span(f,size,at,n)) return 0;
        if (*(u32*)at==0x6d617431) { mat=at; mat_bytes=n; }
        at+=n;
    }
    if (!mat || mat_bytes<12) return 0;
    u32 mats=*(u16*)(mat+8);
    if (!mats || mats>256 || mat_bytes<12+4*mats) return 0;
    at=f+*(u16*)(f+6);
    for(u32 i=0;i<count;++i) {
        u32 n=dl_word((void*)at,4);
        if (*(u32*)at==0x70696331 && n>=0x80 && dl_magic_picture_count<DL_MAGIC_PICTURES) {
            u32 index=*(u16*)(at+0x5c+8); /* pan1 grows eight bytes in HD */
            if(index>=mats) return 0;
            u32 m=mat+dl_word((void*)mat,12+index*4);
            if(!dl_blob_span(mat,mat_bytes,m,40)) return 0;
            u32 p=dl_magic_picture_count;
            if(!dl_copy_string(dl_magic_pictures[p].name,24,(char*)(at+12),24) ||
               !dl_material_string(dl_magic_pictures[p].material,(char*)m)) return 0;
            u32 green=dl_green(dl_word((void*)m,28)) || dl_green(dl_word((void*)m,32));
            for(u32 v=0;v<4;++v) green|=dl_green(dl_word((void*)at,0x54+v*4));
            dl_magic_pictures[p].fill=green;
            dl_magic_pictures[p].black=dl_word((void*)m,28);
            dl_magic_pictures[p].white=dl_word((void*)m,32);
            for(u32 k=0;k<4;++k) dl_magic_pictures[p].vertex[k]=dl_word((void*)at,0x54+k*4);
            for(u32 k=0;k<10;++k) dl_magic_pictures[p].pane[k]=dl_magic_pictures[p].original[k]=*(f32*)(at+0x2c+k*4);
            for(u32 k=0;k<5;++k) dl_magic_pictures[p].tex_srt[k]=k>=3?1:0;
            u32 flags=dl_word((void*)m,36), maps=flags&3, srts=(flags>>2)&3;
            if(srts) {
                u32 srt=m+40+maps*4;
                if(!dl_blob_span(mat,mat_bytes,srt,20)) return 0;
                for(u32 k=0;k<5;++k) dl_magic_pictures[p].tex_srt[k]=*(f32*)(srt+k*4);
            }
            /* First UV set: TL,TR,BL,BR. A single unrotated image is sufficient
             * for these authored gauge pictures; reject unsupported UV shapes. */
            f32* uv=(f32*)(at+0x68);
            if (n>=0x88 && uv[1]==uv[3] && uv[0]==uv[4] && uv[6]==uv[2] && uv[7]==uv[5]) {
                dl_magic_pictures[p].uv[0]=uv[0]; dl_magic_pictures[p].uv[1]=uv[1];
                dl_magic_pictures[p].uv[2]=uv[2]; dl_magic_pictures[p].uv[3]=uv[5];
                ++dl_magic_picture_count;
            }
        }
        at+=n;
    }
    return dl_magic_picture_count!=0;
}
#include "magic_animation.h"
static int dl_magic_pane(const char* name,wwhd_game_pane* out) {
    u32 n=dl_append(dl_magic_key,0,sizeof dl_magic_key,"layout-pane:MagicBar_00/");
    dl_append(dl_magic_key,n,sizeof dl_magic_key,name);
    if(!wwhd_game_pane_get(dl_magic_key,out) || out->version!=1 || !(out->flags&2) || !(out->w>0 && out->h>0)) return 0;
    for(u32 i=0;i<dl_magic_picture_count;++i) if(dl_str_equal(name,dl_magic_pictures[i].name,24)) {
        f32 *a=dl_magic_pictures[i].original,*b=dl_magic_pictures[i].pane;
        f32 ow=a[6]*a[8],oh=a[7]*a[9];
        if(ow>0 && oh>0) {
            out->x+=(b[0]-a[0])*out->w/ow;out->y+=(b[1]-a[1])*out->h/oh;
            out->w*=b[6]*b[8]/ow;out->h*=b[7]*b[9]/oh;
        }
        break;
    }
    return out->w>0 && out->h>0;
}
static u32 dl_magic_red(u32 colour) {
    return (colour&0x0000ffffu)|((colour&0x00ff0000u)<<8)|((colour&0xff000000u)>>8);
}
static u32 dl_magic_image(wwhd_game_pane* pane,u32 picture,u32 variant,u32 fill) {
    /* The raw BC4 texture can be a luminance mask with fully opaque alpha.
     * Apply the actual material, including alpha, before making the red copy. */
    u32 n=dl_append(dl_magic_path,0,sizeof dl_magic_path,"timg/");
    n=dl_append(dl_magic_path,n,sizeof dl_magic_path,pane->texture);
    dl_append(dl_magic_path,n,sizeof dl_magic_path,".bflim");
    u32 bytes=0,texture=dl_magic_file(dl_magic_archive,dl_magic_path,&bytes);
    int srgb=0;
    if(!texture || !dl_art_decode((const u8*)texture,bytes,&srgb)) return 0;
    u32 black=dl_magic_pictures[picture].black,white=dl_magic_pictures[picture].white;
    u32 vertex=dl_magic_pictures[picture].vertex[0];
    int coloured=dl_green(black)||dl_green(white);
    if(coloured) {
        /* The base itself may contain the green fill. Draw an empty copy
         * below the red copy, never leave a green base behind the timer. */
        if(fill) { black=dl_magic_red(black);white=dl_magic_red(white); }
        else { if(dl_green(black)) black&=255; if(dl_green(white)) white&=255; }
    }
    if(dl_green(vertex)) vertex=fill?dl_magic_red(vertex):(vertex&255);
    dl_art_material(black,white,srgb,0);
    /* Fill RGB is a requested replacement colour, not a multiplier for
     * the source green/black gradient. Keep the original coverage alpha. */
    if(fill) {
        for(u32 i=0;i<dl_art_width*dl_art_height;++i) {
            dl_art_pixels[i*4]=255;dl_art_pixels[i*4+1]=32;dl_art_pixels[i*4+2]=32;
        }
        vertex=0xffffff00u|(vertex&255);
    }
    if(!dl_art_bake_uv(dl_magic_pictures[picture].uv,dl_magic_pictures[picture].tex_srt)) return 0;
    dl_magic_tint[variant][fill]=vertex;
    u32 size=dl_art_encode();
    if(!size) return 0;
    static const char* const files[2][2]={{"magic-0-frame.png","magic-0-fill.png"},{"magic-1-frame.png","magic-1-fill.png"}};
    const char* filename=files[variant][fill];
    if(wwhd_file_write(filename,dl_art_png,size)!=(s32)size) return 0;
    return wwhd_hud_texture(WWHD_HUD_DATA,filename);
}
static void dl_magic_load(void) {
    u32 mgr=WWHD_GAME_DATA(0x101F7274,u32);
    if (!mgr) return;
    dl_magic_string[0]=(u32)dl_magic_name;
    dl_magic_string[1]=(u32)&WWHD_GAME_DATA(0x1010A3FC,u32);
    dl_magic_archive=dl_layout_archive(mgr,dl_magic_string);
    u32 size=0,f=dl_magic_file(dl_magic_archive,"blyt/MagicBar_00.bflyt",&size);
    if(!f || !dl_magic_parse(f,size)) return;
    for(u32 v=0;v<2;++v) {
        if(dl_magic_images[v][0] && dl_magic_images[v][1]) continue;
        if(!dl_magic_parse(f,size)) return;
        dl_magic_full_animation(v);
        for(u32 i=0;i<dl_magic_picture_count;++i) {
            u32 green=dl_green(dl_magic_pictures[i].black)||dl_green(dl_magic_pictures[i].white);
            for(u32 k=0;k<4;++k) green|=dl_green(dl_magic_pictures[i].vertex[k]);
            dl_magic_pictures[i].fill=green;
        }
        for(u32 k=0;k<2;++k) if(dl_magic_images[v][k]) {
            wwhd_hud_release(dl_magic_images[v][k]); dl_magic_images[v][k]=0;
        }
        const char* base=v ? "P_MagicBase_01":"P_MagicBase_00";
        if(!dl_magic_pane(base,&dl_magic_base[v])) continue;
        s32 frame=-1,fill=-1; f32 score=1e9f;
        for(u32 i=0;i<dl_magic_picture_count;++i) {
            if(dl_str_equal(dl_magic_pictures[i].name,base,24)) frame=(s32)i;
            if(!dl_magic_pictures[i].fill || !dl_magic_pane(dl_magic_pictures[i].name,&dl_magic_query)) continue;
            /* Choose the authored green picture nearest the selected capacity's
             * base size/centre. Its native texture supplies the rounded ends. */
            f32 d=dl_abs(dl_magic_query.w-dl_magic_base[v].w)+
                2*dl_abs(dl_magic_query.x-dl_magic_base[v].x)+2*dl_abs(dl_magic_query.y-dl_magic_base[v].y);
            if(d<score) { score=d; fill=(s32)i; dl_magic_fill[v]=dl_magic_query; }
        }
        /* Some layouts put the entire coloured gauge in MagicBase itself.
         * In that case its geometry is the full-capacity fill, not a smaller
         * green highlight found elsewhere in the layout. */
        if(frame>=0 && dl_magic_pictures[frame].fill) { fill=frame;dl_magic_fill[v]=dl_magic_base[v]; }
        if(frame<0 || fill<0) continue;
        dl_magic_images[v][0]=dl_magic_image(&dl_magic_base[v],(u32)frame,v,0);
        dl_magic_images[v][1]=dl_magic_image(&dl_magic_fill[v],(u32)fill,v,1);

    }
    dl_magic_loaded=dl_magic_images[0][0] && dl_magic_images[0][1] &&
        dl_magic_images[1][0] && dl_magic_images[1][1];
    /* Before magic is unlocked its hidden native panes may never calculate a
     * matrix. Use Main_00's authored placement, not a guessed screen position. */
    dl_copy_string(dl_magic_key,sizeof dl_magic_key,"layout-pane:Main_00/L_MagicBar_00",40);
    if(wwhd_game_pane_get(dl_magic_key,&dl_magic_query)) {
        dl_magic_authored[0]=dl_magic_query.x; dl_magic_authored[1]=dl_magic_query.y;
        dl_magic_authored_valid=1;
    }
}
WWHD_HOOK_RETURN(0x02675414, dl_magic_track, (void* magic)) {
    u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    u32 v=save && *((u8*)save+0x33)>16;
    if(!dl_game_span((u32)magic,0x8c)) return;
    u32 pane=dl_word(magic,0x5c+v*4);
    if(!dl_game_span(pane,0xa4)) return;
    f32 w=dl_abs(*(f32*)(pane+0x48) * *(f32*)(pane+0x3c));
    f32 h=dl_abs(*(f32*)(pane+0x5c) * *(f32*)(pane+0x40));
    f32 x=*(f32*)(pane+0x54),y=*(f32*)(pane+0x64);
    if(!(w>8 && w<600 && h>2 && h<150 && x>-2000 && x<2000 && y>-1000 && y<1000)) return;
    u32 origin=*((u8*)pane+0x47), bh=origin&3, bv=(origin>>2)&3;
    dl_magic_position[0]=640+x+(bh==1 ? 0 : bh==2 ? -w : -w/2);
    dl_magic_position[1]=360-y+(bv==1 ? h : bv==2 ? 0 : h/2)+8;
    dl_magic_position[2]=w; dl_magic_position[3]=h; dl_magic_position_valid=1;
}
static void dl_magic_hud(u32 list) {
    unsigned long long epoch=wwhd_hud_epoch();
    if(epoch!=dl_magic_epoch) {
        dl_magic_epoch=epoch; dl_magic_loaded=dl_magic_retry=0;
        dl_magic_position_valid=0; dl_magic_authored_valid=0; memset(dl_magic_images,0,sizeof dl_magic_images);
    }
    if(!dl_magic_loaded && !dl_magic_retry) { dl_magic_load(); dl_magic_retry=60; }
    if(dl_magic_retry) --dl_magic_retry;
    u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    u32 v=save && *((u8*)save+0x33)>16;
    if(!dl_magic_images[v][0] || !dl_magic_images[v][1]) return;
    u32 anchor=WWHD_HUD_CENTER;
    if(!dl_magic_position_valid) {
        if(!dl_magic_authored_valid) return;
        dl_magic_position[0]=640+dl_magic_authored[0]+dl_magic_base[v].x-dl_magic_base[v].w/2;
        dl_magic_position[1]=360-dl_magic_authored[1]-dl_magic_base[v].y+dl_magic_base[v].h/2+8;
        dl_magic_position[2]=dl_magic_base[v].w; dl_magic_position[3]=dl_magic_base[v].h;
        anchor=WWHD_HUD_TOP_LEFT;
    }
    f32 scale=dl_magic_position[2]/dl_magic_base[v].w;
    f32 scale_y=dl_magic_position[3]/dl_magic_base[v].h;
    for(u32 layer=0;layer<2;++layer) {
        wwhd_game_pane* p=layer ? &dl_magic_fill[v]:&dl_magic_base[v];
        f32 w=p->w*scale,h=p->h*scale_y;
        f32 x=dl_magic_position[0]+(p->x-dl_magic_base[v].x)*scale+(dl_magic_position[2]-w)/2;
        f32 y=dl_magic_position[1]-(p->y-dl_magic_base[v].y)*scale_y+(dl_magic_position[3]-h)/2;
        f32 fraction=layer ? dl_gauge_fraction():1;
        /* One rejected command invalidates the whole owner list, including
         * DARK. Unsupported/malformed native geometry must stay local here. */
        if(!(x>=-65536 && x<=65536 && y>=-65536 && y<=65536 &&
             w>0 && w<=65536 && h>0 && h<=65536 && fraction>0 && fraction<=1)) continue;
        dl_element=(wwhd_hud_element){.kind=WWHD_HUD_IMAGE,.anchor=anchor,
            .x=x,.y=y,.w=w*fraction,.h=h,.rgba=dl_magic_tint[v][layer],
            .image=dl_magic_images[v][layer],.thickness=1,
            .u0=0,.v0=0,.u1=fraction,.v1=1};
        if(fraction>0) wwhd_hud_emit(list,&dl_element);
    }
}
