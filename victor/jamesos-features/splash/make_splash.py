#!/usr/bin/env python3
"""JamesOS startup splash (replaces the "WireOS" picture shown by rampost before the boot animation).

Writes splash_<name>.raw: 184x96, little-endian RGB565 (exactly 35328 bytes, same as WireOS's picture).
On a Vector 2.0 rampost shrinks the whole picture to 160x80.
"""
import math

import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H, SS = 184, 96, 8
FONT = "/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf"
LAV = (155, 115, 255)
LAV_TOP = (215, 195, 255)


def star_poly(cx, cy, r_long, r_short, rot=0.0, points=4):
    pts = []
    for k in range(points * 2):
        a = rot + k * math.pi / points - math.pi / 2
        r = r_long if k % 2 == 0 else r_short
        pts.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
    return pts


def vertical_gradient(mask, top, bottom):
    h = mask.shape[0]
    t = np.linspace(0, 1, h, dtype=np.float32)[:, None, None]
    g = np.array(top, np.float32) * (1 - t) + np.array(bottom, np.float32) * t
    return g * mask[..., None]


def finish(layer):
    img = Image.fromarray(np.clip(layer, 0, 255).astype(np.uint8)).resize((W, H), Image.BOX)
    return img


def sparkle():
    """A: the JamesOS sparkle emblem with three little stars"""
    m = Image.new("L", (W * SS, H * SS), 0)
    d = ImageDraw.Draw(m)
    cx, cy = W / 2 * SS, H / 2 * SS
    d.polygon(star_poly(cx, cy, 38 * SS, 7.5 * SS), fill=255)
    for (x, y, r) in ((W / 2 + 38, H / 2 - 24, 7), (W / 2 - 40, H / 2 + 20, 5.5), (W / 2 + 30, H / 2 + 28, 4)):
        d.polygon(star_poly(x * SS, y * SS, r * SS, r * 0.22 * SS), fill=255)
    mask = np.array(m).astype(np.float32) / 255.0
    layer = vertical_gradient(mask, LAV_TOP, LAV)
    # soft glow behind the big star
    yy, xx = np.mgrid[0:H * SS, 0:W * SS].astype(np.float32)
    rr = np.sqrt((xx - cx) ** 2 + ((yy - cy) * 1.3) ** 2) / (30 * SS)
    glow = np.clip(1 - rr, 0, 1) ** 2 * 0.35
    layer = np.maximum(layer, np.array(LAV, np.float32) * glow[..., None])
    return finish(layer)


def sleeping_eyes():
    """B: Vector's eyes closed, asleep, with a small z z"""
    m = Image.new("L", (W * SS, H * SS), 0)
    d = ImageDraw.Draw(m)
    for ex in (W / 2 - 38, W / 2 + 38):
        box = [(ex - 22) * SS, (H / 2 - 16) * SS, (ex + 22) * SS, (H / 2 + 14) * SS]
        d.arc(box, start=20, end=160, fill=255, width=int(5 * SS))
    font = ImageFont.truetype(FONT, int(13 * SS))
    font2 = ImageFont.truetype(FONT, int(9 * SS))
    d.text(((W / 2 + 56) * SS, 8 * SS), "z", font=font, fill=255)
    d.text(((W / 2 + 70) * SS, 2 * SS), "z", font=font2, fill=255)
    mask = np.array(m).astype(np.float32) / 255.0
    return finish(vertical_gradient(mask, LAV_TOP, LAV))


def j_badge():
    """C: a rounded badge with a big J and a sparkle"""
    m = Image.new("L", (W * SS, H * SS), 0)
    d = ImageDraw.Draw(m)
    bw, bh = 62, 62
    x0, y0 = (W - bw) / 2, (H - bh) / 2
    d.rounded_rectangle([x0 * SS, y0 * SS, (x0 + bw) * SS, (y0 + bh) * SS], radius=16 * SS,
                        outline=255, width=int(4 * SS))
    font = ImageFont.truetype(FONT, int(44 * SS))
    bb = d.textbbox((0, 0), "J", font=font)
    d.text(((W * SS - (bb[2] - bb[0])) / 2 - bb[0], (H * SS - (bb[3] - bb[1])) / 2 - bb[1] + 2 * SS),
           "J", font=font, fill=255)
    d.polygon(star_poly((x0 + bw + 8) * SS, (y0 + 2) * SS, 9 * SS, 2 * SS), fill=255)
    mask = np.array(m).astype(np.float32) / 255.0
    return finish(vertical_gradient(mask, LAV_TOP, LAV))


def to_raw(img):
    a = np.array(img).astype(np.uint16)
    v = ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)
    return v.astype("<u2").tobytes()


if __name__ == "__main__":
    tiles = []
    for name, fn in (("sparkle", sparkle), ("sleeping", sleeping_eyes), ("badge", j_badge)):
        img = fn()
        raw = to_raw(img)
        assert len(raw) == 35328
        open("splash_%s.raw" % name, "wb").write(raw)
        img.save("splash_%s.png" % name)
        tiles.append((name, img))
    print("ok")
