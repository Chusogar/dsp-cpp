#!/usr/bin/env python3
"""Generates src/drivers/computers/c64_vkb_data.inc: the C64 on-screen keyboard.

The picture of a "breadbin" Commodore 64 keyboard is drawn here from
scratch (no photographs, no ROM data): brown keycaps with their top face,
side and front skirts in perspective, tan function keys, the recessed key
well and the beige case with its power LED.  The top legends are rendered
with FreeSans Bold.  The PETSCII graphics printed on the front of the keys
come from the character ROM the emulator loads, so this script only
records where they go and which screen codes they are.

usage: gen_c64_vkb.py out.inc [preview.png]
requires: Pillow, FreeSans Bold (fonts-freefont-ttf)
"""
import sys
import zlib

from PIL import Image, ImageDraw, ImageFilter, ImageFont

SS = 3                      # supersampling factor
W, H = 1536, 488            # final picture size
U = 80.0                    # one key unit, final pixels
LEFT = (W - 18.25 * U) / 2  # keyboard origin
TOP = 52.0
GAP = 0.055                 # space between caps, in units

FONT = "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf"
FONT_PLAIN = "/usr/share/fonts/truetype/freefont/FreeSans.ttf"

# Breadbin colours.
CASE_TOP = (205, 192, 162)
CASE_BOTTOM = (178, 163, 131)
WELL = (38, 29, 24)
BROWN = dict(top=(92, 76, 64), top2=(74, 60, 50), side=(60, 47, 39), front=(46, 35, 29),
             edge=(110, 94, 80), legend=(236, 230, 214))
TAN = dict(top=(214, 196, 158), top2=(196, 177, 138), side=(170, 151, 114), front=(150, 132, 98),
           edge=(232, 218, 186), legend=(58, 44, 36))

NONE = 0xFF


def k(label, col, row, w=1.0, front=None, glyphs=(NONE, NONE), size=None, flags=0, tan=False):
    return dict(label=label, col=col, row=row, w=w, front=front, glyphs=glyphs, size=size, flags=flags,
                tan=tan)


# glyphs = (C= graphic, SHIFT graphic) screen codes from the KERNAL tables.
G = {
    "A": (112, 65), "B": (127, 66), "C": (124, 67), "D": (108, 68), "E": (113, 69), "F": (123, 70),
    "G": (101, 71), "H": (116, 72), "I": (98, 73), "J": (117, 74), "K": (97, 75), "L": (118, 76),
    "M": (103, 77), "N": (106, 78), "O": (121, 79), "P": (111, 80), "Q": (107, 81), "R": (114, 82),
    "S": (110, 83), "T": (99, 84), "U": (120, 85), "V": (126, 86), "W": (115, 87), "X": (125, 88),
    "Y": (119, 89), "Z": (109, 90), "+": (102, 91), "-": (92, 93), "£": (104, 105), "@": (100, 122),
    "*": (95, 64), "↑": (NONE, 94),
}
# Matrix position (column = CIA1 port A line, row = port B bit) of each key.
M = {
    "DEL": (0, 0), "RETURN": (0, 1), "CRSR_LR": (0, 2), "F7": (0, 3), "F1": (0, 4), "F3": (0, 5),
    "F5": (0, 6), "CRSR_UD": (0, 7),
    "3": (1, 0), "W": (1, 1), "A": (1, 2), "4": (1, 3), "Z": (1, 4), "S": (1, 5), "E": (1, 6), "LSHIFT": (1, 7),
    "5": (2, 0), "R": (2, 1), "D": (2, 2), "6": (2, 3), "C": (2, 4), "F": (2, 5), "T": (2, 6), "X": (2, 7),
    "7": (3, 0), "Y": (3, 1), "G": (3, 2), "8": (3, 3), "B": (3, 4), "H": (3, 5), "U": (3, 6), "V": (3, 7),
    "9": (4, 0), "I": (4, 1), "J": (4, 2), "0": (4, 3), "M": (4, 4), "K": (4, 5), "O": (4, 6), "N": (4, 7),
    "+": (5, 0), "P": (5, 1), "L": (5, 2), "-": (5, 3), ".": (5, 4), ":": (5, 5), "@": (5, 6), ",": (5, 7),
    "£": (6, 0), "*": (6, 1), ";": (6, 2), "HOME": (6, 3), "RSHIFT": (6, 4), "=": (6, 5), "↑": (6, 6),
    "/": (6, 7),
    "1": (7, 0), "←": (7, 1), "CTRL": (7, 2), "2": (7, 3), "SPACE": (7, 4), "CBM": (7, 5), "Q": (7, 6),
    "STOP": (7, 7),
}
FLAG_MOD, FLAG_RESTORE, FLAG_LOCK = 1, 2, 4


