# World objects: W0 proof report

The W0 proof passes for the tested sea-room scenarios. W1 has not started.
The work is on `sdk-world-objects`; the implementation revision is
`97220ea233842ee38cd3b4ad770b2f67c277a4ef`.

W0 provides original handmade platform/ramp, buoy and raft fixtures using native
moving-background collision. The proof is private and opt-in; the converter,
public world-object format and editor commands belong to the later phases.

## Answers to the three unknowns

**Collision memory is shared, rather than a fixed byte allowance per room.**
The background registry contains 256 slots shared by loaded backgrounds and
moving actors. Each slot occupies 20 bytes; the registry including its virtual
table pointer occupies 5,124 bytes. Meshes allocate separately in native actor
solid heaps. The final three fixtures consume 4,636 collision bytes and three
registry slots. The sampled room still has about 9.1 MB free in its actual parent
heap. These measurements establish fixture costs and admission inputs, rather
than a universal room capacity.

**The tested state loads and room changes stay clean.** Full and portable loads,
Last session, a real automatic save and its load, and leaving/re-entering the sea
square completed successfully. Portable/scene reconstruction released the three
old registrations and created three replacements. Occupancy returned to 14 in
the settled proof scene, with exactly three mod bodies. No stale-registration
rejections or crashes occurred in those acceptance runs.

**Native riding carries Link without a Link-specific movement patch.** The bodies
must execute before Link, following the native raft's schedule. A passive observer
confirmed the game's correction function changing Link's actual current and
previous positions. Normal input also made Link jump from the platform onto the
raft and remain aboard through repeated wave cycles and state loads.

## Playable proof and evidence

The main site is sea room 12, east of Windfall, away from Pawprint's island centre.
The platform is at X=75,000, Y=150, Z=-201,500; the buoy is at X=78,000; the main
raft is at X=76,500. The same raft was placed at X=75,700 for the normal jump,
leaving a 150-unit gap from the platform. All models and handmade collision are
original; asset provenance is recorded with the example.

| Check | Observed result |
| --- | --- |
| Ramp and platform | Link walked from ramp ground height 20 to the platform at height 150, then stood normally. |
| Buoy | Native wave height and slope changed its height, pitch and roll; a close run also kept Link grounded on its top. |
| Raft riding | Link remained on the raft through repeated waves; native correction changed his retained position. |
| Normal jump | Ordinary directional input triggered the native automatic jump, landing and standing procedures. |
| Ship against platform | Continued sailing stopped the ship at X=74,450 against the wall at X=74,700. |
| Ship against buoy | Continued sailing held the ship near Z=-201,820, outside the buoy. |
| Ship against raft | The final object envelope held the ship near Z=-202,100 through repeated waves. |
| Full state | Captured/restored successfully on both platform and raft; the final raft-envelope run also passed. |
| Portable state | Removed/recreated the bodies and returned Link to the platform or raft. |
| Room change | A trip to the Windfall square and back released/recreated all three bodies. |
| Last session | Saved on quit, then loaded at startup into the proof scene. |
| Automatic save | The supported two-minute timer wrote Auto save 1; loading it recreated the bodies and returned Link to the platform. |
| 60 fps interpolation | Completed with Link grounded through repeated waves; diagnostics recorded two output frames per logic step and active model blending. |

Acceptance runs used hidden, silent windows, disabled host input, and independent
copies of save and mod-manager inputs. Source input inventories were unchanged.
Crops, run receipts, binary/source hashes and compact native-position summaries
accompany the report. Large raw actor dumps, full frames, copied inputs and shader
caches were removed after recording results.

True 60 is experimental and was removed from acceptance scope. Its interrupted
run contributes no passing result. Higher interpolation rates, storms and drift
remain later-phase checks.

## Native functions and integration

| Function or integration | Entry or value | Use |
| --- | --- | --- |
| `dBgW` constructor | `024F23F4` | Construct each native moving background. |
| `cBgW::Set` | `0200A030` | Build native collision arrays inside the actor's solid-heap callback. |
| `dBgS::Regist` | `024EEA6C` | Register the body with its actor and process ID. |
| `cBgS::Release` | `020087EC` | Remove owned registrations before heap teardown. |
| `dBgW::Move` | `024F43DC` | Update collision after the actor matrix changes. |
| `dBgS_MoveBGProc_TypicalRotY` | `024EE708` | Native rider correction; the proof observer delegates it exactly once. |
| Native sea-height query | `0246BA0C` | Sample centre and nearby wave heights to derive height, pitch and roll. |
| Native raft scheduling | List 3, list priority 65533, execution priority 432 | Update moving backgrounds before Link. |
| Native heap virtual slots | `0x74`, `0x7C` | Read total free space and largest allocation aligned to 32 bytes. |

The native water-height selector at `025871F8` requires a valid collision-check
argument. The proof uses the underlying sea-height query with its verified ABI.

Body, matrix, owner identity and observation counters reside in guest memory.
Ownership checks include the body pointer, actor pointer, process ID and registry
slot. A foreign registration is never released or replaced. Full restoration
uses restored guest state; portable/scene changes use the actor lifecycle to
remove and recreate native bodies.

The private proof rejects mixed fixture/ordinary mod-actor declarations before
installing its shared-profile schedule. Ordinary mod actors keep their existing
schedule when the proof is disabled.

## Memory measurements and proposed limits

| Final handmade collision | Vertices | Triangles | Attribute records | Measured collision allocation |
| --- | ---: | ---: | ---: | ---: |
| Platform and ramp | 16 | 24 | 1 | 1,828 bytes |
| Buoy | 8 | 12 | 1 | 1,132 bytes |
| Raft including object envelope | 16 | 20 | 2 | 1,676 bytes |
| Total | 40 | 56 | 4 | 4,636 bytes |

