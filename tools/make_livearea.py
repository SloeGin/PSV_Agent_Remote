"""Draw the Vita icon and LiveArea images (8-bit palette PNGs, as LiveArea expects).

    pip install pillow
    python tools/make_livearea.py vita/sce_sys vita/assets [vscode-extension/media/icon.png]

The app icon is a handheld with a chat bubble on its screen: the console talking
to an agent. The optional third path also writes it as the VS Code extension icon.

Style matches docs/mockup/branding.html: independent #DDD23B app identity.
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

BG = (0x15, 0x17, 0x0F)
BAR = (0x20, 0x24, 0x17)
LINE = (0x4B, 0x54, 0x30)
BRAND = (0xDD, 0xD2, 0x3B)
GRID = (0x34, 0x3B, 0x22)
TEXT = (0xC6, 0xC9, 0xAA)
MUTED = (0x96, 0x9B, 0x77)
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
        d.line([(cx + i * 6, top), (cx + i * 90, h)], fill=GRID, width=1)
    y, gap = float(top), 3.0
    while y < h:
        d.line([(0, int(y)), (w, int(y))], fill=GRID, width=1)
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
        d.text((x, y), ch, font=font, fill=fill, anchor="ls")
        x += d.textlength(ch, font=font) + spacing
    return int(x - spacing)


def centered(d, y, text, font, fill, spacing, width):
    length = sum(d.textlength(ch, font=font) for ch in text) + spacing * (len(text) - 1)
    return spaced(d, ((width - length) / 2, y), text, font, fill, spacing)


def save(img: Image.Image, path: Path) -> None:
    img.quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(path, optimize=True)


def handheld(size: int, rounded: bool) -> Image.Image:
    """The app icon on a 128-unit grid, drawn 4x and scaled down for smooth edges.

    rounded=False fills the whole square (the Vita masks icons to a circle; the
    design stays inside it); rounded=True gives transparent rounded corners.
    """
    scale = 4
    k = size * scale / 128

    def box(x0, y0, x1, y1):
        return [x0 * k, y0 * k, x1 * k, y1 * k]

    img = Image.new("RGBA", (size * scale, size * scale), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if rounded:
        d.rounded_rectangle(box(0, 0, 128, 128), radius=22 * k, fill=BG)
    else:
        d.rectangle(box(0, 0, 128, 128), fill=BG)
    d.rounded_rectangle(box(14, 36, 114, 92), radius=28 * k, outline=BRAND, width=round(5 * k))  # body
    d.rounded_rectangle(box(38, 46, 90, 82), radius=6 * k, fill=BAR, outline=LINE, width=round(2 * k))  # screen
    d.rounded_rectangle(box(46, 56, 82, 72), radius=4 * k, fill=BRAND)  # chat bubble
    d.polygon([(53 * k, 71 * k), (60 * k, 71 * k), (53 * k, 77 * k)], fill=BRAND)  # its tail
    d.ellipse(box(22, 60, 30, 68), fill=LINE)  # stick
    d.ellipse(box(98, 60, 106, 68), fill=BRAND)  # button
    return img.resize((size, size), Image.Resampling.LANCZOS)


def icon(fonts: Path) -> Image.Image:
    img = handheld(128, rounded=False).convert("RGB")
    scanlines(img)
    return img


def background(fonts: Path) -> Image.Image:
    w, h = 840, 500
    img = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(img)
    horizon = 330
    # Striped half sun sitting on the horizon, then the grid in front of it.
    d.pieslice([w / 2 - 95, horizon - 95, w / 2 + 95, horizon + 95], 180, 360, fill=(0x48, 0x4E, 0x24))
    for i, y in enumerate(range(horizon - 40, horizon, 9)):
        d.rectangle([0, y, w, y + 1 + i], fill=BG)
    d.rectangle([0, horizon, w, h], fill=BG)
    horizon_grid(d, w, h, horizon)
    title = ImageFont.truetype(str(fonts / "mono-medium.ttf"), 44)
    sub = ImageFont.truetype(str(fonts / "sans.ttf"), 20)
    centered(d, 112, "AGENT//REMOTE", title, BRAND, 5, w)
    tag = "Approve your coding agents from the couch."
    d.text((w / 2, 157), tag, font=sub, fill=TEXT, anchor="ms")
    glyphs(d, int(w / 2 - 2 * 26 - 1.5 * 14), 180, 26, 14, 3)
    scanlines(img)
    return img


def startup(fonts: Path) -> Image.Image:
    w, h = 280, 158
    img = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([8, 8, w - 9, h - 9], radius=10, fill=BAR, outline=LINE, width=2)
    title = ImageFont.truetype(str(fonts / "mono-medium.ttf"), 20)
    centered(d, 61, "AGENT//REMOTE", title, BRAND, 1, w)
    glyphs(d, 87, 84, 18, 11, 2)
    small = ImageFont.truetype(str(fonts / "sans.ttf"), 11)
    centered(d, 130, "START", small, MUTED, 2, w)
    scanlines(img)
    return img


def main(sce_sys: str, fonts_dir: str, vscode_icon: str | None = None) -> None:
    out, fonts = Path(sce_sys), Path(fonts_dir)
    (out / "livearea/contents").mkdir(parents=True, exist_ok=True)
    save(icon(fonts), out / "icon0.png")
    save(background(fonts), out / "livearea/contents/bg.png")
    save(startup(fonts), out / "livearea/contents/startup.png")
    if vscode_icon:
        path = Path(vscode_icon)
        path.parent.mkdir(parents=True, exist_ok=True)
        handheld(256, rounded=True).save(path, optimize=True)  # full colour with alpha


if __name__ == "__main__":
    main(*sys.argv[1:4])
