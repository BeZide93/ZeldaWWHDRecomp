# Issue 163: investigation and disabled candidate

Status: **unfinished; not a validated fix**. No push. Base is GitHub `devel`
`30f6c46ad67c2d9c00c329be8bb319ccff5a018a`. Clone:
`/Users/lschaupp/projects/zelda_wwhd-work/wwhd-cx-reticle-163`, branch `reticle-163`.

## Source findings

Read-only references: `../wwhd-actors/wwhd_src` and `../wwhd-actors/tww`.
The separately named `../decomp-readable` directory was absent.

- `d/actor/d_a_player_main_01.cpp`: `023D813C`,
  `daPy_sightPacket_c::setSight`, gets the viewport, projects `mPos`, builds a
  translated/scaled/rotated screen matrix, and enters the translucent 2D list.
  Link draw (`023D9820`) calls it when the sight's draw flag is set.
- `d/actor/d_a_player_main_04b.cpp`: `02414834`,
  `daPy_sightPacket_c::draw`, binds `default_sight`, an identity view and screen
  projection, then draws the packet matrix. It is a projected world marker,
  not a layout pane or a J3DModel passing through the model interpolation hook.
- `d/actor/d_a_player_main_02.cpp`: `023EBCB0`, `checkSightLine`, derives a
  ray from camera eye/centre and clips it against world collision.
- `d/actor/d_a_player_main_07.cpp`: `procBoomerangSubject`, `procRopeSubject`,
  and `setHookshotSight` update the shared sight world position during logic.
  The TWW references are `d_a_player_boomerang.inc`, `d_a_player_rope.inc`, and
  `d_a_player_hook.inc`. **Bow is a control requiring separate verification**:
  its subject procedure does not explicitly update this packet.
- `d/actor/d_a_boomerang.cpp`: `020CBB70`,
  `daBoomerang_sightPacket_c::setSight`, projects a target actor's eye position
  into one of five screen matrices. `020CBE34`, boomerang draw, submits the
  packet; `020CDBFC`, packet draw, consumes those matrices.
- `m_Do/m_Do_lib.cpp`: `025F1018`, `mDoLib_projectViewport`, temporarily
  installs a supplied viewport around `025F0C48`, `mDoLib_project`.
  `02524628`, `dComIfGd_getProjViewMtx`, selects the HD view/projection matrix.
- Runtime `interp.cpp`: `hook_024FFC40` blends camera inputs, runs camera draw,
  then restores the input fields; built matrices remain as drawn. The marker
  does not use the model history interpolation.
- Runtime `true60.cpp`, comments by `g_half_saves`, explicitly documents that
  actors can draw before their pass's camera draw, using the preceding view.
  Thus stale world coordinates are **not the only hypothesis**: hold-pass
  marker projection can also consume the preceding render pass's camera.
- `5eb9a5eed4b5d8c330f0e705f0202cde7de9b1e1` concerns replay of one-shot
  actor no-draw requests. It is useful pass-lifecycle precedent, but it does not
  establish the cause of this projection issue.

## Changes available for investigation

`WWHD_RETICLE_TRACE=1` logs each scoped projection's logic step, draw pass,
blend fraction, packet, slot, world point and screen point. Other projections
call the original function directly.

`WWHD_RETICLE_FIX=1` enables an **experimental, disabled-by-default candidate**:
retain each logic step's screen projection and blend consecutive screen
endpoints at the draw fraction. Hold-pass projections cannot overwrite the
endpoint with a position computed through the preceding render camera. The
scope covers only the shared aiming packet and five boomerang target slots.
Histories reset on state load and fast-forward transition, and reject skipped
steps and changed target identities. Processes currently executing at 60 Hz
bypass this candidate. With interpolation off, projection results are untouched.

This candidate still requires actual camera-cut, true60, pacing, target-marker,
HUD and 30-fps bit-identity validation. Do not enable it by default or describe
it as the fix based on the unit test alone.

## Validation and reproduction status

- Own Release/Metal build succeeded, `-j2`, fresh starting load 37.35.
  Candidate rebuilds also succeeded (latest starting load 41.78).
- `ctest --test-dir build/reticle -R '^interp_reticle$' --output-on-failure`:
  **1/1 passed**. It checks smooth per-pass screen positions at 30/60/120/240,
  conflicting hold-pass projections, first visibility, missing history,
  target-slot reuse, and reset. This is a synthetic regression, not game evidence.
- `git diff --check`: passed.
- Local harness: `local-reticle/run.py`; private copied state:
  `local-reticle/states/slot3.bin`; shader cache and logs also remain private.
  It copies saves per run, disables host input/audio/windows, checks fresh load
  <=45 and exact process names, and terminates only its own process. The updated
  harness waits for ten quiet seconds and stops its own game if another starts.
- Initial completed run `before-boom30`: state loaded and scenario ended, but
  boot A presses had opened the name-entry overlay before state load. There
  were **zero aiming projections**. Its screenshots are not valid reproduction
  evidence and were deleted; the log is retained. Boot presses were removed.
- Corrected attempt `before-boom30b`: another benchmark launched a game after
  our pre-launch check; our process was terminated before state load. No valid
  reticle measurement. The other process was left untouched.
- Further launches waited while the pacing task repeatedly ran `wwhd-v0212`,
  `wwhd-v0213`, and `wwhd-final`. The pending launcher was stopped before this
  handoff; no task-owned game or background launch is left running.

**Before/after numbers: unavailable.** There is no valid 30/60/120 aiming capture
and no evidence yet of two dots within one rendered frame. No claim of root
cause, bit-identical 30-fps output, or full test completion is made.

## Next measurements

Reserve an exclusive game-run window. Start with
`python3 local-reticle/run.py before-boom30c 30 2D`, then repeat at 60 and 120.
Item IDs are boomerang `2D`, grappling hook `25`, bow `27`, hookshot `2F`.
Use a fresh output name each time. The harness equips copied state memory,
holds X at 0.4-3.8 seconds, and turns the right stick at 1.5-3.5 seconds.
Verify aiming in `link.txt`, controller input in `pad.txt`, and the actual PNGs
before accepting projection traces. Compare with `--fix 1`, and test
`--paced 1`. Add true60 and acquired boomerang target fixtures. Measure both
projected coordinate jumps and actual rendered dot positions; delete temporary
frame dumps after extracting the evidence to remain under 1 GB.

Lead reply while unfinished: “We traced the shared aiming marker and boomerang
lock-on projection paths and are checking their timing against interpolated
camera draws. A fix is not validated yet; frame captures and measurements are
still pending.”
