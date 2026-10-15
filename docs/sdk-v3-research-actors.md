# SDK v3 actor research (internal, work in progress)

This records implementation constraints, not a claim of in-game proof. Phase 2 must
wait for the completed phase 1 commit and its proof report.

## Registration and lifetime

The HD process-profile lookup reads an indexed guest pointer table. A native host
C++ object cannot be placed in that table: the game expects guest addresses,
PowerPC method entries, and its actor allocation and destruction protocol. The
readable `f_pc_profile.cpp`, `f_pc_base.h`, and `f_op_actor_mng.cpp` are the local
references. The GameCube `d_a_npc_people.cpp` profile illustrates the create,
delete, execute, deletion-check and draw method group; its layouts are not HD ABI
evidence.

Proposed route: intercept profile lookup for a reserved process number and return
a port-built guest profile whose method entries dispatch through the existing
guest-call bridge. Keep ordinary process lookup unchanged. Before implementation,
audit every process-number consumer and establish a collision-free number. Using
an existing resident as a stand-in needs isolation of its model resource, dialogue,
save checks and deletion behaviour; replacing a resident globally is unsuitable.

The generic actor must join the game's room layer and deletion queues. Its draw
must submit an HD model through the normal renderer (the retained J3D-named
compatibility classes do not imply GameCube model-file support); collision must register and
remove a game collision cylinder; talk must participate in event ordering and the
native attention prompt. Actor identity needs a generation as well as a process
ID, because room reloads and state loads can reuse IDs.

## Phase 1 actor save boundary audit

The allowlist is deliberately narrow: ordinary chests, cuttable signs and the
Um1 resident profile (argument 9). Unknown flag-setting actor types and unsafe
raw parameter combinations remain validator errors.

- The cuttable sign's native execute/create/delete paths were searched for save
  event/switch/chest/item/actor setters and register setters. None occur in the
  inspected sign units. Its cut pieces and reconstruction state are transient
  actor data. Runtime-controlled parameters select the text carrier; successful
  reading writes only the declared mod flag in story-record kind 17.
- Ordinary chest `checkOpen` (`024B4AB4`) is intercepted by actor process identity
  and reads the mod flag. `OpenInit_com` (`024B63A8`) calls `onTbox`; the scoped
  interception redirects that call to the mod flag without calling the game's
  setter. Its separate home-angle switch is forced to 255 (disabled). The
  additional parameter-switch write in `actionGenocide` belongs to the enemy
  clearance chest variant, which the normal-only parameter policy excludes.
  Chest items are restricted to rupees. This is not an arbitrary chest-variant
  safety claim; future variants need their own setter/control-flow audit.
- Um1's original nighttime `checkOrder` selects a retail event ID. A dialogue
  replacement alone therefore does not isolate the actor: the retail private-cut
  interpreter can still choose scripted messages and write save flags. Narrow
  mod-only hooks force generic talk after native `checkOrder`, block `eventMove`
  from entering retail cuts, and restrict `eventOrder` to generic talk orders.
  Retail item-show/photography callbacks are disabled. The retained native
  no-path Um1 wait/talk/visual-update route has no save setter in the inspected
  control flow. Broad save-memory snapshot/rollback was removed; it would conceal
  writes rather than prevent the unsafe script from running.

Synthetic tests exercise these actual runtime hook boundaries, including a fake
retail event function that writes a save byte: it is never invoked for the mod
resident and remains invoked for an ordinary resident. They also verify native
chest flag isolation and unmodified vanilla setters. Full game-save comparisons
and screenshots are separate acceptance evidence, not implied by this audit.

## Resources and conversion: corrected HD format finding

**The native HD route consumes BFRES models and SHARCFB shaders, not a BMD/BDL
archive produced by a GameCube J3D converter.** Phase 2 as literally specified
(glTF → J3D → native HD model loader) therefore has an incorrect format assumption.
This is an implementation contract mismatch, not evidence that custom HD actors
are impossible. The proposed adaptation is glTF/GLB → Wii U BFRES → Yaz0/SARC,
with player-local shader/material templates, after the phase 1 completion gate.

Local evidence, inspected read-only on 2026-10-15:

- `wt-readable/wwhd_src/d/hd_res_model.cpp`, loader `02602BC8`, formats the
  resource filename as `%s.bfres`; for each model it resolves `%s.sharcfb`, loads
  and prepares the shader archive, then binds it to the resource's materials.
  Model upload and shader binding are separate allocation/lifetime operations.
