# World objects: W0 investigation

W0 is in progress. This is a source investigation record, not a passed proof gate.
No W1 converter or public world-object manifest fields have been added.

## Collision storage

The native background registry has 256 entries shared by loaded backgrounds and
moving actors. It is not a separate 256-body allowance for each room. Registration
returns failure when the shared registry is full. Each entry occupies 20 bytes;
the registry plus its virtual table pointer occupies 5,124 bytes.

Mesh storage is allocated separately. Moving collision setup runs inside a solid
heap. The proof must create collision in its actor heap callback, before
registration, and release the body before the heap is destroyed. Calling mesh
setup from an arbitrary per-frame service would violate that allocation contract.

The sizing helper distinguishes input geometry, working arrays and the 188-byte
body. For V vertices, T triangles, B blocks, N tree nodes, G groups and A polygon
attributes, input payload is 52 + 12V + 10T + 2B + 20N + 52G + 16A bytes.
Moving-body working payload is 12V + 32T + 6B + 28N + 32G bytes.
These exclude allocation headers, array cookies, alignment, names, transforms,
actors and rendering. An eight-vertex, twelve-triangle box with one block, node,
group and attribute has 1,092 bytes of collision payload, not a measured heap cost.

The live measurements below cover the proof site. There is no fixed room-wide
collision byte allowance: meshes share native actor heaps with rendering and
other actors. Admission must retain both free memory and a large enough
contiguous allocation. A safe general mod budget still needs a conservative
policy and broader room samples.

## Native integration points

| Function | Entry | Purpose |
| --- | --- | --- |
| `dBgW` constructor | `024F23F4` | Construct the moving collision body. |
| `cBgW::Set` | `0200A030` | Build collision arrays in the solid heap. |
| `dBgS::Regist` | `024EEA6C` | Register a body and its owning actor. |
| `cBgS::Release` | `020087EC` | Remove the registration before heap destruction. |
| `dBgW::Move` | `024F43DC` | Refresh collision after updating its transform. |
| `dBgS_MoveBGProc_Typical` | `024EE658` | Native rider position correction. |
| `dBgS_MoveBGProc_TypicalRotY` | `024EE708` | Rider correction including yaw; used by the native rideable raft. |
| `dLib_getWaterY` | `025871F8` | Water-height selection; requires a valid collision-check argument. |
| Native sea-height query | `0246BA0C` | Wave height at X/Z, used by this proof. |

The native raft registers a moving background and assigns a rider-correction
function. Its ride callback records rider presence and adds rocking behavior;
the background correction path performs the positional carrying. This supports
trying the ordinary native riding path first. It does not prove that a new mod
body carries Link correctly, especially under pitch and roll.

`dLib_getWaterY` also reads its collision-check argument. A proof must supply a
valid check object or use the underlying sea query with its own verified ABI;
passing a null check is not safe.

## Outstanding proof

- Verify the jump onto the final raft envelope and its state-load cases.
- Verify Last session loading and automatic saves.
- Choose conservative collision limits from measured storage and heap headroom.
- Capture hidden, silent game runs, including interpolation and True 60.
- Report the W0 results and revised phase estimates before W1.

Focused sizing, mesh, lifecycle, scheduling and existing actor-contract tests pass
on macOS. The complete W0 gate and final cross-platform refresh remain outstanding; no
universal memory safety limit is claimed. The full macOS suite and targeted
retries have passed all 325 configured tests. Initial retries were needed for a
Java-runtime selection and insufficient free disk; both environment issues were
resolved without weakening tests.

## First live diagnostic

The first hidden, silent macOS run registered all three original bodies through
the native registry. Landing on the platform exercised its polygon attributes.
Shared occupancy reached 25 bodies, including the game's bodies;
the three mod bodies used three slots.

| Handmade mesh | Vertices | Triangles | Measured allocation delta inside its heap callback |
| --- | ---: | ---: | ---: |
| Platform and ramp | 16 | 24 | 1,828 bytes |
| Buoy | 8 | 12 | 1,132 bytes |
| Raft | 8 | 12 | 1,132 bytes |

