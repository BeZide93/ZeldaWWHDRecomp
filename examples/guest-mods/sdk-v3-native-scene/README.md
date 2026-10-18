# Harbor meeting

This content-only SDK v3 preview places two Uo2 residents on Windfall (`sea`, room 11). Talk to the harbor guide to request `harbor-meeting`. Install the complete folder with the mod manager, enable it with `story-notebook`, and restart. No guest-code build or external art is required.

The scene includes mandatory `steps` and an optional `native` alternative. The public preview can select its eligible `turn_camera` route when native readiness checks pass: both residents turn in parallel and the game’s FIXEDFRM/UNITRANS actions control the camera. Its timing is authored independently of the fallback. If readiness fails before native staging, the fallback makes each resident face a fixed point, holds the camera on them, and returns control after a short wait.

Fallback `frames` use a 60 Hz clock. Native `*_ticks` use the 30 Hz game logic clock. Coordinates are world positions; keep both residents and the camera in the same loaded room. Adding dialogue, animation, movement, branching or a following camera to the fallback makes the entire scene use that route.

See [the SDK guide](../../../docs/mod-sdk-v3.md) for the schema, native eligibility and current validation limits. This example passed a public talk-trigger native30 run with native entry, camera return, natural completion/rearm and strict native/HD mod-on/off save parity. Native30 was an explicit user override; the selector does not change the user’s FPS setting. A separate public Plus-skip run passed clean cancellation/rearm, full-state save/load and a later native scene completion, with strict native/HD save parity; see the [Part 5 evidence report](../../../docs/sdk-v3-phase2c-part5-report.md).
