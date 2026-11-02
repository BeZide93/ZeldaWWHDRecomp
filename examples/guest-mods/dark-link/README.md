# Dark Link (prototype 0.1.6)

Standalone code mod for ZeldaWWHDRecomp: charge with sword hits, activate a damage
boost and a black body/red-eye TEV palette. Version 0.1.6 extends the two-register
palette to all seven TEV RGB registers and recognizes all six native eye materials.
It retains the four submission hook pairs from 0.1.5, whose partial tint the user
confirmed working. **The expanded palette's final appearance still needs an in-game
check; host tests cannot establish full shadow coverage or visible red eyes.**
The package contains original code only: no textures, models or game binaries.

## Install

1. Download the **dark-link-wwhdmod** artifact from the **Dark Link mod** Actions
   workflow on this branch. Extract the artifact ZIP once to get `dark-link.wwhdmod`.
2. Install that file in the game's Mods manager and enable **Dark Link**.
3. Enable **Code mods** in setup if needed, complete the code-mod setup/build,
   and restart the game. Accept the normal code-mod trust prompt.

With the gauge visible, the HUD title must read **DARK LINK 0.1.6**. This label
comes from the running code module, not just the package manifest.

The same package is translated for the installed USA or EU build and the host
platform, including Android. It does not require a new APK/runtime feature.

### Upgrading from 0.1.0/0.1.1

Replace the installed package with 0.1.6 and fully restart the game, then load a
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

The palette writes C0/C1/C2 and K0/K1/K2/K3 RGB (indices 4 through 10):
black `(0, 0, 0)` for body/hands/hair, red `(1, 0, 0)` for eyes. The prior
C0/K0-only palette left other TEV color inputs unchanged. This extension targets
those inputs without changing the four material/ambient lighting registers
(indices 0 through 3), native alpha or fog. It does not reintroduce the eleven-slot
lighting override from 0.1.1. Equipment and other actors retain their native colors.

Eye materials are resolved by their native names: `eyeL`, `eyeR`, `eyeLdamA`,
`eyeRdamA`, `eyeLdamB`, `eyeRdamB`. The latter four support damage expressions;
`mayu*` eyebrow materials stay black. Controller identity remains a fallback.
Texture animation, eyelid visibility and alpha remain native. The current player's
body/hands/hair model instance identity limits the override; frozen Link is skipped.

RGB values are saved once, reapplied in nested preparation, and restored bit for
bit. The seven owned override-mask bits are restored to their original values.
Changes by native code to unrelated mask bits survive. Missing parameter pointers
are skipped without enabling an override for that slot.

The scope still uses the mDoExt model entry/update wrappers, viewCalc and
joint-entry hook pairs from 0.1.5. In 0.1.4, restoration at the inner native
preparation return removed the tint before the wrapper's subsequent submission.
These working outer boundaries have not changed. They are submission boundaries,
not immediate GPU draws; asynchronous rendering and the game's actual shader output
are not validated by the host tests. No UBO worker hook is installed.

There is no diagnostic mode/logging, custom aura, eye bloom, White/Gold variant or
transformation animation. No binary upload is required to use this package.

Damage scaling runs after native `at_power_check`, only for the current player's
three sword colliders and the sword attack flag. It changes the freshly calculated
damage, not persistent collider strength, enemy health or the death procedure.
Damage is capped at 127 (63 for native double-hit spins) because the native damage
path uses signed bytes. Charge
is awarded when `cc_at_check` reduces an enemy's health. Special bosses/scripted
targets that bypass these native paths are outside this first version's coverage;
armor-specific handlers can further override the native result.

Automated checks compile the PowerPC ELF, translate and compile it for USA/EU,
and execute gameplay and translated-hook tests. Checks cover all seven TEV RGB
registers, all six named eye variants and a black eyebrow material, hands/hair,
unchanged lighting registers and alpha, missing/partial/full original override
masks, unrelated native mask updates, missing parameter pointers, nested restoration,
the preparation-to-submission interval, both wrappers and standalone view/joint
scopes. Gameplay checks retain cancellation, Free transformation and lifecycle resets.
These checks validate memory behavior, not the game's rendered output.

Test from a fresh session with Free transformation: activate in lit and shaded
areas, blink/take damage to exercise eye variants, toggle off repeatedly and disable
Dark Link. Verify black clothes/skin/hair/hands, red eyes and restoration on exit.
Check casual clothes as well. Confirm **DARK LINK 0.1.6** in the running HUD.

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
`d/d_kankyo_6.cpp`, `d/actor/d_a_swhit0.cpp`, `d/actor/d_a_bgn_draw.cpp`,
`d/actor/d_a_player_main_04b.cpp` and `m_Do/m_Do_ext_heap_model.cpp`.
