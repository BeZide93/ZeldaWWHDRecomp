# Actors and scenes (SDK v3 preview)

**Preview.** This is an experimental extension to [SDK v2](mod-sdk-v2.md), and it may still change.
Mods can place a small set of the game's own actors, add static models of their own, give them
talk actions and play short scenes. Static mod actors use the game's lighting, shadow, collision,
attention and dialogue systems. Animated characters and behaviour callbacks are not available yet.

The worked package is [sdk-v3-windfall](../examples/guest-mods/sdk-v3-windfall/manifest.json).
It contains original text, names existing game actors and includes an original
carved-post model; it includes no game resources. It requires the Story Notebook package and a build with code-mod
support. The example is a data-only content package: its empty content directory
has a placeholder so the package manager can index it. World declarations are
read at startup; enabling or changing a package requires a restart.

## Placing actors

`actors` is an array. Each native actor has an `id`, `actor`, `stage`, `room`, and
`position` with numeric `x`, `y`, `z`. Optional `rotation` uses **degrees** on each
axis, limited to ±360. Optional `scale` has three positive components, each at
most 10; the default is one on each axis. Positions must be finite and within
±350000 game units. Room numbers are 0–63 and stage names are at most eight
ASCII letters, digits or underscores.

The initial allowlist is deliberately small:

| Actor | Names accepted | Parameters |
| --- | --- | --- |
| Sign | `sign`, `kanban`, `PROC_KANBAN` | A mod-owned `flag`; optional dialogue |
| Resident | `resident`, `NpcPeople`, `PROC_NPC_PEOPLE` | `parameters: {"resident": 9}` (Um1) |
| Chest | `chest`, `Tbox`, `takara`, `PROC_TBOX` | A mod-owned `flag`; safe rupee `parameters.item` |

Other actor names and unsafe parameters are validator errors. Only ordinary chest
raw `params: 0` is accepted at present. The port owns the remaining chest/sign
parameters so a package cannot select a game switch or opened bit.

`flag` is a symbolic ID in the declaring mod's story records. It is not a game
save-bit number. Chest opening and sign reading use this namespace. The runtime
rejects colliding flag keys across loaded actors and omits an actor if no story
record can be reserved for its flag.

There are at most 64 declarations per package, 128 across loaded packages and
16 per stage/room across loaded packages. The room cap bounds asynchronous actor
creation and the amount of additional actor work. It is a conservative policy,
not a measurement of spare game heap.

Optional `when` conditions can name `quest` plus `step`, an owned mod `item`
(`item:<id>`), and `time` (`day` or `night`). Conditions are checked when the room
is populated and reconciled in the current room on game logic steps. Pending
creation/deletion retains its identity; an active conversation finishes before
its partner is removed. Actors belong to the native room layer and unload with it.

## Static mod actors

Declare original assets in `models` and refer to their IDs with `kind: "mod"`.
A mod actor uses the same placement, conditions, dialogue and `then` actions as
other placed actors. It must not specify `actor`, `params` or `parameters`.

```json
"models": [
  {"id": "carved-post", "source": "assets/carved_post.glb"}
],
"actors": [
  {
    "id": "post", "kind": "mod", "model": "carved-post",
    "stage": "sea", "room": 11,
    "position": {"x": 100, "y": 1040, "z": -201500},
    "collision": {"radius": 42, "height": 160, "mode": "solid"},
    "attention": {"prompt": "check"},
    "lines": ["A diamond is carved into the wood."],
    "then": [{"action": "flag", "id": "post-checked", "value": true}]
  }
]
```

`source` is a package-relative `.glb` path with no parent components. Up to eight
models may be declared per package. Source files are limited to 8 MiB, 4096
vertices and 8192 triangles per model. Models contain one mesh, one indexed
triangle primitive, one opaque material and one embedded RGB/RGBA PNG. Texture
width and height are at most 512; width must be a multiple of 64 and height a
multiple of eight. The original example and its reproducible source are listed
in [ASSETS.md](../examples/guest-mods/sdk-v3-windfall/ASSETS.md).

In Blender, model in metres with positive Y as the exported up axis. Put the
object origin at its foot for ground placement, triangulate the mesh, assign a
single material with a PNG base-colour texture, and export **glTF 2.0 Binary
(.glb)** with normals and UVs. Export only the selected model and disable
animations, skinning, morph targets and optional compression/extensions. The
converter accepts a single instance in the active scene and bakes rigid node
transforms, including scale; one exported metre becomes 100 game units. The
manifest's position and collision dimensions use game units. The converter
preserves the authored origin. Use opaque base colour with no tint; additional
PBR texture inputs, transparency and double-sided materials are unsupported.

