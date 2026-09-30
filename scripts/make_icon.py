#!/usr/bin/env python3
"""Generates the app icon (original 'Bit' mascot on a dark neon tile). Usage: make_icon.py out.png"""
import sys
from PIL import Image, ImageDraw, ImageFilter
rows = [
"......yy........",".......k........","....hhhhhhhh....","...hkkkkkkkkh...","..hhkbbbbbbkhh..",".hhhkbbbbbbkhhh.",
".hhhkbkbbkbkhhh.",".hhhkbkbbkbkhhh.",".hhhkbbbbbbkhhh.",".hhhkbpbbpbkhhh.","..hhkbbkkbbkhh..","...hkbbbbbbkh...",
"....kkkkkkkk....","....kbdbbdbk....",".....kk..kk....."]
pal = {"y":(255,210,63),"k":(24,36,30),"h":(255,63,180),"b":(120,230,200),"p":(255,140,170),"d":(70,170,150)}
S = 1024
img = Image.new("RGB", (S, S), (11, 13, 16))
d = ImageDraw.Draw(img)
for y in range(S):  # subtle vertical gradient
    c = int(11 + 14 * y / S); d.line([(0, y), (S, y)], fill=(c, c + 2, c + 6))
glow = Image.new("RGB", (S, S), (0, 0, 0)); g = ImageDraw.Draw(glow)
for i, col in enumerate([(166, 255, 46), (33, 230, 242), (255, 63, 180), (255, 138, 31)]):
    x0 = 120 + i * 200; g.rectangle([x0, 800, x0 + 150, 880], fill=col)
glow = glow.filter(ImageFilter.GaussianBlur(28)); img = Image.blend(img, Image.eval(Image.composite(glow, img, Image.new("L", (S, S), 255)), lambda v: v), 0.0)
from PIL import ImageChops
img = ImageChops.add(img, glow)
d = ImageDraw.Draw(img)
for i, col in enumerate([(166, 255, 46), (33, 230, 242), (255, 63, 180), (255, 138, 31)]):
    x0 = 120 + i * 200; d.rounded_rectangle([x0, 810, x0 + 150, 870], 14, fill=col)
u = 48; ox = (S - 16 * u) // 2; oy = 70
for y, r in enumerate(rows):
    for x, ch in enumerate(r):
        if ch in pal:
            d.rectangle([ox + x * u, oy + y * u, ox + (x + 1) * u - 1, oy + (y + 1) * u - 1], fill=pal[ch])
img.save(sys.argv[1])
