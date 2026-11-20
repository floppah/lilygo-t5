#!/usr/bin/env python3
"""Convert the 150x150 4-bit greyscale BMP weather icons into a C header.

The e-paper framebuffer stores two 4-bit pixels per byte, with the even
(left) pixel in the low nibble.  BMP files store 4-bit pixels with the
even pixel in the high nibble, rows bottom-up and padded to 4 bytes.

The mist icon (code 5) is labelled "MIST" and the drizzle icon (code 11) is
labelled "DRIZZLE" in the source artwork; the lettering is replaced with light-
grey motifs (tildes for mist, small raindrops for drizzle) so the icons read
as icons rather than captions next to the forecast labels.

Usage: generate_icons.py [--icons DIR] [--out FILE]
"""

from pathlib import Path
import argparse
import math
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
ICON_DIR = ROOT / "icons"
OUTPUT = Path(__file__).resolve().parents[1] / "weather_icons.h"

ICON_SIZE = 150
ROW_BYTES = ((ICON_SIZE * 4 + 31) // 32) * 4  # 76 for 150 px wide 4bpp


def c_name(code: int) -> str:
    return "icon_m1" if code < 0 else f"icon_{code}"


def load_icon(path: Path) -> bytes:
    data = path.read_bytes()
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if (width, height, bpp) != (ICON_SIZE, ICON_SIZE, 4):
        raise ValueError(f"unexpected icon format: {path}: {width}x{height}x{bpp}")

    out = bytearray(ICON_SIZE * ICON_SIZE // 2)
    for y in range(ICON_SIZE):
        src = pixel_offset + (ICON_SIZE - 1 - y) * ROW_BYTES
        for x in range(ICON_SIZE):
            byte = data[src + x // 2]
            nibble = (byte >> 4) if x % 2 == 0 else (byte & 0x0F)
            idx = y * (ICON_SIZE // 2) + x // 2
            if x % 2 == 0:
                out[idx] = (out[idx] & 0xF0) | nibble
            else:
                out[idx] = (out[idx] & 0x0F) | (nibble << 4)
    return bytes(out)


# --- mist icon text replacement -------------------------------------------
# The 5.bmp artwork labels the mist icon with the word "MIST" under the cloud.
MIST_CODE = 5
MIST_GREY = 10                             # light grey (0 black .. 15 white)
MIST_CLEAR = (104, 134, 32, 113)           # y0, y1, x0, x1 around the word
MIST_ROWS = (112, 122)                     # top pixel row of each tilde line
MIST_TILDE_W = 16
MIST_TILDE_GAP = 4
MIST_TILDE_COUNT = 3
MIST_CENTER_X = 73                         # the word spans x 43..102


def get_pixel(pixels: bytearray, x: int, y: int) -> int:
    byte = pixels[y * (ICON_SIZE // 2) + x // 2]
    return (byte & 0x0F) if x % 2 == 0 else (byte >> 4)


def set_pixel(pixels: bytearray, x: int, y: int, value: int) -> None:
    idx = y * (ICON_SIZE // 2) + x // 2
    if x % 2 == 0:
        pixels[idx] = (pixels[idx] & 0xF0) | value
    else:
        pixels[idx] = (pixels[idx] & 0x0F) | (value << 4)


def clear_rect(pixels: bytearray, rect: tuple[int, int, int, int]) -> None:
    y0, y1, x0, x1 = rect
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            set_pixel(pixels, x, y, 15)


def draw_tilde(pixels: bytearray, x0: int, y0: int) -> None:
    """One antialiased tilde, MIST_TILDE_W wide and 7 rows tall, at (x0, y0)."""
    for dx in range(MIST_TILDE_W):
        phase = 2.0 * math.pi * (dx + 0.5) / MIST_TILDE_W
        curve_y = y0 + 3.2 - 2.4 * math.sin(phase)
        for py in range(y0, y0 + 7):
            distance = abs(py + 0.5 - curve_y)
            cover = min(1.0, max(0.0, 1.15 - distance))
            if cover <= 0.0:
                continue
            level = int(round(15.0 - cover * (15 - MIST_GREY)))
            if level < get_pixel(pixels, x0 + dx, py):
                set_pixel(pixels, x0 + dx, py, level)


def replace_mist_text(pixels: bytearray) -> None:
    clear_rect(pixels, MIST_CLEAR)

    span = MIST_TILDE_COUNT * MIST_TILDE_W + (MIST_TILDE_COUNT - 1) * MIST_TILDE_GAP
    left = MIST_CENTER_X - span // 2
    for row in MIST_ROWS:
        for tilde in range(MIST_TILDE_COUNT):
            draw_tilde(pixels, left + tilde * (MIST_TILDE_W + MIST_TILDE_GAP), row)


# --- drizzle icon text replacement ----------------------------------------
# The 11.bmp artwork labels the drizzle icon with the word "DRIZZLE" under the
# cloud, which is why the sketch used to substitute the light-rain icon (12).
DRIZZLE_CODE = 11
DRIZZLE_GREY = 10                          # light grey (0 black .. 15 white)
DRIZZLE_CLEAR = (104, 134, 18, 128)        # y0, y1, x0, x1 around the word
DRIZZLE_ROWS = (110, 124)                  # top pixel row of each drop line
DRIZZLE_W = 10
DRIZZLE_H = 14
DRIZZLE_GAP = 9
DRIZZLE_COUNT = 3
DRIZZLE_CENTER_X = 73                      # the word spans x 22..124
DRIZZLE_SUPERSAMPLE = 4


def inside_drop(x: float, y: float, w: float, h: float) -> bool:
    """Teardrop point test for a drop whose top-left corner is at (0, 0)."""
    radius = w / 2.0
    center_y = h - radius
    dx = x - radius
    if y < 0 or y > h:
        return False
    if y >= center_y:
        return dx * dx + (y - center_y) * (y - center_y) <= radius * radius
    if y <= 0:
        return False
    # Concave taper from the tip (y = 0) to the widest point (y = center_y).
    return abs(dx) <= radius * (y / center_y) ** 1.45


def draw_drop(pixels: bytearray, x0: int, y0: int, w: int, h: int) -> None:
    n = DRIZZLE_SUPERSAMPLE
    for dy in range(h):
        for dx in range(w):
            hits = 0
            for sy in range(n):
                for sx in range(n):
                    if inside_drop(dx + (sx + 0.5) / n, dy + (sy + 0.5) / n, w, h):
                        hits += 1
            if not hits:
                continue
            cover = hits / (n * n)
            level = int(round(15.0 - cover * (15 - DRIZZLE_GREY)))
            if level < get_pixel(pixels, x0 + dx, y0 + dy):
                set_pixel(pixels, x0 + dx, y0 + dy, level)


def replace_drizzle_text(pixels: bytearray) -> None:
    clear_rect(pixels, DRIZZLE_CLEAR)

    span = DRIZZLE_COUNT * DRIZZLE_W + (DRIZZLE_COUNT - 1) * DRIZZLE_GAP
    left = DRIZZLE_CENTER_X - span // 2
    for row in DRIZZLE_ROWS:
        for drop in range(DRIZZLE_COUNT):
            draw_drop(pixels, left + drop * (DRIZZLE_W + DRIZZLE_GAP), row,
                      DRIZZLE_W, DRIZZLE_H)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--icons", type=Path, default=ICON_DIR,
                        help="directory of NNN.bmp icons (default: %(default)s)")
    parser.add_argument("--out", type=Path, default=OUTPUT,
                        help="header file to write (default: %(default)s)")
    args = parser.parse_args()

    codes = []
    for path in args.icons.glob("*.bmp"):
        match = re.fullmatch(r"(-?\d+)\.bmp", path.name)
        if match:
            codes.append(int(match.group(1)))
    codes.sort()

    lines = [
        "/* Generated by tools/generate_icons.py -- do not edit by hand. */",
        "#ifndef WEATHER_ICONS_H",
        "#define WEATHER_ICONS_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define WEATHER_ICON_SIZE {ICON_SIZE}",
        "",
    ]

    for code in codes:
        pixels = bytearray(load_icon(args.icons / f"{code}.bmp"))
        if code == MIST_CODE:
            replace_mist_text(pixels)
        elif code == DRIZZLE_CODE:
            replace_drizzle_text(pixels)
        lines.append(
            f"static const uint8_t {c_name(code)}_data["
            f"{ICON_SIZE} * {ICON_SIZE} / 2] = {{"
        )
        for i in range(0, len(pixels), 16):
            chunk = ", ".join(f"0x{b:02X}" for b in pixels[i : i + 16])
            lines.append("    " + chunk + ",")
        lines.append("};")
        lines.append("")

    lines.append("struct WeatherIcon {")
    lines.append("    int code;")
    lines.append("    const uint8_t *data;")
    lines.append("};")
    lines.append("")
    lines.append("static const WeatherIcon weather_icons[] = {")
    for code in codes:
        lines.append(f"    {{{code}, {c_name(code)}_data}},")
    lines.append("};")
    lines.append("")
    lines.append(
        "static const int weather_icon_count = "
        f"sizeof(weather_icons) / sizeof(weather_icons[0]);"
    )
    lines.append("")
    lines.append("#endif /* WEATHER_ICONS_H */")
    lines.append("")

    args.out.write_text("\n".join(lines))
    print(f"Wrote {args.out} with {len(codes)} icons")


if __name__ == "__main__":
    main()
