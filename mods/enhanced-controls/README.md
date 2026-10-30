# Enhanced Controls (experimental guest mod)

Standalone `.wwhdmod` for official ZeldaWWHDRecomp main, initially based on v0.5.0
(`264f53ee0eca9d52d39f6ccf1a890eecec018e6e`). Requires mod manager **1.6.0** and
code-mod support. This branch adds a package and CI only; it does not modify the
Android app, runtime, renderer or SDK. The separate `enhanced-on-main` branch is
the seven-feature runtime port, not a prerequisite.

## Included

- Four separately switchable touch macro buttons: **Jump**, **Slash**, **Spin**,
  **Dodge**. Draw your sword first for the three attacks. Jump/slash/dodge send
  a short ZL targeting hold, then A/B. **Spin** rotates the left stick through a
  full circle in eight 45-degree steps (about 267 ms), taps B for about 67 ms,
  then releases. It temporarily overrides the left stick and returns control
  after about 367 ms. It uses the game's quick-spin input rather than charging
  an attack; keep the physical attack button released so B has a rising edge.
  A dodge goes sideways with left/right stick input, otherwise backwards.
  Timing follows game logic time, including true-60 mode, rather than rendered
  frame count. A new macro replaces the previous one; real buttons are preserved.
- **Camera-distance pinch**: 50–200%, with a native camera collision query for
  zooming out. This changes eye-to-target distance, not FOV. Fingers apart zoom
  in. The distance is session state; changing the distance option resets it.
- **Double-tap recenter** on the right-hand gesture pad: a short ZL pulse. As
  with the game's ZL button, a nearby target can be locked onto instead.
- Four combat icons from the Enhanced fork replace the text labels. **Combat
  icons** switches back to text. Missing/failed images fall back to text per
  button. Icons retain their proportions, use the existing touch areas and
  reload after a save-state/HUD reset.
- Live toggles and numeric position/size settings in the mod's settings page.

The visible rectangle on the upper right is a **dedicated gesture pad**, not
an invisible listener over the native right stick. Start a pinch or double tap
there; normal camera dragging remains outside it. Mouse wheel over a claimed
area also changes distance. The mod claims only its displayed rectangles;
claimed touches cannot simultaneously operate built-in touch controls. Move the
macro row / gesture pad in the mod options if they overlap your touch layout.
Positions use 1280×720 layout units centred on the picture, positive Y upwards.
The row moves as a group; there is no native touch-editor integration or drag editor.

Gestures and macros are cancelled during events, messages, menus, stage changes,
first-person aiming and unsupported movement such as swimming, climbing, sailing
and conducting. Touch cancellation, layout changes and detected state restores
also cancel pending input. Releasing a macro button normally lets its short
sequence finish. Holding a physical button still takes precedence over the
macro's release; the mod does not forcibly release the player's inputs.

## Install on Android

1. Download the `enhanced-controls` artifact from the successful **Enhanced
   Controls mod** Actions run. Extract the outer GitHub artifact ZIP to obtain
   `enhanced-controls-0.1.2.wwhdmod`; do **not** extract that file.
2. Enable **Code mods** in the app's setup or **Settings → Mods → More →
   Code-mod support**. If newly enabled, let the app rebuild the game code.
3. **Settings → Mods → More → Install from a file**: choose the `.wwhdmod`.
4. Review and enable it. The manager marks this package experimental and handles
   its save backup. Close and reopen the app when asked.
5. Configure it under **Mods → Community → Enhanced Controls**. Included features
   default on once the package itself is enabled; each can be disabled separately.

No special APK is needed. Do not enable the duplicate controls/zoom from the
runtime PR at the same time. Mods that claim overlapping areas, change input or
adjust the same camera can conflict. The package includes the four combat icons from the Enhanced fork (see
`assets/NOTICE.md` for source attribution). No game files or shaders are included.

## Not included

Touch haptics, CPU affinity policies, hardware performance telemetry/HUD and
Lossless Scaling frame generation still need host APIs/runtime integration.
This package does not pretend to implement those four runtime features.

## Build

From this repository checkout, with Python 3, Clang with a PowerPC backend and
LLD installed (Ubuntu: `sudo apt-get install clang lld`):

```sh
python3 mods/enhanced-controls/build.py
python3 mods/enhanced-controls/tests/run_tests.py
```

Output: `build/enhanced-controls/enhanced-controls-0.1.2.wwhdmod` and
`SHA256SUMS.txt`. `WWHD_PPC_CLANG`, `WWHD_PPC_LLD` and `CC` can select compilers.
The build checks the real upstream package reader and PPC-to-C translator for
both USA and EU. Tests exercise the pure policy and the translated module with
synthetic guest memory/services, without game files. CI runs the same commands
on pushes to `enhanced-mod` and relevant pull requests, then uploads the package.
`workflow_dispatch` is also declared (GitHub exposes the manual button once the
workflow exists on the default branch).

## Camera integration and validation limits

The return hook on the verified `store(camera_process_class*)` (`024F8430`)
adjusts the freshly copied process eye before `view_setup`. The camera body's
simulation is left alone, so zoom does not compound each frame. The host sees
the adjusted view through its usual camera interpolation/culling path. Near
walls, a bounded line query (`024FCBE8`) limits outward movement. Special views
are excluded. These offsets and calls come from the upstream SDK and runtime;
fixed global addresses use the SDK's USA/EU relocation mechanism.

This is an initial **experimental** package. Translation, synthetic tests and
compilation do not prove in-game behavior. Android device playtesting is still
required for collision geometry, both regions, menus, camera transitions,
interpolation/true-60, input combinations and coexistence with other mods.
It does not guarantee forward compatibility with changes to the game's layouts
or upstream APIs. No renderer/frame-generation changes are made here.

## Licence and attribution

MPL-2.0, see `LICENSE` in the package and repository. Macro timings and the
camera-distance/collision approach are adapted from SSunnKing's
`ZeldaWWHDRecompAndroid---ENHANCED` and the `enhanced-on-main` port. The guest
adapter and package build are implemented against the official public SDK.
The archive includes `src/mod.c` and `src/core.h`; build tools and tests are in
`mods/enhanced-controls` on this branch. Public game names/offsets derive from
ZeldaWWHDDecomp/wwhd (CC0), as documented by the upstream generated SDK.
