#!/usr/bin/env python3
"""
Generates book-themed deep-sleep screensaver images for the e-reader, in the
style of a Kindle's sleep screen: minimalist grayscale line art, landscape,
optionally with a short caption.

Output is written both as a .png (for quick preview on a normal computer)
and as a .raw file in the LilyGo EPD47's native 4-bit grayscale framebuffer
format (960x540, 2 pixels packed per byte, 0=black..15=white), ready to drop
onto the SD card's /images folder alongside any existing screensaver .raw
files - main.cpp's drawSleepScreen() picks a random/rotating one from there
each time the device goes to sleep.

Usage:
    python3 tools/generate_sleep_images.py [--out-dir data/images] [--native]

    --native   Pack pixels in the reversed nibble order used by the
               NATIVE_TESTING/SDL mock build instead of the real hardware
               (t5-47-s3) build. Only useful for previewing inside the
               desktop mock; real device .raw files must use the default
               (hardware) order. See DisplayHAL::setPixel for the two orders.
"""
import argparse
import os

from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

WIDTH, HEIGHT = 960, 540
FONT_PATH = os.path.join(os.path.dirname(__file__), "..", "data", "Roboto-Regular.ttf")

BLACK = 0
WHITE = 255


def _font(size):
    try:
        return ImageFont.truetype(FONT_PATH, size)
    except OSError:
        return ImageFont.load_default()


def _caption(draw, text, y, size=30, fill=40):
    font = _font(size)
    bbox = draw.textbbox((0, 0), text, font=font)
    w = bbox[2] - bbox[0]
    draw.text(((WIDTH - w) / 2, y), text, font=font, fill=fill)


def scene_moon_and_book(img, draw):
    """Crescent moon over stars, an open book silhouette resting below."""
    import random
    random.seed(1)
    for _ in range(40):
        x = random.randint(20, WIDTH - 20)
        y = random.randint(20, 260)
        r = random.choice([1, 1, 2])
        draw.ellipse((x - r, y - r, x + r, y + r), fill=90)

    # Crescent moon: a dark circle with a lighter circle overlapping its
    # edge to carve out the crescent shape.
    cx, cy, r = 760, 140, 90
    draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=30)
    draw.ellipse((cx - r + 45, cy - r - 20, cx + r + 45, cy + r - 20), fill=WHITE)

    # Open book silhouette (two curved pages meeting at a spine)
    base_y = 430
    left_pts = [(180, base_y)]
    for i in range(9):
        t = i / 8
        left_pts.append((180 + t * 300, base_y - 90 * (1 - (2 * t - 1) ** 2) - 10))
    left_pts.append((480, base_y))
    right_pts = [(480, base_y)]
    for i in range(9):
        t = i / 8
        right_pts.append((480 + t * 300, base_y - 90 * (1 - (2 * t - 1) ** 2) - 10))
    right_pts.append((780, base_y))
    draw.polygon(left_pts, fill=20)
    draw.polygon(right_pts, fill=20)
    draw.line((480, base_y - 100, 480, base_y), fill=BLACK, width=3)
    # A few page lines for texture
    for i in range(1, 5):
        draw.line((180 + i * 30, base_y - i * 6, 480 - i * 4, base_y - 4), fill=90, width=1)
        draw.line((780 - i * 30, base_y - i * 6, 480 + i * 4, base_y - 4), fill=90, width=1)

    _caption(draw, "Sweet dreams, bookworm.", 470, size=32)


def scene_candle_and_stack(img, draw):
    """Three stacked books with a lit candle resting on top."""
    base_y = 460
    widths = [420, 360, 300]
    heights = [34, 34, 34]
    x_center = WIDTH // 2
    y = base_y
    for w, h in zip(widths, heights):
        draw.rounded_rectangle(
            (x_center - w / 2, y - h, x_center + w / 2, y), radius=6, fill=30, outline=BLACK, width=2
        )
        y -= h + 6

    # Candle on top of the stack
    candle_w, candle_h = 34, 110
    cx = x_center
    cy = y
    draw.rectangle((cx - candle_w / 2, cy - candle_h, cx + candle_w / 2, cy), fill=200, outline=BLACK, width=2)
    # Flame (soft blurred glow, dithers nicely to grayscale)
    flame_layer = Image.new("L", (WIDTH, HEIGHT), WHITE)
    fdraw = ImageDraw.Draw(flame_layer)
    fx, fy = cx, cy - candle_h - 28
    fdraw.ellipse((fx - 26, fy - 34, fx + 26, fy + 34), fill=60)
    flame_layer = flame_layer.filter(ImageFilter.GaussianBlur(10))
    # Darken-composite the blurred glow onto the existing drawing instead of
    # overwriting it outright (a plain paste would wipe out the book stack).
    img.paste(ImageChops.darker(img, flame_layer), (0, 0))
    draw.polygon([(cx, cy - candle_h - 46), (cx - 10, cy - candle_h - 8), (cx + 10, cy - candle_h - 8)], fill=10)
    draw.line((cx, cy - candle_h, cx, cy - candle_h - 8), fill=BLACK, width=2)

    _caption(draw, "Time to rest your eyes.", 490, size=32)


