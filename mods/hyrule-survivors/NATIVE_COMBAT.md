# Native combat integration (0.2.2)

This adapter replaces the prototype's vector attacks with the game's own Link
animations, sword collision and item actors. It does not ship game assets.
The 0.1.0 room/run was reported working by the user; 0.2.2's native-combat fixes need
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
| Bomb | `makeItemType` creates a held bomb; `procGrabThrow_init` (`023E9E48`), proc `71`, plays the full throw. The game releases it at the animation's normal frame and owns velocity, fuse and explosion. Run bombs additionally explode on ground contact and cannot hit Link. |

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
treated as proof that native creation has finished. The next core tick grants a reward once. For Moblins, a separate `HS_DYING`
state retains the pool slot and waits for native deletion instead of calling
`fopAcM_delete` at the fatal hit. Other enemy kinds retain their prior removal
behavior. The Moblin's own reaction stage reconciles a fatal hit after native
boomerang code restores vanilla health.

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
bomb ground impact/self-hit exclusion, upgrades mid-flight, and leaving with live items.
Also compare story inventory before/after entry and return, including a Quest
Log without the sword, bow or bomb bag. Timing and balance may need adjustment
after this pass; the automated checks cannot establish these properties.

## Native-combat corrections in 0.2.1

- A native throw marks its tracked boomerang as released. `returnBoomerang`
  (`02442ED8`) later transfers that same actor from the thrown keep back into
  the equipped keep, often after the throw scheduler is already idle. Cleanup
  now runs independently of that scheduler: it waits for Link's pending catch
  flag (`3B8 & 20`), upper catch animation (`33`) and full-body catch proc (`82`)
  to finish before calling native `deleteEquipItem`. It never deletes a newly
  created, not-yet-thrown boomerang through this return path.
- Native `checkNextMode` treats the upper bow/boomerang animation as an aiming
  request even without item input. `checkNextBowMode` (`023E8984`) and
  `checkNextBoomerangMode` (`023E8F2C`) now decline that transition only for our
  active/held/thrown automatic items. Native loading, release and catch still
  execute; ordinary item aiming outside the run passes through unchanged.
- `checkExplodeBg_norm` (`020C7C34`) runs after native `bgCrrPos` and before
  bouncing. A tracked free bomb with `mAcch.m_flags & GROUND_HIT` calls native
  `procExplode_init` (`020C5EF8`). The flags are at `67C + 28`; `GROUND_HIT` is
  `20`, and actor carry status (`2E0 & 2000`) excludes held bombs. Wall/ceiling
  contacts alone retain normal behavior. Native proc switching prevents
  repeated detonation and the ordinary fuse still provides its normal fallback.
- Collision rejection happens in `cCcS::ChkNoHitAtTg` (`0200CE78`) before hit
  registration. Only a tracked run bomb attacking the current Link returns
  no-hit; bomb-to-enemy, enemy-to-Link and untracked bomb pairs pass through the
  original function. This removes self-damage and hit reactions without blanket
  immunity or suppressing unrelated simultaneous enemy attacks. Actor ownership
  is read from the collider's `mStts` (`44`) and status `mp_actor` (`0C`).

Both translated-region test hosts exercise these regression paths. Actual
camera appearance, animation timing and collision geometry still require the
in-game pass described above.

## Moblin death lifecycle correction in 0.2.2

The reported Android crash while defeating a Moblin follows `mo2_class_dt`
(`021D3C90`), `mDoExt_3DlineMat0`/line destructors (`025E99E0`, `025E91C8`),
array delete (`0273AFC8`) and the heap's unlink path (`0273B310`), with an
invalid access at `00000005`. This locates the failing teardown, not the
original writer or a proven double free. No heap guard or destructor bypass
is installed, and this build must not be described as a reproduced crash fix.

Previously, observing native HP zero or Survivors HP zero immediately enqueued
actor deletion, cutting off the Moblin's death reaction. Now its kill/EXP is
granted once and its state becomes `HS_DYING`. The actor continues executing
while the run is active. It is not targetable, cannot grant another reward,
and retains its slot until lookup confirms native deletion. Wave transitions
and victory also wait for that deletion. Upgrade/pause freezes its animation
and timeout together. After 30 active seconds without completion the run
shows an error; it does not force a destructor as a timeout fallback. Explicit
return/retry still uses the existing scene cleanup path.

Moblin `damage_check` restores the old native HP after boomerang hits. The
`damage_reaction` entry (`02041F94`) is therefore wrapped: only a tracked
Moblin's exact reaction block (`actor + 08BC`, whose first word is `mpEnemy`)
with zero Survivors HP gets native HP zero before the original reaction runs.
This occurs in the same native execute after the restore. Other actors and
living targets pass through. Late tracked contacts never stage a dying enemy
back at 100 HP. Reaction modes and graphics ownership remain native.

Core and both translated-adapter tests cover delayed native disappearance,
slot capacity, pause/upgrade, timeout, champion victory, repeated contacts,
boomerang HP restoration and unrelated-actor isolation. The adapter emits
three short actor-ID messages around a native fatal hit, waiting for death,
and completed removal. The reported Android heap failure still requires an
in-game retest; if it persists, the earlier allocation/free history is needed
to identify the actual corruption.

## Repeated crash: construction pressure and diagnostics (0.2.3)

The second Android log (`2026-11-02 07:55:56`) faults at guest `000000AC`
inside `02752A84`, reached through packet constructors `027F1278` and
`027F1534`, model setup `027F4098`/`027F4404`, model creation `025E38E0`,
and Moblin `createHeap` `021CC524` under `entrySolidHeap`.
This is a construction stack, not the first log's destruction/unlink stack.
It is consistent with an invalid construction pointer, potentially from a
failed allocation; the log does not prove exhaustion, the faulty writer, or
that both crashes share a cause.

Previously wave 5 could keep 24 native Moblins resident. The generic tracking
capacity was being used as the only admission limit, without a measured model
memory budget. Version 0.2.3 conservatively limits Moblins to four, including
pending creation, native death and queued removal. The selected next spawn
waits without consuming its quota or accumulating spawn failures. Combat and
cooldowns continue during that wait. The champion follows the same rule;
other enemy types keep the existing overall limit. Four is a mitigation limit,
not a hardware-derived safe memory budget or a confirmed root-cause fix.

An owned-Moblin-only wrapper around `021CC524` logs begin/return and reads the
current native heap through `02756140`. It queries the heap vtable's native
`getFreeSize` slot `+0x74` before and after the original callback. Heap/vtable
and function pointer ranges are checked. It performs no allocation probes,
heap switching, forced deletion, or destructor bypass. The original callback
still runs exactly once and its success/failure result is preserved. It also
runs normally for unrelated Moblins and outside the arena.

Regression coverage includes pending/alive/dying/retiring admission, continuing
attacks while admission waits, champion admission and the complete 113-enemy
run. Both translated regions test the diagnostic wrapper's ownership, ABI,
callback return values and invalid-pointer skips. These tests stub the native
allocator; they cannot reproduce native model allocation or heap corruption.
For the next device test, verify **0.2.3** in the expedition menu and start a
new run. If the crash persists, collect the full runtime log as well as the
crash log so the last construction/free-byte measurements are available.
