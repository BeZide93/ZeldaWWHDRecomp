# Enhanced Controls (experimental guest mod)

Standalone `.wwhdmod` for official ZeldaWWHDRecomp main, initially based on v0.5.0
(`264f53ee0eca9d52d39f6ccf1a890eecec018e6e`). Requires mod manager **1.6.0** and
code-mod support. This branch adds a package and CI only; it does not modify the
Android app, runtime, renderer or SDK. The separate `enhanced-on-main` branch is
the seven-feature runtime port, not a prerequisite.

## Included

- Four separately switchable touch macro buttons: **Jump**, **Slash**, **Spin**,
  **Dodge**. Draw your sword first for the three attacks. Jump/slash/dodge send
  a short ZL targeting hold, then A/B. **Spin** directly starts the native quick-spin attack on
  Link's next game update, once per tap. It adds no stick rotation or B press
  and keeps physical input intact. Animation, effects, hit detection and damage
  remain native. It only starts from normal standing, walking or targeting
  movement with a drawn sword; guarding, equipping, curses, item catches,
  carrying, blocked item input and all other action states refuse the request.
  Rejected requests are discarded, never replayed after an animation ends.
  A dodge goes sideways with left/right stick input, otherwise backwards.
  Timing follows game logic time, including true-60 mode, rather than rendered
  frame count. A new macro replaces the previous one; real buttons are preserved.
- Four combat icons from the Enhanced fork, with per-button icon/text selection
  in the editor. Missing images fall back to text. Icons retain their proportions
  and reload after a save-state/HUD reset.
- A **macro-only touch editor**, opened by the **Edit** touch button. The mod's
  settings contain only **Show editor button**, **horizontal position** and
  **vertical position**.

The mod claims only its displayed button rectangles during gameplay; the Edit
launcher takes priority over macros. Positions use 1280×720 layout units centred
on the picture, positive Y upwards.

Version **0.3.0** removes the gesture pad, pinch zoom and double-tap recenter,
including the camera hook and collision query. Existing saved editor layouts and
options remain compatible. The HUD visibility fix from 0.2.1 remains included.

## Macro editor

1. Tap **Edit** (top centre by default). Choose **Jump**, **Slash**, **Spin** or
   **Dodge** in the top row, or select a button directly, then drag it.
2. **Smaller/Larger** changes the selected button in 5% steps (60–180%).
   **Opacity** cycles 100/80/60/40/20%. **Shape** selects a circle/square frame;
   **Fill** changes the frame background. **Style** switches icon/text.
   The original icons remain circular artwork inside the chosen frame.
3. **Visible** enables/disables that button. Hidden buttons remain dimly visible
   in the editor and selectable using the top tabs, even when all four are hidden.
4. **Grid** enables a 20-unit snap grid. **Reset all** resets the draft layout.
   **Save / Done** saves and closes; **Cancel** discards the draft.

Changes update each button's hit area as well as its appearance. Drag positions
are bounded to the 1280×720 safe area beneath the editor toolbar. The editor
previews hidden/very faint buttons more brightly to keep them editable.
Saved layouts live in this mod's own data directory as two alternating,
versioned/checksummed files (`macro-layout-a.bin`, `macro-layout-b.bin`). A short
or failed write keeps the editor open with an error; retry Save or use Cancel.
The previous complete generation remains available. Layouts persist across app
restarts and game saves; loading a save state reloads the saved layout and closes
an unfinished editor. They are shared by this mod's sessions, not per Quest Log.
Version 0.2.0 starts with the default row when no editor layout exists; old
numeric row settings are replaced by this per-button layout.

Editing cancels pending macros and blocks controller buttons/sticks until the
editor closes and inputs return to neutral. Menus, player changes and lifecycle
interruptions discard the draft. **The game world keeps running**: edit in a safe
place. This is a guest-mod overlay, not the native Android editor. Native Android
buttons can intercept touches before a mod; temporarily hide those controls with
the app's touch-controls toggle if they cover the editor or its buttons. Avoid
placing macro buttons beneath native controls when using both layouts.

Macros are cancelled during events, messages, menus, stage changes,
first-person aiming and unsupported movement such as swimming, climbing, sailing
and conducting. Touch cancellation, layout changes and detected state restores
also cancel pending input. Releasing a macro button normally lets its short
sequence finish. Holding a physical button still takes precedence over the
macro's release; the mod does not forcibly release the player's inputs.

## Install on Android

1. Download the `enhanced-controls` artifact from the successful **Enhanced
   Controls mod** Actions run. Extract the outer GitHub artifact ZIP to obtain
   `enhanced-controls-0.3.0.wwhdmod`; do **not** extract that file.
2. Enable **Code mods** in the app's setup or **Settings → Mods → More →
   Code-mod support**. If newly enabled, let the app rebuild the game code.
3. **Settings → Mods → More → Install from a file**: choose the `.wwhdmod`.
4. Review and enable it. The manager marks this package experimental and handles
   its save backup. Close and reopen the app when asked.
5. Under **Mods → Community → Enhanced Controls**, set the Edit button's position
   and visibility. Use **Edit** in-game for all four macro buttons, including
   their visibility.

No special APK is needed. Do not enable the duplicate controls from the
runtime PR at the same time. Mods that claim overlapping areas or change input can conflict. The package includes the four combat icons from the Enhanced fork (see
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

Output: `build/enhanced-controls/enhanced-controls-0.3.0.wwhdmod` and
`SHA256SUMS.txt`. `WWHD_PPC_CLANG`, `WWHD_PPC_LLD`, `CC` and `CXX` can select compilers.
The build checks the real upstream package reader and PPC-to-C translator for
both USA and EU. Tests exercise the pure policy and the translated module with
synthetic guest memory/services, without game files. Emitted HUD commands also
pass through upstream's actual HUD store: geometry, texture handles and complete
list publication are checked for normal controls, text fallback and the editor.
This catches invalid commands that would otherwise hide the entire overlay.
CI runs the same commands
on pushes to `enhanced-mod` and relevant pull requests, then uploads the package.
`workflow_dispatch` is also declared (GitHub exposes the manual button once the
workflow exists on the default branch).

## Validation limits

This is an initial **experimental** package. Translation, synthetic tests and
compilation do not prove in-game behavior. Android device playtesting is still
required for both regions, menus, true-60, input combinations and coexistence with other mods.
It does not guarantee forward compatibility with changes to the game's layouts
or upstream APIs. No renderer/frame-generation changes are made here.

## Licence and attribution

The spin call is `daPy_lk_c::procCutTurn_init(link, 1)` (`023E6380`), the
same initializer used by the native quick-spin input path. Its signature,
argument and state checks were checked against public WWHD decomp commit
`2c1deae356b6299394743a28e65fe632b032ae5c`, `d_a_player_main_02.cpp`,
`d_a_player_main_03.cpp` and `d_a_player_main.cpp`. The request is consumed
from the existing Link Execute hook and uses SDK relocations for USA/EU.

MPL-2.0, see `LICENSE` in the package and repository. Macro timings are adapted from SSunnKing's
`ZeldaWWHDRecompAndroid---ENHANCED` and the `enhanced-on-main` port. The guest
adapter and package build are implemented against the official public SDK.
The archive includes `src/mod.c`, `src/core.h` and `src/editor.h`; build tools and tests are in
`mods/enhanced-controls` on this branch. Public game names/offsets derive from
ZeldaWWHDDecomp/wwhd (CC0), as documented by the upstream generated SDK.
