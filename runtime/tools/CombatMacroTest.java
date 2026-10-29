// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;
public class CombatMacroTest {
    static void check(boolean v) { if (!v) throw new AssertionError(); }
    public static void main(String[] args) {
        CombatMacro m = new CombatMacro();
        for (int kind : new int[]{CombatMacro.JUMP,CombatMacro.VERTICAL,CombatMacro.DODGE}) {
            m.start(kind,1000,0,0);
            check(m.target(1000) && m.face(1059)==0);
            check(m.face(1060)==(kind==CombatMacro.VERTICAL?2:1));
            check(m.face(1199)!=0 && m.face(1200)==0 && m.target(1259));
            check(!m.active(1260) && !m.target(1260) && m.face(1260)==0);
        }
        m.start(CombatMacro.SPIN,0,0,0);
        check(m.face(0)==2 && m.face(1099)==2 && !m.target(1099));
        check(m.face(1100)==0 && !m.active(1150));
        m.start(CombatMacro.DODGE,0,-.8f,0); check(m.x()==-1 && m.y()==0);
        m.start(CombatMacro.DODGE,0,0,0); check(m.x()==0 && m.y()==1);
        m.cancel(); check(m.face(50)==0 && !m.target(50) && !m.dodge(50));
        m.start(CombatMacro.SPIN,0,0,0); m.start(CombatMacro.JUMP,30,0,0);
        check(m.face(30)==0 && m.face(90)==1); // replacement releases the previous B
        CameraTap tap = new CameraTap();
        tap.down(1,100,100,0); check(!tap.up(1,50,40,false));
        tap.down(2,102,101,150); check(tap.up(2,200,40,false));
        tap.down(1,100,100,300); tap.move(1,200,100,8); check(!tap.up(1,330,40,false));
        tap.down(1,100,100,400); check(!tap.up(1,410,40,false));
        tap.down(1,100,100,500); tap.reset(); check(!tap.up(1,510,40,false)); // pinch takeover
        tap.down(1,100,100,600); check(!tap.up(1,900,40,false)); // hold
        tap.down(1,100,100,1000); check(!tap.up(1,1010,40,false));
        tap.down(1,200,100,1100); check(!tap.up(1,1110,40,false)); // far apart
        tap.down(1,200,100,1200); check(!tap.up(1,1210,40,true)); // cancelled
        System.out.println("Combat macro timing, replacement and cancellation passed");
    }
}
