# Enemy lock-on assistance — implementation and validation (#192)

## Current behavior

The optional **Extended enemy lock-on** built-in mod is disabled by default.
Its range multiplier is **1x–3x**, default **1x**. It changes the native enemy
channel's horizontal acquisition/release limits, not shared distance tables,
actual-distance ranking, height/angle rules or NPC interaction distances.

With cycling enabled (the default), **R3 while locked onto an enemy** rebuilds
and sorts the native eligible-target list and chooses the next live enemy,
wrapping around and skipping NPCs, invalid targets and the current enemy. The
native list holds at most **eight total candidates**; this does not scan every
enemy in a room or bypass the game's eligibility rules. No alternate enemy
means no target change. The native state machines still own target publication,
release and Link/camera updates. R3 outside an enemy lock keeps its native use.

A simultaneous native targeting-button edge has priority. Overlay, native pause,
event and scene-transition gates reject queued cycles; focus loss and both full
and portable save/load boundaries clear and disarm them until R3 is released.
No actor pointers are retained across frames.

See [mod-manager.md](mod-manager.md) for preferences, profiles and environment overrides.

## Automated checks performed

- Full Linux/Clang build with freshly generated local USA game code: passed.
- Complete CTest suite: **188/188 passed**. This includes five lock-on-specific
  tests for the real hook/state-machine implementation and startup settings,
  alongside manager/profile persistence and focus-reset coverage.
- Lock-on runtime fixtures using both the USA identity map and the shipped EU
  code/data map: passed. These exercise mapped guest helper calls; they are not
  a native EU gameplay run.
- Lock-on runtime fixture with UndefinedBehaviorSanitizer: passed.
- Recompiler Python tests: **22 passed**, including regional hook mapping checks.
- Vulkan renderer smoke on the actual local device: passed.
- Six native-game boot/input smoke cases with private copied saves: passed
  (assistance off/on, each at 30 fps, interpolated 60 fps and true60). Original
  save files were verified unchanged.

The runtime fixtures model native list building, PID resolution and button
judgement. They verify round-robin traversal through all eight slots, wraparound,
no-alternate restoration of both lists, missing/dead targets, invalid counts and
indices, register preservation, one cycle per press, native-button priority,
scene-object identity, expired input, pause/event/transition gates and resets.
They do **not** prove how the feature feels or behaves in real combat.

Focused checks after building the test targets:

```sh
ctest --test-dir build/lock-on-clang -R '^lock_on' --output-on-failure
```

Repeat the native-game smoke with a matching locally built binary/game and a
playable Quest Log 1. The output directory must not exist:

```sh
python3 runtime/tools/lock_on_smoke.py \
  /path/to/wwhd /path/to/game /path/to/save /path/to/fresh-smoke-work
```

The runner keeps copied saves, settings, caches and logs in its work directory
and writes scalar results to `results.json`. It defaults to Vulkan; use
`--renderer metal` for a separately available Metal build. These local outputs,
checkpoints, game assets and generated game code must not be committed or shared.

## Gameplay feedback and remaining coverage

The player tested the local playtest build with the mod enabled at **2x** range
and R3 cycling enabled, and reported that everything works correctly. The exact
combat setup, targeting modes and edge cases exercised were not recorded, so
this feedback does not establish exhaustive coverage of the matrix below.

Further regression coverage should use a private checkpoint containing several
live enemies:

- Compare disabled and 1x behavior with vanilla, then measure acquisition and
  release at 2x and 3x. Check vertical separation, behind-camera/out-of-angle
  enemies, obstacles and NPC/object interactions against native behavior.
- Hold the targeting button and cycle through at least three enemies repeatedly;
  confirm the visible lock marker, Link's facing/attacks and camera all follow the
  newly selected enemy without releasing the lock.
- Repeat in both native hold and toggle targeting modes, including simultaneous
  targeting/R3 presses, a single remaining enemy, disappearing/defeated enemies
  and a current target outside acquisition range but inside release range.
- Check pause/resume, overlay opening, focus loss, stage changes, full and
  portable saves/loads while R3 is held, and disabling/re-enabling the mod.
- Repeat the combat cases at 30 fps, interpolated 60 fps and true60; execute
  native EU gameplay and macOS/Windows builds separately. Those platform/gameplay
  combinations have not been validated in this implementation session.
