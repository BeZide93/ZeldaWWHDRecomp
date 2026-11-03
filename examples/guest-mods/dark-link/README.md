# Dark Link (prototype 0.1.31 — separate native frame artwork)

Standalone code mod for ZeldaWWHDRecomp: sword hits charge a transformation with
black body/equipment, red eyes and boosted sword damage. The user confirmed the
complete body, mouth, eyes, sword and shield appearance in **0.1.11**. That
texture-binding repair at **viewCalc return** is retained.

## Changed in 0.1.31

The 0.1.30 screenshot confirms that enlarging the green background did not
restore the missing native frame. That background enlargement is removed:
`P_MagicBase` is drawn at its own inner bounds again. The outer rectangle is
reserved for the separate `W_MagicBase` frame image and row/D-pad spacing.

The frame loader now supports untextured native materials, using their authored
white colour in linear space. Previously it required exactly one texture map,
so a valid solid-colour frame failed to load. Textured frame slices still use
the existing decoder/sampler. A wholly untextured frame does not require TXL.

The exact capacity-matched `W_MagicBase_00/01` frame remains independently
visible even if it belongs to the native Flash animation group. Membership
alone must not classify the normal frame as disposable warning artwork.
Only its border slices are copied, never window content. Other hidden/flash
panes and frames whose inner opening would cover the fill remain excluded.

Tests now distinguish the inner background draw from the outer border draw,
check solid frame pixels and transparent centres, and exercise zero-map
materials without a texture list or readable window content. Textured frames,
capacity selection, scaling, spacing and unrelated flash rejection remain
covered. The screenshots do not expose the frame material itself, so the
on-device result is still needed to confirm which rejection affected it.

Install `dark-link-0.1.31.wwhdmod`, complete the mod build and restart.
No app or Enhanced Controls rebuild is required.

## Changed in 0.1.30 (superseded background sizing)

The user confirmed the 0.1.29 gauge is now close, but its frame is too thin.
The background was still drawn at the inner `P_MagicBase` size. Its outer
bounds now come from the matching `W_MagicBase_00/01` rectangle, even when
that window's warning artwork is hidden. Only enclosing, unrotated, bounded
geometry is accepted; unavailable or invalid bounds retain the previous size.
This uses the existing native background art and does not enable flash content.
The red inner fill retains its dimensions and independent charge/time crop.

Placement includes both outer margins so the larger frame does not overlap
the normal magic bar. Details and borders use the same placement calculation;
the existing D-pad reservation measures the resulting outer row height.
Synthetic tests cover short/long capacity, unequal X/Y scale, unchanged fill
size, an eight-unit gap between outer frames, and repeated cached draws.
Final frame thickness still needs checking on the device with its native assets.

Install `dark-link-0.1.30.wwhdmod`, complete the mod build and restart.
No app or Enhanced Controls rebuild is required.

## Changed in 0.1.29

The 0.1.28 screenshot identifies the selection error: `P_MagicBase_00` was
selected as `BASE+FILL`, while the actual `P_Magic_00` (190x12, two texture
maps) was discarded as `outline-bounds`. The 386x18 / 380x12 long variant
uses the corresponding `_01` names. These exact native fill names now take
precedence over the old green-colour/first-stage-progress heuristic, including
when the native pane is hidden. Only Dark Link's charge/time crops the result.

The separate MagicBase artwork retains its authored colours; removing green
from that background also removed the native frame shading. Neutralisation
is now limited to layouts where the same picture really is both base and fill.
Named `W_MagicBase_00/01` frames can ignore native visibility for the matching
capacity, but flash-group membership and geometric opening checks still apply.
A frame must leave the actual inner fill unobstructed, rather than enclose the
larger background as well. The report panel now fits its row count.

Tests reproduce the reported names, dimensions, texture-map counts and hidden
panes at 0/16/32 magic capacity; they assert fill geometry, background pixels,
transparent frame centres, flash rejection and no source-resource mutation.
This fixes selection and frame handling, not a complete material interpreter:
the picture baker still samples the first texture map. The screenshot does not
expose the second map's combiner, so no guessed two-texture blend is introduced.
**Final appearance still needs on-device checking.**

Install `dark-link-0.1.29.wwhdmod`, complete the code-mod build and restart.
No app or Enhanced Controls rebuild is required. The 0.1.26 startup guard and
0.1.28 diagnostic text-buffer ownership fix remain in place.

## Changed in 0.1.28

Fixes the entire Dark Link overlay disappearing when **Appearance diagnostics**
is enabled. The new gauge report in 0.1.27 passed a guest-stack string to
`wwhd_hud_emit`. The runtime's `svc_hud_record` accepts buffers only inside the
calling mod's assigned memory region; it therefore rejected the string and
invalidated the whole list, including the gauge and touch controls.

