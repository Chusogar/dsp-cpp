#!/usr/bin/env python3
"""Generates src/drivers/computers/ql_vkb_data.inc: Sinclair QL keyboard.

Modelled on the QL's black wedge: low, domed black keys with white
legends, the F1-F5 column on the left, TABULATE / CAPS LOCK / SHIFT / CTRL /
ALT, the inverted-L ENTER, the cursor keys either side of the space bar, and
the ribbed black case. Drawn from scratch with FreeSans; no logos.

usage: gen_ql_vkb.py out.inc [preview.png]
"""
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy.ndimage import binary_closing, distance_transform_edt, gaussian_filter

from vkb_common import SS, Canvas, lerp, write_inc

W, H = 1536, 520
U = 84.0
FCOL = 1.25                 # width of the F-key column
GAP_F = 0.55                # between the F column and the main block
LEFT = (W - (FCOL + GAP_F + 15.0) * U) / 2
MAIN = LEFT + (FCOL + GAP_F) * U
TOP = 64.0

KEY = (30, 30, 33)
KEY_TOP = (58, 58, 63)
KEY_EDGE = (12, 12, 14)
INK = (236, 236, 236)

c = Canvas(W, H)
keys = []


# Light from the top, a little to the left and in front (image y grows down).
_L = np.array([-0.28, -0.70, 0.66])
_L /= np.linalg.norm(_L)
_HV = _L + np.array([0.0, 0.0, 1.0])
_HV /= np.linalg.norm(_HV)


def shade_cap(boxes, base=(23, 23, 26)):
    """Paints a glossy, domed black cap whose outline is the union of the
    rounded boxes [(x1, y1, x2, y2, r)] (final-picture units). The surface is
    a height field: a quarter-round bevel at the rim rising into a low dome,
    lit with diffuse + two specular lobes, like the QL's moulded keys."""
    m = 6
    bx1 = min(b[0] for b in boxes) - m
    by1 = min(b[1] for b in boxes) - m
    bx2 = max(b[2] for b in boxes) + m
    by2 = max(b[3] for b in boxes) + m
    ox, oy = c.s(bx1), c.s(by1)
    w, h = c.s(bx2) - ox, c.s(by2) - oy
    mask = Image.new("L", (w, h), 0)
    md = ImageDraw.Draw(mask)
    for x1, y1, x2, y2, r in boxes:
        md.rounded_rectangle((c.s(x1) - ox, c.s(y1) - oy, c.s(x2) - ox, c.s(y2) - oy), radius=c.s(r), fill=255)
    a = np.asarray(mask, dtype=np.float32) / 255.0
    if len(boxes) > 1:
        # Round the inside corner of composite (L-shaped) caps.
        rr = int(0.10 * U * SS)
        yy, xx = np.mgrid[-rr:rr + 1, -rr:rr + 1]
        disk = xx * xx + yy * yy <= rr * rr
        padded = np.pad(a > 0.5, rr + 2)
        a = binary_closing(padded, structure=disk)[rr + 2:-rr - 2, rr + 2:-rr - 2].astype(np.float32)
    d = distance_transform_edt(a > 0.5).astype(np.float32)
    bevel = 0.15 * U * SS
    t = np.clip(d / bevel, 0.0, 1.0)
    height = bevel * 0.55 * np.sqrt(1.0 - (1.0 - t) ** 2)
    height += bevel * 0.22 * (1.0 - np.exp(-d / (2.2 * bevel)))   # gentle dome over the top
    height = gaussian_filter(height, 1.2)
    gy, gx = np.gradient(height)
    n = np.dstack([-gx, -gy, np.ones_like(gx)])
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    diff = np.clip(n @ _L, 0.0, 1.0)
    nh = np.clip(n @ _HV, 0.0, 1.0)
    spec = 0.75 * nh ** 70 + 0.16 * nh ** 9
    # Sky reflection: faces tilted towards the top pick up a little light.
    sky = np.clip(-n[:, :, 1], 0.0, 1.0) * 0.10
    col = np.array(base, dtype=np.float32) / 255.0
    rgb = col[None, None, :] * (0.55 + 0.95 * diff[:, :, None]) + (spec + sky)[:, :, None]
    rgb = np.clip(rgb, 0.0, 1.0)
    out = np.dstack([rgb * 255.0, a * 255.0]).astype(np.uint8)
    layer = Image.fromarray(out, "RGBA")
    c.img.alpha_composite(layer, (ox, oy))

# Case: black, with the fine ribbing of the QL's top.
c.shadow([(14, 10, W - 14, H - 4, 14)], 7, alpha=170)
c.vgrad_rrect(8, 4, W - 8, H - 8, 12, (38, 38, 41), (20, 20, 22))
for i in range(0, 22):
    y = 12 + i * 2.0
    c.draw.line([(c.s(20), c.s(y)), (c.s(W - 20), c.s(y))], fill=(46, 46, 50, 255) if i % 2 else (26, 26, 28, 255),
                width=c.s(1))
