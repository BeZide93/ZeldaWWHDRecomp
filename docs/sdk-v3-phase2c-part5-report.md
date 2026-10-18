# SDK v3 Phase 2C, Part 5: native scene preview

## Review status

This report describes the Part 5 implementation and the evidence available on
2026-10-18. The Part 5 commit SHA will be supplied after the reviewed changes are
committed; it cannot identify its own commit here.

The public selector is enabled for eligible authored `turn_camera` scenes.
Private native-selection environment variables are not required by that route.
The shipped harbor example passed public talk-trigger acceptance in RUN092/093,
with all `WWHD_TEST_NATIVE_*` variables absent: seven native entry calls, TURN
callbacks, camera return, natural completion and rearm, terminal exit 0, and
strict native/HD mod-on/off save parity. RUN090 demonstrated 60 genuine deferred attempts
followed by whole fallback; it did **not** demonstrate native entry.

The first full macOS run reported 385 passes and two failures out of 387 tests.
The saved-flags fixture and build-coverage audit failures were corrected. The
final current unfiltered macOS suite passed 388/388 with zero failures in 147.69 seconds,
after 39 affected targets rebuilt successfully; source hashes and the main binary
remained unchanged. The current retired-route production snapshot
passed 244/244 Linux/Windows compile-only checks. An independent rehash found no
changes in its 345 transitive dependencies. Cross compilation is not execution
on Linux or Windows.

## Previous parts and supported actors

The existing repository convention places the public contract in
[the SDK v3 guide](mod-sdk-v3.md) and worked content under
[examples/guest-mods](../examples/guest-mods/README.md). This report supplements
those contracts with implementation and acceptance boundaries.

| Part | Commit | Delivered scope |
| --- | --- | --- |
| 1 | `547356c92f49331c45e43f5a8e49170553f1dc97` | Native residents, props, switches, doors and fixed enemy variants. |
| 2 | `876c1d2f04cdc86a56d84e991a3481736142ada8` | Native combat integration for original SDK enemies. |
| 3 | `74bd32e1ce50452e81e6556e7d472c70b6ae5479` | Carryable original props and held-item models. |
| 4 | `d41efa014f2e93e79a4aee2674d86f20425634c1` | Native particles and textures on owned native props. |
| 5 | Pending reviewed commit | Authored native scenes, cancellation, repeated activation and guarded save/load integration. |

Part 5 does not broaden the actor allowlist:

| Actor family | Supported preview surface | Part 5 native scene use |
| --- | --- | --- |
| Signs | `sign`, `kanban`, `PROC_KANBAN`; mod-owned flags/dialogue. | Fallback scene interactions only. |
| Residents | `uo2`, `um1`, seated `ym1` through the documented resident aliases and fixed parameters. Ym1 does not support scene move/face. | Exactly two placed Uo2 residents, same package, stage and room. |
| Chests | Ordinary chest aliases with safe item parameters and mod-owned opened flags. | No native scene staff adapter. |
| Breakable props | `pot`, `barrel`, `crate`; owned broken flags and bounded mod drops. | No native scene staff adapter. |
| Switches/doors | Fixed yellow floor switch and rotating gate with same-mod linkage. | No retail event-script or transition adapter. |
| Native enemies | Ordinary green ChuChu, type-4 Bokoblin, ordinary Moblin and Keese (KI bat, not Morth). | No native scene enemy staff adapter. |
| Original mod models | Static/skinned models; the documented carry, held-item and texture contracts. Original combat enemies require the skinned combat contract. | Existing fallback capabilities remain available. |

