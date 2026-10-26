"""Draw the Vita icon and LiveArea images (8-bit palette PNGs, as LiveArea expects).

    pip install pillow
    python tools/make_livearea.py vita/sce_sys vita/assets

Style matches docs/mockup/index.html: warm dark base, amber + teal phosphor, mono type.
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

BG = (0x17, 0x12, 0x0F)
BAR = (0x21, 0x1A, 0x15)
LINE = (0x3A, 0x2F, 0x27)
AMBER = (0xFF, 0xB3, 0x5C)
TEAL = (0x5F, 0xC9, 0xB8)
TEXT = (0xF1, 0xE6, 0xD6)
MUTED = (0x9C, 0x8F, 0x80)
CROSS, CIRCLE, TRIANGLE, SQUARE = (0x7A, 0xA7, 0xFF), (0xFF, 0x6B, 0x6B), (0x3F, 0xD0, 0xA6), (0xE4, 0x8B, 0xD2)


def scanlines(img: Image.Image, step: int = 3, strength: int = 6) -> None:
    px = img.load()
    for y in range(0, img.height, step):
        for x in range(img.width):
            r, g, b = px[x, y]
            px[x, y] = (min(255, r + strength), min(255, g + strength), min(255, b + strength))


def horizon_grid(d: ImageDraw.ImageDraw, w: int, h: int, top: int) -> None:
    """Retro perspective grid fading towards a horizon at `top`."""
    cx = w / 2
    for i in range(-14, 15):
        d.line([(cx + i * 6, top), (cx + i * 90, h)], fill=(0x2A, 0x22, 0x1C), width=1)
    y, gap = float(top), 3.0
    while y < h:
        d.line([(0, int(y)), (w, int(y))], fill=(0x2A, 0x22, 0x1C), width=1)
        y += gap
        gap *= 1.35


def glyphs(d: ImageDraw.ImageDraw, x: int, y: int, size: int, gap: int, width: int) -> None:
    s = size
    d.polygon([(x + s / 2, y + 2), (x + s - 2, y + s - 3), (x + 2, y + s - 3)], outline=TRIANGLE, width=width)
    x += s + gap
    d.ellipse([x + 2, y + 2, x + s - 2, y + s - 2], outline=CIRCLE, width=width)
    x += s + gap
    d.line([(x + 3, y + 3), (x + s - 3, y + s - 3)], fill=CROSS, width=width)
    d.line([(x + s - 3, y + 3), (x + 3, y + s - 3)], fill=CROSS, width=width)
    x += s + gap
    d.rectangle([x + 3, y + 3, x + s - 3, y + s - 3], outline=SQUARE, width=width)


def spaced(d: ImageDraw.ImageDraw, xy, text: str, font, fill, spacing: int) -> int:
    x, y = xy
    for ch in text:
        d.text((x, y), ch, font=font, fill=fill)
        x += d.textlength(ch, font=font) + spacing
    return int(x - spacing)


def save(img: Image.Image, path: Path) -> None:
    img.quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(path, optimize=True)


def icon(fonts: Path) -> Image.Image:
    img = Image.new("RGB", (128, 128), BG)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([10, 22, 117, 105], radius=12, fill=BAR, outline=LINE, width=2)
    mono = ImageFont.truetype(str(fonts / "mono-medium.ttf"), 34)
    d.text((22, 40), ">", font=mono, fill=TEAL)
    d.text((48, 40), "_", font=mono, fill=AMBER)
    d.ellipse([96, 30, 104, 38], fill=AMBER)
    small = ImageFont.truetype(str(fonts / "mono-medium.ttf"), 11)
    spaced(d, (22, 86), "C//R", small, MUTED, 3)
    scanlines(img)
    return img


def background(fonts: Path) -> Image.Image:
    w, h = 840, 500
    img = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(img)
    horizon = 330
    # Striped half sun sitting on the horizon, then the grid in front of it.
    d.pieslice([w / 2 - 95, horizon - 95, w / 2 + 95, horizon + 95], 180, 360, fill=(0x4A, 0x2C, 0x16))
    for i, y in enumerate(range(horizon - 40, horizon, 9)):
        d.rectangle([0, y, w, y + 1 + i], fill=BG)
    d.rectangle([0, horizon, w, h], fill=BG)
    horizon_grid(d, w, h, horizon)
    title = ImageFont.truetype(str(fonts / "mono-medium.ttf"), 44)
    sub = ImageFont.truetype(str(fonts / "sans.ttf"), 20)
    width = sum(d.textlength(c, font=title) for c in "AGENT//REMOTE") + 6 * 13
    x = int((w - width) / 2)
    x = spaced(d, (x, 70), "AGENT", title, AMBER, 6) + 6
    x = spaced(d, (x, 70), "//", title, MUTED, 6) + 6
    spaced(d, (x, 70), "REMOTE", title, AMBER, 6)
    tag = "Approve your coding agents from the couch."
    d.text(((w - d.textlength(tag, font=sub)) / 2, 134), tag, font=sub, fill=TEXT)
    glyphs(d, int(w / 2 - 2 * 26 - 1.5 * 14), 178, 26, 14, 3)
    scanlines(img)
    return img


def startup(fonts: Path) -> Image.Image:
    w, h = 280, 158
    img = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([8, 8, w - 9, h - 9], radius=10, fill=BAR, outline=LINE, width=2)
    title = ImageFont.truetype(str(fonts / "mono-medium.ttf"), 20)
    x = 26
    x = spaced(d, (x, 46), "AGENT", title, AMBER, 3) + 3
    x = spaced(d, (x, 46), "//", title, MUTED, 3) + 3
    spaced(d, (x, 46), "REMOTE", title, AMBER, 3)
    glyphs(d, 26, 92, 20, 10, 2)
    scanlines(img)
    return img


def main(sce_sys: str, fonts_dir: str) -> None:
    out, fonts = Path(sce_sys), Path(fonts_dir)
    (out / "livearea/contents").mkdir(parents=True, exist_ok=True)
    save(icon(fonts), out / "icon0.png")
    save(background(fonts), out / "livearea/contents/bg.png")
    save(startup(fonts), out / "livearea/contents/startup.png")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
