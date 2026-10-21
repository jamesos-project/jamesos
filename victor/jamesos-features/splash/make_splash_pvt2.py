#!/usr/bin/env python3
"""PVT-only startup pictures, round 2 (no "PVT" text): anvil, gear, crest. Private extra."""
import math, random, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
W, H, SS = 184, 96, 8
BW, BH = W * SS, H * SS
FONT = "/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf"
yy, xx = np.mgrid[0:BH, 0:BW].astype(np.float32)
def P(x, y): return (x * SS, y * SS)
def mask_of(draw_fn):
    m = Image.new("L", (BW, BH), 0); draw_fn(ImageDraw.Draw(m)); return np.array(m, np.float32) / 255.0
def bevel(mask, blur=1.8):
    soft = np.array(Image.fromarray((mask * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(blur * SS)), np.float32) / 255.0
    gy, gx = np.gradient(soft)
    return np.clip(0.5 + (-gx - gy) * 9.0 * SS / 6, 0, 1), np.clip(1 - soft * 1.15, 0, 1) * mask
def steel_fill(mask, top, bot):
    shade, edge = bevel(mask)
    v = np.clip((yy - top) / max(1, bot - top), 0, 1)[..., None]
    c = np.array((236, 240, 247), np.float32) * (1 - v) + np.array((100, 110, 128), np.float32) * v
    return c * (0.72 + 0.56 * shade[..., None]) * (1 - 0.25 * edge[..., None])
def glow_floor(arr, cy, amt):
    g = np.exp(-(((xx - BW / 2) / (BW * 0.45)) ** 2 + ((yy - cy * SS) / (BH * 0.25)) ** 2)) * amt
    return arr + g[..., None] * np.array((1.0, 0.42, 0.08), np.float32)
def finish(arr, name, out):
    im = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)).resize((W, H), Image.BOX)
    a = np.array(im).astype(np.uint16)
    v = ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)
    open("%s/splash_%s.raw" % (out, name), "wb").write(v.astype("<u2").tobytes())
    return im

def anvil(out):
    """A: a steel anvil with a red-hot bar on it and sparks flying"""
    body = [P(52, 40), P(140, 40), P(140, 44), P(128, 50), P(108, 52), P(108, 66), P(120, 74), P(120, 80),
            P(70, 80), P(70, 74), P(82, 66), P(82, 52), P(64, 50), P(40, 46), P(30, 41), P(52, 40)]
    am = mask_of(lambda d: d.polygon(body, fill=255))
    arr = glow_floor(np.zeros((BH, BW, 3), np.float32), 46, 60)
    arr = arr * (1 - am[..., None]) + steel_fill(am, 40 * SS, 80 * SS) * am[..., None]
    bar = mask_of(lambda d: d.rounded_rectangle([P(72, 33), P(124, 39)], radius=2 * SS, fill=255))
    v = np.clip((yy - 33 * SS) / (6 * SS), 0, 1)[..., None]
    hot = np.array((255, 240, 180), np.float32) * (1 - v) + np.array((255, 110, 15), np.float32) * v
    halo = np.array(Image.fromarray((bar * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(4 * SS)), np.float32) / 255.0
    arr += (halo * 1.4)[..., None] * np.array((255, 120, 20), np.float32) * 0.6
    arr = arr * (1 - bar[..., None]) + hot * bar[..., None]
    im = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)); d = ImageDraw.Draw(im)
    rnd = random.Random(9)
    for _ in range(26):   # spark streaks flying up from the bar
        a = rnd.uniform(-2.6, -0.5); L = rnd.uniform(5, 16); x0, y0 = rnd.uniform(80, 116), 33
        x1, y1 = x0 + math.cos(a) * L, y0 + math.sin(a) * L; b = rnd.uniform(0.5, 1)
        d.line([P(x0 + math.cos(a) * L * 0.5, y0 + math.sin(a) * L * 0.5), P(x1, y1)], fill=(255, int(200 * b), int(90 * b)), width=int(0.8 * SS))
    return finish(np.array(im, np.float32), "pvt_anvil", out)

