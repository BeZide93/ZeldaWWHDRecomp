# Static world objects

Static world objects let a mod add walkable ground and solid obstacles using its
own models. Link and the ship use the game's background collision. Ground
materials select the game's footstep sounds and surface effects.

This page covers static objects. Floating, drifting, climbable walls, hookshot
and grapple targets, distant island models, shore foam and sea-chart markers are
later additions.

## Place a model with collision

Declare an original static `.glb` model, then refer to it from an actor:

```json
{
  "models": [
    {"id": "island", "source": "assets/island.glb"}
  ],
  "actors": [
    {
      "id": "island-a",
      "kind": "mod",
      "model": "island",
      "behaviour": "static",
      "stage": "sea",
      "room": 12,
      "position": {"x": 75000, "y": 0, "z": -201500},
      "collision": {"mesh": "island"}
    }
  ]
}
```

`model` selects the visible model. `collision.mesh` names a declared static model
whose geometry supplies collision; it can name the same model. `behaviour` is
currently `static`. A world object uses a rigid model and has no carry, combat,
path, animation or dialogue behavior.

GLB coordinates are metres. Conversion uses 100 game units per metre and keeps
the authored origin. Manifest positions and shape dimensions use game units;
rotations use degrees. Optional `rotation` and `scale` apply to both appearance
and collision. Keep object origins near their geometry rather than modelling
at sea-map coordinates in Blender.

## Author collision in the GLB

Keep one visible mesh with one indexed triangle primitive, one opaque material
and one embedded PNG texture, as for existing static mod models. Additional
collision meshes may have multiple triangle primitives. Name each collision
mesh or its scene node with an `_col` suffix, for example `island_col` or
`pier_col`. These meshes are omitted from rendering.

A collision primitive needs indexed triangles and `POSITION`. Normals and UVs
are optional; they do not control collision. Assign one of these material names:

| Material | Surface |
| --- | --- |
| `col_stone` | Stone |
| `col_grass` | Grass |
| `col_wood` | Wood |
| `col_sand` | Sand |
| `col_metal` | Metal |
| `col_wall` | Ordinary wall surface |

Use separate material slots for different ground types. Triangle direction
matters: orient ground faces upward and obstacle faces outward. The converter
preserves node transforms and corrects winding for mirrored transforms. It
rejects zero-area faces, invalid indices, nonfinite coordinates and unsupported
material names.

When no `_col` mesh exists, collision can be derived from the visible triangles.
Equivalent positions are welded, but the converter does not reduce a detailed
model to an arbitrary triangle budget. The visible material’s `col_*` name
selects the fallback surface; other visible material names use stone. Use a
separate, simpler `_col` mesh when the render mesh exceeds the collision limits.

Names such as `col_climb`, `col_hookshot`, `col_noship` and `col_shiponly` are not
available in this phase. The native collision filter groups the ship with other
objects; it has no separate ship-only category.

Ship collision samples several heights rather than treating every thin floating
surface as a solid hull. Give static coasts and obstacles sufficient vertical
sides. A shallow horizontal sheet alone is not a reliable obstacle to the ship.
The original island example extends its coast below sea level.

## Simple collision shapes

Use a shape instead of a collision mesh for a small obstacle. All shapes are
centered at the actor origin, including their vertical dimension. They follow
the actor's rotation and scale. Their optional `surface` defaults to `stone` and
accepts `stone`, `grass`, `wood`, `sand`, `metal` or `wall`.

```json
{"shape": "box", "size": {"x": 200, "y": 100, "z": 300}, "surface": "wood"}
```

Box `size` gives full width, height and depth. A box with height 100 spans local
Y=-50 to Y=50. To rest it on ground height 0, place its origin at Y=50.

```json
{"shape": "cylinder", "radius": 100, "height": 200, "surface": "stone"}
```

```json
{"shape": "sphere", "radius": 100, "surface": "metal"}
```

Put one of these objects inside the actor's `collision` field, alongside
`behaviour: "static"`. Cylinder and sphere collision use bounded triangle
approximations. A shape cannot also specify `mesh`.

## Limits and placement

Each collision mesh is limited to 4,096 vertices and 4,096 triangles. Every local
coordinate must stay within ±20,000 game units, and the extent along each axis
must be at most 20,000 units. These bounds are checked again after actor scale
is applied. Smaller meshes make collision checks cheaper.

