#!/usr/bin/env python3
"""Erzeugt ein synthetisches Wasserzaehler-Bild als PGM fuer den Host-Test.

Der Zaehler wird mechanisch korrekt gerendert: eine Rolle steht genau so weit
zwischen zwei Ziffern, wie es die naechstniedrigere Stelle vorgibt. Genau diese
Zwangsbedingung wertet die Firmware in ihrer Kaskade aus - das Bild ist also
ein echter Test der Erkennung und nicht nur eine huebsche Attrappe.

    python3 make_test_image.py --reading 01234.567 --out /tmp/meter.pgm
"""

import argparse
import math
import os
import random

from PIL import Image, ImageDraw, ImageFont

W, H = 640, 480

# Muss mit tools/hostcheck/test_recognize.cpp uebereinstimmen.
DIGITS = [(55 + i * 105, 120, 72, 100) for i in range(5)]
DIALS = [(90 + i * 150, 340, 110, 110) for i in range(3)]

FONTS = {
    "liberation": "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    "dejavu": "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "free": "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf",
}

BG = 140        # Gehaeuse
DRUM = 232      # Ziffernrolle
INK = 22        # Ziffer
FACE = 238      # Zifferblatt der Zeiger
NEEDLE = 40     # Zeiger (rot -> in Graustufen dunkel)


def digit_font(path, box_h):
    """Schrift so skalieren, dass die Ziffer 78 % der Fensterhoehe hoch ist."""
    target = int(box_h * 0.78)
    size = target
    font = ImageFont.truetype(path, size)
    for _ in range(30):
        b = font.getbbox("8")
        h = b[3] - b[1]
        if h == 0 or abs(h - target) <= 1:
            break
        size = max(4, int(round(size * target / h)))
        font = ImageFont.truetype(path, size)
    return font


def draw_digit(d, img, cx, cy, value, font):
    b = font.getbbox(str(value))
    w, h = b[2] - b[0], b[3] - b[1]
    d.text((cx - w / 2 - b[0], cy - h / 2 - b[1]), str(value), fill=INK, font=font)


def render(reading_str, font_key, last_frac, rotation, noise, blur, seed):
    random.seed(seed)
    n_int = len(DIGITS)
    n_dial = len(DIALS)

    txt = reading_str.replace(",", ".")
    if "." in txt:
        ip, fp = txt.split(".")
    else:
        ip, fp = txt, ""
    ip = ip.rjust(n_int, "0")
    fp = fp.ljust(n_dial, "0")
    if len(ip) != n_int or len(fp) != n_dial:
        raise SystemExit(f"Zaehlerstand muss {n_int} Vor- und {n_dial} Nachkommastellen haben")

    true_digits = [int(c) for c in ip] + [int(c) for c in fp]

    # Kontinuierliche Stellung von unten nach oben aufbauen.
    n = n_int + n_dial
    x = [0.0] * n
    x[n - 1] = true_digits[n - 1] + last_frac
    for e in range(n - 2, -1, -1):
        x[e] = true_digits[e] + x[e + 1] / 10.0

    img = Image.new("L", (W, H), BG)
    d = ImageDraw.Draw(img)
    font_path = FONTS[font_key]

    # Rahmen um das Zaehlwerk, wie ihn ein echtes Zaehlerfenster hat.
    fx0 = DIGITS[0][0] - 14
    fy0 = DIGITS[0][1] - 14
    fx1 = DIGITS[-1][0] + DIGITS[-1][2] + 14
    fy1 = DIGITS[0][1] + DIGITS[0][3] + 14
    d.rectangle([fx0, fy0, fx1, fy1], fill=90)

    for i, (rx, ry, rw, rh) in enumerate(DIGITS):
        d.rectangle([rx, ry, rx + rw - 1, ry + rh - 1], fill=DRUM)
        font = digit_font(font_path, rh)
        cx = rx + rw / 2
        base = ry + rh / 2
        frac = x[i] - math.floor(x[i])
        cur = int(math.floor(x[i])) % 10
        # Die Trommel dreht nach oben: die aktuelle Ziffer wandert mit
        # wachsendem Anteil aus dem Fenster heraus, die naechste kommt nach.
        window = img.crop((rx, ry, rx + rw, ry + rh))
        wd = ImageDraw.Draw(window)
        for k in (-1, 0, 1):
            val = (cur + k) % 10
            cy = (base - ry) + k * rh - frac * rh
            b = font.getbbox(str(val))
            tw, th = b[2] - b[0], b[3] - b[1]
            wd.text((rw / 2 - tw / 2 - b[0], cy - th / 2 - b[1]), str(val), fill=INK, font=font)
        img.paste(window, (rx, ry))

    for i, (rx, ry, rw, rh) in enumerate(DIALS):
        cx, cy = rx + rw / 2, ry + rh / 2
        r = min(rw, rh) / 2
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=FACE, outline=70, width=2)
        for t in range(10):
            a = math.radians(t * 36)
            d.line([cx + math.sin(a) * r * 0.86, cy - math.cos(a) * r * 0.86,
                    cx + math.sin(a) * r * 0.97, cy - math.cos(a) * r * 0.97], fill=70, width=2)
        pos = x[n_int + i]
        a = math.radians(pos * 36.0)
        d.line([cx, cy, cx + math.sin(a) * r * 0.8, cy - math.cos(a) * r * 0.8],
               fill=NEEDLE, width=4)
        d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=NEEDLE)

    if rotation:
        img = img.rotate(-rotation, resample=Image.BILINEAR, fillcolor=BG)
    if blur:
        from PIL import ImageFilter
        img = img.filter(ImageFilter.GaussianBlur(blur))
    if noise:
        px = img.load()
        for y in range(H):
            for xx in range(W):
                v = px[xx, y] + random.gauss(0, noise)
                px[xx, y] = max(0, min(255, int(v)))

    return img, true_digits, x


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reading", default="01234.567")
    ap.add_argument("--font", default="liberation", choices=sorted(FONTS))
    ap.add_argument("--last-frac", type=float, default=0.35)
    ap.add_argument("--rotation", type=float, default=0.0)
    ap.add_argument("--noise", type=float, default=3.0)
    ap.add_argument("--blur", type=float, default=0.6)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    img, digits, pos = render(a.reading, a.font, a.last_frac, a.rotation, a.noise, a.blur, a.seed)
    img.save(a.out)
    print(f"{a.out}: {a.reading} -> Stellen {digits}")
    print("  Rollenstellungen: " + ", ".join(f"{p:.3f}" for p in pos))


if __name__ == "__main__":
    main()
