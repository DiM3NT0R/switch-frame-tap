#!/usr/bin/env python3
# v0.7.12: docs/hero-switch-to-pc.jpg, the picture at the top of the README - a
# drawn Switch and laptop showing the same frame (docs/shot-mk8-race-1080p.jpg).
#   python3 tools/gen_readme_hero.py   (writes hero.jpg here; copy it to docs/)
from PIL import Image, ImageDraw, ImageFilter
S = 2                                   # supersample
W, H = 1600 * S, 760 * S
def P(*v): return tuple(int(x * S) for x in v)

img = Image.new("RGB", (W, H), (12, 14, 18))
# background: a soft vertical gradient and a glow behind the devices
bg = Image.new("RGB", (1, H))
for y in range(H):
    t = y / H
    bg.putpixel((0, y), (int(12 + 10 * t), int(14 + 12 * t), int(18 + 18 * t)))
img = bg.resize((W, H))
glow = Image.new("L", (W, H), 0)
ImageDraw.Draw(glow).ellipse(P(300, 80, 1300, 760), fill=110)
glow = glow.filter(ImageFilter.GaussianBlur(160 * S))
img = Image.composite(Image.new("RGB", (W, H), (40, 110, 160)), img, glow.point(lambda v: int(v * 0.45)))
d = ImageDraw.Draw(img)
frame = Image.open("/home/tomas/switch-stream-project/docs/shot-mk8-race-1080p.jpg").convert("RGB")

def screen(box, radius):
    x0, y0, x1, y1 = box
    im = frame.resize((x1 - x0, y1 - y0), Image.LANCZOS)
    m = Image.new("L", im.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, im.size[0] - 1, im.size[1] - 1), radius, fill=255)
    img.paste(im, (x0, y0), m)

def shadow(box, radius, blur, alpha):
    sh = Image.new("L", (W, H), 0)
    ImageDraw.Draw(sh).rounded_rectangle(box, radius, fill=alpha)
    sh = sh.filter(ImageFilter.GaussianBlur(blur))
    img.paste(Image.new("RGB", (W, H), (0, 0, 0)), (0, 0), sh)

# ---- the laptop (right, behind) ----
lx0, ly0, lx1, ly1 = P(640, 70, 1460, 545)                     # lid
shadow(P(600, 560, 1500, 640), 40 * S, 30 * S, 160)
shadow((lx0, ly0 + 20 * S, lx1, ly1 + 20 * S), 26 * S, 24 * S, 140)
d = ImageDraw.Draw(img)
d.rounded_rectangle((lx0, ly0, lx1, ly1), 26 * S, fill=(28, 31, 38), outline=(70, 76, 88), width=2 * S)
screen((lx0 + 22 * S, ly0 + 22 * S, lx1 - 22 * S, ly1 - 22 * S), 8 * S)
d.ellipse((((lx0 + lx1) // 2) - 4 * S, ly0 + 8 * S, ((lx0 + lx1) // 2) + 4 * S, ly0 + 16 * S), fill=(60, 64, 74))   # camera
# the deck
d.polygon([P(600, 552), P(1500, 552), P(1530, 590), P(570, 590)], fill=(52, 57, 67))
d.rounded_rectangle(P(570, 586, 1530, 600), 6 * S, fill=(38, 42, 50))
d.rectangle(P(640, 545, 1460, 553), fill=(40, 44, 52))           # hinge
d.rounded_rectangle(P(985, 556, 1115, 566), 4 * S, fill=(70, 76, 88))   # the notch

# ---- the Switch (left, in front) ----
cx, cy = 430, 470                                              # its centre
bw, bh = 470, 270                                              # the tablet body
jw = 110                                                       # each Joy-Con
bx0, by0, bx1, by1 = cx - bw / 2, cy - bh / 2, cx + bw / 2, cy + bh / 2
shadow(P(bx0 - jw, by1 - 10, bx1 + jw, by1 + 40), 60 * S, 26 * S, 190)
d = ImageDraw.Draw(img)
# Joy-Con: rounded on the outside
d.rounded_rectangle(P(bx0 - jw, by0, bx0 + 40, by1), 64 * S, fill=(0, 171, 230))      # neon blue
d.rounded_rectangle(P(bx1 - 40, by0, bx1 + jw, by1), 64 * S, fill=(255, 76, 72))      # neon red
d.rectangle(P(bx0, by0, bx1, by1), fill=(24, 26, 30))                                  # the body
# controls: sticks and buttons
def stick(x, y):
    d.ellipse(P(x - 25, y - 25, x + 25, y + 25), fill=(30, 32, 36))
    d.ellipse(P(x - 17, y - 17, x + 17, y + 17), fill=(52, 55, 60))
def btns(x, y, r=11, gap=24):
    for dx, dy in ((0, -gap), (0, gap), (-gap, 0), (gap, 0)):
        d.ellipse(P(x + dx - r, y + dy - r, x + dx + r, y + dy + r), fill=(30, 32, 36))
stick(bx0 - jw / 2 + 18, cy - 62)
btns(bx0 - jw / 2 + 18, cy + 40)
btns(bx1 + jw / 2 - 18, cy - 62)
stick(bx1 + jw / 2 - 18, cy + 40)
d.rounded_rectangle(P(bx0 - jw / 2 + 4, by0 + 18, bx0 - jw / 2 + 30, by0 + 24), 3 * S, fill=(30, 32, 36))   # minus
d.rounded_rectangle(P(bx1 + jw / 2 - 30, by0 + 18, bx1 + jw / 2 - 4, by0 + 24), 3 * S, fill=(30, 32, 36))   # plus
screen(P(bx0 + 24, by0 + 18, bx1 - 24, by1 - 18), 6 * S)

# ---- the cable: from the Switch's bottom to the laptop's side ----
import math
pts = []
x0c, y0c = cx, by1 + 2
x3c, y3c = 1462, 520
for k in range(101):
    t = k / 100
    # a cubic curve dipping below both
    p0, p1, p2, p3 = (x0c, y0c), (x0c, y0c + 150), (x3c + 120, y3c + 160), (x3c, y3c)
    x = (1-t)**3*p0[0] + 3*(1-t)**2*t*p1[0] + 3*(1-t)*t**2*p2[0] + t**3*p3[0]
    y = (1-t)**3*p0[1] + 3*(1-t)**2*t*p1[1] + 3*(1-t)*t**2*p2[1] + t**3*p3[1]
    pts.append(P(x, y))
d.line(pts, fill=(200, 205, 214), width=7 * S, joint="curve")
d.rounded_rectangle(P(cx - 14, by1 - 4, cx + 14, by1 + 14), 4 * S, fill=(150, 156, 166))   # the plug
# a soft highlight travelling along it (the stream)
for k in range(40, 64, 3):
    x, y = pts[k]
    r = 7 * S
    d.ellipse((x - r, y - r, x + r, y + r), fill=(56, 189, 248))

out = img.resize((W // S, H // S), Image.LANCZOS)
out.save("hero.png", optimize=True)
out.convert("RGB").save("hero.jpg", quality=88, optimize=True)
print(out.size)
