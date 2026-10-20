# Mod stages

A mod stage is a new place for the game: a cave or a dungeon of its own, with its own rooms, that Link
walks into from a room of the game and leaves through an exit. Your package describes the stage and ships
the rooms' ground models; the port builds the stage on the player's machine from the player's own game
files when the game starts, and the game loads it like any of its own stages.

Mod stages are experimental. A stage has rooms built from your models or taken from the game's rooms,
spawn points, exits, doors (plain, small-key, boss and shutter doors), chests with small keys, the map, the
compass and the boss key, enemies and pots, return warps, ways in from the game's rooms, save flags of its
own and a dungeon map.

A larger example, a three-room dungeon with a locked door, a key in a chest and an enemy, is
`examples/guest-mods/mod-stage-dungeon` in the repository.

## A first stage

This package adds a chamber behind a hidden passage of the lava cave on the Great Sea's square 34. Its exit
leads back to the sea outside the cave (`examples/guest-mods/mod-stage-cave` in the repository):

```json
{
  "format_version": 1,
  "id": "mod-stage-cave",
  "name": "A room behind the lava cave",
  "version": "0.1.0",
  "game_id": "wwhd",
  "kind": "content",
  "content_dir": "content",
  "experimental": true,
  "stages": [
    {
      "id": "chamber",
      "template": "Cave01",
      "kind": "cave",
      "music_like": "Cave01",
      "entrance": {"stage": "sea", "room": 34, "point": 1},
      "rooms": [
        {
          "room": 0,
          "kit": 0,
          "ground": "assets/room.glb",
          "material": "lambert2_v",
          "materials": {"wall": "E3mR02roomKB_v", "pillar": "E3mR02YK_v"},
          "spawns": [{"point": 0, "position": {"x": -1000, "y": 0, "z": 0}, "angle": 90}],
          "exits": [
            {
              "id": "to-sea",
              "box": {"min": {"x": 900, "y": -100, "z": -800}, "max": {"x": 1200, "y": 100, "z": 800}},
              "to": {"stage": "sea", "room": 34, "point": 1}
            }
          ]
        }
      ],
      "entries": [
        {
          "id": "hidden-passage",
          "stage": "Cave01",
          "room": 0,
          "position": {"x": -1300, "y": -200, "z": -900},
          "size": {"x": 200, "y": 200, "z": 200},
          "to": {"room": 0, "point": 0}
        }
      ]
    }
  ]
}
```

Link walks into the entry's box in the lava cave and arrives at spawn point 0 of the chamber. Walking onto
the floor at the chamber's far end takes him back to the sea.

A package with stages needs no other package: stages are not story content.

## The stage

