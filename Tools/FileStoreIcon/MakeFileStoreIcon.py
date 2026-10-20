#!/usr/bin/env python3
"""
Generates FileStore.ico: a FileStore E01 stacked on a hard drive unit, in
the colours of the front panel drawing, for a shortcut to BeebEm in
FileStore mode. Each icon size is drawn separately so the small sizes stay
sharp.

Usage: python3 MakeFileStoreIcon.py [output.ico]
Requires Pillow (pip install pillow).
"""

import os
import re
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, "..", "..", "Src", "FileStorePanelArt.h")

CASE        = (222, 213, 190)
CASE_LIGHT  = (240, 234, 216)
CASE_SHADOW = (160, 150, 128)
PLINTH      = (196, 186, 160)
FLAP        = (150, 76, 26)   # Amber flap over the dark fascia
FLAP_EDGE   = (205, 140, 60)
STRIPE      = (236, 232, 214)
LABEL       = (18, 18, 18)
LABEL_TEXT  = (232, 230, 215)
RED         = (255, 45, 35)
GREEN       = (150, 245, 40)

SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]


class PanelArt:
    """Text outlines and Acorn logo from Src/FileStorePanelArt.h."""

    def __init__(self, path):
        text = open(path).read()

        def block(name):
            return re.search(name + r"[^{]*=\s*\{(.*?)\n\};", text, re.S).group(1)

        self.upm = int(re.search(r"UNITS_PER_EM\s*=\s*(\d+)", text).group(1))
        self.cap = int(re.search(r"CAP_HEIGHT\s*=\s*(\d+)", text).group(1))
        self.ops = [int(v) for v in re.findall(r"-?\d+", re.sub(r"//.*", "", block("PanelFontOutlines")))]
        self.glyphs = {c: (int(a), int(f), int(n)) for c, a, f, n in
                       re.findall(r"\{ '(.)', (-?\d+), (\d+), (\d+) \}", block("PanelFontGlyphs"))}
        self.logo = [[(float(x), float(y)) for x, y in re.findall(r"\{ (-?[\d.]+)f, (-?[\d.]+)f \}", block(name))]
                     for name in ("PanelLogoNut", "PanelLogoCup", "PanelLogoStalk")]
        self.logo_width = float(re.search(r"PANEL_LOGO_WIDTH = ([\d.]+)f", text).group(1))

    def width(self, text, size):
        return sum(self.glyphs.get(c, self.glyphs[" "])[0] for c in text) * size / self.upm

    def draw_text(self, im, text, x, baseline, size, colour, scale=4):
        """Draw text with its baseline at y, anti-aliased by supersampling."""
        w, h = im.size
        mask = Image.new("L", (w * scale, h * scale), 0)
        s = size / self.upm * scale

        for c in text:
            advance, first, count = self.glyphs.get(c, self.glyphs[" "])
            glyph = Image.new("L", mask.size, 0)
            i = first
            points = []
            ox, oy = x * scale, baseline * scale

            def pt(j):
                return (ox + self.ops[j] * s, oy - self.ops[j + 1] * s)

            while i < first + count:
                op = self.ops[i]
                if op == 0:
                    points = [pt(i + 1)]
                    i += 3
                elif op == 1:
                    points.append(pt(i + 1))
                    i += 3
                elif op == 2:
                    p0, p1, p2, p3 = points[-1], pt(i + 1), pt(i + 3), pt(i + 5)
                    for k in range(1, 9):
                        t = k / 8
                        points.append(tuple((1 - t) ** 3 * p0[n] + 3 * (1 - t) ** 2 * t * p1[n] +
                                            3 * (1 - t) * t * t * p2[n] + t ** 3 * p3[n] for n in range(2)))
                    i += 7
                else:
                    contour = Image.new("L", mask.size, 0)
                    ImageDraw.Draw(contour).polygon(points, fill=255)
                    glyph = Image.frombytes("L", mask.size,
                                            bytes(a ^ b for a, b in zip(glyph.tobytes(), contour.tobytes())))
                    i += 1

            mask = Image.frombytes("L", mask.size, bytes(max(a, b) for a, b in zip(mask.tobytes(), glyph.tobytes())))
            x += advance * size / self.upm

        mask = mask.resize((w, h), Image.LANCZOS)
        im.paste(Image.new("RGBA", (w, h), colour + (255,)), (0, 0), mask)

    def draw_logo(self, im, x, y, height, colour, scale=4):
        w, h = im.size
        mask = Image.new("L", (w * scale, h * scale), 0)
        d = ImageDraw.Draw(mask)
        for part in self.logo:
            d.polygon([((x + px * height) * scale, (y + py * height) * scale) for px, py in part], fill=255)
        mask = mask.resize((w, h), Image.LANCZOS)
        im.paste(Image.new("RGBA", (w, h), colour + (255,)), (0, 0), mask)


