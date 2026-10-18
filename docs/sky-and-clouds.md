# Sky and clouds

These settings change how the sky looks. They don't change the game itself, and each one applies
immediately. You'll find them in the settings overlay under **Graphics > Effects**. On macOS they
are also in the **Graphics** menu.

## Bloom strength

Bloom is the soft glow around bright areas. 100% is the original game. Lower values give a
crisper, more saturated picture. At 0% bloom is off.

## Horizon haze

Horizon haze is the pale band low in the sky, just above the sea. 100% is the original game.
Lower values give a clearer, deeper blue sky near the horizon:

- At dusk the glow around the setting sun stays.
- At night the sky stays dark.
- Islands, the sea and characters don't change; only the sky behind them does.

For a vivid sky, try **Bloom strength 0%** with **Horizon haze 30%**.

Environment variable for tests: `WWHD_HORIZON_HAZE=0.3` (0 = no haze, 1 = original). It
overrides the saved setting.

## Cloud shadows

Cloud shadows add the soft shadows of drifting clouds on the sea, the islands, the boat and the
characters. They move with the wind. They are subtle at noon, weaker at dusk and dawn, off at
night, and paler in stormy weather. Off is the original game.

The shadows come from the same clouds the game puts in the sky. Those clouds are painted far away
in the sky, so a shadow won't always sit right under the cloud you see.

Environment variable for tests: `WWHD_CLOUD_SHADOWS=1`.
