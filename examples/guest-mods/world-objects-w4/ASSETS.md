# Original Horizon Isle assets

Author: Lukas S. License: MPL-2.0.

`assets/make_fixtures.py` reproduces all three GLBs using only Python's standard
library. Geometry and embedded opaque checker atlases are original. No game
models, textures, collision files or archives are included.

| Model | Render triangles | Purpose |
| --- | ---: | --- |
| `island.glb` | 40 | Full island, beach, mountain and wooden pier |
| `island-lod.glb` | 26 | Matching mountain silhouette without pier or bottom |
| `island-foam.glb` | 8 | Opaque pale ribbon around the waterline |

The full island has one visible textured primitive plus a separate authored
collision mesh: 24 welded vertices, 40 triangles, 648 WCOL bytes. Beach, hillside
and pier use `col_sand`, `col_grass` and `col_wood`. Its local bounds are
(-1200,-900,-1900)..(1200,4000,1000) game units. The beach and lower grassy band
provide walking space; the steep mountain forms the distant silhouette. The
pier reaches south. The underwater wall extends to -900 for native ship queries.

The distant model has no collision mesh. Its 4000-unit peak remains within the
full island's bounds; it does not invent a distant collision island. The foam
model also has no collision mesh. It lies at local y=12 around x +/-1205..1290,
z +/-1005..1090. Its pale opaque checker imitates broken foam without requiring
alpha rendering; waves can cover it. It is authored coast decoration, not a
simulation of breaking water.

The example is placed at (75000,0,-201500), sea room 12. Enable it separately
from the W1/W3 examples, which occupy the same area. The manifest declares the
mod-owned `horizon.arrived` arrival flag, chart label and original distant/shore
models. The distant silhouette appears from neighboring sea squares. The foam
ribbon follows the authored coast while the game's waves may cover its low edge.