The [native enemy contract](mod-sdk-v3.md#native-enemies-preview) fixes native
parameters, isolates owned rewards and prevents retail-cohort participation.
The [original combat contract](mod-sdk-v3.md#original-mod-enemies-combat-preview)
owns health/reactions, verifies Link attack/projectile lineage, and uses native
shield and damage-invulnerability behavior. Defeat rewards are transactional;
ordinary deletion, load cleanup or failed admission does not grant them. Part 5
adds no raw health, retail drop-table or save-bit authority.

## Native grammar and fallback boundary

The author supplies mandatory fallback `steps` and an optional explicit
`native: {"type": "turn_camera", ...}` alternative. The native recipe runs two
Uo2 TURN actions in parallel, followed by WAIT, with a native FIXEDFRM hold,
UNITRANS move and timekeeper. It uses its own timing rather than translating the
fallback timeline. The selected immutable descriptor binds the exact image,
actor targets, staff/cut grammar, pool offsets, completion mask and watchdog.
Legacy WAIT and typed TURN retain separate codec/storage contexts.

Native selection accepts only a linear fallback composed of fixed-position
`face`, fixed-XYZ `camera`, `wait` and one final `end`. Actor targets, camera
following/paths, dialogue, movement, animation, choices, audio, fades, particles,
branches and intermediate actions select the **whole fallback**. Unsupported
steps are not silently omitted or partly translated. The unsupported
`face.target_actor` variant passed actual public talk-trigger whole-fallback
acceptance in RUN086/087, including strict native/HD save parity.

A pre-entry transient control/manager/camera or Stage-census refusal can retain
the actual FIFO front for up to 60 scene-tick attempts. The request remains
unowned, duplicates remain suppressed and later requests cannot pass it. On the
60th deferred result the whole fallback is selected. Skip, abort or state load
can remove a deferred request without native cleanup or completion rewards.
Denied or throwing starts remain blocked; once staging/publication was attempted,
there is no fallback replay. Completion rewards require a one-use natural
completion token bound to the original activation and run lineage.

Every timer uses the game's 30 Hz logic clock, independently of render frames:
`wait_ticks`, each camera interval and `timekeeper_ticks` are bounded to 1–9000;
the combined hold/move interval is at most 9000; the timekeeper covers WAIT and
the camera interval. The watchdog is greater than the timekeeper and at most
18000. The shipped/probed example uses a 300-tick timekeeper and 600-tick watchdog;
9000 is an authoring limit, not a tested scene length. TURN's native timer is
conditional on angular convergence, so the independent host watchdog remains
necessary.

## Ownership and native lifetime

Eligibility and status enums confer no native authority. Actual startup issues
the declaration/activation identity. Entry then requires fresh actor identities,
Stage/resource proofs, an exact Session arena and journal, real callback coverage,
idle Tracker depths, and authenticated current CPU/thread/stack boundaries.
Readiness and bounded observations are repeated before mutation.
The public selection flag does not enable the separate low-level helper
`publication_enabled` descriptors; those remain disabled.

The native event image is retained in its exact owned arena while genuine native
OrderControl/CheckStart, SetData, actor CutInfo and event callbacks consume it.
The implementation does not replace the Stage event-list archive. Native
manager pools and retail flags are restored before the arena can be freed.
Hooks retain genuine original-call, TLS actor/action and one-use dispatch proofs;
matching bytes or a copied journal never authorize a callback.

Stage scans and restore permits are thread-affine. Physical quiescence is
acquired before new Stage pins. An actual known Session fence obtained through
nonblocking try-fence can already be held across that acquisition; no guest path
blocks waiting on it. Stage acquisition is nonblocking because a
parked guest may already hold its mutex. Cleanup drops Stage pins before physical
quiescence, and drops all physical freezes/pins before broad native EndControl.
Borrowed read-only quiescence is released before the actual thread serializer
reacquires its registry mutex, while the Session fence remains held. Real
serializer regression tests cover this ordering.

Cancellation has a separate proof chain from natural terminal completion. A
queued stop is handled at the genuine pending CameraRun boundary. It requires
fresh preclaim model graphs, real force-1 manager/camera completion and an
unwound, claimed outer-return receipt. Draining and durable Detaching precede
pose writes. Both residents and the camera are restored, native model refresh
runs under fresh authenticated guards, and genuine released observations precede
retirement. A partial native/host failure retains the claim and quarantines;
it does not fabricate final flags, invoke cleanup twice or free live objects.

The actual native post-talk control tuple can legitimately retain cached partner
PIDs while mode and order count are zero. Retail talkEnd clears actor commands
and event ID but does not clear those caches; a genuine new compulsory order
replaces them with null-partner sentinels. Readiness/Prepare now admit this passive
idle tuple while retaining full source/Stage/manager/camera and stability checks.
Every owned mode3, Running, terminal and cancellation predicate still requires
all four null-partner sentinels. No new cache-restoration writes or record fields
were added.

## Save/load and repeated scenes

An aggregate gate covers every declared Session plus the optional private
bootstrap Session. Active checkpoints originate only from genuine guarded
capture and commit. They retain immutable original Expected/program, preclaim
poses/model lineage, depth/clock origin and original frontend activation.
Saved bytes are matched against that host checkpoint; they cannot mint ownership.
Repeated loads fork fresh one-use attempts from the immutable checkpoint.

Loading first captures and revokes the actual current owner, independently of
the saved run's identity. A shared Stage restore group validates every child and
finishes the global Stage receipt once. Each Session keeps its actual fence;
only the exact current Session may borrow the aggregate owner's retained fence.
After memory/thread restoration, a fresh post-service ticket, provisional
Draining Tracker, original-baseline model clone, restored clock and full Restore
observation prepare a dormant Context. Final model/Tracker/front/frontend opens
are prevalidated, callback-free and nonthrowing under physical quiescence.

The frontend retains the original activation across replacement, reconciles the
specific load generation and preserves later user stops. Natural retirement
issues a source-validated one-use rearm certificate; a new generation creates a
fresh Tracker/model/Context instead of reviving the old one. Clean cancellation
uses a separate opaque Released origin, minted while the genuine claimed outer
receipt is live and installed only after claim release. Later consumption uses
fresh bounded quiescence/Stage/root checks and real readiness outside the freeze;
it never projects natural final flags or grants a completion reward. The genuine
Controller/Session fixture passes cancellation rearm, idle capture and a second
seven-call native start, with wrong-owner, borrow, lifetime and boundary negatives.
RUN098 diagnosed a separate actual integration fault: after clean cancellation,
ordinary callbacks still entered the retired Draining Tracker as Foreign before
the next logic-boundary rearm. The Tracker correctly faulted, so the cancel seal
refused its first source check. The observed rearm SP was 16-byte aligned; stack
alignment was not this refusal. Both outer and inner Context routing now delegate
without entering that old Tracker only after a private CancelOrigin-authenticated
retirement proof: genuine Released/Install.can_free, claim released, exact CPU and
host, unchanged Session baseline/Unpublished journal, live unrevoked Context, and
covered fault-free idle Draining depth. Active, failed and restored-dormant paths
keep their existing guards; the generic Draining/Foreign rule is unchanged.
Updated351/388 passed 2/2 with private-proof positive and rejection cases. Actual
RUN100/101 passed public cancellation save/load/reuse acceptance: native entry
at logic step 1696, Plus cancellation with clean release and rearm, full-state
save 2250/load 2350, then a second native start 2596 and natural completion/rearm.
Both processes exited 0 with unchanged shared inputs; the final 2708-byte native
payload and all four HD sections matched strictly. No native test flags or
fallback steps were used.

| Load case | Evidence and boundary |
| --- | --- |
| Active snapshot, same active run | Actual native resume followed by natural completion/rearm. |
| Historical snapshot after retirement; repeated same file | RUN074 resumed three loads at epochs 2/3/4, each completed/rearmed. |
| Saved active A while current B is active | RUN079: two restored Contexts, three natural rearms, zero load denials. |
| Saved all-pristine while A is active | RUN081: revoked old owner/frontend removed without natural rewards; future B started and rearmed, zero restored Contexts. |
| Fresh process, initial pristine snapshot | RUN083: empty host checkpoint history imported an empty arena through genuine same-resource Stage proof; later native scene completed/rearmed. |
| Active snapshot across process restart | Deliberately refused: the original host Origin/model/activation lineage is unavailable. Snapshot bytes cannot reconstruct it. |

Fresh-process initial import requires a genuinely never-exposed current Session,
exact zero arena/journal/definition/allocation, complete saved/live roots and a
current same-resource Stage receipt. Imports before the first genuine SetInfo
remain refused. Changed declarations and different resource graphs are not
promised compatible. Progress loading preserves current mod records and follows
its separate cleanup/recreation path; it does not fabricate an active native
checkpoint.

## Validation and evidence index

All native30 acceptance runs used an explicit runner/user override; this work
does not change the user's default FPS setting or establish other frame-rate
modes from those runs. On/off pairs use the same frozen binary, copied save,
manager/package and shader inputs, matching controls/timing, input SHA inventories
and final unchanged-source checks. Successful comparisons below are strict
native-save payload plus all four HD-section comparisons, not screenshots alone.
Raw game states/images are excluded from the repository; retained local receipts
and crops identify the runs.

| Evidence | Result at report preparation |
| --- | --- |
| Early native cancellation and natural repeat runs | Actual gameplay/control return, retained=0; on/off native and HD parity. |
| RUN074/075 historical same-file loads | Three actual resumes/completions; strict native/HD parity. |
| RUN079/080 cross-slot restore | Actual B→saved A and later historical load; strict native/HD parity. |
| RUN081/082 empty-front restore and reuse | All-pristine restore, later B activation; strict native/HD parity. |
| RUN083/084 initial import | Fresh-process pristine import, two Stage renewals, later completion; strict native/HD parity. |
| RUN090/091 public dialogue diagnostic | Exactly 60 passive-PID readiness refusals, then whole fallback; **not native acceptance**. |
| RUN092/093 public shipped example | Seven native entry calls, natural completion/rearm, terminal 0 and strict native/HD parity; no native test flags. |
| RUN086/087 unsupported whole fallback | All five fallback steps, no native ownership, terminal 0 and strict native/HD parity. |
| RUN094/095 public Plus skip diagnostic | Genuine cancellation release=4, retained=0, but rearm=0. OFF remained in Pause UI and produced no portable save; parity was not established. This historical failure is superseded by the passing RUN100/101 fix acceptance. |
| RUN100/101 public Plus skip, save/load and reuse | Clean release/rearm, full-state save/load, second native start/natural rearm; terminal 0, unchanged shared inputs and strict native/HD parity; no native test flags or fallback. |
| Targeted idle-policy tests | Install274, Backend296, Readiness305 and real Context387: 4/4 passed; strict six-unit compile passed. |
| Cancellation/rearm kernel | Canonical 388/351 passed 2/2; includes an eight-aligned readiness refusal and an explicitly simulated later parent-frame boundary for success. Actual public reuse is separately proven by RUN100/101. |
| Full macOS CTest | Final current unfiltered 388/388 passed, zero failures in 147.69 seconds; 39 affected targets rebuilt; source/main hashes unchanged. |
| Linux/Windows production audit | Current retired-route snapshot: 244/244 compile-only checks passed; 345 dependencies independently rehashed unchanged. |

Local evidence is under `sdk-v3-phase2c5-probe` in the external work directory.
Key immutable receipt hashes:

| Receipt | SHA-256 |
| --- | --- |
| `part5-final-macos-ctest/current-rerun/receipt.json` | `d4b04729d7f0dafda5e590e3501a701e8f33eb4a3d4b0b4e1c848ca759b79d38` |
| `native30-scene-cancel-rearm-100/feature-evidence.json` | `1d78d749454f6575809ac4f1ec50d6c0502f4cbb3882ba59b860bc5608b11469` |
| `public-cancel-rearm-100-native-save-pair.json` | `a99115b94fd1b4df4450557a28a75d4ee16a8c19b0acc2dc736549994b235766` |
| `cancel-rearm-100-unit-receipt.json` | `8e77ffdd4df70ef95e76f46f8bd3579bd742d16b592352ee3836fd49b3f30a90` |
| `final-production-cross-retired-100/current-full-receipt.json` | `c0d0f563de508be3e0d61bf4728ec90febae2af20bda2bedeffe539aa691efa4` |
| `cancel-rearm-388-351-receipt.json` | `fab148e1e5793dff6caf7f7073d8fb2ceb397a49b4fcf9f0a4e2ef8b05132f7f` |
| `final-production-cross-public/final-receipt.json` | `68e2885d21d4654788d726b3e45225b9e235426d91355701674864879190ae00` |
| `native30-wait-bootstrap-079/feature-evidence.json` | `f9ffbfd140dc65ee7f9a5cfe86e8e825cdffd0a8a8ad3e5172f30186946003d4` |
| `native30-wait-bootstrap-081/feature-evidence.json` | `f224a97bbcf32176ae22bfe823d7abccf26278dceb07c1732a39a1be9b359e7a` |
| `native30-wait-bootstrap-083/feature-evidence.json` | `2430a87cd0a6c872d45d79414abfcd4ae044b289cb926031811c891da79b0670` |
| `native30-scene-example-092/feature-evidence.json` | `d1170cb9a2c56fb99d2e3d9a78b26e55a5139bc9ab5ca5af48f18f3d40db8d3d` |
| `native30-scene-fallback-086/feature-evidence.json` | `d3c3914c99e27150d8812d5064e499fed694f2e5daa0519fadb856eeea54a06b` |
| `native30-scene-skip-094/feature-evidence.json` | `0be87f4aaefd33e1cb0a9594bf835c47b6953d06b33163efd52a72a42d29d3d5` |
| `retail-post-talk-control-cache-audit.json` | `63fca556a047b1857dbe733c8d2bfbd0a675f9ca3044e58b8385209be0d7c691` |

Public example RUN092/093 uses frozen binary
`f289a04868783a6384317e5eddb5c7ab97ffd8c12f50261b9d903504a17517c7`.
Its final acceptance receipt confirms the public natural-completion result.
The updated cancellation-rearm fixture and current cross compilation have passed;
RUN100/101 game acceptance and the final current macOS rerun have passed. No broad arbitrary-native-event,
all-actor, cross-resource or cross-process-active-resume claim is made.
