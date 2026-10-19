#!/usr/bin/env python3
"""
Generates Src/FileStorePanelArt.h: vector outlines for the text and Acorn
logo on the FileStore front panel flap, drawn by BeebWinFileStore.cpp.

The text glyphs come from Vidaloka (Copyright (c) 2011 Cyreal, SIL Open
Font License 1.1, see OFL.txt), a close match to the lettering on the
FileStore labels. GDI+ can't load OpenType fonts with PostScript (CFF)
outlines, so the glyphs are stored as outlines rather than loading the
font at run time.

The logo was traced from a photo of a FileStore badge and smoothed. Its
three parts are the nut, the cup (with the neck down to the stalk) and the
stalk, in units of the logo height, origin top left, y down.

Usage: python3 MakePanelArt.py [font.otf] [output.h]
Requires fontTools (pip install fonttools).
"""

import os
import sys

from fontTools.pens.recordingPen import RecordingPen
from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))

# Characters available to the panel text
CHARS = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZcorn"

# Path operations, as stored in the outline array
OP_MOVE, OP_LINE, OP_CURVE, OP_CLOSE = 0, 1, 2, 3

LOGO_PARTS = [
    [
        (0.3131, 0.0000), (0.3229, 0.0004), (0.3327, 0.0015), (0.3441, 0.0038), (0.3546, 0.0068),
        (0.3649, 0.0106), (0.3751, 0.0152), (0.3837, 0.0198), (0.4009, 0.0289), (0.4153, 0.0380),
        (0.4271, 0.0471), (0.4374, 0.0562), (0.4469, 0.0653), (0.4560, 0.0745), (0.4644, 0.0836),
        (0.4722, 0.0927), (0.4794, 0.1018), (0.4864, 0.1109), (0.4936, 0.1201), (0.4999, 0.1292),
        (0.5052, 0.1383), (0.5097, 0.1474), (0.5141, 0.1565), (0.5187, 0.1657), (0.5230, 0.1748),
        (0.5272, 0.1839), (0.5313, 0.1930), (0.5350, 0.2021), (0.5383, 0.2112), (0.5416, 0.2204),
        (0.5450, 0.2295), (0.5485, 0.2386), (0.5517, 0.2477), (0.5547, 0.2568), (0.5577, 0.2660),
        (0.5605, 0.2751), (0.5629, 0.2842), (0.5649, 0.2933), (0.5667, 0.3024), (0.5687, 0.3116),
        (0.5709, 0.3207), (0.5731, 0.3298), (0.5750, 0.3389), (0.5767, 0.3480), (0.5781, 0.3571),
        (0.5793, 0.3663), (0.5810, 0.3754), (0.5831, 0.3845), (0.5853, 0.3936), (0.5871, 0.4027),
        (0.5886, 0.4119), (0.5896, 0.4210), (0.5904, 0.4301), (0.5912, 0.4392), (0.5922, 0.4483),
        (0.5931, 0.4574), (0.5941, 0.4666), (0.5945, 0.4757), (0.5940, 0.4848), (0.5935, 0.4954),
        (0.5912, 0.5000), (0.5851, 0.5023), (0.0410, 0.5023), (0.0350, 0.5000), (0.0327, 0.4954),
        (0.0321, 0.4848), (0.0316, 0.4757), (0.0321, 0.4666), (0.0330, 0.4574), (0.0339, 0.4483),
        (0.0349, 0.4392), (0.0357, 0.4301), (0.0366, 0.4210), (0.0376, 0.4119), (0.0390, 0.4027),
        (0.0409, 0.3936), (0.0431, 0.3845), (0.0452, 0.3754), (0.0468, 0.3663), (0.0481, 0.3571),
        (0.0495, 0.3480), (0.0512, 0.3389), (0.0531, 0.3298), (0.0553, 0.3207), (0.0574, 0.3116),
        (0.0595, 0.3024), (0.0613, 0.2933), (0.0632, 0.2842), (0.0657, 0.2751), (0.0685, 0.2660),
        (0.0715, 0.2568), (0.0744, 0.2477), (0.0777, 0.2386), (0.0811, 0.2295), (0.0846, 0.2204),
        (0.0878, 0.2112), (0.0912, 0.2021), (0.0949, 0.1930), (0.0989, 0.1839), (0.1031, 0.1748),
        (0.1075, 0.1657), (0.1120, 0.1565), (0.1164, 0.1474), (0.1209, 0.1383), (0.1262, 0.1292),
        (0.1326, 0.1201), (0.1397, 0.1109), (0.1467, 0.1018), (0.1540, 0.0927), (0.1617, 0.0836),
        (0.1702, 0.0745), (0.1792, 0.0653), (0.1887, 0.0562), (0.1990, 0.0471), (0.2109, 0.0380),
        (0.2252, 0.0289), (0.2424, 0.0198), (0.2511, 0.0152), (0.2612, 0.0106), (0.2715, 0.0068),
        (0.2821, 0.0038), (0.2935, 0.0015), (0.3033, 0.0004),
    ],
    [
        (0.5957, 0.5441), (0.6049, 0.5464), (0.6087, 0.5517), (0.6096, 0.5638), (0.6090, 0.5730),
        (0.6074, 0.5821), (0.6049, 0.5912), (0.6021, 0.6003), (0.5992, 0.6094), (0.5962, 0.6185),
        (0.5928, 0.6277), (0.5888, 0.6368), (0.5846, 0.6459), (0.5802, 0.6550), (0.5756, 0.6641),
        (0.5701, 0.6733), (0.5639, 0.6824), (0.5571, 0.6915), (0.5502, 0.7006), (0.5431, 0.7097),
        (0.5356, 0.7188), (0.5268, 0.7280), (0.5169, 0.7371), (0.5061, 0.7462), (0.4949, 0.7553),
        (0.4822, 0.7644), (0.4681, 0.7736), (0.4511, 0.7827), (0.4314, 0.7918), (0.4068, 0.8009),
        (0.3797, 0.8100), (0.3571, 0.8192), (0.3462, 0.8283), (0.3435, 0.8298), (0.3404, 0.8404),
        (0.3404, 0.8670), (0.2857, 0.8670), (0.2857, 0.8404), (0.2827, 0.8298), (0.2800, 0.8283),
        (0.2690, 0.8192), (0.2464, 0.8100), (0.2193, 0.8009), (0.1947, 0.7918), (0.1751, 0.7827),
        (0.1581, 0.7736), (0.1439, 0.7644), (0.1312, 0.7553), (0.1201, 0.7462), (0.1093, 0.7371),
        (0.0993, 0.7280), (0.0905, 0.7188), (0.0830, 0.7097), (0.0760, 0.7006), (0.0690, 0.6915),
        (0.0623, 0.6824), (0.0561, 0.6733), (0.0506, 0.6641), (0.0459, 0.6550), (0.0416, 0.6459),
        (0.0374, 0.6368), (0.0333, 0.6277), (0.0300, 0.6185), (0.0270, 0.6094), (0.0241, 0.6003),
        (0.0212, 0.5912), (0.0187, 0.5821), (0.0172, 0.5730), (0.0166, 0.5638), (0.0175, 0.5517),
        (0.0213, 0.5464), (0.0304, 0.5441),
    ],
    [
        (0.0147, 0.7748), (0.6266, 0.9405), (0.6352, 0.9447), (0.6420, 0.9513), (0.6465, 0.9596),
        (0.6482, 0.9690), (0.6469, 0.9784), (0.6427, 0.9870), (0.6361, 0.9938), (0.6277, 0.9983),
        (0.6184, 1.0000), (0.6090, 0.9987), (0.0081, 0.7966), (0.0049, 0.7951), (0.0023, 0.7926),
        (0.0006, 0.7895), (0.0000, 0.7859), (0.0005, 0.7824), (0.0020, 0.7792), (0.0045, 0.7766),
        (0.0077, 0.7750), (0.0112, 0.7743),
    ],
]

