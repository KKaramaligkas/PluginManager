#!/usr/bin/env python3
"""
Generates the Plugin Manager artwork:
  res/ICON0.PNG, res/PIC1.PNG           (XMB icon and background of the app)
  store/icons/<id>.png                  (144x80 store tiles for entries without art)
and extracts ICON0.PNG from homebrew EBOOT.PBP files given with --pbp id=path.

Requires Pillow. Usage:
  python3 tools/make_art.py [--pbp daedalusx64=/path/EBOOT.PBP ...]
"""

import argparse
import math
import os
import struct

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
FONT_REG = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

CATEGORY = {
    "plugin": (138, 92, 232),
    "homebrew": (28, 168, 158),
    "emulator": (222, 84, 84),
    "game": (72, 178, 92),
    "utility": (58, 128, 228),
    "theme": (226, 98, 168),
}

# id, title, subtitle, category (entries whose release has no ICON0 of its own)
TILES = [
    ("xmbih", "XMB Item Hider", "VSH plugin", "plugin"),
    ("gclite", "Game Categories Lite", "VSH plugin", "plugin"),
    ("cheatdevice", "CheatDevice Remastered", "GTA LCS / VCS", "plugin"),
    ("tempar", "TempAR", "Cheat device", "plugin"),
    ("cddaenabler", "CDDA Enabler", "POPS plugin", "plugin"),
    ("zerovsh", "ZeroVSH Patcher", "VSH plugin", "plugin"),
    ("remotejoylite", "RemoteJoyLite", "Screen streaming", "plugin"),
    ("aemu", "PRO Online", "Online ad-hoc play", "plugin"),
]


def font(path, size):
    return ImageFont.truetype(path, size)


def gradient(w, h, top, bottom, horizontal=False):
    img = Image.new("RGBA", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            t = (x / (w - 1)) if horizontal else (y / (h - 1))
            px[x, y] = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,)
    return img


def shade(c, f):
    return tuple(max(0, min(255, int(v * f))) for v in c)


def puzzle(draw, x, y, s, fill):
    """A puzzle piece of size s at (x, y): a body with a knob on top and one on the right."""
    k = s * 0.22
    body = [x, y + k, x + s - k, y + s]
    draw.rounded_rectangle(body, radius=s * 0.12, fill=fill)
    r = k * 0.85
    cx = x + (s - k) / 2
    draw.ellipse([cx - r, y + k - r * 1.35, cx + r, y + k + r * 0.65], fill=fill)
    cy = y + k + (s - k) / 2
    draw.ellipse([x + s - k - r * 0.65, cy - r, x + s - k + r * 1.35, cy + r], fill=fill)


def glyph(draw, category, x, y, s, fill):
    if category == "plugin":
        puzzle(draw, x, y, s, fill)
    elif category == "emulator":
        draw.rounded_rectangle([x, y + s * 0.25, x + s, y + s * 0.8], radius=s * 0.25, fill=fill)
    else:  # utility / homebrew: a wrench-like tile
        draw.rounded_rectangle([x + s * 0.1, y + s * 0.1, x + s * 0.9, y + s * 0.9], radius=s * 0.2, outline=fill, width=max(2, int(s * 0.12)))
        draw.rectangle([x + s * 0.35, y + s * 0.35, x + s * 0.65, y + s * 0.65], fill=fill)


def wrap(draw, text, fnt, width):
    words, lines, cur = text.split(), [], ""
    for w in words:
        test = (cur + " " + w).strip()
        if draw.textlength(test, font=fnt) <= width:
            cur = test
        else:
            if cur:
                lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines


def fit_lines(draw, text, width, sizes, max_lines):
    """Largest font size at which every word fits and the text wraps in max_lines."""
    for size in sizes:
        fnt = font(FONT_BOLD, size)
        if all(draw.textlength(w, font=fnt) <= width for w in text.split()):
            lines = wrap(draw, text, fnt, width)
            if len(lines) <= max_lines:
                return fnt, lines
    fnt = font(FONT_BOLD, sizes[-1])
    return fnt, wrap(draw, text, fnt, width)[:max_lines]


def fit_font(draw, text, path, width, sizes):
    for size in sizes:
        fnt = font(path, size)
        if draw.textlength(text, font=fnt) <= width:
            return fnt
    return font(path, sizes[-1])


