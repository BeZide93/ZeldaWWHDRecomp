/* Copy native wnd1 frame slices surrounding the selected MagicBase. Layout
 * format: tools/story_mods/lyt/sections/{wnd1,mat1,txl1}.py. No invented border.
 * Only resident game resources are read; generated PNGs stay in mod Data. */
#pragma once
static u32 dl_magic_border[2];
static wwhd_game_pane dl_magic_border_pane[2];
static u8 dl_frame_pixels[DL_ART_PIXELS*4];
static u32 dl_frame_w,dl_frame_h;
static int dl_frame_texture(u32 mat,u32 mb,u32 txl,u32 tb,u32 index) {
    if(mb<12 || index>=*(u16*)(mat+8) || !dl_blob_span(mat,mb,mat+12+index*4,4)) return 0;
    u32 m=mat+dl_word((void*)mat,12+index*4);
    if(!dl_blob_span(mat,mb,m,44) || (dl_word((void*)m,36)&3)!=1 || tb<12) return 0;
    u32 ti=*(u16*)(m+40),nt=*(u16*)(txl+8);
    if(ti>=nt || !dl_blob_span(txl,tb,txl+12+ti*4,4)) return 0;
    u32 name=txl+12+dl_word((void*)txl,12+ti*4),n=0;
    if(!dl_blob_span(txl,tb,name,1)) return 0;
    while(n<64 && dl_blob_span(txl,tb,name+n,1) && *((u8*)name+n)) ++n;
    if(n==64 || !dl_blob_span(txl,tb,name+n,1)) return 0;
    dl_copy_string(dl_magic_path,sizeof dl_magic_path,"timg/",6);
    dl_append(dl_magic_path,5,sizeof dl_magic_path,(char*)name);
    u32 bytes=0,t=dl_magic_file(dl_magic_archive,dl_magic_path,&bytes);int srgb;
    if(!t || !dl_art_decode((u8*)t,bytes,&srgb)) return 0;
    dl_magic_picture style={0};
    if(!dl_material_string(style.material,(char*)m)) return 0;
    style.black=dl_word((void*)m,28);style.white=dl_word((void*)m,32);
    style.tex_srt[3]=style.tex_srt[4]=1;
    f32 uv[4]={0,0,1,1};
    if((dl_word((void*)m,36)>>2)&3) {
        if(!dl_blob_span(mat,mb,m+44,20)) return 0;
        for(u32 i=0;i<5;++i) style.tex_srt[i]=*(f32*)(m+44+4*i);
    }
    dl_magic_material_target=&style;dl_magic_apply_animations();dl_magic_material_target=0;
    dl_art_material(style.black,style.white,srgb);
    return dl_art_bake_sampler(uv,style.tex_srt,*((u8*)m+42)&3,*((u8*)m+43)&3,dl_art_width,dl_art_height);
}
/* Stretch the native edge pixels, retaining the authored corner dimensions.
 * One-/four-frame windows derive edges from corners; eight-frame windows
 * provide explicit edge materials. Flips are baked, never invalid HUD UVs. */
