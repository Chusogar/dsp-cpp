#!/usr/bin/env python3
"""Generates src/drivers/computers/spectrum_vkb_data.inc: ZX Spectrum 48K keyboard.

The 40 grey-blue rubber keys standing through the black keyboard plate, with
everything the 48K prints: on the keys the character, keyword, red SYMBOL
SHIFT symbol and block graphic; on the plate the green extended-mode words
above, the red ones below, and the colour names over the number row. The
picture is drawn here from scratch (no photographs, no logos) with FreeSans.

usage: gen_zx48_vkb.py out.inc [preview.png]
"""
import sys

from vkb_common import Canvas, lerp, write_inc

W, H = 1536, 560
U = 106.0                       # key pitch
KEY_W, KEY_H = 0.80 * U, 0.56 * U
ROW_X = [0.0, 0.5, 0.75, 0.0]   # stagger of the four rows, in keys
PLATE_W = 10.75 * U + 0.55 * U
PLATE_X = (W - PLATE_W) / 2
TOP = 18.0

PLATE = (22, 22, 24)
GREEN = (70, 205, 90)
RED = (238, 52, 48)
WHITE = (236, 236, 232)
KEY_TOP = (160, 166, 176)
KEY_MID = (138, 145, 156)
KEY_BOT = (110, 116, 128)

COLOURS = {"BLUE": (92, 118, 255), "RED": (240, 50, 45), "MAGENTA": (236, 72, 236), "GREEN": (60, 210, 70),
           "CYAN": (60, 214, 222), "YELLOW": (240, 232, 60), "WHITE": WHITE}

# (code, main, keyword, red symbol, green above, red below)
NUM = [
    (3 * 8 + 0, "1", "EDIT", "!", "BLUE", "DEF FN", 0b0001),
    (3 * 8 + 1, "2", "CAPS LOCK", "@", "RED", "FN", 0b0010),
    (3 * 8 + 2, "3", "TRUE VIDEO", "#", "MAGENTA", "LINE", 0b0011),
    (3 * 8 + 3, "4", "INV. VIDEO", "$", "GREEN", "OPEN #", 0b0100),
    (3 * 8 + 4, "5", "←", "%", "CYAN", "CLOSE #", 0b0101),
    (4 * 8 + 4, "6", "↓", "&", "YELLOW", "MOVE", 0b0110),
    (4 * 8 + 3, "7", "↑", "'", "WHITE", "ERASE", 0b0111),
    (4 * 8 + 2, "8", "→", "(", None, "POINT", 0b0000),
    (4 * 8 + 1, "9", "GRAPHICS", ")", None, "CAT", None),
    (4 * 8 + 0, "0", "DELETE", "_", "BLACK", "FORMAT", None),
]
LET = {
    "Q": (2 * 8 + 0, "PLOT", "<=", "SIN", "ASN"), "W": (2 * 8 + 1, "DRAW", "<>", "COS", "ACS"),
    "E": (2 * 8 + 2, "REM", ">=", "TAN", "ATN"), "R": (2 * 8 + 3, "RUN", "<", "INT", "VERIFY"),
    "T": (2 * 8 + 4, "RAND", ">", "RND", "MERGE"), "Y": (5 * 8 + 4, "RETURN", "AND", "STR$", "["),
    "U": (5 * 8 + 3, "IF", "OR", "CHR$", "]"), "I": (5 * 8 + 2, "INPUT", "AT", "CODE", "IN"),
    "O": (5 * 8 + 1, "POKE", ";", "PEEK", "OUT"), "P": (5 * 8 + 0, "PRINT", '"', "TAB", "©"),
    "A": (1 * 8 + 0, "NEW", "STOP", "READ", "~"), "S": (1 * 8 + 1, "SAVE", "NOT", "RESTORE", "|"),
    "D": (1 * 8 + 2, "DIM", "STEP", "DATA", "\\"), "F": (1 * 8 + 3, "FOR", "TO", "SGN", "{"),
    "G": (1 * 8 + 4, "GO TO", "THEN", "ABS", "}"), "H": (6 * 8 + 4, "GO SUB", "↑", "SQR", "CIRCLE"),
    "J": (6 * 8 + 3, "LOAD", "-", "VAL", "VAL$"), "K": (6 * 8 + 2, "LIST", "+", "LEN", "SCREEN$"),
    "L": (6 * 8 + 1, "LET", "=", "USR", "ATTR"), "Z": (0 * 8 + 1, "COPY", ":", "LN", "BEEP"),
    "X": (0 * 8 + 2, "CLEAR", "£", "EXP", "INK"), "C": (0 * 8 + 3, "CONT", "?", "LPRINT", "PAPER"),
    "V": (0 * 8 + 4, "CLS", "/", "LLIST", "FLASH"), "B": (7 * 8 + 4, "BORDER", "*", "BIN", "BRIGHT"),
    "N": (7 * 8 + 3, "NEXT", ",", "INKEY$", "OVER"), "M": (7 * 8 + 2, "PAUSE", ".", "PI", "INVERSE"),
}

