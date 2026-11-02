/* Pixel/PNG regressions use synthetic data, never Nintendo textures. */
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t u8;typedef uint32_t u32;typedef float f32;
#include "magic_art.h"
static u8 texture[1100];
static void fixture(u32 code,u32 n) {
    memset(texture,0,sizeof texture);
    u8* f=texture+n;dl_art_put(f,0x464c494d);dl_art_put(f+20,0x696d6167);
    f[29]=4;f[31]=4;f[34]=code;f[35]=1;dl_art_put(f+36,n);
}
int main(int argc,char** argv) {
    assert(argc==2);int srgb=0;
    fixture(0x10,512);texture[0]=255;texture[1]=0;
    /* BC4-alpha: first alpha 255, next alpha 0, then six interpolated values. */
    const u8 codes[6]={0x88,0xc6,0xfa,0x88,0xc6,0xfa};memcpy(texture+2,codes,6);
    assert(dl_art_decode(texture,552,&srgb));
    const u8 expected[8]={255,0,218,182,145,109,72,36};
    for(u32 i=0;i<16;++i) { assert(dl_art_pixels[i*4]==255);assert(dl_art_pixels[i*4+3]==expected[i%8]); }
    dl_art_material(0x00000000,0xff2020ff,srgb,0);
    for(u32 i=0;i<16;++i) { assert(dl_art_pixels[i*4]==255);assert(dl_art_pixels[i*4+1]<100);assert(dl_art_pixels[i*4+3]==expected[i%8]); }
    u32 n=dl_art_encode();assert(n>100);
    FILE* f=fopen(argv[1],"wb");assert(f);assert(fwrite(dl_art_png,1,n,f)==n);fclose(f);
    assert(!dl_art_decode(texture,39,&srgb));
    texture[512+35]=3;assert(!dl_art_decode(texture,552,&srgb));
    fixture(0xc,512);texture[0]=0;texture[1]=0xf8; /* BC1 red, code 0 */
    assert(dl_art_decode(texture,552,&srgb));assert(dl_art_pixels[0]==255&&dl_art_pixels[1]==0&&dl_art_pixels[3]==255);
    fixture(0x11,1024);texture[0]=180;texture[8]=90; /* BC5 luminance+alpha */
    assert(dl_art_decode(texture,1064,&srgb));assert(dl_art_pixels[0]==180&&dl_art_pixels[1]==180&&dl_art_pixels[3]==90);
    fixture(9,1024);texture[0]=10;texture[1]=20;texture[2]=30;texture[3]=40;
    assert(dl_art_decode(texture,1064,&srgb));assert(dl_art_pixels[0]==10&&dl_art_pixels[3]==40);
    /* Known GX2 microtile addresses; test each ordering and macro bank bits. */
    assert(dl_art_address(1,0,8,2,0,64)==8);
    assert(dl_art_address(0,1,8,2,0,64)==16);
    assert(dl_art_address(1,0,8,2,0,128)==32);
    assert(dl_art_address(0,1,8,2,0,128)==16);
    assert(dl_art_address(8,0,32,4,0,32)==768);
    assert(dl_art_address(0,0,32,4,1,32)==256);
    puts("Dark Link native artwork pixel tests passed");
}
