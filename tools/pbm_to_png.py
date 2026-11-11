#!/usr/bin/env python3
"""Convert host-test PBM previews to PNG when Pillow is available."""
from __future__ import annotations

import sys
from pathlib import Path


def make_contact_sheet(directory: Path, Image) -> None:
    names = [
        ("splash.png", "Splash"),
        ("sleep.png", "Sleep"),
        ("menu_home.png", "Home"),
        ("menu_books.png", "Books"),
        ("menu_settings.png", "Settings"),
        ("playing.png", "Playing"),
        ("paused.png", "Paused"),
        ("finished.png", "Finished"),
    ]
    tiles = []
    for name, label in names:
        path = directory / name
        if path.exists():
            tiles.append((Image.open(path).convert("L"), label))
    if not tiles:
        return
    scale = 2
    tile_w, tile_h = 200 * scale, 200 * scale
    label_h = 22
    gap = 16
    cols, rows = 4, 2
    width = gap + cols * (tile_w + gap)
    height = gap + rows * (label_h + tile_h + gap)
    sheet = Image.new("L", (width, height), 240)
    try:
        from PIL import ImageDraw, ImageFont
        draw = ImageDraw.Draw(sheet)
        font = ImageFont.load_default()
    except Exception:
        draw = None
        font = None
    for i, (im, label) in enumerate(tiles):
        r, c = divmod(i, cols)
        x = gap + c * (tile_w + gap)
        y = gap + r * (label_h + tile_h + gap)
        if draw is not None:
            draw.text((x, y), label, fill=0, font=font)
        scaled = im.resize((tile_w, tile_h), Image.Resampling.NEAREST)
        sheet.paste(scaled, (x, y + label_h))
    out = directory / "ui_contact_sheet.png"
    sheet.save(out)
    print(f"wrote {out}")


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: pbm_to_png.py DIR", file=sys.stderr)
        return 2
    try:
        from PIL import Image
    except ImportError:
        return 0
    directory = Path(sys.argv[1])
    for pbm in sorted(directory.glob("*.pbm")):
        image = Image.open(pbm)
        png = pbm.with_suffix(".png")
        image.convert("1").save(png)
        print(f"wrote {png}")
    make_contact_sheet(directory, Image)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