| Field | |
|---|---|
| `id` | The package's name for the stage (lowercase letters, digits, `-`, `_`, `.`; up to 32). The game gets a name of its own for it from the port; you never need it. |
| `template` | A stage of the game the new stage takes its lighting, camera, door models and room kits from. Its rooms are not part of the new stage. |
| `kind` | `cave` (the default) or `dungeon`. |
| `save_group` | Optional: stages with the same `save_group` share their save flags, keys and dungeon items (a dungeon and its boss stage, say). Default: the stage's own id. |
| `music_like` | Optional: a stage of the game whose music and ambient sounds the stage plays. The boss rooms' music follows the story and can't be borrowed. Without it the stage is silent apart from battle music. |
| `entrance` | A place of the game: `stage`, `room` and spawn `point`. A save made in the stage continues here when the package is not installed (see [Saves](#saves)). Usually the spot in front of the way in. |
| `rooms` | 1 to 16 rooms, numbered 0, 1, 2, ... in order. |
| `doors` | Optional, up to 16: the game's doors between two rooms (see [Doors](#doors)). |
| `joins` | Optional: pairs of rooms joined by an open passage without a door, `[[2, 3]]`. Both rooms are kept loaded while Link is in either. |
| `entries` | Optional, up to 4: boxes in rooms of the game that take Link into the stage. |
| `warps` | Optional, up to 4: return warps (see [Return warps](#return-warps)). |
| `map` | Optional: `{"title": "Hidden Keep"}` gives the stage a dungeon map (see [The dungeon map](#the-dungeon-map)); the title (up to 40 characters) shows above its floors. Without it the map screen shows the sea chart, as in the game's caves. |

Every room must be reachable: from where Link comes in (the entries' rooms, room 0 without entries) through
doors, joins and exits. Check names a room that can't be reached.

A package declares up to 4 stages.

### Rooms

| Field | |
|---|---|
| `room` | The room's number: 0 for the first, then 1, 2, ... |
| `kit` | A room of the game whose materials, textures and light the room uses: a room number of the template, or `{"stage": "M_NewD2", "room": 3}` for a room of any stage. Default: the template's room of the room's own number. |
| `ground` | The room's model: a `.glb` file of the package (see [The ground model](#the-ground-model)), or `"kit"`: the kit room itself, its models and its collision, as the game has it. |
| `material` | Optional: the kit room's material the model is drawn with. Default: the kit room's first. |
| `materials` | Optional: the model's material names mapped to kit materials, for a model with several materials. A model material that is also a kit material's name is drawn with that one. |
| `spawns` | 1 to 8 spawn points: `point` (0 to 99, unique in the stage), `position`, `angle` in degrees (0 faces +z, 90 faces +x) and `start`: `stand` (the default) or `fall` (Link drops in from above). |
| `exits` | Up to 8 exits: an `id`, a `box` (`min` and `max` corners) and the place it leads `to`. |
| `actors` | Up to 16 of the game's actors (see [Actors](#actors)). |
| `floor` | Optional: the dungeon map's floor the room is on: 1 (1F, the default) to 5, or -1 (B1) to -5. |

An exit's box marks floor: the ground's floor triangles whose middle lies in the box take Link to the exit's
place when he steps on them, the way the game's own caves are left. Walls and ceilings in the box don't
count, and an exit whose box covers no floor is an error. Make the box a little taller than the floor so
uneven ground stays inside.

A place `to` is:

- a place of the game: `{"stage": "sea", "room": 34, "point": 1}`;
- a spawn of the same stage: `{"room": 1, "point": 3}` (no `stage`);
- a spawn of another of the package's stages: `{"stage": "stage:<id>", "room": 0, "point": 0}`.

A kit room used as it is (`"ground": "kit"`) keeps its floors, walls and its surfaces (lava hurts, water
is swimmable); its own ways out of its stage are removed, and the room's `exits` boxes work on its floor as
on a model's. Its actors are not taken over: place the ones you want with `actors`. A kit room of another
stage than the template is lit by the template's first room's light.

### Doors

```json
{"position": {"x": -1000, "y": 0, "z": 0}, "angle": 90, "rooms": [0, 1], "lock": "small_key"}
```

A door stands at `position`, turned by `angle`, between the two `rooms` it joins. Leave an opening in both
rooms' ground models where the door stands (the door is 3 m wide and 3 m high). The port lists each room's
door neighbours in the stage's room table, which the door needs to open. `lock`:

- `none` (the default): the door opens when Link walks up to it and presses A.
- `small_key`: the door is locked; a small key opens it for good (the key is used up).
- `boss`: the boss door; the stage's boss key opens it.
- `shutter`: bars close the door until switch `switch` (0 to 63) of the stage is on.

The template's stage must have the door's model: Cave01, M_NewD2, kindan and Siren have all four; a stage with a door its template lacks is left out with a message.
A small-key door needs a small key in a chest of the stage's save group, and a boss door the boss key; Check
says so when one is missing. The key counter shows in a stage with small keys.

### Actors

```json
{"id": "key", "actor": "chest", "item": "small_key", "position": {"x": -300, "y": -200, "z": 0}, "angle": 270}
```

A room's `actors` are the game's own actors, created by the game when the room loads:

| `actor` | |
|---|---|
| `chest` | `item`: `small_key` (in a small chest), `map` or `compass` (in a larger one) or `boss_key` (in the big chest). Link opens it from its front (the side `angle` faces). Each chest opens once per save. |
| `chuchu` | `variant` `green` (default), `red`, `blue`, `yellow` or `black`. |
| `bokoblin` | `variant` `stick` (default), `shield` or `machete`. |
| `keese` | `variant` `normal` (default) or `fire`. |
| `darknut` | `variant` `darknut` (default), `shield`, `helmet` or `mighty`. |
| `pot`, `big_pot` | A pot Link can lift and throw. |

`id` names the actor in the manifest (unique in the room), `position` and `angle` (degrees about the vertical
axis) place it. The chests are numbered in the order they appear in the manifest, per save group, and a save
remembers them by number: when you change a released package, add new chests after the old ones.

### Return warps

```json
"warps": [{"id": "home", "room": 2, "position": {"x": -2450, "y": -200, "z": 1500}, "size": {"x": 200, "y": 200, "z": 200}}]
```

A warp is a box in a room of the stage: when Link walks into it, the warp light takes him to `to` (a place,
as for an exit; by default the stage's `entrance`). Like an entry, it works once Link has been outside it.

### Entries

An entry is a box in a room of the game: `stage`, `room`, `position` (the middle of the box's bottom) and
`size` (10 to 5000 units each way). When Link walks into it, the game takes him to the stage's spawn `to`
(`room` and `point`). An entry only works once Link has been outside its box since he came into the room, so
arriving inside one never sends him away again at once.

## The ground model

A room's ground model is one `.glb` file (glTF 2.0, up to 16 MiB). As for [world objects](world-objects.md),
one metre in the file is 100 game units. The room's coordinates are the stage's: put the room where you
want it around the origin, and make the spawns, exits and doors fit it.

- **Visible meshes** are drawn with the kit room's materials and textures; your model's texture coordinates
  (and vertex colours, when it has them) are used, its own textures are not. Up to 20000 visible triangles per
  room, with materials from up to four of the kit room's models.
- **Collision**: meshes or nodes whose name ends in `_col` are the room's collision and are not drawn. Their
  material names choose the surface, as for world objects: `col_stone`, `col_grass`, `col_wood`, `col_sand`,
  `col_metal`, `col_wall`, `col_objects`, `col_climb`, `col_hookshot`, `col_water` (swimmable), `col_shallow`,
  `col_waterfall`, `col_ice`, `col_void`. Without a `_col` mesh, the visible triangles are the collision, on
  stone (or on the surface a `col_` material name gives). Up to 20000 collision triangles per room.
- Faces are seen from the side their normal points to; for a room seen from inside, the walls and ceiling
  face inwards.

### Kit materials

The materials are the template's own: their names are those of the kit room's model. When a name is wrong,
the game's log says which ones the kit room has (`the kit room has no material "..." (it has: ...)`), so
starting with any name and reading the log is a quick way to find them.

## The dungeon map

A stage with `map` has a dungeon map on the map screen (the GamePad's MAP, or the menu's MAP tab with the
Pro Controller). The port draws it from the rooms' floors, seen from above, on the game's dungeon map page:

- the rooms Link has been in, and the room he is in, with his place and the way he faces;
- with the stage's dungeon map (a chest with `"item": "map"`): every room, the ones he hasn't been in lighter;
- with the compass: the chests (gold while closed, grey once open) and the boss door (a red mark);
- the title, and each floor's name on the floor buttons, the floor Link is on marked.

The map shows the floor Link is on; the floor buttons don't switch floors yet. Higher ground shows lighter
and pits and basins darker, and a room's edge is drawn where its floor ends.

## Saves

The game's save file never names a mod stage. When the player saves in the stage, the save file stores the
stage's `entrance`, so the file always loads, with or without the package. Where in the stage the save was
made is kept in the port's save area for mods (with story progress, after the game's own bytes): loading
that Quest Log while the package is installed continues in the stage, at the spawn Link came in by.
Without the package, or when the stage has changed so that spawn is gone, it continues at the entrance.
Continuing inside the stage needs the game built with code-mod support (the same as story progress);
otherwise the Quest Log continues at the entrance.

Each save group keeps its own flags, as each of the game's dungeons does: which chests are open and which
doors unlocked, the switches, the rooms Link has seen, the small keys and the map, compass and boss key. They
are kept in the port's save area for mods and saved with the game's saves; the save file's own table of
stage flags is never written for a mod stage. Without code-mod support they are kept until the game is
closed. A save state made in the stage carries them; a portable save state (the editor's) keeps the stage
flags the game has when it is loaded.

## Save states

Save states made in a mod stage load only while the package that makes it is installed; otherwise the
state is refused with a message and nothing changes. The slot list names the stage by its id and package.

## Building

The port builds each stage when the game starts, from the player's game files (the template's stage
archive and the kit rooms) and your models, and keeps the result in the mod manager's data folder. A stage
is built again only when the package, the player's game files or the port's builder change; otherwise the
kept one is used. A stage the player's game can't make (another game version without the template, a
wrong kit material, an exit box that covers no floor) is left out with the reason in the game's log; the
rest of the package works.

Nothing from the game goes into a package: ship your models, the manifest and your own files only.

## Editor

With `--editor`, the warp list (`list_locations`) has a group **Mod stages** with every room of the
installed packages' stages, named by the stage's id and package. The `warp` command takes a package's stage
as `"stage": "stage:<package id>/<stage id>"`, for example
`{"command": "warp", "params": {"stage": "stage:mod-stage-cave/chamber", "room": 0, "point": 0}}`.

## Limits

- The dungeon map shows only the floor Link is on, and the map can't be zoomed or moved by touch.
- Only the actors listed under [Actors](#actors); no boss yet, and no actors of a kit room's own.
- The stage's rooms are lit by the template; a sky only comes with a template that has one.
- Ballad of Gales and the blue warp don't lead to or from a mod stage.
