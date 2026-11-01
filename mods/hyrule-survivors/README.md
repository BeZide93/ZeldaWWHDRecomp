# Hyrule Survivors — Savage Labyrinth prototype

An experimental, standalone **ZeldaWWHDRecomp guest mod**. It starts a six-wave
Survivors expedition in a separate copy of **Savage Labyrinth floor 1
(`Cave09/Room1`)**. The original room's geometry, collision and native enemy
assets come from the player's installed game. This repository contains only
source code and configuration; no game models, textures or map archives.

This is a first playable candidate, **not yet verified inside the game**. The
build validates the simulation, stage manifest, PowerPC compilation, and USA/EU
translation. These checks cannot establish native enemy behavior, collision,
camera behavior, performance or save restoration in an actual game session.

## Install and start

1. Use a current ZeldaWWHDRecomp build with guest mods, custom stages, live
   touch/input services and the vector HUD from this repository.
2. Download `hyrule-survivors-wwhdmod` from the **Hyrule Survivors mod** GitHub
   Actions run and extract the outer artifact ZIP. The contained
   `hyrule-survivors-0.1.0.wwhdmod` is the installable package.
3. In **Mods → Installed packages**, choose the package, install it and enable
   it. Allow the normal setup/rebuild and restart the game when requested.
4. Load a Quest Log and stand in an ordinary playable area. Tap/click
   **HYRULE SURVIVORS**, or press **L + R + Minus**. Alternatively, switch on
   **Open Survivors** in this mod's options, then close the menu.
5. Choose **Begin expedition**. After the floor check, press **A** to start.

Only the recompilation is supported: this package does not install into a Wii U
or Cemu. No Dawnlight dependency or direct Dawnlight binary port is included.
Use a backed-up Quest Log for this untested first candidate.

## Included gameplay

| Wave | Enemies | Count |
| --- | --- | ---: |
| Wings in the dark | Keese | 12 |
| Emerald tide | ChuChus | 16 |
| Little devils | Miniblins | 20 |
| Bokoblin assault | Bokoblins | 24 |
| Moblin guard | Moblins | 28 |
| The labyrinth champion | Mixed enemies, then an elite Moblin | 13 |

The run ends in victory after all **113 enemies** are defeated. At most **24
native enemies**, including pending creations and removals, can occupy the
pool. The champion is an ordinary native Moblin with **160 Survivors HP**;
it has a boss health bar, but no new model or AI. Every cleared intermediate
wave heals one heart and collects remaining EXP.

- **Spirit Sword:** automatic radial strikes around Link.
- **Hero's Bow:** automatically aims at the nearest enemy; higher ranks pierce.
- **Boomerang:** hits on its outward and return journeys.
- **Bombs:** timed area damage around the targeted location.
- **EXP and levels:** collect green orbs; choose one of three upgrades while
  combat is paused. Weapon ranks, damage, cooldown, reach, pickup radius,
  heart containers and healing are available. The first three level-ups each
  offer one new weapon; choosing it remains optional.
- **HUD:** health, EXP, level, wave, timer, enemy count, kills, weapon ranks,
  champion HP, pause, results and per-weapon damage totals.
- **Results:** victory/defeat, retry, return to the starting location, persistent
  best kill count and victory count in the mod's own `Data/records-v1.bin`.

The four weapons use the mod's own hit/HP rules and screen-projected vector
effects. They are **not native sword/bow animations or newly imported 3D
weapons**. Arrows aim when fired, rather than homing afterward. Native enemies
retain their normal movement and attacks; ordinary manual sword/item attacks
are disabled during the run. Projectiles currently do not collide with room
walls, and their overlay visuals are not depth-occluded by the world.

## Controls

| Action | Input |
| --- | --- |
| Move / camera / roll / lock-on | Normal game controls |
| Open expedition menu | L + R + Minus, HUD button, or mod option |
| Start / resume / confirm upgrade / retry | A |
| Choose upgrade | D-pad or left stick left/right; tap a card |
| Pause | Plus or HUD Pause button |
| Resume from pause | A or B |
| Return from ready/results | B or Return button |
| Return while paused | Return button |

