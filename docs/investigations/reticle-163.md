# Issue 163: aiming-marker interpolation

Implemented on local branch `reticle-163`, based on GitHub `devel`
`30f6c46ad67c2d9c00c329be8bb319ccff5a018a`. Clone:
`/Users/lschaupp/projects/zelda_wwhd-work/wwhd-cx-reticle-163`.
Nothing was pushed. The final commit SHA is provided in the handoff; use
`git rev-parse reticle-163` to resolve it locally.

## Cause and drawing paths

Read-only references were `../wwhd-actors/wwhd_src` and
`../wwhd-actors/tww`; the separately named `decomp-readable` was absent.

The red aiming dot is `daPy_sightPacket_c`, a world point projected into a
screen-space packet, not a layout pane or an interpolated J3D model:

- `023D813C`, `setSight` (`d_a_player_main_01.cpp`), projects `mPos`, builds
  a translated/scaled/rotated matrix and queues the translucent 2D packet.
  Link draw `023D9820` calls it when the sight draw flag is set.
- `02414834`, packet `draw` (`d_a_player_main_04b.cpp`), uses
  `default_sight`, identity view, screen projection and the packet matrix.
- `023EBCB0`, `checkSightLine` (`d_a_player_main_02.cpp`), constructs the
  logic-camera ray and clips it against world collision. Boomerang, grapple
  and hookshot procedures in `d_a_player_main_07.cpp` populate the world point.
- Bow first-person is a control: its subject procedure does not use the shared
  red packet. Actual aiming captures had zero shared-packet trace rows.
- Boomerang lock-on uses `daBoomerang_sightPacket_c::setSight`, `020CBB70`,
  projecting up to five actors' eye positions into separate matrices.
  Actor draw is `020CBE34`, packet draw `020CDBFC` (`d_a_boomerang.cpp`).
- `025F1018`, `mDoLib_projectViewport`, wraps projection `025F0C48`;
  `02524628` supplies the active view/projection matrix.

The trace establishes that camera draw precedes sight projection in the tested
path. The camera changes every interpolated draw, while the sight world point
remains at the next logic endpoint. Projecting that endpoint through successive
camera positions produces a repeating sawtooth. The original stale-camera-order
hypothesis was not confirmed for this path.

For boomerang logic step 550 at 120 FPS, projection X coordinates were:

| Draw fraction | Before | After |
| --- | ---: | ---: |
| 0.25 | 954.143555 | 936.333252 |
| 0.50 | 948.069458 | 936.200439 |
| 0.75 | 942.245483 | 936.309143 |
| 1.00 | 936.358154 | 936.358154 |

The original world endpoint is constant within this step. The corrected point
moves with the camera fraction, retaining the exact endpoint at fraction 1.
These are `mDoLib_projectViewport` coordinates, not captured-image pixels.

## Fix

The three scoped hooks cover the shared sight setter, boomerang slot setter,
and their viewport projection. Retain consecutive logic world positions per
packet/slot and interpolate them at the camera's actual drawn fraction before
calling the original projection. Immediately restore the source's raw words.
Other callers of the projection function go directly to the original.

History rejects missing steps and changed target identity (point address plus
actor process ID), resets on state load and fast-forward transitions, snaps
with camera cuts, and bypasses actors already running at 60 Hz. Normal 30 FPS
without tracing goes directly to the original setters. `WWHD_RETICLE_FIX=0`
disables the default-enabled fix; `WWHD_RETICLE_TRACE=1` logs scoped world,
projected screen and interpolated world coordinates.

True60 needs two additional measures because actor rollback restores render
fields before queued packets are painted. Capture the projection matrix after
the current camera draw, temporarily use it only for scoped marker projection,
and restore it immediately. Preserve only the marker's current-pass render
matrix and boomerang slot flags after actor rollback. Gameplay fields remain
restored. Tests check that stale pass matrices are not replayed and all five
slot flags survive. This extends the pass-lifecycle handling illustrated by
issue 126's no-draw fix (`5eb9a5ee`); it does not change general HUD projection.

## Measurements

All runs used a copied slot-3 state and save, hidden windows, no audio and
scripted X aiming plus right-stick camera turning. Item IDs were boomerang
`2D`, grapple `25`, bow `27`, hookshot `2F`. The selected inventory entry and
cached selected-item byte were both set in the copied state. Script origin is
30 logic steps after state load; aim is held 0.4–3.8 s, turn 1.5–3.5 s.
These are correctness measurements, not performance benchmarks. The user
explicitly waived the load limit for this work; fresh uptime was still recorded
and builds used `-j2`.

The tracked [numeric evidence](reticle-163-results.json) contains projection
statistics, eight consecutive mid-turn TV-frame component measurements per
capture, and hashes for all TV/DRC captures. Logs and the local harness remain
private under `local-reticle/`. The JSON deliberately excludes superseded
true60 prototypes. Projection traces and asynchronous PNG indices are measured
separately; they are not assumed to identify the same rendered frame.

Maximum consecutive red-dot centre movement in eight 1280×720 mid-turn TV
captures (largest visually confirmed red component):