def gear(out):
    """B: a steel gear with a molten glowing core"""
    cx, cy, R, r_in, teeth = W / 2, H / 2, 34, 27, 12
    pts = []
    for k in range(teeth * 4):
        a = k * 2 * math.pi / (teeth * 4) - math.pi / 2
        rr = R if (k % 4) in (1, 2) else r_in
        pts.append(P(cx + math.cos(a) * rr, cy + math.sin(a) * rr))
    gm = mask_of(lambda d: (d.polygon(pts, fill=255), d.ellipse([P(cx - 14, cy - 14), P(cx + 14, cy + 14)], fill=0)))
    arr = np.zeros((BH, BW, 3), np.float32)
    dist = np.sqrt((xx - cx * SS) ** 2 + (yy - cy * SS) ** 2) / SS
    core = np.clip(1 - dist / 14, 0, 1)
    arr += (np.clip(1 - dist / 30, 0, 1) ** 2 * 120)[..., None] * np.array((1.0, 0.45, 0.08), np.float32)
    coremask = (dist < 14).astype(np.float32)
    corecol = np.array((255, 110, 15), np.float32) + (core ** 1.5)[..., None] * np.array((0, 140, 160), np.float32)
    arr = arr * (1 - coremask[..., None]) + corecol * coremask[..., None]
    arr = arr * (1 - gm[..., None]) + steel_fill(gm, (cy - R) * SS, (cy + R) * SS) * gm[..., None]
    # bolt holes
    holes = mask_of(lambda d: [d.ellipse([P(cx + math.cos(a) * 20.5 - 2, cy + math.sin(a) * 20.5 - 2), P(cx + math.cos(a) * 20.5 + 2, cy + math.sin(a) * 20.5 + 2)], fill=255) for a in [k * math.pi / 3 for k in range(6)]])
    arr = arr * (1 - holes[..., None]) + np.array((35, 38, 45), np.float32) * holes[..., None]
    return finish(arr, "pvt_gear", out)

def crest(out):
    """C: a steel shield crest with a gold J, two stars, warm forge light behind"""
    cx = W / 2
    shield = [P(cx - 26, 12), P(cx + 26, 12), P(cx + 26, 46), P(cx + 18, 64), P(cx, 84), P(cx - 18, 64), P(cx - 26, 46)]
    sm = mask_of(lambda d: d.polygon(shield, fill=255))
    arr = np.zeros((BH, BW, 3), np.float32)
    halo = np.array(Image.fromarray((sm * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(9 * SS)), np.float32) / 255.0
    arr += halo[..., None] * np.array((255, 120, 30), np.float32) * 0.45
    arr = arr * (1 - sm[..., None]) + steel_fill(sm, 12 * SS, 84 * SS) * sm[..., None]
    rim_in = mask_of(lambda d: d.polygon([P(cx - 22, 16), P(cx + 22, 16), P(cx + 22, 45), P(cx + 15, 61), P(cx, 78), P(cx - 15, 61), P(cx - 22, 45)], fill=255))
    rim = np.clip(sm - rim_in, 0, 1)
    gold = np.array((255, 200, 80), np.float32)
    arr = arr * (1 - rim[..., None]) + gold * rim[..., None] * (0.8 + 0.4 * bevel(rim, 0.8)[0][..., None])
    j = mask_of(lambda d: d.text(P(cx, 44), "J", font=ImageFont.truetype(FONT, 36 * SS), fill=255, anchor="mm"))
    jsh, _ = bevel(j, 1.2)
    v = np.clip((yy - 26 * SS) / (36 * SS), 0, 1)[..., None]
    jcol = (np.array((255, 225, 130), np.float32) * (1 - v) + np.array((205, 140, 40), np.float32) * v) * (0.8 + 0.4 * jsh[..., None])
    arr = arr * (1 - j[..., None]) + jcol * j[..., None]
    im = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)); d = ImageDraw.Draw(im)
    for sx in (cx - 44, cx + 44):   # two small stars beside the crest
        r1, r2 = 5 * SS, 1.6 * SS
        pts = [(sx * SS + math.cos(k * math.pi / 4 - math.pi / 2) * (r1 if k % 2 == 0 else r2), 46 * SS + math.sin(k * math.pi / 4 - math.pi / 2) * (r1 if k % 2 == 0 else r2)) for k in range(8)]
        d.polygon(pts, fill=(255, 210, 110))
    return finish(np.array(im, np.float32), "pvt_crest", out)

if __name__ == "__main__":
    out, prev = sys.argv[1], sys.argv[2]
    ims = [anvil(out), gear(out), crest(out)]
    font = ImageFont.truetype(FONT, 22)
    sheet = Image.new("RGB", (3 * (W * 3 + 12), H * 3 + 40), (22, 22, 30)); d = ImageDraw.Draw(sheet)
    for i, (im, t) in enumerate(zip(ims, ("A  Anvil", "B  Molten gear", "C  Gold crest"))):
        sheet.paste(im.resize((W * 3, H * 3), Image.LANCZOS), (i * (W * 3 + 12), 38)); d.text((i * (W * 3 + 12) + 6, 6), t, fill=(235, 235, 235), font=font)
    sheet.save(prev + "/pvt_splash_options2.png")
