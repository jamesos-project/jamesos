#!/usr/bin/env python3
"""PVT startup pictures, round 3: gold sparkle, JamesOS gold, Angkor sunrise, golden star sky."""
import math, random, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
W, H, SS = 184, 96, 8
BW, BH = W * SS, H * SS
FONT = "/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf"
yy, xx = np.mgrid[0:BH, 0:BW].astype(np.float32)
def P(x, y): return (x * SS, y * SS)
def star(cx, cy, rl, rs, pts=4):
    return [P(cx + math.cos(k * math.pi / pts - math.pi / 2) * (rl if k % 2 == 0 else rs),
              cy + math.sin(k * math.pi / pts - math.pi / 2) * (rl if k % 2 == 0 else rs)) for k in range(pts * 2)]
def mask_of(fn):
    m = Image.new("L", (BW, BH), 0); fn(ImageDraw.Draw(m)); return np.array(m, np.float32) / 255.0
def blur(m, r): return np.array(Image.fromarray((np.clip(m, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r * SS)), np.float32) / 255.0
def vgrad(top, bot, y0, y1):
    v = np.clip((yy - y0 * SS) / ((y1 - y0) * SS), 0, 1)[..., None]
    return np.array(top, np.float32) * (1 - v) + np.array(bot, np.float32) * v
def finish(arr, name, out):
    im = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)).resize((W, H), Image.BOX)
    a = np.array(im).astype(np.uint16)
    v = ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)
    open("%s/splash_%s.raw" % (out, name), "wb").write(v.astype("<u2").tobytes()); return im

def gold_sparkle(out):
    m = mask_of(lambda d: (d.polygon(star(92, 46, 26, 5.5), fill=255), d.polygon(star(122, 26, 6, 1.6), fill=255),
                           d.polygon(star(64, 60, 4.5, 1.3), fill=255), d.polygon(star(118, 66, 3.5, 1.1), fill=255)))
    arr = blur(m, 5)[..., None] * np.array((255, 170, 50), np.float32) * 0.7
    col = vgrad((255, 240, 190), (215, 150, 45), 20, 72)
    return finish(arr * (1 - m[..., None]) + col * m[..., None], "pvt_goldsparkle", out)

def gold_name(out):
    m = mask_of(lambda d: d.text(P(92, 46), "JamesOS", font=ImageFont.truetype(FONT, 30 * SS), fill=255, anchor="mm"))
    soft = blur(m, 1.2); gy, gx = np.gradient(soft); sh = np.clip(0.5 + (-gx - gy) * 9.0 * SS / 6, 0, 1)
    col = vgrad((255, 235, 160), (190, 125, 35), 34, 58) * (0.8 + 0.4 * sh[..., None])
    arr = blur(m, 6)[..., None] * np.array((255, 160, 40), np.float32) * 0.35
    line = mask_of(lambda d: (d.line([P(46, 66), P(138, 66)], fill=255, width=int(0.8 * SS)),))
    arr = arr * (1 - line[..., None]) + line[..., None] * np.array((200, 150, 60), np.float32)
    return finish(arr * (1 - m[..., None]) + col * m[..., None], "pvt_goldname", out)

def angkor(out):
    arr = vgrad((25, 20, 60), (255, 140, 60), 0, 70)
    arr = arr * 0 + vgrad((30, 25, 70), (250, 150, 70), 0, 72)
    sun = np.exp(-(((xx - 92 * SS) ** 2 + (yy - 66 * SS) ** 2) / (14 * SS) ** 2))
    arr += sun[..., None] * np.array((255, 220, 140), np.float32) * 0.9
    def towers(d):
        base = 76
        for cx, h, w in ((92, 40, 9), (70, 30, 7), (114, 30, 7), (52, 22, 6), (132, 22, 6)):
            top = base - h
            pts = [P(cx - w, base), P(cx - w, top + h * 0.45)]
            for k in range(5):   # stepped, lotus-bud tower
                y = top + h * 0.45 - k * h * 0.09; ww = w * (1 - k * 0.17)
                pts += [P(cx - ww, y), P(cx - ww * 0.82, y - h * 0.06)]
            pts += [P(cx, top)]
            for k in reversed(range(5)):
                y = top + h * 0.45 - k * h * 0.09; ww = w * (1 - k * 0.17)
                pts += [P(cx + ww * 0.82, y - h * 0.06), P(cx + ww, y)]
            pts += [P(cx + w, top + h * 0.45), P(cx + w, base)]
            d.polygon(pts, fill=255)
        d.rectangle([P(24, 66), P(160, 96)], fill=255)   # galleries / ground
        d.rectangle([P(40, 62), P(144, 66)], fill=255)
    m = mask_of(towers)
    arr = arr * (1 - m[..., None]) + m[..., None] * np.array((18, 12, 28), np.float32)
    wat = (yy > 84 * SS).astype(np.float32)   # moat reflection
    refl = np.flipud(arr)[: BH] * 0.35
    arr = arr * (1 - wat[..., None]) + (refl * wat[..., None] + np.array((20, 15, 30), np.float32) * wat[..., None])
    return finish(arr, "pvt_angkor", out)

def star_sky(out):
    arr = vgrad((4, 6, 22), (14, 10, 34), 0, 96)
    rnd = random.Random(21); im = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)); d = ImageDraw.Draw(im)
    for _ in range(90):
        x, y, b = rnd.uniform(0, W), rnd.uniform(0, H), rnd.uniform(0.25, 0.8); r = rnd.uniform(0.35, 0.8) * SS
        d.ellipse([x * SS - r, y * SS - r, x * SS + r, y * SS + r], fill=(int(200 * b), int(205 * b), int(255 * b)))
    arr = np.array(im, np.float32)
    m = mask_of(lambda d: d.polygon(star(92, 46, 20, 3.5), fill=255))
    arr += blur(m, 7)[..., None] * np.array((255, 190, 80), np.float32) * 0.8
    arr = arr * (1 - m[..., None]) + vgrad((255, 245, 200), (235, 170, 60), 26, 66) * m[..., None]
    return finish(arr, "pvt_starsky", out)

if __name__ == "__main__":
    out, prev = sys.argv[1], sys.argv[2]
    ims = [gold_sparkle(out), gold_name(out), angkor(out), star_sky(out)]
    font = ImageFont.truetype(FONT, 22)
    cw = W * 3 + 12
    sheet = Image.new("RGB", (2 * cw, 2 * (H * 3 + 40)), (22, 22, 30)); d = ImageDraw.Draw(sheet)
    for i, (im, t) in enumerate(zip(ims, ("1  Gold sparkle", "2  JamesOS in gold", "3  Angkor sunrise", "4  Golden star sky"))):
        x, y = (i % 2) * cw, (i // 2) * (H * 3 + 40)
        sheet.paste(im.resize((W * 3, H * 3), Image.LANCZOS), (x, y + 38)); d.text((x + 6, y + 6), t, fill=(235, 235, 235), font=font)
    sheet.save(prev + "/pvt_splash_options3.png")
