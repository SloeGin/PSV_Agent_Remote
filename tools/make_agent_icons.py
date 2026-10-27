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
COLORS = {"claude": (0xD9, 0xA1, 0x80, 255), "codex": (0x10, 0xA3, 0x7F, 255)}


def render(agent):
    image = Image.new("RGBA", (SIZE * SCALE, SIZE * SCALE))
    draw = ImageDraw.Draw(image)
    if agent == "claude":
        path = [(12, 3), (12, 9)]
        rotations = 12
    else:
        # M12 3.5 C16 1.5 20 5 18 9 L12 12 L8 9.7 V5.3 Z
        path = []
        for i in range(25):
            t = i / 24
            u = 1 - t
            path.append((u**3*12 + 3*u*u*t*16 + 3*u*t*t*20 + t**3*18,
                         u**3*3.5 + 3*u*u*t*1.5 + 3*u*t*t*5 + t**3*9))
        path += [(12, 12), (8, 9.7), (8, 5.3), path[0]]
        rotations = 6
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
