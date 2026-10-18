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

Live registry occupancy, largest available solid-heap block and actual allocation
deltas still need measurement near the proof site. No safe per-room mod budget
has been established yet.

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
| `dLib_getWaterY` | `025871F8` | Native water-height selection including sea waves. |

The native raft registers a moving background and assigns a rider-correction
function. Its ride callback records rider presence and adds rocking behavior;
the background correction path performs the positional carrying. This supports
trying the ordinary native riding path first. It does not prove that a new mod
body carries Link correctly, especially under pitch and roll.

`dLib_getWaterY` also reads its collision-check argument. A proof must supply a
valid check object or use the underlying sea query with its own verified ABI;
passing a null check is not safe.

## Outstanding proof

- Build original handmade platform/ramp, buoy and raft geometry.
- Measure actual collision heap use and shared registry occupancy.
- Show Link walking and riding, and the ship colliding with all three bodies.
- Verify unregister/recreate behavior across room and stage transitions.
- Verify full and portable state loads, Last session and automatic saves.
- Capture hidden, silent game runs, including interpolation and True 60.
- Report the W0 results and revised phase estimates before W1.

The standalone collision-sizing test passes on macOS. No in-game acceptance,
full CTest result, cross-platform build result or memory safety limit is claimed.

## First live diagnostic

The first hidden, silent macOS run registered all three original bodies through
the native registry. The log recorded Link's native ground check identifying
the platform. Shared occupancy reached 25 bodies, including the game's bodies;
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
