#!/usr/bin/env python3
"""JamesOS PVT boot animation: "The Forge" (private extra - only for James's PVT).

Sparks and molten metal pour into a mold, the letters PVT glow white/orange-hot,
shimmer with heat, get quenched (spark burst), cool to beveled steel, a shine sweeps
across and PROTOTYPE is engraved underneath. 14 s, 24 fps, ends black so it loops.

Writes boot_184x96.raw and boot_160x80.raw (RGB565 little-endian frames, vic-bootAnim format).
"""
import math, random, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

FONT = "/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf"
FPS, N, SS = 24, 336, 6

def seg(f, a, b): return min(1.0, max(0.0, (f - a) / float(b - a)))
def eio(t): t = min(1.0, max(0.0, t)); return t * t * (3 - 2 * t)
def lerp(a, b, t): return a + (b - a) * t

def render(W, H):
    S = min(W / 184.0, H / 96.0)
    BW, BH = W * SS, H * SS
    rnd = random.Random(11)
    yy, xx = np.mgrid[0:BH, 0:BW].astype(np.float32)

    def tmask(txt, size, cy, spacing=0):
        m = Image.new("L", (BW, BH), 0); d = ImageDraw.Draw(m)
        font = ImageFont.truetype(FONT, int(size * S * SS))
        d.text((BW / 2, cy * S * SS), txt, font=font, fill=255, anchor="mm")
        return np.array(m, np.float32) / 255.0

    letters = tmask("PVT", 50, 42)
    inside = letters > 0.5
    ys, _ = np.nonzero(inside); top, bot = float(ys.min()), float(ys.max())
    sub = tmask("PROTOTYPE", 8.5, 84)

    # bevel: light from the top-left using the slope of a blurred copy of the letters
    soft = np.array(Image.fromarray((letters * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2.2 * S * SS)), np.float32) / 255.0
    gy, gx = np.gradient(soft)
    shade = np.clip(0.5 + (-gx - gy) * 9.0 * S * SS / 6, 0, 1)              # 0 dark edge .. 1 lit edge
    edge = np.clip(1 - soft * 1.15, 0, 1) * letters                          # near the outline
    vert = np.clip((yy - top) / (bot - top), 0, 1)

    steel = (np.array((236, 240, 247), np.float32) * (1 - vert)[..., None] +
             np.array((105, 115, 132), np.float32) * vert[..., None])
    steel = steel * (0.75 + 0.5 * shade[..., None]) * (1 - 0.25 * edge[..., None])
    sub_rgb = np.array((175, 185, 200), np.float32)

    sparks = []
    frames = []
    for f in range(N):
        master = eio(seg(f, 0, 10)) * (1 - seg(f, N - 14, N))
        fill_line = bot + 2 * SS - (bot - top + 6 * SS) * eio(seg(f, 26, 104))
        filled = inside & (yy >= fill_line)
        # heat 1 = white hot, 0 = cold steel
        heat = 1.0 if f < 150 else 1 - eio(seg(f, 150, 214))
        # molten colour: white-yellow at the top surface, orange below, flickering
        flick = 0.88 + 0.12 * np.sin(xx * 0.05 / S + f * 0.55) * np.sin(yy * 0.07 / S - f * 0.41)
        surf = np.clip(1 - (yy - fill_line) / (9 * SS), 0, 1)
        molten = (np.array((255, 135, 25), np.float32) * (1 - surf)[..., None] +
                  np.array((255, 235, 160), np.float32) * surf[..., None]) * flick[..., None]
        if heat >= 1:
            metal = molten
        elif heat > 0.55:      # orange -> deep red
            t = (1 - heat) / 0.45
            metal = molten * (1 - t) + np.array((185, 35, 15), np.float32) * t
        else:                  # deep red -> steel
            t = 1 - heat / 0.55
            metal = np.array((185, 35, 15), np.float32) * (1 - t) + steel * t
            metal = metal + np.array((120, 20, 0), np.float32) * (1 - t) * edge[..., None] * 0.6   # edges stay hot longest

        arr = np.zeros((BH, BW, 3), np.float32)
        # the empty mold: a dark outline, fading once it's full
        outline = (letters > 0.08) & (letters < 0.92)
        arr[outline] = np.array((70, 52, 44), np.float32) * (1 - seg(f, 96, 120))
        arr[filled] = metal[filled]

        # ember glow on the "floor" while hot
        glow_amt = eio(seg(f, 24, 50)) * (heat * 0.9 + 0.1 * (1 - seg(f, 150, 230)))
        if glow_amt > 0:
            g = np.exp(-(((xx - BW / 2) / (BW * 0.45)) ** 2 + ((yy - bot) / (BH * 0.3)) ** 2)) * 70 * glow_amt
            arr += g[..., None] * np.array((1.0, 0.42, 0.08), np.float32)

        # shine sweeps (once when it's done, once more before the end)
        for s0, s1 in ((214, 236), (292, 310)):
            if s0 <= f < s1:
                sx = -50 * SS + (BW + 100 * SS) * eio(seg(f, s0, s1))
                band = np.clip(1 - np.abs((xx - sx) + (yy - BH / 2) * 0.55) / (6 * S * SS), 0, 1) ** 1.5
                arr += (band * inside)[..., None] * 230

        # PROTOTYPE engraved underneath, letter by letter
        if f >= 222:
            reveal = np.clip((seg(f, 222, 252) * 1.25 - xx / BW * 1.0) / 0.12 + 1 - 1, 0, 1)
            reveal = np.clip((seg(f, 222, 252) * (BW * 1.3) - (xx - BW * 0.3)) / (8 * SS), 0, 1)
            arr += (sub * reveal)[..., None] * sub_rgb

        im = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))

        # heat shimmer: wobble rows above hot metal
        if 40 <= f < 200:
            amp = 1.6 * S * SS * heat * eio(seg(f, 40, 70))
            if amp > 0.3:
                a = np.array(im)
                for y in range(0, int(top + 4 * SS)):
                    sh = int(amp * math.sin(y * 0.035 / S + f * 0.6) * (1 - y / (top + 4 * SS)))
                    if sh: a[y] = np.roll(a[y], sh, axis=0)
                im = Image.fromarray(a)

        d = ImageDraw.Draw(im)
        # pouring stream from the top
        if 14 <= f < 106:
            w = 2.4 * S * SS * (1 - seg(f, 98, 106))
            wob = math.sin(f * 0.7) * 0.6 * SS
            d.rectangle([BW / 2 - w + wob, 0, BW / 2 + w + wob, fill_line], fill=(255, 205, 110))
            d.rectangle([BW / 2 - w * 0.35 + wob, 0, BW / 2 + w * 0.35 + wob, fill_line], fill=(255, 245, 210))
            for _ in range(3):
                sparks.append([BW / 2 + rnd.uniform(-4, 4) * SS, fill_line, rnd.uniform(-6, 6) * SS / 2,
                               rnd.uniform(-8, -3) * SS / 2, 1.0])
        # quench: big burst of sparks + a puff of steam
        if 150 <= f < 160:
            for _ in range(9):
                sparks.append([rnd.uniform(0.25, 0.75) * BW, rnd.uniform(top, bot), rnd.uniform(-7, 7) * SS / 2,
                               rnd.uniform(-9, -2) * SS / 2, 1.0])
        if 150 <= f < 204:
            # soft, see-through steam rising off the metal
            st = eio(seg(f, 150, 160)) * (1 - seg(f, 162, 204))
            steam = Image.new("L", (BW, BH), 0); sd = ImageDraw.Draw(steam)
            for k in range(9):
                px = BW * (0.22 + 0.56 * k / 8) + math.sin(f * 0.08 + k * 1.7) * 5 * SS
                py = top + 6 * SS - (f - 150) * 0.8 * SS - (k % 3) * 5 * SS
                r = (4 + (f - 150) * 0.22 + (k % 2) * 2) * S * SS
                sd.ellipse([px - r, py - r, px + r, py + r], fill=255)
            steam = steam.filter(ImageFilter.GaussianBlur(4 * S * SS))
            sa = np.array(steam, np.float32)[..., None] / 255.0 * 0.35 * st
            base = np.array(im, np.float32)
            im = Image.fromarray(np.clip(base * (1 - sa) + 200 * sa, 0, 255).astype(np.uint8))
            d = ImageDraw.Draw(im)
        alive = []
        for s in sparks:
            ox, oy = s[0], s[1]
            s[0] += s[2]; s[1] += s[3]; s[3] += 0.38 * SS; s[4] -= 0.03
            if s[4] > 0 and -10 < s[1] < BH:
                l = s[4]   # white-yellow when new -> orange -> dim red as it dies
                c = (int(255 * min(1, l * 1.6)), int(lerp(60, 225, l) * l), int(lerp(5, 140, l) * l * l))
                d.line([(ox, oy), (s[0], s[1])], fill=c, width=max(1, int(0.9 * S * SS)))   # trail
                alive.append(s)
        sparks[:] = alive

        arr = np.array(im, np.float32) * master
        frames.append(Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)).resize((W, H), Image.BOX))
    return frames

def to_raw(frames, path):
    with open(path, "wb") as fh:
        for fr in frames:
            a = np.array(fr).astype(np.uint16)
            v = ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)
            fh.write(v.astype("<u2").tobytes())

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    prev = sys.argv[2] if len(sys.argv) > 2 else out
    for (W, H) in ((184, 96), (160, 80)):
        fr = render(W, H)
        to_raw(fr, "%s/boot_%dx%d.raw" % (out, W, H))
        if W == 184:
            big = [x.resize((W * 3, H * 3), Image.LANCZOS) for x in fr[::2]]
            big[0].save("%s/preview_forge.gif" % prev, save_all=True, append_images=big[1:], duration=int(2000 / FPS), loop=0)
            idx = (40, 80, 120, 165, 185, 225, 250, 300)
            sheet = Image.new("RGB", (4 * (W * 2 + 6), 2 * (H * 2 + 6)), (30, 30, 40))
            for n, i in enumerate(idx):
                sheet.paste(fr[i].resize((W * 2, H * 2), Image.LANCZOS), ((n % 4) * (W * 2 + 6), (n // 4) * (H * 2 + 6)))
            sheet.save("%s/sheet_forge.png" % prev)
        print("done", W, H, len(fr))
