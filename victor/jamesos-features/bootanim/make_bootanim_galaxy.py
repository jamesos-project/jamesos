#!/usr/bin/env python3
"""
JamesOS "Galaxy Ignite" boot animation (24 fps, 270 frames = 11.25 s, loops).

  0-20    a single spark pulses in the dark (heartbeat)
  20-44   it bursts: shockwave ring + particles fly out
  44-96   the particles swirl into a spinning two-arm galaxy with a glowing core
  96-122  the galaxy pours into two points -> Vector's eyes grow out of them
  122-160 eyes look left, right, blink
  160-182 eyes move up and shrink
  176-208 "JamesOS" is built out of flying particles, left to right
  204-214 the particles melt into solid gradient letters; a shine sweeps across
  250-270 fade to black (clean loop)

Writes boot_184x96.raw (Vector 1.0 / PVT) and boot_160x80.raw (Vector 2.0):
little-endian RGB565, frames back to back.
"""
import math
import random

import numpy as np
from PIL import Image, ImageDraw, ImageFont

FPS = 24
N = 270
SS = 4
FONT = "/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf"

LAV = (155, 115, 255)
LAV_TOP = (200, 175, 255)
CYAN = (110, 215, 255)
PINK = (255, 125, 215)
WHITE = (255, 255, 255)


def ease_out(t):
    t = min(1.0, max(0.0, t))
    return 1 - (1 - t) ** 3


def ease_in_out(t):
    t = min(1.0, max(0.0, t))
    return t * t * (3 - 2 * t)


def seg(f, a, b):
    return min(1.0, max(0.0, (f - a) / float(b - a)))


def mix(c1, c2, t):
    return tuple(c1[i] + (c2[i] - c1[i]) * t for i in range(3))


def text_gradient(t):
    """lavender -> cyan -> pink across the word"""
    if t < 0.5:
        return mix(LAV_TOP, CYAN, t * 2)
    return mix(CYAN, PINK, (t - 0.5) * 2)


