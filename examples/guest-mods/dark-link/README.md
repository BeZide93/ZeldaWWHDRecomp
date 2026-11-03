# Dark Link (prototype 0.1.25 — native gauge comparison)

Standalone code mod for ZeldaWWHDRecomp: sword hits charge a transformation with
black body/equipment, red eyes, a black native Magic Armor aura and boosted sword
damage. Includes the original Dark Link medallion and shared touch editor.

## Changed in 0.1.25

This is the first native gauge rendering proof requested after 0.1.24 still
showed a flat bar. It deliberately draws a **second GREEN, FULL magic gauge**
below the ordinary one. The test gauge does **not** show Dark Link charge or
remaining duration yet. Those gameplay values and transformation controls still
work; red recolouring and fill integration follow after the native design is
confirmed in-game. Start with a save where the normal magic gauge is visible.

The old texture reconstruction is removed: no BFLIM decoding, picture/frame
selection, material approximation, PNG generation or overlay gauge commands.
The game constructs a fresh MagicBar_00 layout, binds its native animations and
draws all of its panes/materials itself. No game assets are uploaded or packaged.

## Install and compare

1. Replace the installed Dark Link mod with `dark-link-0.1.25.wwhdmod`, finish its
   code-mod build, then fully restart the game. The native init hook must see the
   HUD being created; loading this test into an already-created HUD is unsupported.
2. Enable **Show native gauge test**. On a save with normal magic, compare the
   second green bar's frame, highlights, segments and width with the original.
   The copy is always full, including when Dark Link is inactive or out of charge.
3. Check normal and doubled magic, different heart-row layouts, menus, a stage
   change, toggle off/on and a full-state save/reload made with this version.
   No app rebuild or Enhanced Controls update is needed.

Use **L3 + R3** or the Dark Link touch button to transform/cancel. Defaults:
100 charge, 5 per damaging sword hit, 20 seconds, 200% sword damage. **Free
transformation** allows immediate transformation with unlimited duration.
Menus/cutscenes pause the timer. Death, stage/player changes and disabling clear
the transformation. No normal-save magic/rupee values are written.

## Native gauge implementation

The source is the public CC0 `ZeldaWWHDDecomp/wwhd` at
`b2811a3ce838d1f875bc8e68536c1950af5d7ffc`, particularly
`hd_screen_magic_bar.cpp`, `hd_parts.cpp`, `hd_parts_layout.cpp`,
`hd_ui_020021FC.cpp` and `hd_ui_02004A3C.cpp`.

- After native MagicBar init (02675294), construct a separate 0x8C controller on
  the UI heap and initialize it with a null parts descriptor. The new layout must
  have a distinct root, distinct layout object and no parent; aliases are rejected.
  This is a native construction, not a shallow copy of intrusive UI structures.
- The native loader binds style animations. For missing progress slots 0/1, a
  bounded SARC/BFLAN metadata reader resolves the native G_Bar_00 tag with the
  authored 16/32-frame duration and binds it through 02004E04. It reads no texture
  pixels or animation curves. InAll is set to its authored end; Flash, ordinary
  Link ColorChange and ScaleChange are set to zero. Native animation evaluation
  and rendering handle the complete artwork. Ambiguous tags are rejected.
- After Main_00 layout calculation, set the copy to full 16/32 capacity and use
  the original root's calculated axis-aligned transform, offset down by the bar
  height plus eight layout units. Calculate the copy through native 026F8CD0.
  Invalid or unsupported transforms suppress only the additional bar.
- After the original MagicBar drawLayout call (026F8D08), draw the prepared copy
  with that owner's same DrawInfo. This also covers an embedded original layout
  whose own drawLayout skips submission because its parent already drew it.
  No native GPU work runs from the mod overlay callback; that remains for buttons
  and optional diagnostics. Up to four independent native HUD owners are tracked.
- Do not execute the copied controller's ordinary state machine: it would read
  global magic, warning and visibility state. The source object remains untouched.
  Finalize and destroy each copy before its source owner's resources are released;
  constructor recursion, duplicate teardown and stale/recycled identities are
  guarded. Full states restore native allocations and mod records together, so a
  host HUD-image epoch change must not discard the native copies.

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

Optional **Appearance diagnostics** shows `DL 0.1.25`, `Magic` (successful native
constructions), `Pos` (prepared geometry), and `Native green/full Draw` (submitted
native copy calls). `Fill` still reports gameplay charge/time, not this test's
constant-full bar. `State` identifies the most recent construction failure:
0 none, 1 owner capacity, 2 UI heap, 3 allocation, 4 independent layout/root,
5 animation metadata, 6 native animation bindings. No new file logging is added.

Automated tests cover native allocation/init arguments, independent owners,
progress binding names, original-object/save-byte preservation, native draw
context, recursion, pause/menu/toggle gates, cleanup ordering, allocation/load
failure, rejected shared roots and malformed metadata, host-image epoch changes,
and invalid matrices. Existing body/eyes/equipment, aura, gameplay and D-pad
regressions remain. Runtime HUD validation checks the surviving controls with no
overlay gauge image. The shared-editor test exercises both mod load orders,
separate image ownership and save/reload/cancel.

These tests use synthetic memory and mocked native calls. USA/EU translation and
compilation verify the mod ABI, **not** Nintendo's layout loader, GPU rendering
or final on-device appearance. Actual game resources/executable are unavailable
in this environment. This package is an explicit in-game rendering comparison.

## Build

```sh
python examples/guest-mods/dark-link/build.py --cc "python -m ziglang cc" --host-cc "python -m ziglang cc"
```

Output: `build/dark-link/dark-link.wwhdmod`. The package contains the manifest,
compiled mod, README and already-approved original Dark Link icon/notice.
Generated packages are not committed. Mod-only edits run package CI; platform
app-build filters remain in place.

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

The confirmed calculation-time Main_00 shortcut spacing is retained. It now
uses the prepared native gauge geometry, without emitting an overlay bar.

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