def main():
    font_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "Vidaloka-Regular.otf")
    out_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "..", "Src", "FileStorePanelArt.h")

    font = TTFont(font_path)
    cmap = font.getBestCmap()
    glyph_set = font.getGlyphSet()
    hmtx = font["hmtx"]
    upm = font["head"].unitsPerEm
    cap_height = font["OS/2"].sCapHeight

    ops = []
    glyphs = []

    for ch in CHARS:
        name = cmap[ord(ch)]
        pen = RecordingPen()
        glyph_set[name].draw(pen)
        first = len(ops)

        for op, pts in pen.value:
            # Font units, y up
            if op == "moveTo":
                ops += [OP_MOVE, *map(round, pts[0])]
            elif op == "lineTo":
                ops += [OP_LINE, *map(round, pts[0])]
            elif op == "curveTo":
                if len(pts) != 3:
                    raise ValueError("Unexpected curve in glyph " + name)
                ops += [OP_CURVE] + [round(v) for p in pts for v in p]
            elif op in ("closePath", "endPath"):
                ops += [OP_CLOSE]
            else:
                raise ValueError("Unexpected path operation %s in glyph %s" % (op, name))

        glyphs.append((ch, hmtx[name][0], first, len(ops) - first))

    names = {cmap[ord(ch)]: ch for ch in CHARS}
    kerning = []

    if "kern" in font:
        for (left, right), value in sorted(font["kern"].kernTables[0].kernTable.items()):
            if left in names and right in names and value != 0:
                kerning.append((names[left], names[right], value))

    def char_literal(ch):
        return "'\\''" if ch == "'" else "'%s'" % ch

    out = []
    out.append("// Generated by Tools/FileStorePanelArt/MakePanelArt.py - do not edit.")
    out.append("//")
    out.append("// Text glyph outlines from Vidaloka, Copyright (c) 2011 Cyreal")
    out.append("// (www.cyreal.org), licensed under the SIL Open Font License 1.1,")
    out.append("// see Tools/FileStorePanelArt/OFL.txt.")
    out.append("//")
    out.append("// Acorn logo traced from a photo of a FileStore badge.")
    out.append("")
    out.append("#ifndef FILESTORE_PANEL_ART_HEADER")
    out.append("#define FILESTORE_PANEL_ART_HEADER")
    out.append("")
    out.append("constexpr int PANEL_FONT_UNITS_PER_EM = %d;" % upm)
    out.append("constexpr int PANEL_FONT_CAP_HEIGHT   = %d;" % cap_height)
    out.append("")
    out.append("// Outline operations: move (x, y), line (x, y), curve (x1, y1, x2, y2, x, y)")
    out.append("// and close, in font units with y up")
    out.append("constexpr short PANEL_OP_MOVE  = %d;" % OP_MOVE)
    out.append("constexpr short PANEL_OP_LINE  = %d;" % OP_LINE)
    out.append("constexpr short PANEL_OP_CURVE = %d;" % OP_CURVE)
    out.append("constexpr short PANEL_OP_CLOSE = %d;" % OP_CLOSE)
    out.append("")
    out.append("static const short PanelFontOutlines[] = {")
    for ch, advance, first, count in glyphs:
        if count:
            body = ops[first:first + count]
            out.append("\t// %s" % ("space" if ch == " " else ch))
            for i in range(0, len(body), 16):
                out.append("\t" + ", ".join(str(v) for v in body[i:i + 16]) + ",")
    out.append("};")
    out.append("")
    out.append("struct PanelGlyph")
    out.append("{")
    out.append("\tchar Char;")
    out.append("\tshort Advance;")
    out.append("\tunsigned short First;")
    out.append("\tunsigned short Count;")
    out.append("};")
    out.append("")
    out.append("static const PanelGlyph PanelFontGlyphs[] = {")
    for ch, advance, first, count in glyphs:
        out.append("\t{ %s, %d, %d, %d }," % (char_literal(ch), advance, first, count))
    out.append("};")
    out.append("")
    out.append("struct PanelKerningPair")
    out.append("{")
    out.append("\tchar Left;")
    out.append("\tchar Right;")
    out.append("\tshort Value;")
    out.append("};")
    out.append("")
    out.append("static const PanelKerningPair PanelFontKerning[] = {")
    for left, right, value in kerning:
        out.append("\t{ %s, %s, %d }," % (char_literal(left), char_literal(right), value))
    out.append("};")
    out.append("")
    out.append("// Acorn logo, in units of its height, origin top left, y down. Each part")
    out.append("// is a closed polygon.")
    out.append("constexpr float PANEL_LOGO_WIDTH = %.4ff;" % max(x for part in LOGO_PARTS for x, _ in part))
    out.append("")
    for name, part in zip(("Nut", "Cup", "Stalk"), LOGO_PARTS):
        out.append("static const float PanelLogo%s[][2] = {" % name)
        for i in range(0, len(part), 4):
            out.append("\t" + " ".join("{ %.4ff, %.4ff }," % p for p in part[i:i + 4]))
        out.append("};")
        out.append("")
    out.append("#endif")

    with open(out_path, "w", newline="\n") as f:
        f.write("\n".join(out) + "\n")

if __name__ == "__main__":
    main()