def scene_glasses_on_book(img, draw):
    """Reading glasses resting on an open book."""
    base_y = 400
    draw.polygon([(230, base_y), (480, base_y - 70), (730, base_y), (480, base_y + 20)], fill=25)
    draw.line((480, base_y - 70, 480, base_y + 20), fill=BLACK, width=2)
    for i in range(1, 6):
        draw.line((240 + i * 8, base_y - i * 2, 470 - i * 4, base_y - i), fill=100, width=1)
        draw.line((720 - i * 8, base_y - i * 2, 490 + i * 4, base_y - i), fill=100, width=1)

    # Glasses: two lens circles + bridge + temple arms
    gy = base_y - 40
    lens_r = 55
    lcx, rcx = 400, 560
    draw.ellipse((lcx - lens_r, gy - lens_r, lcx + lens_r, gy + lens_r), outline=BLACK, width=6)
    draw.ellipse((rcx - lens_r, gy - lens_r, rcx + lens_r, gy + lens_r), outline=BLACK, width=6)
    draw.line((lcx + lens_r, gy, rcx - lens_r, gy), fill=BLACK, width=6)
    draw.line((lcx - lens_r, gy, lcx - lens_r - 60, gy - 18), fill=BLACK, width=5)
    draw.line((rcx + lens_r, gy, rcx + lens_r + 60, gy - 18), fill=BLACK, width=5)
    # Lens sheen
    draw.arc((lcx - 30, gy - 40, lcx + 10, gy), 200, 320, fill=180, width=3)
    draw.arc((rcx - 30, gy - 40, rcx + 10, gy), 200, 320, fill=180, width=3)

    _caption(draw, "Pages paused...", 470, size=32)


def scene_quill_and_inkwell(img, draw):
    """A quill leaning in an inkwell beside a rolled scroll."""
    # Inkwell (small rounded jar)
    jx, jy = 330, 400
    draw.polygon([(jx - 55, jy), (jx - 65, jy - 60), (jx + 65, jy - 60), (jx + 55, jy)], fill=20)
    draw.ellipse((jx - 65, jy - 70, jx + 65, jy - 50), fill=BLACK)
    draw.ellipse((jx - 45, jy - 65, jx + 45, jy - 53), fill=60)

    # Quill feather (a curved shaft with a few barb strokes)
    shaft = [(jx + 10, jy - 60)]
    for i in range(1, 14):
        t = i / 13
        shaft.append((jx + 10 + t * 260, jy - 60 - t * 300 + 40 * t * t))
    draw.line(shaft, fill=BLACK, width=6, joint="curve")
    for i in range(2, 12, 2):
        bx, by = shaft[i]
        draw.line((bx, by, bx - 30, by - 40), fill=40, width=4)
        draw.line((bx, by, bx + 24, by - 46), fill=40, width=4)

    # Rolled scroll to the right
    sx, sy = 650, 400
    draw.rounded_rectangle((sx, sy - 40, sx + 220, sy), radius=20, fill=30, outline=BLACK, width=3)
    draw.ellipse((sx - 20, sy - 40, sx + 20, sy), fill=200, outline=BLACK, width=3)
    draw.ellipse((sx + 200, sy - 40, sx + 240, sy), fill=200, outline=BLACK, width=3)
    for i in range(3):
        draw.line((sx + 30, sy - 30 + i * 8, sx + 190, sy - 30 + i * 8), fill=110, width=2)

    _caption(draw, "The story continues later.", 470, size=32)


def scene_coffee_and_books(img, draw):
    """A steaming cup beside a small stack of books."""
    base_y = 440
    widths = [200, 170, 140]
    x_left = 260
    y = base_y
    for i, w in enumerate(widths):
        draw.rounded_rectangle((x_left - w / 2 + i * 6, y - 30, x_left + w / 2 + i * 6, y), radius=5, fill=25, outline=BLACK, width=2)
        y -= 36

    # Cup
    cx, cy = 620, base_y - 10
    draw.rounded_rectangle((cx - 70, cy - 90, cx + 70, cy), radius=10, fill=200, outline=BLACK, width=4)
    draw.arc((cx + 55, cy - 70, cx + 120, cy - 15), -90, 90, fill=BLACK, width=6)
    draw.ellipse((cx - 70, cy - 100, cx + 70, cy - 80), fill=60, outline=BLACK, width=3)

    # Steam (soft blurred wisps)
    steam_layer = Image.new("L", (WIDTH, HEIGHT), WHITE)
    sdraw = ImageDraw.Draw(steam_layer)
    for dx in (-25, 0, 25):
        pts = [(cx + dx, cy - 100 - t) for t in range(0, 120, 6)]
        pts = [(x + 14 * ((i % 4) - 1.5), y) for i, (x, y) in enumerate(pts)]
        sdraw.line(pts, fill=90, width=6)
    steam_layer = steam_layer.filter(ImageFilter.GaussianBlur(4))
    # Darken-composite the steam wisps instead of overwriting the whole
    # canvas (a plain paste would wipe out the cup and book stack).
    img.paste(ImageChops.darker(img, steam_layer), (0, 0))

    _caption(draw, "Bookmark saved.", 480, size=32)