# Recess around the keys (the keyboard sits in a shallow tray).
tray = (LEFT - 14, TOP - 14, MAIN + 15.0 * U + 14, TOP + 5 * U + 12)
c.rrect(tray[0] - 2, tray[1] - 2, tray[2] + 2, tray[3] + 3, 10, (44, 44, 48, 255))
c.vgrad_rrect(*tray, 9, (16, 16, 18), (22, 22, 25))


def key(x0, row, x, w, legends, code, flags=0, name="", size=None, h=1.0, base=None):
    """x in units from base (MAIN by default); row 0-4."""
    bx = MAIN if base is None else base
    g = 0.20 * U
    x1, y1 = bx + x * U + g / 2, TOP + row * U + g / 2
    x2, y2 = bx + (x + w) * U - g / 2, TOP + (row + h) * U - g / 2
    r = min(0.24 * U, (y2 - y1) * 0.36)
    # The socket the key stands in, and the key's shadow on the tray.
    c.rrect(x1 - 2.5, y1 - 1.5, x2 + 2.5, y2 + 3.5, r + 2, (6, 6, 7, 255))
    c.shadow([(x1, y1 + 4, x2, y2 + 6, r)], 4, alpha=235)
    shade_cap([(x1, y1, x2, y2, r)])
    cx, cy = (x1 + x2) / 2, (y1 + y2) / 2 - 2
    s = size or 0.30 * U
    if len(legends) == 1:
        c.text(cx, cy, legends[0], s, INK, "mm", max_w=x2 - x1 - 10)
    elif len(legends) == 2:
        c.text(cx, cy - 0.17 * U, legends[0], size or 0.21 * U, INK, "mm", max_w=x2 - x1 - 10)
        c.text(cx, cy + 0.15 * U, legends[1], size or 0.24 * U, INK, "mm", max_w=x2 - x1 - 10)
    if code is not None:
        keys.append((code, flags, (x1, y1, x2, y2), name or " ".join(legends)))
    return x1, y1, x2, y2


def arrow(rect, direction):
    """Hollow block arrow, as printed on the QL cursor keys."""
    x1, y1, x2, y2 = rect
    cx, cy = (x1 + x2) / 2, (y1 + y2) / 2 - 3
    a = 0.20 * U
    pts = [(-a, -0.35 * a), (0.1 * a, -0.35 * a), (0.1 * a, -0.85 * a), (a, 0), (0.1 * a, 0.85 * a),
           (0.1 * a, 0.35 * a), (-a, 0.35 * a)]
    rot = {"right": lambda p: p, "left": lambda p: (-p[0], p[1]), "down": lambda p: (p[1], p[0]),
           "up": lambda p: (p[1], -p[0])}[direction]
    poly = [(c.s(cx + rot(p)[0]), c.s(cy + rot(p)[1])) for p in pts]
    c.draw.polygon(poly, outline=INK + (255,), width=c.s(1.6))


def m(row, mask):
    return row * 8 + (mask.bit_length() - 1)


# F1-F5 column.
for i, (name, code) in enumerate([("F1", m(0, 0x02)), ("F2", m(0, 0x08)), ("F3", m(0, 0x10)), ("F4", m(0, 0x01)),
                                  ("F5", m(0, 0x20))]):
    key(0, i, 0, FCOL, [name], code, size=0.22 * U, base=LEFT)

# Row 1.
row1 = [(("©", "ESC"), m(1, 0x08)), (("!", "1"), m(4, 0x08)), (("@", "2"), m(6, 0x02)),
        (("#", "3"), m(4, 0x02)), (("$", "4"), m(0, 0x40)), (("%", "5"), m(0, 0x04)), (("^", "6"), m(6, 0x04)),
        (("&", "7"), m(0, 0x80)), (("*", "8"), m(6, 0x01)), (("(", "9"), m(5, 0x01)), ((")", "0"), m(6, 0x20)),
        (("_", "–"), m(5, 0x20)), (("+", "="), m(3, 0x20)), (("~", "£"), m(2, 0x20)),
        (("|", "\\"), m(1, 0x20))]
for i, (leg, code) in enumerate(row1):
    key(0, 0, i, 1, list(leg), code, size=0.17 * U if leg[1] == "ESC" else None)

# Row 2: TABULATE, Q..P, { [, } ], ENTER (upper part).
key(0, 1, 0, 1.5, ["TABULATE"], m(5, 0x08), size=0.15 * U)
for i, (ch, code) in enumerate(zip("QWERTYUIOP", [m(6, 0x08), m(5, 0x02), m(6, 0x10), m(5, 0x10), m(6, 0x40),
                                                 m(5, 0x40), m(6, 0x80), m(5, 0x04), m(5, 0x80), m(4, 0x20)])):
    key(0, 1, 1.5 + i, 1, [ch], code)
