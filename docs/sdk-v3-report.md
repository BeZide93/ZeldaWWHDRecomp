# SDK v3 phase report — 2026-10-15

Branch: `sdk-v3-actors-events`. Base: fresh `origin/devel`,
`7d96afe239ed1e0513e791ed1ab40e3c4c9809c1`. The phase-1 checkpoint is the commit
containing this report; its full SHA is reported in the delivery message.
All work is local; nothing was pushed.

## Phase 1

Implemented the manifest, package validation and native runtime for placed
chests, signs and the Um1 resident. Unknown actors and unsafe parameter variants
fail validation. Mod-owned flags live in story-record kind 17. Selected-package
validation checks aggregate actor limits and flag-key collisions before startup
or enablement. Conditions reconcile quest progress, item ownership and day/night
in the current room, including asynchronous creation/deletion and state reloads.

Placed characters use native dialogue with ordered give/take/quest/flag/scene
actions. The native resident's retail event-script, item-show and photography
routes are blocked for mod instances; ordinary instances retain their original
behaviour. The sign uses native Read targeting. Chests redirect ordinary opened
checks/setters to mod flags and disable native switches. Nested vanilla chest
opening preserves the vanilla setter.

| Scene step | Implementation and validation | Game proof |
| --- | --- | --- |
| camera | Fixed, actor target, linear pan, bounded duration | Lighthouse shot, native letterbox and HUD hiding |
| say | Native message window, character reference, completion deadline | Both scene lines and resident/sign text |
| move | Placed resident only; native walk animation, movement, background correction, collision and matrix update | Synthetic native-call/movement contract; no dedicated rendered walking run |
| face | Placed resident only; point/actor target and native matrix refresh | Synthetic native-call contract |
| wait | 60 Hz timeline, two units per base 30 Hz service | State-machine/native contract |
| sound | Five validated native UI sound names | Native-call contract; runs were deliberately muted |
| fade | Native fade, duration normalized to the scene clock | Fade-out and fade-in captures |
| end | Ordered mod-owned actions, absent on skip/abort/failure | Natural finish; no-reward abort contract tests |

Triggers cover room/area entry, placed-character talk, quest steps and mod-chart
salvage completion. One scene owns control; requests use a bounded FIFO and are
deferred during native events. Room changes discard stale room/area requests.

### Event-system route

Used the permitted **fallback**: native compulsory-event admission, direct native
camera/message/fade calls, and native teardown. No stage event-list resource is
replaced. Runtime binary-list construction remains unproved with respect to
mutable staff/cut ownership and resource lifetime.

The native camera's trim calculation normally shrinks during event mode. An
opt-in hook applies the game's cinema height only to the scene-owned camera;
the native projection/scissor pipeline draws the bars. HUD, camera, trim and
control are restored only while the scene still owns their native event lease.
Plus/Start skips and is consumed; sticks are locked while dialogue A/B remains.

Actual game tests exposed two defects that are fixed and covered by regressions:
Um1 could enter its retail scripted dialogue at night, and immediate message
status 16→19 could wedge subsequent talks. Natural dialogue now waits for native
closed status 18. Non-natural exits reset the owned message through the verified
native message-core reset before releasing control. They do not depend on another
A press. State-captured lease snapshots also handle loading an old mid-scene
state after the host player has already finished.

### Tests and proof

- macOS Metal build: PASS; full CTest: **219/219 PASS**.
- Strict synthetic tests cover manifest errors, aggregate package limits/hash
  collisions, native actor spawning/conditions/deletion, chest/sign flags,
  ordinary actor behaviour, all scene effects, skip/abort/deadlines, repeated
  message cleanup, interruption and state-load cleanup.
- Linux x86-64 and Windows x86-64: actor/scene sources and generated stub game
  source compile to ELF/COFF objects with Zig. These are compile checks, not full
  cross-platform runtime links or game runs.
- Own headless game processes used `WWHD_NO_AUDIO`, `WWHD_NO_GAMEPAD`,
  `WWHD_NO_HOST_INPUT`, hidden windows and copied save inputs. Windfall spawns
  both actors, the sign reads its original text, the resident speaks the mod
  lines, and talking plays the lighthouse scene and restores normal control.
- Isolated skip during a message restores control without further A presses.
  Full-state testing restored the same mid-scene state five times; each load
  aborted its restored lease and returned control. An earlier run hit the
  existing full-state thread-parking guard and was safely refused; the successful
  repeated-load run is the evidence, not the refused attempt.
- Save safety: mod on/off produced **zero differing bytes across the entire
  0x12A0-byte native save-info block**. Native portable-save serialization also
  produced identical 0xA94-byte checked save data, 16-byte HD player, 4-byte HD
  status, 20-byte HD event and 220-byte HD map blocks. Both native checksum
  validations passed. Even time/checksum bytes matched in these runs.

Local evidence is outside the repository at
`/Users/lschaupp/projects/zelda_wwhd-work/sdk-v3-proof/evidence/`.
The parent proof folder contains the runner, logs and numeric save comparisons.
Only selected TV captures and recorded results are retained; raw actor dumps,
redundant captures and generated save states are removed. No game resources or
game-derived data files were added to the repository. One copied save input and
one reusable state location served the runs. Observed load stayed far below 55.

Preview documentation: [mod-sdk-v3.md](mod-sdk-v3.md).
Worked package: [Windfall manifest](../examples/guest-mods/sdk-v3-windfall/manifest.json).
Research: [sdk-v3-research-actors.md](sdk-v3-research-actors.md).

## Phase 2 — stopped at the research gate

No phase-2 implementation or phase-2 commit is claimed. The requested J3D model
pipeline is not usable through the native HD loader: that loader consumes Wii U
**BFRES plus per-model SHARCFB** resources. Legacy `.bmd` names are compatibility
aliases for native resources, not evidence that it accepts J3D BMD/BDL files.
The existing notebook converter is a static BFRES template patcher; it does not
provide custom skeletons, weights or animation clips.

Following the task's instruction to stop and report an infeasible phase-2 step,
the J3D-specific route stops here. A BFRES adaptation is proposed in the research
doc, with shader/material, skeleton, archive identity, generic actor lifecycle,
save-state and licensing gates. This is not a claim that custom HD actors are
impossible. It is a concrete format incompatibility and an unproved replacement
pipeline, which must not be presented as a working skinned actor converter.

Consequently custom models, generic actor registration, behaviour C callbacks,
scene choices/music/paths, custom-actor true/interpolated-60 captures and heap
measurements are **not delivered**. Conditional effort estimates for the adapted
route are in the research doc; no heap headroom or converter fidelity is invented.