The player's machine converts the original GLB to Wii U BFRES using a static
material, toon texture and shader from the player's own game. Each model receives
its own archive and model identity. Generated BFRES and shaders belong in the
package's local data cache; do not ship them in packages. Editing the model or
changing the player template invalidates its cache key. Packages and this
repository must never include game-derived material or shader templates.

`collision` specifies an upright cylinder with explicit game-unit dimensions:
`radius` is greater than zero and at most 1000; `height` is greater than zero and
at most 2000. The cylinder stays upright and uses these declared dimensions
independently of the model's rendered XYZ rotation and scale. Choose `mode: "solid"`
or `"pushable"`. `attention.prompt` chooses `"check"` (the default) or `"talk"`.
Dialogue uses the existing message and action system; static actors face Link by
turning their whole body. Animation, skeletal look-at and custom behaviour code
are not part of this model format. Model counts and geometry caps bound input
size. The runtime also measures native game heap allocations and enforces
2 MiB per model, 8 MiB per mod and 16 MiB per room. Shared model resources count
once; actor instances count separately. Cached model resources remain charged
until the game unloads them. Loading preserves at least 8 MiB in the backing
archive heap and 1 MiB in the actor heap's parent. A model that cannot fit fails
creation and retries on a room reload, rather than allocating repeatedly.
These budgets cover game heaps; they do not describe GPU memory usage.

## Dialogue and actions

An actor's `lines` is a string or an array of at most three strings (240 UTF-8
bytes combined). The game displays it through its message window. Its `then`
array contains at most eight actions:

```json
"then": [
  {"action": "give", "id": "item:token", "count": 1},
  {"action": "quest_step", "id": "lantern", "step": 2},
  {"action": "scene", "id": "lighthouse"}
]
```

`take` uses an item reference and count. `flag` uses a mod flag ID and boolean
`value`. Referenced items, quests, quest steps and scenes must be declared in the
same manifest. Actions run in order after the talk closes; a failing action stops
the sequence. Skipping or aborting a scene does not apply its end actions.

A placed character belongs to its declaring mod: its actor ID is local to that
mod. Multiple mods may place separate characters using the same native resident
model. They do not compete for that placed character's lines. Existing catalogue
resident takeovers keep SDK v2's rules: tracked quest first, then start order;
one quest per talk. The existing choice box chooses among eligible quest topics.
This preview adds no choice box to a placed character's `lines` or to a scene.

## Scenes

`scenes` contains up to 16 scenes per package. Each scene has an `id` and 1–64
`steps`, with exactly one final `end`. Timed steps require positive `frames` or
`seconds` (not both). Frames are 60 Hz timeline units; the base 30 Hz game logic
advances two units per service. Validation caps the total at 18000 units (five
minutes). Fast-forward intentionally follows the game clock.

| Step | Fields |
| --- | --- |
| `camera` | `eye`, `target` or `target_actor`, duration; optional `pan: true`, `eye_to`, `target_to` |
| `say` | `actor`, `text`, duration as a completion deadline |
| `move` | A placed resident `actor`, `position`, duration |
| `face` | A placed resident `actor`, `position` or `target_actor` |
| `wait` | Duration |
| `sound` | A validated game UI sound name |
| `fade` | `direction: "in"` or `"out"`, duration |
| `end` | Optional `then` actions |

Character references name a placed actor ID or `link` where supported. Movement
and facing are limited to placed residents; they cannot move or turn Link, signs
or chests. Unknown
references, missing durations, nonfinite coordinates and unsupported sound names
are errors. Dialogue deadlines abort if the message has not completed; they do
not silently award the scene's rewards.

An optional `trigger` uses `type`: `room` (stage/room), `area` (also position and
radius), `talk` (actor), `quest_step` (quest/step), or `salvage` (chart). A dialogue
`scene` action can also request playback. Requests wait for native events to end;
only one scene holds control at a time, with a bounded queue and duplicate-request
suppression.

The implementation uses the **fallback route**: native compulsory-event admission,
the game's camera and message functions, and native event teardown. It does not
replace the stage's event-list resource. Runtime event-list generation would need
additional ownership and resource-lifetime verification. Native admission provides
the event control lock; camera trimming supplies the letterbox framing.

Completion, timeout, interruption and state load release the scene's ownership.
Non-natural exits reset only the scene-owned message through the native message
core, so restoring control does not require another A press.
The lease snapshot is stored in state-captured runtime memory so loading an older
mid-scene state can restore control even after the host player has finished that
scene. The port checks ownership before resetting anything: an unrelated native
event must retain its camera and control. Press **Plus/Start** during a scene to skip it; the skip press is consumed so it
does not also open the pause menu. A/B remain available for the message window.
The scene route remains experimental; native event-list generation is not part
of this preview.
