# Dark Link (prototype 0.1.7)

Standalone code mod for ZeldaWWHDRecomp: charge with sword hits, activate a damage
boost and a black-body/red-eye appearance. Version 0.1.7 adds per-instance albedo
texture views after native texture animation/refresh. Version 0.1.6's seven TEV
registers still only recolored lit surfaces and did not produce red eyes in the
user's test, so this version targets the sampled base texture as well.
**The new path's in-game appearance still needs verification.**
The package contains original code only: no textures, models or game binaries.

## Install

1. Download the **dark-link-wwhdmod** artifact from the **Dark Link mod** Actions
   workflow on this branch. Extract the artifact ZIP once to get `dark-link.wwhdmod`.
2. Install that file in the game's Mods manager and enable **Dark Link**.
3. Enable **Code mods** in setup if needed, complete the code-mod setup/build,
   and restart the game. Accept the normal code-mod trust prompt.

With the gauge visible, the HUD title must read **DARK LINK 0.1.7**. This label
comes from the running code module, not just the package manifest.

The same package is translated for the installed USA or EU build and the host
platform, including Android. It does not require a new APK/runtime feature.

### Upgrading from 0.1.0/0.1.1

Replace the installed package with 0.1.7 and fully restart the game, then load a
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

The new texture path operates on the current player's body, hands and hair only.
At the native model texture refresh (0x027F50F8), it first removes any previous
owned bindings, lets the game update textures/animation, then replaces each
material's explicitly named `_a0` albedo binding with an immutable copied FTEX/GX2
texture descriptor. It resolves the native material index instead of assuming
that J3D and NNS material arrays have the same order or that albedo is slot zero.
The runtime's native prop texture support uses this same refresh/binding boundary.

The copied descriptor selects constant RGB `(0, 0, 0)` for body/hands/hair and
`(1, 0, 0)` for eyes. Both GX2 component selectors and their packed texture register
are changed. The original alpha selector, dimensions, image/mip pointers, format,
UV animation and other register bits are preserved. The original descriptors and
image bytes are never modified. This works at sampling time and does not require
recompressing textures, creating/uploading images, or replacing game files.

Eye materials are resolved by native names: `eyeL`, `eyeR`, `eyeLdamA`,
`eyeRdamA`, `eyeLdamB`, `eyeRdamB`, with controller identity as a fallback.
Eyebrows use black. The currently animated eye texture is used after each refresh,
including its original transparency. Toon ramps and other sampler slots are left
untouched; materials without an explicit `_a0` sampler are skipped rather than
recoloring an unknown texture. Equipment and other actors keep their bindings.

Bindings remain live after the outer submission wrapper returns, because queued
packets may still read them. They are restored before the next native texture
refresh and on cancellation, disable, death, transition and player deletion.
Restoration checks player/model/material-array/resource identity and only replaces
slots still containing the mod's exact view, preserving newer native/other-mod
bindings. Stale identities are discarded without dereferencing old slot pointers.
Frozen Link receives no new texture overrides.

The descriptor cache holds at most 512 immutable views (roughly 162 KiB). It has
no dynamic allocations, pixel copies or eager frees; published entries are never
reused or rewritten while queued draws could reference them. Identical descriptors
reuse a view. On capacity exhaustion or unsupported/invalid structures, the new
binding is skipped and the existing TEV tint remains the fallback.

The existing seven-register TEV palette and its four user-confirmed submission
hook pairs are unchanged. RGB and the original override bits are restored at
scope exit; lighting registers, alpha and fog are not modified. No UBO worker
hook is installed. There is no diagnostic mode/logging, custom aura, eye bloom,
White/Gold variant or transformation animation.

Damage scaling runs after native `at_power_check`, only for the current player's
three sword colliders and the sword attack flag. It changes the freshly calculated
damage, not persistent collider strength, enemy health or the death procedure.
Damage is capped at 127 (63 for native double-hit spins) because the native damage
path uses signed bytes. Charge
is awarded when `cc_at_check` reduces an enemy's health. Special bosses/scripted
targets that bypass these native paths are outside this first version's coverage;
armor-specific handlers can further override the native result.

Automated checks compile the PowerPC ELF, translate and compile it for USA/EU,
and execute gameplay and translated-hook tests. Texture tests use synthetic
FMAT/FTEX data with reordered material indices and albedo in slot one. They cover
black/red hardware component selectors, original alpha and all other descriptor
words, immutable cached views, animated eye-frame changes, bindings retained after
outer submission, native/other-mod changes, absent samplers and invalid resources,
hands/hair, freezing, cancellation, disable, stage changes, death, deletion and
stale material-array identity. Existing TEV/timing/gameplay checks also run.
These checks validate memory and descriptor behavior, not the game's shader output.

Test from a fresh session with Free transformation: activate in lit and shaded
areas, blink/take damage to exercise eye variants, toggle off repeatedly and disable
Dark Link. Verify black clothes/skin/hair/hands, red eyes and restoration on exit.
Check casual clothes as well. Confirm **DARK LINK 0.1.7** in the running HUD.

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

Texture-path references: `runtime/src/mods/story_prop_textures.cpp`,
`runtime/src/mods/story_props.cpp`, `runtime/src/mods/story_actor_textures.cpp`,
`tools/story_mods/export_glb.py`, `runtime/third_party/cemu/gx2_texture_regs.cpp`,
`runtime/third_party/cemu/Cafe/HW/Latte/ISA/LatteReg.h`, and the public decomp's
`d/actor/d_a_obj_ftree.cpp` and `d/actor/d_a_tbox.cpp`.