c = Canvas(W, H)

# Case and plate.
plate_h = H - 2 * 10
c.shadow([(PLATE_X - 6, 14, PLATE_X + PLATE_W + 6, H - 4, 22)], 8, alpha=150)
c.vgrad_rrect(PLATE_X - 14, 6, PLATE_X + PLATE_W + 14, H - 6, 22, (40, 40, 42), (8, 8, 9))
c.vgrad_rrect(PLATE_X, 14, PLATE_X + PLATE_W, H - 14, 10, (34, 34, 37), (16, 16, 18))
keys = []
key_rects = []


def key_shape(x, y):
    return (x, y, x + KEY_W, y + KEY_H)


def draw_key(x, y):
    x1, y1, x2, y2 = key_shape(x, y)
    r = 0.11 * U
    # The hole in the plate around the rubber key.
    c.rrect(x1 - 3, y1 - 3, x2 + 3, y2 + 4, r + 3, (6, 6, 7, 255))
    # Rubber cap: rounded, light from above.
    c.vgrad_rrect(x1, y1, x2, y2, r, None, None,
                  stops=[(0.0, lerp(KEY_TOP, (255, 255, 255), 0.10)), (0.08, KEY_TOP), (0.30, KEY_MID),
                         (0.80, lerp(KEY_MID, KEY_BOT, 0.5)), (1.0, KEY_BOT)])
    key_rects.append((x1 - 2, y1 - 2, x2 + 2, y2 + 3))
    return x1, y1, x2, y2


def graphic(x, y, size, bits):
    """Block graphic: a small square with its quarters filled per bits (TR, TL, BR, BL)."""
    s = size
    c.draw.rectangle(c.box(x, y, x + s, y + s), outline=WHITE + (255,), width=c.s(1.2))
    h = s / 2
    quads = {1: (x + h, y), 2: (x, y), 4: (x + h, y + h), 8: (x, y + h)}
    for bit, (qx, qy) in quads.items():
        if bits & bit:
            c.draw.rectangle(c.box(qx, qy, qx + h, qy + h), fill=WHITE + (255,))


row_y = [TOP + 0.62 * U, TOP + 0.62 * U + 1.16 * U, TOP + 0.62 * U + 2.32 * U, TOP + 0.62 * U + 3.48 * U]
small = 0.155 * U

# Row 1: numbers.
for i, (code, main, cap, red, colour, below, gbits) in enumerate(NUM):
    x = PLATE_X + 0.42 * U + (ROW_X[0] + i) * U
    y = row_y[0]
    cx = x + KEY_W / 2
    if colour == "BLACK":
        c.rrect(cx - 0.30 * U, y - 0.53 * U, cx + 0.30 * U, y - 0.33 * U, 2, WHITE + (255,))
        c.text(cx, y - 0.43 * U, "BLACK", small, (12, 12, 12), "mm", max_w=0.56 * U)
    elif colour:
        c.text(cx, y - 0.43 * U, colour, small, COLOURS[colour], "mm", max_w=0.9 * U)
    c.text(cx, y - 0.18 * U, cap, small if len(cap) > 1 else 0.2 * U, WHITE, "mm", max_w=0.92 * U)
    x1, y1, x2, y2 = draw_key(x, y)
    c.text(x1 + 0.10 * U, y2 - 0.09 * U, main, 0.30 * U, WHITE, "ls")
    c.text(x2 - 0.13 * U, y1 + 0.17 * U, red, 0.21 * U, RED, "mm")
    if gbits is not None:
        graphic(x1 + 0.36 * U, y1 + 0.07 * U, 0.14 * U, gbits)
    c.text(cx, y2 + 0.17 * U, below, small, RED, "mm", max_w=0.95 * U)
    keys.append((code, 0, key_rects[-1], main))


