# Dark Link (prototype 0.1.4)

Standalone code mod for ZeldaWWHDRecomp: charge with sword hits, activate a damage
boost and a partial body tint. Version 0.1.4 restores the original C0/K0 color
approach after the diagnostic build, with corrected restoration of its override
bits. **Complete shadow recoloring and visible red eyes are still unresolved.**
This is a recovery of the partial appearance, not a finished Dark Link skin.
The package contains original code only: no textures, models or game binaries.

## Install

1. Download the **dark-link-wwhdmod** artifact from the **Dark Link mod** Actions
   workflow on this branch. Extract the artifact ZIP once to get `dark-link.wwhdmod`.
2. Install that file in the game's Mods manager and enable **Dark Link**.
3. Enable **Code mods** in setup if needed, complete the code-mod setup/build,
   and restart the game. Accept the normal code-mod trust prompt.

The same package is translated for the installed USA or EU build and the host
platform, including Android. It does not require a new APK/runtime feature.

### Upgrading from 0.1.0/0.1.1

Replace the installed package with 0.1.4 and fully restart the game, then load a
normal in-game save. Do not resume a full emulator/recomp save state made with the
old visual effect active: 0.1.1 left extra material override flags set. The new
version cannot infer those flags' original values from an already altered state.
Normal save data is not modified by this mod.

## Play

- Default gauge: 100 points; each damaging sword hit through the normal enemy
  collision handler adds 5. Twenty such hits fill the gauge.
- Press **L3 + R3 together** (both stick clicks), or tap **DARK**, to transform.
  Press again to cancel. On keyboard, use the bindings for both stick clicks.
- Default duration: **20 seconds of gameplay**. Default sword damage: **200%**.
  Pause menus and cutscenes suspend the timer; 30/60 FPS use the host's logic time.
- The HUD changes from charge to remaining duration while transformed.
- **Free transformation** enables immediate, unlimited-duration transformation;
  **Transform automatically when full** is an alternative to the button combo.
- Capacity, gain, duration, damage, HUD visibility and touch visibility/position
  are live options in the mod settings. Duration changes affect the next activation.
- Death, a stage transition, player deletion or disabling the mod clear the charge
  and transformation. No normal-save fields or global cheat settings are changed.

Touch coordinates use the centered 1280 x 720 canvas. The button's drawing and
touch claim share the same position, including on wider displays. No touch region
is claimed while the button is hidden or normal gameplay is suspended. If your
host controller mapping assigns another action to a stick click, use the DARK
button, automatic activation, or adjust that host binding.

## Appearance and limits

Only native C0/K0 RGB (indices 4 and 7) are changed. These are the two registers
used by the original version that the user reported partially recolored Link.
The other nine color registers, native alpha and fog remain unchanged. Current
player body/hands/hair instance identity limits the override; frozen Link is
skipped. Eye materials use the game's own names, with controller fallback. The
user's 0.1.3 trace confirmed the body eye indices 8/11; these indices are resolved
dynamically rather than hard-coded.

RGB values are saved once, reapplied in nested preparation, and restored bit for
bit. The two override-mask bits are restored to their original values instead of
being left enabled. Changes by native code to unrelated mask bits survive.
Scopes now surround native model entry/update (material preparation), ending
before viewCalc queues its asynchronous job. No visual hooks run around
viewCalc, joint submission or the UBO worker. This is a narrower scope than
0.1.0; its real-game behavior still requires confirmation.

There is no diagnostic mode/logging in this build. The ineffective local fog
approach has been removed. There is no custom aura, eye bloom, White/Gold variant
or transformation animation. A complete appearance needs further work; no binary
upload is required to use this fallback.

Damage scaling runs after native `at_power_check`, only for the current player's
three sword colliders and the sword attack flag. It changes the freshly calculated
damage, not persistent collider strength, enemy health or the death procedure.
Damage is capped at 127 (63 for native double-hit spins) because the native damage
path uses signed bytes. Charge
is awarded when `cc_at_check` reduces an enemy's health. Special bosses/scripted
targets that bypass these native paths are outside this first version's coverage;
armor-specific handlers can further override the native result.

Automated checks compile the PowerPC ELF, translate and compile it for USA/EU,
and execute gameplay and translated-hook tests. Checks cover both tint registers,
all untouched registers/alpha, absent/partial/full original override masks,
unrelated native mask changes, nested reapplication/restoration, name/controller
eye detection, missing uniforms, foreign models, frozen state, scope bounds,
controller/touch cancellation, Free transformation and lifecycle resets. These
checks validate memory behavior, not the game's rendered output.

Test from a fresh session with Free transformation: activate in a lit/shaded room,
then toggle off repeatedly and disable Dark Link. Verify restoration of clothes,
skin and eyes. The shaded areas may still retain their original colors; do not
treat this build as a confirmed fix for that limitation.

## Build

From the repository root, with a PowerPC-capable clang (also used as host compiler):

```sh
python examples/guest-mods/dark-link/build.py
```

Or use Zig's bundled clang:

```sh
python examples/guest-mods/dark-link/build.py --cc "python -m ziglang cc" --host-cc "python -m ziglang cc"
```

Output: `build/dark-link/dark-link.wwhdmod`. Only the manifest, original compiled
mod and this README are packaged. Build outputs are not committed.

Native layout references: the CC0 public `ZeldaWWHDDecomp/wwhd` source,
`d/d_cc_uty.cpp`, `include/d/d_cc_d.h`, `include/JSystem/J3DGraph.h`,
`d/actor/d_a_player_main_01.cpp`, `include/d/actor/d_a_player_main_01.h`,
`d/d_kankyo_6.cpp` and `m_Do/m_Do_ext_heap_model.cpp`.
