# Dark Link (prototype 0.1.12)

Standalone code mod for ZeldaWWHDRecomp: sword hits charge a transformation with
black body/equipment, red eyes and boosted sword damage. The user confirmed the
complete body, mouth, eyes, sword and shield appearance in **0.1.11**. That
texture-binding repair at **viewCalc return** is retained.

## New in 0.1.12

- Reuse Link's native **Magic Armor shell (Ymgcs00)** with black RGB and alpha
  blending. An additive black effect would be invisible. The native animation,
  skinning, UV animation, texture alpha and depth settings are retained.
- Replace the hand-drawn bottom-centre panel with the game's **MagicBar_00**
  frame and fill artwork. The fill is red and shows charge before transformation,
  remaining duration while active, or a full bar in Free transformation mode.
- Place the copy eight layout units below the current native magic bar. Read
  its live position and size; before magic is unlocked, use the authored
  Main_00 placement. Support normal and doubled magic capacity. Neither the
  displayed magic amount nor save data is changed.
- Appearance diagnostics default to **off**. Existing saved preferences may
  remain on; disable **Appearance diagnostics** to hide the panel.

The new aura and gauge have passed synthetic-memory tests and USA/EU translation
and compilation. **Their appearance still needs testing in the game**; these
checks cannot verify the user's actual layout resources or GPU shader output.
The package contains original code only, with no game artwork/models/binaries.

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
native update path.

Only the current player's shell at Link+5700 joins the existing black palette
and `_a0` texture-view path. Its FMAT render state uses source-alpha and inverse
source-alpha colour blending while active. Restoration checks actor, model,
material array and resource identities and restores owned bits while preserving
unrelated changes. Other actors and held non-sword items are excluded.

### Magic gauge artwork

Read the resident **MagicBar_00** SARC/BFLYT with bounded offsets. Find the native
P_MagicBase_00/01 pictures and their authored green fill picture using material
and vertex colours, then query their authored geometry and load the referenced
textures with the SDK's `WWHD_HUD_GAME` service. Frame material colours and UVs
are retained; the fill uses the original coverage texture tinted red. Partial
fill crops its UVs and width, rather than stretching the texture.

The native HD magic controller (02675414) supplies the live base pane position,
size and origin. On full-state load, discard invalid image handles and renew
artwork and positioning. Missing/unsupported resources are skipped and retried;
there is no replacement drawn rectangle. No layout files are modified, and
Nintendo artwork is never included in this repository or package.

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
`DL 0.1.12 ON/OFF`; `Magic` counts loaded frame/fill images (up to four),
`Pos` is 1 for native or 2 for authored placement, and `Aura` reports the shell
model being present. Body material/albedo/eye counts, binding retention and late
repair counters retain their existing meanings. Diagnostics do not prove GPU
output. No Share log or game-file upload is required.

Automated checks cover gameplay/timing, body/eye/equipment texture restoration,
nested native refresh, animated face replacements, malformed layout bounds,
red fill and UV cropping, native placement, handle reuse/state-load invalidation,
visual-only aura frames, genuine armor coexistence and blend restoration.

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
