#!/usr/bin/env python3
"""Generates src/drivers/computers/amstrad_cpc_vkb_data.inc: CPC 464 keyboard.

Modelled on the Spanish CPC 464 ("ORDENADOR PERSONAL EN COLOR"): black
sculpted caps in the charcoal case, red ESC, green TAB / FIJA MAYS / MAYS /
CTRL / BORR / COPIA, blue INTRO keys, the cursor cross over the numeric pad
(f0-f9), the ventilation slots along the top and the recessed label strip
with the power LED. Drawn from scratch with FreeSans; the label strip is
left plain (no logos or model badges).

usage: gen_cpc464_vkb.py out.inc [preview.png]
"""
import sys

from vkb_common import Canvas, lerp, write_inc

W, H = 1536, 600
U = 75.0
GAP = 0.07
MAIN_W = 15.4
PAD_X = MAIN_W + 0.95       # keypad / cursor block, in units from the main block
LEFT = (W - (PAD_X + 3.0) * U) / 2
ROW0 = 2.25                 # main row 1, in units below the top of the key area
TOP = 30.0

CASE_TOP = (66, 67, 70)
CASE_BOT = (46, 47, 50)

# Cap palettes: top face, top face lower edge, side skirt, front skirt, legend.
PAL = {
    "black": ((56, 57, 60), (44, 45, 48), (34, 35, 38), (24, 25, 27), (236, 236, 232)),
    "red": ((214, 40, 46), (190, 30, 36), (150, 22, 28), (118, 16, 20), (250, 244, 240)),
    "green": ((70, 182, 104), (52, 156, 84), (38, 126, 66), (28, 100, 52), (244, 250, 244)),
    "blue": ((86, 116, 178), (70, 98, 158), (54, 78, 130), (40, 60, 104), (244, 248, 255)),
}

c = Canvas(W, H)
keys = []

# Case.
c.shadow([(14, 10, W - 14, H - 4, 18)], 7, alpha=170)
c.vgrad_rrect(8, 4, W - 8, H - 8, 16, CASE_TOP, CASE_BOT)
c.vgrad_rrect(8, 4, W - 8, 12, 6, (96, 97, 101), CASE_TOP)
# Ventilation slots along the back edge.
x = 40.0
while x < W - 44:
    c.rrect(x, 14, x + 5, 30, 2, (20, 20, 22, 255))
    c.draw.line([(c.s(x), c.s(30.5)), (c.s(x + 5), c.s(30.5))], fill=(88, 89, 93, 255), width=c.s(1))
    x += 11.5


def ux(v):
    return LEFT + v * U


def uy(v):
    return TOP + v * U


# Recessed label strip over the main block, with the power LED.
sx1, sy1, sx2, sy2 = ux(0), uy(0.75), ux(MAIN_W), uy(1.95)
c.rrect(sx1 - 2, sy1 - 2, sx2 + 2, sy2 + 3, 4, (30, 30, 33, 255))
c.vgrad_rrect(sx1, sy1, sx2, sy2, 3, (54, 55, 58), (44, 45, 48))
c.draw.line([(c.s(sx1 + 4), c.s(sy2 + 2)), (c.s(sx2 - 4), c.s(sy2 + 2))], fill=(84, 85, 89, 255), width=c.s(1))
lx, ly = sx2 - 0.55 * U, sy1 + 0.32 * U
c.draw.ellipse(c.box(lx - 7, ly - 7, lx + 7, ly + 7), fill=(30, 8, 8, 255))
c.draw.ellipse(c.box(lx - 5, ly - 5, lx + 5, ly + 5), fill=(205, 30, 26, 255))
c.draw.ellipse(c.box(lx - 3, ly - 3, lx, ly - 1), fill=(255, 150, 140, 255))
c.text(lx - 12, ly, "ENC.", 12, (218, 218, 214), "rm")

# Key wells: the main block, the cursor cross and the numeric pad.
def well(x1, y1, x2, y2):
    c.rrect(ux(x1) - 7, uy(y1) - 7, ux(x2) + 7, uy(y2) + 8, 8, (22, 22, 24, 255))
    c.rrect(ux(x1) - 5, uy(y1) - 5, ux(x2) + 5, uy(y2) + 5, 7, (16, 16, 18, 255))


well(0, ROW0, MAIN_W, ROW0 + 5)
well(PAD_X, ROW0, PAD_X + 3, ROW0 + 4)
well(PAD_X + 1, ROW0 - 2.55, PAD_X + 2, ROW0 - 0.2)
well(PAD_X, ROW0 - 1.85, PAD_X + 3, ROW0 - 0.95)


