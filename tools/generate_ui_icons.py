#!/usr/bin/env python3
"""
Generates the launcher/app line icons used by the portrait UI kit.

Each icon is drawn with PIL at 4x resolution, downsampled for anti-aliasing,
quantised to 16 grey levels and packed 2 pixels per byte (first pixel in the
HIGH nibble - this is the icon format's own order, independent of the
display's framebuffer nibble order; UI::icon() unpacks it pixel by pixel).

Usage:
    python3 tools/generate_ui_icons.py            # writes src/ui/ui_icons.h
    python3 tools/generate_ui_icons.py --preview  # also writes ui_icons_preview.png
"""
import argparse
import math
import os

from PIL import Image, ImageDraw

SIZE = 96
SS = 4  # supersampling factor
S = SIZE * SS
STROKE = 6 * SS
OUT = os.path.join(os.path.dirname(__file__), "..", "src", "ui", "ui_icons.h")


def canvas():
    img = Image.new("L", (S, S), 255)
    return img, ImageDraw.Draw(img)


def rrect(d, box, r, width=STROKE, fill=None):
    d.rounded_rectangle([v * SS for v in box], radius=r * SS, outline=0, width=width, fill=fill)


def line(d, pts, width=STROKE):
    d.line([(x * SS, y * SS) for x, y in pts], fill=0, width=width, joint="curve")
    for x, y in (pts[0], pts[-1]):
        r = width / 2
        d.ellipse((x * SS - r, y * SS - r, x * SS + r, y * SS + r), fill=0)


def icon_book():
    img, d = canvas()
    # Open book: two pages meeting at a spine.
    line(d, [(48, 26), (48, 78)])
    d.polygon([(48 * SS, 26 * SS), (14 * SS, 18 * SS), (14 * SS, 70 * SS), (48 * SS, 78 * SS)], outline=0)
    line(d, [(48, 26), (14, 18), (14, 70), (48, 78)])
    line(d, [(48, 26), (82, 18), (82, 70), (48, 78)])
    for i, y in enumerate((34, 44, 54)):
        line(d, [(22, y - 4 + i), (40, y + i)], width=3 * SS)
        line(d, [(56, y + i), (74, y - 4 + i)], width=3 * SS)
    return img


def icon_bookmark():
    img, d = canvas()
    pts = [(28, 12), (68, 12), (68, 84), (48, 66), (28, 84), (28, 12)]
    line(d, pts)
    line(d, [(38, 30), (58, 30)], width=4 * SS)
    return img


def icon_image():
    img, d = canvas()
    rrect(d, (10, 18, 86, 78), 8)
    d.ellipse((60 * SS, 28 * SS, 72 * SS, 40 * SS), fill=0)
    line(d, [(18, 70), (38, 46), (52, 60), (62, 50), (78, 70)], width=5 * SS)
    return img


def icon_folder():
    img, d = canvas()
    line(d, [(10, 26), (10, 78), (86, 78), (86, 32), (46, 32), (38, 22), (14, 22), (10, 26)])
    line(d, [(10, 40), (86, 40)], width=4 * SS)
    return img


def icon_calculator():
    img, d = canvas()
    rrect(d, (20, 10, 76, 86), 8)
    rrect(d, (28, 18, 68, 34), 3, width=4 * SS)
    for row in range(3):
        for col in range(3):
            cx = 32 + col * 16
            cy = 48 + row * 13
            d.rounded_rectangle(((cx - 5) * SS, (cy - 4) * SS, (cx + 5) * SS, (cy + 4) * SS), radius=2 * SS, fill=0)
    return img


def icon_settings():
    img, d = canvas()
    cx, cy = 48 * SS, 48 * SS
    teeth = 8
    pts = []
    for i in range(teeth * 4):
        a = 2 * math.pi * i / (teeth * 4)
        r = 38 if (i % 4) in (0, 1) else 29
        pts.append((cx + r * SS * math.cos(a), cy + r * SS * math.sin(a)))
    d.polygon(pts, fill=0)
    r = 22 * SS
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)
    r = 12 * SS
    d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=0, width=STROKE)
    return img


def icon_bluetooth():
    img, d = canvas()
    line(d, [(30, 30), (66, 64), (48, 82), (48, 14), (66, 32), (30, 66)])
    return img


