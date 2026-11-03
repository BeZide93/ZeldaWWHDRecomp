# Enemy HP Bars

A standalone `enemy-hp-bars-0.1.1.wwhdmod` for ZeldaWWHDRecomp. Displays floating
health bars and rising, fading damage numbers on the main game picture. It reads
enemy health; it does not change combat, difficulty, drops or save data. No
textures, images, models or game files are included.

## Install

Download the **enemy-hp-bars-wwhdmod** artifact from the **Enemy HP Bars mod**
GitHub Actions run. Extract the artifact ZIP and install the `.wwhdmod` through
**Settings → Mods**, enable it and complete the code-mod setup if requested.
Requires mod manager 1.6.0 or later. The same guest package targets USA and EU.

## Settings

All options are live and also appear on the in-game Mods settings page.

| Setting | Default |
| --- | --- |
| Enable Enemy HP Bars | On |
| Show HP bars | On |
| Show damage numbers | On |
| HP bar visibility | Always; alternatively four seconds after damage |
| Show current / maximum HP | Off |
| Display size | 100%; adjustable from 50–200% |
| Maximum distance | 2,000 game units; adjustable from 300–6,000 |

Bars turn green, yellow and red as HP decrease. Damage numbers last one second.
Numbers are **actual native HP lost**, capped at remaining HP on a lethal hit,
not hearts or theoretical weapon damage. Hits within one enemy update can be
combined. Healing, blocked hits and a stun that restores HP before the update
finishes do not create damage numbers. Environmental HP loss is also shown.

## Coverage and limits

Initial coverage: ChuChu, Bokoblin, Moblin, Keese, Miniblin, Peahat, Rat,
Darknut, Boko Baba, Bubble, ordinary Octorok, Magtail, Morth, Mothula, ReDead,
Poe, Armos, Armos Knight, Kargaroc, Wizzrobe and Floormaster. Shared-class
Octorok rocks, Baba Buds and Wizzrobe spells are excluded.

Bosses and Stalfos's separate body/parts health are not handled. Only retail
enemy profiles are observed; custom enemies with their own HP need an adapter.
In particular, **Hyrule Survivors 0.2.x is deliberately hidden in its generated
labyrinth arena (`Znpfl81`)**: that prototype keeps native HP at a sentinel of
100 and stores real run HP privately. Showing those native values would be
incorrect. Both mods can be installed; this version displays native enemies
outside the Survivors arena, and does not provide Survivors HP integration.

Bars use the enemy's eye position with a per-type height fallback. This is a
screen overlay, not depth-tested world geometry; it can appear through walls.
Behind-camera points are rejected, and drawing is clipped by the viewport.
Only the current room is tracked. Cutscenes, transitions and native menu flags
hide/clear the display. Room changes, Quest Log loads and full-state epochs
reset it without producing phantom damage. Maximum HP uses the native maximum
when available, otherwise the highest health observed for that actor.

At most 128 enemy snapshots, 32 nearest bars and 64 damage numbers are kept.
Popups store positions, never actor pointers, and can safely finish after an
enemy is deleted. All drawing stays below the host's 1,024-element HUD budget.
Only entry/return hooks are installed; no game functions are replaced.

## Build and validation

On Linux, install Python 3, a C compiler, PowerPC-enabled Clang and LLD:

```sh
python mods/enemy-hp-bars/build.py
```

Output: `build/enemy-hp-bars/enemy-hp-bars-0.1.1.wwhdmod`.
Override `WWHD_PPC_CLANG`, `WWHD_PPC_LLD`, `CC` or `WWHD_HOST_CC` if needed.
`--tests-only` runs the portable tracking tests without Clang or game files.

The full build compiles PowerPC, translates it using the real install-time
translator for USA and EU, compiles both host modules and runs the shipped code
against synthetic game memory and HUD services. Tests cover damage accounting,
overkill, actor identity reuse/deletion, bounded pools, live options, projection,
room/event/load resets, Survivors suppression and unchanged native memory.
The phase-order regression follows the real host: actor execution, publication
of the new logic counter, then the HUD callback. Both that completed pass and
same-counter callbacks can draw; stale, rewound or different-epoch data cannot.
These checks do not replace in-game testing of enemy behavior and positioning.

## Changes in 0.1.1

Fix the invisible overlay: the host publishes its new logic counter after the
enemy hooks run and before drawing the HUD. Version 0.1.0 incorrectly required
the enemy snapshot and HUD counters to be identical, rejecting every normal
completed pass. Existing settings are preserved when updating the package.

Native method/profile IDs come from the repository's `story_enemies.cpp`,
`story_enemy_ownership.cpp` and `story_enemies_more.h`. Layouts and region
relocations use the existing SDK. Source license: MPL-2.0, as the repository.
