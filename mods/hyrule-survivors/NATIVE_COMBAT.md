# Native combat integration (0.2.0)

This adapter replaces the prototype's vector attacks with the game's own Link
animations, sword collision and item actors. It does not ship game assets.
The 0.1.0 room/run was reported working by the user; 0.2.0's new combat needs
in-game acceptance testing. Synthetic host tests verify dispatch and ownership,
not the appearance or complete behavior of the actual game functions.

## Reference and address convention

Entry points and layouts were checked against the public
[WWHD decompilation](https://github.com/ZeldaWWHDDecomp/wwhd/tree/b2811a3ce838d1f875bc8e68536c1950af5d7ffc/wwhd_src),
especially `d/actor/d_a_player_main.cpp`, its player implementation files,
`d_a_arrow.cpp`, `d_a_boomerang.cpp`, `d_a_bomb.cpp`, the enemy damage paths,
and the associated headers. The existing guest SDK supplies function bindings
and Link/actor types. Addresses below are the SDK's canonical USA addresses;
the install-time translator maps code/data for USA or EU. Never use these as
untranslated EU addresses.

| Ability | Native entry points and behavior |
| --- | --- |
| Spin Attack | `procCutTurn_init` (`023E6380`), `setSwordModel` (`023DE0B4`), proc `55`. The normal spin drives its animation, sword cylinder and effects. Reach scales `m35A4`, the spin's target cylinder radius. |
| Hero's Bow | `makeItemType` (`023DF600`), `setBowReloadAnime` (`023EB080`), `makeArrow` (`023EB120`). Normal ready-action processing progresses upper animations `0C → 36 → 0E` and releases the arrow. No first-person aiming proc is entered. |
| Boomerang | `makeItemType`, upper `BOOMTHROW` (`34`) via `setActAnimeUpper` (`023DE7E8`). Native item-action processing calls `throwBoomerang` (`023FB10C`) at frame 7. It uses the selected enemy as its temporary native lock target, then restores the prior lock pointer. |
| Bomb | `makeItemType` creates a held bomb; `procGrabThrow_init` (`023E9E48`), proc `71`, plays the full throw. The game releases it at the animation's normal frame and owns velocity, fuse, explosion and self-damage. |

`arrowShooting` (`02053814`) receives the selected enemy's current eye position
before native launch velocity and collision setup. This adjusts the real arrow's
launch direction, without a separate simulated projectile or homing update.
The shared sphere-radius setter (`02018C8C`) is modified only for a tracked
bomb's explosion sphere: bomb `+920`, geometry `+118`, native radius `200`.

## Ownership, timing and damage

The scheduler gives ready weapons a round-robin opportunity. An accepted attack
starts its cooldown; a blocked attack remains ready. Only one Link attack
animation is started at a time. Walking/idle procs are allowed; rolling,
damage, falling, carrying another object and other actions defer scheduling.
Native animation recovery still limits the effective firing rate.

Up to 16 item actors are tracked by pointer, process ID and expected profile:
arrow `1D8`, boomerang `1B0`, bomb `126`. Link's equipped, thrown and grabbed
actor keeps are at `6590`, `6598`, `65A0`; his lock pointer is at `65B0`.
The adapter refuses a second boomerang while one is returning and respects
the native three-player-bomb limit. Items are removed after 15 run seconds
if still present, preventing embedded arrows from filling the pool forever.
The HD boomerang aim flag (`827C`) is cleared on throw setup, and the throw
heading (`827E`) is saved for the native catch animation.

`cc_at_check` (`025192A8`) is intercepted only when both the target belongs to
this run and the collision source is a tracked item or the current spin.
Native defenses decide whether execution reaches this damage path. The native
function still runs for reactions/effects. Survivors damage is credited once
per attack and enemy generation; reusing an enemy slot permits a fresh hit.
Native signed-byte health is staged at 100 and then synchronized to alive/dead,
so the champion's 160 Survivors HP cannot overflow it. A pending enemy hit is
treated as proof that native creation has finished. Reward/deletion remains
owned by the next core tick, including native death or boomerang paths that
restore vanilla health after `cc_at_check`.

Native stun-only branches can bypass damage, notably some ChuChu/boomerang
interactions. Native walls, shields and misses apply. The previous synthetic
arrow piercing and guaranteed return-leg boomerang damage are intentionally
absent. There is no proximity-damage fallback.

## Pause, exit and temporary equipment

Owned arrow (`02053184`), boomerang (`020CC454`) and bomb (`020C506C`) execute
wrappers pause with the run. Ordinary scene projectiles pass through unchanged.
Results, retry and return delete tracked items and clear owned actor keeps.
Record lookup checks ID/profile so stale pointers cannot delete a different
actor. Scene teardown resets the records after the old room is unloaded.

The pre-run save snapshot is captured before temporary Hero's Sword selection
and arrow/bomb provisioning. Save-payload offsets `0E`, `69`, `6A`, `6F`, `70`
are inside that snapshot. Pending arrow/bomb deltas at gameinfo `5B68`/`5B6C`
are cleared before restoring story inventory. Existing serializer isolation
continues to write the pre-run payload during an expedition.

## Verification boundary

The build compiles the PowerPC ELF, translates it for both supported regions,
and executes both translated modules with stubbed native calls. Tests check
all four dispatch paths, busy/action gates, native launch aiming, temporary
boomerang target restoration, exact bomb-radius ownership, real-contact-only
damage, hit deduplication, slot reuse, scaled/fatal HP, pause behavior, item
creation failures and exit cleanup. Core tests also cover all six waves and
the existing progression/return tests remain in place.

Before accepting the new combat in-game, check bow release while walking,
boomerang catches during another ability, spin reach, native enemy defenses,
bomb throwing/self-damage, upgrades mid-flight, and leaving with live items.
Also compare story inventory before/after entry and return, including a Quest
Log without the sword, bow or bomb bag. Timing and balance may need adjustment
after this pass; the automated checks cannot establish these properties.