| Renderer | Item | FPS | Before, pixels | After, pixels |
| --- | --- | ---: | ---: | ---: |
| Metal | Boomerang | 60 | 8.168 | 0.247 |
| Metal | Boomerang | 120 | 12.201 | 0.203 |
| Metal | Grapple | 60 | 10.486 | 0.501 |
| Metal | Grapple | 120 | 15.689 | 0.241 |
| Vulkan | Boomerang | 60 | 8.188 | 0.227 |
| Vulkan | Boomerang | 120 | 12.231 | 0.219 |
| Vulkan | Grapple | 60 | 10.482 | 0.488 |
| Vulkan | Grapple | 120 | 15.703 | 0.239 |

Steady-turn projected maximum jumps (origin +55 through +94 logic steps,
1.833–3.133 s) were 12.084→0.254 and 18.053→0.241 for boomerang at 60/120;
grapple was 16.003→1.094 and 24.080→0.956. Metal and Vulkan projection results
agree. This window excludes turn onset; it is not the whole-turn maximum.

Across the entire scripted turn (origin +45 through +104), boomerang maximum
jumps were 15.366→14.155 at 60 and 21.885→7.097 at 120; grapple was
16.431→14.244 and 24.080→7.503. At turn onset the 30 Hz endpoint itself moves
about 28 projection units, which the fix spreads across two/four draws. At
120 FPS the whole-turn RMS drops 10.213→0.921 for boomerang and
13.048→0.959 for grapple; p95 drops 17.883→0.217 and 23.290→0.685.
The JSON retains full-turn and steady-turn statistics rather than hiding onset.

Hookshot was also captured at 30/60/120; steady-turn after maxima were about
1.077/1.038 at 60/120. Bow was verified as actual first-person aiming, with no
shared marker to alter. Keep game speed was tested off and on for both reported
items at 60/120; adaptive Vulkan 120 runs contained fractions 0.25, 0.333, 0.5,
0.667, 0.75 and 1, exercising varying draw counts rather than assuming four.
Default true60 groups were tested on Metal for all four items and on Vulkan
for boomerang/grapple. The final true60 runs use `*-true60-v2` or `final-*`
evidence; the earlier non-v2 boomerang/grapple/hookshot prototypes are excluded.

All five boomerang lock-on slots were exercised at 60/120 and true60. The opt-in
`WWHD_RETICLE_TARGET_FIXTURE=1` selects nearby live actors and calls original
`setLockActor`; it bypasses acquisition only, then uses real marker projection
and drawing. Two markers were visible, with remaining targets offscreen. This
is not a claim of naturally acquired five-target gameplay. The red-component
extractor measures the aiming dot, not the yellow target texture; target coverage
comes from scoped traces, verified captures and actual-hook tests.

## Invariants, tests and limitations

The 30 FPS direct-original path is covered by actual-hook tests, including raw
source restoration, signed zero, CPU clobbers and unrelated guest/HUD bytes.
Whole-image RGBA comparisons were 16/16 equal for bow/hookshot, grapple repeat,
and boomerang/grapple snapshot controls. Some other pairs were 15/16, including
the final no-trace boomerang snapshot pair. Two fix-disabled baseline runs also
differed on a frame. Therefore whole-run image determinism is not established;
we do not attribute those isolated differences to a specific renderer race.
Normal 30 FPS executes the original path without interpolation writes.

At interpolated rates, examined exact-endpoint frames were identical, while a
boomerang blended-frame difference was confined to the dot/glow rectangle
(609,345)–(647,375). DRC captures were unchanged. Guest-memory tests cover
unrelated HUD isolation; this does not assert exhaustive image equality across
every nondeterministic capture.

Release BOTH (Metal/Vulkan) build succeeded. Final checks:

```
cmake --build build/reticle --target interp_reticle_test interp_reticle_hooks_test -j2
ctest --test-dir build/reticle -R '^interp_(reticle|reticle_hooks|pacing|nodraw)$' --output-on-failure
python3 -m unittest discover -s tools/true60 -p test_reticle_measure.py -v
git diff --check
```

Four CTests and four Python tests pass. Coverage includes perspective projection
with a translating camera at 1/2/4/8 passes, adaptive pass counts, camera/history
snap and reset, five slots, actor reuse, original-call behavior, scoped true60
projection restoration, and marker-matrix preservation after simulated rollback.
Existing pacing/no-draw regressions remain green.

Testing used Apple M3 Max, Metal and Vulkan through MoltenVK (Vulkan 1.3.357,
driver 0.2.2210). Native Windows Vulkan and a physical 180 Hz scanout were not
tested. Captures reproduce temporal position alternation, but did **not** show
two distinct aiming dots within one GPU image. The reported within-frame doubling
therefore remains unverified locally; no display/video-compositing explanation
is asserted. Native reporter confirmation is still needed for that symptom.

No README/CHANGELOG edits, pushes, original saves/states changes, or builds/runs
in the main clone. Temporary PNGs are deleted after extracting measurements and
hashes; private output remains below 1 GB.

## Suggested lead reply

“We found that the aiming point stayed at its 30 Hz world position while the
camera interpolated between frames. The fix interpolates that point with the
camera and preserves the marker render data through true60 rollback. Headless
Metal/Vulkan tests greatly reduce the per-frame dot jumps at 60/120 FPS, with
30 FPS using the original path. Please retest on Windows, especially the
reported two-dot appearance on a 180 Hz display.”
