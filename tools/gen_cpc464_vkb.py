#!/usr/bin/env python3
"""Generates src/drivers/computers/amstrad_cpc_vkb_data.inc: CPC 464 keyboard.

The 74 keys of the CPC 464 as sculpted caps seen from the front and above
(top face, side and front skirts), in the dark case: charcoal caps for the
main block, light grey for the editing keys, a red ESC, green f-key pad and
blue ENTER keys. Drawn from scratch with FreeSans; no logos or badges.

usage: gen_cpc464_vkb.py out.inc [preview.png]
"""
import sys

from vkb_common import Canvas, lerp, write_inc

W, H = 1536, 500
U = 79.0
GAP = 0.06
MAIN_W, PAD_GAP, PAD_W = 15.0, 0.55, 3.0
LEFT = (W - (MAIN_W + PAD_GAP + PAD_W) * U) / 2
TOP = 62.0

CASE_TOP = (58, 58, 62)
CASE_BOT = (30, 30, 33)
WELL = (14, 14, 16)

# Cap palettes: top face, top face lower edge, side skirt, front skirt, legend.
PAL = {
    "dark": ((70, 70, 75), (54, 54, 58), (40, 40, 44), (28, 28, 31), (238, 238, 236)),
    "grey": ((176, 176, 172), (150, 150, 146), (122, 122, 118), (98, 98, 95), (30, 30, 32)),
    "red": ((214, 58, 52), (186, 42, 38), (150, 30, 28), (118, 22, 20), (250, 244, 238)),
    "green": ((70, 168, 92), (52, 140, 72), (38, 112, 56), (28, 88, 44), (248, 250, 244)),
    "blue": ((62, 120, 214), (46, 98, 186), (34, 76, 150), (26, 58, 118), (248, 250, 255)),
}

c = Canvas(W, H)
keys = []

# Case top and key well.
c.shadow([(16, 12, W - 16, H - 4, 26)], 8, alpha=170)
c.vgrad_rrect(10, 6, W - 10, H - 8, 24, CASE_TOP, CASE_BOT)
c.vgrad_rrect(10, 6, W - 10, 14, 6, (92, 92, 98), CASE_TOP)
well = (LEFT - 12, TOP - 12, LEFT + (MAIN_W + PAD_GAP + PAD_W) * U + 12, TOP + 5 * U + 12)
c.rrect(well[0] - 2, well[1] - 3, well[2] + 2, well[3] + 2, 12, (20, 20, 22, 255))
c.rrect(*well, 10, WELL + (255,))
# Power indicator over the pad (a plain LED and label, no branding).
lx, ly = LEFT + (MAIN_W + PAD_GAP + 2.6) * U, 32
c.draw.ellipse(c.box(lx - 9, ly - 6, lx + 9, ly + 6), fill=(40, 10, 10, 255))
c.draw.ellipse(c.box(lx - 7, ly - 4, lx + 7, ly + 4), fill=(226, 36, 30, 255))
c.draw.ellipse(c.box(lx - 4, ly - 3, lx, ly - 1), fill=(255, 160, 150, 255))
c.text(lx - 16, ly, "ON", 13, (150, 150, 156), "rm")