def render(W, H, out_raw, preview):
    rnd = random.Random(7)
    S = H / 96.0
    cx, cy = W / 2.0, H / 2.0

    # ---- galaxy particles ----
    parts = []
    for i in range(170):
        arm = i % 2
        rn = rnd.random() ** 0.8                        # 0 = core, 1 = rim
        parts.append(dict(
            arm=arm, rn=rn,
            jitter=rnd.uniform(-0.35, 0.35),
            burst_a=rnd.uniform(0, 2 * math.pi), burst_r=rnd.uniform(0.15, 0.62) * W,
            col=rnd.choice([LAV, LAV, CYAN, PINK, WHITE]),
            size=rnd.uniform(0.6, 1.25),
            eye_dx=rnd.uniform(-1, 1), eye_dy=rnd.uniform(-1, 1)))

    bg = [dict(x=rnd.uniform(0, W), y=rnd.uniform(0, H), ph=rnd.uniform(0, 6.3),
               sp=rnd.uniform(0.08, 0.22), b=rnd.uniform(0.2, 0.65)) for _ in range(int(40 * S))]

    # ---- text: final mask + particle targets ----
    font = ImageFont.truetype(FONT, int(30 * S) * SS)
    small = ImageFont.truetype(FONT, int(9 * S) * SS)
    tmask = Image.new("L", (W * SS, H * SS), 0)
    td = ImageDraw.Draw(tmask)
    bb = td.textbbox((0, 0), "JamesOS", font=font)
    tw = bb[2] - bb[0]
    text_x = (W * SS - tw) / 2 - bb[0]
    text_y = H * SS * 0.60 - bb[1]
    td.text((text_x, text_y), "JamesOS", font=font, fill=255)
    tm = np.array(tmask).astype(np.float32) / 255.0
    t_left, t_right = (W * SS - tw) / 2, (W * SS - tw) / 2 + tw
    ys_t, xs_t = np.nonzero(tm > 0.6)
    pick = rnd.sample(range(len(xs_t)), min(320, len(xs_t)))
    targets = []
    for k in pick:
        tx, ty = xs_t[k] / SS, ys_t[k] / SS
        u = (xs_t[k] - t_left) / max(1.0, tw)
        start_side = rnd.choice(["top", "bottom", "left", "right"])
        if start_side == "top":
            sx, sy = rnd.uniform(0, W), -4 * S
        elif start_side == "bottom":
            sx, sy = rnd.uniform(0, W), H + 4 * S
        elif start_side == "left":
            sx, sy = -4 * S, rnd.uniform(0, H)
        else:
            sx, sy = W + 4 * S, rnd.uniform(0, H)
        targets.append(dict(tx=tx, ty=ty, sx=sx, sy=sy, u=u, delay=u * 14 + rnd.uniform(0, 4)))

    xs = np.arange(W * SS, dtype=np.float32)[None, :]
    ys = np.arange(H * SS, dtype=np.float32)[:, None]
    grad_u = np.clip((xs - t_left) / max(1.0, tw), 0, 1)
    text_col = np.zeros((H * SS, W * SS, 3), np.float32)
    for c in range(3):
        lut = np.array([text_gradient(v)[c] for v in np.linspace(0, 1, 256)], np.float32)
        text_col[..., c] = lut[(grad_u * 255).astype(int)].repeat(H * SS, axis=0)

    raw = open(out_raw, "wb")
    for f in range(N):
        img = Image.new("RGB", (W * SS, H * SS), (0, 0, 0))
        d = ImageDraw.Draw(img)
        master = 1 - ease_in_out(seg(f, 250, 268))

        def dot(x, y, r, col, a=1.0):
            a *= master
            if a <= 0.01:
                return
            c = tuple(int(v * a) for v in col)
            R = r * SS * S
            d.ellipse([x * SS - R, y * SS - R, x * SS + R, y * SS + R], fill=c)

        # background stars (after the burst)
        if f >= 30:
            bga = ease_out(seg(f, 30, 60))
            for s in bg:
                a = s["b"] * (0.55 + 0.45 * math.sin(s["ph"] + f * s["sp"])) * bga
                dot(s["x"], s["y"], 0.45, (200, 200, 255), a)

        # 1. heartbeat spark
        if f < 24:
            pulse = max(0.0, math.sin(f / 10.0 * math.pi)) if f < 20 else 1.0
            r = 1.2 + 2.2 * pulse
            dot(cx, cy, r * 2.4, LAV, 0.25 + 0.3 * pulse)
            dot(cx, cy, r, WHITE, 0.6 + 0.4 * pulse)

        # 2. burst ring
        if 20 <= f < 40:
            p = seg(f, 20, 40)
            R = ease_out(p) * 0.75 * W
            ring = tuple(int(v * (1 - p) * master) for v in (210, 185, 255))
            d.ellipse([(cx - R) * SS, (cy - R * 0.55) * SS, (cx + R) * SS, (cy + R * 0.55) * SS],
                      outline=ring, width=max(SS, int(2 * S * SS)))
            if p < 0.4:
                dot(cx, cy, (1 - p / 0.4) * 7, WHITE)

        # 3-4. particles: burst -> galaxy -> into the eyes
        eye_gap = 0.20 * W
        eye_centers = [(cx - eye_gap, cy), (cx + eye_gap, cy)]
        if 20 <= f < 124:
            burst = ease_out(seg(f, 20, 40))
            to_gal = ease_in_out(seg(f, 40, 64))
            to_eye = ease_in_out(seg(f, 96, 120))
            spin = 0.0
            for k in range(f):
                spin += 0.035 + 0.09 * seg(k, 70, 118)        # spins up before pouring into the eyes
            # glowing core
            core_a = ease_out(seg(f, 44, 70)) * (1 - to_eye)
            if core_a > 0:
                for rr, cc, aa in ((14, LAV, 0.18), (8, (190, 160, 255), 0.35), (3.2, WHITE, 0.9)):
                    dot(cx, cy, rr * (0.9 + 0.1 * math.sin(f * 0.3)), cc, aa * core_a)
            for i, p in enumerate(parts):
                bx = cx + math.cos(p["burst_a"]) * p["burst_r"] * burst
                by = cy + math.sin(p["burst_a"]) * p["burst_r"] * burst * 0.6
                r = (0.06 + 0.42 * p["rn"]) * W
                th = p["arm"] * math.pi + p["rn"] * 4.2 + p["jitter"] + spin
                gx = cx + math.cos(th) * r
                gy = cy + math.sin(th) * r * 0.45
                x = bx + (gx - bx) * to_gal
                y = by + (gy - by) * to_gal
                ex, ey = eye_centers[i % 2]
                ex += p["eye_dx"] * 0.05 * W
                ey += p["eye_dy"] * 0.12 * H
                x += (ex - x) * to_eye
                y += (ey - y) * to_eye
                a = 1.0 if f < 116 else 1 - seg(f, 116, 124)
                dot(x, y, p["size"] * 2.2, p["col"], 0.22 * a)
                dot(x, y, p["size"] * 0.9, p["col"], a)

        frame = np.array(img).astype(np.float32)

        # 5. eyes: grow from the two points, look around, blink, move up
        if f >= 112:
            open_p = ease_out(seg(f, 112, 132))
            look = 0.0
            if 132 <= f < 160:
                q = seg(f, 132, 160)
                look = math.sin(q * 2 * math.pi) * 0.06 * W          # left, then right, back
            blink = math.sin(seg(f, 160, 168) * math.pi) if 160 <= f < 168 else 0.0
            move = ease_in_out(seg(f, 166, 188))
            ew = 0.27 * W * (1 - 0.30 * move) * (0.25 + 0.75 * open_p)
            eh = max(2 * S, 0.58 * H * (1 - 0.45 * move) * open_p * (1 - 0.92 * blink))
            ecy = cy - move * 0.24 * H
            gap = eye_gap * (1 - 0.18 * move)
            emask = Image.new("L", (W * SS, H * SS), 0)
            em = ImageDraw.Draw(emask)
            boxes = []
            for ecx in (cx - gap + look, cx + gap + look):
                x0, x1 = (ecx - ew / 2) * SS, (ecx + ew / 2) * SS
                y0, y1 = (ecy - eh / 2) * SS, (ecy + eh / 2) * SS
                em.rounded_rectangle([x0, y0, x1, y1], radius=min(ew * 0.24, eh / 2) * SS, fill=255)
                boxes.append((y0, y1))
            m = np.array(emask).astype(np.float32) / 255.0
            y0 = min(b[0] for b in boxes)
            y1 = max(b[1] for b in boxes)
            t = np.clip((np.arange(H * SS, dtype=np.float32) - y0) / max(1.0, y1 - y0), 0, 1)[:, None, None]
            g = np.array(LAV_TOP, np.float32) * (1 - t) + np.array(LAV, np.float32) * t
            g = np.broadcast_to(g, frame.shape) * master
            frame = frame * (1 - m[..., None]) + g * m[..., None]

        # 6. "JamesOS" built from particles, then solid with a shine
        if 176 <= f < 214:
            pimg = Image.new("RGB", (W * SS, H * SS), (0, 0, 0))
            pd = ImageDraw.Draw(pimg)
            fade_dots = 1 - seg(f, 206, 214)
            for tgt in targets:
                q = ease_in_out(seg(f, 176 + tgt["delay"], 196 + tgt["delay"]))
                x = tgt["sx"] + (tgt["tx"] - tgt["sx"]) * q
                y = tgt["sy"] + (tgt["ty"] - tgt["sy"]) * q
                col = text_gradient(tgt["u"])
                a = (0.35 + 0.65 * q) * fade_dots * master
                R = 0.8 * SS * S
                pd.ellipse([x * SS - R, y * SS - R, x * SS + R, y * SS + R],
                           fill=tuple(int(v * a) for v in col))
            frame = np.maximum(frame, np.array(pimg).astype(np.float32))
        if f >= 204:
            solid = ease_out(seg(f, 204, 214)) * master
            col = text_col.copy()
            for start in (220, 236):                     # two shine sweeps
                if start <= f < start + 14:
                    p = seg(f, start, start + 14)
                    bx = t_left - 20 * SS + (tw + 40 * SS) * p
                    diag = xs + (ys - text_y) * 0.4
                    band = np.clip(1 - np.abs(diag - bx) / (9 * SS), 0, 1)[..., None]
                    col = col * (1 - band) + 255 * band
            mm = tm * solid
            frame = frame * (1 - mm[..., None]) + col * mm[..., None]
            if f >= 212:
                va = ease_out(seg(f, 212, 226)) * master
                vimg = Image.new("L", (W * SS, H * SS), 0)
                vd = ImageDraw.Draw(vimg)
                vb = vd.textbbox((0, 0), "v1.0", font=small)
                vd.text(((W * SS - (vb[2] - vb[0])) / 2 - vb[0], text_y + bb[3] + 2 * S * SS - vb[1]),
                        "v1.0", font=small, fill=255)
                vm = np.array(vimg).astype(np.float32) / 255.0 * va
                frame = frame * (1 - vm[..., None]) + np.array([170, 145, 255], np.float32) * vm[..., None]

        out = Image.fromarray(np.clip(frame, 0, 255).astype(np.uint8)).resize((W, H), Image.BOX)
        arr = np.array(out).astype(np.uint16)
        rgb565 = ((arr[..., 0] >> 3) << 11) | ((arr[..., 1] >> 2) << 5) | (arr[..., 2] >> 3)
        raw.write(rgb565.astype("<u2").tobytes())
        preview.append(out)
    raw.close()


if __name__ == "__main__":
    for (W, H) in ((184, 96), (160, 80)):
        frames = []
        render(W, H, "boot_%dx%d.raw" % (W, H), frames)
        big = [fr.resize((W * 3, H * 3), Image.NEAREST) for fr in frames]
        big[0].save("preview_galaxy_%dx%d.gif" % (W, H), save_all=True, append_images=big[1:],
                    duration=int(1000 / FPS), loop=0)
        for k in (12, 30, 60, 85, 110, 128, 150, 186, 196, 230):
            frames[k].resize((W * 3, H * 3), Image.NEAREST).save("gx_%dx%d_%03d.png" % (W, H, k))
        print("done", W, H, len(frames))
