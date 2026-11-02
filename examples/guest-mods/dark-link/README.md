# Dark Link (prototype 0.1.0)

Standalone code mod for ZeldaWWHDRecomp. Charge with damaging sword hits, then
transform into a charcoal-colored Link with red eyes and stronger sword attacks.
This package contains original code only: no textures, models, game binaries or
save data. It uses the player's installed Link model and native material uniforms.

## Install

1. Download the **dark-link-wwhdmod** artifact from the **Dark Link mod** Actions
   workflow on this branch. Extract the artifact ZIP once to get `dark-link.wwhdmod`.
2. Install that file in the game's Mods manager and enable **Dark Link**.
3. Enable **Code mods** in setup if needed, complete the code-mod setup/build,
   and restart the game. Accept the normal code-mod trust prompt.

The same package is translated for the installed USA or EU build and the host
platform, including Android. It does not require a new APK/runtime feature.

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

## Scope and limits

This first version implements the body tint, red eye material colors and gameplay
mechanic. It does **not** include Dawnlight's custom flowing aura, eye-bloom shader,
White/Gold variants or a transformation animation. Eye textures and expressions,
depth testing and eyelid occlusion remain native. Frozen Link keeps his ice look.
Equipment retains its normal appearance.

Damage scaling runs after native `at_power_check`, only for the current player's
three sword colliders and the sword attack flag. It changes the freshly calculated
damage, not persistent collider strength, enemy health or the death procedure.
Damage is capped at 127 (63 for native double-hit spins) because the native damage
path uses signed bytes. Charge
is awarded when `cc_at_check` reduces an enemy's health. Special bosses/scripted
targets that bypass these native paths are outside this first version's coverage;
armor-specific handlers can further override the native result.

Visual overrides match the live player model instances (body, hands and hair).
Eye materials are identified by the native eye animation-controller pointers.
RGB uniforms are saved and restored around model/joint submissions and HD draws;
alpha and resource files are untouched. Unknown material counts fail closed.

Automated checks compile the PowerPC ELF, translate and compile it for both game
regions, and execute state and mocked guest-hook tests (including nested material
restoration). These checks do not replace a real-game visual/playtest. First test
Free transformation in a lit room, eye animation, different outfits, swimming,
menus, scene changes and combat against Bokoblins/Moblins. Compare body/eyes and
other actors before, during and after activation on Android and desktop.

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