def letter(c):
    col, row = M[c]
    return k([c], col, row, glyphs=G[c])


def digit(d, shifted, colour):
    col, row = M[d]
    return k([shifted, d] if shifted else [d], col, row, front=colour)


def sym(c, top=None):
    col, row = M[c]
    return k(top or [c], col, row, glyphs=G.get(c, (NONE, NONE)))


rows = [
    (0.0, [sym("←"), digit("1", "!", "BLK"), digit("2", '"', "WHT"), digit("3", "#", "RED"),
           digit("4", "$", "CYN"), digit("5", "%", "PUR"), digit("6", "&", "GRN"), digit("7", "'", "BLU"),
           digit("8", "(", "YEL"), digit("9", ")", "RVS ON"), digit("0", None, "RVS OFF"), sym("+"), sym("-"),
           sym("£"), k(["CLR", "HOME"], *M["HOME"], size=0.17), k(["INST", "DEL"], *M["DEL"], size=0.17)]),
    (0.0, [k(["CTRL"], *M["CTRL"], w=1.5, size=0.19, flags=FLAG_MOD)] + [letter(c) for c in "QWERTYUIOP"] +
     [sym("@"), sym("*"), sym("↑"), k(["RESTORE"], -1, -1, w=1.5, size=0.17, flags=FLAG_RESTORE)]),
    (0.0, [k(["RUN", "STOP"], *M["STOP"], size=0.17), k(["SHIFT", "LOCK"], -1, -1, size=0.15, flags=FLAG_LOCK)] +
     [letter(c) for c in "ASDFGHJKL"] + [sym(":", ["[", ":"]), sym(";", ["]", ";"]), sym("="),
                                         k(["RETURN"], *M["RETURN"], w=2.0, size=0.19)]),
    (0.0, [k(["C="], *M["CBM"], size=0.24, flags=FLAG_MOD), k(["SHIFT"], *M["LSHIFT"], w=1.5, size=0.19,
                                                              flags=FLAG_MOD)] +
     [letter(c) for c in "ZXCVBNM"] + [sym(",", ["<", ","]), sym(".", [">", "."]), sym("/", ["?", "/"]),
                                       k(["SHIFT"], *M["RSHIFT"], w=1.5, size=0.19, flags=FLAG_MOD),
                                       k(["↑", "CRSR", "↓"], *M["CRSR_UD"], size=0.15),
                                       k(["←", "CRSR", "→"], *M["CRSR_LR"], size=0.15)]),
    (3.5, [k([""], *M["SPACE"], w=9.0)]),
]
fkeys = [("f1", "f2", "F1"), ("f3", "f4", "F3"), ("f5", "f6", "F5"), ("f7", "f8", "F7")]


def S(v):
    return int(round(v * SS))


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def vgrad(draw, box, c1, c2):
    x1, y1, x2, y2 = box
    for y in range(y1, y2):
        draw.line([(x1, y), (x2 - 1, y)], fill=lerp(c1, c2, (y - y1) / max(1, y2 - y1 - 1)))


def font(size, plain=False):
    return ImageFont.truetype(FONT_PLAIN if plain else FONT, S(size))


img = Image.new("RGB", (W * SS, H * SS))
d = ImageDraw.Draw(img)
vgrad(d, (0, 0, W * SS, H * SS), CASE_TOP, CASE_BOTTOM)
# Fine horizontal moulding texture on the case.
for y in range(0, H * SS, SS * 3):
    d.line([(0, y), (W * SS, y)], fill=lerp(lerp(CASE_TOP, CASE_BOTTOM, y / (H * SS)), (0, 0, 0), 0.03), width=1)
img = img.filter(ImageFilter.GaussianBlur(SS * 0.6))
d = ImageDraw.Draw(img)

# Key well: the cut-out in the case the caps sit in.
kb_w = 18.25 * U
well = (S(LEFT - 10), S(TOP - 10), S(LEFT + kb_w + 10), S(TOP + 5 * U + 10))
shadow = Image.new("L", img.size, 0)
ImageDraw.Draw(shadow).rounded_rectangle(well, radius=S(10), fill=255)
d.rounded_rectangle((well[0] - S(2), well[1] - S(3), well[2] + S(2), well[3] + S(2)), radius=S(12),
                    fill=lerp(CASE_TOP, (0, 0, 0), 0.35))
d.rounded_rectangle((well[0], well[1], well[2] + S(1), well[3] + S(3)), radius=S(10),
                    fill=lerp(CASE_TOP, (255, 255, 255), 0.25))
d.rounded_rectangle(well, radius=S(10), fill=WELL)