key(0, 1, 11.5, 1, ["{", "["], m(3, 0x01))
key(0, 1, 12.5, 1, ["}", "]"], m(2, 0x01))

# Row 3: CAPS LOCK, A..L, : ;, " ', ENTER.
key(0, 2, 0, 1.75, ["CAPS LOCK"], m(3, 0x02), size=0.15 * U)
for i, (ch, code) in enumerate(zip("ASDFGHJKL", [m(4, 0x10), m(3, 0x08), m(4, 0x40), m(3, 0x10), m(3, 0x40),
                                                m(4, 0x04), m(4, 0x80), m(3, 0x04), m(4, 0x01)])):
    key(0, 2, 1.75 + i, 1, [ch], code)
key(0, 2, 10.75, 1, [":", ";"], m(3, 0x80))
key(0, 2, 11.75, 1, ['"', "'"], m(2, 0x80))
# ENTER: an inverted L, the narrow upper part over the wide lower one, as one cap.
g = 0.20 * U
ex1, ey1, ex2, ey2 = MAIN + 12.75 * U + g / 2, TOP + 2 * U + g / 2, MAIN + 14.5 * U - g / 2, TOP + 3 * U - g / 2
ux1, uy1 = MAIN + 13.5 * U + g / 2, TOP + 1 * U + g / 2
r = (ey2 - ey1) * 0.36
for dx, dy, grow, col in [(0, -1.5, 2.5, (6, 6, 7, 255))]:
    c.rrect(ex1 - grow, ey1 + dy, ex2 + grow, ey2 + 3.5, r + 2, col)
    c.rrect(ux1 - grow, uy1 + dy, ex2 + grow, ey2 + 3.5, r + 2, col)
c.shadow([(ex1, ey1 + 4, ex2, ey2 + 6, r), (ux1, uy1 + 4, ex2, ey2 + 6, r)], 4, alpha=235)
shade_cap([(ex1, ey1, ex2, ey2, r), (ux1, uy1, ex2, ey2, r), (ux1, ey1 - r, ex2 - r, ey1 + r, 0)])
c.text((ex1 + ex2) / 2, (ey1 + ey2) / 2 - 2, "ENTER", 0.17 * U, INK, "mm")
keys.append((m(1, 0x01), 0, (ux1, uy1, ex2, ey1), "ENTER (upper)"))
keys.append((m(1, 0x01), 0, (ex1, ey1, ex2, ey2), "ENTER"))

# Row 4: SHIFT, Z../, SHIFT.
key(0, 3, 0, 2.1, ["SHIFT"], m(7, 0x01), 1, "SHIFT (left)", size=0.17 * U)
for i, (ch, code) in enumerate(zip("ZXCVBNM", [m(2, 0x02), m(7, 0x08), m(2, 0x08), m(7, 0x10), m(2, 0x10),
                                              m(7, 0x40), m(2, 0x40)])):
    key(0, 3, 2.1 + i, 1, [ch], code)
for i, (leg, code) in enumerate([(("<", ","), m(7, 0x80)), ((">", "."), m(2, 0x04)), (("?", "/"), m(7, 0x20))]):
    key(0, 3, 9.1 + i, 1, list(leg), code)
key(0, 3, 12.1, 2.4, ["SHIFT"], m(7, 0x01), 1, "SHIFT (right)", size=0.17 * U)

# Row 5: CTRL, cursor left/right, space bar, cursor up/down, ALT.
key(0, 4, 0, 1.6, ["•  CTRL"], m(7, 0x02), 1, "CTRL", size=0.16 * U)
arrow(key(0, 4, 1.65, 0.95, [], m(1, 0x02), name="LEFT"), "left")
arrow(key(0, 4, 2.65, 0.95, [], m(1, 0x10), name="RIGHT"), "right")
key(0, 4, 3.75, 6.85, [""], m(1, 0x40), name="SPACE")
arrow(key(0, 4, 10.75, 0.95, [], m(1, 0x04), name="UP"), "up")
arrow(key(0, 4, 11.75, 0.95, [], m(1, 0x80), name="DOWN"), "down")
key(0, 4, 12.85, 1.65, ["ALT"], m(7, 0x04), 1, size=0.17 * U)

img = c.final()
if len(sys.argv) > 2:
    from PIL import Image
    bg = Image.new("RGBA", img.size, (0, 0, 120, 255))
    bg.alpha_composite(img)
    bg.convert("RGB").save(sys.argv[2])
size = write_inc(sys.argv[1], img, keys, "QlVkb",
                 "// Generated by tools/gen_ql_vkb.py, do not edit.\n"
                 "// Sinclair QL keyboard; key codes are IPC matrix row * 8 + bit.\n")
print(f"{len(keys)} keys, {size} bytes compressed")