def cap(x, y, w, h, legends, pal="black", size=None, code=None, flags=0, name="", notch=None):
    """Key at (x, y) in units relative to the key area; notch=(dx, dh) makes an L."""
    top, top2, side, front, ink = PAL[pal]
    g = GAP * U
    x1, y1, x2, y2 = ux(x) + g / 2, uy(y) + g / 2, ux(x + w) - g / 2, uy(y + h) - g / 2
    c.shadow([(x1, y1 + 3, x2, y2 + 4, 8)], 3, alpha=220)
    c.rrect(x1, y1, x2, y2, 7, side + (255,))
    ix, it, ib = 0.10 * U, 0.04 * U, 0.19 * U
    tx1, ty1, tx2, ty2 = x1 + ix, y1 + it, x2 - ix, y2 - ib
    for i in range(c.s(y2 - 2) - c.s(ty2)):
        t = i / max(1, c.s(y2 - 2) - c.s(ty2))
        yy = c.s(ty2) + i
        xa = c.s(tx1) + (c.s(x1 + 3) - c.s(tx1)) * t
        xb = c.s(tx2) + (c.s(x2 - 3) - c.s(tx2)) * t
        c.draw.line([(xa, yy), (xb, yy)], fill=lerp(front, lerp(front, (0, 0, 0), 0.4), t) + (255,))
    for i in range(c.s(ix)):
        t = i / max(1, c.s(ix))
        c.draw.line([(c.s(x1) + i, c.s(y1 + 6) + i // 3), (c.s(x1) + i, c.s(y2 - 6))],
                    fill=lerp(lerp(side, (255, 255, 255), 0.12), side, t) + (255,))
        c.draw.line([(c.s(x2) - i, c.s(y1 + 6) + i // 3), (c.s(x2) - i, c.s(y2 - 6))],
                    fill=lerp(lerp(side, (0, 0, 0), 0.25), side, t) + (255,))
    c.vgrad_rrect(tx1, ty1, tx2, ty2, 6, None, None,
                  stops=[(0.0, lerp(top, (255, 255, 255), 0.20)), (0.07, top), (0.55, lerp(top, top2, 0.6)),
                         (1.0, top2)])
    lx1 = tx1
    if notch:
        # INTRO: the lower part is narrower on the left, as on the real L-shaped key.
        dx, dh = notch
        nx2 = ux(x + dx) - g / 2
        ny1 = uy(y + dh) + g / 2
        c.rrect(x1 - 4, ny1 - 1, nx2 + 1, y2 + 6, 4, (16, 16, 18, 255))
        c.draw.line([(c.s(x1 + 2), c.s(ny1 - 1)), (c.s(nx2), c.s(ny1 - 1))], fill=front + (255,), width=c.s(3))
        c.draw.line([(c.s(nx2 + 1), c.s(ny1)), (c.s(nx2 + 1), c.s(y2 - 3))], fill=side + (255,), width=c.s(3))
        lx1 = nx2
    sz = size or 0.30 * U
    if len(legends) == 1:
        c.text((lx1 + tx2) / 2, (ty1 + ty2) / 2, legends[0], sz, ink, "mm", max_w=tx2 - lx1 - 6)
    elif len(legends) == 2 and size is None:
        # Shifted character over the plain one, centred, as printed on the 464.
        c.text((tx1 + tx2) / 2, ty1 + (ty2 - ty1) * 0.30, legends[0], 0.24 * U, ink, "mm")
        c.text((tx1 + tx2) / 2, ty1 + (ty2 - ty1) * 0.74, legends[1], 0.27 * U, ink, "mm")
    elif len(legends) == 2:
        c.text((tx1 + tx2) / 2, ty1 + (ty2 - ty1) * 0.32, legends[0], sz, ink, "mm", max_w=tx2 - tx1 - 6)
        c.text((tx1 + tx2) / 2, ty1 + (ty2 - ty1) * 0.70, legends[1], sz, ink, "mm", max_w=tx2 - tx1 - 6)
    if code is not None:
        keys.append((code, flags, (x1, y1, x2, y2), name or " ".join(legends)))


def k(r, b):
    return r * 8 + b


R = ROW0
small = 0.17 * U

# Row 1.
cap(0, R, 1, 1, ["ESC"], "red", small, k(8, 2))
row = [(("!", "1"), k(8, 0)), (('"', "2"), k(8, 1)), (("#", "3"), k(7, 1)), (("$", "4"), k(7, 0)),
       (("%", "5"), k(6, 1)), (("&", "6"), k(6, 0)), (("'", "7"), k(5, 1)), (("(", "8"), k(5, 0)),
       ((")", "9"), k(4, 1)), (("_", "Ø"), k(4, 0)), (("=", "-"), k(3, 1)), (("£", "↑"), k(3, 0))]
x = 1.0
for leg, code in row:
    cap(x, R, 1, 1, list(leg), code=code)
    x += 1
cap(x, R, 1, 1, ["CLR"], size=small, code=k(2, 0))
cap(x + 1, R, MAIN_W - x - 1, 1, ["←BORR"], "green", small, k(9, 7))

# Row 2: TAB, Q..P, |@, *[, INTRO (L-shaped, rows 2-3).
cap(0, R + 1, 1.4, 1, ["TAB"], "green", small, k(8, 4))
x = 1.4
for ch, code in zip("QWERTYUIOP", [k(8, 3), k(7, 3), k(7, 2), k(6, 2), k(6, 3), k(5, 3), k(5, 2), k(4, 3),
                                  k(4, 2), k(3, 3)]):
    cap(x, R + 1, 1, 1, [ch], code=code)
    x += 1
cap(x, R + 1, 1, 1, ["|", "@"], code=k(3, 2))
cap(x + 1, R + 1, 1, 1, ["*", "["], code=k(2, 1))
cap(x + 2, R + 1, MAIN_W - x - 2, 2, ["INTRO"], "blue", small, k(2, 2), notch=(0.25, 1.0))

# Row 3: FIJA MAYS, A..L, Ñ, :;, +].
cap(0, R + 2, 1.65, 1, ["FIJA", "MAYS"], "green", 0.15 * U, k(8, 6))
x = 1.65
for ch, code in zip("ASDFGHJKL", [k(8, 5), k(7, 4), k(7, 5), k(6, 5), k(6, 4), k(5, 4), k(5, 5), k(4, 5),
                                 k(4, 4)]):
    cap(x, R + 2, 1, 1, [ch], code=code)
    x += 1
cap(x, R + 2, 1, 1, ["Ñ"], code=k(3, 5))
cap(x + 1, R + 2, 1, 1, [":", ";"], code=k(3, 4))
cap(x + 2, R + 2, 1, 1, ["+", "]"], code=k(2, 3))

# Row 4: MAYS, Z..\, MAYS.
cap(0, R + 3, 2.15, 1, ["MAYS"], "green", small, k(2, 5), 1, "MAYS (left)")
x = 2.15
for ch, code in zip("ZXCVBNM", [k(8, 7), k(7, 7), k(7, 6), k(6, 7), k(6, 6), k(5, 6), k(4, 6)]):
    cap(x, R + 3, 1, 1, [ch], code=code)
    x += 1
for leg, code in [(("<", ","), k(4, 7)), ((">", "."), k(3, 7)), (("?", "/"), k(3, 6)), (("`", "\\"), k(2, 6))]:
    cap(x, R + 3, 1, 1, list(leg), code=code)
    x += 1
cap(x, R + 3, MAIN_W - x, 1, ["MAYS"], "green", small, k(2, 5), 1, "MAYS (right)")

# Row 5: space bar and CTRL.
cap(3.2, R + 4, 8.85, 1, [""], code=k(5, 7), name="SPACE")
cap(12.05, R + 4, 1.0, 1, ["CTRL"], "green", 0.15 * U, k(2, 7), 1)

# Cursor cross with COPIA in the middle.
cap(PAD_X + 1, R - 2.55, 1, 0.9, ["↑"], size=0.34 * U, code=k(0, 0))
cap(PAD_X, R - 1.85, 1, 0.9, ["←"], size=0.34 * U, code=k(1, 0))
cap(PAD_X + 1, R - 1.85, 1, 0.9, ["COPIA"], "green", 0.15 * U, k(1, 1))
cap(PAD_X + 2, R - 1.85, 1, 0.9, ["→"], size=0.34 * U, code=k(0, 1))
cap(PAD_X + 1, R - 1.10, 1, 0.9, ["↓"], size=0.34 * U, code=k(0, 2))

# Numeric / function pad (f0-f9).
pad = [[("7", k(1, 2)), ("8", k(1, 3)), ("9", k(0, 3))],
       [("4", k(2, 4)), ("5", k(1, 4)), ("6", k(0, 4))],
       [("1", k(1, 5)), ("2", k(1, 6)), ("3", k(0, 5))],
       [("Ø", k(1, 7)), ("•", k(0, 7)), ("INTRO", k(0, 6))]]
for r, cells in enumerate(pad):
    for i, (leg, code) in enumerate(cells):
        if leg == "INTRO":
            cap(PAD_X + i, R + r, 1, 1, [leg], "blue", 0.15 * U, code)
        else:
            cap(PAD_X + i, R + r, 1, 1, [leg], size=0.36 * U if leg != "•" else 0.2 * U, code=code,
                name="f" + leg if leg.isdigit() else ("f0" if leg == "Ø" else "."))

# The L-shaped INTRO's rectangle overlaps "+]": list it last so the smaller
# keys win the hit test.
keys.sort(key=lambda kk: kk[0] == k(2, 2))
img = c.final()
if len(sys.argv) > 2:
    from PIL import Image
    bg = Image.new("RGBA", img.size, (0, 0, 120, 255))
    bg.alpha_composite(img)
    bg.convert("RGB").save(sys.argv[2])
size = write_inc(sys.argv[1], img, keys, "CpcVkb",
                 "// Generated by tools/gen_cpc464_vkb.py, do not edit.\n"
                 "// Amstrad CPC 464 (Spanish) keyboard; key codes are matrix line * 8 + bit.\n")
print(f"{len(keys)} keys, {size} bytes compressed")