static void dl_frame_blit(u32 x0,u32 y0,u32 x1,u32 y1,u32 flip,u32 mx,u32 my,u32 edge) {
    if(x1<=x0 || y1<=y0) return;
    for(u32 y=y0;y<y1;++y) for(u32 x=x0;x<x1;++x) {
        f32 u=((f32)(x-x0)+0.5f)/(x1-x0),v=((f32)(y-y0)+0.5f)/(y1-y0);
        if(edge==1) v=1-0.5f/dl_art_height;
        if(edge==2) u=1-0.5f/dl_art_width;
        if(mx) u=1-u;if(my) v=1-v;
        f32 a=u,b=v;
        if(flip==1) u=1-a;else if(flip==2) v=1-b;
        else if(flip==3) { u=b;v=1-a; }
        else if(flip==4) { u=1-a;v=1-b; }
        else if(flip==5) { u=1-b;v=a; }
        f32 sx=u*dl_art_width-0.5f,sy=v*dl_art_height-0.5f;
        sx=sx<0?0:sx>dl_art_width-1?dl_art_width-1:sx;
        sy=sy<0?0:sy>dl_art_height-1?dl_art_height-1:sy;
        u32 ix=(u32)sx,iy=(u32)sy,jx=ix+1<dl_art_width?ix+1:ix,jy=iy+1<dl_art_height?iy+1:iy;
        f32 fx=sx-ix,fy=sy-iy;
        for(u32 c=0;c<4;++c) {
            f32 tl=dl_art_pixels[(iy*dl_art_width+ix)*4+c],tr=dl_art_pixels[(iy*dl_art_width+jx)*4+c];
            f32 bl=dl_art_pixels[(jy*dl_art_width+ix)*4+c],br=dl_art_pixels[(jy*dl_art_width+jx)*4+c];
            f32 top=tl+(tr-tl)*fx,bottom=bl+(br-bl)*fx;
            dl_frame_pixels[(y*dl_frame_w+x)*4+c]=(u8)(top+(bottom-top)*fy+0.5f);
        }
    }
}
static u32 dl_frame_image(u32 wnd,u32 wb,u32 mat,u32 mb,u32 txl,u32 tb,wwhd_game_pane* p,u32 variant) {
    if(wb<0x70 || !(p->w>0 && p->w<1024 && p->h>0 && p->h<256)) return 0;
    u32 count=*((u8*)wnd+0x64),kind=(*((u8*)wnd+0x65)>>2)&3,table=wnd+dl_word((void*)wnd,0x6c);
    if((count!=1 && count!=4 && count!=8) || (kind!=0 && kind!=2) || !dl_blob_span(wnd,wb,table,count*4)) return 0;
    f32 w=*(f32*)(wnd+0x4c),h=*(f32*)(wnd+0x50);
    if(!(w>0 && w<1000000 && h>0 && h<1000000) ||
       (u32)*(u16*)(wnd+0x5c)+*(u16*)(wnd+0x5e)>w ||
       (kind==0 && (u32)*(u16*)(wnd+0x60)+*(u16*)(wnd+0x62)>h)) return 0;
    dl_frame_w=(u32)(p->w+0.5f);dl_frame_h=(u32)(p->h+0.5f);
    if(!dl_frame_w || !dl_frame_h || dl_frame_w*dl_frame_h>DL_ART_PIXELS) return 0;
    u32 l=(u32)(*(u16*)(wnd+0x5c)*dl_frame_w/w+0.5f),r=(u32)(*(u16*)(wnd+0x5e)*dl_frame_w/w+0.5f);
    u32 t=(u32)(*(u16*)(wnd+0x60)*dl_frame_h/h+0.5f),b=(u32)(*(u16*)(wnd+0x62)*dl_frame_h/h+0.5f);
    if(l+r>dl_frame_w || (kind==0 && t+b>dl_frame_h)) return 0;
    memset(dl_frame_pixels,0,dl_frame_w*dl_frame_h*4);
    const u32 W=dl_frame_w,H=dl_frame_h;
    u32 rects[8][4]={{0,0,l,t},{W-r,0,W,t},{0,H-b,l,H},{W-r,H-b,W,H},
                    {0,t,l,H-b},{W-r,t,W,H-b},{l,0,W-r,t},{l,H-b,W-r,H}};
    if(kind==2) { rects[0][3]=H;rects[1][3]=H; }
    for(u32 i=0;i<(kind==2?2u:8u);++i) {
        u32 material=count==1?0:count==8?i:i<4?i:i==5?1:i==7?2:0;
        u32 entry=wnd+dl_word((void*)table,material*4);
        if(!dl_blob_span(wnd,wb,entry,4)) return 0;
        u32 flip=*((u8*)entry+2);
        if(flip>5 || !dl_frame_texture(mat,mb,txl,tb,*(u16*)entry)) return 0;
        u32 mx=count==1 && (i==1 || i==3 || i==5),my=count==1 && (i==2 || i==3 || i==7);
        u32 edge=(i>=4 && count!=8)?(i<6?1:2):0;
        dl_frame_blit(rects[i][0],rects[i][1],rects[i][2],rects[i][3],flip,mx,my,edge);
    }
    dl_art_width=W;dl_art_height=H;memcpy(dl_art_pixels,dl_frame_pixels,W*H*4);
    u32 alpha=dl_magic_style_alpha((char*)(wnd+12));
    for(u32 i=0;i<W*H;++i) dl_art_pixels[i*4+3]=dl_art_pixels[i*4+3]*alpha/255;
    u32 bytes=dl_art_encode();if(!bytes) return 0;
    const char* path=variant?"magic-1-border.png":"magic-0-border.png";
    if(wwhd_file_write(path,dl_art_png,bytes)!=(s32)bytes) return 0;
    return wwhd_hud_texture(WWHD_HUD_DATA,path);
}
static void dl_magic_load_border(u32 f,u32 size,u32 v) {
    if(dl_magic_border[v]) { wwhd_hud_release(dl_magic_border[v]);dl_magic_border[v]=0; }
    u32 at=f+*(u16*)(f+6),count=*(u16*)(f+16),mat=0,mb=0,txl=0,tb=0,best=0,best_bytes=0;
    f32 score=1e9f;wwhd_game_pane chosen;
    for(u32 i=0;i<count;++i) {
        if(!dl_blob_span(f,size,at,8)) return;
        u32 n=dl_word((void*)at,4);if(n<8 || !dl_blob_span(f,size,at,n)) return;
        u32 type=dl_word((void*)at,0);
        if(type==0x6d617431) { mat=at;mb=n; }
        if(type==0x74786c31) { txl=at;tb=n; }
        if(type==0x776e6431 && n>=0x70) {
            char name[24];
            if(!dl_copy_string(name,sizeof name,(char*)(at+12),24)) return;
            if(!dl_magic_style_alpha(name)) { at+=n;continue; }
            u32 k=dl_append(dl_magic_key,0,sizeof dl_magic_key,"layout-pane:MagicBar_00/");
            dl_append(dl_magic_key,k,sizeof dl_magic_key,name);
            if(wwhd_game_pane_get(dl_magic_key,&dl_magic_query) && dl_magic_query.version==1) {
                wwhd_game_pane* p=&dl_magic_query;wwhd_game_pane* base=&dl_magic_base[v];
                f32 dw=p->w-base->w,dh=p->h-base->h,dx=dl_abs(p->x-base->x),dy=dl_abs(p->y-base->y);
                f32 raw_w=*(f32*)(at+0x4c),raw_h=*(f32*)(at+0x50);
                if(!(raw_w>0 && raw_h>0)) { at+=n;continue; }
                f32 l=*(u16*)(at+0x5c)*p->w/raw_w,r=*(u16*)(at+0x5e)*p->w/raw_w;
                u32 kind=(*((u8*)at+0x65)>>2)&3;
                f32 t=kind==2?0:*(u16*)(at+0x60)*p->h/raw_h,b=kind==2?0:*(u16*)(at+0x62)*p->h/raw_h;
                /* Its transparent inner opening must contain the gauge, not
                 * just its outer box. A white effect with thick slices used
                 * to qualify even when it covered nearly the entire fill. */
                int encloses=p->x-p->w/2+l<=base->x-base->w/2+0.5f &&
                    p->x+p->w/2-r>=base->x+base->w/2-0.5f &&
                    p->y+p->h/2-t>=base->y+base->h/2-0.5f &&
                    p->y-p->h/2+b<=base->y-base->h/2+0.5f;
                if(encloses && p->rotation==0 && dw>=0 && dw<=128 && dh>=0 && dh<=64 && dx<=dw/2+1 && dy<=dh/2+1) {
                    f32 d=dw+dh+2*dx+2*dy;
                    if(d<score) { score=d;best=at;best_bytes=n;chosen=*p; }
                }
            }
        }
        at+=n;
    }
    if(best && mat && txl) {
        dl_magic_border[v]=dl_frame_image(best,best_bytes,mat,mb,txl,tb,&chosen,v);
        dl_magic_border_pane[v]=chosen;
    }
}
