# Dark Link (diagnostic prototype 0.1.3)

Standalone code mod for ZeldaWWHDRecomp. The gauge, activation, HUD and sword
boost are implemented. **The Dark Link appearance is not working yet.** Version
0.1.3 removes the ineffective fog edits and provides a bounded, read-only probe
of the model/material path. It does not recolor Link, and is not a visual fix.
This package contains original code only: no textures, models, game binaries or
save data.

## Install

1. Download the **dark-link-wwhdmod** artifact from the **Dark Link mod** Actions
   workflow on this branch. Extract the artifact ZIP once to get `dark-link.wwhdmod`.
2. Install that file in the game's Mods manager and enable **Dark Link**.
3. Enable **Code mods** in setup if needed, complete the code-mod setup/build,
   and restart the game. Accept the normal code-mod trust prompt.

The same package is translated for the installed USA or EU build and the host
platform, including Android. It does not require a new APK/runtime feature.

### Upgrading from 0.1.0/0.1.1

Replace the installed package with 0.1.3 and fully restart the game, then load a
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

## Capture the visual diagnostic

1. Install 0.1.3 and start a fresh session from a normal in-game save.
2. Keep **Visual diagnostic logs** enabled (default in this diagnostic build).
3. Wait about three seconds in gameplay, enable **Free transformation**, then
   tap **DARK**, wait three seconds, tap it again and wait three seconds.
4. Share the regular application log. Android's setup screen has **Share log**;
   a crash is not needed. Look for `[guestmod:dark-link]` and `DL` probe lines.

The probe logs the version, active/enabled/gameplay state, observed model-path
counts and one scalar snapshot per player model/path/state. It covers at most
four state changes and 60 logic steps per state. Repeated frames do not produce
repeated snapshots. Switch the diagnostic option off/on to capture again.
Logging stops while the option is off. It reads material IDs, pointer addresses,
flags and per-instance material records, not texture/model asset contents.

Path IDs in the log: 0 = mDoExt model entry wrapper, 1 = native model entry,
2 = native model entry return, 3 = viewCalc entry. Model slots: 0 = body,
1 = hands, 2 = hair. All numeric row values are hexadecimal. An absent path or
model in a capture is evidence too; the summary includes zero match counts.
No asynchronous UBO worker hook is installed. Unknown material counts are skipped.

## Scope and limits

The previous material-register edit altered lighting and left override bits set.
The subsequent local PE-fog edit had no visible effect in the user's game.
`runtime/src/fog_pin.cpp` shows the HD renderer copying fog from a per-view object
into scene uniforms, so local legacy fog fields are not evidence of a working
per-material recolor. The precise Link shader/material route remains unresolved.
Do not replace it with another write based only on a synthetic-memory test.

There is no custom aura, eye bloom, White/Gold variant or transformation animation.
The diagnostic build leaves all game material/render data unchanged.

Damage scaling runs after native `at_power_check`, only for the current player's
three sword colliders and the sword attack flag. It changes the freshly calculated
damage, not persistent collider strength, enemy health or the death procedure.
Damage is capped at 127 (63 for native double-hit spins) because the native damage
path uses signed bytes. Charge
is awarded when `cc_at_check` reduces an enemy's health. Special bosses/scripted
targets that bypass these native paths are outside this first version's coverage;
armor-specific handlers can further override the native result.

Automated checks compile the PowerPC ELF, translate and compile it for both game
regions, and execute state and mocked guest-hook tests. Tests cover gameplay,
controller/touch cancellation, read-only material/instance snapshots, model
identity filtering, per-phase snapshot suppression, capture-window/session bounds,
and logging disabled. They do not execute the game's renderer or prove a visual
fix. A real application capture is required for the next implementation step.

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
