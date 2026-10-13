# story-notebook

One small story quest in the story notebook, "The storyteller's lantern", described as data in
`manifest.json` (`quests`). When the mod is on, a storyteller's letter waits in the Rito postbox (once
Link has the Delivery Bag); the first letter from a mod brings the notebook, and this one also a sealed
letter for the storyteller's old friend who keeps Windfall's light. Open the notebook from the Delivery
Bag, choose the quest and "Show on chart", and talk to Kreeb (`windfall-kreeb` in the NPC catalogue: on
Windfall, up on the windmill by the light; he is there at any time and in every part of the story): he reads the letter and thanks Link with an
orange rupee (100), and the quest moves to the notebook's archive. He only says the quest's lines while
this quest is the one shown on the chart; otherwise he talks as in the game.

The mod's code only logs the quest's state: the port does the rest from the manifest
([docs/mod-sdk-v2.md](../../../docs/mod-sdk-v2.md#quests-in-the-manifest)). The texts are original to
this example.
