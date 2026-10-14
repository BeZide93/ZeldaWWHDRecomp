"""wnd1: window pane (9-slice frames). Hook for the lyt-window work (branch story-lyt-window).

Contract: write(p, ctx) returns the section body (without the "wnd1" header), starting with pan1.body(p).
Materials for the content and the frames are added through ctx.material(name) (description materials)
or ctx.add_material(name, encoded bytes). Learn the layout from the game's own wnd1 sections only.
"""


def write(p, ctx):
    raise NotImplementedError("window panes (wnd1) are not written yet (lyt-window)")