These deltas include the fixture input and the native collision body/arrays, not
render models or the actor's base allocation. Each native actor base allocation
was separately measured as 4,176 bytes. The existing actor allocation accounting
accepted the three actors with approximately 0.98 MB charged in that room. This
is a diagnostic sample, not a certified room capacity or a final mod limit.

The initial collision attributes were zero-filled. In this format, zero is a
valid native exit reference: landing on the platform selected exit 0 and caused
a room reload. The bodies released cleanly during that transition and were
recreated, with the same allocation deltas, but this is not acceptance of the
intended static-platform behavior. The builder now writes explicit no-exit,
no-camera, no-Link-reference and no-path sentinels. Regression tests check those
fields. Wall, special-ground, through and hookshot attributes remain ordinary.

The sheltered Windfall square returned a constant native wave height. The next
proof site is in sea room 12, east of Windfall, around x=75,000 and z=-201,500,
away from the neighbouring island's centre. The native sea function samples its
rendered height table and returns the base height outside its active area, so
observing stationary distant fixtures does not establish wave following. A live
run near the bodies remains necessary.

Current checks: three new native sizing/mesh/lifecycle tests, two original-asset
tests, and the four existing static/skinned/combat/carry actor contracts pass on
macOS. The executable builds from freshly generated game code for the current
branch. An earlier attempt using older SDK-generated code failed at link time;
that generated-code mismatch was corrected. Compile-only Linux, Windows and
Android checks passed for the actor unit before the neutral-attribute fix and
must be refreshed against the final proof sources.


## Native riding and scheduling

The original proof copied the ordinary static mod actor's schedule. That ran its
collision update after Link. Scene drawing clears the native background movement
flags, so Link saw no movement flag on his next execute. He could remain grounded
by vertical collision correction while the camera followed the raft. Camera
correction callbacks alone were therefore insufficient evidence of riding.

The private proof rejects mixed fixture and ordinary mod-actor declarations, so
its shared-profile scheduling change cannot affect another mod actor. The
isolated proof now copies the native raft's process list (3), list priority
(65533) and execution priority (432). It updates the moving background before
Link. A passive observer delegates the native rider function exactly once and
records its arguments and results. The corrected run shows calls on Link's actual
current and previous positions, including persistent changes in X/Z and height.
No code moves Link on behalf of the raft.

Link remained grounded on the raft for over 1,200 logic steps through repeated
native wave cycles. The raft uses the sea-height query at its centre and four
nearby points; pitch and roll follow that slope. This establishes the native
carrying path at 30 Hz, pending the frame-rate and state-load cases above.

## Ramp, state loads and room recreation

A scripted walk moved Link from the ramp near z=-202,320 (ground height 20) to the
platform at z=-201,585 (height 150), without moving his position through proof
code. A full state captured 575.3 MB, compressed to 275.4 MB, then restored
successfully. The portable state was 6,786 bytes. Its native arrival starts above
the saved location; Link subsequently settled back onto the platform.

Portable loading released all three old bodies and constructed three new ones.
An explicit trip to the Windfall square and back repeated that release/recreate
cycle. Shared registry occupancy returned to 14 (11 native bodies plus three
proof bodies), with unchanged collision allocation deltas. The run exited cleanly
and wrote a 6,781-byte Last session state. Source save and mod-manager inputs were
unchanged. Last session loading and the automatic-save path still need proof.

## Parent heap sample

Capacity must be measured after the native solid-heap callback returns. Native
creation temporarily allocates the largest parent block, then shrinks the actor
heap to its actual use; free space inside that temporary heap is not a room
budget. The proof now queries the actor solid heap's actual parent after shrink.
The native heap vtable provides total free space at slot 0x74 and maximum aligned
allocation at slot 0x7C, with alignment 32.