These deltas include source collision and native body/working arrays, excluding
render models and the actor base. Each actor base separately costs 4,176 bytes.
Existing allocation accounting charged approximately 0.98 MB for the three
actors and their original render resources in that room.

For V vertices, T triangles, B blocks, N tree nodes, G groups and A attributes,
source payload is 52 + 12V + 10T + 2B + 20N + 52G + 16A bytes. Moving working
arrays use 12V + 32T + 6B + 28N + 32G bytes; the body uses another 188 bytes.
Names, array cookies, alignment and allocation headers are additional. The
native measured deltas remain authoritative.

Capacity is sampled after native heap creation returns and shrinks its temporary
solid heap. Measuring free space inside the callback would incorrectly report
the temporary largest-block construction allowance as a room budget. With the
final fixtures, the actual parent had 9,073,872 bytes free and a largest aligned
block of 7,551,008 bytes. The settled scene used 14 background slots, including
three mod slots. Subsequent recreation retained three mod registrations and
stable fixture allocation costs; native allocations/fragmentation varied.

A starting **W1 policy to validate**, not an enforced or demonstrated safe limit,
is 16 mod bodies per room, 1 MiB aggregate collision, 256 KiB per body, and 16
native registry slots left unused. Candidate per-body vertex/triangle caps are
2,048/4,096, subject to the actual tree, group and attribute byte budget. Rendering
and actor allocations retain separate budgets. Admission should also leave at
least 2 MiB free in the actual parent and sufficient contiguous space. Crowded
rooms and larger meshes need their own acceptance before these limits ship.

## Findings that changed the proof

The ordinary static-actor schedule updated collision after Link. Link could
appear grounded through vertical correction while only the camera received
moving-background correction. Copying the native raft schedule made actual Link
correction occur; camera callbacks alone were not accepted as riding evidence.

The original 50-unit raft sides stopped the ship intermittently, then allowed it
through as wave phases changed. Native ship wall circles sample a few heights:
ship height, +75, +150 and a deep sea-stage sample at world height -600. A shallow
wall can fall between those samples.

The final raft keeps its thin visible/walkable deck and adds eight vertical
object-collision triangles at the same footprint, spanning local Y=-300 to 200.
They add no ground faces. A second native attribute has through-mask `0xED`, so
Link, camera, arrows, bombs, boomerangs and rope pass through while object-class
queries collide. The ship uses the object category: **this is not a native
ship-exclusive mask**. The envelope costs 544 additional bytes. W1 must reflect
this distinction in its public attributes rather than promise an unsupported
ship-only bit.

Zero-filled polygon attributes initially selected native exit 0 on landing.
The builder now writes explicit no-exit, no-camera, no-Link-reference and no-path
sentinels, covered by regression tests.

## Validation and performance

- macOS Metal application build and full CTest: **325/325 passed** in the final run.
- Focused mesh, sizing, owner/lifecycle, schedule and mixed-actor isolation checks,
  plus existing static/skinned/combat/carry actor contracts, pass.
- Original render assets pass reproducibility, geometry and texture checks.
- Linux x86-64, Windows x86-64 and Android arm64 compile checks pass for the actor
  translation unit including all collision headers. These are compile checks,
  not complete platform application links. No phone was used.

Initial checks encountered stale generated-code inputs, an unavailable selected
Java runtime and insufficient free disk. Fresh generated inputs, the installed
JDK and restored disk space resolved them without weakening checks. The final
full suite is green. Two automatic-save harness attempts were excluded: escaped
newlines and an unsupported one-minute interval left the feature disabled. The
passing run used real newline-delimited settings and the supported two-minute
interval, observed the actual timer write, then loaded that file.

Uncapped interpolation diagnostics sampled roughly 49–59 logic steps/s near the
proof scene, with two output frames per step. Main-thread logic samples were
about 5.3–6.0 ms and record passes about 1.2–1.4 ms in the late samples. These
include the game's existing scene work; they are not collision-only overhead,
percentile frame times or stable capped-FPS measurements. Android performance
was not measured. The crops and native position records support the tested
motion; they do not certify all hardware, weather or frame rates.

## Public surface, remaining risks and next phases

New public manifest fields: **none**. New editor commands: **none**. The original
example uses existing model/actor declarations; `WWHD_WORLD_OBJECT_PROOF=1`
activates only its private handmade proof. Mod progress continues through the
SDK's mod-owned state; this feature adds no game-event/save-bit manipulation.

Automatic placement-conflict checks, general collision conversion, material
mapping and production budgets remain W1 work. Storms, drift and obstacle
response remain W2 work. Native object/ship category sharing and thin-surface
collision need explicit handling in that design. The tested room/load cycles
provide evidence for those cases, not a universal leak or capacity guarantee.

Rough engineering estimates, including playable proofs and regression checks:

| Phase | Estimate | Main uncertainty |
| --- | --- | --- |
| W1 | 4–7 days | Collision hierarchy/converter limits, placement queries and native attribute categories. |
| W2 | 4–8 days | Storm-wave bounds, object envelopes and drift/obstacle interaction. |
| W3 | 3–6 days | Whether grapple anchors need native actor behavior beyond polygon attributes. |
| W4 | 5–10 days | Neighbour-square drawing, shoreline effects and chart/banner integration. |

Changelog suggestion: Added a prototype for walkable and floating mod objects
with native Link riding and ship collision.

Work stops at the W0 report gate before W1.