def letter_row(r, letters, extra_after=None):
    for i, ch in enumerate(letters):
        code, word, red, green, below = LET[ch]
        x = PLATE_X + 0.42 * U + (ROW_X[r] + i) * U
        y = row_y[r]
        cx = x + KEY_W / 2
        c.text(cx, y - 0.17 * U, green, small, GREEN, "mm", max_w=0.95 * U)
        x1, y1, x2, y2 = draw_key(x, y)
        c.text(x1 + 0.09 * U, y1 + 0.20 * U, ch, 0.26 * U, WHITE, "lm")
        c.text(x2 - 0.08 * U, y1 + 0.15 * U, red, 0.14 * U if len(red) > 1 else 0.19 * U, RED, "rm",
               max_w=0.42 * U)
        c.text((x1 + x2) / 2, y2 - 0.10 * U, word, 0.13 * U, WHITE, "mm", max_w=0.70 * U)
        c.text(cx, y2 + 0.17 * U, below, small, RED, "mm", max_w=0.95 * U)
        keys.append((code, 0, key_rects[-1], ch))


letter_row(1, "QWERTYUIOP")
letter_row(2, "ASDFGHJKL")
# ENTER closes row 3.
x = PLATE_X + 0.42 * U + (ROW_X[2] + 9) * U
x1, y1, x2, y2 = draw_key(x, row_y[2])
c.text((x1 + x2) / 2, (y1 + y2) / 2, "ENTER", 0.17 * U, WHITE, "mm", max_w=0.7 * U)
keys.append((6 * 8 + 0, 0, key_rects[-1], "ENTER"))

# Row 4: CAPS SHIFT, Z..M, SYMBOL SHIFT, BREAK SPACE.
x = PLATE_X + 0.42 * U + ROW_X[3] * U
x1, y1, x2, y2 = draw_key(x, row_y[3])
c.text((x1 + x2) / 2, y1 + 0.20 * U, "CAPS", 0.15 * U, WHITE, "mm")
c.text((x1 + x2) / 2, y1 + 0.38 * U, "SHIFT", 0.15 * U, WHITE, "mm")
keys.append((0 * 8 + 0, 1, key_rects[-1], "CAPS SHIFT"))
for i, ch in enumerate("ZXCVBNM"):
    code, word, red, green, below = LET[ch]
    x = PLATE_X + 0.42 * U + (ROW_X[3] + 1 + i) * U
    y = row_y[3]
    cx = x + KEY_W / 2
    c.text(cx, y - 0.17 * U, green, small, GREEN, "mm", max_w=0.95 * U)
    x1, y1, x2, y2 = draw_key(x, y)
    c.text(x1 + 0.09 * U, y1 + 0.20 * U, ch, 0.26 * U, WHITE, "lm")
    c.text(x2 - 0.08 * U, y1 + 0.15 * U, red, 0.19 * U, RED, "rm")
    c.text((x1 + x2) / 2, y2 - 0.10 * U, word, 0.13 * U, WHITE, "mm", max_w=0.70 * U)
    c.text(cx, y2 + 0.17 * U, below, small, RED, "mm", max_w=0.95 * U)
    keys.append((code, 0, key_rects[-1], ch))
x = PLATE_X + 0.42 * U + (ROW_X[3] + 8) * U
x1, y1, x2, y2 = draw_key(x, row_y[3])
c.text((x1 + x2) / 2, y1 + 0.20 * U, "SYMBOL", 0.15 * U, RED, "mm", max_w=0.68 * U)
c.text((x1 + x2) / 2, y1 + 0.38 * U, "SHIFT", 0.15 * U, RED, "mm")
keys.append((7 * 8 + 1, 1, key_rects[-1], "SYMBOL SHIFT"))
x = PLATE_X + 0.42 * U + (ROW_X[3] + 9) * U
x1, y1, x2, y2 = draw_key(x, row_y[3])
c.text((x1 + x2) / 2, y1 + 0.18 * U, "BREAK", 0.13 * U, WHITE, "mm")
c.text((x1 + x2) / 2, y1 + 0.38 * U, "SPACE", 0.18 * U, WHITE, "mm", max_w=0.7 * U)
keys.append((7 * 8 + 0, 0, key_rects[-1], "BREAK SPACE"))

img = c.final()
if len(sys.argv) > 2:
    preview = img.copy()
    bg = __import__("PIL.Image", fromlist=["Image"]).new("RGBA", img.size, (0, 0, 200, 255))
    bg.alpha_composite(preview)
    bg.convert("RGB").save(sys.argv[2])
size = write_inc(sys.argv[1], img, keys, "ZxVkb",
                 "// Generated by tools/gen_zx48_vkb.py, do not edit.\n"
                 "// ZX Spectrum 48K keyboard; key codes are matrix row * 8 + bit.\n")
print(f"{len(keys)} keys, {size} bytes compressed")