- `d/d_resorce.cpp`, `ResourcePathInfoHD::loadResource` (`026057F4`), enumerates
  the HD resource header's model and skeletal-animation dictionaries. It exposes
  a model under a legacy `.bmd` lookup name **after** constructing the native
  resource. `loadSkeletalAnimation` (`0260873C`) similarly creates animation
  objects from the archive's skeletal-animation dictionary. Legacy C++ names
  such as `J3DModelData`, `J3DAnmTransform` and `.bmd` aliases cannot establish
  that the underlying payload is a GameCube BMD or BCK file.
- `d_a_npc_people.cpp::createHeap` loads model/animation resources by the
  character's fixed archive IDs, creates morph controllers, and expects head
  and backbone joints. Replacing only a body mesh does not create a compatible
  arbitrary skeleton, animation set or generic actor.
- The repository's [notebook_model.py](../tools/story_mods/notebook_model.py)
  already reads one GLB triangle primitive and patches a **BFRES 3.4.0.1**
  template from the player's own `Object/Vbeso.szs`. It replaces static
  positions/normals/UVs, triangle indices, bounds and one colour texture. It
  preserves the template's skeleton, materials, samplers and shader archive,
  then writes a Yaz0-wrapped SARC. This is a narrow, useful existing static
  pipeline, not a general skeletal or animation converter.

No local game-derived source or data is copied into this repository by this
research. The loader addresses and format/lifetime findings above are descriptions
of the interfaces to audit, not redistributed implementation code.

### Build-time tool candidates

The previously investigated BMD/BDL tools remain relevant only if an independent
J3D loader or a further J3D → BFRES translation stage is deliberately added.
Neither exists here; adding either would be extra phase 2 scope.

