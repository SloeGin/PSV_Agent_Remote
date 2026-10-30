"""Rasterize the mockup's fixed Agent marks offline. Requires Pillow only to rebuild.

    python3 tools/make_agent_icons.py
    python3 tools/make_agent_icons.py --check
"""

import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw

SIZE = 24
SCALE = 8
COLORS = {"claude": (0xD9, 0xA1, 0x80, 255), "codex": (0x10, 0xA3, 0x7F, 255),
          "kiro": (0xA7, 0x8B, 0xFA, 255)}


def _stroked(draw, agent, path, rotations):
    width = int(1.5 * SCALE)
    for i in range(rotations):
        angle = i * math.tau / rotations
        c, s = math.cos(angle), math.sin(angle)
        points = [((12 + (x-12)*c - (y-12)*s)*SCALE,
                   (12 + (x-12)*s + (y-12)*c)*SCALE) for x, y in path]
        draw.line(points, fill=COLORS[agent], width=width, joint="curve")
        for x, y in points:
            r = width / 2
            draw.ellipse((x-r, y-r, x+r, y+r), fill=COLORS[agent])


def _ghost(draw, agent):
    """An original ghost silhouette: rounded dome, wavy hem, two eyes.

    Not a reproduction of any brand mascot; a generic ghost built from an arc,
    a scalloped base, and two cutout eyes, tinted with the Kiro palette.
    """
    fill = COLORS[agent]
    s = SCALE
    left, right, top = 5, 19, 4
    shoulder, hem = 13.5, 20.0
    # Body outline: rounded top (sampled arc) down the sides to a scalloped hem.
    body = []
    for i in range(33):  # top arc from left shoulder to right shoulder
        a = math.pi * (1 - i / 32)
        body.append((12 + (right - 12) * math.cos(a), shoulder - (shoulder - top) * math.sin(a)))
    body.append((right, hem))
    feet = 4  # scallops along the bottom hem
    span = (right - left) / feet
    for k in range(feet):
        x0 = right - k * span
        body.append((x0 - span / 2, hem - 1.6))  # notch up between feet
        body.append((x0 - span, hem))
    body.append((left, shoulder))
    draw.polygon([(x * s, y * s) for x, y in body], fill=fill)
    # Eyes punched out (transparent) so the silhouette reads as a ghost.
    for ex in (9.6, 14.4):
        draw.ellipse(((ex - 1.5) * s, (10.5 - 1.9) * s, (ex + 1.5) * s, (10.5 + 1.9) * s),
                     fill=(0, 0, 0, 0))


def render(agent):
    image = Image.new("RGBA", (SIZE * SCALE, SIZE * SCALE))
    draw = ImageDraw.Draw(image)
    if agent == "claude":
        _stroked(draw, agent, [(12, 3), (12, 9)], 12)
    elif agent == "codex":
        # M12 3.5 C16 1.5 20 5 18 9 L12 12 L8 9.7 V5.3 Z
        path = []
        for i in range(25):
            t = i / 24
            u = 1 - t
            path.append((u**3*12 + 3*u*u*t*16 + 3*u*t*t*20 + t**3*18,
                         u**3*3.5 + 3*u*u*t*1.5 + 3*u*t*t*5 + t**3*9))
        path += [(12, 12), (8, 9.7), (8, 5.3), path[0]]
        _stroked(draw, agent, path, 6)
    else:  # kiro: filled ghost silhouette, drawn directly at full resolution
        _ghost(draw, agent)
    return image.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Verify committed images against the generator")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "vita/assets/agents")
    args = parser.parse_args()
    if not args.check: args.output.mkdir(parents=True, exist_ok=True)
    for agent in COLORS:
        target = args.output / f"{agent}.png"
        generated = render(agent)
        if args.check:
            with Image.open(target) as image:
                assert image.mode == "RGBA" and image.size == (SIZE, SIZE), target
                assert image.tobytes() == generated.tobytes(), f"Regenerate {target}"
                assert image.getchannel("A").getextrema() == (0, 255), target
        else:
            generated.save(target, optimize=True)
        print(target.name + (": verified" if args.check else ": generated"))


if __name__ == "__main__":
    main()
