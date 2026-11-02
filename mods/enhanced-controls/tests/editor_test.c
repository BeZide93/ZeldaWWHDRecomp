/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <stdio.h>
#include "../editor.h"
int main(void) {
    EcEditor e={0}; e.saved=ee_defaults(); assert(ee_valid(&e.saved)); ee_begin(&e);
    assert(ee_touch(&e,2,300,185)==-1); /* opener release cannot save */
    ee_touch(&e,0,-150,-180); ee_touch(&e,1,-400,-240); ee_touch(&e,2,-400,-240);
    assert(e.draft.button[0].x==-400 && e.draft.button[0].y==-240 && e.saved.button[0].x==-150);
    ee_apply(&e,EE_GRID); ee_touch(&e,0,-400,-240); ee_touch(&e,2,113,-137);
    assert(e.draft.button[0].x==120 && e.draft.button[0].y==-140);
    ee_touch(&e,0,120,-140); ee_touch(&e,1,9999,9999); ee_touch(&e,2,9999,9999);
    assert(ee_valid(&e.draft));
    for(int i=0;i<100;++i) ee_apply(&e,EE_LARGER);
    assert(e.draft.button[0].size==180 && ee_valid(&e.draft));
    for(int i=0;i<100;++i) ee_apply(&e,EE_SMALLER);
    assert(e.draft.button[0].size==60 && ee_valid(&e.draft));
    ee_apply(&e,EE_OPACITY); assert(e.draft.button[0].opacity==80);
    for(int i=0;i<4;++i) ee_apply(&e,EE_OPACITY); assert(e.draft.button[0].opacity==100);
    ee_apply(&e,EE_SHAPE); ee_apply(&e,EE_FILL); ee_apply(&e,EE_ICONS); ee_apply(&e,EE_VISIBLE);
    assert(e.draft.button[0].shape==1 && e.draft.button[0].filled && !e.draft.button[0].icons && !e.draft.button[0].visible);
    ee_touch(&e,0,300,305); assert(e.selected==3); /* tabs recover any overlapping/hidden button */
    ee_touch(&e,0,300,185); assert(ee_touch(&e,2,300,185)==EE_SAVE);
    ee_touch(&e,0,300,185); ee_touch(&e,1,350,185); assert(ee_touch(&e,2,300,185)==-1);
    ee_touch(&e,0,300,185); ee_touch(&e,3,300,185); assert(ee_touch(&e,2,300,185)==-1);
    ee_touch(&e,0,300,185); ee_touch(&e,4,2,0); assert(ee_touch(&e,2,300,185)==-1);
    ee_close(&e); ee_begin(&e); assert(e.draft.button[0].x==-150 && e.draft.button[0].visible);
    EcLayoutFile f={0x45434C59,1,5,0,e.saved}; f.checksum=ee_hash(&f); assert(ee_file_valid(&f));
    ++f.layout.button[0].x; assert(!ee_file_valid(&f)); f.checksum=ee_hash(&f); assert(ee_file_valid(&f));
    f.layout.button[0].size=999; f.checksum=ee_hash(&f); assert(!ee_file_valid(&f));
    f.layout=ee_defaults(); f.version=2; f.checksum=ee_hash(&f); assert(!ee_file_valid(&f));
    ee_apply(&e,EE_RESET); assert(ee_valid(&e.draft) && !e.draft.grid);
    puts("Editor: drag, bounds, snapping, actions, cancel and file validation passed");
}
