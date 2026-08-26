#!/usr/bin/env python3
"""Procedurally draws SnesFox.app's icon (a geometric fox head, no external art assets) and
writes a macOS .iconset directory. Colors are pulled from display.cpp's applyModernDarkTheme()
so the icon matches the app's own in-window dark/teal theme.

Usage: tools/make_icon.py <output.iconset dir>
Then: iconutil -c icns <output.iconset> -o AppIcon.icns  (done by release-emu-binary-app.sh)
"""
import sys
import os
from PIL import Image, ImageDraw

SIZE = 1024

BG_TOP = (26, 28, 33, 255)      # ~ theme bg   (0.10, 0.11, 0.13)
BG_BOTTOM = (15, 16, 19, 255)   # slightly darker, for a subtle vertical gradient
ACCENT = (79, 199, 183, 255)    # theme accent (0.31, 0.78, 0.72)
ACCENT_DARK = (66, 173, 160, 255)
CREAM = (232, 234, 235, 255)    # theme text   (0.90, 0.91, 0.92)
DARK = (18, 19, 22, 255)


def draw_icon():
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    for y in range(SIZE):
        t = y / (SIZE - 1)
        row = tuple(int(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * t) for i in range(3))
        draw.line([(0, y), (SIZE, y)], fill=row + (255,))

    cx, cy = SIZE // 2, 570

    # Ears (two accent triangles, tips pointing up/outward).
    draw.polygon([(cx - 300, cy - 30), (cx - 120, cy - 30), (cx - 230, cy - 300)], fill=ACCENT)
    draw.polygon([(cx + 300, cy - 30), (cx + 120, cy - 30), (cx + 230, cy - 300)], fill=ACCENT)
    # Inner-ear notches, darker, echoing the outer ear shape.
    draw.polygon([(cx - 250, cy - 55), (cx - 155, cy - 55), (cx - 210, cy - 230)], fill=ACCENT_DARK)
    draw.polygon([(cx + 250, cy - 55), (cx + 155, cy - 55), (cx + 210, cy - 230)], fill=ACCENT_DARK)

    # Head (broad accent triangle).
    draw.polygon([(cx - 280, cy - 20), (cx + 280, cy - 20), (cx, cy + 310)], fill=ACCENT)

    # Muzzle (lighter, smaller triangle nested near the bottom of the head).
    draw.polygon([(cx - 130, cy + 30), (cx + 130, cy + 30), (cx, cy + 250)], fill=CREAM)

    # Eyes.
    eye_r = 20
    for ex in (cx - 130, cx + 130):
        ey = cy + 70
        draw.ellipse([ex - eye_r, ey - eye_r, ex + eye_r, ey + eye_r], fill=DARK)

    # Nose.
    draw.polygon([(cx - 26, cy + 205), (cx + 26, cy + 205), (cx, cy + 240)], fill=DARK)

    return img


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <output.iconset dir>", file=sys.stderr)
        return 1
    iconset_dir = sys.argv[1]
    os.makedirs(iconset_dir, exist_ok=True)

    master = draw_icon()
    # (filename, pixel size) — the exact 10 entries iconutil expects in a .iconset.
    variants = [
        ("icon_16x16.png", 16),
        ("icon_16x16@2x.png", 32),
        ("icon_32x32.png", 32),
        ("icon_32x32@2x.png", 64),
        ("icon_128x128.png", 128),
        ("icon_128x128@2x.png", 256),
        ("icon_256x256.png", 256),
        ("icon_256x256@2x.png", 512),
        ("icon_512x512.png", 512),
        ("icon_512x512@2x.png", 1024),
    ]
    for filename, size in variants:
        resized = master.resize((size, size), Image.LANCZOS)
        resized.save(os.path.join(iconset_dir, filename))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
