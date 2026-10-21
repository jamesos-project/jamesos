#!/usr/bin/env python3
"""JamesOS "Wake Up" boot animation.

Vector is asleep under the stars (flat breathing eyes, little z z floating up),
slowly wakes, blinks, looks around, smiles with a sparkle, then his eyes move up
while "JamesOS" types out underneath with a blinking cursor. Ends black so it loops.

Writes boot_184x96.raw (1.0 / PVT) and boot_160x80.raw (2.0):
little-endian RGB565 frames back to back, 24 fps, played by vic-bootAnim from
/persist/boot_anim.raw.
"""
import math, random, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf"
FPS = 24
N = 336            # 14 seconds: wake up once, then stay awake a while (fewer loops on a long boot)
SS = 4             # draw 4x bigger, then shrink = smooth edges

PURPLE = np.array((150, 80, 255), np.float32)
PURPLE_LIGHT = np.array((195, 140, 255), np.float32)
PINK = np.array((255, 120, 215), np.float32)


def seg(f, a, b): return min(1.0, max(0.0, (f - a) / float(b - a)))
def eio(t): t = min(1.0, max(0.0, t)); return t * t * (3 - 2 * t)
def eout(t): t = min(1.0, max(0.0, t)); return 1 - (1 - t) ** 3


def star_poly(cx, cy, r, rot):
    return [(cx + math.cos(rot + k * math.pi / 4 - math.pi / 2) * (r if k % 2 == 0 else r * 0.3),
             cy + math.sin(rot + k * math.pi / 4 - math.pi / 2) * (r if k % 2 == 0 else r * 0.3))
            for k in range(8)]