def store_tile(title, subtitle, category):
    base = CATEGORY.get(category, (110, 118, 132))
    img = gradient(144, 80, shade(base, 1.0), shade(base, 0.42))
    overlay = Image.new("RGBA", (144, 80), (0, 0, 0, 0))
    od = ImageDraw.Draw(overlay)
    od.ellipse([-60, -70, 110, 36], fill=(255, 255, 255, 36))
    img = Image.alpha_composite(img, overlay)
    d = ImageDraw.Draw(img)

    glyph(d, category, 9, 21, 36, (255, 255, 255, 235))

    text_x, width = 54, 144 - 54 - 6
    ft, lines = fit_lines(d, title, width, [15, 14, 13, 12, 11, 10], 3)
    fs = fit_font(d, subtitle, FONT_REG, width, [10, 9, 8])
    lh = ft.size + 2
    total = lh * len(lines) + fs.size + 3
    y = (80 - total) / 2
    for ln in lines:
        d.text((text_x, y), ln, font=ft, fill=(255, 255, 255, 255))
        y += lh
    d.text((text_x, y + 1), subtitle, font=fs, fill=(255, 255, 255, 190))
    return img.convert("RGB")


def app_icon():
    img = gradient(144, 80, (34, 52, 96), (10, 14, 28))
    over = Image.new("RGBA", (144, 80), (0, 0, 0, 0))
    od = ImageDraw.Draw(over)
    od.ellipse([-50, -80, 130, 40], fill=(90, 150, 255, 45))
    img = Image.alpha_composite(img, over)
    d = ImageDraw.Draw(img)
    # a 2x2 grid of store tiles, one of them a puzzle piece
    colors = [(138, 92, 232), (28, 168, 158), (222, 84, 84), (58, 128, 228)]
    for i, c in enumerate(colors):
        x = 8 + (i % 2) * 26
        y = 13 + (i // 2) * 28
        d.rounded_rectangle([x, y, x + 22, y + 24], radius=5, fill=c + (255,))
    puzzle(d, 11, 15, 17, (255, 255, 255, 240))
    ft = fit_font(d, "Manager", FONT_BOLD, 144 - 66 - 5, [17, 16, 15, 14])
    d.text((66, 17), "Plugin", font=ft, fill=(255, 255, 255, 255))
    d.text((66, 17 + ft.size + 3), "Manager", font=ft, fill=(255, 255, 255, 255))
    d.text((67, 60), "for ARK-5", font=font(FONT_REG, 10), fill=(170, 200, 255, 255))
    return img.convert("RGBA")


def pic1():
    img = gradient(480, 272, (22, 30, 52), (6, 8, 16))
    over = Image.new("RGBA", (480, 272), (0, 0, 0, 0))
    od = ImageDraw.Draw(over)
    for w in range(3):
        pts = []
        for i in range(0, 481, 8):
            y = 170 + w * 22 + math.sin(i * 0.012 + w * 1.7) * (18 - w * 5)
            pts.append((i, y))
        od.polygon(pts + [(480, 272), (0, 272)], fill=(90, 150, 255, 16 + w * 8))
        od.line(pts, fill=(170, 210, 255, 50 + w * 12), width=1)
    # faint tile grid on the right
    for r in range(3):
        for c in range(4):
            x, y = 250 + c * 52, 30 + r * 34
            od.rounded_rectangle([x, y, x + 44, y + 26], radius=4, fill=(255, 255, 255, 10 + ((r + c) % 3) * 6))
    img = Image.alpha_composite(img, over.filter(ImageFilter.GaussianBlur(0.6)))
    return img.convert("RGB")


def pbp_icon(path):
    with open(path, "rb") as f:
        data = f.read()
    magic, _ver, *offsets = struct.unpack("<4sI8I", data[:40])
    if magic != b"\x00PBP":
        raise ValueError(path + " is not a PBP")
    start, end = offsets[1], offsets[2]
    png = data[start:end]
    if not png.startswith(b"\x89PNG"):
        raise ValueError(path + " has no ICON0.PNG")
    return png


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pbp", action="append", default=[], help="id=path/to/EBOOT.PBP")
    args = ap.parse_args()

    os.makedirs(os.path.join(ROOT, "res"), exist_ok=True)
    icons = os.path.join(ROOT, "store", "icons")
    os.makedirs(icons, exist_ok=True)

    icon = app_icon()
    icon.save(os.path.join(ROOT, "res", "ICON0.PNG"), optimize=True)
    icon.convert("RGB").save(os.path.join(icons, "pluginmanager.png"), optimize=True)
    pic1().save(os.path.join(ROOT, "res", "PIC1.PNG"), optimize=True)

    for ident, title, subtitle, cat in TILES:
        store_tile(title, subtitle, cat).save(os.path.join(icons, ident + ".png"), optimize=True)

    for spec in args.pbp:
        ident, path = spec.split("=", 1)
        png = pbp_icon(path)
        img = Image.open(__import__("io").BytesIO(png)).convert("RGBA")
        if img.size != (144, 80):
            img = img.resize((144, 80), Image.LANCZOS)
        img.save(os.path.join(icons, ident + ".png"), optimize=True)

    print("artwork written to", ROOT)


if __name__ == "__main__":
    main()
