"""txt1: text pane in the game font. Hook for the lyt-text work (branch story-lyt-text).

Contract: write(p, ctx) returns the section body (without the "txt1" header), starting with pan1.body(p).
Fonts are added to fnl1 through ctx.font(file name); the text pane's material through ctx.material /
ctx.add_material. Learn the layout from the game's own txt1 sections only.
"""


def write(p, ctx):
    raise NotImplementedError("text panes (txt1) are not written yet (lyt-text)")