def cap(x, y, w, h, legends, pal="dark", size=None, code=None, flags=0, name=""):
    top, top2, side, front, ink = PAL[pal]
    g = GAP * U
    x1, y1, x2, y2 = LEFT + x * U + g / 2, TOP + y * U + g / 2, LEFT + (x + w) * U - g / 2, TOP + (y + h) * U - g / 2
    c.shadow([(x1, y1 + 3, x2, y2 + 4, 8)], 3, alpha=210)
    c.rrect(x1, y1, x2, y2, 7, side + (255,))
    ix, it, ib = 0.10 * U, 0.04 * U, 0.20 * U
    tx1, ty1, tx2, ty2 = x1 + ix, y1 + it, x2 - ix, y2 - ib
    # Front skirt (trapezoid, darker at the base) and side shading.
    for i in range(c.s(y2 - 2) - c.s(ty2)):
        t = i / max(1, c.s(y2 - 2) - c.s(ty2))
        yy = c.s(ty2) + i
        xa = c.s(tx1) + (c.s(x1 + 3) - c.s(tx1)) * t
        xb = c.s(tx2) + (c.s(x2 - 3) - c.s(tx2)) * t
        c.draw.line([(xa, yy), (xb, yy)], fill=lerp(front, lerp(front, (0, 0, 0), 0.4), t) + (255,))
    for i in range(c.s(ix)):
        t = i / max(1, c.s(ix))
        c.draw.line([(c.s(x1) + i, c.s(y1 + 6) + i // 3), (c.s(x1) + i, c.s(y2 - 6))],
                    fill=lerp(lerp(side, (255, 255, 255), 0.10), side, t) + (255,))
        c.draw.line([(c.s(x2) - i, c.s(y1 + 6) + i // 3), (c.s(x2) - i, c.s(y2 - 6))],
                    fill=lerp(lerp(side, (0, 0, 0), 0.25), side, t) + (255,))
    # Slightly dished top face with a highlight along its top edge.
    c.vgrad_rrect(tx1, ty1, tx2, ty2, 6, None, None,
                  stops=[(0.0, lerp(top, (255, 255, 255), 0.18)), (0.08, top), (0.55, lerp(top, top2, 0.6)),
                         (1.0, top2)])
    # Legends.
    sz = size or 0.22 * U
    if len(legends) == 1:
        c.text((tx1 + tx2) / 2, (ty1 + ty2) / 2, legends[0], sz, ink, "mm", max_w=tx2 - tx1 - 6)
    elif len(legends) == 2:
        c.text(tx1 + 0.10 * U, ty1 + (ty2 - ty1) * 0.30, legends[0], sz, ink, "lm")
        c.text(tx1 + 0.10 * U, ty1 + (ty2 - ty1) * 0.74, legends[1], sz, ink, "lm")
    if code is not None:
        keys.append((code, flags, (x1, y1, x2, y2), name or " ".join(legends)))


def k(r, b):
    return r * 8 + b


# Row 1.
row = [("ESC", k(8, 2), "red"), (("!", "1"), k(8, 0)), (('"', "2"), k(8, 1)), (("#", "3"), k(7, 1)),
       (("$", "4"), k(7, 0)), (("%", "5"), k(6, 1)), (("&", "6"), k(6, 0)), (("'", "7"), k(5, 1)),
       (("(", "8"), k(5, 0)), ((")", "9"), k(4, 1)), (("_", "0"), k(4, 0)), (("=", "-"), k(3, 1)),
       (("£", "^"), k(3, 0)), ("CLR", k(2, 0), "grey"), ("DEL", k(9, 7), "grey")]
x = 0.0
for item in row:
    leg, code = item[0], item[1]
    pal = item[2] if len(item) > 2 else "dark"
    leg = [leg] if isinstance(leg, str) else list(leg)
    cap(x, 0, 1, 1, leg, pal, size=0.19 * U if len(leg[0]) > 2 else None, code=code)
    x += 1

# Row 2: TAB, Q..[, ENTER (tall, rows 2-3).
cap(0, 1, 1.75, 1, ["TAB"], "grey", 0.19 * U, k(8, 4))
x = 1.75
for ch, code in zip("QWERTYUIOP", [k(8, 3), k(7, 3), k(7, 2), k(6, 2), k(6, 3), k(5, 3), k(5, 2), k(4, 3),
                                  k(4, 2), k(3, 3)]):
    cap(x, 1, 1, 1, [ch], code=code)
    x += 1
cap(x, 1, 1, 1, ["|", "@"], code=k(3, 2))
cap(x + 1, 1, 1, 1, ["{", "["], code=k(2, 1))
cap(13.75, 1, 1.25, 2, ["ENTER"], "blue", 0.19 * U, k(2, 2))

# Row 3: CAPS LOCK, A..], .
cap(0, 2, 1.75, 1, ["CAPS", "LOCK"], "grey", 0.17 * U, k(8, 6))
x = 1.75
for ch, code in zip("ASDFGHJKL", [k(8, 5), k(7, 4), k(7, 5), k(6, 5), k(6, 4), k(5, 4), k(5, 5), k(4, 5),
                                 k(4, 4)]):
    cap(x, 2, 1, 1, [ch], code=code)
    x += 1
cap(x, 2, 1, 1, ["*", ":"], code=k(3, 5))
cap(x + 1, 2, 1, 1, ["+", ";"], code=k(3, 4))
cap(x + 2, 2, 1, 1, ["}", "]"], code=k(2, 3))

# Row 4: SHIFT, Z../ \, SHIFT.
cap(0, 3, 2.25, 1, ["SHIFT"], "grey", 0.19 * U, k(2, 5), 1, "SHIFT (left)")
x = 2.25
for ch, code in zip("ZXCVBNM", [k(8, 7), k(7, 7), k(7, 6), k(6, 7), k(6, 6), k(5, 6), k(4, 6)]):
    cap(x, 3, 1, 1, [ch], code=code)
    x += 1
for leg, code in [(("<", ","), k(4, 7)), ((">", "."), k(3, 7)), (("?", "/"), k(3, 6)), (("`", "\\"), k(2, 6))]:
    cap(x, 3, 1, 1, list(leg), code=code)
    x += 1
cap(x, 3, 15 - x, 1, ["SHIFT"], "grey", 0.19 * U, k(2, 5), 1, "SHIFT (right)")

# Row 5: CTRL, COPY, space bar, ENTER.
cap(0, 4, 1.75, 1, ["CTRL"], "grey", 0.19 * U, k(2, 7), 1)
cap(1.75, 4, 1.5, 1, ["COPY"], "grey", 0.19 * U, k(1, 1))
cap(3.25, 4, 8.75, 1, [""], code=k(5, 7), name="SPACE")
cap(12.0, 4, 3.0, 1, ["ENTER"], "blue", 0.19 * U, k(0, 6))

# Function key pad and cursor keys.
px = MAIN_W + PAD_GAP
pad = [[("f7", k(1, 2)), ("f8", k(1, 3)), ("f9", k(0, 3))],
       [("f4", k(2, 4)), ("f5", k(1, 4)), ("f6", k(0, 4))],
       [("f1", k(1, 5)), ("f2", k(1, 6)), ("f3", k(0, 5))],
       [("f0", k(1, 7)), ("↑", k(0, 0)), (".", k(0, 7))],
       [("←", k(1, 0)), ("↓", k(0, 2)), ("→", k(0, 1))]]
for r, cells in enumerate(pad):
    for i, (leg, code) in enumerate(cells):
        arrow = leg in "←↑→↓"
        cap(px + i, r, 1, 1, [leg], "grey" if arrow else "green", 0.30 * U if arrow else 0.24 * U, code)

img = c.final()
if len(sys.argv) > 2:
    from PIL import Image
    bg = Image.new("RGBA", img.size, (0, 0, 120, 255))
    bg.alpha_composite(img)
    bg.convert("RGB").save(sys.argv[2])
size = write_inc(sys.argv[1], img, keys, "CpcVkb",
                 "// Generated by tools/gen_cpc464_vkb.py, do not edit.\n"
                 "// Amstrad CPC 464 keyboard; key codes are matrix line * 8 + bit.\n")
print(f"{len(keys)} keys, {size} bytes compressed")
