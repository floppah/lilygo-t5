#!/usr/bin/env python3
"""Build mono_icons.h from the original SVG artwork.

The current-conditions glyphs sit beside Ubuntu32 text (24 px digits); the
forecast glyphs sit beside Ubuntu20 text (16 px digits).  The forecast
thermometer is rendered at 20 px because its tube detail does not survive
rasterising any smaller.  Icons are emitted as 1-bit bitmaps: MSB first,
row-major from the top, 1 = background and 0 = black.

Usage: generate_mono_icons.py [--out FILE]
"""

from pathlib import Path
import argparse
import re

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "unused"
OUTPUT = ROOT / "mono_icons.h"

THERMOMETER_SVG = ASSETS / "thermometer-icon.svg"
RAINDROP_SVG = ASSETS / "raindrop-weather-svgrepo-com.svg"
CURRENT_ICON_HEIGHT = 24            # Ubuntu32 digit height
FORECAST_THERMOMETER_HEIGHT = 20    # smallest height that keeps the tube readable
FORECAST_RAINDROP_HEIGHT = 16       # Ubuntu20 digit height
SUPERSAMPLE = 8
BLACK_THRESHOLD = 0.4   # bias towards ink so the 1-px outlines stay continuous

TOKEN = re.compile(r"[MmLlCcZzHhVvSsQqTtAa]|-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")
VIEWBOX = re.compile(r'viewBox="([-\d.]+) ([-\d.]+) ([\d.]+) ([\d.]+)"')
PATH = re.compile(r'<path d="([^"]+)"')


def flatten_path(d: str, scale: float) -> list[list[tuple[float, float]]]:
    """Flatten an SVG path (M/L/H/V/C/S/Z, absolute and relative) to polygons."""
    tokens = TOKEN.findall(d)
    polygons = []
    current = []
    x = y = start_x = start_y = 0.0
    command = None
    last_control = None
    i = 0

    def number() -> float:
        nonlocal i
        value = float(tokens[i])
        i += 1
        return value

    while i < len(tokens):
        token = tokens[i]
        if token.isalpha():
            command = token
            i += 1
            if command not in "CcSs":
                last_control = None
            if command in "zZ":
                if len(current) > 1:
                    polygons.append(current)
                current = []
                x, y = start_x, start_y
                continue
            if command in "Mm":
                if len(current) > 1:
                    polygons.append(current)
                current = []
                if command == "M":
                    x, y = number(), number()
                else:
                    x, y = x + number(), y + number()
                start_x, start_y = x, y
                command = "L" if command == "M" else "l"
                current.append((x * scale, y * scale))
                continue

        if command in "CcSs":
            x0, y0 = x, y
            if command == "C":
                x1, y1, x2, y2, x, y = (number() for _ in range(6))
            elif command == "c":
                dx1, dy1, dx2, dy2, dx, dy = (number() for _ in range(6))
                x1, y1 = x0 + dx1, y0 + dy1
                x2, y2 = x0 + dx2, y0 + dy2
                x, y = x0 + dx, y0 + dy
            else:
                if command == "S":
                    x2, y2, x, y = (number() for _ in range(4))
                else:
                    dx2, dy2, dx, dy = (number() for _ in range(4))
                    x2, y2 = x0 + dx2, y0 + dy2
                    x, y = x0 + dx, y0 + dy
                if last_control is None:
                    x1, y1 = x0, y0
                else:
                    x1, y1 = 2 * x0 - last_control[0], 2 * y0 - last_control[1]
            last_control = (x2, y2)
            for step in range(1, 17):
                t = step / 16
                m = 1 - t
                bx = (m**3 * x0 + 3 * m * m * t * x1 +
                      3 * m * t * t * x2 + t**3 * x)
                by = (m**3 * y0 + 3 * m * m * t * y1 +
                      3 * m * t * t * y2 + t**3 * y)
                current.append((bx * scale, by * scale))
            continue

        if command in "LlHhVv":
            if command == "L":
                x, y = number(), number()
            elif command == "l":
                x, y = x + number(), y + number()
            elif command == "H":
                x = number()
            elif command == "h":
                x = x + number()
            elif command == "V":
                y = number()
            else:
                y = y + number()
            current.append((x * scale, y * scale))
            continue

        raise ValueError(f"unsupported SVG command {command!r}")

    if len(current) > 1:
        polygons.append(current)
    return polygons


def inside(px: float, py: float, polygons) -> bool:
    """Even-odd point-in-polygon test (correct for this flat, non-overlapping art)."""
    hit = False
    for polygon in polygons:
        count = len(polygon)
        for j in range(count):
            x1, y1 = polygon[j]
            x2, y2 = polygon[(j + 1) % count]
            if (y1 > py) != (y2 > py):
                crossing = x1 + (py - y1) * (x2 - x1) / (y2 - y1)
                if crossing > px:
                    hit = not hit
    return hit