Admission also checks estimated native collision storage: at most 512 KiB per
body and 2 MiB in a room. The existing limit of 16 placed actors per room,
across enabled mods, also applies; ordinary actors use those places too.
Rendering and actor memory have their own budgets. The native shared registry must retain at
least 16 free slots. Registration also requires native heap headroom: at least
1 MiB free and a contiguous aligned allocation of at least 256 KiB after the
actor heap is finalized. Passing the mesh limits does not guarantee an
allocation in every room.

Placement checks use the player's installed game resources. They protect native
collision bounds, authored actors and fixed events, and routes. The current
index is deliberately conservative:

- Every authored native actor or tag, across layers, reserves 10,000 units around
  its position in X/Z.
- Native routes reserve a 2,000-unit corridor.
- Protected geometry and routes receive an additional 100-unit placement clearance.
  Native ground wholly below the object may touch its base; penetrating it is refused.
- An object must stay inside the indexed room. A sea object that crosses into a
  neighbouring square is refused until coverage for that placement is available.
- Missing, malformed or unsupported collision, event or route data prevents
  placement rather than treating the room as empty.

A conflict can therefore reserve more space than the visible island or event.
Move or shrink the object and query again. Do not bypass a coverage error to
place an island beside an unknown event. Conflicts between mod objects and
collision-memory budgets are reported alongside native placement findings.

## Editor commands

These commands belong to the editor command layer. The existing editor UI is
unchanged. Requests below show the command name followed by its parameters.
An optional `mod` selects another open package.

Prepare a declared static model's collision before placing it during a running
game:

```text
prepare_collision {"model":"island"}
```

Preparation runs in the background. Its result note reports that work has
started. Query the object once its collision is ready; if this is the first mod
model in the running game, a restart may be required.

Place a new actor atomically, without an intermediate invalid cylinder actor:

```text
place_world_object {"id":"island-a","model":"island","behaviour":"static","stage":"sea","room":12,"position":{"x":75000,"y":0,"z":-201500},"collision":{"mesh":"island"}}
```

Optional `rotation` and `scale` use `{x,y,z}`. `behaviour` defaults to `static`.
The actor id must be unused. The command creates one undo step.

Change collision and behavior together:

```text
set_world_object {"id":"island-a","behaviour":"static","collision":{"shape":"box","size":{"x":200,"y":100,"z":300},"surface":"wood"}}
```

This also creates one undo step. A rejected change leaves the package untouched.
The ordinary `set_actor` command accepts `behaviour`, but coordinated changes
should use `set_world_object` so the actor remains valid.

Inspect the prepared geometry or placement:

```text
preview_collision {"id":"island-a"}
placement_conflicts {"id":"island-a"}
```

`preview_collision` returns world-space vertices and edges suitable for a
wireframe, plus placement and budget findings. It does not add a wireframe panel
to the UI. `placement_conflicts` returns the findings without wireframe geometry.
Both queries leave the package and undo history unchanged. Simple shapes need
no model-collision preparation.

## Make a small island in Blender

1. Model a low-poly hill with a beach and a pier near the origin. Use metre
   coordinates. Keep one visible mesh and one opaque textured material; use a
   texture atlas for several visible colors.
2. Duplicate or rebuild the terrain as simple collision geometry. Delete visual
   details that Link cannot walk on. Name the collision object `island_col`.
3. Assign `col_sand` to beach faces, `col_grass` to hill faces and `col_wood` to
   pier faces. Check upward ground normals, outward coast normals and all joins
   between ramps and platforms. Extend coast walls below the sea.
4. Triangulate the visible and collision meshes. Check that collision has no
   collapsed faces and stays within the vertex, triangle and size limits.
5. Export the visible mesh and `_col` mesh into one GLB with its PNG embedded.
   Do not hide collision by excluding it from the export: the converter removes
   `_col` meshes from the visible output.
6. Add the model and static actor declarations to the package. Choose an empty,
   fully indexed part of the target room. Inspect collision and placement
   findings before testing the object.
7. Walk from the beach to the hill and pier. Check footsteps on each material,
   sail into the coast, and test a room change and a state load. Recheck placement
   after changing scale, rotation or position.

The complete original example is
[`examples/guest-mods/world-objects-w1`](../examples/guest-mods/world-objects-w1).
Its [`assets/make_fixtures.py`](../examples/guest-mods/world-objects-w1/assets/make_fixtures.py)
reproduces the GLB using only the Python standard library. It contains a textured
hill, beach and pier, with separate sand, grass and wood collision primitives.
[`ASSETS.md`](../examples/guest-mods/world-objects-w1/ASSETS.md) records its origin
and license.

Ship only your original source models and package files. The game prepares its
rendering resources and collision cache locally; generated cache archives and
resources taken from the game do not belong in an exported mod.
