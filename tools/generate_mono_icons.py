#!/usr/bin/env python3
"""Build mono_icons.h from the original SVG artwork.

The current-conditions glyphs sit beside Ubuntu32 text (24 px digits); the
forecast glyphs sit beside Ubuntu20 text (16 px digits).  The forecast
thermometer is rendered at 20 px because its tube detail does not survive
rasterising any smaller.  The no-wifi glyph is shown beside the battery
indicator and is rendered at 20 px to match the Ubuntu20 percentage text.
Icons are emitted as 1-bit bitmaps: MSB first, row-major from the top,
1 = background and 0 = black.

Usage: generate_mono_icons.py [--out FILE]
"""

from pathlib import Path
import argparse
import math
import re

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "unused"
OUTPUT = ROOT / "mono_icons.h"

THERMOMETER_SVG = ASSETS / "thermometer-icon.svg"
RAINDROP_SVG = ASSETS / "raindrop-weather-svgrepo-com.svg"
NO_WIFI_SVG = ASSETS / "no-wifi.svg"
CURRENT_ICON_HEIGHT = 24            # Ubuntu32 digit height
FORECAST_THERMOMETER_HEIGHT = 20    # smallest height that keeps the tube readable
FORECAST_RAINDROP_HEIGHT = 16       # Ubuntu20 digit height
NO_WIFI_HEIGHT = 20                 # Ubuntu20 digit height
SUPERSAMPLE = 8
BLACK_THRESHOLD = 0.4   # bias towards ink so the 1-px outlines stay continuous

TOKEN = re.compile(r"[MmLlCcZzHhVvSsQqTtAa]|-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")
VIEWBOX = re.compile(r'viewBox="([-\d.]+) ([-\d.]+) ([\d.]+) ([\d.]+)"')
CIRCLE_ATTR = re.compile(r'\b(cx|cy|r)="([-\d.]+)"')
ELEMENT = re.compile(r'<path\b[^>]*\bd="([^"]+)"|<circle\b[^>]*>')


def _angle(ux: float, uy: float, vx: float, vy: float) -> float:
    dot = ux * vx + uy * vy
    length = math.hypot(ux, uy) * math.hypot(vx, vy)
    if length == 0:
        return 0.0
    return math.copysign(math.acos(max(-1.0, min(1.0, dot / length))),
                         ux * vy - uy * vx)


def flatten_arc(x0: float, y0: float, rx: float, ry: float, phi: float,
                large_arc: bool, sweep: bool, x: float, y: float,
                scale: float) -> list[tuple[float, float]]:
    """Sample an SVG elliptical arc (endpoint parameterisation) as points."""
    rx, ry = abs(rx), abs(ry)
    if rx == 0 or ry == 0:
        return [(x * scale, y * scale)]

    phi_rad = math.radians(phi)
    cos_phi, sin_phi = math.cos(phi_rad), math.sin(phi_rad)
    dx, dy = (x0 - x) / 2, (y0 - y) / 2
    x1p = cos_phi * dx + sin_phi * dy
    y1p = -sin_phi * dx + cos_phi * dy

    lam = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry)
    if lam > 1:
        scale_r = math.sqrt(lam)
        rx *= scale_r
        ry *= scale_r

    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    coef = (1 if large_arc != sweep else -1) * math.sqrt(
        max(0.0, num / den) if den else 0.0)
    cxp = coef * rx * y1p / ry
    cyp = -coef * ry * x1p / rx
    cx = cos_phi * cxp - sin_phi * cyp + (x0 + x) / 2
    cy = sin_phi * cxp + cos_phi * cyp + (y0 + y) / 2

    theta1 = _angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dtheta = _angle((x1p - cxp) / rx, (y1p - cyp) / ry,
                    (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dtheta > 0:
        dtheta -= 2 * math.pi
    elif sweep and dtheta < 0:
        dtheta += 2 * math.pi

    steps = max(8, int(abs(dtheta) * 16 / math.pi))
    return [
        ((cx + rx * math.cos(theta1 + dtheta * i / steps) * cos_phi -
          ry * math.sin(theta1 + dtheta * i / steps) * sin_phi) * scale,
         (cy + rx * math.cos(theta1 + dtheta * i / steps) * sin_phi +
          ry * math.sin(theta1 + dtheta * i / steps) * cos_phi) * scale)
        for i in range(1, steps + 1)
    ]


def circle_polygon(tag: str, scale: float, segments: int = 64):
    """Approximate a <circle> element as a polygon."""
    values = {name: float(value) for name, value in CIRCLE_ATTR.findall(tag)}
    cx, cy, r = values["cx"], values["cy"], values["r"]
    return [
        ((cx + r * math.cos(2 * math.pi * i / segments)) * scale,
         (cy + r * math.sin(2 * math.pi * i / segments)) * scale)
        for i in range(segments)
    ]


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

        if command in "Aa":
            rx, ry, rotation = number(), number(), number()
            large_arc = bool(number())
            sweep = bool(number())
            if command == "A":
                x, y = number(), number()
            else:
                x, y = x + number(), y + number()
            current.extend(flatten_arc(current[-1][0] / scale,
                                       current[-1][1] / scale, rx, ry,
                                       rotation, large_arc, sweep, x, y, scale))
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

    polygons = []
    # Walk paths and circles in document order so overlapping art keeps its
    # even-odd fill semantics.
    path_count = 0
    for element in ELEMENT.finditer(source):
        d = element.group(1)
        if d is not None:
            if path_limit is not None and path_count >= path_limit:
                continue
            path_count += 1
            polygons.extend(flatten_path(d, scale))
        else:
            polygons.append(circle_polygon(element.group(0), scale))

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
    no_wifi = render_svg(NO_WIFI_SVG, NO_WIFI_HEIGHT)

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
    emit_icon(lines, "no_wifi",
              f"no-wifi indicator beside the battery ({NO_WIFI_SVG.name}, "
              f"{NO_WIFI_HEIGHT}px)", *no_wifi)
    lines.append("#endif /* MONO_ICONS_H */")
    lines.append("")

    args.out.write_text("\n".join(lines))
    print(f"Wrote {args.out}: "
          f"thermometer {therm_w}x{therm_h}, raindrop {drop_w}x{drop_h}, "
          f"thermometer_small {small_therm[0]}x{small_therm[1]}, "
          f"raindrop_small {small_drop[0]}x{small_drop[1]}, "
          f"no_wifi {no_wifi[0]}x{no_wifi[1]}")


if __name__ == "__main__":
    main()
