# Story notebook artwork

`notebook_icon_192.png`: the story notebook's icon in the Delivery Bag (drawn by the port over the
notebook's bag place, `runtime/src/mods/story_notebook_bag.h`). Original artwork made for this
project by its maintainer; no game asset. Downscaled (premultiplied alpha) from the 1235 × 1235
transparent master, which is kept outside the repository. Compiled into the executable
(`cmake/EmbedBytes.cmake`).

`notebook_game.glb`: the notebook's 3D model (2,495 triangles, position, normal, one texture coordinate
and its 512 × 512 base colour texture embedded), same author. `tools/story_mods/notebook_model.py` builds
the get-item model from it on the player's machine (with the material and shader of the player's own
game files, which are never part of the repository).

Licence: original artwork by the project lead (made with ChatGPT image generation), published under the
repository's licence (confirmed 2026-10-13).