def render_svg(path: Path, height: int, path_limit: int | None = None):
    """Rasterise an SVG's viewBox at `height` px tall, 1 bpp.

    The canvas width is rounded up to a whole number of bytes (the mono icon
    format addresses rows in bytes); the artwork is centred in it.
    """
    source = path.read_text()
    match = VIEWBOX.search(source)
    if not match:
        raise ValueError(f"{path}: no viewBox")
    _, _, view_width, view_height = (float(v) for v in match.groups())
    scale = height / view_height
    art_width = round(view_width * scale)
    width = (art_width + 7) // 8 * 8
    left = (width - art_width) // 2

    paths = PATH.findall(source)
    if path_limit is not None:
        paths = paths[:path_limit]
    polygons = []
    for d in paths:
        polygons.extend(flatten_path(d, scale))

    black_at = BLACK_THRESHOLD * SUPERSAMPLE * SUPERSAMPLE
    rows = []
    for y in range(height):
        row = []
        for x in range(width):
            art_x = x - left
            if art_x < 0 or art_x >= art_width:
                row.append(1)
                continue
            hits = 0
            for sy in range(SUPERSAMPLE):
                for sx in range(SUPERSAMPLE):
                    if inside(art_x + (sx + 0.5) / SUPERSAMPLE,
                              y + (sy + 0.5) / SUPERSAMPLE, polygons):
                        hits += 1
            row.append(0 if hits >= black_at else 1)
        rows.append(row)
    return width, height, rows


def pack_rows(rows: list[list[int]]) -> bytes:
    out = bytearray()
    for row in rows:
        acc = 0
        bits = 0
        for value in row:
            acc = (acc << 1) | value
            bits += 1
            if bits == 8:
                out.append(acc)
                acc = 0
                bits = 0
        if bits:
            out.append(acc << (8 - bits))
    return bytes(out)


def emit_icon(lines, c_ident, title, width, height, rows) -> None:
    pixels = pack_rows(rows)
    lines.append(f"/* {title} */")
    lines.append(f"#define {c_ident.upper()}_ICON_WIDTH {width}")
    lines.append(f"#define {c_ident.upper()}_ICON_HEIGHT {height}")
    lines.append("")
    lines.append("/* 1 bpp, MSB first, row-major from the top; 1 = background, 0 = black. */")
    lines.append(
        f"static const uint8_t {c_ident}_icon_data[{width} * {height} / 8] = {{"
    )
    for i in range(0, len(pixels), 8):
        chunk = ", ".join(f"0x{b:02X}" for b in pixels[i : i + 8])
        lines.append("    " + chunk + ",")
    lines.append("};")
    lines.append("")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=OUTPUT,
                        help="header file to write (default: %(default)s)")
    args = parser.parse_args()

    therm_w, therm_h, therm_rows = render_svg(THERMOMETER_SVG,
                                              CURRENT_ICON_HEIGHT)
    # The raindrop's first path is the drop outline; the two highlight strokes
    # that follow collapse to single pixels at these sizes, so they are left out.
    drop_w, drop_h, drop_rows = render_svg(RAINDROP_SVG, CURRENT_ICON_HEIGHT, 1)
    small_therm = render_svg(THERMOMETER_SVG, FORECAST_THERMOMETER_HEIGHT)
    small_drop = render_svg(RAINDROP_SVG, FORECAST_RAINDROP_HEIGHT, 1)

    lines = [
        "/* Generated by tools/generate_mono_icons.py -- do not edit by hand. */",
        "#ifndef MONO_ICONS_H",
        "#define MONO_ICONS_H",
        "",
        "#include <stdint.h>",
        "",
    ]
    emit_icon(lines, "thermometer",
              f"thermometer beside the temperature ({THERMOMETER_SVG.name}, "
              f"{CURRENT_ICON_HEIGHT}px)", therm_w, therm_h, therm_rows)
    emit_icon(lines, "raindrop",
              f"raindrop beside the precipitation chance ({RAINDROP_SVG.name}, "
              f"{CURRENT_ICON_HEIGHT}px)", drop_w, drop_h, drop_rows)
    emit_icon(lines, "thermometer_small",
              f"thermometer beside forecast temperatures ({THERMOMETER_SVG.name}, "
              f"{FORECAST_THERMOMETER_HEIGHT}px)", *small_therm)
    emit_icon(lines, "raindrop_small",
              f"raindrop beside forecast precipitation chances ({RAINDROP_SVG.name}, "
              f"{FORECAST_RAINDROP_HEIGHT}px)", *small_drop)
    lines.append("#endif /* MONO_ICONS_H */")
    lines.append("")

    args.out.write_text("\n".join(lines))
    print(f"Wrote {args.out}: "
          f"thermometer {therm_w}x{therm_h}, raindrop {drop_w}x{drop_h}, "
          f"thermometer_small {small_therm[0]}x{small_therm[1]}, "
          f"raindrop_small {small_drop[0]}x{small_drop[1]}")


if __name__ == "__main__":
    main()
