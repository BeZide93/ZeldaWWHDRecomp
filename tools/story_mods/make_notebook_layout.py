#!/usr/bin/env python3
"""The story notebook page as a game layout (local only, story mods are WIP).

usage: make_notebook_layout.py ART_DIR OUT_DIR
  ART_DIR  the painted art (frame.png, banner.png: the user's artwork, never in this repository)
  OUT_DIR  gets layout/notebook.json and the PNGs it names: the Story Notebook package's layout/ folder
           (manifest "notebook": {"layout": "layout/notebook.json"}); the port builds the archive from it
           (runtime/src/mods/lyt_build.h, story_notebook_layout.h). Check it with tools/story_mods/lyt
           (lyt.Description.load) or build it with story_lyt.py.
A dev tool (local only while the story mods are WIP): it writes our own small pictures (white, ring,
cross, cursor corner) and copies the art it is given, cleaned and cropped; no art lives here.

Geometry: the page of runtime/src/mods/story_notebook_page.h (framed look), on a 1280 x 720 screen,
converted to layout coordinates (origin in the centre, y up). Pane names are the contract with
runtime/src/mods/story_notebook_layout.cpp. Animations run at 30 frames a second (one frame per item
screen draw).
"""
import json
import math
import os
import sys

from PIL import Image, ImageDraw

W, H = 1280, 720
FRAME_ASPECT = 1448 / 1086
ROWS = 5
FPS = 30

# colours of story_notebook_page.h (RGBA)
INK, INK_SOFT, PLATE_INK, WHITE, SEAL = 0x003800FF, 0x3C6B2EFF, 0x0B3D06FF, 0xFFFFFFFF, 0xF2E4C2FF
SHADE = 0x0A140A88


def linear(c):
    """An sRGB colour as the linear values the game applies (alpha unchanged)."""
    f = lambda v: round(255 * (v / 255 / 12.92 if v / 255 <= 0.04045 else ((v / 255 + 0.055) / 1.055) ** 2.4))
    return [f(v) for v in c[:3]] + list(c[3:])