def draw_unit(im, d, art, x0, y0, w, h, size, lit_red, name):
    """One unit: case, amber flap with stripes, label, badge and LEDs."""
    s = size / 256.0
    edge = max(1, round(3 * s))

    # Case and recessed base
    base_h = max(1, round(h * 0.18))
    d.rectangle([x0 + round(w * 0.06), y0 + h - base_h, x0 + w - round(w * 0.06) - 1, y0 + h - 1], fill=PLINTH)
    case_bottom = y0 + h - base_h
    d.rectangle([x0, y0, x0 + w - 1, case_bottom - 1], fill=CASE)

    if size >= 32:
        d.rectangle([x0, y0, x0 + w - 1, y0 + edge - 1], fill=CASE_LIGHT)
        d.rectangle([x0, case_bottom - edge, x0 + w - 1, case_bottom - 1], fill=CASE_SHADOW)

    # Flap
    margin_x = max(1, round(w * 0.03))
    margin_y = max(1, round((case_bottom - y0) * 0.18))
    fx0, fy0 = x0 + margin_x, y0 + margin_y
    fx1, fy1 = x0 + w - margin_x - 1, case_bottom - margin_y - 1

    d.rectangle([fx0, fy0, fx1, fy1], fill=FLAP)

    if size >= 48:
        d.rectangle([fx0, fy0, fx1, fy0 + edge - 1], fill=FLAP_EDGE)

    # Stripes: whole pixel pitch, at least 2 pixels
    flap_w = fx1 - fx0 + 1
    stripes = 24 if size >= 128 else 12 if size >= 48 else 6 if size >= 24 else 4
    pitch = max(2, flap_w // stripes)
    stripes = flap_w // pitch
    offset = fx0 + (flap_w - stripes * pitch) // 2
    stripe_w = max(1, pitch // 2)

    for i in range(stripes):
        sx = offset + i * pitch + (pitch - stripe_w) // 2
        d.rectangle([sx, fy0 + (edge if size >= 48 else 0), sx + stripe_w - 1, fy1], fill=STRIPE)

    # Label on the left and badge on the right, across the middle of the flap
    flap_h = fy1 - fy0 + 1
    band_h = max(2, round(flap_h * 0.40))
    by0 = fy0 + (flap_h - band_h) // 2
    by1 = by0 + band_h - 1
    lx0 = offset + (pitch if size >= 48 else pitch // 2)
    lx1 = offset + max(2, round(stripes * 0.42)) * pitch
    bx0 = offset + stripes * pitch - max(2, round(stripes * 0.30)) * pitch
    bx1 = offset + stripes * pitch - (pitch if size >= 48 else pitch // 2) - 1

    if size >= 24:
        d.rectangle([lx0, by0, lx1, by1], fill=LABEL)
    d.rectangle([bx0, by0, bx1, by1], fill=LABEL)

    # The real lettering and logo on the larger sizes
    if size >= 128:
        text = "FILESTORE " + name
        text_size = band_h * 0.62 / (art.cap / art.upm)
        while art.width(text, text_size) > (lx1 - lx0) * 0.88:
            text_size -= 0.5
        cap = text_size * art.cap / art.upm
        art.draw_text(im, text, lx0 + ((lx1 - lx0) - art.width(text, text_size)) / 2,
                      by0 + (band_h + cap) / 2, text_size, LABEL_TEXT)

        logo_h = band_h * 0.80
        acorn_size = 0.71 * logo_h / (art.cap / art.upm)
        gap = art.width("n", acorn_size)
        total = art.width("Acorn", acorn_size) + gap + art.logo_width * logo_h
        while total > (bx1 - bx0) * 0.90:
            logo_h -= 0.5
            acorn_size = 0.71 * logo_h / (art.cap / art.upm)
            gap = art.width("n", acorn_size)
            total = art.width("Acorn", acorn_size) + gap + art.logo_width * logo_h
        ax = bx0 + ((bx1 - bx0) - total) / 2
        logo_top = by0 + (band_h - logo_h) / 2
        art.draw_text(im, "Acorn", ax, logo_top + 0.80 * logo_h, acorn_size, LABEL_TEXT)
        art.draw_logo(im, ax + art.width("Acorn", acorn_size) + gap, logo_top, logo_h, LABEL_TEXT)

    # Red and green LEDs, between the stripes left of the badge
    led = max(1, round(band_h * 0.45))
    ly = by0 + (band_h - led) // 2
    gx = bx0 - max(2, round(pitch * 1.2)) - led
    rx = gx - max(2, round(pitch * 1.5)) - led

    if size >= 24:
        d.rectangle([rx, ly, rx + led - 1, ly + led - 1], fill=RED if lit_red else (130, 25, 20))
    d.rectangle([gx, ly, gx + led - 1, ly + led - 1], fill=GREEN)


def draw_icon(art, size):
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    if size >= 48:
        # The FileStore with a hard drive unit underneath, filling the width
        unit_h = round(size * 0.40)
        gap = max(1, round(size * 0.04))
        top = (size - (2 * unit_h + gap)) // 2

        draw_unit(im, d, art, 0, top, size, unit_h, size, True, "E01")
        draw_unit(im, d, art, 0, top + unit_h + gap, size, unit_h, size, False, "E20")
    else:
        # Just the FileStore at the small sizes, taller so it stays clear
        unit_h = round(size * 0.62)
        draw_unit(im, d, art, 0, (size - unit_h) // 2, size, unit_h, size, True, "E01")

    return im


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "FileStore.ico")
    art = PanelArt(ART)
    images = [draw_icon(art, size) for size in SIZES]

    images[-1].save(out_path, format="ICO", sizes=[(s, s) for s in SIZES],
                    append_images=images[:-1])



if __name__ == "__main__":
    main()
