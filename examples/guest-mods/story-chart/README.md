# story-chart

A story mod's chart, treated like the game's treasure charts: when the mod is on, Link gets the
storyteller's chart in the loaded save file. Its marker is the top of Windfall's lighthouse
(square D2).

- While Link only owns the chart, the sea chart shows nothing for it, as for an owned treasure
  chart.
- Climb to the top of the lighthouse: the mod finishes the chart. Open the MAP screen: the game's
  red hand-drawn circle (the mark of a salvaged treasure) marks the spot, on the sea chart and in
  the zoomed view of square D2.

The save file remembers the chart and whether it is finished; save the game to keep it.

It shows `wwhd_chart_give`, `wwhd_chart_mark` and `wwhd_chart_finish` from
[docs/mod-sdk-v2.md](../../../docs/mod-sdk-v2.md#charts-and-markers).
