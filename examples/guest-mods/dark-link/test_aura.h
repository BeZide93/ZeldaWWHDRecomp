/* Native aura regressions retained unchanged from 0.1.24. */
static void test_aura(void) {
    st32(LINK+0x5700,AURA); st16(AURA+0x2a,1); st32(AURA+0x34,AURA_MAT);
    st32(AURA_MAT,AURA_RES); st32(AURA_RES,0x464d4154);
    st32(AURA_RES+0x20,AURA_RS-(AURA_RES+0x20));
    st32(AURA_RS,7); st32(AURA_RS+0x14,0xcc0100); st32(AURA_RS+0x1c,0x20010101);
    for(int i=0;i<2;++i) {
        uint32_t b=LINK+(i ? 0x5704:0x5614),a=AURA_ANM+i*0x100,v=AURA_VALUE+i*0x100;
        st32(b+0x10,a); st32(b+0x20,v); st16(b+0xa,0); /* native init(play=FALSE): no playback limit */
        st32(a+4,AURA_ANM+0x300);st32(AURA_ANM+0x314,0x02000008);
        stf32(a,0); stf32(b+4,0); stf32(v,0); st32(v+0x10,0x02000004);
    }
    uint8_t original_save[0x40]; memcpy(original_save,ppc_ptr(SAVE),sizeof original_save);
    uint32_t flags=ld32(LINK+0x3bc); unsigned timer=ld16(LINK+0x699e);
    hook(0x0240b7f4,2,LINK,0);
    assert((float)ldf32(AURA_ANM)>30 && (ld32(AURA_RS+0x1c)&0x1fff)==0x500);
    assert(ld32(LINK+0x3bc)==flags && ld16(LINK+0x699e)==timer);
    hook(0x0240b7f4,3,LINK,0); assert((float)ldf32(AURA_ANM)==0);
    hook(0x023dbbe4,2,LINK,0); assert((float)ldf32(AURA_ANM)>29);
    hook(0x023dbbe4,3,LINK,0); assert((float)ldf32(AURA_ANM)==0 && (float)ldf32(AURA_VALUE)==0);
    assert(aura_notifications>=6 && !memcmp(original_save,ppc_ptr(SAVE),sizeof original_save));
    st32(AURA_RS+0x1c,0x504); /* native non-separate alpha uses RGB equation */
    hook(0x0240b7f4,2,LINK,0);
    assert((ld32(AURA_RS+0x1c)&0x1fff)==0x500);
    assert((ld32(AURA_RS+0x1c)&(1u<<29)) && ((ld32(AURA_RS+0x1c)>>16)&0x1fff)==0x504);
    hook(0x0240b7f4,3,LINK,0);
    /* Return native separate-alpha fixture before restoration assertions. */
    st32(AURA_RS+0x1c,0x20010101);
    st32(LINK+0x3bc,flags|1); stf32(AURA_ANM,12);
    hook(0x0240b7f4,2,LINK,0); assert((float)ldf32(AURA_ANM)==12);
    hook(0x0240b7f4,3,LINK,0); assert((float)ldf32(AURA_ANM)==12);
    st32(LINK+0x3bc,flags);
    st32(AURA_RS,ld32(AURA_RS)|0x400); /* another mod's unrelated flag */
    st32(AURA_RS+0x1c,ld32(AURA_RS+0x1c)|0x80000000u);
    enabled=0; ++revision; tick();
    assert(ld32(AURA_RS)==0x407 && ld32(AURA_RS+0x14)==0xcc0100 && ld32(AURA_RS+0x1c)==0xa0010101);
    hook(0x023dbbe4,2,LINK,0); assert((float)ldf32(AURA_ANM)==12);
    hook(0x023dbbe4,3,LINK,0);
    st32(LINK+0x5700,0); enabled=1; ++revision; tick();
    buttons(0); for(int i=0;i<20;++i) hit(1); buttons(0x60000); tick(); assert(damage(3)==6);
}