The HUD labels are English in this initial version. Health is displayed in
quarter-heart units: **24 = six hearts**. Touch, mouse and controller selection
share the same action path. The HUD layout uses the central 1280×720 canvas.

## Run isolation

On entry, the adapter snapshots the story save payload, Quest Log slot and
return position. The run has its own health, weapons, EXP and upgrades. On a
normal return, it restores the payload and returns Link to the entry location.
During a run, the native serializer is wrapped so that a save request writes
the pre-run story payload. Loading a Quest Log cancels the old snapshot,
including when reloading the same slot.

This is not a full snapshot of every transient scene/event object. Enter from
ordinary gameplay; avoid active quests/cutscenes or other mods that alter the
same actors, save hooks or scene transitions. Full recomp save states also
rewind the mod's memory; the separate record file is not rewound, so it is a
casual local record rather than a replay-proof leaderboard.

The arena clones the room through the existing stage builder. The adapter
checks connected floor samples before starting and uses them as spawn points.
Creation, disappearance and cleanup failures lead to an interruption screen
instead of silently granting kills or leaving an unwinnable wave. Geometry
and native floor queries still need the in-game check below.

## Build and checks

From the repository root on Linux, with Python 3, a C/C++ compiler, Clang with
PowerPC support, and LLD:

```sh
python3 mods/hyrule-survivors/build.py
```

Output: `build/hyrule-survivors/hyrule-survivors-0.1.0.wwhdmod`.
`CC`, `CXX`, `WWHD_PPC_CLANG`, and `WWHD_PPC_LLD` select compilers.
The translated host module requires Clang; `WWHD_HOST_CC` overrides its path
(otherwise the same Clang executable builds both targets).
`--tests-only` runs the game-independent checks without the PowerPC tools.

The build runs behavioral tests for pause/grace/defeat, bounded native actor
lifetime, pending deletion, spawn failures, EXP overflow and duplicate rewards,
upgrade selection, swept arrow hits, returning boomerangs, bombs, all six waves,
victory and restart. It also parses the manifest with the runtime's actual
stage parser and checks the generated stage-name agreement. Finally it builds
the guest ELF and translates/compiles it for **USA and EU** using the actual
install-time translator. A synthetic host executes both translated modules to
check entry, floor validation, input capture, every HUD screen, damage routing,
save serialization, normal return and Quest Log isolation. It supplies stubbed
native game calls, so this is an adapter test, not an in-game test. A game dump
is not used by these checks. This test harness currently requires Linux;
the resulting guest package uses the cross-platform runtime ABI.

Source layout: `core.c` owns simulation; `game.c` adapts native actors, input,
damage, scene transitions and save handling; `hud.c` draws the vector UI;
`manifest.json` declares the borrowed room and options. The generated stage
name uses the runtime allocator's first candidate; a hash collision with
another installed custom stage is not handled by this prototype.

### Required in-game acceptance pass

Before treating this as a stable release, test on a backed-up save:

1. Enter from an ordinary land area; verify floor placement, camera, walls,
   native enemies, the floor-check log and controller/mouse/touch controls.
2. Check each of the five enemy kinds, native attacks and health loss; verify
   that the run remains playable without any story inventory prerequisites.
3. Unlock each weapon; pause during projectiles; select all upgrade categories.
   Check EXP pickups, champion HP and frame rate at the 24-enemy limit.
4. Win, lose and retry repeatedly. Check orphaned enemy weapons, old effects,
   delayed creation and duplicate rewards. Verify both result screens.
5. Return from ready, pause, defeat, victory and an error. Compare story health,
   inventory, flags, Quest Log, position and camera before/after the run.
6. Exercise save requests, title/Quest Log reload, full save-state restore,
   widescreen and both game regions. Confirm the story save stays intact.

These runtime checks are outstanding; compilation and simulation are not a
substitute for them.

License: MPL-2.0, matching the repository. Native game assets are not distributed.
