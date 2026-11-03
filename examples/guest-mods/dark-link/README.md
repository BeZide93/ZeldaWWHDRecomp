# Dark Link (prototype 0.1.22)

Standalone code mod for ZeldaWWHDRecomp: sword hits charge a transformation with
black body/equipment, red eyes and boosted sword damage. The user confirmed the
complete body, mouth, eyes, sword and shield appearance in **0.1.11**. That
texture-binding repair at **viewCalc return** is retained.

## Changed in 0.1.22

The 0.1.21 device screenshot showed a white overlay and only a small visible
part of the red fill. Two defects in the copy are now covered by regressions:
window selection ignored visibility/alpha, and its outer-box test accepted
thick frame slices that covered the gauge. The screenshot alone cannot identify
which native resource produced the overlay.

The copy now reconstructs the displayed, non-warning style from native BFLAN
bindings: `InAll` at its end, `Flash` at zero, `ColorChange` at zero (ordinary
Link), `ScaleChange` at zero, followed by the normal/doubled full-capacity curve.
This follows `hd_screen_magic_bar.cpp` in decomp revision
`b2811a3ce838d1f875bc8e68536c1950af5d7ffc`. Named style tags must match their
binding group; only wholly validated animations are applied, in fixed order.
Pane visibility and alpha tracks now affect the copy, including its ancestors.
Window material colours and texture transforms use the same displayed state.

Warning effects in `G_Flash_00` are excluded even when their animation is absent.
A copied window's transparent inner opening must contain the selected gauge;
matching only its outer size is insufficient. Partial pane alpha is baked into
the generated pixels. No live HUD or magic/save values are changed.

Texture detail, four vertex colours, red/green channel recolouring and the
user-confirmed 0.1.20 D-pad correction remain. The temporary D-pad diagnostics
and flat-red overwrite removed in 0.1.21 stay removed. No extra logging/hooks or
game artwork are added. Enhanced Controls 0.4.0 remains compatible.

Validation uses synthetic artwork, including invisible/transparent windows,
thick frames, hidden parents, warning groups with missing animations, partial
alpha and white authoring colours animated to the displayed green. The published
0.1.21 module fails the invisible-frame regression; the corrected module passes.
The precise final gauge appearance still needs an in-game check.

## Added in 0.1.17

- Original Dark Link medallion icon in the Enhanced Controls style.
- The same touch editor as Enhanced Controls, with a standalone saved profile.
- One shared editor for all five mod buttons when Enhanced Controls 0.4.0 is
  installed; update both packages to enable this.

See the 0.1.17 section below for editor ownership, compatibility and validation.

## Fixed in 0.1.15

- Bake native texture coordinates and animated scale/translation into the
  copied gauge pixels. Version 0.1.14 sent these coordinates directly to the
  HUD: out-of-range or reversed UVs cause the renderer to discard the entire
  mod list, including the DARK button. The emitted UVs now stay within [0,1].
- Resample with bilinear filtering and edge clamping; preserve coverage alpha
  and crop the baked full-capacity red fill by the Dark Link timer.
- Reject unsupported rotated textures and invalid gauge geometry locally so
  they cannot discard the button or diagnostic overlay.
- Replay actual translated-hook HUD output through the runtime's real Store
  validator, including reversed/out-of-range scales and malformed resources.
- Retain 0.1.14's black aura blend equation/full-capacity animation handling
  and the user-confirmed 0.1.11 body/face/eye/equipment binding path.

USA/EU compilation, translated-hook regressions and synthetic pixel/PNG tests
pass. **The new appearance still needs checking in-game**: no game executable
or actual game artwork is available in this development environment. These
checks do not establish GPU output or support for every authored material.
The package contains original code only, with no game artwork/models/binaries.
No app rebuild is needed.

## Install and play

1. Download `dark-link.wwhdmod` from the **Dark Link mod** workflow artifact
   `dark-link-wwhdmod`, or use the supplied versioned package.
2. Replace the installed Dark Link package in Mods, finish its code-mod build,
   and fully restart the game. The same package supports USA/EU and Android.
3. Press **L3 + R3** together or tap **DARK** to transform; press/tap again to
   cancel. Defaults: 100 charge, 5 per damaging sword hit, 20 seconds, 200% damage.

**Free transformation** permits immediate transformation with unlimited duration.
Capacity, charge gain, duration, damage, automatic activation, gauge visibility,
and DARK button visibility/position remain configurable. The touch button keeps
its existing behaviour. Menus and cutscenes pause the transformation timer and
hide mod HUD. Death, stage changes, player deletion and disabling clear the
transformation. No normal-save values or global cheat options are written.

