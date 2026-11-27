# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# The scoreboard's team emblems (cs::kScoreboardEmblems). Needs Pillow.
#
#   python scripts/makeScoreboardEmblems.py
#       Draws the project's own, white over clear, into data/cstrike.
#   python scripts/makeScoreboardEmblems.py --ct <picture> --t <picture> [--out <folder>]
#       Turns pictures of the player's own (PNG, JPG, WebP...) into the emblems, into <folder>/cstrike:
#       by default the player's folder (%APPDATA%/Modern-Client.v2), which the game reads before data/
#       and which is not part of the repository. Either may be left out.

import argparse
import math
import os
import sys
from pathlib import Path
from PIL import Image, ImageDraw
S = 1024
OUT = 128
W = (255, 255, 255, 255)
C = (0, 0, 0, 0)

def star(cx, cy, r1, r2, n=5, rot=-90):
    pts = []
    for i in range(n * 2):
        r = r1 if i % 2 == 0 else r2
        a = math.radians(rot + i * 180 / n)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts

def shield(cx, top, w, h):
    # a heraldic shield: a flat top, straight sides, curving down to a point
    hw = w / 2
    side = top + h * 0.45
    bottom = top + h
    def bez(p0, p1, p2, n=40):
        return [((1-t)**2*p0[0]+2*(1-t)*t*p1[0]+t*t*p2[0], (1-t)**2*p0[1]+2*(1-t)*t*p1[1]+t*t*p2[1])
                for t in (i/n for i in range(n+1))]
    pts = [(cx - hw, top), (cx + hw, top), (cx + hw, side)]
    pts += bez((cx + hw, side), (cx + hw, bottom - h * 0.18), (cx, bottom))[1:]
    pts += bez((cx, bottom), (cx - hw, bottom - h * 0.18), (cx - hw, side))[1:]
    return pts

def scaled(pts, cx, cy, k):
    return [(cx + (x - cx) * k, cy + (y - cy) * k) for x, y in pts]

def ct():
    im = Image.new("RGBA", (S, S), C)
    d = ImageDraw.Draw(im)
    outer = shield(S / 2, 70, 740, 880)
    d.polygon(outer, fill=W)
    d.polygon(scaled(outer, S / 2, 470, 0.87), fill=C)
    d.polygon(scaled(outer, S / 2, 470, 0.78), fill=W)
    d.polygon(star(S / 2, 450, 240, 96), fill=C)
    return im

def t():
    im = Image.new("RGBA", (S, S), C)
    d = ImageDraw.Draw(im)
    # three charges side by side
    left, top, bottom, gap, wide = 190, 330, 900, 26, 196
    for i in range(3):
        x = left + i * (wide + gap)
        d.rounded_rectangle((x, top, x + wide, bottom), radius=70, fill=W)
    # the tape binding them
    for y in (430, 760):
        d.rectangle((left - 30, y, left + 3 * wide + 2 * gap + 30, y + 70), fill=W)
        d.rectangle((left - 30 + 18, y + 18, left + 3 * wide + 2 * gap + 30 - 18, y + 52), fill=C)
    # the timer on top, its display cut out
    d.rounded_rectangle((300, 120, 724, 340), radius=36, fill=W)
    d.rounded_rectangle((340, 160, 684, 260), radius=14, fill=C)
    for i in range(4):
        x = 362 + i * 82
        d.rectangle((x, 182, x + 52, 238), fill=W)
    # the wires from the timer to the charges
    d.line([(330, 300), (230, 330), (240, 410)], fill=W, width=26, joint="curve")
    d.line([(694, 300), (790, 330), (780, 410)], fill=W, width=26, joint="curve")
    return im

NAMES = {"ct": "scoreboardCounterTerrorists", "t": "scoreboardTerrorists"}
LARGEST = 256  # the longer side of a player's picture, at most


def own(picture):
    """A player's picture as an emblem: RGBA, its longer side at most LARGEST, its aspect kept."""
    image = Image.open(picture).convert("RGBA")
    image.thumbnail((LARGEST, LARGEST), Image.LANCZOS)
    return image


def main():
    parser = argparse.ArgumentParser(description="The scoreboard's team emblems.")
    parser.add_argument("--ct", help="a picture for the counter-terrorists")
    parser.add_argument("--t", help="a picture for the terrorists")
    parser.add_argument("--out", help="the folder whose cstrike/ gets them (default: the player's folder)")
    args = parser.parse_args()
    if not args.ct and not args.t:
        folder = Path(args.out) if args.out else Path(__file__).resolve().parent.parent / "data" / "cstrike"
        folder.mkdir(parents=True, exist_ok=True)
        for name, image in ((NAMES["ct"], ct()), (NAMES["t"], t())):
            image.resize((OUT, OUT), Image.LANCZOS).save(folder / f"{name}.tga", rle=False)
            print(folder / f"{name}.tga")
        return
    base = Path(args.out) if args.out else Path(os.environ["APPDATA"]) / "Modern-Client.v2"
    folder = base / "cstrike"
    folder.mkdir(parents=True, exist_ok=True)
    for key, picture in (("ct", args.ct), ("t", args.t)):
        if picture:
            target = folder / f"{NAMES[key]}.tga"
            own(picture).save(target, rle=False)
            print(target)


if __name__ == "__main__":
    sys.exit(main())