At the sea-room-12 site, after all three bodies were created, the parent had
9,074,416 bytes free and a largest aligned block of 7,551,552 bytes. On a later
room recreation, free space was 9,074,472 bytes and the largest block was
7,708,480 bytes. Total free space remained effectively stable, while fragmentation
changed the largest block. These are measured headroom for this room/save state,
not an engine guarantee for every room or a claim that this much collision may
safely be added.


## Ship collision and the raft envelope

The ship's native wall correction samples circles at its position height, 75 and
150 units above it, plus a deep sea-stage sample at world height -600. A thin
floating wall can fall between those samples. The original 50-unit raft hull
stopped the ship initially, then let it pass as the independent wave phases
changed. Mesh registration alone therefore did not establish reliable ship
collision.

The final handmade raft retains that thin, walkable deck and adds eight vertical
object-collision triangles spanning local height -300 to 200 at the same outer
footprint. There are no extra ground faces. A second native polygon attribute
uses through-mask 0xED: camera, Link, arrows, bombs, boomerangs and rope pass
through; object-class queries still collide. This is a collision envelope for
objects, including the ship, not a ship-exclusive category. Native filtering has
no separate ship bit: the ship uses the object category. W1 must account for this
when defining its public attribute names.

The ship reached the static platform and remained at X=74,450 against the wall
at X=74,700 under continued sailing input. It similarly remained near
Z=-201,820 against the buoy, and near Z=-202,100 against the final raft envelope,
through repeated waves. No ship movement or collision code was added.

The final raft contains 16 vertices, 20 triangles and two attribute records. Its
measured collision allocation is 1,676 bytes, 544 bytes more than the original
thin hull. The three final proof bodies cost 4,636 collision bytes in total. At
the same room-12 site, parent free space after construction is 9,073,872 bytes;
the largest aligned block is 7,551,008 bytes. The native registry still uses
exactly three mod slots.

## Limits proposed for the next phase

There is no universal collision-memory allowance for a room. The registry is
shared globally, and mesh storage competes with other allocations in the
selected native heap. The proof's native allocation deltas match the sizing
model plus names, alignment and native array cookies. This allows conservative
admission without attempting to fill the game's heap or registry to failure.

A starting W1 policy to validate is: at most 16 mod background bodies in a room,
a 1 MiB aggregate collision budget, a 256 KiB collision budget per body, and at
least 16 native registry slots left unused. Vertex/triangle limits must fit those
byte budgets; 2,048 vertices and 4,096 triangles are upper candidates per body,
not an entitlement to allocate every body at that size. Rendering and actor
allocations retain their separate existing budgets. A new body must also leave
at least 2 MiB free in its actual parent and enough contiguous aligned space for
construction. These are proposed SDK safety margins for W1, not measured engine
maxima. More crowded rooms and storm conditions remain validation cases.

## Public surface and phase estimates

W0 adds no public manifest fields or editor commands. The original example uses
existing model and actor declarations. Only the private collision-proof option
activates its handmade native bodies; ordinary mod actors keep their existing
profile and collision behavior. Exported assets remain original source models.

Rough engineering estimates after this gate, including proofs and regression
checks:

| Phase | Estimate | Main uncertainty |
| --- | --- | --- |
| W1 | 4–7 days | Collision hierarchy/converter limits, placement queries and native attribute categories. |
| W2 | 4–8 days | Storm-wave bounds, object envelopes and drift/obstacle interaction. |
| W3 | 3–6 days | Whether grapple anchors need native actor behavior beyond polygon attributes. |
| W4 | 5–10 days | Neighbour-square drawing, shoreline effects and chart/banner integration. |

These estimates should be revised at each phase's playable proof. W1 has not
started.


## Jump and final-envelope state loads

A second placement, using the same original models, put the raft at X=75,700 so
there was a 150-unit gap from the platform. Normal directional input made Link
run off the platform, enter the native automatic-jump procedure, land on the
raft and resume ordinary standing/riding. His position was not changed after
the initial test setup. Full and portable state loads then succeeded with the
final 20-triangle raft envelope present; after portable reconstruction, Link
settled back onto the raft and continued riding.
