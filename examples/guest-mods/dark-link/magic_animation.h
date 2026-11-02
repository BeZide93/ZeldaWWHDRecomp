/* Read the full-capacity state of the game's gauge animation. BFLAN format:
 * tools/story_mods/lyt/anim/{pat1,pai1,tracks}.py. Never advance the live HUD or
 * write magic/save values. Only G_Bar_00 animations with 16/32 frames qualify. */
#pragma once
static u32 dl_magic_animation_ok[2];
static int dl_magic_number(f32 v) { return v>=-1000000 && v<=1000000; }
static int dl_magic_key_value(u32 f,u32 bytes,u32 e,f32 frame,f32* result) {
    if((e&3)||!dl_blob_span(f,bytes,e,12)) return 0;
    u32 curve=*((u8*)e+2),count=*(u16*)(e+4),offset=dl_word((void*)e,8);
    if(offset<12||offset>bytes) return 0;
    u32 keys=e+offset;
    u32 stride=curve==1?8:curve==2?12:0;
    if(!stride||!count||count>256||(keys&3)||!dl_blob_span(f,bytes,keys,count*stride)) return 0;
    f32 previous=-1000001;
    for(u32 k=0;k<count;++k) {
        u32 key=keys+k*stride;f32 t=*(f32*)key;
        if(!dl_magic_number(t)||t<previous) return 0;
        previous=t;
        if(curve==2 && (!dl_magic_number(*(f32*)(key+4))||!dl_magic_number(*(f32*)(key+8)))) return 0;
    }
    u32 left=0;
    while(left+1<count && *(f32*)(keys+(left+1)*stride)<=frame) ++left;
    u32 a=keys+left*stride;
    *result=curve==1?(f32)*(u16*)(a+4):*(f32*)(a+4);
    if(curve==1 || left+1==count || frame<=*(f32*)a) return 1;
    u32 b=a+stride;f32 dt=*(f32*)b-*(f32*)a;
    if(!(dt>0)) return 1;
    f32 t=(frame-*(f32*)a)/dt,t2=t*t,t3=t2*t;
    *result=(2*t3-3*t2+1)**(f32*)(a+4)+(t3-2*t2+t)*dt**(f32*)(a+8)
        +(-2*t3+3*t2)**(f32*)(b+4)+(t3-t2)*dt**(f32*)(b+8);
    return dl_magic_number(*result);
}
static u32 dl_magic_colour_byte(u32 c,u32 channel,f32 value) {
    u32 v=value<0?0:value>255?255:(u32)(value+0.5f),shift=24-channel*8;
    return (c&~(255u<<shift))|(v<<shift);
}
static int dl_magic_tracks(u32 f,u32 bytes,u32 pai,f32 frame,int apply) {
    if(!dl_blob_span(f,bytes,pai,20)) return 0;
    u32 count=*(u16*)(pai+14),table=pai+dl_word((void*)pai,16);
    if(count>256 || !dl_blob_span(f,bytes,table,count*4)) return 0;
    for(u32 i=0;i<count;++i) {
        u32 c=pai+dl_word((void*)table,i*4);
        if(!dl_blob_span(f,bytes,c,32)) return 0;
        u32 tags=*((u8*)c+28),kind=*((u8*)c+29);
        if(tags>32 || !dl_blob_span(f,bytes,c+32,tags*4)) return 0;
        for(u32 j=0;j<tags;++j) {
            u32 tag=c+dl_word((void*)c,32+j*4);
            if(!dl_blob_span(f,bytes,tag,8)) return 0;
            u32 type=*(u32*)tag,entries=*((u8*)tag+4);
            if(!dl_blob_span(f,bytes,tag+8,entries*4)) return 0;
            for(u32 k=0;k<entries;++k) {
                u32 e=tag+dl_word((void*)tag,8+k*4);f32 value;
                if(!dl_magic_key_value(f,bytes,e,frame,&value)) return 0;
                if(!apply) continue;
                u32 index=*(u8*)e,target=*((u8*)e+1);
                for(u32 p=0;p<dl_magic_picture_count;++p) {
                    const char* name=kind==1?dl_magic_pictures[p].material:dl_magic_pictures[p].name;
                    if(!dl_str_equal(name,(const char*)c,28)) continue;
                    if(kind==1 && type==0x464c4d43 && target<8) { /* FLMC */
                        u32* colour=target<4?&dl_magic_pictures[p].black:&dl_magic_pictures[p].white;
                        *colour=dl_magic_colour_byte(*colour,target&3,value);
                    } else if(kind==1 && type==0x464c5453 && index==0 && target<5) /* FLTS */
                        dl_magic_pictures[p].tex_srt[target]=value;
                    else if(kind==0 && type==0x464c5041 && target<10) /* FLPA */
                        dl_magic_pictures[p].pane[target]=value;
                    else if(kind==0 && type==0x464c5643 && target<16) { /* FLVC */
                        u32* colour=&dl_magic_pictures[p].vertex[target/4];
                        *colour=dl_magic_colour_byte(*colour,target&3,value);
                    }
                }
            }
        }
    }
    return 1;
}
static int dl_magic_animation_file(u32 f,u32 bytes,u32 variant) {
    if(bytes<20 || *(u32*)f!=0x464c414e || *(u16*)(f+4)!=0xfeff) return 0;
    u32 at=f+*(u16*)(f+6),sections=*(u16*)(f+16),pat=0,pai=0;
    if(sections>64) return 0;
    for(u32 i=0;i<sections;++i) {
        if(!dl_blob_span(f,bytes,at,8)) return 0;
        u32 n=dl_word((void*)at,4);
        if(n<8||(n&3)||!dl_blob_span(f,bytes,at,n)) return 0;
        if(*(u32*)at==0x70617431) pat=at;
        if(*(u32*)at==0x70616931) pai=at;
        at+=n;
    }
    if(!pat||!pai||!dl_blob_span(f,bytes,pat,28)||!dl_blob_span(f,bytes,pai,20)) return 0;
    u32 frames=variant?32:16,groups=*(u16*)(pat+10),table=pat+dl_word((void*)pat,16);
    if(*(u16*)(pai+8)!=frames || groups>32 || !dl_blob_span(f,bytes,table,groups*28)) return 0;
    int bar=0;
    for(u32 i=0;i<groups;++i) if(dl_str_equal((const char*)(table+i*28),"G_Bar_00",24)) bar=1;
    if(!bar || !dl_magic_tracks(f,bytes,pai,(f32)frames,0)) return 0;
    return dl_magic_tracks(f,bytes,pai,(f32)frames,1);
}
static void dl_magic_full_animation(u32 variant) {
    dl_magic_animation_ok[variant]=0;
    u32 arc=dl_magic_archive;
    /* The same archive was validated by dl_magic_file immediately above. */
    u32 size=dl_word((void*)arc,8),data=dl_word((void*)arc,12),fat=arc+*(u16*)(arc+4);
    u32 n=*(u16*)(fat+6),nodes=fat+*(u16*)(fat+4);
    for(u32 i=0;i<n;++i) {
        u32 start=dl_word((void*)nodes,i*16+8),end=dl_word((void*)nodes,i*16+12);
        if(end<=start||end>size-data||end-start<20) continue;
        u32 f=arc+data+start;
        if((f&3)||*(u32*)f!=0x464c414e) continue;
        if(dl_magic_animation_file(f,end-start,variant)) { dl_magic_animation_ok[variant]=1;return; }
    }
}