## Implementation

### Native aura

`setAuraEffect` (0240B7F4) updates the native shell's matrices and UV animation.
When no real Magic Armor/Tingle effect is active, temporarily supply its BRK
animation frames during that call, then restore the normal native fade value.
During Link's draw, expose the active frames and restore the frame controller,
resource and renderer channel afterwards. No shield flag, Tingle timer, magic,
rupees or invulnerability is changed. An actual armor effect retains its own
native update path. Animation length comes from the resource vtable, not the
uninitialized playback end field.

Only the current player's shell at Link+5700 joins the existing black palette
and `_a0` texture-view path. Its FMAT render state uses zero and inverse
source-alpha colour blending while active. If alpha originally shared the RGB
equation, preserve that original equation in separate-alpha fields. Restoration checks actor, model,
material array and resource identities and restores owned bits while preserving
unrelated changes. Other actors and held non-sword items are excluded.

### Magic gauge artwork

Read the resident **MagicBar_00** SARC/BFLYT with bounded offsets. Find the native
P_MagicBase_00/01 and the authored green fill. Query their original geometry and
UVs. Decode the referenced BFLIM (RGBA8, BC1–5; linear, micro and macro tiling),
apply the material's black/white colours in linear light, bake all four vertex
colours, then swap the final red/green channels. Preserve
texture/material alpha and contrast. When the base itself is green, remove its
green chroma in the empty copy and retain its full-size art for the red fill.
Bake the selected UV rectangle and texture scale/translation into the copied
pixels with bilinear sampling and edge clamping. Partial fill then crops image
width and normalized UVs; native out-of-range/reversed UVs never reach the HUD.

Cache generated PNGs as `magic-{0,1}-{frame,fill,border}.png` in this mod's
Data folder and load them through the existing HUD image service. The source
archive is read-only; these local derivatives are never included in the package
or uploaded. Conversion only runs on load/retry, not on every drawing frame.
Each decoded image is bounded to 131072 pixels and each cache write is below
1 MiB. Unsupported or missing textures are skipped and retried, with no fake
rectangle fallback. Picture vertex gradients are baked into the images. Window
borders use their original frame textures/materials and displayed alpha. Their
inner opening must enclose the selected gauge. Hidden/warning panes are excluded.
Arbitrary multi-texture shader effects are not reproduced.

A bounded BFLAN reader first applies the displayed style described above, then
selects a 16/32-frame animation explicitly bound to
`G_Bar_00`, validates all tracks before applying any, and evaluates its full
frame. It supports FLPA picture fields, FLMC material colours, FLVC corner
colours and the first FLTS texture transform. Animated ancestor-pane transforms,
texture-pattern swaps and rotated UVs are not supported. If no matching valid
animation exists, the static authored state is retained. Only mod-owned copies
change; the native HUD, animation controllers and magic amount stay untouched.

The native HD magic controller (02675414) supplies live base position, size and
origin. Place the copy eight layout units below it; before magic is unlocked,
use Main_00's authored placement. Normal and doubled capacities are supported.
The gauge shows charge, remaining duration, or full capacity in Free mode,
independently of normal magic. Full-state load renews image handles and position.

### Existing appearance and gameplay

Body, hands, hair, sword grip/blade, scabbard and shield keep 0.1.11's immutable
FTEX/GX2 views. RGB selectors are black, or red for the six named eye materials;
alpha, images, UVs and toon ramps remain native. Repair after native viewCalc
handles late mouth/eye texture animation. Restore the latest native binding on
cancel/disable, with identity checks for stale models. Cache capacity is bounded.
Scoped C0..C2/K0..K3 palette overrides preserve original RGB and override flags.

Damage scaling applies to freshly computed native sword damage, capped at 127
(or 63 for a double-hit spin). Only damaging hits through the native sword
collision path charge the gauge. Boss-specific handlers may override damage.

## Diagnostics and validation

Enable **Appearance diagnostics** only when needed. The first line reads
`DL 0.1.22 ON/OFF`; `Magic` counts loaded frame/fill images (up to four),
`Pos` is 1 for native or 2 for authored placement, and `Aura` reports the shell
model being present. Body material/albedo/eye counts, binding retention and late
repair counters retain their existing meanings. `Fill` reports the actual
Dark Link percentage (100% in Free mode); `Full` reports whether matching full
animations were applied for normal/doubled capacity. Diagnostics do not prove GPU
output. No Share log or game-file upload is required.

