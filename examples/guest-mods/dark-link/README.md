# Dark Link (prototype 0.1.14)

Standalone code mod for ZeldaWWHDRecomp: sword hits charge a transformation with
black body/equipment, red eyes and boosted sword damage. The user confirmed the
complete body, mouth, eyes, sword and shield appearance in **0.1.11**. That
texture-binding repair at **viewCalc return** is retained.

## Fixed in 0.1.14

- The visible shell still uses violet shader/BRK colours. Suppress source RGB
  in the shell's blend equation: `0 * source + (1 - sourceAlpha) * destination`.
  This produces a black veil using the native animated coverage, independently
  of the shader RGB. Preserve the original alpha equation and restore owned
  blend fields when disabled.
- Read the full-capacity state of native `G_Bar_00` animations (16 or 32 frames),
  including picture size/scale, material colours and texture transforms. The
  static BFLYT state alone can describe a partially filled bar.
- Give fill pixels the requested red RGB directly while retaining their alpha.
  Do not multiply a dark source gradient by red or darken it again with green
  vertex RGB. Apply the authored/animated UV transform before duration cropping.
- Keep the working 0.1.11 body/face/eye/equipment binding path unchanged, and
  retain 0.1.13's resource-derived aura animation length.

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
apply the material's black/white colours in linear light, and replace fill RGB
with (255, 32, 32). Preserve texture/material alpha. When the base itself is green,
make its empty copy black and use the same full-size art for the red fill.
Partial fill crops the image width and UVs, rather than stretching it.

Cache four small generated PNGs as `magic-{0,1}-{frame,fill}.png` in this mod's
Data folder and load them through the existing HUD image service. The source
archive is read-only; these local derivatives are never included in the package
or uploaded. Conversion only runs on load/retry, not on every drawing frame.
Each decoded image is bounded to 131072 pixels and each cache write is below
1 MiB. Unsupported or missing textures are skipped and retried, with no fake
rectangle fallback. The frame retains its first authored vertex colour; the
fill retains its alpha and uses neutral vertex RGB. Arbitrary multi-texture
shader effects and per-corner alpha gradients are not reproduced.

A bounded BFLAN reader selects a 16/32-frame animation explicitly bound to
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
`DL 0.1.14 ON/OFF`; `Magic` counts loaded frame/fill images (up to four),
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
