#!/usr/bin/env python3
"""Rendert die eingebauten Start-Templates (Ziffern 0-9) und schreibt sie als
C-Header fuer die Firmware.

Die Templates sind nur ein *Bootstrap*: sie stammen aus einer serifenlosen
Standardschrift und nicht von einer echten Zaehlerrolle. Sobald der Anlernen-
Dialog der Web-UI benutzt wurde, ueberschreiben die echten Kamerabilder diese
Vorlagen und die Erkennung wird deutlich sicherer.

Aufruf:
    python3 tools/make_default_templates.py

Erzeugt: firmware/WaterMeterCam/default_templates.h
"""

import os


from PIL import Image, ImageDraw, ImageFont

TPL_W = 16
TPL_H = 24

# Zaehlerrollen benutzen eine kraeftige, leicht schmale Grotesk. Liberation Sans
# Bold kommt dem am naechsten von dem, was ueberall verfuegbar ist.
FONT_CANDIDATES = [
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf",
]

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "firmware", "WaterMeterCam", "default_templates.h")

# Auf dieser Groesse wird gerendert und danach flaechengemittelt verkleinert.
SS = 8


def pick_font():
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            return path
    raise SystemExit("Keine passende TTF gefunden - FONT_CANDIDATES anpassen.")


def render_digit(font_path, digit):
    """Rendert eine Ziffer weiss auf schwarz, formatfuellend, mit etwas Rand."""
    w, h = TPL_W * SS, TPL_H * SS
    # Zielhoehe der Ziffer: 78 % der Templatehoehe (auf der Rolle steht ober- und
    # unterhalb der Ziffer immer etwas Trommelflaeche).
    target_h = int(h * 0.78)

    size = target_h
    font = ImageFont.truetype(font_path, size)
    # Schriftgroesse so nachregeln, dass die Ziffernhoehe wirklich passt.
    for _ in range(30):
        box = font.getbbox(str(digit))
        cur_h = box[3] - box[1]
        if cur_h == 0:
            break
        if abs(cur_h - target_h) <= 1:
            break
        size = max(4, int(round(size * target_h / cur_h)))
        font = ImageFont.truetype(font_path, size)

    img = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(img)
    box = font.getbbox(str(digit))
    dw, dh = box[2] - box[0], box[3] - box[1]
    draw.text(((w - dw) / 2 - box[0], (h - dh) / 2 - box[1]), str(digit), fill=255, font=font)

    # Ziffern der Rolle sind meist etwas schmaler als die Schrift -> horizontal
    # leicht stauchen, damit sie den Rahmen nicht sprengen.
    small = img.resize((TPL_W, TPL_H), Image.BOX)
    return list(small.getdata())


def main():
    font_path = pick_font()
    rows = []
    for d in range(10):
        px = render_digit(font_path, d)
        assert len(px) == TPL_W * TPL_H
        # Eine Bildzeile je Quelltextzeile - so bleibt der Header lesbar und
        # es kann nie mitten in einer Zahl umgebrochen werden.
        lines = [
            "    " + ",".join(str(v) for v in px[y * TPL_W:(y + 1) * TPL_W]) + ","
            for y in range(TPL_H)
        ]
        lines[-1] = lines[-1].rstrip(",")
        rows.append("  { /* %d */\n%s\n  }" % (d, "\n".join(lines)))

    body = ",\n".join(rows)
    header = f"""// AUTOMATISCH ERZEUGT von tools/make_default_templates.py - nicht von Hand aendern.
//
// Start-Templates fuer die Ziffern 0-9, {TPL_W}x{TPL_H} Graustufen, Ziffer hell auf
// dunklem Grund. Sie dienen nur als Startpunkt; das Anlernen in der Web-UI
// ersetzt sie durch echte Ausschnitte des eigenen Zaehlers.
#pragma once

#include <Arduino.h>

#include "config.h"

static const uint8_t DEFAULT_TEMPLATES[NUM_CLASSES][TPL_W * TPL_H] PROGMEM = {{
{body}
}};
"""

    out = os.path.abspath(OUT)
    with open(out, "w") as fh:
        fh.write(header)
    print("geschrieben:", out, "-", os.path.getsize(out), "Bytes")

    # Kleine ASCII-Vorschau zur Sichtkontrolle.
    for d in range(10):
        px = render_digit(font_path, d)
        print(f"--- {d} ---")
        for y in range(TPL_H):
            print("".join(" .:-=+*#%@"[min(9, px[y * TPL_W + x] * 10 // 256)] for x in range(TPL_W)))


if __name__ == "__main__":
    main()
