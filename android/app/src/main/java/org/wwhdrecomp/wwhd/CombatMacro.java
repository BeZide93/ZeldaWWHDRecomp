// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

// Timed virtual-pad overlay, independent of physical fingers. Based on the sequences in
// SSunnKing's Enhanced fork (MPL-2.0). Never owns or releases the user's own buttons.
final class CombatMacro {
    static final int JUMP = 0, VERTICAL = 1, SPIN = 2, DODGE = 3;
    private int kind = -1;
    private long start;
    private float dodgeX, dodgeY;
    void start(int which, long now, float x, float y) {
        kind = which; start = now;
        dodgeX = Math.abs(x) > .35f ? Math.signum(x) : 0;
        dodgeY = dodgeX == 0 ? 1 : 0; // SDL +y is backwards
    }
    void cancel() { kind = -1; }
    boolean active(long now) {
        if (kind >= 0 && now - start >= (kind == SPIN ? 1150 : 260)) cancel();
        return kind >= 0;
    }
    int face(long now) {
        if (!active(now)) return 0;
        long t = now - start;
        if (kind == SPIN) return t < 1100 ? 2 : 0; // B, then release
        return t >= 60 && t < 200 ? (kind == VERTICAL ? 2 : 1) : 0;
    }
    boolean target(long now) { return active(now) && kind != SPIN; }
    boolean dodge(long now) { return active(now) && kind == DODGE; }
    float x() { return dodgeX; }
    float y() { return dodgeY; }
}