def rgba(c):
    return [(c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255, c & 255]


# ---- the page's geometry (story_notebook_page.h Page::frame_box / frame_part / geo, art.h) ----
class R:
    def __init__(self, x0, y0, x1, y1, w=1448, h=1086):
        self.x0, self.y0, self.x1, self.y1 = x0 / w, y0 / h, x1 / w, y1 / h


FR_TITLE = R(112, 92, 735, 160)
FR_TAB0, FR_TAB1 = R(748, 123, 1031, 197), R(1043, 123, 1311, 197)
FR_SLOT = R(1327, 94, 1395, 162)
FR_CONTENT = R(105, 228, 1365, 905)
FR_PLATE_L, FR_PLATE_R = R(80, 935, 464, 1023), R(948, 937, 1390, 1024)
BANNER_W, BANNER_H = 2103, 311
B_LEFT, B_RIGHT = 524 / BANNER_W, 1484 / BANNER_W
B_MED_X, B_MED_Y, B_MED_R = 199 / BANNER_W, 164 / BANNER_H, 97 / BANNER_H
B_PILL = R(1552, 106, 1919, 201, BANNER_W, BANNER_H)
B_TEXT = R(340, 30, 1490, 215, BANNER_W, BANNER_H)


def frame_box():
    h = 696.0
    w = h * FRAME_ASPECT
    return 640 - w / 2, 12.0, w, h


def frame_part(r):
    fx, fy, fw, fh = frame_box()
    return fx + r.x0 * fw, fy + r.y0 * fh, (r.x1 - r.x0) * fw, (r.y1 - r.y0) * fh


def geo():
    cx, cy, cw, ch = frame_part(FR_CONTENT)
    return {"content": (cx, cy, cw, ch), "list_x": cx + 8, "list_y": cy + 8, "list_w": cw - 16, "row_h": (ch - 16) / ROWS}


def row_x(share, x, w, h):
    k = h / 311.0 * 2103.0
    left, right = B_LEFT * k, (1 - B_RIGHT) * k
    if left + right >= w:
        return x + share * w
    if share <= B_LEFT:
        return x + share * k
    if share >= B_RIGHT:
        return x + w - (1 - share) * k
    return x + left + (share - B_LEFT) / (B_RIGHT - B_LEFT) * (w - left - right)


# ---- panes ----
def place(x, y, w, h, parent=(0.0, 0.0)):
    """A pane's translate (relative to its parent's centre in layout coordinates) for a page box."""
    return [round(x + w / 2 - W / 2 - parent[0], 3), round(H / 2 - (y + h / 2) - parent[1], 3)]


def pic(name, x, y, w, h, material, parent=(0.0, 0.0), uvs=None, **kw):
    p = {"type": "picture", "name": name, "size": [round(w, 3), round(h, 3)], "translate": place(x, y, w, h, parent),
         "material": material}
    if uvs:
        p["uvs"] = [uvs]
    p.update(kw)
    return p


def null(name, x, y, w, h, children, parent=(0.0, 0.0), **kw):
    p = {"type": "null", "name": name, "size": [round(w, 3), round(h, 3)], "translate": place(x, y, w, h, parent),
         "influence_alpha": True, "children": children}
    p.update(kw)
    return p


def centre(x, y, w, h):
    return (x + w / 2 - W / 2, H / 2 - (y + h / 2))


def text(name, x, y, size, colour, value="", bold=False, width=None, align="left", parent=(0.0, 0.0), capacity=96, k=1.2,
         square=False, **kw):
    """A text pane whose text starts at the page's (x, y) (top left of the text) in the given font size."""
    # k: the game font's glyphs are smaller in their cell than the HUD's at the same size (measured
    # against the HUD page: x-height 1.2 - 1.3 times, hudref); square: the icon font's round buttons
    h = size * 1.35 * k
    w = width if width is not None else 600
    pos = {"left": ["left", "top"], "center": ["center", "top"], "right": ["right", "top"]}[align]
    # the game's own text panes: the letters take the material's white colour, the font's border its black
    # colour (alpha 0: no border); the text colours stay white. Bold (the HUD's menu labels): a light border
    # around dark letters, a dark one around light letters. Material colours are linear, the HUD's sRGB.
    fill = rgba(colour)
    light = sum(fill[:3]) > 3 * 160
    # plain text: the border in the letters' own colour (the HUD's plain letters are as heavy as that)
    border = ([44, 58, 46, 255] if light else [255, 255, 255, 255]) if bold else fill[:3] + [96]
    p = {"type": "text", "name": name, "size": [round(w, 3), round(h, 3)], "translate": place(x, y, w, h, parent),
         "font": "CKingMain.bffnt" if bold else "CKingMsg.bffnt", "font_size": [round(size * k * (1.0 if square else 0.92), 2), round(size * k, 2)],
         "text": value, "capacity": capacity, "text_position": pos, "line_alignment": align,
         "text_material": {"black": linear(border), "white": linear(fill)}}
    p.update(kw)
    return p


# ---- our own small pictures (drawn here) ----
def write_small_art(out):
    Image.new("RGBA", (8, 8), (255, 255, 255, 255)).save(os.path.join(out, "white.png"))
    s = 64
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.ellipse([1, 1, s - 2, s - 2], fill=tuple(rgba(0xD9822BFF)))
    d.ellipse([s * 0.19, s * 0.19, s * 0.81, s * 0.81], outline=(255, 255, 255, 255), width=6)
    im.save(os.path.join(out, "ring.png"))
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c = tuple(rgba(INK_SOFT))
    d.line([10, 10, s - 11, s - 11], fill=c, width=7)
    d.line([s - 11, 10, 10, s - 11], fill=c, width=7)
    im.save(os.path.join(out, "cross.png"))
    # the cursor's corner bracket (top-left; the other corners use flipped texture coordinates)
    s = 32
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    brown = (70, 45, 20, 255)  # the HUD's cursor: solid dark brown brackets
    d.rectangle([0, 0, s - 1, 7], fill=brown)
    d.rectangle([0, 0, 7, s - 1], fill=brown)
    im.save(os.path.join(out, "corner.png"))


def clean_edges(im, min_alpha=96, crop=True):
    """story_notebook_art.h clean_edges: texels under min_alpha cleared, cropped to what is left."""
    im = im.convert("RGBA")
    px = im.load()
    w, h = im.size
    x0, y0, x1, y1 = w, h, 0, 0
    for y in range(h):
        for x in range(w):
            if px[x, y][3] >= min_alpha:
                x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x + 1), max(y1, y + 1)
            else:
                px[x, y] = (0, 0, 0, 0)
    return im.crop((x0, y0, x1, y1)) if crop else im