def render(W, H):
    S = min(W / 184.0, H / 96.0)          # 1.0 on the big screen, ~0.83 on the 2.0
    BW, BH = W * SS, H * SS
    cx, cy = BW / 2, BH / 2
    rnd = random.Random(7)
    font = ImageFont.truetype(FONT, int(17 * S * SS))
    zfont = [ImageFont.truetype(FONT, int(s * S * SS)) for s in (9, 12, 15)]
    word = "JamesOS"

    # stars: small dots, a few bigger 4-point twinklers; kept dim so the eyes pop
    stars = []
    for _ in range(int(34 * S * S)):
        stars.append(dict(x=rnd.uniform(0, BW), y=rnd.uniform(0, BH), b=rnd.uniform(0.25, 0.6),
                          ph=rnd.uniform(0, 6.28), sp=rnd.uniform(0.08, 0.2), big=False))
    for _ in range(5):
        stars.append(dict(x=rnd.uniform(0.05, 0.95) * BW, y=rnd.uniform(0.08, 0.92) * BH, b=0.75,
                          ph=rnd.uniform(0, 6.28), sp=rnd.uniform(0.1, 0.16), big=True))

    # text gradient: lilac on the left of the word -> pink on the right
    tb = ImageDraw.Draw(Image.new("L", (1, 1))).textbbox((0, 0), word, font=font)
    tl = (BW - (tb[2] - tb[0])) / 2
    xs = np.clip((np.arange(BW, dtype=np.float32) - tl) / (tb[2] - tb[0]), 0, 1)[None, :, None]
    text_grad = np.broadcast_to(PURPLE_LIGHT * (1 - xs) + PINK * xs, (BH, BW, 3))

    frames = []
    for f in range(N):
        im = Image.new("RGB", (BW, BH), (0, 0, 0))
        d = ImageDraw.Draw(im)
        master = eio(seg(f, 0, 10)) * (1 - seg(f, N - 12, N))   # fade in from / out to black

        # 1. stars (fade a little when the text comes, so it reads clearly)
        star_dim = 1 - 0.35 * eio(seg(f, 128, 140))
        for s in stars:
            tw = 0.55 + 0.45 * math.sin(f * s["sp"] + s["ph"])
            v = s["b"] * tw * star_dim * master
            c = (int(200 * v), int(185 * v), int(255 * v))
            if s["big"]:
                d.polygon(star_poly(s["x"], s["y"], (1.6 + 1.6 * tw) * S * SS, 0), fill=c)
            else:
                d.rectangle([s["x"], s["y"], s["x"] + SS * 0.9, s["y"] + SS * 0.9], fill=c)

        # 2. eyes
        # asleep 0-40 (flat lines breathing) -> half open 42-52 -> sleepy pause -> fully open 58-70
        half = eio(seg(f, 42, 52))
        full = eio(seg(f, 58, 70))
        open_p = 0.35 * half + 0.65 * full
        blink = 1.0
        for b0 in (76, 196, 262, 304):          # wake-up blink, then calm blinks while awake
            if b0 <= f <= b0 + 8:
                blink = 1 - 0.93 * math.sin(math.pi * seg(f, b0, b0 + 8))
        look = 0.0
        if 88 <= f < 112:
            look = math.sin(seg(f, 88, 112) * math.pi * 2) * 10 * S * SS
        if 220 <= f < 248:                      # a slow glance around while awake
            look = math.sin(seg(f, 220, 248) * math.pi * 2) * 6 * S * SS
        squint = max(eio(seg(f, 112, 120)) * (1 - eio(seg(f, 132, 138))),
                     eio(seg(f, 276, 284)) * (1 - eio(seg(f, 296, 302))))
        up = eio(seg(f, 126, 140))
        scale = 1 - 0.35 * up
        ew = 34 * S * SS * scale
        eh = max(2.5 * S * SS, (3 + 33 * open_p) * S * SS * blink * scale)
        ey = cy - up * 16 * S * SS
        gap = 25 * S * SS * scale

        if f < 42:
            br = 0.65 + 0.3 * math.sin(f * 2 * math.pi / 28)     # slow sleepy breathing
            ecol = PURPLE * br
        else:
            ecol = PURPLE + (PURPLE_LIGHT - PURPLE) * (0.3 + 0.3 * math.sin(f * 0.09))
        ecol = tuple(int(v * master) for v in ecol)
        for sx in (-1, 1):
            ex = cx + sx * gap + look
            r = max(1, min(ew, eh) * 0.28)
            d.rounded_rectangle([ex - ew / 2, ey - eh / 2, ex + ew / 2, ey + eh / 2], radius=r, fill=ecol)
            if squint > 0:   # happy eyes: cut the bottom with a curve
                d.ellipse([ex - ew * 0.75, ey + eh / 2 - eh * squint * 0.62,
                           ex + ew * 0.75, ey + eh / 2 + eh * 1.6], fill=(0, 0, 0))

        # 3. z z z floating up from the right eye while asleep
        for k, start in enumerate((0, 12, 24, 36)):
            life = seg(f, start, start + 26)
            if 0 < life < 1:
                zx = cx + gap + ew * 0.35 + life * 16 * S * SS
                zy = ey - 4 * S * SS - life * 28 * S * SS + math.sin(life * 6) * 2 * S * SS
                a = math.sin(math.pi * life) * master * (1 - seg(f, 40, 48))
                fnt = zfont[min(2, int(life * 3))]
                d.text((zx, zy), "z", font=fnt, fill=(int(215 * a), int(190 * a), int(255 * a)))

        # 4. sparkle next to the happy eyes
        for s0 in (114, 278):
          if s0 <= f < s0 + 22:
            t = eout(seg(f, s0, s0 + 8)) * (1 - seg(f, s0 + 16, s0 + 22))
            spx, spy = cx + gap + ew / 2 + 8 * S * SS, ey - eh / 2 - 2 * S * SS
            d.polygon(star_poly(spx, spy, 8 * S * SS * t, f * 0.12), fill=(int(255 * master),) * 3)
            d.polygon(star_poly(spx - 9 * S * SS, spy + 9 * S * SS, 3 * S * SS * t, -f * 0.12),
                      fill=(int(220 * master), int(200 * master), int(255 * master)))

        # 5. "JamesOS" types out with a blinking cursor
        if f >= 134:
            k = int(min(len(word), (f - 134) / 2.4 + 1))
            s = word[:k]
            bb = d.textbbox((0, 0), word, font=font)
            tw_ = bb[2] - bb[0]
            x0 = cx - tw_ / 2 - bb[0]
            y0 = cy + 20 * S * SS - bb[1] - (bb[3] - bb[1]) / 2
            m = Image.new("L", im.size, 0)
            ImageDraw.Draw(m).text((x0, y0), s, font=font, fill=255)
            a = np.array(m, np.float32)[..., None] / 255.0 * master
            arr = np.array(im, np.float32) * (1 - a) + text_grad * a
            im = Image.fromarray(arr.astype(np.uint8))
            d = ImageDraw.Draw(im)
            if k < len(word) or ((f // 6) % 2 == 0 and f < 200):   # cursor blinks a bit, then goes away
                cw = d.textbbox((x0, y0), s, font=font)[2]
                c = int(255 * master)
                d.rectangle([cw + 2 * S * SS, y0 + bb[3] - 3 * S * SS,
                             cw + 8 * S * SS, y0 + bb[3] - 1 * S * SS], fill=(c, c, c))

        frames.append(im.resize((W, H), Image.BOX))
    return frames


def to_raw(frames, path):
    with open(path, "wb") as fh:
        for fr in frames:
            arr = np.array(fr).astype(np.uint16)
            v = ((arr[..., 0] >> 3) << 11) | ((arr[..., 1] >> 2) << 5) | (arr[..., 2] >> 3)
            fh.write(v.astype("<u2").tobytes())


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    for (W, H) in ((184, 96), (160, 80)):
        fr = render(W, H)
        to_raw(fr, "%s/boot_%dx%d.raw" % (out, W, H))
        big = [x.resize((W * 3, H * 3), Image.NEAREST) for x in fr]
        big[0].save("%s/preview_wakeup_%dx%d.gif" % (out, W, H), save_all=True,
                    append_images=big[1:], duration=int(1000 / FPS), loop=0)
        idx = (20, 66, 116, 175, 200, 236, 290, 330)
        sc = 2
        sheet = Image.new("RGB", (4 * (W * sc + 6), 2 * (H * sc + 6)), (30, 30, 40))
        for n, i in enumerate(idx):
            sheet.paste(fr[i].resize((W * sc, H * sc), Image.NEAREST),
                        ((n % 4) * (W * sc + 6), (n // 4) * (H * sc + 6)))
        sheet.save("%s/sheet_wakeup_%dx%d.png" % (out, W, H))
        print("done", W, H, len(fr))