The report now uses a static buffer in the mod's BSS. The service copies the
string synchronously, allowing the same buffer to be reused for each line.
The translated callback tests now check element and text pointer ownership
before the existing geometry/UTF-8/limit validation. This catches the published
0.1.27 module before the fix; the previous tests copied arbitrary guest memory
and therefore missed the service-level rejection.

The user's 0.1.28 screenshot confirmed that the report works and supplied the
native pane names used by the 0.1.29 selection fix above.

## Gauge diagnostics added in 0.1.27

The user confirmed that 0.1.26 removed the startup crash. The red gauge still
looks flat. This version adds read-only observations to **Appearance diagnostics**
(default off); it does **not** claim to fix the design or change gauge selection.
The second native HUD loader from 0.1.25 remains removed, and the startup-recovery
package checks remain active.

Use the current package described above.
Enable **Appearance diagnostics** in the mod settings and capture one screenshot
with the gauge visible. Below the appearance panel, the report shows the active
capacity, picture/window counts, rejected UVs, animation mask, selected fill
material colours, and each parsed pane's selection/rejection reason. For picture
rows the numeric triplet is texture count / progress flag / effective alpha;
window rows use frame count in place of texture count. At most 48 rows are shown;
truncation is explicitly counted. No diagnostic file or game artwork is exported.

`BASE`, `FILL`, `BASE+FILL`, `DETAIL` and `BORDER` identify submitted artwork.
`hidden/flash`, `outline-bounds`, `opaque-centre`, `wnd-opening/bounds`,
`wnd-image-failed`, `pixels-failed` and `pane-query` distinguish the existing
filters and loading failures. The animation mask bits are InAll, Flash,
ColorChange, ScaleChange and progress, in that order. These observations identify
which existing assumptions fail on the actual game resources, avoiding another
unverified change to the loader or rendering rules.

Gameplay, body/eyes/equipment, aura, D-pad spacing and the shared editor retain
the 0.1.26 behavior. Diagnostics do not write game memory or invoke a new native
function. They record metadata while the existing image cache loads; disabling
them emits no report commands. Existing generated gauge images remain local to
the mod's Data folder. No app rebuild or Enhanced Controls update is needed.

## Startup recovery retained from 0.1.26

The reported 0.1.25 crash enters the game's layout loader (0287F640) through our
second MagicBar init call. It removed the embedded parts descriptor and entered
a standalone loading path whose resources had not been established. The null
dereference happens inside the native call, before post-load checks. Mocked init
calls did not exercise the real resource loader. Version 0.1.26 restored the
0.1.24 gauge and removed native copy construction/drawing/teardown; an actual
on-device restart has now been confirmed. Do not use 0.1.25 for play or load its
full-state snapshots. The resource-only gauge path is retained; full multi-texture material support
is still pending.

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
green chroma in the empty copy. Prefer a distinct progress-animated fill; only
without one use the base itself as the full-size fill.
Bake the selected UV rectangle and texture scale/translation into the copied
pixels with bilinear sampling and the native clamp/repeat/mirror sampler.
Repeated textures use at least pane resolution to retain their detail. Partial fill then crops image
width and normalized UVs; native out-of-range/reversed UVs never reach the HUD.

Cache generated PNGs as `magic-{0,1}-{frame,fill,border}.png` and
`magic-{0,1}-detail-{00..07}.png` in this mod's
Data folder and load them through the existing HUD image service. The source
archive is read-only; these local derivatives are never included in the package
or uploaded. Conversion only runs on load/retry, not on every drawing frame.
Each decoded image is bounded to 131072 pixels and each cache write is below
1 MiB. Unsupported or missing textures are skipped and retried, with no fake
rectangle fallback. Picture vertex gradients are baked into the images. Window
borders use their original frame textures/materials and displayed alpha. Their
inner opening must enclose the selected gauge. Hidden/warning window decorations are excluded; the core base/fill use mod-owned
visibility and never inherit native pane/ancestor fade alpha.
Up to eight additional picture layers per capacity preserve native outlines
and progress artwork. Static outlines require a transparent inner area; progress
layers must fit inside the gauge. Their native geometry and texture detail are
retained, with green artwork recoloured red. Arbitrary multi-texture shader
effects are not reproduced.

A bounded BFLAN reader applies InAll at its end and Flash/ColorChange/ScaleChange
at zero for the displayed ordinary-Link style, then
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
`DL 0.1.31 ON/OFF`; `Magic` counts loaded frame/fill images (up to four),
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