def scene_bird_on_book(img, draw):
    """A small bird perched on an open book resting on a branch."""
    # Branch
    draw.line((60, 380, 900, 340), fill=30, width=14)
    draw.line((700, 355, 780, 300), fill=30, width=8)

    # Open book on the branch
    base_y = 370
    draw.polygon([(340, base_y), (500, base_y - 40), (660, base_y), (500, base_y + 15)], fill=20)
    draw.line((500, base_y - 40, 500, base_y + 15), fill=BLACK, width=2)

    # Bird body + head + beak + tail, perched on the book's spine
    bx, by = 500, base_y - 90
    draw.ellipse((bx - 45, by - 40, bx + 45, by + 40), fill=BLACK)
    draw.ellipse((bx - 20, by - 75, bx + 30, by - 25), fill=BLACK)
    draw.polygon([(bx + 28, by - 55), (bx + 55, by - 50), (bx + 28, by - 42)], fill=60)
    draw.polygon([(bx - 45, by + 10), (bx - 90, by - 5), (bx - 45, by - 25)], fill=BLACK)
    draw.ellipse((bx + 5, by - 62, bx + 13, by - 54), fill=WHITE)

    _caption(draw, "Shh... resting.", 460, size=32)


SCENES = [
    ("sleep_moon_and_book", scene_moon_and_book),
    ("sleep_candle_and_stack", scene_candle_and_stack),
    ("sleep_glasses_on_book", scene_glasses_on_book),
    ("sleep_quill_and_inkwell", scene_quill_and_inkwell),
    ("sleep_coffee_and_books", scene_coffee_and_books),
    ("sleep_bird_on_book", scene_bird_on_book),
]


def quantize_to_4bit(img_l):
    """Floyd-Steinberg dither an 8-bit grayscale image down to 16 levels
    (4 bits/pixel) so gradients (glows, steam, sheens) still read cleanly on
    the e-paper panel instead of banding."""
    palette_img = Image.new("P", (1, 1))
    palette = []
    for i in range(16):
        v = round(i * 255 / 15)
        palette.extend([v, v, v])
    palette.extend([0, 0, 0] * (256 - 16))
    palette_img.putpalette(palette)
    quantized = img_l.convert("RGB").quantize(palette=palette_img, dither=Image.FLOYDSTEINBERG)
    return quantized.convert("L")


def pack_4bpp(img_l, native_order):
    """Packs an 8-bit (already-quantized-to-16-levels) grayscale image into
    the EPD47's 4bpp framebuffer format: 960x540, 2 pixels per byte, value
    0=black..15=white.

    Nibble order depends on target (see DisplayHAL::setPixel):
      - hardware (t5-47-s3, the default / real device): even x -> low
        nibble, odd x -> high nibble.
      - native mock (--native): even x -> high nibble, odd x -> low nibble.
    """
    pixels = img_l.load()
    out = bytearray(WIDTH * HEIGHT // 2)
    for y in range(HEIGHT):
        row_base = y * WIDTH
        for x in range(0, WIDTH, 2):
            v0 = pixels[x, y] * 15 // 255
            v1 = pixels[x + 1, y] * 15 // 255
            idx = (row_base + x) // 2
            if native_order:
                out[idx] = (v0 << 4) | v1
            else:
                out[idx] = (v1 << 4) | v0
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", default="data/images", help="Directory to write .raw/.png files into")
    parser.add_argument("--native", action="store_true", help="Pack for the NATIVE_TESTING/SDL mock instead of real hardware")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    for name, scene_fn in SCENES:
        img = Image.new("L", (WIDTH, HEIGHT), WHITE)
        draw = ImageDraw.Draw(img)
        scene_fn(img, draw)

        quantized = quantize_to_4bit(img)

        png_path = os.path.join(args.out_dir, f"{name}.png")
        quantized.save(png_path)

        raw_bytes = pack_4bpp(quantized, args.native)
        raw_path = os.path.join(args.out_dir, f"{name}.raw")
        with open(raw_path, "wb") as f:
            f.write(raw_bytes)

        expected = WIDTH * HEIGHT // 2
        assert len(raw_bytes) == expected, f"{name}: expected {expected} bytes, got {len(raw_bytes)}"
        print(f"Wrote {raw_path} ({len(raw_bytes)} bytes) and {png_path}")


if __name__ == "__main__":
    main()
