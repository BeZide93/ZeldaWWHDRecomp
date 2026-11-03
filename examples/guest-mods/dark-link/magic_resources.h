/* Read only native animation TAGS/duration, never artwork or animation curves.
 * The game's animator evaluates the resource. No generated/cached textures. */
#pragma once
typedef struct { char progress[2][32];f32 in_end;u32 found; } DLNativeTags;
static void dl_native_animation(u32 f,u32 size,DLNativeTags* out) {
    if(size<20 || *(u32*)f!=0x464c414e || *(u16*)(f+4)!=0xfeff)return;
    u32 offset=*(u16*)(f+6),sections=*(u16*)(f+16),pat=0,pai=0,pat_size=0;
    if(offset<20 || offset>size || sections>64)return;
    u32 at=f+offset;
    for(u32 i=0;i<sections;++i) {
        if((at&3) || !dl_blob_span(f,size,at,8))return;
        u32 n=dl_word((void*)at,4);
        if(n<8 || (n&3) || !dl_blob_span(f,size,at,n))return;
        if(*(u32*)at==0x70617431){pat=at;pat_size=n;}
        if(*(u32*)at==0x70616931 && n>=20)pai=at;
        at+=n;
    }
    if(!pat || !pai || pat_size<28)return;
    u32 groups=*(u16*)(pat+10),name_off=dl_word((void*)pat,12),group_off=dl_word((void*)pat,16);
    if(groups>32 || name_off>=pat_size || group_off>=pat_size)return;
    u32 name=pat+name_off,group=pat+group_off,duration=*(u16*)(pai+8);
    if(!dl_blob_span(pat,pat_size,group,groups*28))return;
    char tag[32];u32 length=0;
    while(length<31 && dl_blob_span(pat,pat_size,name+length,1) && *((u8*)name+length)) {
        tag[length]=*((u8*)name+length);++length;
    }
    if(!length || length==31 || !dl_blob_span(pat,pat_size,name+length,1))return;
    tag[length]=0;
    for(u32 i=0;i<groups;++i) {
        const char* binding=(const char*)(group+i*28);
        if(dl_str_equal(binding,"G_All_00",28) && dl_str_equal(tag,"InAll",32) && duration && duration<=1000) {
            out->in_end=(f32)duration;out->found|=4;
        }
        if(dl_str_equal(binding,"G_Bar_00",28) && (duration==16 || duration==32)) {
            u32 v=duration==32;
            if(out->found&(1u<<v)) {
                /* Ambiguous resources must not silently select a different curve. */
                if(!dl_str_equal(out->progress[v],tag,32))out->found|=8;
            } else memcpy(out->progress[v],tag,length+1);
            out->found|=1u<<v;
        }
    }
}
static int dl_native_tags(DLNativeTags* out) {
    memset(out,0,sizeof *out);
    u32 manager=WWHD_GAME_DATA(0x101F7274,u32);
    if(!manager)return 0;
    u32 name[2]={(u32)"MagicBar_00",(u32)&WWHD_GAME_DATA(0x1010A3FC,u8)};
    u32 arc=dl_layout_archive(manager,name);
    if(!dl_ui_span(arc,0x20) || *(u32*)arc!=0x53415243)return 0;
    u32 size=dl_word((void*)arc,8),data=dl_word((void*)arc,12),fat_off=*(u16*)(arc+4);
    if(size>16*1024*1024 || !dl_ui_span(arc,size) || data>=size || fat_off>=size)return 0;
    u32 fat=arc+fat_off;
    if((fat&3) || !dl_blob_span(arc,size,fat,12) || *(u32*)fat!=0x53464154)return 0;
    u32 count=*(u16*)(fat+6),node_off=*(u16*)(fat+4);
    if(!count || count>4096 || node_off<12 || node_off>size-fat_off)return 0;
    u32 nodes=fat+node_off;
    if((nodes&3) || !dl_blob_span(arc,size,nodes,count*16))return 0;
    for(u32 i=0;i<count;++i) {
        u32 start=dl_word((void*)nodes,i*16+8),end=dl_word((void*)nodes,i*16+12);
        if(end<=start || end>size-data || end-start<20 || ((arc+data+start)&3))continue;
        dl_native_animation(arc+data+start,end-start,out);
    }
    return out->found==7;
}
