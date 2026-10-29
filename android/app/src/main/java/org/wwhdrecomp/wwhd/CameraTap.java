// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

// Recognizes two short, nearby taps; swipes, holds and pinches cannot recenter the camera.
final class CameraTap {
    private int pointer = -1;
    private long downAt, previousUp = -1;
    private float x, y, previousX, previousY;
    void reset() { pointer = -1; previousUp = -1; }
    void down(int id, float px, float py, long now) { pointer=id; x=px; y=py; downAt=now; }
    void move(int id, float px, float py, float slop) {
        if (id==pointer && Math.hypot(px-x,py-y)>slop) reset();
    }
    boolean up(int id, long now, float slop, boolean cancelled) {
        if (cancelled) { reset(); return false; }
        if (id!=pointer) return false;
        pointer=-1;
        if (now-downAt>=250) { previousUp=-1; return false; }
        if (previousUp>=0 && now-previousUp<300 && Math.hypot(x-previousX,y-previousY)<slop) {
            previousUp=-1; return true;
        }
        previousUp=now; previousX=x; previousY=y; return false;
    }
}
