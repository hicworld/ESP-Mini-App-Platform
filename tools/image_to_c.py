"""Convert a local image into an RGB565 header embedded in a mini-app WASM.

Usage: python tools/image_to_c.py artwork.png apps/my_app/artwork.h --name artwork
Requires Pillow. The output has no runtime file dependency.
"""
import argparse
import re
from pathlib import Path
from PIL import Image


def convert(source: Path, target: Path, name: str, background: int) -> None:
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
        raise ValueError("--name must be a C identifier")
    image = Image.open(source).convert("RGBA")
    width, height = image.size
    if not 1 <= width <= 128 or not 1 <= height <= 128:
        raise ValueError("image dimensions must be 1..128 pixels on each side")
    base = Image.new("RGBA", image.size,
                     ((background >> 16) & 255, (background >> 8) & 255,
                      background & 255, 255))
    base.alpha_composite(image)
    pixels = []
    for r, g, b, _ in base.getdata():
        pixels.append((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3))
    lines = ["#pragma once", "#include <stdint.h>",
             f"#define {name.upper()}_WIDTH {width}",
             f"#define {name.upper()}_HEIGHT {height}",
             f"static const uint16_t {name}_pixels[{len(pixels)}] = {{"]
    lines += ["    " + ", ".join(f"0x{x:04x}" for x in pixels[i:i+12]) + ","
              for i in range(0, len(pixels), 12)]
    lines += ["};", ""]
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("\n".join(lines), encoding="utf-8")
    print(f"{target}: {width}x{height}, {len(pixels)*2} RGB565 bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--name", required=True)
    parser.add_argument("--background", default="0x000000", type=lambda x: int(x, 0))
    args = parser.parse_args()
    if not 0 <= args.background <= 0xFFFFFF:
        parser.error("--background must be 0x000000..0xffffff")
    convert(args.source, args.output, args.name, args.background)