# Power LED and its label, top right.
led_x, led_y = LEFT + kb_w - 40, 24
d.ellipse((S(led_x - 9), S(led_y - 7), S(led_x + 9), S(led_y + 7)), fill=(60, 30, 26))
d.ellipse((S(led_x - 7), S(led_y - 5), S(led_x + 7), S(led_y + 5)), fill=(222, 40, 30))
d.ellipse((S(led_x - 4), S(led_y - 4), S(led_x + 1), S(led_y - 1)), fill=(255, 170, 150))
d.text((S(led_x - 18), S(led_y)), "POWER", font=font(13), anchor="rm", fill=(112, 98, 76))

keys_out = []
caps = Image.new("RGBA", img.size, (0, 0, 0, 0))
cd = ImageDraw.Draw(caps)
cap_shadow = Image.new("L", img.size, 0)
csd = ImageDraw.Draw(cap_shadow)


def draw_cap(x, y, w, legends, size, front, glyphs, tan):
    pal = TAN if tan else BROWN
    g = GAP * U
    x1, y1, x2, y2 = x + g / 2, y + g / 2, x + w * U - g / 2, y + U - g / 2
    csd.rounded_rectangle((S(x1 - 1), S(y1 + 4), S(x2 + 1), S(y2 + 4)), radius=S(9), fill=200)
    # Skirt (sides), seen from the front and slightly above.
    cd.rounded_rectangle((S(x1), S(y1), S(x2), S(y2)), radius=S(7), fill=pal["side"])
    inset_x, inset_top, inset_bot = 0.11 * U, 0.035 * U, 0.25 * U
    tx1, ty1, tx2, ty2 = x1 + inset_x, y1 + inset_top, x2 - inset_x, y2 - inset_bot
    # Front skirt: a trapezoid from the bottom of the top face to the base.
    cd.polygon([(S(tx1), S(ty2)), (S(tx2), S(ty2)), (S(x2 - 3), S(y2 - 2)), (S(x1 + 3), S(y2 - 2))],
               fill=pal["front"])
    for i in range(S(y2 - 2) - S(ty2)):
        t = i / max(1, S(y2 - 2) - S(ty2))
        yy = S(ty2) + i
        xa = S(tx1) + (S(x1 + 3) - S(tx1)) * t
        xb = S(tx2) + (S(x2 - 3) - S(tx2)) * t
        cd.line([(xa, yy), (xb, yy)], fill=lerp(pal["front"], lerp(pal["front"], (0, 0, 0), 0.35), t))
    # Side skirts get a soft left-to-right falloff.
    for i in range(S(inset_x)):
        t = i / max(1, S(inset_x))
        c = lerp(lerp(pal["side"], (255, 255, 255), 0.08), pal["side"], t)
        cd.line([(S(x1) + i, S(y1 + 6) + i // 3), (S(x1) + i, S(y2 - 6))], fill=c)
        c = lerp(lerp(pal["side"], (0, 0, 0), 0.18), pal["side"], t)
        cd.line([(S(x2) - i, S(y1 + 6) + i // 3), (S(x2) - i, S(y2 - 6))], fill=c)
    # Dished top face.
    cd.rounded_rectangle((S(tx1), S(ty1), S(tx2), S(ty2)), radius=S(6), fill=pal["top"])
    face = Image.new("RGB", (S(tx2) - S(tx1), S(ty2) - S(ty1)))
    fd = ImageDraw.Draw(face)
    vgrad(fd, (0, 0, face.width, face.height), lerp(pal["top"], pal["edge"], 0.25), pal["top2"])
    # Concave dish: a darker band through the middle of the face.
    dish = Image.new("L", face.size, 0)
    ImageDraw.Draw(dish).ellipse((-face.width * 0.15, face.height * 0.1, face.width * 1.15, face.height * 1.5),
                                 fill=70)
    dish = dish.filter(ImageFilter.GaussianBlur(S(6)))
    face = Image.composite(Image.new("RGB", face.size, lerp(pal["top2"], (0, 0, 0), 0.12)), face, dish)
    mask = Image.new("L", face.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, face.width - 1, face.height - 1), radius=S(6), fill=255)
    caps.paste(face, (S(tx1), S(ty1)), mask)
    # Highlight on the top edge of the face.
    cd.line([(S(tx1 + 5), S(ty1 + 0.6)), (S(tx2 - 5), S(ty1 + 0.6))], fill=pal["edge"], width=S(1.2))
    # Legends.
    if legends and legends != [""]:
        sz = (size or (0.30 if len(legends) == 1 else 0.24)) * U
        f = font(sz)
        if tan:
            cd.text((S((tx1 + tx2) / 2), S((ty1 + ty2) / 2 + 1)), legends[0], font=f, anchor="mm",
                    fill=pal["legend"])
        elif len(legends) == 1 and len(legends[0]) <= 1:
            cd.text((S(tx1 + 0.12 * U), S(ty1 + 0.08 * U)), legends[0], font=f, anchor="la", fill=pal["legend"])
        elif len(legends) == 1:
            cd.text((S((tx1 + tx2) / 2), S((ty1 + ty2) / 2)), legends[0], font=f, anchor="mm", fill=pal["legend"])
        else:
            n = len(legends)
            line_h = (ty2 - ty1 - 0.08 * U) / n
            centred = n == 3 or max(len(s) for s in legends) > 2
            for i, s in enumerate(legends):
                cy = ty1 + 0.04 * U + line_h * (i + 0.5)
                if centred:
                    cd.text((S((tx1 + tx2) / 2), S(cy)), s, font=f, anchor="mm", fill=pal["legend"])
                else:
                    cd.text((S(tx1 + 0.12 * U), S(cy)), s, font=f, anchor="lm", fill=pal["legend"])
    fx1, fy1, fx2, fy2 = tx1, ty2 + 1, tx2, y2 - 3
    if front:
        fsz = 0.105 * U if " " in front else 0.13 * U
        cd.text((S((fx1 + fx2) / 2), S((fy1 + fy2) / 2 + 0.5)), front, font=font(fsz),
                anchor="mm", fill=lerp(pal["legend"], pal["front"], 0.15))
    return (x1, y1, x2, y2), (fx1, fy1, fx2, fy2)


for r, (xoff, row) in enumerate(rows):
    x = LEFT + xoff * U
    y = TOP + r * U
    for key in row:
        rect, front_rect = draw_cap(x, y, key["w"], key["label"], key["size"], key["front"], key["glyphs"],
                                    False)
        keys_out.append((key, rect, front_rect))
        x += key["w"] * U
for r, (top, front, name) in enumerate(fkeys):
    x = LEFT + 16.75 * U
    y = TOP + r * U
    col, row = M[name]
    key = k([top], col, row, w=1.5, front=front, tan=True)
    rect, front_rect = draw_cap(x, y, 1.5, [top], 0.26, front, (NONE, NONE), True)
    keys_out.append((key, rect, front_rect))

cap_shadow = cap_shadow.filter(ImageFilter.GaussianBlur(S(3)))
img.paste(Image.new("RGB", img.size, (8, 5, 4)), (0, 0), cap_shadow.point(lambda v: v * 0.8))
img.paste(caps, (0, 0), caps)
img = img.resize((W, H), Image.LANCZOS)

if len(sys.argv) > 2:
    img.save(sys.argv[2])

raw = img.tobytes()
# Rows are stored as deltas against the previous row: the gradients and
# repeated caps compress much better that way.
filt = bytearray(len(raw))
stride = W * 3
for y in range(H):
    for i in range(stride):
        p = y * stride + i
        filt[p] = (raw[p] - (raw[p - stride] if y else 0)) & 0xFF
data = zlib.compress(bytes(filt), 9)

with open(sys.argv[1], "w") as out:
    out.write("// Generated by tools/gen_c64_vkb.py, do not edit.\n")
    out.write(f"// Keyboard picture: {W}x{H} RGB, each row a byte delta of the row above, zlib.\n")
    out.write(f"constexpr int kC64VkbWidth = {W};\nconstexpr int kC64VkbHeight = {H};\n")
    out.write("const unsigned char kC64VkbPicture[] = {\n")
    for i in range(0, len(data), 24):
        out.write("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",\n")
    out.write("};\n")
    out.write("// matrix column, row (-1: not in the matrix), flags (1 modifier, 2 RESTORE, 4 SHIFT LOCK),\n")
    out.write("// cap rect, front face rect, front PETSCII screen codes (C=, SHIFT; 255 = none).\n")
    out.write("const C64VkbKey kC64VkbKeys[] = {\n")
    for key, (x1, y1, x2, y2), (fx1, fy1, fx2, fy2) in keys_out:
        name = " ".join(key["label"]) or "SPACE"
        out.write(f"    {{{key['col']}, {key['row']}, {key['flags']}, {round(x1)}, {round(y1)}, {round(x2)}, "
                  f"{round(y2)}, {round(fx1)}, {round(fy1)}, {round(fx2)}, {round(fy2)}, {key['glyphs'][0]}, "
                  f"{key['glyphs'][1]}}},  // {name}\n")
    out.write("};\n")
print(f"{len(keys_out)} keys, picture {len(data)} bytes compressed")
