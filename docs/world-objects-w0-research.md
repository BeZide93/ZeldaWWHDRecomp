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