Automated checks cover gameplay/timing, body/eye/equipment texture restoration,
nested native refresh, animated face replacements, malformed layout bounds,
material-derived red pixels and alpha, PNG checksums/zlib decoding, full-width
coloured bases, UV cropping, native placement, handle reuse/state-load invalidation,
zero-length playback controllers with valid resource lengths, visual-only aura
frames, genuine armor coexistence, RGB suppression and preserved alpha blending.
Regression fixtures cover half-width authored fills becoming full-width at both
16/32 frames, texture SRT, malformed animation rejection, full Free-mode coverage
and neutral vertex tint. These are synthetic resource fixtures, not a game run.

In-game acceptance: activate Dark Link, check the black aura in motion and the
red bar below normal magic, drain/charge the bar, then toggle off and verify
normal Link/armor appearance. Check a save before and after acquiring magic,
doubled magic, menus, a stage change and full-state reload.

## Build

```sh
python examples/guest-mods/dark-link/build.py
```

With Zig's bundled clang:

```sh
python examples/guest-mods/dark-link/build.py --cc "python -m ziglang cc" --host-cc "python -m ziglang cc"
```

Output: `build/dark-link/dark-link.wwhdmod`. Only manifest, compiled mod and this
README are packaged. Builds are not committed. Mod-only edits run package CI;
the app platform build filters are retained.

Layout references: public CC0 `ZeldaWWHDDecomp/wwhd`, especially
`d/actor/d_a_player_main_04.cpp`, its aura/item headers, `d_a_player_main_01.cpp`,
`d/hd_screen_magic_bar.cpp`, and `d/hd_parts_progress_bar.cpp`.
Texture/art references: this repository's `story_prop_textures.cpp`,
`game_layouts.cpp`, `aspect.cpp`, `tools/story_mods/lyt`, Cemu GX2 registers,
[BfresLibrary RenderState](https://github.com/KillzXGaming/BfresLibrary/blob/master/BfresLibrary/Shared/Models/Material/RenderState.cs)
and [NW4F render-state bit layout](https://github.com/nw4f/G3d/blob/nsmbu/include/nw/g3d/res/g3d_ResMaterial.h).

### 0.1.17 — shared touch editor and Shortcut spacing

The transformation button now uses an original transparent Dark Link medallion
matching the Enhanced Controls icons. The image is included in the package;
there are no copied game textures in the repository.

Tap **Edit** to move the button, change its size, opacity, circle/square shape,
fill, icon/text style or visibility. The toolbar matches Enhanced Controls.
**Save / Done** persists the layout; **Cancel** discards the draft. Hidden buttons
remain available through the top tabs. Editing suppresses combat and transform
inputs; the game continues running, so edit in a safe place.

Install **Enhanced Controls 0.4.0+ together with Dark Link 0.1.17+** for one Edit
button and all five tabs (Jump, Slash, Spin, Dodge, Dark Link). Enhanced Controls
owns the combined saved layout; each mod still owns and draws its own textures.
With Enhanced Controls absent/disabled, Dark Link uses its own standalone saved
layout. Updating only one package cannot enable the combined editor. The native
controller overlay's separate editor is not part of this mod-button editor.
Legacy Dark Link X/Y options seed a fresh standalone layout; subsequent changes
are made through Edit. Enhanced Controls' four-button v1 layouts migrate to v2.

The 0.1.17 Main_00 drawLayout spacing attempt is superseded by 0.1.18 below.

The SDK currently has no mod exports. The shared editor uses a versioned,
synchronous value-only rendezvous at the verified read-only native getter
0267598C (`hdHudMainStateNumber_c::getNumber`, returns its object's first word).
Only explicit protocol packets in the loader's mod arena are handled; native
game objects are ignored. There are no fixed scratch addresses, saved peer
pointers, cross-owner texture handles or new host services. Thus these are mod
package updates; they do not require a new app build. The public layout/getter
evidence is pinned to ZeldaWWHDDecomp/wwhd `b2811a3ce838d1f875bc8e68536c1950af5d7ffc`.

Validation: USA/EU translation and native compilation, existing gameplay,
appearance/aura/gauge regressions, native-shaped nested Shortcut draw fixtures,
and real runtime HUD transaction validation. The two-module integration test
loads separately translated modules at different guest bases, tests both hook
orders, save/restart/cancel/short writes, hidden buttons, standalone modes and
per-owner image/list validation. Run it after building both packages:

```
python examples/guest-mods/dark-link/test_shared.py --enhanced-root /path/to/enhanced-mod
```

These tests use synthetic memory; actual in-game positioning still needs a
runtime check on the target device.
