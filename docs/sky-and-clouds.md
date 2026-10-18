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
