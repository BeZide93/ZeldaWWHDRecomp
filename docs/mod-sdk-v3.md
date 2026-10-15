# Actors and scenes (SDK v3 preview)

**Draft preview.** This is an experimental extension to [SDK v2](mod-sdk-v2.md).
The implementation and its game proof are still being checked. Custom actor types,
models and guest behaviour callbacks are not available in this phase 1 draft.

The worked package is [sdk-v3-windfall](../examples/guest-mods/sdk-v3-windfall/manifest.json).
It contains original text and names existing game actors; it includes no game
resources. It requires the Story Notebook package and a build with code-mod
support. The example is a data-only content package: its empty content directory
has a placeholder so the package manager can index it. World declarations are
read at startup; enabling or changing a package requires a restart.

## Placing actors

`actors` is an array. Each actor has an `id`, `actor`, `stage`, `room`, and
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
Proof and remaining limits are recorded in the phase report.