| Candidate | Capability | Licence/status finding |
| --- | --- | --- |
| [Existing notebook converter](../tools/story_mods/notebook_model.py) | One static GLB mesh/PNG → patched Wii U BFRES + SARC | Repository code under its MPL-2.0 licence; generated game-derived template/shaders stay player-local. Best bounded starting point. |
| [Switch Toolbox](https://github.com/KillzXGaming/Switch-Toolbox) | Upstream advertises Wii U BFRES import from DAE/FBX/OBJ/CSV, rigged models and animation sections; no direct glTF import listed | [GPL-3.0 licence text](https://github.com/KillzXGaming/Switch-Toolbox/blob/master/LICENSE). Archived Windows/Visual Studio application with separately credited dependencies; audit dependency notices and cross-platform CLI feasibility before reuse. A candidate build-time tool, not a verified WWHD converter. |
| [BfresLibrary](https://github.com/KillzXGaming/BfresLibrary) | Wii U/Switch BFRES reading/writing and shader-parameter access; upstream notes material-conversion profiles are needed | README delegates licensing to Syroot's GitLab licence; that target redirected to sign-in during this audit. Licence grant/dependency terms not independently verified here. Do not vendor until resolved. |
| [BfresPlatformConverter](https://github.com/KillzXGaming/BfresPlatformConverter) | Existing BFRES Wii U/Switch platform conversion; per-game material handlers | [MIT for this repository](https://github.com/KillzXGaming/BfresPlatformConverter/blob/master/LICENSE); BfresLibrary dependency licence still needs verification. Does not establish a glTF → WWHD authoring path. |
| [RiiStudio](https://github.com/snailspeed3/RiiStudio) | BMD import; prior audit found its README limits import to non-rigged models | Prior audit found no project-wide licence grant in the inspected master tree; not approved for reuse. Also wrong native HD output format. |
| [SuperBMD](https://github.com/Sage-of-Mirrors/SuperBMD) / [RenolY2 fork](https://github.com/RenolY2/SuperBMD) | BMD/BDL export and skinning | Prior audit found no licence file in the inspected fork/original roots; not approved for reuse. Also wrong native HD output format. |
| [SuperBMD Blender exporter](https://github.com/Sage-of-Mirrors/superbmd-collada-exporter-2.8) | COLLADA intermediary | Upstream states GPL-2.0-or-later; neither a complete converter nor native HD output. |

Upstream capabilities/licence texts were checked on 2026-10-15. Tool licence
claims above do not license any Nintendo model, texture, skeleton or shader.
A standalone build-time process is the proposed integration boundary; nothing
from these candidates has been incorporated into the runtime by this work.

### Concrete blockers and proposed adaptation

1. **Static model first.** Generalize the existing template patcher with strict
   GLB validation, bounded decoded sizes, reproducible names and cache keys.
   Validate the resulting BFRES using the actual HD loader, then the native
   actor's draw, lighting and shadow paths. A successful get-item model does not
   prove room-actor rendering or heap headroom.
2. **Material/shader contract.** The notebook template preserves one game's
   material/SHARCFB pairing. glTF PBR does not supply that pairing. Define a
   bounded toon material schema and either derive compatible templates locally
   from the player's game or implement and validate independently authored
   GX2-compatible shader resources. The latter is additional substantial work;
   no general shader compiler/exporter has been selected or proved here.
3. **Skeleton and clips.** The existing patcher does not consume glTF joints,
   weights, inverse-bind matrices or animation channels. A new BFRES skeleton,
   matrix-palette and skeletal-animation writer is required, or an explicitly
   constrained template rig with retargeting. Native controllers and joint-name
   expectations must be audited. No skinned custom character proof exists.
4. **Resource identity and lifetime.** Routing a generated archive through the
   existing host-file service is feasible in principle, but global replacement
   of a retail resident archive would alter the real resident too. Use a unique
   archive/resource identity and a generic actor lifecycle, with guest-owned
   allocations, reference counts and reconstruction after state loads.
5. **Distribution boundary.** Commit only original source meshes/textures,
   converter code and metadata. Generated BFRES/SARC files that retain retail
   skeletons, material data or shaders must stay in the player's cache and must
   not become example/release assets. The original example model's MPL-2.0
   grant cannot cover the retail template portions of generated output.

**Research gate recommendation:** stop the *J3D-specific* implementation route
before phase 2 code; do not describe J3D output as usable by the native HD loader.
After phase 1 is fully proved and committed, the BFRES adaptation is the concrete
candidate route. Accept it as a change to the technical approach before promising
phase 2 completion. None of the evidence establishes an absolute impossibility;
it establishes missing skeletal/shader/lifecycle work and an unproved integration.

## Animation and behaviour proposal

BCK (keyed) and BCA (sampled) describe the GameCube J3D formats requested in
the prompt. For the HD route, author skeletal clips in the native BFRES resource
dictionaries and validate the retained animation-controller wrappers; do not
assume a BCK/BCA exporter is sufficient. Idle, walk and talk need named clips,
loop settings, a frame controller and consistent joint ordering with the model. Look-at needs a bounded joint/body rotation layered
after clip evaluation; it must not accumulate across render interpolation passes.
Walking also needs collision-aware movement and animation speed tied to game
logic time. A position teleport is not a walking implementation.

Proposed versioned C API (not exported yet): an opaque actor handle; callbacks
`create`, `execute`, `draw`, `talk`, `on_hit`, and `destroy`; bounded getters/setters
for position, angle, clip and collision; and a fixed-size per-actor data block.
Callbacks use guest functions through the existing SDK guest dispatcher. Execute
runs once per game step; draw may run on interpolation passes and must not mutate
saved state. Resource allocation belongs to create/destroy. Talk and hit callbacks
enqueue actions rather than recursively enter an event. Invalid or stale handles
must fail without touching guest memory.

Persist path progress and talked-to state as mod story records. Do not serialize
host pointers, guest vtables or resource handles. On state load abort active
scenes, invalidate generations and reconstruct resources from the loaded room.

## Effort and acceptance gates

Estimates are engineering days, conditional on verified HD bindings:

| Step | Estimate | Gate |
| --- | --- | --- |
| Profile registration and empty lifecycle | 3–5 | Room unload/reload and state-load tests |
| BFRES static template pipeline and original actor model/archive | 4–8 | Native render, lighting, shadow, measured heap delta |
| Cylinder collision and attention/talk | 3–5 | Link blocked, talk prompt, no game save-bit writes |
| BFRES rig/skin/clip authoring beyond existing static patcher | 10–20+ | Independently licensed writer/tool, native golden archive, rig/texture limits |
| HD materials/shaders and resource identity | 4–8+ | Native binding, player-local templates, unique actor archive |
| Animation, look-at and path movement | 5–10 | Idle/walk/talk, collision, true/interpolated 60 |
| Guest callback API | 4–7 | ABI contracts, invalid handles, callback lifetime |
| Scene paths, choices and stress proof | 5–10 | Native branching, interruption, queued mods, state load |

No heap limit is called measured until actual before/after runs exist. Candidate
limits must cover decoded textures, model expansion and animation allocation, not
just compressed archive bytes. Phase 2 registration and converter decisions must
be updated here before phase 2 code starts.