def icon_sleep():
    img, d = canvas()
    # Crescent moon with two small stars.
    r = 34 * SS
    cx, cy = 44 * SS, 52 * SS
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=0)
    r2 = 30 * SS
    cx2, cy2 = 62 * SS, 38 * SS
    d.ellipse((cx2 - r2, cy2 - r2, cx2 + r2, cy2 + r2), fill=255)
    for sx, sy, sr in ((68, 30, 5), (80, 46, 3)):
        d.ellipse(((sx - sr) * SS, (sy - sr) * SS, (sx + sr) * SS, (sy + sr) * SS), fill=0)
    return img


def icon_ship():
    img, d = canvas()
    # Three-masted clipper under sail.
    hull = [(8, 70), (88, 70), (78, 84), (20, 84)]
    d.polygon([(x * SS, y * SS) for x, y in hull], fill=0)
    for mx, top in ((30, 16), (50, 8), (70, 18)):
        line(d, [(mx, top), (mx, 70)], width=4 * SS)
    sails = [
        [(18, 22), (42, 22), (40, 38), (20, 38)], [(17, 42), (43, 42), (41, 62), (19, 62)],
        [(38, 14), (62, 14), (60, 32), (40, 32)], [(37, 36), (63, 36), (61, 62), (39, 62)],
        [(60, 24), (80, 24), (78, 40), (62, 40)], [(59, 44), (81, 44), (79, 62), (61, 62)],
    ]
    for sail in sails:
        d.polygon([(x * SS, y * SS) for x, y in sail], fill=255, outline=0)
        d.line([(x * SS, y * SS) for x, y in sail + [sail[0]]], fill=0, width=3 * SS)
    line(d, [(88, 70), (94, 52)], width=3 * SS)  # bowsprit
    line(d, [(94, 52), (70, 18)], width=2 * SS)  # jib stay
    return img


ICONS = [
    ("ICON_BOOK", icon_book),
    ("ICON_BOOKMARK", icon_bookmark),
    ("ICON_IMAGE", icon_image),
    ("ICON_FOLDER", icon_folder),
    ("ICON_CALCULATOR", icon_calculator),
    ("ICON_SETTINGS", icon_settings),
    ("ICON_BLUETOOTH", icon_bluetooth),
    ("ICON_SLEEP", icon_sleep),
    ("ICON_SHIP", icon_ship),
]


def pack(img):
    small = img.resize((SIZE, SIZE), Image.LANCZOS)
    px = list(small.tobytes())
    out = bytearray()
    for i in range(0, len(px), 2):
        a = min(15, round(px[i] / 17))
        b = min(15, round(px[i + 1] / 17))
        out.append((a << 4) | b)
    return small, bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", action="store_true")
    args = ap.parse_args()

    blobs = []
    previews = []
    for name, fn in ICONS:
        small, data = pack(fn())
        blobs.append((name, data))
        previews.append(small)

    with open(OUT, "w") as f:
        f.write("// Generated by tools/generate_ui_icons.py - do not edit by hand.\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("#define UI_ICON_SIZE %d\n\n" % SIZE)
        f.write("enum UIIconId {\n")
        for i, (name, _) in enumerate(blobs):
            f.write("    %s = %d,\n" % (name, i))
        f.write("    ICON_COUNT = %d\n};\n\n" % len(blobs))
        f.write("// 4-bit grey, first pixel in the high nibble, 15 = background.\n")
        f.write("extern const uint8_t g_uiIcons[%d][%d];\n\n" % (len(blobs), SIZE * SIZE // 2))
        f.write("#ifdef UI_ICONS_DEFINE_DATA\n")
        f.write("const uint8_t g_uiIcons[%d][%d] = {\n" % (len(blobs), SIZE * SIZE // 2))
        for name, data in blobs:
            f.write("  { // %s\n" % name)
            for i in range(0, len(data), 24):
                f.write("    " + ", ".join("0x%02X" % b for b in data[i:i + 24]) + ",\n")
            f.write("  },\n")
        f.write("};\n#endif\n")

    if args.preview:
        sheet = Image.new("L", (SIZE * len(previews), SIZE), 255)
        for i, p in enumerate(previews):
            sheet.paste(p, (i * SIZE, 0))
        sheet.save("ui_icons_preview.png")
    print("Wrote %s (%d icons)" % (os.path.normpath(OUT), len(blobs)))


if __name__ == "__main__":
    main()