def prepare_art(art, out):
    frame = clean_edges(Image.open(os.path.join(art, "frame.png")), crop=False)
    frame.save(os.path.join(out, "frame.png"))
    banner = clean_edges(Image.open(os.path.join(art, "banner.png")))
    bh = 160
    banner = banner.resize((round(banner.width * bh / banner.height), bh), Image.LANCZOS)
    banner.save(os.path.join(out, "banner.png"))
    return frame.size, banner.size


def build(art, out_dir):
    out = os.path.join(out_dir, "layout")
    os.makedirs(out, exist_ok=True)
    write_small_art(out)
    (frame_w, frame_h), (banner_w, banner_h) = prepare_art(art, out)
    tex = lambda png: {"png": png, "tile": "tiled"}
    textures = {"NbFrame_00": tex("frame.png"), "NbBanner_00": tex("banner.png"), "NbWhite_00": tex("white.png"),
                "NbRing_00": tex("ring.png"), "NbCross_00": tex("cross.png"), "NbCorner_00": tex("corner.png")}
    mat = lambda t, white=None: {"textures": [{"texture": t}], **({"white": white} if white else {})}
    materials = {"M_Frame": mat("NbFrame_00"), "M_Banner": mat("NbBanner_00"), "M_Ring": mat("NbRing_00"),
                 "M_Cross": mat("NbCross_00"), "M_Corner": mat("NbCorner_00"),
                 "M_Shade": mat("NbWhite_00", rgba(SHADE)), "M_Wash": mat("NbWhite_00", [255, 255, 255, 0x30]),
                 "M_TabOn": mat("NbWhite_00", [255, 255, 255, 0x50]), "M_TabOff": mat("NbWhite_00", [0x1E, 0x3C, 0x14, 0x30]),
                 "M_PillNew": mat("NbWhite_00", [0x2E, 0x8A, 0x2E, 0xC8]), "M_PillNotYet": mat("NbWhite_00", [0x7C, 0x8A, 0x6C, 0xD8]),
                 "M_PillDone": mat("NbWhite_00", [0x9C, 0x3A, 0x2A, 0xD0])}

    fx, fy, fw, fh = frame_box()
    page_c = centre(fx, fy, fw, fh)  # N_Page sits at the screen centre (scales around it)
    page_c = (0.0, 0.0)
    g = geo()
    cont = g["content"]
    content_children = []
    rows = []
    for i in range(ROWS):
        x, w = g["list_x"], g["list_w"]
        y, h = g["list_y"] + g["row_h"] * i + 2, g["row_h"] - 4
        rc = centre(x, y, w, h)
        k = h / banner_h * banner_w  # the banner's width at this height (art: its own aspect)
        lw, rw = B_LEFT * k, (1 - B_RIGHT) * k
        kids = [pic("P_Row%dL" % i, x, y, lw, h, "M_Banner", rc, [[0, 0], [B_LEFT, 0], [0, 1], [B_LEFT, 1]]),
                pic("P_Row%dM" % i, x + lw, y, w - lw - rw, h, "M_Banner", rc, [[B_LEFT, 0], [B_RIGHT, 0], [B_LEFT, 1], [B_RIGHT, 1]]),
                pic("P_Row%dR" % i, x + w - rw, y, rw, h, "M_Banner", rc, [[B_RIGHT, 0], [1, 0], [B_RIGHT, 1], [1, 1]])]
        bx = row_x(B_TEXT.x0 - 0.02, x, w, h)
        kids.append(pic("P_Row%dWash" % i, bx, y + h * 0.12, x + w - h * 0.25 - bx, h * 0.76, "M_Wash", rc, visible=False))
        mcx, mcy, mr = row_x(B_MED_X, x, w, h), y + B_MED_Y * h, B_MED_R * h * 0.86
        kids.append(text("T_Row%dInitial" % i, mcx - mr, mcy - mr * 0.68, mr * 1.3, SEAL, "?", bold=True, width=2 * mr,
                         align="center", parent=rc, capacity=4, k=1.0))
        tx, px0, px1 = row_x(B_TEXT.x0, x, w, h), row_x(B_PILL.x0, x, w, h), row_x(B_PILL.x1, x, w, h)
        tw = px0 - tx - 20
        for dim, colour in (("", INK), ("Dim", INK_SOFT)):
            kids.append(text("T_Row%dTitle%s" % (i, dim), tx, y + h * 0.13, 25, colour, bold=True, width=tw, parent=rc,
                             visible=False))
            kids.append(text("T_Row%dNext%s" % (i, dim), tx, y + h * 0.47, 19, colour, width=tw, parent=rc, visible=False, k=1.2))
        py, ph = y + B_PILL.y0 * h, (B_PILL.y1 - B_PILL.y0) * h
        for name in ("New", "NotYet", "Done"):
            kids.append(pic("P_Row%dPill%s" % (i, name), px0 + 2, py + 2, px1 - px0 - 4, ph - 4, "M_Pill" + name, rc, visible=False))
        kids.append(text("T_Row%dStatus" % i, px0, py + (ph - 17) / 2, 17, WHITE, bold=True, width=px1 - px0, align="center",
                         parent=rc, capacity=24, k=1.0))
        kids.append(pic("P_Row%dRing" % i, px0 - 18 - 12, py + ph / 2 - 12, 24, 24, "M_Ring", rc, visible=False))
        rows.append(null("N_Row%d" % i, x, y, w, h, kids, page_c, visible=False))
    content_children += rows
    # the empty list
    content_children.append(text("T_Empty1", cont[0] + 40, cont[1] + 60, 30, INK_SOFT, width=cont[2] - 80, visible=False, k=1.18))
    content_children.append(text("T_Empty2", cont[0] + 40, cont[1] + 104, 24, INK_SOFT, width=cont[2] - 80, visible=False, k=1.18))
    # scroll marks
    content_children.append(text("T_Up", g["list_x"] + g["list_w"] / 2 - 20, g["list_y"] - 26, 22, INK_SOFT, "^", width=40,
                                 align="center", visible=False, capacity=4))
    content_children.append(text("T_Down", g["list_x"] + g["list_w"] / 2 - 20, g["list_y"] + g["row_h"] * ROWS - 6, 22, INK_SOFT,
                                 "v", width=40, align="center", visible=False, capacity=4))
    # the cursor: four corner brackets around the selected row (N_Cursor moves to the row)
    cx, cy, cw, chh = g["list_x"] + 4, g["list_y"] + 2 + 2, g["list_w"] - 8, g["row_h"] - 8
    b = 4 + 2
    cur_c = centre(cx, cy, cw, chh)
    cs = 22
    corners = []
    for name, (ux0, ux1, uy0, uy1), (ox, oy) in (("TL", (0, 1, 0, 1), (cx - b, cy - b)), ("TR", (1, 0, 0, 1), (cx + cw + b - cs, cy - b)),
                                                 ("BL", (0, 1, 1, 0), (cx - b, cy + chh + b - cs)),
                                                 ("BR", (1, 0, 1, 0), (cx + cw + b - cs, cy + chh + b - cs))):
        corners.append(pic("P_Cursor" + name, ox, oy, cs, cs, "M_Corner", cur_c, [[ux0, uy0], [ux1, uy0], [ux0, uy1], [ux1, uy1]]))
    content_children.append(null("N_Cursor", cx, cy, cw, chh, corners, page_c, visible=False))
    content = null("N_Content", 0, 0, W, H, content_children, page_c)

    # the frame, title, close mark, tabs, plates
    kids = [pic("P_Frame", fx, fy, fw, fh, "M_Frame", page_c)]
    tx, ty, tw, th = frame_part(FR_TITLE)
    kids.append(text("T_Title", tx, ty, 44, INK, "Story Notebook", bold=True, width=tw + 200, capacity=32, k=1.1))
    sx, sy, sw, sh = frame_part(FR_SLOT)
    r = sw / 2 * 0.42 * 2 + 6
    kids.append(pic("P_Close", sx + sw / 2 - r / 2, sy + sh / 2 - r / 2, r, r, "M_Cross", page_c))
    for i, fr in enumerate((FR_TAB0, FR_TAB1)):
        x, y, w, h = frame_part(fr)
        kids.append(pic("P_Tab%dOn" % i, x + 6, y + 6, w - 12, h - 12, "M_TabOn", page_c, visible=False))
        kids.append(pic("P_Tab%dOff" % i, x + 6, y + 6, w - 12, h - 12, "M_TabOff", page_c))
        for state, colour in (("On", PLATE_INK), ("Off", INK_SOFT)):
            kids.append(text("T_Tab%d%s" % (i, state), x, y + (h - 25) / 2, 25, colour, ("Quests", "Archive")[i], bold=True,
                             width=w, align="center", visible=state == "Off", capacity=24))
    plates = []
    for name, fr, glyph, label in (("Back", FR_PLATE_L, "", "Close"), ("Track", FR_PLATE_R, "", "Open")):
        x, y, w, h = frame_part(fr)
        pc = centre(x, y, w, h)
        size = 34
        pw = size + 6 + len(label) * size * 0.8 * 0.44  # the label's width as the HUD measures it
        px, py = x + (w - pw) / 2, y + (h - size) / 2 - 2
        plates.append(null("N_Plate" + name, x, y, w, h, [
            text("T_%sIcon" % name, px, py, size, WHITE, glyph, width=size + 4, parent=pc, capacity=4, font="CKingPic.bffnt",
                 k=1.0, square=True),
            text("T_%sLabel" % name, px + size + 6, py + size * 0.1, size * 0.8, INK, label, bold=True, width=w, parent=pc,
                 capacity=24)], page_c))
    kids += plates
    kids.append(content)
    page = null("N_Page", 0, 0, W, H, kids)
    shade = pic("P_Shade", -400, -200, 2080, 1120, "M_Shade")
    root = null("N_Notebook", 0, 0, W, H, [null("N_Shade", 0, 0, W, H, [shade]), page])

    groups = {"G_Open": ["N_Shade", "N_Page"], "G_Content": ["N_Content"], "G_Back": ["N_PlateBack"], "G_Track": ["N_PlateTrack"],
              "G_Cursor": ["P_CursorTL", "P_CursorTR", "P_CursorBL", "P_CursorBR"]}
    for i in range(ROWS):
        groups["G_Row%d" % i] = ["N_Row%d" % i]

    f = lambda s: max(1, round(s * FPS))
    open_n, close_n, turn_n, press_n, bump_n = f(0.22), f(0.15), f(0.20), f(0.16), f(0.18)

    def ease_out_back(t):
        c, u = 1.6, t - 1.0
        return 1.0 + (c + 1.0) * u ** 3 + c * u * u

    def ease_out(t):
        return 1 - (1 - t) ** 3

    def sampled(fn, n):
        return [[k, round(fn(k / n), 4)] for k in range(n + 1)]

    scale_open = sampled(lambda t: 0.92 + 0.08 * ease_out_back(t), open_n)
    fade_open = sampled(lambda t: 255 * min(1.0, t * 1.6), open_n)
    anims = {
        "Open": {"frames": open_n, "groups": ["G_Open"], "tracks": [
            {"pane": "N_Page", "target": "sx", "keys": scale_open, "interp": "linear"},
            {"pane": "N_Page", "target": "sy", "keys": scale_open, "interp": "linear"},
            {"pane": "N_Page", "target": "alpha", "keys": fade_open},
            {"pane": "N_Shade", "target": "alpha", "keys": fade_open}]},
        "Close": {"frames": close_n, "groups": ["G_Open"], "tracks": [
            {"pane": "N_Page", "target": "sx", "keys": [[0, 1], [close_n, 0.95]]},
            {"pane": "N_Page", "target": "sy", "keys": [[0, 1], [close_n, 0.95]]},
            {"pane": "N_Page", "target": "alpha", "keys": [[0, 255], [close_n, 0]]},
            {"pane": "N_Shade", "target": "alpha", "keys": [[0, 255], [close_n, 0]]}]},
    }
    for name, d in (("TurnRight", 1), ("TurnLeft", -1)):
        anims[name] = {"frames": turn_n, "groups": ["G_Content"], "tracks": [
            {"pane": "N_Content", "target": "tx", "keys": sampled(lambda t: d * (1 - ease_out(t)) * 70, turn_n)},
            {"pane": "N_Content", "target": "alpha", "keys": sampled(lambda t: 255 * ease_out(t), turn_n)}]}
    press = sampled(lambda t: 1.0 - 0.10 * math.sin(t * math.pi), press_n)
    for name in ("Back", "Track"):
        anims["Press" + name] = {"frames": press_n, "groups": ["G_" + name], "tracks": [
            {"pane": "N_Plate" + name, "target": "sx", "keys": press}, {"pane": "N_Plate" + name, "target": "sy", "keys": press}]}
    bump = sampled(lambda t: 1.0 + 0.02 * math.sin(t * math.pi), bump_n)
    for i in range(ROWS):
        anims["Bump%d" % i] = {"frames": bump_n, "groups": ["G_Row%d" % i], "tracks": [
            {"pane": "N_Row%d" % i, "target": "sx", "keys": bump}, {"pane": "N_Row%d" % i, "target": "sy", "keys": bump}]}
    # the cursor breathes: its brackets move out and back (2 to 6 px), 0.9 times a second
    breathe_n = f(1 / 0.9)
    off = lambda t: 2.0 * math.sin(t * 2 * math.pi)
    tracks = []
    for name, (sxn, syn) in (("TL", (-1, 1)), ("TR", (1, 1)), ("BL", (-1, -1)), ("BR", (1, -1))):
        pane = next(p for p in corners if p["name"] == "P_Cursor" + name)
        bx_, by_ = pane["translate"]
        tracks.append({"pane": pane["name"], "target": "tx", "keys": sampled(lambda t: bx_ + sxn * off(t), breathe_n)})
        tracks.append({"pane": pane["name"], "target": "ty", "keys": sampled(lambda t: by_ + syn * off(t), breathe_n)})
    anims["Breathe"] = {"frames": breathe_n, "loop": True, "groups": ["G_Cursor"], "tracks": tracks}

    desc = {"name": "StoryNotebook_00", "size": [W, H], "textures": textures, "materials": materials, "panes": [root],
            "groups": groups, "animations": anims}
    with open(os.path.join(out, "notebook.json"), "w") as fh:
        json.dump(desc, fh, indent=1, ensure_ascii=False)
    return desc


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    d = build(sys.argv[1], sys.argv[2])
    print("layout/notebook.json: %d textures, %d animations" % (len(d["textures"]), len(d["animations"])))
