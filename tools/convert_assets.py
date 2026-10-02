#!/usr/bin/env python3
"""Build a deterministic browser-asset package from the preserved 3DO media."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib


RUNTIME_SOURCE_ROOTS = ("SF_Resources",)
REFERENCE_SOURCE_ROOTS = ("Coded8bpp", "3DO cels")
DISABLED_MEDIA_DIRECTORIES = frozenset(("Music", "Video", "Voices", "Samples"))
OBSOLETE_RUNTIME_FILES = frozenset(("Data/Cosine", "Data/Tangent"))
FULL_SHEET_BYTES = 320 * 256 * 2
RAW_IMAGE_BYTES = 320 * 240 * 2
MANIFEST_NAME = "assets.manifest.json"
RUNTIME_SCHEMA_VERSION = 1
RUNTIME_GENERATOR = "tools/convert_assets.py --build-runtime"
SOUND_EFFECTS_GENERATOR = "tools/convert_assets.py --sound-effects-only"
STREAMED_MEDIA_GENERATOR = "tools/convert_assets.py --streamed-media-only"
CINEMATICS_GENERATOR = "tools/convert_assets.py --cinematics-only"
UI_TEXTURES_GENERATOR = "tools/convert_assets.py --ui-textures-only"
UI_JSON_SCHEMA_PATHS = {
    "font-metrics-v1": "../../schemas/font-metrics-v1.schema.json",
    "game-configuration-v1": "../../schemas/game-configuration-v1.schema.json",
    "legacy-text-indexed-v1": "../../schemas/legacy-text-indexed-v1.schema.json",
    "legacy-text-lines-v1": "../../schemas/legacy-text-lines-v1.schema.json",
    "texture-animation-v1": "../../schemas/texture-animation-v1.schema.json",
    "ui-assets-manifest-v1": "../../schemas/ui-assets-manifest-v1.schema.json",
    "world-metadata-v1": "../../schemas/world-metadata-v1.schema.json",
}
GAME_CEL_PALETTE_RECORDS = frozenset((132, 133, 134))
UI_FONT_TEXTURE_SCALE = 4
UI_FONT_ATLAS_WIDTH = 512
UI_GAME_ATLAS_WIDTH = 4096
UI_GAME_ATLAS_PADDING = 2

ALPHABET_GLYPH_ADVANCES = (
    8, 6, 8, 8, 8, 8, 8, 8, 8, 8, 11, 10, 10, 10, 9, 9, 11, 10, 4,
    7, 11, 9, 12, 10, 12, 9, 12, 10, 10, 9, 10, 10, 13, 10, 10, 9,
    11, 11, 10,
)
WORLD_FIXED_CEL_GRID_COLUMNS = 16
SKY_FILE_BYTES = 1024
SKY_HEADER_BYTES = 8
SKY_COLOUR_COUNT = 256
MAP_DIMENSION = 256
MAP_BYTES = MAP_DIMENSION * MAP_DIMENSION
POLY_MAP_DIMENSION = 128
POLY_MAP_BYTES = POLY_MAP_DIMENSION * POLY_MAP_DIMENSION


class ConversionError(Exception):
    """Raised when a file resembling a supported format is malformed."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def safe_path(path: Path) -> str:
    """Encode each source-path byte using a portable, deterministic filename."""
    parts = []
    for part in path.parts:
        encoded = []
        for byte in part.encode("utf-8"):
            if (ord("A") <= byte <= ord("Z") or ord("a") <= byte <= ord("z")
                    or ord("0") <= byte <= ord("9") or byte in b".-_"):
                encoded.append(chr(byte))
            else:
                encoded.append(f"~{byte:02X}")
        parts.append("".join(encoded))
    return "/".join(parts)


def output_path(kind: str, source_root: str, relative_path: Path, suffix: str = "") -> str:
    return f"{kind}/{safe_path(Path(source_root) / relative_path)}{suffix}"


def write_bytes(output_root: Path, relative_path: str, data: bytes, manifest_only: bool) -> dict:
    entry = {
        "path": relative_path,
        "mime": "application/octet-stream",
        "type": "raw-copy",
        "size": len(data),
        "sha256": sha256_bytes(data),
        "emitted": not manifest_only,
    }
    if not manifest_only:
        destination = output_root / Path(relative_path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    return entry


def write_file_copy(
    output_root: Path, relative_path: str, source: Path, source_hash: str, manifest_only: bool
) -> dict:
    entry = {
        "path": relative_path,
        "mime": "application/octet-stream",
        "type": "raw-copy",
        "size": source.stat().st_size,
        "sha256": source_hash,
        "emitted": not manifest_only,
    }
    if not manifest_only:
        destination = output_root / Path(relative_path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    return entry


def png_rgb(width: int, height: int, pixels: bytes) -> bytes:
    if len(pixels) != width * height * 3:
        raise ConversionError("PNG input does not contain exactly one RGB value per pixel")
    scanlines = b"".join(
        b"\x00" + pixels[row * width * 3:(row + 1) * width * 3] for row in range(height)
    )
    def chunk(name: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + name + payload + struct.pack(
            ">I", zlib.crc32(name + payload) & 0xFFFFFFFF
        )
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(scanlines, level=9))
        + chunk(b"IEND", b"")
    )


def png_rgba(width: int, height: int, pixels: bytes) -> bytes:
    if len(pixels) != width * height * 4:
        raise ConversionError("PNG input does not contain exactly one RGBA value per pixel")
    scanlines = b"".join(
        b"\x00" + pixels[row * width * 4:(row + 1) * width * 4] for row in range(height)
    )
    def chunk(name: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + name + payload + struct.pack(
            ">I", zlib.crc32(name + payload) & 0xFFFFFFFF
        )
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(scanlines, level=9))
        + chunk(b"IEND", b"")
    )


def rgba_pixels_from_indices(
    indices: list[int], width: int, height: int,
    palette: list[tuple[int, int, int, int]], transparent_zero: bool,
    coded_pmode: bool = False
) -> bytes:
    if len(indices) != width * height:
        raise ConversionError("indexed CEL has an unexpected pixel count")
    pixels = bytearray(width * height * 4)
    for index, palette_index in enumerate(indices):
        if palette_index == -1:
            continue
        palette_entry = palette_index & 31 if coded_pmode else palette_index
        if palette_entry >= len(palette):
            raise ConversionError(f"CEL pixel references palette entry {palette_index}, "
                                  f"but only {len(palette)} entries exist")
        offset = index * 4
        pixels[offset:offset + 4] = bytes(palette[palette_entry])
        if transparent_zero and palette_entry == 0:
            pixels[offset + 3] = 0
        elif coded_pmode and palette_index & 32:
            pixels[offset + 3] = 128
    return bytes(pixels)


def rgba_pixels_from_direct_values(values: list[int], width: int, height: int,
                                   transparent_zero: bool) -> bytes:
    if len(values) != width * height:
        raise ConversionError("direct-colour CEL has an unexpected pixel count")
    pixels = bytearray(width * height * 4)
    for index, word in enumerate(values):
        if word == -1 or (transparent_zero and word == 0):
            continue
        offset = index * 4
        pixels[offset] = ((word >> 10) & 0x1F) * 255 // 31
        pixels[offset + 1] = ((word >> 5) & 0x1F) * 255 // 31
        pixels[offset + 2] = (word & 0x1F) * 255 // 31
        pixels[offset + 3] = 255
    return bytes(pixels)


def scale3x_rgba(pixels: bytes, width: int, height: int) -> tuple[bytes, int, int]:
    """Apply the deterministic Scale3x edge expansion to an RGBA raster."""
    if width <= 0 or height <= 0 or len(pixels) != width * height * 4:
        raise ConversionError("Scale3x input dimensions do not match the RGBA raster")

    def pixel(x: int, y: int) -> bytes:
        x = min(max(x, 0), width - 1)
        y = min(max(y, 0), height - 1)
        offset = (y * width + x) * 4
        return pixels[offset:offset + 4]

    output_width = width * 3
    output = bytearray(output_width * height * 3 * 4)
    for y in range(height):
        for x in range(width):
            a, b, c = pixel(x - 1, y - 1), pixel(x, y - 1), pixel(x + 1, y - 1)
            d, e, f = pixel(x - 1, y), pixel(x, y), pixel(x + 1, y)
            g, h, i = pixel(x - 1, y + 1), pixel(x, y + 1), pixel(x + 1, y + 1)
            if b != h and d != f:
                expanded = (
                    d if d == b else e,
                    b if b == f else e,
                    f if f == h else e,
                    d if d == h else e,
                    e,
                    f if b == f else e,
                    d if d == h else e,
                    h if h == f else e,
                    f if f == h else e,
                )
            else:
                expanded = (e,) * 9
            for output_y in range(3):
                for output_x in range(3):
                    offset = ((y * 3 + output_y) * output_width +
                              x * 3 + output_x) * 4
                    output[offset:offset + 4] = expanded[output_y * 3 + output_x]
    return bytes(output), output_width, height * 3


def scale3x_rgba_tiles(pixels: bytes, width: int, height: int,
                       tile_width: int, tile_height: int) -> tuple[bytes, int, int]:
    """Scale fixed atlas cells independently to prevent cross-cell edge sampling."""
    if (tile_width <= 0 or tile_height <= 0 or width % tile_width != 0 or
            height % tile_height != 0):
        raise ConversionError("Scale3x atlas tiles do not divide the RGBA raster")
    output_width = width * 3
    output = bytearray(output_width * height * 3 * 4)
    for top in range(0, height, tile_height):
        for left in range(0, width, tile_width):
            tile = bytearray(tile_width * tile_height * 4)
            for row in range(tile_height):
                source_start = ((top + row) * width + left) * 4
                destination_start = row * tile_width * 4
                tile[destination_start:destination_start + tile_width * 4] = \
                    pixels[source_start:source_start + tile_width * 4]
            scaled, scaled_width, scaled_height = scale3x_rgba(
                bytes(tile), tile_width, tile_height
            )
            for row in range(scaled_height):
                source_start = row * scaled_width * 4
                destination_start = ((top * 3 + row) * output_width + left * 3) * 4
                output[destination_start:destination_start + scaled_width * 4] = \
                    scaled[source_start:source_start + scaled_width * 4]
    return bytes(output), output_width, height * 3


def xbr_style_2x_rgba(pixels: bytes, width: int, height: int) -> tuple[bytes, int, int]:
    """Apply an xBR-style diagonal-corner expansion to semantic RGBA contributions."""
    output_width = width * 2
    output = bytearray(output_width * height * 2 * 4)

    def pixel_at(x: int, y: int) -> tuple[float, float, float, float]:
        if x < 0 or x >= width or y < 0 or y >= height:
            return 0.0, 0.0, 0.0, 0.0
        offset = (y * width + x) * 4
        return tuple(component / 255.0 for component in pixels[offset:offset + 4])

    def interpolate(first: tuple[float, float, float, float],
                    second: tuple[float, float, float, float],
                    factor: float) -> tuple[float, float, float, float]:
        return tuple(
            first[index] * (1.0 - factor) + second[index] * factor
            for index in range(4)
        )

    def distance(first: tuple[float, float, float, float],
                 second: tuple[float, float, float, float]) -> float:
        return sum(abs(first[index] - second[index]) for index in range(4))

    def similar(first: tuple[float, float, float, float],
                second: tuple[float, float, float, float]) -> bool:
        return distance(first, second) <= 0.18

    def blend(corner: tuple[float, float, float, float],
              center: tuple[float, float, float, float]) -> tuple[float, float, float, float]:
        return interpolate(center, corner, 0.75)

    for y in range(height):
        for x in range(width):
            center = pixel_at(x, y)
            above = pixel_at(x, y - 1)
            left = pixel_at(x - 1, y)
            right = pixel_at(x + 1, y)
            below = pixel_at(x, y + 1)
            upper_left = center
            upper_right = center
            lower_left = center
            lower_right = center

            if not similar(above, below) and not similar(left, right):
                if similar(above, left):
                    upper_left = blend(left, center)
                if similar(above, right):
                    upper_right = blend(right, center)
                if similar(below, left):
                    lower_left = blend(left, center)
                if similar(below, right):
                    lower_right = blend(right, center)

            output_x = x * 2
            output_y = y * 2
            for local_x, local_y, value in (
                    (0, 0, upper_left), (1, 0, upper_right),
                    (0, 1, lower_left), (1, 1, lower_right)):
                offset = ((output_y + local_y) * output_width + output_x + local_x) * 4
                output[offset:offset + 4] = bytes(
                    round(component * 255) for component in value
                )
    return bytes(output), output_width, height * 2


def xbr_style_4x_rgba(pixels: bytes, width: int, height: int) -> tuple[bytes, int, int]:
    """Use two semantic xBR-style passes to produce a 4x glyph atlas."""
    first, first_width, first_height = xbr_style_2x_rgba(pixels, width, height)
    return xbr_style_2x_rgba(first, first_width, first_height)


def scale_game_alphabet(pixels: bytes, width: int, height: int) -> tuple[bytes, int, int]:
    """Use ordinary alpha coverage because Alphabet's two P-modes are equivalent."""
    return scale_game_sprite(normalise_game_pmode_alpha(pixels), width, height)


def normalise_game_pmode_alpha(pixels: bytes) -> bytes:
    """Convert an equivalent Game CEL P-mode marker into ordinary opacity."""
    coverage = bytearray(pixels)
    for offset in range(3, len(coverage), 4):
        coverage[offset] = 255 if coverage[offset] != 0 else 0
    return bytes(coverage)


def unpremultiply_rgba(pixels: bytes) -> bytes:
    """Convert premultiplied RGBA samples to the browser's straight-alpha form."""
    output = bytearray(pixels)
    for offset in range(0, len(output), 4):
        alpha = output[offset + 3]
        if alpha == 0:
            output[offset:offset + 3] = bytes(3)
        elif alpha != 255:
            for component in range(3):
                output[offset + component] = min(
                    255, round(output[offset + component] * 255 / alpha)
                )
    return bytes(output)


def premultiply_rgba(pixels: bytes) -> bytes:
    """Associate RGB with opacity before interpolating Game CEL coverage."""
    output = bytearray(pixels)
    for offset in range(0, len(output), 4):
        alpha = output[offset + 3]
        for component in range(3):
            output[offset + component] = round(output[offset + component] * alpha / 255)
    return bytes(output)


def scale_game_sprite(pixels: bytes, width: int, height: int) -> tuple[bytes, int, int]:
    """Scale a straight-alpha Game CEL with the common xBR-style rasterizer."""
    scaled, scaled_width, scaled_height = xbr_style_4x_rgba(
        premultiply_rgba(pixels), width, height
    )
    return unpremultiply_rgba(scaled), scaled_width, scaled_height


def scale_world_cel_32(pixels: bytes, width: int, height: int) -> tuple[bytes, int, int]:
    """Use xBR-style expansion to make every square world CEL a 32-pixel tile."""
    if width != height or width not in (4, 8, 16, 32):
        raise ConversionError("world CEL must be a square 4, 8, 16, or 32-pixel image")
    scale = 32 // width
    padded_width = width + 2
    padded = bytearray(padded_width * (height + 2) * 4)
    source = premultiply_rgba(pixels)
    for y in range(height + 2):
        source_y = min(max(y - 1, 0), height - 1)
        for x in range(width + 2):
            source_x = min(max(x - 1, 0), width - 1)
            source_offset = (source_y * width + source_x) * 4
            destination_offset = (y * padded_width + x) * 4
            padded[destination_offset:destination_offset + 4] = \
                source[source_offset:source_offset + 4]

    scaled = bytes(padded)
    scaled_width = padded_width
    scaled_height = height + 2
    while scaled_width < padded_width * scale:
        scaled, scaled_width, scaled_height = xbr_style_2x_rgba(
            scaled, scaled_width, scaled_height
        )

    output = bytearray(32 * 32 * 4)
    for y in range(32):
        source_offset = ((y + scale) * scaled_width + scale) * 4
        destination_offset = y * 32 * 4
        output[destination_offset:destination_offset + 32 * 4] = \
            scaled[source_offset:source_offset + 32 * 4]
    return unpremultiply_rgba(output), 32, 32


def _solve_least_squares(samples: list[tuple[float, float]], degree: int) -> list[float]:
    """Fit a low-order polynomial using normal equations and pivoted elimination."""
    size = degree + 1
    matrix = [[0.0] * (size + 1) for _ in range(size)]
    for coordinate, value in samples:
        powers = [coordinate ** power for power in range(size * 2)]
        for row in range(size):
            for column in range(size):
                matrix[row][column] += powers[row + column]
            matrix[row][size] += value * powers[row]

    for column in range(size):
        pivot = max(range(column, size), key=lambda row: abs(matrix[row][column]))
        if abs(matrix[pivot][column]) < 0.000001:
            raise ConversionError("planet matte curve is underdetermined")
        matrix[column], matrix[pivot] = matrix[pivot], matrix[column]
        divisor = matrix[column][column]
        matrix[column] = [value / divisor for value in matrix[column]]
        for row in range(size):
            if row == column:
                continue
            factor = matrix[row][column]
            matrix[row] = [
                value - factor * pivot_value
                for value, pivot_value in zip(matrix[row], matrix[column])
            ]
    return [matrix[row][size] for row in range(size)]


def matte_pyramid_planet(pixels: bytes, width: int, height: int) -> bytes:
    """Infer conservative coverage for the dark outer rim of a Pyramid planet."""
    if len(pixels) != width * height * 4:
        raise ConversionError("planet matte input dimensions do not match the RGBA raster")
    opaque = [
        (x, y) for y in range(height) for x in range(width)
        if pixels[(y * width + x) * 4 + 3] != 0
    ]
    if not opaque:
        return pixels

    centre_x = sum(x for x, _y in opaque) / len(opaque)
    centre_y = sum(y for _x, y in opaque) / len(opaque)
    radius = max(math.hypot(x - centre_x, y - centre_y) for x, y in opaque)
    if radius < 2:
        return pixels

    bins = max(8, round(radius * 2))
    total = [0.0] * bins
    count = [0] * bins
    radial_samples: list[tuple[float, float, int]] = []
    for x, y in opaque:
        offset = (y * width + x) * 4
        luminance = (
            pixels[offset] * 0.2126 +
            pixels[offset + 1] * 0.7152 +
            pixels[offset + 2] * 0.0722
        ) / 255
        radial = math.hypot(x - centre_x, y - centre_y) / radius
        radial_samples.append((radial, luminance, offset))
        if radial <= 0.70:
            index = min(bins - 1, int(radial * bins))
            total[index] += luminance
            count[index] += 1

    samples = [
        ((index + 0.5) / bins, total[index] / count[index])
        for index in range(bins) if count[index] != 0
    ]
    if len(samples) < 2:
        return pixels
    coefficients = _solve_least_squares(samples, min(3, len(samples) - 1))
    output = bytearray(pixels)
    for radial, luminance, offset in radial_samples:
        if radial <= 0.70:
            continue
        expected = sum(
            coefficient * radial ** power
            for power, coefficient in enumerate(coefficients)
        )
        expected = min(1.0, max(0.04, expected))
        coverage = min(1.0, max(0.0, (luminance + 0.015) / (expected + 0.015)))
        output[offset + 3] = round(coverage * 255)
    return bytes(output)


def _pack_maxrects_page(
    sprites: list[tuple[int | str, tuple[bytes, int, int]]],
    atlas_width: int, atlas_height: int, padding: int
) -> tuple[dict[str, dict[str, int]], list[tuple[int | str, tuple[bytes, int, int]]], int]:
    """Place sprites with best-short-side-fit MaxRects, without rotating pixels."""
    free = [(0, 0, atlas_width, atlas_height)]
    placements: dict[str, dict[str, int]] = {}
    unplaced = []
    used_height = 0

    for key, (pixels, width, height) in sprites:
        if len(pixels) != width * height * 4:
            raise ConversionError("UI atlas sprite has inconsistent RGBA dimensions")
        packed_width = width + padding * 2
        packed_height = height + padding * 2
        candidates = [
            (max(used_height, rectangle[1] + packed_height),
             min(rectangle[2] - packed_width, rectangle[3] - packed_height),
             max(rectangle[2] - packed_width, rectangle[3] - packed_height),
             rectangle[1], rectangle[0], index)
            for index, rectangle in enumerate(free)
            if packed_width <= rectangle[2] and packed_height <= rectangle[3]
        ]
        if not candidates:
            unplaced.append((key, (pixels, width, height)))
            continue
        _height, _short_side, _long_side, y, x, index = min(candidates)
        placements[str(key)] = {
            "x": x + padding, "y": y + padding, "width": width, "height": height
        }
        used = (x, y, packed_width, packed_height)
        used_height = max(used_height, y + packed_height)
        split = []
        for rectangle in free:
            rectangle_x, rectangle_y, rectangle_width, rectangle_height = rectangle
            right = rectangle_x + rectangle_width
            bottom = rectangle_y + rectangle_height
            used_right = x + packed_width
            used_bottom = y + packed_height
            if used_right <= rectangle_x or x >= right or \
                    used_bottom <= rectangle_y or y >= bottom:
                split.append(rectangle)
                continue
            if x > rectangle_x:
                split.append((rectangle_x, rectangle_y, x - rectangle_x, rectangle_height))
            if used_right < right:
                split.append((used_right, rectangle_y, right - used_right, rectangle_height))
            if y > rectangle_y:
                split.append((rectangle_x, rectangle_y, rectangle_width, y - rectangle_y))
            if used_bottom < bottom:
                split.append((rectangle_x, used_bottom, rectangle_width, bottom - used_bottom))
        free = [
            rectangle for index, rectangle in enumerate(split)
            if rectangle[2] > 0 and rectangle[3] > 0 and not any(
                other_index != index and
                other[0] <= rectangle[0] and other[1] <= rectangle[1] and
                other[0] + other[2] >= rectangle[0] + rectangle[2] and
                other[1] + other[3] >= rectangle[1] + rectangle[3]
                for other_index, other in enumerate(split)
            )
        ]
    return placements, unplaced, used_height


def _rasterize_ui_sprite_atlas(
    sprites: dict[int | str, tuple[bytes, int, int]], placements: dict[str, dict[str, int]],
    atlas_width: int, atlas_height: int
) -> bytes:
    atlas = bytearray(atlas_width * atlas_height * 4)
    for key, (pixels, width, height) in sprites.items():
        placement = placements[str(key)]
        for row in range(height):
            source = row * width * 4
            destination = ((placement["y"] + row) * atlas_width + placement["x"]) * 4
            atlas[destination:destination + width * 4] = pixels[source:source + width * 4]
    return bytes(atlas)


def pack_ui_sprite_atlas(sprites: dict[int | str, tuple[bytes, int, int]],
                         atlas_width: int = UI_GAME_ATLAS_WIDTH,
                         padding: int = UI_GAME_ATLAS_PADDING) -> tuple[
                             bytes, int, int, dict[str, dict[str, int]]
                         ]:
    """Pack RGBA sprites tightly into one transparent-guttered texture atlas."""
    if atlas_width <= padding * 2:
        raise ConversionError("UI atlas width is too small for its gutters")
    items = sorted(
        sprites.items(),
        key=lambda item: (
            -(item[1][1] + padding * 2) * (item[1][2] + padding * 2),
            -max(item[1][1], item[1][2]), str(item[0])
        )
    )
    height = sum(sprite[2] + padding * 2 for _key, sprite in items)
    placements, unplaced, used_height = _pack_maxrects_page(
        items, atlas_width, height, padding
    )
    if unplaced:
        raise ConversionError("UI atlas packing left sprites unplaced")
    return (
        _rasterize_ui_sprite_atlas(sprites, placements, atlas_width, used_height),
        atlas_width, used_height, placements
    )


def pack_ui_sprite_atlas_pages(
    sprites: dict[int | str, tuple[bytes, int, int]],
    atlas_width: int = UI_GAME_ATLAS_WIDTH,
    atlas_height: int = UI_GAME_ATLAS_WIDTH,
    padding: int = UI_GAME_ATLAS_PADDING
) -> list[tuple[bytes, int, int, dict[str, dict[str, int]]]]:
    """Pack sprites into deterministic WebGL-safe MaxRects atlas pages."""
    if atlas_width <= padding * 2 or atlas_height <= padding * 2:
        raise ConversionError("UI atlas page is too small for its gutters")
    remaining = sorted(
        sprites.items(),
        key=lambda item: (
            -(item[1][1] + padding * 2) * (item[1][2] + padding * 2),
            -max(item[1][1], item[1][2]), str(item[0])
        )
    )
    pages = []
    while remaining:
        placements, unplaced, used_height = _pack_maxrects_page(
            remaining, atlas_width, atlas_height, padding
        )
        if len(unplaced) == len(remaining):
            raise ConversionError("UI atlas sprite exceeds the WebGL-safe page dimensions")
        page_sprites = {
            key: sprite for key, sprite in remaining if str(key) in placements
        }
        pages.append((
            _rasterize_ui_sprite_atlas(page_sprites, placements, atlas_width, used_height),
            atlas_width, used_height, placements
        ))
        remaining = unplaced
    return pages


def gaussian_blur_foreground(values: list[float], mask: list[bool],
                             width: int, height: int) -> list[float]:
    """Blur foreground values without allowing transparent or outline pixels into the filter."""
    kernel = (1, 2, 1)
    kernel_radius = len(kernel) // 2

    def convolve_horizontal(samples: list[float]) -> list[float]:
        output = [0.0] * len(samples)
        for y in range(height):
            for x in range(width):
                total = 0.0
                for index, weight in enumerate(kernel):
                    sample_x = min(width - 1, max(0, x + index - kernel_radius))
                    total += samples[y * width + sample_x] * weight
                output[y * width + x] = total
        return output

    def convolve_vertical(samples: list[float]) -> list[float]:
        output = [0.0] * len(samples)
        for y in range(height):
            for x in range(width):
                total = 0.0
                for index, weight in enumerate(kernel):
                    sample_y = min(height - 1, max(0, y + index - kernel_radius))
                    total += samples[sample_y * width + x] * weight
                output[y * width + x] = total
        return output

    weighted_values = [value if included else 0.0 for value, included in zip(values, mask)]
    weighted_mask = [1.0 if included else 0.0 for included in mask]
    numerators = convolve_vertical(convolve_horizontal(weighted_values))
    denominators = convolve_vertical(convolve_horizontal(weighted_mask))
    return [
        numerator / denominator if denominator != 0.0 else 0.0
        for numerator, denominator in zip(numerators, denominators)
    ]


def bilinear_scale(values: list[float], width: int, height: int,
                   scale: int) -> list[float]:
    """Scale a scalar field while retaining its low-resolution spatial variation."""
    output_width = width * scale
    output = [0.0] * (output_width * height * scale)

    def value_at(x: int, y: int) -> float:
        return values[min(height - 1, max(0, y)) * width + min(width - 1, max(0, x))]

    for y in range(height * scale):
        source_y = (y + 0.5) / scale - 0.5
        top = int(source_y // 1)
        vertical = source_y - top
        for x in range(width * scale):
            source_x = (x + 0.5) / scale - 0.5
            left = int(source_x // 1)
            horizontal = source_x - left
            upper = value_at(left, top) * (1.0 - horizontal) + \
                value_at(left + 1, top) * horizontal
            lower = value_at(left, top + 1) * (1.0 - horizontal) + \
                value_at(left + 1, top + 1) * horizontal
            output[y * output_width + x] = upper * (1.0 - vertical) + lower * vertical
    return output


def masked_bilinear_scale(values: list[float], mask: list[bool], width: int, height: int,
                          scale: int) -> list[float]:
    """Scale foreground values without interpolating transparent or outline samples."""
    numerator = bilinear_scale(
        [value if included else 0.0 for value, included in zip(values, mask)],
        width, height, scale
    )
    denominator = bilinear_scale(
        [1.0 if included else 0.0 for included in mask],
        width, height, scale
    )
    return [
        value / coverage if coverage != 0.0 else 0.0
        for value, coverage in zip(numerator, denominator)
    ]


def unsharp_blend(base: list[float], blurred: list[float], strength: float = 1.25) -> list[float]:
    """Restore foreground-only detail after smoothing, without exceeding valid coverage."""
    if len(base) != len(blurred):
        raise ConversionError("unsharp blend inputs must have equal dimensions")
    return [
        min(1.0, max(0.0, smooth + strength * (detail - smooth)))
        for detail, smooth in zip(base, blurred)
    ]


def font_pixel_weights(value: bytes) -> tuple[float, float, float]:
    """Convert a 3DO Message font sample to premultiplied text contributions."""
    intensity, colour_type = value[:2]
    if colour_type in (0, 1):
        level = 8 if intensity == 7 else intensity + 1
        weight = level / 8.0
        return (weight, 0.0, weight) if colour_type == 0 else (0.0, weight, weight)

    level = 0 if intensity == 0 else (8 if intensity == 7 else intensity + 1)
    return level / 8.0, (8 - level) / 8.0, 1.0


def scale_font_atlas(pixels: bytes, width: int, height: int,
                     char_width: int, char_height: int,
                     scale: int = UI_FONT_TEXTURE_SCALE) -> tuple[bytes, int, int]:
    """Rasterize foreground/outline/opacity glyph data independently per cell."""
    if scale != 4:
        raise ConversionError("font texture scale must use the xBR-style rasterizer")
    atlas_width = width * scale
    atlas_height = height * scale
    output = bytearray(atlas_width * atlas_height * 4)

    for top in range(0, height, char_height):
        for left in range(0, width, char_width):
            foreground_values = []
            foreground_mask = []
            outline = bytearray(char_width * char_height * 4)
            foreground = bytearray(char_width * char_height * 4)
            for y in range(char_height):
                for x in range(char_width):
                    source = ((top + y) * width + left + x) * 4
                    destination = (y * char_width + x) * 4
                    foreground_values.append(0.0)
                    foreground_mask.append(False)
                    if pixels[source + 3] != 0:
                        foreground_weight, outline_weight, _opacity = font_pixel_weights(
                            pixels[source:source + 4]
                        )
                        pixel_index = y * char_width + x
                        foreground_values[pixel_index] = foreground_weight
                        foreground_mask[pixel_index] = foreground_weight != 0.0
                        foreground[destination + 3] = 255 if foreground_weight != 0.0 else 0
                        outline[destination + 3] = round(outline_weight * 255)

            blurred_foreground = bilinear_scale(
                gaussian_blur_foreground(
                    foreground_values, foreground_mask, char_width, char_height
                ),
                char_width, char_height, scale
            )
            sharpened_foreground = unsharp_blend(
                masked_bilinear_scale(
                    foreground_values, foreground_mask, char_width, char_height, scale
                ),
                blurred_foreground
            )
            scaled_foreground, glyph_width, glyph_height = xbr_style_4x_rgba(
                bytes(foreground), char_width, char_height
            )
            scaled_outline, outline_width, outline_height = xbr_style_4x_rgba(
                bytes(outline), char_width, char_height
            )
            if (glyph_width, glyph_height) != (outline_width, outline_height):
                raise ConversionError("font layer scaling produced inconsistent dimensions")
            for y in range(glyph_height):
                destination_start = ((top * scale + y) * atlas_width + left * scale) * 4
                for x in range(glyph_width):
                    source_offset = (y * glyph_width + x) * 4
                    destination = destination_start + x * 4
                    foreground_coverage = scaled_foreground[source_offset + 3] / 255.0
                    foreground_value = sharpened_foreground[y * glyph_width + x]
                    foreground_contribution = foreground_coverage * foreground_value
                    outline_contribution = scaled_outline[source_offset + 3] / 255.0
                    output[destination] = round(foreground_contribution * 255)
                    output[destination + 1] = round(outline_contribution * 255)
                    output[destination + 3] = round(
                        min(1.0, foreground_contribution + outline_contribution) * 255
                    )
    return bytes(output), atlas_width, atlas_height


def rgb555_to_png(data: bytes, width: int, height: int) -> bytes:
    if len(data) != width * height * 2:
        raise ConversionError("RGB555 input has an unexpected length")
    pixels = bytearray(width * height * 3)
    for index in range(width * height):
        word = (data[index * 2] << 8) | data[index * 2 + 1]
        pixels[index * 3] = ((word >> 10) & 0x1F) * 255 // 31
        pixels[index * 3 + 1] = ((word >> 5) & 0x1F) * 255 // 31
        pixels[index * 3 + 2] = (word & 0x1F) * 255 // 31
    return png_rgb(width, height, bytes(pixels))


def rgb555_entries(data: bytes, count: int) -> list[tuple[int, int, int, int]]:
    if len(data) != count * 2:
        raise ConversionError("RGB555 palette has an unexpected length")
    entries = []
    for index in range(count):
        word = struct.unpack(">H", data[index * 2:index * 2 + 2])[0]
        entries.append((
            ((word >> 10) & 0x1F) * 255 // 31,
            ((word >> 5) & 0x1F) * 255 // 31,
            (word & 0x1F) * 255 // 31,
            255,
        ))
    return entries


def rgb555_rgba_pixels(data: bytes, width: int, height: int) -> bytes:
    """Decode contiguous big-endian RGB555 samples into straight RGBA pixels."""
    if len(data) != width * height * 2:
        raise ConversionError("RGB555 input has an unexpected length")
    pixels = bytearray(width * height * 4)
    for index in range(width * height):
        word = struct.unpack_from(">H", data, index * 2)[0]
        offset = index * 4
        pixels[offset:offset + 4] = bytes((
            ((word >> 10) & 0x1F) * 255 // 31,
            ((word >> 5) & 0x1F) * 255 // 31,
            (word & 0x1F) * 255 // 31,
            255,
        ))
    return bytes(pixels)


def sky_rgba_pixels(data: bytes) -> bytes:
    """Decode the 3DO sky table, including its two native header words."""
    if len(data) != SKY_FILE_BYTES:
        raise ConversionError("sky table must be exactly 1,024 bytes")
    entries = []
    for offset in range(SKY_HEADER_BYTES, len(data), 4):
        colour = struct.unpack_from(">H", data, offset)[0]
        if colour & 0x8000:
            raise ConversionError("sky table colour uses the RGB555 high-bit flag")
        entries.append(colour)
    if len(entries) != SKY_COLOUR_COUNT - SKY_HEADER_BYTES // 4:
        raise ConversionError("sky table has an unexpected colour-entry count")
    # The native 1,024-byte sky buffer leaves its final two nominal entries in
    # zero-initialized adjacent storage. Preserve that effective table shape.
    entries.extend((0,) * (SKY_COLOUR_COUNT - len(entries)))
    return rgb555_rgba_pixels(
        b"".join(struct.pack(">H", entry) for entry in entries), SKY_COLOUR_COUNT, 1
    )


def backdrop_rgba_pixels(data: bytes) -> bytes:
    """Decode a 3DO 320x240 RGB555 backdrop stored as adjacent scanline pairs."""
    payload = data
    if len(data) == RAW_IMAGE_BYTES + 36:
        if (data[:4] != b"IMAG" or data[28:32] != b"PDAT" or
                struct.unpack_from(">II", data, 8) != (320, 240) or
                struct.unpack_from(">I", data, 32)[0] != RAW_IMAGE_BYTES + 8):
            raise ConversionError("IMAG backdrop wrapper is invalid")
        payload = data[36:]
    if len(payload) != RAW_IMAGE_BYTES:
        raise ConversionError("backdrop must contain exactly 320x240 RGB555 pixels")
    pixels = bytearray(320 * 240 * 4)
    for y in range(240):
        for x in range(320):
            source = ((y >> 1) * 320 + x) * 4 + ((y & 1) * 2)
            word = struct.unpack_from(">H", payload, source)[0]
            destination = (y * 320 + x) * 4
            pixels[destination:destination + 4] = bytes((
                ((word >> 10) & 0x1F) * 255 // 31,
                ((word >> 5) & 0x1F) * 255 // 31,
                (word & 0x1F) * 255 // 31,
                255,
            ))
    return bytes(pixels)


def map_rgba_pixels(data: bytes, dimension: int = MAP_DIMENSION) -> bytes:
    """Encode a native map byte into the red channel of an opaque RGBA pixel."""
    map_bytes = dimension * dimension
    if len(data) < map_bytes:
        raise ConversionError(f"map must contain at least {dimension}x{dimension} bytes")
    pixels = bytearray(map_bytes * 4)
    for index, value in enumerate(data[:map_bytes]):
        offset = index * 4
        pixels[offset:offset + 4] = bytes((value, 0, 0, 255))
    return bytes(pixels)


def indexed_to_png(indices: list[int], width: int, height: int,
                   palette: list[tuple[int, int, int, int]]) -> bytes:
    if len(indices) != width * height:
        raise ConversionError("indexed CEL has an unexpected pixel count")
    pixels = bytearray(width * height * 4)
    for index, palette_index in enumerate(indices):
        if palette_index == -1:
            continue
        if palette_index >= len(palette):
            raise ConversionError(f"CEL pixel references palette entry {palette_index}, "
                                  f"but only {len(palette)} entries exist")
        pixels[index * 4:index * 4 + 4] = bytes(palette[palette_index])
    return png_rgba(width, height, bytes(pixels))


def direct_values_to_png(values: list[int], width: int, height: int) -> bytes:
    return png_rgba(width, height,
                    rgba_pixels_from_direct_values(values, width, height, False))


def iff_chunks(data: bytes) -> tuple[bytes, dict[bytes, bytes]]:
    if len(data) < 12 or data[:4] != b"FORM":
        raise ConversionError("not an IFF FORM")
    form_length = struct.unpack(">I", data[4:8])[0]
    form_end = 8 + form_length
    if form_end > len(data):
        raise ConversionError("IFF FORM length exceeds file length")
    chunks: dict[bytes, bytes] = {}
    offset = 12
    while offset + 8 <= form_end:
        name = data[offset:offset + 4]
        length = struct.unpack(">I", data[offset + 4:offset + 8])[0]
        end = offset + 8 + length
        if end > form_end:
            raise ConversionError(f"IFF {name!r} chunk exceeds FORM length")
        chunks.setdefault(name, data[offset + 8:end])
        offset = end + (length & 1)
    if offset != form_end:
        raise ConversionError("IFF FORM has trailing partial data")
    return data[8:12], chunks


def extended80_to_int(data: bytes) -> int:
    if len(data) != 10:
        raise ConversionError("AIFF sample rate is not an 80-bit float")
    sign_exponent, mantissa = struct.unpack(">HQ", data)
    sign = -1 if sign_exponent & 0x8000 else 1
    exponent = (sign_exponent & 0x7FFF) - 16383
    if mantissa == 0:
        return 0
    shift = exponent - 63
    if shift >= 0:
        value = mantissa << shift
    else:
        divisor = 1 << -shift
        value = (mantissa + divisor // 2) // divisor
    return sign * value


def aiff_markers(data: bytes) -> dict[int, int]:
    if len(data) < 2:
        raise ConversionError("AIFF MARK chunk is too short")
    marker_count = struct.unpack(">H", data[:2])[0]
    offset = 2
    markers = {}
    for _ in range(marker_count):
        if offset + 7 > len(data):
            raise ConversionError("AIFF MARK chunk ends before a marker")
        marker_id, frame = struct.unpack(">HI", data[offset:offset + 6])
        name_length = data[offset + 6]
        offset += 7 + name_length
        if offset & 1:
            offset += 1
        if offset > len(data):
            raise ConversionError("AIFF MARK marker name exceeds its chunk")
        if marker_id in markers:
            raise ConversionError("AIFF MARK chunk has duplicate marker IDs")
        markers[marker_id] = frame
    if offset != len(data):
        raise ConversionError("AIFF MARK chunk has trailing data")
    return markers


def aiff_sustain_loop(chunks: dict[bytes, bytes], frames: int) -> dict:
    instrument = chunks.get(b"INST")
    if instrument is None:
        return {}
    if len(instrument) != 20:
        raise ConversionError("AIFF INST chunk is not 20 bytes")
    (_base_note, _detune, _low_note, _high_note, _low_velocity, _high_velocity,
     _gain, play_mode, begin_marker, end_marker, _release_mode,
     _release_begin, _release_end) = struct.unpack(">BbBBBBhHHHHHH", instrument)
    if play_mode == 0:
        return {}
    if play_mode != 1:
        raise ConversionError(f"AIFF sustain loop mode {play_mode} is unsupported")
    markers = aiff_markers(chunks.get(b"MARK", b""))
    try:
        start = markers[begin_marker]
        end = markers[end_marker]
    except KeyError as error:
        raise ConversionError("AIFF sustain loop references a missing marker") from error
    if start >= end or end > frames:
        raise ConversionError("AIFF sustain loop markers are outside the sample")
    return {"loop_start": start, "loop_end": end}


def sdx2_to_pcm(payload: bytes, frames: int, channels: int) -> bytes:
    if len(payload) != frames * channels:
        raise ConversionError("SDX2 payload length does not match its frame count")
    histories = [0] * channels
    pcm = bytearray(frames * channels * 2)
    for index, code in enumerate(payload):
        signed = code if code < 128 else code - 256
        value = 2 * signed * abs(signed)
        channel = index % channels
        if code & 1:
            value += histories[channel]
        value = max(-32768, min(32767, value))
        histories[channel] = value
        struct.pack_into("<h", pcm, index * 2, value)
    return bytes(pcm)


def aiff_to_wav(data: bytes) -> tuple[bytes, dict]:
    form_type, chunks = iff_chunks(data)
    if form_type not in (b"AIFF", b"AIFC") or b"COMM" not in chunks or b"SSND" not in chunks:
        raise ConversionError("missing AIFF COMM or SSND chunk")
    comm = chunks[b"COMM"]
    if len(comm) < 18:
        raise ConversionError("AIFF COMM chunk is too short")
    channels, frames, sample_size = struct.unpack(">HIH", comm[:8])
    sample_rate = extended80_to_int(comm[8:18])
    compression = b"NONE" if form_type == b"AIFF" else comm[18:22]
    if compression not in (b"NONE", b"twos", b"SDX2"):
        raise ConversionError(
            f"AIFC compression {compression.decode('ascii', 'replace')!r} is unsupported"
        )
    if channels == 0 or frames == 0 or sample_rate <= 0 or sample_rate > 384000:
        raise ConversionError("AIFF has invalid channel, frame, or sample-rate values")
    if sample_size not in (8, 16, 24, 32):
        raise ConversionError(f"unsupported PCM sample size {sample_size}")
    ssnd = chunks[b"SSND"]
    if len(ssnd) < 8:
        raise ConversionError("AIFF SSND chunk is too short")
    data_offset, _block_size = struct.unpack(">II", ssnd[:8])
    sample_bytes = (sample_size + 7) // 8
    compressed = compression == b"SDX2"
    if compressed and sample_size != 16:
        raise ConversionError("SDX2 AIFC must describe 16-bit reconstructed samples")
    payload_length = frames * channels if compressed else frames * channels * sample_bytes
    start = 8 + data_offset
    end = start + payload_length
    if end > len(ssnd):
        raise ConversionError("AIFF SSND chunk is shorter than its COMM frame count")
    payload = bytes(ssnd[start:end])
    if compressed:
        pcm = sdx2_to_pcm(payload, frames, channels)
        output_sample_size = 16
        output_sample_bytes = 2
    else:
        pcm = bytearray(payload)
        if sample_size == 8:
            for index in range(len(pcm)):
                pcm[index] ^= 0x80
        elif sample_bytes > 1:
            for index in range(0, len(pcm), sample_bytes):
                pcm[index:index + sample_bytes] = pcm[index:index + sample_bytes][::-1]
        pcm = bytes(pcm)
        output_sample_size = sample_size
        output_sample_bytes = sample_bytes
    byte_rate = sample_rate * channels * output_sample_bytes
    block_align = channels * output_sample_bytes
    wav = (
        b"RIFF"
        + struct.pack("<I", 36 + len(pcm))
        + b"WAVEfmt "
        + struct.pack("<IHHIIHH", 16, 1, channels, sample_rate, byte_rate, block_align,
                      output_sample_size)
        + b"data"
        + struct.pack("<I", len(pcm))
        + pcm
    )
    metadata = {
        "channels": channels,
        "frames": frames,
        "sample_rate": sample_rate,
        "sample_size": output_sample_size,
        "source_compression": compression.decode("ascii"),
    }
    metadata.update(aiff_sustain_loop(chunks, frames))
    return bytes(wav), metadata


def stream_chunks(data: bytes):
    offset = 0
    while offset < len(data):
        if offset + 8 > len(data):
            raise ConversionError("3DO stream has a truncated chunk header")
        chunk_type = data[offset:offset + 4]
        chunk_size = struct.unpack_from(">I", data, offset + 4)[0]
        if chunk_size < 8:
            raise ConversionError("3DO stream has a chunk shorter than its header")
        chunk_end = offset + chunk_size
        if chunk_end > len(data):
            raise ConversionError("3DO stream chunk exceeds the file boundary")
        yield chunk_type, data[offset:chunk_end]
        offset = (chunk_end + 3) & ~3
    if offset != len(data):
        raise ConversionError("3DO stream has invalid chunk alignment")


def riff_chunk(chunk_type: bytes, payload: bytes) -> bytes:
    if len(chunk_type) != 4:
        raise ConversionError("RIFF chunk type must be four bytes")
    return chunk_type + struct.pack("<I", len(payload)) + payload + (b"\0" if len(payload) & 1 else b"")


def riff_list(list_type: bytes, payload: bytes) -> bytes:
    if len(list_type) != 4:
        raise ConversionError("RIFF list type must be four bytes")
    return b"LIST" + struct.pack("<I", len(payload) + 4) + list_type + payload + (
        b"\0" if len(payload) & 1 else b""
    )


def cinepak_stream_to_avi(data: bytes) -> tuple[bytes, dict]:
    """Demux a 3DO FILM/SNDS stream into an AVI FFmpeg can transcode."""
    video_header = None
    frames = []
    audio_header = None
    compressed_audio = bytearray()

    for chunk_type, chunk in stream_chunks(data):
        if len(chunk) < 20:
            continue
        subtype = chunk[16:20]
        if chunk_type == b"FILM" and subtype == b"FHDR":
            if len(chunk) != 44 or video_header is not None:
                raise ConversionError("3DO stream has an invalid Cinepak header")
            version, codec, height, width, frame_rate, frame_count = struct.unpack_from(
                ">6I", chunk, 20
            )
            if (version != 0 or codec != int.from_bytes(b"cvid", "big") or
                    not (0 < width <= 4096 and 0 < height <= 4096)):
                raise ConversionError("3DO stream has an unsupported Cinepak descriptor")
            if frame_rate == 0 or frame_count == 0:
                raise ConversionError("3DO stream has an invalid Cinepak timing descriptor")
            video_header = (width, height, frame_rate, frame_count)
        elif chunk_type == b"FILM" and subtype == b"FRME":
            if len(chunk) < 28:
                raise ConversionError("3DO stream has a truncated Cinepak frame")
            _duration, frame_size = struct.unpack_from(">2I", chunk, 20)
            if frame_size == 0 or frame_size > len(chunk) - 28:
                raise ConversionError("3DO stream has an invalid Cinepak frame size")
            frames.append(bytes(chunk[28:28 + frame_size]))
        elif chunk_type == b"SNDS" and subtype == b"SHDR":
            if len(chunk) != 64 or audio_header is not None:
                raise ConversionError("3DO stream has an invalid audio descriptor")
            (_version, _buffers, _amplitude, _pan, _format, sample_size, sample_rate,
             channels, compression, compression_ratio, sample_count) = struct.unpack_from(
                ">11I", chunk, 20
            )
            if (sample_size != 16 or sample_rate == 0 or channels not in (1, 2) or
                    compression != int.from_bytes(b"SDX2", "big") or compression_ratio != 2 or
                    sample_count == 0):
                raise ConversionError("3DO stream has an unsupported SDX2 audio descriptor")
            audio_header = (sample_rate, channels, sample_count)
        elif chunk_type == b"SNDS" and subtype == b"SSMP":
            if len(chunk) < 24:
                raise ConversionError("3DO stream has a truncated audio packet")
            sample_size = struct.unpack_from(">I", chunk, 20)[0]
            if sample_size == 0 or sample_size > len(chunk) - 24:
                raise ConversionError("3DO stream has an invalid audio packet size")
            compressed_audio.extend(chunk[24:24 + sample_size])

    if video_header is None or audio_header is None:
        raise ConversionError("3DO stream must contain Cinepak video and SDX2 audio")
    width, height, frame_rate, expected_frames = video_header
    if len(frames) != expected_frames:
        raise ConversionError("3DO stream Cinepak frame count disagrees with its header")
    sample_rate, channels, expected_audio_bytes = audio_header
    if len(compressed_audio) != expected_audio_bytes or len(compressed_audio) % channels:
        raise ConversionError("3DO stream audio payload disagrees with its descriptor")

    pcm = sdx2_to_pcm(bytes(compressed_audio), len(compressed_audio) // channels, channels)
    block_align = channels * 2
    max_frame_size = max(len(frame) for frame in frames)
    audio_rate = sample_rate * block_align
    video_handler = b"cvid"
    avi_header = struct.pack(
        "<14I",
        1_000_000 // frame_rate,
        max(max_frame_size * frame_rate, audio_rate),
        0,
        0x10,
        len(frames),
        0,
        2,
        max(max_frame_size, len(pcm)),
        width,
        height,
        0,
        0,
        0,
        0,
    )
    video_stream_header = struct.pack(
        "<4s4sIHHIIIIIIIIhhhh",
        b"vids",
        video_handler,
        0,
        0,
        0,
        0,
        1,
        frame_rate,
        0,
        len(frames),
        max_frame_size,
        0xFFFFFFFF,
        0,
        0,
        0,
        width,
        height,
    )
    video_format = struct.pack(
        "<IiiHH4sIiiII", 40, width, height, 1, 24, video_handler, width * height * 3,
        0, 0, 0, 0
    )
    audio_stream_header = struct.pack(
        "<4s4sIHHIIIIIIIIhhhh",
        b"auds",
        b"\0\0\0\0",
        0,
        0,
        0,
        0,
        block_align,
        audio_rate,
        0,
        len(pcm) // block_align,
        len(pcm),
        0xFFFFFFFF,
        block_align,
        0,
        0,
        0,
        0,
    )
    audio_format = struct.pack("<HHIIHH", 1, channels, sample_rate, audio_rate, block_align, 16)
    header_list = riff_list(
        b"hdrl",
        riff_chunk(b"avih", avi_header) +
        riff_list(b"strl", riff_chunk(b"strh", video_stream_header) +
                  riff_chunk(b"strf", video_format)) +
        riff_list(b"strl", riff_chunk(b"strh", audio_stream_header) +
                  riff_chunk(b"strf", audio_format)),
    )
    movie_list = riff_list(
        b"movi",
        b"".join(riff_chunk(b"00dc", frame) for frame in frames) +
        riff_chunk(b"01wb", pcm),
    )
    avi = b"RIFF" + struct.pack("<I", len(header_list) + len(movie_list) + 4) + b"AVI " + \
        header_list + movie_list
    return avi, {
        "width": width,
        "height": height,
        "frame_rate": frame_rate,
        "frames": len(frames),
        "audio_channels": channels,
        "audio_sample_rate": sample_rate,
        "audio_frames": len(pcm) // block_align,
    }


def text_to_utf8(data: bytes) -> bytes | None:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return None
    if any((ord(character) < 32 and character not in "\t\r\n") for character in text):
        return None
    return text.replace("\r\n", "\n").replace("\r", "\n").encode("utf-8")


def palette_json(data: bytes) -> bytes:
    return palette_metadata_json(rgb555_entries(data, 32), "rgb555be-32")


def palette_metadata_json(entries: list[tuple[int, int, int, int]], format_name: str) -> bytes:
    serialized = []
    for index, (red, green, blue, alpha) in enumerate(entries):
        rgb555 = (((red * 31 + 127) // 255) << 10) | (((green * 31 + 127) // 255) << 5) | (
            (blue * 31 + 127) // 255
        )
        serialized.append({
            "index": index,
            "rgb555": f"0x{rgb555:04X}",
            "rgba": [red, green, blue, alpha],
        })
    return (json.dumps({"format": format_name, "entries": serialized},
                       indent=2, sort_keys=True) + "\n").encode("utf-8")


def monochrome_palette_json(data: bytes) -> bytes:
    if len(data) != 1024:
        raise ConversionError("monochrome palette length is not 1,024 bytes")
    colours = bytearray()
    for offset in range(0, len(data), 8):
        colour = data[offset:offset + 2]
        if data[offset:offset + 8] != colour * 4:
            raise ConversionError("monochrome palette entry does not contain four identical colours")
        colours.extend(colour)
    return palette_metadata_json(rgb555_entries(bytes(colours), 128), "rgb555be-monochrome-128")


def planet_info_json(data: bytes) -> bytes:
    if len(data) != 24:
        raise ConversionError("planet info record length is not 24 bytes")
    explosion_words = struct.unpack(">4I", data[:16])
    payload = {
        "format": "star-fighter-planet-info-v1",
        "explosion_data": [f"0x{word:08X}" for word in explosion_words],
        "height_check": data[16],
        "space_mission": data[17],
        "reserved": [data[18], data[19]],
        "comet_rate": struct.unpack(">I", data[20:24])[0],
    }
    return (json.dumps(payload, indent=2, sort_keys=True) + "\n").encode("utf-8")


class BitReader:
    def __init__(self, data: bytes):
        self.data = data
        self.bit_offset = 0

    def read(self, bits: int) -> int:
        if bits < 0 or self.bit_offset + bits > len(self.data) * 8:
            raise ConversionError("packed CEL command exceeds its row boundary")
        value = 0
        for _ in range(bits):
            byte = self.data[self.bit_offset // 8]
            value = (value << 1) | ((byte >> (7 - (self.bit_offset & 7))) & 1)
            self.bit_offset += 1
        return value

    def remaining_is_zero(self) -> bool:
        while self.bit_offset < len(self.data) * 8:
            if self.read(1):
                return False
        return True


CEL_BPP = {1: 1, 2: 2, 3: 4, 4: 6, 5: 8}
CCB_PACKED = 1 << 9
CCB_BACKGROUND_ZERO_OPAQUE = 1 << 14
CCB_LDPLUT = 1 << 23
CCB_PXOR = 1 << 11
CCB_USEAV = 1 << 13
GAME_CEL_PIXC_TYPE2 = (
    0x03000300, 0x08000800, 0x07000700, 0x10001000, 0x0B000B00,
    0x18001800, 0x0F000F00, 0x13001300, 0x17001700, 0x1B001B00,
    0x1F001F00,
)


def pixc_source_scale(half: int, use_av: bool) -> float:
    divisor = (16, 2, 4, 8)[(half >> 8) & 3]
    first = 0 if half & 0x8000 else ((half >> 10) & 7) + 1
    second = 1 if ((half >> 6) & 3) == 3 else 0
    if use_av and second != 0:
        second /= (1, 2, 4, 1)[(half >> 4) & 3]
    return (first / divisor + second) / (2 if half & 1 else 1)


def pixc_operation(half: int, flags: int) -> str:
    first_source = (half >> 15) & 1
    second_source = (half >> 6) & 3
    use_av = bool(flags & CCB_USEAV)
    if flags & CCB_PXOR:
        return "replace"
    if use_av and half & 2 and second_source == 2:
        return "subtract" if first_source == 0 else "reverse_subtract"
    if first_source != 0 and second_source == 3:
        return "diminish"
    if second_source == 2:
        return "mix" if use_av else "add"
    return "replace"


def game_cel_pmode_is_equivalent(pixc: int, flags: int) -> bool:
    """Check every supported Game CEL shade before discarding its P-mode bit."""
    use_av = bool(flags & CCB_USEAV)
    for shade in GAME_CEL_PIXC_TYPE2:
        combined = pixc | shade
        first = combined & 0xFFFF
        second = combined >> 16
        if (pixc_operation(first, flags), pixc_source_scale(first, use_av)) != \
                (pixc_operation(second, flags), pixc_source_scale(second, use_av)):
            return False
    return True


def cel_height(preamble0: int) -> int:
    return ((preamble0 >> 6) & 0x3FF) + 1


def cel_width(preamble1: int) -> int:
    return (preamble1 & 0x7FF) + 1


def decode_packed_rows(payload: bytes, height: int, bits_per_pixel: int,
                       expected_width: int | None = None,
                       allow_legacy_padding: bool = False) -> tuple[list[int], int]:
    """Decode the row-aligned RLE written by Get16Data/Get32Data."""
    if height <= 0 or height > 1024:
        raise ConversionError("packed CEL height is out of range")
    offset = 0
    pixels: list[int] = []
    width = expected_width
    for row_index in range(height):
        if offset >= len(payload):
            raise ConversionError("packed CEL ends before all rows")
        header_size = 1 if bits_per_pixel < 8 else 2
        if offset + header_size > len(payload):
            raise ConversionError("packed CEL row is missing its size header")
        row_offset = (payload[offset] if header_size == 1 else
                      (payload[offset] << 8) | payload[offset + 1])
        row_size = (row_offset + 2) * 4
        if row_size > len(payload) - offset:
            raise ConversionError("packed CEL row offset exceeds payload")
        reader = BitReader(payload[offset + header_size:offset + row_size])
        row: list[int] = []
        while True:
            if width is not None and len(row) == width:
                break
            command = reader.read(2)
            if command == 0:
                if not allow_legacy_padding and reader.read(2) != 0:
                    raise ConversionError("packed CEL has an unsupported command")
                break
            count = reader.read(6) + 1
            if command == 1:
                row.extend(reader.read(bits_per_pixel) for _ in range(count))
            elif command == 2:
                row.extend([-1] * count)
            elif command == 3:
                row.extend([reader.read(bits_per_pixel)] * count)
            else:
                raise ConversionError("packed CEL uses unsupported command 2")
            if width is not None and len(row) > width:
                raise ConversionError("packed CEL row exceeds its declared width")
        if not allow_legacy_padding and not reader.remaining_is_zero():
            raise ConversionError("packed CEL row has non-zero padding")
        if width is None:
            width = len(row)
        elif len(row) != width:
            raise ConversionError(
                f"packed CEL row {row_index} has width {len(row)}, expected {width}"
            )
        pixels.extend(row)
        offset += row_size
    if width is None or width == 0:
        raise ConversionError("packed CEL has no pixels")
    if not allow_legacy_padding and any(payload[offset:]):
        raise ConversionError("packed CEL has non-zero trailing bytes")
    return pixels, width


def decode_unpacked_rows(payload: bytes, width: int, height: int) -> list[int]:
    if width <= 0 or height <= 0 or width > 2048 or height > 1024:
        raise ConversionError("unpacked CEL dimensions are out of range")
    row_bytes = max(8, (width + 1) // 2)
    if len(payload) != row_bytes * height:
        raise ConversionError(
            f"unpacked CEL payload is {len(payload)} bytes, expected {row_bytes * height}"
        )
    indices = []
    for row in range(height):
        data = payload[row * row_bytes:(row + 1) * row_bytes]
        for column in range(width):
            indices.append(data[column // 2] >> 4 if column % 2 == 0 else data[column // 2] & 0x0F)
        if any(data[(width + 1) // 2:]):
            raise ConversionError("unpacked CEL row has non-zero padding")
    return indices


def packed_row_width(payload: bytes, bits_per_pixel: int) -> int:
    header_size = 1 if bits_per_pixel < 8 else 2
    if len(payload) < header_size:
        raise ConversionError("packed CEL row is missing its size header")
    row_offset = (payload[0] if header_size == 1 else
                  (payload[0] << 8) | payload[1])
    row_size = (row_offset + 2) * 4
    if row_size > len(payload):
        raise ConversionError("packed CEL row offset exceeds payload")
    reader = BitReader(payload[header_size:row_size])
    width = 0
    while reader.bit_offset + 2 <= len(reader.data) * 8:
        command = reader.read(2)
        if command == 0:
            return width
        count = reader.read(6) + 1
        width += count
        if command == 1:
            reader.read(count * bits_per_pixel)
        elif command == 3:
            reader.read(bits_per_pixel)
    if reader.bit_offset == len(reader.data) * 8:
        return width
    raise ConversionError("packed CEL command exceeds its row boundary")


def parse_native_indexed_cel(data: bytes) -> tuple[list[int], int, int, list[tuple[int, int, int, int]], str]:
    if (len(data) < 108 or data[:4] != b"CCB " or data[80:84] != b"PDAT"):
        raise ConversionError("not a complete CCB/PDAT CEL")
    flags = struct.unpack(">I", data[12:16])[0]
    width, height = struct.unpack(">II", data[72:80])
    pdat_length = struct.unpack(">I", data[84:88])[0]
    pdat_end = 80 + pdat_length
    if not (flags & CCB_LDPLUT) or not (flags & CCB_PACKED):
        raise ConversionError("CEL is not packed indexed data")
    if pdat_length < 12 or pdat_end + 12 > len(data) or data[pdat_end:pdat_end + 4] != b"PLUT":
        raise ConversionError("indexed CEL is missing an adjacent PLUT chunk")
    plut_length = struct.unpack(">I", data[pdat_end + 4:pdat_end + 8])[0]
    palette_count = struct.unpack(">I", data[pdat_end + 8:pdat_end + 12])[0]
    palette_end = 12 + palette_count * 2
    aligned_palette_end = (palette_end + 3) & ~3
    if plut_length != aligned_palette_end or pdat_end + plut_length != len(data):
        raise ConversionError("indexed CEL PLUT length is inconsistent")
    if any(data[pdat_end + palette_end:pdat_end + plut_length]):
        raise ConversionError("indexed CEL PLUT has non-zero padding")
    preamble0 = struct.unpack(">I", data[88:92])[0]
    bits_per_pixel = CEL_BPP.get(preamble0 & 7)
    if bits_per_pixel is None:
        raise ConversionError(f"CEL uses unsupported indexed bpp code {preamble0 & 7}")
    if cel_height(preamble0) != height:
        raise ConversionError("CEL preamble height disagrees with its CCB header")
    palette = rgb555_entries(data[pdat_end + 12:pdat_end + palette_end], palette_count)
    pixels, decoded_width = decode_packed_rows(data[92:pdat_end], height, bits_per_pixel, width)
    return pixels, width, height, palette, f"indexed-{bits_per_pixel}bpp-packed"


def parse_native_direct_cel(data: bytes) -> tuple[bytes, int, int, str]:
    if (len(data) < 96 or data[:4] != b"CCB " or data[80:84] != b"PDAT"):
        raise ConversionError("not a complete CCB/PDAT CEL")
    flags = struct.unpack(">I", data[12:16])[0]
    width, height = struct.unpack(">II", data[72:80])
    pdat_end = 80 + struct.unpack(">I", data[84:88])[0]
    preamble0 = struct.unpack(">I", data[88:92])[0]
    if flags & CCB_LDPLUT or preamble0 & 7 != 6 or cel_height(preamble0) != height:
        raise ConversionError("CEL is not an established direct RGB555 CEL")
    if pdat_end != len(data):
        raise ConversionError("direct CEL has trailing chunks or an inconsistent PDAT length")
    if flags & CCB_PACKED:
        values, decoded_width = decode_packed_rows(data[92:pdat_end], height, 16, width)
        if decoded_width != width:
            raise ConversionError("packed direct CEL width disagrees with CCB header")
        return direct_values_to_png(values, width, height), width, height, "rgb555be-packed-png"
    preamble1 = struct.unpack(">I", data[92:96])[0]
    if cel_width(preamble1) != width:
        raise ConversionError("direct CEL preamble width disagrees with CCB header")
    pixels = data[96:pdat_end]
    if len(pixels) != width * height * 2:
        raise ConversionError("direct CEL payload length is inconsistent")
    return rgb555_to_png(pixels, width, height), width, height, "rgb555be-png"


def grouped_records(data: bytes) -> list[bytes]:
    if len(data) < 4:
        raise ConversionError("grouped CEL is too short for an offset table")
    table_length = struct.unpack(">I", data[:4])[0]
    if table_length == 0 or table_length % 4 or table_length > len(data):
        raise ConversionError("grouped CEL has an invalid offset-table length")
    count = table_length // 4
    offsets = struct.unpack(f">{count}I", data[:table_length])
    if offsets[0] != table_length or any(left >= right for left, right in zip(offsets, offsets[1:])):
        raise ConversionError("grouped CEL offsets are not strictly increasing")
    if offsets[-1] >= len(data):
        raise ConversionError("grouped CEL final offset is outside the file")
    return [data[start:end] for start, end in zip(offsets, (*offsets[1:], len(data)))]


def parse_grouped_16(record: bytes) -> tuple[
    list[int], int, int, list[tuple[int, int, int, int]], str
]:
    if len(record) < 44:
        raise ConversionError("grouped 16 CEL record is too short")
    flags = struct.unpack(">I", record[:4])[0]
    if flags & CCB_PACKED:
        raise ConversionError("grouped 16 CEL unexpectedly uses packed data")
    palette = rgb555_entries(record[4:36], 16)
    preamble0 = struct.unpack(">I", record[36:40])[0]
    preamble1 = struct.unpack(">I", record[40:44])[0]
    if preamble0 & 7 != 3 or cel_height(preamble0) != 16 or cel_width(preamble1) != 16:
        raise ConversionError("grouped 16 CEL is not the established 16x16 4bpp format")
    return decode_unpacked_rows(record[44:], 16, 16), 16, 16, palette, "indexed-4bpp-unpacked"


def parse_grouped_32(record: bytes) -> tuple[
    list[int], int, int, list[tuple[int, int, int, int]], str
]:
    if len(record) < 44:
        raise ConversionError("grouped 32 CEL record is too short")
    descriptor = struct.unpack(">I", record[:4])[0]
    bitsize = descriptor & 0xFF
    if bitsize > 3:
        raise ConversionError("grouped 32 CEL has an unsupported size descriptor")
    width = 4 << bitsize
    flags = struct.unpack(">I", record[4:8])[0]
    palette = rgb555_entries(record[8:40], 16)
    preamble0 = struct.unpack(">I", record[40:44])[0]
    if preamble0 & 7 != 3 or cel_height(preamble0) != width:
        raise ConversionError("grouped 32 CEL preamble is not the established 4bpp square format")
    if flags & CCB_PACKED:
        indices, decoded_width = decode_packed_rows(record[44:], width, 4, width)
        if decoded_width != width:
            raise ConversionError("packed grouped 32 CEL width disagrees with descriptor")
        encoding = "indexed-4bpp-packed"
    else:
        if len(record) < 48:
            raise ConversionError("unpacked grouped 32 CEL record is too short")
        preamble1 = struct.unpack(">I", record[44:48])[0]
        if cel_width(preamble1) != width:
            raise ConversionError("unpacked grouped 32 CEL preamble width disagrees with descriptor")
        indices = decode_unpacked_rows(record[48:], width, width)
        encoding = "indexed-4bpp-unpacked"
    return indices, width, width, palette, encoding


def parse_grouped_game(record: bytes) -> tuple[
    list[int], int, int, list[tuple[int, int, int, int]], str
]:
    if len(record) < 76:
        raise ConversionError("grouped game CEL record is too short")
    flags = struct.unpack(">I", record[4:8])[0]
    if not (flags & CCB_LDPLUT) or not (flags & CCB_PACKED):
        raise ConversionError("grouped game CEL is not a packed indexed record")
    palette = rgb555_entries(record[8:72], 32)
    preamble0 = struct.unpack(">I", record[72:76])[0]
    bits_per_pixel = CEL_BPP.get(preamble0 & 7)
    if bits_per_pixel is None:
        raise ConversionError(
            f"grouped game CEL uses unsupported indexed bpp code {preamble0 & 7}"
        )
    height = cel_height(preamble0)
    packed_rows = record[76:]
    width = packed_row_width(packed_rows, bits_per_pixel)
    indices, width = decode_packed_rows(
        packed_rows, height, bits_per_pixel, width, allow_legacy_padding=True
    )
    return indices, width, height, palette, f"indexed-{bits_per_pixel}bpp-packed"


def parse_grouped_game_direct_values(record: bytes) -> tuple[list[int], int, int]:
    if len(record) < 16:
        raise ConversionError("grouped game CEL record is too short")
    flags = struct.unpack(">I", record[4:8])[0]
    source_offset = 72 if flags & CCB_LDPLUT else 8
    if len(record) < source_offset + 8:
        raise ConversionError("grouped direct-colour CEL record is too short")
    preamble0 = struct.unpack(">I", record[source_offset:source_offset + 4])[0]
    if preamble0 & 7 != 6:
        raise ConversionError("grouped game CEL is not in the established 16bpp format")
    height = cel_height(preamble0)
    if flags & CCB_PACKED:
        packed_rows = record[source_offset + 4:]
        width = packed_row_width(packed_rows, 16)
        values, width = decode_packed_rows(packed_rows, height, 16, width,
                                           allow_legacy_padding=True)
        return values, width, height
    preamble1 = struct.unpack(">I", record[source_offset + 4:source_offset + 8])[0]
    width = cel_width(preamble1)
    pixels = record[source_offset + 8:]
    if len(pixels) != width * height * 2:
        raise ConversionError("grouped direct-colour CEL payload length is inconsistent")
    return [
        struct.unpack(">H", pixels[offset:offset + 2])[0]
        for offset in range(0, len(pixels), 2)
    ], width, height


def parse_grouped_game_direct(record: bytes) -> tuple[bytes, int, int]:
    values, width, height = parse_grouped_game_direct_values(record)
    return direct_values_to_png(values, width, height), width, height


def parse_tiles_4bpp(data: bytes) -> list[list[int]]:
    if not data or len(data) % 16:
        raise ConversionError("4x4 tile data length is not a positive multiple of 16")
    tiles = []
    for tile_offset in range(0, len(data), 16):
        indices = []
        for row_offset in range(tile_offset, tile_offset + 16, 4):
            word = struct.unpack(">I", data[row_offset:row_offset + 4])[0]
            indices.extend((word >> shift) & 0x3F for shift in (26, 20, 14, 8))
        tiles.append(indices)
    return tiles


def tile_palette(entries: list[tuple[int, int, int, int]]) -> list[tuple[int, int, int, int]]:
    if len(entries) != 32:
        raise ConversionError("4x4 tile palette must contain 32 base colours")
    return entries + [(red // 2, green // 2, blue // 2, alpha) for red, green, blue, alpha in entries]


def source_files(root: Path):
    for directory, directories, filenames in os.walk(root, followlinks=False):
        directories[:] = sorted(name for name in directories if not (Path(directory) / name).is_symlink())
        for filename in sorted(filenames):
            candidate = Path(directory) / filename
            if not candidate.is_symlink() and candidate.is_file():
                yield candidate


def is_included_source(source_root_name: str, source_root: Path, source: Path,
                       include_disabled_media: bool) -> bool:
    if source_root_name != "SF_Resources" or include_disabled_media:
        return True
    return source.relative_to(source_root).parts[0] not in DISABLED_MEDIA_DIRECTORIES


def validate_output_path(base: Path, output: Path) -> None:
    """Keep an optional --clean operation confined to a non-source repo child."""
    base = base.resolve()
    try:
        relative_output = output.relative_to(base)
    except ValueError as error:
        raise ConversionError("--output must be inside the repository") from error
    if not relative_output.parts:
        raise ConversionError("--output must not be the repository root")
    if output.is_symlink():
        raise ConversionError("--output must not be a symbolic link")
    for root_name in RUNTIME_SOURCE_ROOTS + REFERENCE_SOURCE_ROOTS:
        source_root = base / root_name
        try:
            output.relative_to(source_root)
        except ValueError:
            continue
        raise ConversionError("--output must not be inside a source directory")


def clean_output_path(output: Path) -> None:
    """Only delete an empty directory or one previously made by this tool."""
    if not output.exists():
        return
    if not output.is_dir():
        raise ConversionError("--output exists but is not a directory")
    if any(output.iterdir()):
        manifest_path = output / MANIFEST_NAME
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise ConversionError(
                "--clean refuses to delete a non-empty directory without this tool's manifest"
            ) from error
        if manifest.get("generator") not in (
                "tools/convert_assets.py", RUNTIME_GENERATOR, SOUND_EFFECTS_GENERATOR,
                STREAMED_MEDIA_GENERATOR, CINEMATICS_GENERATOR):
            raise ConversionError("--clean refuses to delete a directory with an unrecognized manifest")
    shutil.rmtree(output)


def classify_source(path: Path, data_prefix: bytes, text: bytes | None) -> tuple[str, str]:
    suffix = path.suffix.lower()
    if data_prefix.startswith(b"FORM") and data_prefix[8:12] in (b"AIFF", b"AIFC"):
        return "audio/aiff", "aiff-container"
    if data_prefix.startswith(b"SHDR"):
        return "application/x-3do-stream", "3do-cinepak-stream"
    if data_prefix.startswith(b"CCB ") and data_prefix[80:84] == b"PDAT":
        return "application/x-3do-cel", "3do-cel"
    if suffix == ".pcx" and len(data_prefix) >= 4 and data_prefix[:4] == b"\x0A\x05\x01\x08":
        return "image/x-pcx", "pcx-container"
    if data_prefix.startswith(b"8BPS\x00\x01"):
        return "image/vnd.adobe.photoshop", "photoshop-psd-container"
    if suffix == ".pal":
        return "application/x-3do-palette", "rgb555be-palette"
    if suffix == ".eps":
        return "application/postscript", "eps"
    if text is not None:
        return "text/plain; charset=utf-8", "text"
    return "application/octet-stream", "unknown-raw"


def decoder_requirement(source_root_name: str, relative: Path, source_type: str,
                        conversion_note: str | None, output_count: int) -> str | None:
    if source_type == "aiff-container" and conversion_note:
        return "Implement and verify the AIFC SDX2 (Square Root Delta Exact) codec, then transcode to WAV or Ogg."
    if source_type == "3do-cinepak-stream":
        return "Demux the 3DO SHDR stream and decode its FILM/Cinepak chunks before WebM/MP4 transcode."
    if source_type == "3do-cel" and conversion_note:
        return "Add a verified decoder for this CEL's unsupported bpp/preamble command variant; retain CCB, PDAT, and PLUT validation."
    if source_type == "pcx-container":
        return "Verify the PCX encoding parameters and palette placement, then decode this PCX variant to PNG."
    if source_type == "photoshop-psd-container":
        return "Verify the Photoshop PSD channel/compression layout, then flatten it to PNG."
    if source_root_name == "SF_Resources" and relative.parts[0] == "Cels" and conversion_note:
        return "Document the exceptional grouped-CEL descriptor or packed-row variant, then decode only after each record validates."
    if output_count > 1:
        return None
    top_level = relative.parts[0]
    requirements = {
        "Animations": "Document the binary animation-table layout emitted by Utility/GetAnim.c.",
        "Data": "Map this binary table to its consuming runtime structure before assigning an element type.",
        "Fonts": "Establish glyph metrics, atlas encoding, and palette association before producing web fonts or textures.",
        "Graphics": "Establish the packed graphics-record schema and all record offsets from its runtime consumer.",
        "Info": "Establish the binary planet-info structure and byte order from its runtime consumer.",
        "Missions": "Establish this mission/map record schema from its runtime consumer before decoding.",
        "Sky": "Establish the sky-gradient/table structure and colour interpretation from its runtime consumer.",
        "Video": "Demux the 3DO SHDR stream and decode its FILM/Cinepak chunks before WebM/MP4 transcode.",
    }
    if source_root_name == "3DO cels":
        return "Identify the source artwork/container format; no checked-in converter defines this file layout."
    return requirements.get(top_level, "Identify this binary format from a checked-in producer or consuming runtime before decoding.")


def append_conversion(
    outputs: list[dict], output_root: Path, path: str, data: bytes, mime: str, asset_type: str,
    metadata: dict, manifest_only: bool
) -> None:
    entry = write_bytes(output_root, path, data, manifest_only)
    entry.update({"mime": mime, "type": asset_type})
    entry.update(metadata)
    outputs.append(entry)


def append_indexed_cel_outputs(
    outputs: list[dict], output_root: Path, source_root_name: str, relative: Path, name: str,
    indices: list[int], width: int, height: int, palette: list[tuple[int, int, int, int]],
    encoding: str, manifest_only: bool
) -> None:
    image_path = output_path("images", source_root_name, relative, f"/{name}.png")
    palette_path = output_path("palettes", source_root_name, relative, f"/{name}.palette.json")
    append_conversion(
        outputs, output_root, palette_path, palette_metadata_json(palette, "rgb555be"),
        "application/json", "indexed-cel-palette", {"entries": len(palette)}, manifest_only
    )
    append_conversion(
        outputs, output_root, image_path, indexed_to_png(indices, width, height, palette),
        "image/png", encoding,
        {
            "width": width,
            "height": height,
            "palette": palette_path,
            "row_order": "source-first-row",
        },
        manifest_only,
    )


def append_grouped_indexed_outputs(
    outputs: list[dict], output_root: Path, source_root_name: str, relative: Path, data: bytes,
    parser, manifest_only: bool
) -> list[str]:
    errors = []
    for index, record in enumerate(grouped_records(data)):
        try:
            indices, width, height, palette, encoding = parser(record)
            append_indexed_cel_outputs(
                outputs, output_root, source_root_name, relative, f"{index:04d}",
                indices, width, height, palette, encoding, manifest_only
            )
        except ConversionError as error:
            errors.append(f"record {index}: {error}")
    return errors


def append_grouped_game_outputs(
    outputs: list[dict], output_root: Path, source_root_name: str, relative: Path, data: bytes,
    manifest_only: bool
) -> list[str]:
    errors = []
    for index, record in enumerate(grouped_records(data)):
        try:
            flags = struct.unpack(">I", record[4:8])[0]
            if flags & CCB_LDPLUT:
                indices, width, height, palette, encoding = parse_grouped_game(record)
                append_indexed_cel_outputs(
                    outputs, output_root, source_root_name, relative, f"{index:04d}",
                    indices, width, height, palette, encoding, manifest_only
                )
            else:
                png, width, height = parse_grouped_game_direct(record)
                append_conversion(
                    outputs, output_root,
                    output_path("images", source_root_name, relative, f"/{index:04d}.png"),
                    png, "image/png", "rgb555be-png",
                    {"width": width, "height": height, "row_order": "source-first-row"},
                    manifest_only,
                )
        except (ConversionError, struct.error) as error:
            errors.append(f"record {index}: {error}")
    return errors


def font_atlas_pixels(data: bytes) -> tuple[bytes, int, int, dict[str, int]]:
    header_size = 84
    columns = 16
    if len(data) < header_size:
        raise ConversionError("Message font is smaller than its header")
    read_word = lambda offset: struct.unpack_from(">I", data, offset)[0]
    chunk_id = read_word(0)
    chunk_size = read_word(4)
    char_height = read_word(16)
    char_width = read_word(20)
    bits_per_pixel = read_word(24)
    first_char = read_word(28)
    last_char = read_word(32)
    char_extra = read_word(36)
    leading = read_word(48)
    char_info_offset = read_word(52)
    char_info_size = read_word(56)
    char_data_offset = read_word(60)
    char_data_size = read_word(64)
    if (chunk_id != int.from_bytes(b"FONT", "big") or chunk_size != len(data) or
            bits_per_pixel != 5 or char_height == 0 or char_width == 0 or
            first_char > last_char or
            char_info_size < (last_char - first_char + 1) * 4 or
            char_info_offset > len(data) or
            char_info_size > len(data) - char_info_offset or
            char_data_offset > len(data) or
            char_data_size > len(data) - char_data_offset):
        raise ConversionError("Message font header is invalid")
    glyph_count = last_char - first_char + 1
    atlas_width = char_width * columns
    atlas_height = char_height * ((glyph_count + columns - 1) // columns)
    pixels = bytearray(atlas_width * atlas_height * 4)
    glyph_widths = [
        read_word(char_info_offset + index * 4) & 0xFF
        for index in range(glyph_count)
    ]

    def write_pixel(base_x: int, base_y: int, x: int, y: int, value: int) -> None:
        if x >= char_width or y >= char_height or value == 0:
            return
        offset = ((base_y + y) * atlas_width + base_x + x) * 4
        pixels[offset] = value & 7
        pixels[offset + 1] = value >> 3
        pixels[offset + 3] = 255

    for index in range(glyph_count):
        info = read_word(char_info_offset + index * 4)
        glyph_offset = info >> 10
        glyph_width = info & 0xFF
        next_offset = (read_word(char_info_offset + (index + 1) * 4) >> 10
                       if index + 1 < glyph_count else char_data_size)
        if glyph_width == 0 or glyph_offset > next_offset or next_offset > char_data_size:
            continue
        base_x = (index % columns) * char_width
        base_y = (index // columns) * char_height
        source_offset = char_data_offset + glyph_offset
        source_end = char_data_offset + next_offset
        x = y = 0

        def advance() -> bool:
            nonlocal x, y
            x += 1
            if x == glyph_width:
                x = 0
                y += 1
                return True
            return False

        while y < char_height and source_offset + 1 < source_end:
            packet = struct.unpack_from(">H", data, source_offset)[0]
            source_offset += 2
            if packet & 0x8000:
                for shift in (10, 5, 0):
                    write_pixel(base_x, base_y, x, y, (packet >> shift) & 31)
                    advance()
                    if y == char_height:
                        break
            elif packet & 0xF000 == 0x4000:
                y += packet & 0xFF
                x = 0
            elif packet & 0xF000 == 0x2000:
                for _ in range((packet >> 5) & 31):
                    if y == char_height:
                        break
                    write_pixel(base_x, base_y, x, y, packet & 31)
                    advance()
            elif packet & 0xF000 == 0x1000:
                line_delta = (packet & 7) + 1
                for _ in range((packet >> 3) & 31):
                    if y == char_height:
                        break
                    source_y = y - line_delta
                    if x < char_width and source_y >= 0:
                        source = ((base_y + source_y) * atlas_width + base_x + x) * 4
                        destination = ((base_y + y) * atlas_width + base_x + x) * 4
                        pixels[destination:destination + 4] = pixels[source:source + 4]
                    advance()
            elif packet == 0:
                x = 0
                y += 1
            else:
                for shift in (5, 0):
                    write_pixel(base_x, base_y, x, y, (packet >> shift) & 31)
                    if advance() or y == char_height:
                        break
    return bytes(pixels), atlas_width, atlas_height, {
        "char_height": char_height,
        "char_width": char_width,
        "char_extra": char_extra,
        "first_char": first_char,
        "last_char": last_char,
        "leading": leading,
        "glyph_widths": glyph_widths,
    }


def font_glyph_sprites(pixels: bytes, atlas_width: int, metadata: dict[str, int]) -> \
        dict[int, tuple[bytes, int, int]]:
    """Extract each rendered glyph from the legacy fixed-grid font atlas."""
    scale = UI_FONT_TEXTURE_SCALE
    glyphs = {}
    glyph_height = metadata["char_height"] * scale
    cell_width = metadata["char_width"] * scale
    for index, advance in enumerate(metadata["glyph_widths"]):
        glyph_width = advance * scale
        source_x = (index % 16) * cell_width
        source_y = (index // 16) * glyph_height
        glyph = bytearray(glyph_width * glyph_height * 4)
        for row in range(glyph_height):
            source = ((source_y + row) * atlas_width + source_x) * 4
            destination = row * glyph_width * 4
            glyph[destination:destination + glyph_width * 4] = \
                pixels[source:source + glyph_width * 4]
        glyphs[index] = (bytes(glyph), glyph_width, glyph_height)
    return glyphs


def write_ui_texture(output: Path, relative: Path, data: bytes, refresh: bool) -> dict:
    """Preserve editable PNGs, but always refresh generated metadata and binary data."""
    destination = output / relative
    if refresh or not destination.exists() or destination.suffix.lower() != ".png":
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    return {
        "path": relative.as_posix(),
        "sha256": sha256_file(destination),
        "size": destination.stat().st_size,
    }


def write_content_addressed_ui_binary(
    output: Path, family: Path, data: bytes, schema: str, source: Path, refresh: bool
) -> dict:
    """Write a normalized compatibility payload once, addressed by its final bytes."""
    entry = write_ui_texture(
        output, family / f"{sha256_bytes(data)}.bin", data, refresh
    )
    entry.update({
        "byte_length": len(data),
        "schema": schema,
        "source_path": source.as_posix(),
        "source_sha256": sha256_file(source),
    })
    return entry


def world_atlas_set(
    output: Path, planet: str, category: str,
    sprites: dict[int, tuple[bytes, int, int]], refresh: bool,
    fixed_grid: bool = False
) -> tuple[list[dict], dict[str, dict], dict | None]:
    """Write compact fixed grids or transparent-guttered variable-size atlas pages."""
    if fixed_grid:
        dimensions = {(width, height) for _pixels, width, height in sprites.values()}
        if len(dimensions) != 1:
            raise ConversionError(f"Cels/{planet}.{category} is not fixed-size")
        tile_width, tile_height = dimensions.pop()
        tile_count = max(sprites) + 1
        rows = (tile_count + WORLD_FIXED_CEL_GRID_COLUMNS - 1) // \
            WORLD_FIXED_CEL_GRID_COLUMNS
        width = WORLD_FIXED_CEL_GRID_COLUMNS * tile_width
        height = rows * tile_height
        pixels = bytearray(width * height * 4)
        for index, (tile, source_width, source_height) in sprites.items():
            if source_width != tile_width or source_height != tile_height:
                raise ConversionError(f"Cels/{planet}.{category} grid tile dimensions differ")
            destination_x = (index % WORLD_FIXED_CEL_GRID_COLUMNS) * tile_width
            destination_y = (index // WORLD_FIXED_CEL_GRID_COLUMNS) * tile_height
            for row in range(tile_height):
                source = row * tile_width * 4
                destination = ((destination_y + row) * width + destination_x) * 4
                pixels[destination:destination + tile_width * 4] = \
                    tile[source:source + tile_width * 4]
        texture = write_ui_texture(
            output, Path("world") / planet / category / "atlas-00.png",
            png_rgba(width, height, bytes(pixels)), refresh
        )
        texture.update({
            "page": 0,
            "width": width,
            "height": height,
            "padding": 0,
        })
        return [texture], {}, {
            "atlas": texture["path"],
            "columns": WORLD_FIXED_CEL_GRID_COLUMNS,
            "tile_width": tile_width,
            "tile_height": tile_height,
            "tile_count": tile_count,
        }

    atlases = []
    regions = {}
    for page, (pixels, width, height, packed_regions) in enumerate(
        pack_ui_sprite_atlas_pages(sprites)
    ):
        texture = write_ui_texture(
            output, Path("world") / planet / category / f"atlas-{page:02d}.png",
            png_rgba(width, height, pixels), refresh
        )
        texture.update({
            "page": page,
            "width": width,
            "height": height,
            "padding": UI_GAME_ATLAS_PADDING,
        })
        atlases.append(texture)
        for index, region in packed_regions.items():
            regions[index] = {
                "atlas": texture["path"],
                "page": page,
                **region,
            }
    return atlases, regions, None


def world_collision_palette_indices(graphics_path: Path) -> set[int]:
    """Return the .32 palette records selected by collision-driven debris."""
    data = graphics_path.read_bytes()
    if len(data) < 12:
        raise ConversionError(f"Graphics/{graphics_path.name} is too short")
    groups = zip(struct.unpack_from(">III", data), (64, 128, 16))
    palette_indices = set()
    for base, record_count in groups:
        require_range(data, base, record_count * 64, "graphics record group")
        for record in range(record_count):
            offset = base + record * 64
            collision_offset = struct.unpack_from(">I", data, offset + 16)[0]
            if collision_offset == 0:
                continue
            collision_start = base + collision_offset
            require_range(data, collision_start, 12, "collision header")
            collision_count = struct.unpack_from(">i", data, collision_start)[0]
            if collision_count < -1:
                raise ConversionError("graphics collision count is invalid")
            require_range(data, collision_start + 12, (collision_count + 1) * 28,
                          "collision records")
            for collision in range(collision_count + 1):
                palette_indices.add(data[collision_start + 13 + collision * 28])
    return palette_indices


def build_world_texture_sets(raw_root: Path, output: Path, refresh: bool) -> tuple[dict, dict]:
    """Build shared and active-world RGBA atlases for grouped planet CELs.

    Static materials retain their legacy local texture IDs in the manifest, but
    each ID resolves to a deduplicated final RGBA tile.  Debris palette
    overrides are emitted as their own pre-baked material variants.
    """
    cels_root = raw_root / "SF_Resources" / "Cels"
    graphics_root = raw_root / "SF_Resources" / "Graphics"
    world_sets = {}
    material_references = {}
    tiles = {}

    def register_tile(category: str, pixels: bytes, width: int, height: int,
                      world: str) -> tuple[str, int, int, str]:
        key = (category, width, height, sha256_bytes(pixels))
        if key not in tiles:
            tiles[key] = {"pixels": pixels, "width": width, "height": height,
                          "worlds": set()}
        tiles[key]["worlds"].add(world)
        return key

    for source in sorted(
        (path for path in cels_root.iterdir() if path.is_file() and path.suffix in (".16", ".32")),
        key=lambda path: path.name
    ):
        planet = source.stem
        category = source.suffix[1:]
        errors = []
        sprites = {}
        try:
            parser = parse_grouped_16 if category == "16" else parse_grouped_32
            for index, record in enumerate(grouped_records(source.read_bytes())):
                try:
                    indices, width, height, palette, _encoding = parser(record)
                    pixels = rgba_pixels_from_indices(indices, width, height, palette, False)
                    if category == "32":
                        pixels, width, height = scale_world_cel_32(pixels, width, height)
                    flags = struct.unpack(">I", record[0:4] if category == "16"
                                          else record[4:8])[0]
                    transparency = 0 if category == "16" else \
                        struct.unpack(">I", record[0:4])[0] >> 8
                    sprites[index] = (indices, palette, pixels, width, height,
                                      flags, transparency)
                except (ConversionError, struct.error) as error:
                    errors.append(f"{index}: {error}")
        except (ConversionError, struct.error) as error:
            errors.append(f"source: {error}")

        world_category = {
            "source_path": f"Cels/{source.name}",
            "source_sha256": sha256_file(source),
            "unconverted": errors,
        }
        materials = [None] * (max(sprites, default=-1) + 1)
        for index, (_indices, _palette, pixels, width, height,
                    _flags, _transparency) in sprites.items():
            material_references[(planet, category, index)] = register_tile(
                category, pixels, width, height, planet
            )
            materials[index] = index
        world_category["_materials"] = materials
        world_category["_sprites"] = sprites
        world_sets.setdefault(planet, {})[category] = world_category

    # Pre-bake each collision palette applied to the four debris source tiles.
    for world, categories in world_sets.items():
        graphics_path = graphics_root / world.upper()
        if "32" not in categories or not graphics_path.is_file():
            continue
        palette_indices = world_collision_palette_indices(graphics_path)
        sprites = categories["32"]["_sprites"]
        debris = {}
        for texture_index in range(192, 196):
            source = sprites.get(texture_index)
            if source is None:
                continue
            indices, _default_palette, _pixels, width, height, _flags, _transparency = source
            # scale_world_cel_32 normalizes every supported source CEL to 32x32.
            source_width = int(math.isqrt(len(indices)))
            if source_width * source_width != len(indices):
                raise ConversionError("debris source CEL is not square")
            variants = {}
            for palette_index in sorted(palette_indices):
                palette_source = sprites.get(palette_index)
                if palette_source is None:
                    continue
                palette = palette_source[1]
                pixels = rgba_pixels_from_indices(
                    indices, source_width, source_width, palette, False
                )
                pixels, variant_width, variant_height = scale_world_cel_32(
                    pixels, source_width, source_width
                )
                variants[str(palette_index)] = register_tile(
                    "32", pixels, variant_width, variant_height, world
                )
            if variants:
                debris[str(texture_index)] = variants
        categories["32"]["_debris"] = debris

    shared_atlases = {}
    tile_locations = {}
    for category in ("16", "32"):
        shared_keys = sorted(
            (key for key, tile in tiles.items()
             if key[0] == category and len(tile["worlds"]) > 1),
            key=lambda key: key[3]
        )
        shared_sprites = {
            slot: (tiles[key]["pixels"], tiles[key]["width"], tiles[key]["height"])
            for slot, key in enumerate(shared_keys)
        }
        for slot, key in enumerate(shared_keys):
            tile_locations[key] = {"scope": "shared", "slot": slot}
        atlases, _regions, grid = world_atlas_set(
            output, "shared", category, shared_sprites, refresh, fixed_grid=True
        ) if shared_sprites else ([], {}, None)
        shared_atlases[category] = {"atlases": atlases, "grid": grid}

    for world, categories in world_sets.items():
        for category, values in categories.items():
            local_keys = sorted(
                (key for key, tile in tiles.items()
                 if key[0] == category and tile["worlds"] == {world}),
                key=lambda key: key[3]
            )
            local_sprites = {
                slot: (tiles[key]["pixels"], tiles[key]["width"], tiles[key]["height"])
                for slot, key in enumerate(local_keys)
            }
            for slot, key in enumerate(local_keys):
                tile_locations[key] = {"scope": "world", "slot": slot}
            atlases, _regions, grid = world_atlas_set(
                output, world, category, local_sprites, refresh, fixed_grid=True
            ) if local_sprites else ([], {}, None)
            values["atlases"] = atlases
            values["grid"] = grid
            values["materials"] = [
                None if reference is None else tile_locations[
                    material_references[(world, category, reference)]
                ]
                for reference in values.pop("_materials")
            ]
            values["debris_materials"] = {
                texture: {
                    palette: tile_locations[reference]
                    for palette, reference in variants.items()
                }
                for texture, variants in values.pop("_debris", {}).items()
            }

    for world, categories in world_sets.items():
        descriptors = bytearray(struct.pack("<4sIHH", b"SFCM", 1, 256, 256))
        for category in ("16", "32"):
            sprites = categories.get(category, {}).get("_sprites", {})
            for index in range(256):
                sprite = sprites.get(index)
                if sprite is None:
                    descriptors.extend(struct.pack("<IB3x", 0, 0))
                else:
                    descriptors.extend(struct.pack("<IB3x", sprite[5], sprite[6] + 1))
        descriptor = write_ui_texture(
            output, Path("world") / world / "materials.bin",
            bytes(descriptors), refresh
        )
        descriptor.update({
            "byte_length": len(descriptors),
            "schema": "static-cel-materials-v1",
        })
        categories["material_descriptors"] = descriptor
        for category in ("16", "32"):
            if category in categories:
                categories[category].pop("_sprites")

    return world_sets, shared_atlases
    return world_sets


def build_sky_textures(raw_root: Path, output: Path, refresh: bool) -> dict:
    sky_root = raw_root / "SF_Resources" / "Sky"
    skies = {}
    for source in sorted((path for path in sky_root.iterdir() if path.is_file()),
                         key=lambda path: path.name):
        data = source.read_bytes()
        pixels = sky_rgba_pixels(data)
        texture = write_ui_texture(
            output, Path("sky") / f"{source.name}.png", png_rgba(SKY_COLOUR_COUNT, 1, pixels),
            refresh
        )
        texture.update({
            "width": SKY_COLOUR_COUNT,
            "height": 1,
            "source_sha256": sha256_file(source),
            "legacy_data": write_content_addressed_ui_binary(
                output, Path("compatibility") / "sky", data, "identity-copy-v1",
                source, refresh
            ),
        })
        skies[source.name] = texture
    return skies


def build_backdrop_textures(raw_root: Path, output: Path, refresh: bool) -> dict:
    images_root = raw_root / "SF_Resources" / "Images"
    backdrops = {}
    for source in sorted((path for path in images_root.iterdir() if path.is_file()),
                         key=lambda path: path.name):
        pixels = backdrop_rgba_pixels(source.read_bytes())
        texture = write_ui_texture(
            output, Path("backdrops") / f"{source.name}.png", png_rgba(320, 240, pixels),
            refresh
        )
        texture.update({"width": 320, "height": 240, "source_sha256": sha256_file(source)})
        backdrops[source.name] = texture
    return backdrops


def build_map_textures(raw_root: Path, output: Path, refresh: bool) -> dict:
    maps_root = raw_root / "SF_Resources" / "Missions" / "Maps"
    map_directories = sorted({
        source.parent for source in source_files(maps_root)
        if source.name in ("H_MAP", "P_MAP", "S_MAP")
    }, key=lambda path: path.relative_to(maps_root).as_posix())
    maps = {}
    for directory in map_directories:
        identity = directory.relative_to(maps_root).as_posix()
        entry = {"height": None, "polygon": None, "tile": None, "unconverted": []}
        for source_name, output_name, manifest_name, dimension in (
            ("H_MAP", "height", "height", MAP_DIMENSION),
            ("P_MAP", "polygon", "polygon", POLY_MAP_DIMENSION),
            ("S_MAP", "tile", "tile", MAP_DIMENSION),
        ):
            source = directory / source_name
            if not source.is_file():
                entry["unconverted"].append(f"{source_name}: missing")
                continue
            try:
                source_data = source.read_bytes()
                map_bytes = dimension * dimension
                pixels = map_rgba_pixels(source_data, dimension)
                texture = write_ui_texture(
                    output, Path("maps") / directory.relative_to(maps_root) /
                    f"{output_name}.png", png_rgba(dimension, dimension, pixels), refresh
                )
                texture.update({
                    "width": dimension,
                    "height": dimension,
                    "source_path": f"Missions/Maps/{identity}/{source_name}",
                    "source_sha256": sha256_file(source),
                    "source_size": map_bytes,
                })
                entry[manifest_name] = texture
            except ConversionError as error:
                entry["unconverted"].append(f"{source_name}: {error}")
        maps[identity] = entry
    return maps


def build_mission_records(raw_root: Path, output: Path, refresh: bool) -> dict:
    missions_root = raw_root / "SF_Resources" / "Missions"
    missions = {}
    for source in sorted(
        (path for path in source_files(missions_root)
         if len(path.relative_to(missions_root).parts) == 2 and
         path.name.startswith("MISS_")),
        key=lambda path: path.relative_to(missions_root).as_posix()
    ):
        level, mission_name = source.relative_to(missions_root).parts
        mission_number = mission_name.removeprefix("MISS_")
        normalized, schema, _normalized_words = normalize_mission(source.read_bytes())
        asset = write_ui_texture(
            output, Path("missions") / level / f"{mission_name}.bin", normalized, refresh
        )
        asset.update({
            "byte_length": len(normalized),
            "schema": schema,
            "source_path": f"Missions/{level}/{mission_name}",
            "source_sha256": sha256_file(source),
        })
        missions[f"{level}/{mission_number}"] = asset
    return missions


def build_ui_texture_manifest(raw_root: Path, output: Path, refresh: bool) -> dict:
    cels_root = raw_root / "SF_Resources" / "Cels"
    font_path = raw_root / "SF_Resources" / "Fonts" / "Message"
    required_directories = (
        raw_root / "SF_Resources" / "Palettes",
        raw_root / "SF_Resources" / "Sky",
        raw_root / "SF_Resources" / "Images",
        raw_root / "SF_Resources" / "Missions" / "Maps",
        raw_root / "SF_Resources" / "Text",
        raw_root / "SF_Resources" / "Info",
        raw_root / "SF_Resources" / "Animations",
    )
    if not cels_root.is_dir() or not font_path.is_file() or not all(
            directory.is_dir() for directory in required_directories):
        raise ConversionError(
            "raw asset root does not contain required UI, world, sky, backdrop, and map resources"
        )
    stale_alphabet_sprites = output / "game" / "Alphabet"
    if stale_alphabet_sprites.is_dir():
        for stale in stale_alphabet_sprites.iterdir():
            if stale.is_file():
                stale.unlink()
        stale_alphabet_sprites.rmdir()

    game_sets = {}
    game_atlas_sprites: dict[str, tuple[bytes, int, int]] = {}
    game_atlas_sprite_keys: dict[str, str] = {}
    alphabet_sprites: dict[int, tuple[bytes, int, int]] = {}
    alphabet_draw_sizes: dict[int, tuple[int, int]] = {}
    for source in sorted(cels_root.glob("Game.*")):
        legacy_data, legacy_schema, _legacy_words = normalize_cel_offsets(
            source.read_bytes()
        )
        sprites = {}
        errors = []
        palette_records = []
        for index, record in enumerate(grouped_records(source.read_bytes())):
            try:
                pixc = struct.unpack(">I", record[:4])[0]
                flags = struct.unpack(">I", record[4:8])[0]
                source_offset = 72 if flags & CCB_LDPLUT else 8
                if len(record) < source_offset + 4:
                    raise ConversionError("grouped game CEL record is too short")
                bits_per_pixel = record[source_offset + 3] & 7
                if index in GAME_CEL_PALETTE_RECORDS and len(record) == 64:
                    palette_records.append(index)
                    continue
                if not game_cel_pmode_is_equivalent(pixc, flags):
                    raise ConversionError(
                        "Game CEL uses non-equivalent P-modes and cannot be baked to RGBA"
                    )
                if flags & CCB_LDPLUT and bits_per_pixel in CEL_BPP:
                    indices, width, height, palette, _encoding = parse_grouped_game(record)
                    pixels = rgba_pixels_from_indices(
                        indices, width, height, palette, True, bits_per_pixel >= 4
                    )
                else:
                    values, width, height = parse_grouped_game_direct_values(record)
                    pixels = rgba_pixels_from_direct_values(
                        values, width, height,
                        bool(flags & CCB_PACKED) or
                        not bool(flags & CCB_BACKGROUND_ZERO_OPAQUE)
                    )
                pixels = normalise_game_pmode_alpha(pixels)
                if source.name == "Game.Pyramid" and index < 16:
                    pixels = unpremultiply_rgba(matte_pyramid_planet(pixels, width, height))
                scaled, scaled_width, scaled_height = scale_game_sprite(
                    pixels, width, height
                )
                if source.name == "Game.Alphabet":
                    alphabet_sprites[index] = (scaled, scaled_width, scaled_height)
                    alphabet_draw_sizes[index] = (width, height)
                    continue
                atlas_key = f"{sha256_bytes(scaled)}:{scaled_width}x{scaled_height}"
                game_atlas_sprites.setdefault(
                    atlas_key, (scaled, scaled_width, scaled_height)
                )
                game_atlas_sprite_keys[f"{source.name[5:]}:{index}"] = atlas_key
                entry = write_ui_texture(
                    output, Path("game") / source.name[5:] / f"{index:04d}.png",
                    png_rgba(scaled_width, scaled_height, scaled), refresh
                )
                entry.update({"width": scaled_width, "height": scaled_height})
                sprites[str(index)] = entry
            except (ConversionError, struct.error) as error:
                errors.append(f"{index}: {error}")
        game_set = {
            "source_sha256": sha256_file(source),
            "sprites": sprites,
            "palette_records": palette_records,
            "unconverted": errors,
        }
        if source.name != "Game.Alphabet":
            game_set["legacy_data"] = write_content_addressed_ui_binary(
                output, Path("compatibility") / "game-cels", legacy_data,
                legacy_schema, source, refresh
            )
        game_sets[source.name[5:]] = game_set

    game_atlases = []
    for page, (pixels, width, height, regions) in enumerate(
        pack_ui_sprite_atlas_pages(game_atlas_sprites)
    ):
        sprite_regions = {
            sprite: regions[atlas_key]
            for sprite, atlas_key in game_atlas_sprite_keys.items()
            if atlas_key in regions
        }
        game_atlas = write_ui_texture(
            output, Path("game") / f"Game-atlas-{page:02d}.png",
            png_rgba(width, height, pixels), refresh
        )
        game_atlas.update({
            "width": width,
            "height": height,
            "padding": UI_GAME_ATLAS_PADDING,
            "sprites": sprite_regions,
        })
        game_atlases.append(game_atlas)

    font_pixels, font_width, font_height, font_metadata = font_atlas_pixels(
        font_path.read_bytes()
    )
    glyph_widths = font_metadata.pop("glyph_widths")
    font_scaled, font_scaled_width, font_scaled_height = scale_font_atlas(
        font_pixels, font_width, font_height,
        font_metadata["char_width"], font_metadata["char_height"]
    )
    message_sprites = font_glyph_sprites(font_scaled, font_scaled_width, {
        **font_metadata,
        "glyph_widths": glyph_widths,
    })
    message_pixels, message_width, message_height, message_regions = \
        pack_ui_sprite_atlas(message_sprites, UI_FONT_ATLAS_WIDTH)
    message_font = write_ui_texture(
        output, Path("font") / "Message.png",
        png_rgba(message_width, message_height, message_pixels), refresh
    )
    message_font.update({
        "width": message_width,
        "height": message_height,
        "source_sha256": sha256_file(font_path),
    })
    message_metrics = {
        "schema": "font-metrics-v1",
        "render_style": "text-weights",
        "image": message_font["path"],
        "atlas_width": message_width,
        "atlas_height": message_height,
        "texture_scale": UI_FONT_TEXTURE_SCALE,
        **font_metadata,
        "glyphs": {
            str(font_metadata["first_char"] + index): {
                "source_rect": [
                    message_regions[str(index)]["x"],
                    message_regions[str(index)]["y"],
                    message_regions[str(index)]["width"],
                    message_regions[str(index)]["height"],
                ],
                "advance": advance,
                "draw_width": advance,
                "draw_height": font_metadata["char_height"],
            }
            for index, advance in enumerate(glyph_widths)
        },
    }
    message_metrics_data = (
        json.dumps(message_metrics, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")
    message_metrics_entry = write_ui_texture(
        output, Path("font") / "Message.metrics.json", message_metrics_data, refresh
    )
    message_metrics_entry.update({
        "byte_length": len(message_metrics_data),
        "schema": "font-metrics-v1",
    })
    message_font["metrics"] = message_metrics_entry

    fonts = {"Message": message_font}
    if alphabet_sprites:
        if len(alphabet_sprites) != 40 or len(ALPHABET_GLYPH_ADVANCES) != 39:
            raise ConversionError("Game.Alphabet glyph inventory is invalid")
        alphabet_pixels, alphabet_width, alphabet_height, alphabet_regions = \
            pack_ui_sprite_atlas(alphabet_sprites, UI_FONT_ATLAS_WIDTH)
        alphabet_font = write_ui_texture(
            output, Path("font") / "Alphabet.png",
            png_rgba(alphabet_width, alphabet_height, alphabet_pixels), refresh
        )
        alphabet_font.update({"width": alphabet_width, "height": alphabet_height})
        alphabet_metrics = {
            "schema": "font-metrics-v1",
            "render_style": "texture-coverage",
            "image": alphabet_font["path"],
            "atlas_width": alphabet_width,
            "atlas_height": alphabet_height,
            "texture_scale": UI_FONT_TEXTURE_SCALE,
            "glyphs": {
                str(index): {
                    "source_rect": [
                        alphabet_regions[str(index)]["x"],
                        alphabet_regions[str(index)]["y"],
                        alphabet_regions[str(index)]["width"],
                        alphabet_regions[str(index)]["height"],
                    ],
                    "advance": ALPHABET_GLYPH_ADVANCES[index]
                    if index < len(ALPHABET_GLYPH_ADVANCES) else 0,
                    "draw_width": alphabet_draw_sizes[index][0],
                    "draw_height": alphabet_draw_sizes[index][1],
                }
                for index in sorted(alphabet_sprites)
            },
        }
        alphabet_metrics_data = (
            json.dumps(alphabet_metrics, indent=2, sort_keys=True) + "\n"
        ).encode("utf-8")
        alphabet_metrics_entry = write_ui_texture(
            output, Path("font") / "Alphabet.metrics.json", alphabet_metrics_data, refresh
        )
        alphabet_metrics_entry.update({
            "byte_length": len(alphabet_metrics_data),
            "schema": "font-metrics-v1",
        })
        alphabet_font["metrics"] = alphabet_metrics_entry
        fonts["Alphabet"] = alphabet_font
    world_sets, shared_world_atlases = build_world_texture_sets(raw_root, output, refresh)
    return {
        "schema_version": 1,
        "generator": UI_TEXTURES_GENERATOR,
        "schema_paths": UI_JSON_SCHEMA_PATHS,
        "game_sets": game_sets,
        "game_atlases": game_atlases,
        "fonts": fonts,
        "world_sets": world_sets,
        "shared_world_atlases": shared_world_atlases,
        "world_graphics": build_world_graphics_library(raw_root, output, refresh),
        "compatibility": build_legacy_palette_assets(raw_root, output, refresh),
        "sky": build_sky_textures(raw_root, output, refresh),
        "backdrops": build_backdrop_textures(raw_root, output, refresh),
        "maps": build_map_textures(raw_root, output, refresh),
        "missions": build_mission_records(raw_root, output, refresh),
        "text": build_text_assets(raw_root, output, refresh),
        "configuration": build_configuration_asset(raw_root, output, refresh),
        "world_metadata": build_world_metadata_asset(raw_root, output, refresh),
        "texture_animations": build_texture_animations(raw_root, output, refresh),
    }


def append_grouped_tile_outputs(
    outputs: list[dict], output_root: Path, source_root_name: str, relative: Path, data: bytes,
    source_root: Path, manifest_only: bool
) -> list[str]:
    palette_source = source_root / "Palettes" / f"{relative.stem}.pal"
    if not palette_source.is_file():
        return [f"missing 32-entry palette {palette_source.relative_to(source_root)}"]
    try:
        palette = tile_palette(rgb555_entries(palette_source.read_bytes(), 32))
        palette_path = output_path("palettes", source_root_name, Path("Palettes") / palette_source.name, ".json")
        for index, tile in enumerate(parse_tiles_4bpp(data)):
            append_conversion(
                outputs, output_root,
                output_path("images", source_root_name, relative, f"/{index:04d}.png"),
                indexed_to_png(tile, 4, 4, palette), "image/png", "indexed-6bpp-tile",
                {
                    "width": 4,
                    "height": 4,
                    "palette": palette_path,
                    "row_order": "source-first-row",
                },
                manifest_only,
            )
    except ConversionError as error:
        return [str(error)]
    return []


def conversion_summary(errors: list[str]) -> str | None:
    if not errors:
        return None
    return f"{len(errors)} records preserved raw; {errors[0]}"


def require_range(data: bytes | bytearray, offset: int, length: int, description: str) -> None:
    if offset < 0 or length < 0 or offset > len(data) - length:
        raise ConversionError(f"{description} exceeds its file bounds")


def store_le32_from_be(data: bytearray, offset: int, normalized_words: set[int]) -> int:
    require_range(data, offset, 4, "32-bit field")
    value = struct.unpack_from(">I", data, offset)[0]
    struct.pack_into("<I", data, offset, value)
    normalized_words.add(offset // 4)
    return value


def byte_ranges(words: set[int]) -> list[dict[str, int]]:
    if not words:
        return []
    ranges = []
    first = previous = min(words)
    for word in sorted(words):
        if word == first:
            continue
        if word != previous + 1:
            ranges.append({"offset": first * 4, "length": (previous - first + 1) * 4})
            first = word
        previous = word
    ranges.append({"offset": first * 4, "length": (previous - first + 1) * 4})
    return ranges


def normalize_mission(data: bytes) -> tuple[bytes, str, set[int]]:
    if len(data) != 4924:
        raise ConversionError("mission record must be exactly 4,924 bytes")
    output = bytearray(data)
    normalized_words: set[int] = set()
    for offset in (4, 8, 20, 24, 28, 40, 840, 1356, 3440):
        store_le32_from_be(output, offset, normalized_words)
    for performance in range(10):
        for word in range(19):
            store_le32_from_be(output, 44 + performance * 76 + word * 4, normalized_words)
    for path in range(8):
        store_le32_from_be(output, 1360 + path * 260, normalized_words)
    for ship in range(32):
        ship_start = 3444 + ship * 44
        for offset in (24, 28, 32, 36, 40):
            if offset in (24, 28):
                output[ship_start + offset:ship_start + offset + 4] = b"\0\0\0\0"
                normalized_words.add((ship_start + offset) // 4)
            else:
                store_le32_from_be(output, ship_start + offset, normalized_words)
    for start in (4884, 4904):
        for offset in range(start, start + 20):
            if ord("a") <= output[offset] <= ord("z"):
                output[offset] -= ord("a") - ord("A")
        normalized_words.update(range(start // 4, (start + 20) // 4))
    return bytes(output), "mission-data-v1", normalized_words


def normalize_configuration(data: bytes) -> tuple[bytes, str, set[int]]:
    if len(data) != 556:
        raise ConversionError("configuration record must be exactly 556 bytes")
    output = bytearray(data)
    normalized_words: set[int] = set()
    for offset in range(24, 48, 4):
        store_le32_from_be(output, offset, normalized_words)
    for score in range(20):
        store_le32_from_be(output, 76 + score * 24 + 20, normalized_words)
    return bytes(output), "game-configuration-v1", normalized_words


def decode_fixed_ascii(data: bytes, description: str) -> str:
    value = data.split(b"\0", 1)[0]
    try:
        text = value.decode("ascii")
    except UnicodeDecodeError as error:
        raise ConversionError(f"{description} is not ASCII") from error
    if any(ord(character) < 32 or ord(character) > 126 for character in text):
        raise ConversionError(f"{description} contains unsupported control characters")
    return text


def json_asset_entry(output: Path, relative: Path, data: bytes, schema: str,
                     source: Path, refresh: bool) -> dict:
    # JSON resources are generated compatibility contracts, never user-editable art.
    # Always update them when their serialized representation changes.
    destination = output / relative
    if not destination.exists() or destination.read_bytes() != data:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    entry = {
        "path": relative.as_posix(),
        "sha256": sha256_file(destination),
        "size": destination.stat().st_size,
    }
    entry.update({
        "byte_length": len(data),
        "schema": schema,
        "source_path": source.as_posix(),
        "source_sha256": sha256_file(source),
    })
    return entry


def parse_legacy_text(source: Path, indexed: bool) -> list[str]:
    try:
        text = source.read_text(encoding="ascii")
    except UnicodeDecodeError as error:
        raise ConversionError(f"text resource {source} is not ASCII") from error
    strings: list[str] = []
    indexed_strings: dict[int, str] = {}
    for line in text.splitlines():
        if line == "#":
            break
        if line.startswith("|"):
            continue
        if any(ord(character) < 32 or ord(character) > 126 for character in line):
            raise ConversionError(f"text resource {source} contains unsupported characters")
        if indexed:
            if line == "":
                raise ConversionError(f"text resource {source} has an empty indexed record")
            if len(line) < 4 or not line[:3].isdigit() or line[3] != " ":
                raise ConversionError(f"text resource {source} has an invalid message index")
            message_id = int(line[:3])
            if message_id > 256 or message_id in indexed_strings:
                raise ConversionError(f"text resource {source} has a duplicate or invalid message index")
            indexed_strings[message_id] = line[4:]
        else:
            strings.append(line)
    else:
        raise ConversionError(f"text resource {source} is missing its terminator")
    if indexed:
        if not indexed_strings:
            raise ConversionError(f"text resource {source} has no messages")
        strings = [indexed_strings.get(message_id, "")
                   for message_id in range(max(indexed_strings) + 1)]
    maximum_strings = {
        "Game": 128,
        "Menu": 160,
        "Title": 80,
    }.get(source.name, 128)
    if len(strings) > maximum_strings:
        raise ConversionError(
            f"text resource {source} has more than {maximum_strings} messages"
        )
    return strings


def build_text_assets(raw_root: Path, output: Path, refresh: bool) -> dict:
    text_root = raw_root / "SF_Resources" / "Text"
    if not text_root.is_dir():
        raise ConversionError("raw asset root does not contain text resources")
    assets = {}
    for source in sorted(source_files(text_root),
                         key=lambda path: path.relative_to(text_root).as_posix()):
        relative = source.relative_to(text_root)
        indexed = relative.name in ("Game", "Menu", "Title")
        strings = parse_legacy_text(source, indexed)
        data = (json.dumps(strings, indent=2, ensure_ascii=True) + "\n").encode("utf-8")
        output_relative = Path("text") / relative.parent / f"{relative.name}.json"
        assets[relative.as_posix()] = json_asset_entry(
            output, output_relative, data,
            "legacy-text-indexed-v1" if indexed else "legacy-text-lines-v1",
            source, refresh
        )
    return assets


def build_configuration_asset(raw_root: Path, output: Path, refresh: bool) -> dict:
    source = raw_root / "SF_Resources" / "Data" / "CONFIG"
    data = source.read_bytes()
    if len(data) != 556:
        raise ConversionError("configuration record must be exactly 556 bytes")
    configuration = {
        "schema": "game-configuration-v1",
        "version": data[0],
        "reserved": [data[1], data[2], data[3], data[11]],
        "language": data[4],
        "control_method": data[5],
        "music_volume": data[6],
        "sound_volume": data[7],
        "music_on": data[8],
        "sound_on": data[9],
        "video_on": data[10],
        "flight_controls": list(data[12:24]),
        "stick_bounds": {
            "x_min": struct.unpack_from(">i", data, 24)[0],
            "x_max": struct.unpack_from(">i", data, 28)[0],
            "y_min": struct.unpack_from(">i", data, 32)[0],
            "y_max": struct.unpack_from(">i", data, 36)[0],
            "z_min": struct.unpack_from(">i", data, 40)[0],
            "z_max": struct.unpack_from(">i", data, 44)[0],
        },
        "music_tracks": list(data[48:56]),
        "pilot": decode_fixed_ascii(data[56:76], "configuration pilot"),
        "high_scores": [
            [
                {
                    "name": decode_fixed_ascii(
                        data[76 + (level * 5 + entry) * 24:
                             96 + (level * 5 + entry) * 24],
                        "high-score name"
                    ),
                    "score": struct.unpack_from(">i", data,
                                                96 + (level * 5 + entry) * 24)[0],
                }
                for entry in range(5)
            ]
            for level in range(4)
        ],
    }
    json_data = (json.dumps(configuration, indent=2, sort_keys=True) + "\n").encode("utf-8")
    return json_asset_entry(output, Path("configuration-v1.json"), json_data,
                            "game-configuration-v1", source, refresh)


def build_world_metadata_asset(raw_root: Path, output: Path, refresh: bool) -> dict:
    info_root = raw_root / "SF_Resources" / "Info"
    if not info_root.is_dir():
        raise ConversionError("raw asset root does not contain world metadata")
    worlds = {}
    sources = []
    for source in sorted(info_root.glob("*.info")):
        data = source.read_bytes()
        if len(data) != 24:
            raise ConversionError(f"world metadata {source} must be exactly 24 bytes")
        world = source.stem
        worlds[world] = {
            "explosion1_data1": struct.unpack_from(">I", data, 0)[0],
            "explosion1_data2": struct.unpack_from(">I", data, 4)[0],
            "explosion2_data1": struct.unpack_from(">I", data, 8)[0],
            "explosion2_data2": struct.unpack_from(">I", data, 12)[0],
            "explosion_heightcheck": data[16],
            "space_mission": data[17],
            "reserved": list(data[18:20]),
            "comet_rate": struct.unpack_from(">I", data, 20)[0],
        }
        sources.append({
            "path": source.relative_to(raw_root / "SF_Resources").as_posix(),
            "sha256": sha256_file(source),
        })
    if not worlds:
        raise ConversionError("world metadata contains no world records")
    document = {"schema": "world-metadata-v1", "worlds": worlds}
    json_data = (json.dumps(document, indent=2, sort_keys=True) + "\n").encode("utf-8")
    entry = write_ui_texture(output, Path("world-metadata-v1.json"), json_data, refresh)
    entry.update({
        "byte_length": len(json_data),
        "schema": "world-metadata-v1",
        "sources": sources,
    })
    return entry


def normalize_word_table(data: bytes, schema: str) -> tuple[bytes, str, set[int]]:
    if len(data) == 0 or len(data) % 4:
        raise ConversionError(f"{schema} must have a non-zero multiple-of-four length")
    output = bytearray(data)
    normalized_words: set[int] = set()
    for offset in range(0, len(output), 4):
        store_le32_from_be(output, offset, normalized_words)
    return bytes(output), schema, normalized_words


def normalize_cel_offsets(data: bytes) -> tuple[bytes, str, set[int]]:
    if len(data) < 4:
        raise ConversionError("CEL container is missing its offset table")
    output = bytearray(data)
    normalized_words: set[int] = set()
    offset_bytes = struct.unpack_from(">I", output, 0)[0]
    if offset_bytes < 4 or offset_bytes > len(output) or offset_bytes % 4:
        raise ConversionError("CEL container has an invalid offset table length")
    for offset in range(0, offset_bytes, 4):
        value = struct.unpack_from(">I", output, offset)[0]
        if value < offset_bytes or value >= len(output):
            raise ConversionError("CEL container has an invalid record offset")
        store_le32_from_be(output, offset, normalized_words)
    return bytes(output), "cel-offset-table-v1", normalized_words


def normalize_animation(data: bytes) -> tuple[bytes, str, set[int]]:
    if len(data) < 4:
        raise ConversionError("animation file is missing its header")
    output = bytearray(data)
    normalized_words: set[int] = set()
    animation_count = struct.unpack_from(">I", output, 0)[0]
    if animation_count > (len(output) - 4) // 4:
        raise ConversionError("animation file has too many offsets")
    for number in range(animation_count):
        offset_slot = 4 + number * 4
        animation_offset = struct.unpack_from(">I", output, offset_slot)[0]
        animation_start = 4 + animation_offset
        require_range(output, animation_start, 28, "animation metadata")
        sprite_count = struct.unpack_from(">I", output, animation_start + 12)[0]
        frames_per_sprite = struct.unpack_from(">I", output, animation_start + 20)[0]
        record_words = 7 + sprite_count * (frames_per_sprite + 1)
        require_range(output, animation_start, record_words * 4, "animation frame table")
        store_le32_from_be(output, offset_slot, normalized_words)
        for word in range(record_words):
            store_le32_from_be(output, animation_start + word * 4, normalized_words)
    store_le32_from_be(output, 0, normalized_words)
    return bytes(output), "texture-animation-v1", normalized_words


def texture_animation_document(data: bytes) -> dict:
    if len(data) < 4:
        raise ConversionError("animation file is missing its header")
    animation_count = struct.unpack_from(">I", data, 0)[0]
    if animation_count > (len(data) - 4) // 4:
        raise ConversionError("animation file has too many offsets")
    animations = []
    for number in range(animation_count):
        animation_offset = struct.unpack_from(">I", data, 4 + number * 4)[0]
        animation_start = 4 + animation_offset
        require_range(data, animation_start, 28, "animation metadata")
        enabled, countdown, interval, sprite_count, frame, frame_count, loop_frame = \
            struct.unpack_from(">7I", data, animation_start)
        if enabled not in (0, 1):
            raise ConversionError("animation has an invalid enabled state")
        if frame_count == 0 or frame >= frame_count or loop_frame >= frame_count:
            raise ConversionError("animation has an invalid frame range")
        record_words = 7 + sprite_count * (frame_count + 1)
        require_range(data, animation_start, record_words * 4, "animation frame table")
        sprites = []
        sprite_start = animation_start + 28
        for sprite in range(sprite_count):
            cursor = sprite_start + sprite * (frame_count + 1) * 4
            destination = struct.unpack_from(">I", data, cursor)[0]
            frames = list(struct.unpack_from(f">{frame_count}I", data, cursor + 4))
            sprites.append({"destination": destination, "frames": frames})
        animations.append({
            "enabled": bool(enabled),
            "initial_countdown": countdown,
            "frame_interval": interval,
            "initial_frame": frame,
            "frame_count": frame_count,
            "loop_frame": loop_frame,
            "sprites": sprites,
        })
    return {"schema": "texture-animation-v1", "animations": animations}


def build_texture_animations(raw_root: Path, output: Path, refresh: bool) -> dict:
    animations_root = raw_root / "SF_Resources" / "Animations"
    if not animations_root.is_dir():
        raise ConversionError("raw asset root does not contain texture animations")
    animations = {}
    for source in sorted(animations_root.glob("*.anim")):
        document = texture_animation_document(source.read_bytes())
        data = (json.dumps(document, indent=2, sort_keys=True) + "\n").encode("utf-8")
        animations[source.stem] = json_asset_entry(
            output, Path("texture-animations") / f"{source.stem}.json", data,
            "texture-animation-v1", source, refresh
        )
    if not animations:
        raise ConversionError("texture animation directory contains no animation records")
    return animations


def mark_word_range(raw_words: set[int], capacity_words: int, offset: int, length: int,
                    description: str) -> None:
    require_range(bytes(capacity_words * 4), offset, length, description)
    first = offset // 4
    end = (offset + length + 3) // 4
    raw_words.update(range(first, end))


def normalize_graphics(data: bytes) -> tuple[bytes, str, set[int]]:
    if len(data) == 0 or len(data) % 4:
        raise ConversionError("graphics data must have a non-zero multiple-of-four length")
    capacity_words = len(data) // 4
    raw_words: set[int] = set()
    collision_words: set[int] = set()
    group_offsets = [struct.unpack_from(">I", data, group * 4)[0] for group in range(3)]

    for group_offset, record_count in zip(group_offsets, (64, 128, 16)):
        require_range(data, group_offset, record_count * 64, "graphics record group")
        for record in range(record_count):
            record_offset = group_offset + record * 64
            collision_offset = struct.unpack_from(">I", data, record_offset + 16)[0]
            if collision_offset:
                collision_start = group_offset + collision_offset
                require_range(data, collision_start, 12, "collision header")
                collision_count = struct.unpack_from(">I", data, collision_start)[0]
                collision_bytes = 12 + (collision_count + 1) * 28
                require_range(data, collision_start, collision_bytes, "collision records")
                for collision in range(collision_count + 1):
                    collision_words.add((collision_start + 12 + collision * 28) // 4)

            graphic_offset = struct.unpack_from(">I", data, record_offset + 8)[0]
            if not graphic_offset:
                continue
            graphic_start = group_offset + graphic_offset
            require_range(data, graphic_start, 40, "graphic mesh header")
            node_bytes = 40 + (data[graphic_start + 33] + 1) * 3
            mark_word_range(raw_words, capacity_words, graphic_start, node_bytes, "graphic nodes")
            for link_group in range(7):
                link_offset = struct.unpack_from(">I", data, graphic_start + link_group * 4)[0]
                if not link_offset:
                    continue
                link_start = graphic_start + link_offset
                require_range(data, link_start, 4, "graphic link header")
                link_count = struct.unpack_from(">I", data, link_start)[0]
                link_bytes = 4 + (link_count + 1) * 20
                require_range(data, link_start, link_bytes, "graphic link records")
                mark_word_range(raw_words, capacity_words, link_start, link_bytes, "graphic link records")
            vector_offset = struct.unpack_from(">I", data, graphic_start + 28)[0]
            if vector_offset:
                vector_start = graphic_start + vector_offset
                require_range(data, vector_start, 4, "graphic vector header")
                vector_count = struct.unpack_from(">I", data, vector_start)[0]
                vector_bytes = 4 + (vector_count + 1) * 12
                require_range(data, vector_start, vector_bytes, "graphic vector records")
                mark_word_range(raw_words, capacity_words, vector_start, vector_bytes,
                                "graphic vector records")

    raw_words.update(collision_words)
    output = bytearray(data)
    normalized_words = set(range(capacity_words)) - raw_words
    for word in normalized_words:
        store_le32_from_be(output, word * 4, set())
    return bytes(output), "graphics-mixed-layout-v1", normalized_words


def normalize_runtime_asset(relative: Path, data: bytes) -> tuple[bytes, str, set[int]]:
    parts = relative.parts
    if len(parts) >= 3 and parts[0] == "Missions" and parts[1] != "Maps" and \
            parts[-1].startswith("MISS_"):
        return normalize_mission(data)
    if parts == ("Data", "CONFIG"):
        return normalize_configuration(data)
    if parts == ("Data", "Cosine"):
        return normalize_word_table(data, "cosine-table-v1")
    if parts == ("Data", "Tangent"):
        return normalize_word_table(data, "tangent-table-v1")
    if len(parts) == 2 and parts[0] == "Cels" and \
            (relative.suffix in (".16", ".32") or relative.name.startswith("Game.")):
        return normalize_cel_offsets(data)
    if len(parts) == 2 and parts[0] == "Info" and relative.suffix == ".info":
        if len(data) != 24:
            raise ConversionError("planet data record must be exactly 24 bytes")
        output = bytearray(data)
        words: set[int] = set()
        store_le32_from_be(output, 20, words)
        return bytes(output), "planet-data-v1", words
    if len(parts) == 2 and parts[0] == "Animations" and relative.suffix == ".anim":
        return normalize_animation(data)
    if len(parts) == 2 and parts[0] == "Graphics":
        return normalize_graphics(data)
    return data, "identity-copy-v1", set()


def build_world_graphics_library(raw_root: Path, output: Path, refresh: bool) -> dict:
    """Emit normalized world model/collision packages as content-addressed binaries."""
    graphics_root = raw_root / "SF_Resources" / "Graphics"
    world_graphics = {}

    if not graphics_root.is_dir():
        return world_graphics
    for source in sorted(path for path in graphics_root.iterdir() if path.is_file()):
        normalized, schema, _normalized_words = normalize_graphics(source.read_bytes())
        world = source.name[0] + source.name[1:].lower()
        world_graphics[world] = write_content_addressed_ui_binary(
            output, Path("world") / "graphics", normalized, schema,
            source, refresh
        )
    return world_graphics


def build_legacy_palette_assets(raw_root: Path, output: Path, refresh: bool) -> dict:
    """Emit the monochrome palette still consumed by the compatibility compositor."""
    source = raw_root / "SF_Resources" / "Palettes" / "Monochrome.pal"
    if not source.is_file():
        return {}
    return {
        "monochrome_palette": write_content_addressed_ui_binary(
            output, Path("compatibility") / "palettes", source.read_bytes(),
            "identity-copy-v1", source, refresh
        )
    }


def build_runtime_manifest(base: Path, source_root: Path, output: Path) -> dict:
    resources = source_root / "SF_Resources"
    if not resources.is_dir():
        raise ConversionError("--raw-root must contain SF_Resources")
    # The browser now loads every remaining resource from ui-assets, but the
    # build contract still uses this empty directory as its preload root.
    (output / "SF_Resources").mkdir(parents=True, exist_ok=True)
    stale_cels = output / "SF_Resources" / "Cels"
    if stale_cels.is_dir():
        for suffix in (".4", ".16", ".32"):
            for stale in stale_cels.glob(f"*{suffix}"):
                stale.unlink()
        for stale in stale_cels.glob("Game.*"):
            stale.unlink()
    for directory in ("Animations", "Fonts", "Graphics", "Palettes", "Sky", "Text", "Info", "Data"):
        stale_directory = output / "SF_Resources" / directory
        if stale_directory.is_dir():
            for stale in stale_directory.rglob("*"):
                if stale.is_file():
                    stale.unlink()
    for relative_name in OBSOLETE_RUNTIME_FILES:
        stale = output / "SF_Resources" / relative_name
        if stale.is_file():
            stale.unlink()
    assets = []
    for source in source_files(resources):
        relative = source.relative_to(resources)
        if relative.as_posix() in OBSOLETE_RUNTIME_FILES or \
                relative.parts[0] in DISABLED_MEDIA_DIRECTORIES or \
                relative.parts[0] in ("Animations", "Data", "Fonts", "Graphics", "Images", "Info",
                                      "Missions", "Palettes", "Sky", "Text"):
            continue
        if len(relative.parts) == 2 and relative.parts[0] == "Cels" and \
                (relative.suffix in (".4", ".16", ".32") or
                 relative.name.startswith("Game.")):
            continue
        if len(relative.parts) >= 4 and relative.parts[:2] == ("Missions", "Maps") and \
                relative.name in ("H_MAP", "S_MAP"):
            continue
        original = source.read_bytes()
        normalized, schema, normalized_words = normalize_runtime_asset(relative, original)
        destination = output / "SF_Resources" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(normalized)
        assets.append({
            "source_path": f"SF_Resources/{relative.as_posix()}",
            "runtime_path": f"SF_Resources/{relative.as_posix()}",
            "size": len(original),
            "source_sha256": sha256_bytes(original),
            "runtime_sha256": sha256_bytes(normalized),
            "schema": schema,
            "schema_version": RUNTIME_SCHEMA_VERSION,
            "transformed_ranges": byte_ranges(normalized_words),
        })
    assets.sort(key=lambda asset: asset["source_path"])
    try:
        manifest_source_root = source_root.relative_to(base).as_posix()
    except ValueError as error:
        raise ConversionError("--raw-root must be inside the repository") from error
    return {
        "schema_version": RUNTIME_SCHEMA_VERSION,
        "generator": RUNTIME_GENERATOR,
        "source_root": manifest_source_root,
        "excluded_directories": [f"SF_Resources/{directory}"
                                 for directory in sorted(DISABLED_MEDIA_DIRECTORIES |
                                                         {"Animations", "Data", "Graphics", "Images",
                                                          "Info", "Fonts", "Missions",
                                                          "Palettes", "Sky", "Text"})],
        "excluded_file_patterns": [
            "SF_Resources/Fonts/*",
            "SF_Resources/Cels/*.4",
            "SF_Resources/Cels/*.16",
            "SF_Resources/Cels/*.32",
            "SF_Resources/Cels/Game.*",
            "SF_Resources/Animations/*",
            "SF_Resources/Data/*",
            "SF_Resources/Data/Cosine",
            "SF_Resources/Data/Tangent",
            "SF_Resources/Info/*",
            "SF_Resources/Text/*",
        ],
        "asset_count": len(assets),
        "assets": assets,
    }


def verify_runtime_output(source_root: Path, output: Path) -> int:
    manifest_path = output / MANIFEST_NAME
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"verification failed: cannot read {manifest_path}: {error}", file=sys.stderr)
        return 1
    if manifest.get("generator") != RUNTIME_GENERATOR:
        print("verification failed: runtime manifest was not generated by --build-runtime",
              file=sys.stderr)
        return 1
    mismatched = 0
    for relative_name in OBSOLETE_RUNTIME_FILES:
        if (output / "SF_Resources" / relative_name).exists():
            print(f"verification failed: obsolete runtime asset {relative_name} was emitted",
                  file=sys.stderr)
            mismatched += 1
    for asset in manifest.get("assets", []):
        relative = Path(asset["source_path"]).relative_to("SF_Resources")
        source = source_root / "SF_Resources" / relative
        destination = output / asset["runtime_path"]
        try:
            original = source.read_bytes()
            normalized, schema, normalized_words = normalize_runtime_asset(relative, original)
            emitted = destination.read_bytes()
        except (OSError, ConversionError) as error:
            print(f"verification failed for {asset['source_path']}: {error}", file=sys.stderr)
            mismatched += 1
            continue
        if (sha256_bytes(original) != asset.get("source_sha256") or
                sha256_bytes(emitted) != asset.get("runtime_sha256") or
                emitted != normalized or schema != asset.get("schema") or
                byte_ranges(normalized_words) != asset.get("transformed_ranges")):
            print(f"verification failed for {asset['source_path']}", file=sys.stderr)
            mismatched += 1
    print(f"verified {manifest.get('asset_count', 0)} runtime assets: {mismatched} mismatched")
    return int(mismatched != 0)


def convert_one(
    source_root_name: str, source_root: Path, source: Path, output_root: Path, manifest_only: bool
) -> dict:
    relative = source.relative_to(source_root)
    source_hash = sha256_file(source)
    data = source.read_bytes()
    text = text_to_utf8(data)
    source_mime, source_type = classify_source(source, data[:88], text)
    if source_root_name == "SF_Resources" and relative.parts[0] == "Cels":
        source_mime, source_type = "application/x-star-fighter-grouped-cel", "grouped-cel"
    outputs = [write_file_copy(
        output_root, output_path("raw", source_root_name, relative), source, source_hash, manifest_only
    )]
    outputs[0]["mime"] = source_mime
    outputs[0]["type"] = source_type
    conversion_note = None

    try:
        if data.startswith(b"FORM") and data[8:12] in (b"AIFF", b"AIFC"):
            wav, metadata = aiff_to_wav(data)
            append_conversion(
                outputs, output_root, output_path("audio", source_root_name, relative, ".wav"), wav,
                "audio/wav", "pcm-wav", metadata, manifest_only
            )
        elif (source_root_name == "SF_Resources" and relative.parts[0] == "Images"
              and (len(data) == RAW_IMAGE_BYTES
                   or (len(data) == RAW_IMAGE_BYTES + 36 and data[:4] == b"IMAG"))):
            pixels = data[36:] if len(data) == RAW_IMAGE_BYTES + 36 else data
            append_conversion(
                outputs, output_root, output_path("images", source_root_name, relative, ".png"),
                rgb555_to_png(pixels, 320, 240), "image/png", "rgb555be-png",
                {"width": 320, "height": 240, "row_order": "source-first-row"}, manifest_only
            )
        elif (source_root_name == "Coded8bpp" and source.suffix.lower() == ".cel"
              and data[:4] == b"CCB " and data[80:84] == b"PDAT"
              and struct.unpack(">I", data[12:16])[0] & CCB_LDPLUT
              and struct.unpack(">I", data[12:16])[0] & CCB_PACKED):
            indices, width, height, palette, encoding = parse_native_indexed_cel(data)
            append_indexed_cel_outputs(
                outputs, output_root, source_root_name, relative, "image",
                indices, width, height, palette, encoding, manifest_only
            )
        elif (source_root_name == "Coded8bpp" and source.suffix.lower() == ".cel"
              and len(data) == 88 + FULL_SHEET_BYTES and data[:4] == b"CCB "
              and data[80:84] == b"PDAT"):
            append_conversion(
                outputs, output_root, output_path("images", source_root_name, relative, ".png"),
                rgb555_to_png(data[88:], 320, 256), "image/png", "rgb555be-png",
                {"width": 320, "height": 256, "row_order": "source-first-row"}, manifest_only
            )
        elif (source_root_name == "Coded8bpp" and source.suffix.lower() == ".cel"
              and data[:4] == b"CCB " and data[80:84] == b"PDAT"
              and not (struct.unpack(">I", data[12:16])[0] & CCB_LDPLUT)):
            png, width, height, encoding = parse_native_direct_cel(data)
            append_conversion(
                outputs, output_root, output_path("images", source_root_name, relative, ".png"),
                png, "image/png", encoding,
                {"width": width, "height": height, "row_order": "source-first-row"}, manifest_only
            )
        elif (source_root_name == "Coded8bpp" and "Unpacked" in relative.parts
              and source.suffix.lower() == ".data" and len(data) == 512):
            append_conversion(
                outputs, output_root, output_path("images", source_root_name, relative, ".png"),
                rgb555_to_png(data, 16, 16), "image/png", "rgb555be-png",
                {"width": 16, "height": 16, "row_order": "source-first-row"}, manifest_only
            )
        elif (source_root_name == "Coded8bpp" and "Processed" in relative.parts
              and source.suffix.lower() == ".data"):
            parent = relative.parts[relative.parts.index("Processed") - 1].lower()
            parser = parse_grouped_16 if parent.endswith("16") else parse_grouped_32 if parent.endswith("32") else None
            if parser is None:
                raise ConversionError("processed CEL path does not identify a 16 or 32 format")
            indices, width, height, palette, encoding = parser(data)
            append_indexed_cel_outputs(
                outputs, output_root, source_root_name, relative, "image",
                indices, width, height, palette, encoding, manifest_only
            )
        elif source_root_name == "SF_Resources" and relative.parts[0] == "Cels":
            if relative.suffix.lower() == ".16":
                conversion_note = conversion_summary(append_grouped_indexed_outputs(
                    outputs, output_root, source_root_name, relative, data, parse_grouped_16, manifest_only
                ))
            elif relative.suffix.lower() == ".32":
                conversion_note = conversion_summary(append_grouped_indexed_outputs(
                    outputs, output_root, source_root_name, relative, data, parse_grouped_32, manifest_only
                ))
            elif relative.suffix.lower() == ".4":
                conversion_note = conversion_summary(append_grouped_tile_outputs(
                    outputs, output_root, source_root_name, relative, data, source_root, manifest_only
                ))
            elif relative.name.startswith("Game."):
                conversion_note = conversion_summary(append_grouped_game_outputs(
                    outputs, output_root, source_root_name, relative, data, manifest_only
                ))
        elif source.suffix.lower() == ".pal" and len(data) == 1024:
            append_conversion(
                outputs, output_root, output_path("palettes", source_root_name, relative, ".json"),
                monochrome_palette_json(data), "application/json", "rgb555be-monochrome-palette-json",
                {"entries": 128}, manifest_only
            )
        elif source.suffix.lower() == ".pal" and len(data) == 64:
            append_conversion(
                outputs, output_root, output_path("palettes", source_root_name, relative, ".json"),
                palette_json(data), "application/json", "rgb555be-palette-json",
                {"entries": 32}, manifest_only
            )
        elif source_root_name == "SF_Resources" and relative.parts[0] == "Info" and len(data) == 24:
            append_conversion(
                outputs, output_root, output_path("metadata", source_root_name, relative, ".json"),
                planet_info_json(data), "application/json", "star-fighter-planet-info-json",
                {}, manifest_only
            )
        elif source_root_name == "SF_Resources" and relative.parts[0] == "Animations":
            document = texture_animation_document(data)
            converted = (
                json.dumps(document, indent=2, sort_keys=True) + "\n"
            ).encode("utf-8")
            append_conversion(
                outputs, output_root,
                output_path("texture-animations", source_root_name, relative, ".json"),
                converted, "application/json", "texture-animation-v1",
                {"animations": len(document["animations"])}, manifest_only
            )
        elif source_root_name == "SF_Resources" and relative.parts[0] == "Text":
            indexed = relative.name in ("Game", "Menu", "Title")
            strings = parse_legacy_text(source, indexed)
            converted = (
                json.dumps(strings, indent=2, ensure_ascii=True) + "\n"
            ).encode("utf-8")
            append_conversion(
                outputs, output_root,
                output_path("text", source_root_name, relative, ".json"),
                converted, "application/json",
                "legacy-text-indexed-v1" if indexed else "legacy-text-lines-v1",
                {"strings": len(strings)}, manifest_only
            )
        elif text is not None:
            append_conversion(
                outputs, output_root, output_path("text", source_root_name, relative, ".txt"), text,
                "text/plain; charset=utf-8", "utf8-text", {}, manifest_only
            )
    except ConversionError as error:
        conversion_note = str(error)

    requirement = decoder_requirement(
        source_root_name, relative, source_type, conversion_note, len(outputs)
    )
    return {
        "source_path": f"{source_root_name}/{relative.as_posix()}",
        "mime": source_mime,
        "type": source_type,
        "size": len(data),
        "sha256": source_hash,
        "outputs": outputs,
        **({"conversion_note": conversion_note} if conversion_note else {}),
        **({"decoder_requirement": requirement} if requirement else {}),
    }


def build_manifest(base: Path, output: Path, manifest_only: bool,
                   source_root_names: tuple[str, ...],
                   include_disabled_media: bool) -> dict:
    roots = []
    for root_name in source_root_names:
        root = base / root_name
        if not root.is_dir():
            raise ConversionError(f"required source directory is missing: {root_name}")
        roots.append((root_name, root))

    assets = []
    for root_name, root in roots:
        for source in source_files(root):
            if not is_included_source(root_name, root, source, include_disabled_media):
                continue
            assets.append(convert_one(root_name, root, source, output, manifest_only))
    assets.sort(key=lambda asset: asset["source_path"])
    return {
        "schema_version": 1,
        "generator": "tools/convert_assets.py",
        "source_roots": list(source_root_names),
        "excluded_directories": [] if include_disabled_media else
        [f"SF_Resources/{directory}" for directory in sorted(DISABLED_MEDIA_DIRECTORIES)],
        "asset_count": len(assets),
        "assets": assets,
    }


def build_sound_effects_manifest(base: Path, output: Path, manifest_only: bool) -> dict:
    source_root = base / "SF_Resources" / "Samples"
    if not source_root.is_dir():
        raise ConversionError("required sound-effects directory is missing: SF_Resources/Samples")
    assets = []
    for source in source_files(source_root):
        if source.suffix.lower() not in (".aif", ".aiff"):
            continue
        relative = source.relative_to(source_root)
        data = source.read_bytes()
        wav, metadata = aiff_to_wav(data)
        emitted = write_bytes(
            output, f"samples/{safe_path(relative.with_suffix('.wav'))}", wav, manifest_only
        )
        emitted.update({"mime": "audio/wav", "type": "pcm-wav"})
        emitted.update(metadata)
        assets.append({
            "source_path": f"SF_Resources/Samples/{relative.as_posix()}",
            "sha256": sha256_file(source),
            "outputs": [emitted],
        })
    if not assets:
        raise ConversionError("SF_Resources/Samples contains no AIFF effect samples")
    return {
        "schema_version": 1,
        "generator": SOUND_EFFECTS_GENERATOR,
        "asset_count": len(assets),
        "assets": assets,
    }


def resolve_ffmpeg(configured_path: Path | None) -> list[str]:
    if configured_path is not None:
        if configured_path.is_file():
            return [str(configured_path)]
        raise ConversionError(f"FFmpeg executable does not exist: {configured_path}")
    executable = shutil.which("ffmpeg")
    if executable is not None:
        return [executable]
    wsl = shutil.which("wsl.exe")
    if wsl is not None:
        try:
            available = subprocess.run(
                [wsl, "--", "ffmpeg", "-version"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            ).returncode == 0
        except OSError:
            available = False
        if available:
            return [wsl, "--", "ffmpeg"]
    raise ConversionError(
        "FFmpeg with the libopus encoder is required for streamed media conversion; "
        "install it, expose it through WSL, or pass --ffmpeg <path>"
    )


def wav_to_opus(wav: bytes, bitrate: str, ffmpeg: list[str]) -> bytes:
    try:
        process = subprocess.run(
            [
                *ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin",
                "-i", "pipe:0", "-map_metadata", "-1",
                "-c:a", "libopus", "-application", "audio", "-vbr", "on",
                "-b:a", bitrate, "-f", "ogg", "pipe:1",
            ],
            input=wav,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except OSError as error:
        raise ConversionError(f"could not run FFmpeg: {error}") from error
    if process.returncode != 0:
        detail = process.stderr.decode("utf-8", "replace").strip()
        raise ConversionError(f"FFmpeg Opus conversion failed: {detail}")
    if not process.stdout.startswith(b"OggS"):
        raise ConversionError("FFmpeg did not produce an Ogg Opus stream")
    return process.stdout


def cinepak_avi_to_mp4(avi: bytes, ffmpeg: list[str]) -> bytes:
    def ffmpeg_path(path: Path) -> str:
        resolved = path.resolve()
        if len(ffmpeg) >= 3 and Path(ffmpeg[0]).name.lower() == "wsl.exe":
            if not resolved.drive:
                raise ConversionError("WSL FFmpeg requires a Windows drive-backed temporary path")
            return "/mnt/" + resolved.drive[0].lower() + resolved.as_posix()[2:]
        return str(resolved)

    try:
        with tempfile.TemporaryDirectory(prefix="starfighter-cinematic-") as temporary:
            temporary_root = Path(temporary)
            source = temporary_root / "source.avi"
            destination = temporary_root / "cinematic.mp4"
            source.write_bytes(avi)
            process = subprocess.run(
                [
                    *ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin",
                    "-f", "avi", "-i", ffmpeg_path(source), "-map_metadata", "-1",
                    "-map", "0:v:0", "-map", "0:a:0",
                    "-c:v", "libx264", "-profile:v", "baseline", "-level:v", "3.0",
                    "-pix_fmt", "yuv420p", "-crf", "22", "-preset", "slow",
                    "-c:a", "aac", "-b:a", "96k", "-movflags", "+faststart",
                    ffmpeg_path(destination),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            mp4 = destination.read_bytes() if process.returncode == 0 and destination.is_file() else b""
    except OSError as error:
        raise ConversionError(f"could not run FFmpeg: {error}") from error
    if process.returncode != 0:
        detail = process.stderr.decode("utf-8", "replace").strip()
        raise ConversionError(f"FFmpeg cinematic conversion failed: {detail}")
    if len(mp4) < 16 or mp4[4:8] != b"ftyp":
        raise ConversionError("FFmpeg did not produce an MP4 cinematic")
    return mp4


def build_streamed_media_manifest(base: Path, output: Path, manifest_only: bool,
                                  ffmpeg: list[str] | None) -> dict:
    source_root = base / "SF_Resources"
    groups = (("Music", "music", "96k"), ("Voices", "voices", "40k"))
    assets = []
    for source_directory, output_directory, bitrate in groups:
        root = source_root / source_directory
        if not root.is_dir():
            raise ConversionError(f"required streamed-media directory is missing: {source_directory}")
        for source in source_files(root):
            relative = source.relative_to(root)
            data = source.read_bytes()
            wav, metadata = aiff_to_wav(data)
            opus = b"" if manifest_only else wav_to_opus(wav, bitrate, ffmpeg)
            emitted = write_bytes(
                output, f"{output_directory}/{safe_path(relative.with_suffix('.opus'))}",
                opus, manifest_only
            )
            emitted.update({"mime": "audio/ogg; codecs=opus", "type": "opus", "bitrate": bitrate})
            emitted.update(metadata)
            assets.append({
                "source_path": f"SF_Resources/{source_directory}/{relative.as_posix()}",
                "sha256": sha256_file(source),
                "outputs": [emitted],
            })
    if not assets:
        raise ConversionError("no streamed-media assets were found")
    return {
        "schema_version": 1,
        "generator": STREAMED_MEDIA_GENERATOR,
        "asset_count": len(assets),
        "assets": assets,
    }


def build_cinematics_manifest(base: Path, output: Path, manifest_only: bool,
                              ffmpeg: list[str] | None) -> dict:
    source_root = base / "SF_Resources" / "Video"
    if not source_root.is_dir():
        raise ConversionError("required cinematic directory is missing: SF_Resources/Video")
    assets = []
    for source in source_files(source_root):
        relative = source.relative_to(source_root)
        avi, metadata = cinepak_stream_to_avi(source.read_bytes())
        mp4 = b"" if manifest_only else cinepak_avi_to_mp4(avi, ffmpeg)
        emitted = write_bytes(
            output, f"video/{safe_path(relative.with_suffix('.mp4'))}", mp4, manifest_only
        )
        emitted.update({
            "mime": 'video/mp4; codecs="avc1.42E01E, mp4a.40.2"',
            "type": "h264-aac-mp4",
        })
        emitted.update(metadata)
        assets.append({
            "source_path": f"SF_Resources/Video/{relative.as_posix()}",
            "sha256": sha256_file(source),
            "outputs": [emitted],
        })
    if not assets:
        raise ConversionError("SF_Resources/Video contains no cinematic streams")
    return {
        "schema_version": 1,
        "generator": CINEMATICS_GENERATOR,
        "asset_count": len(assets),
        "assets": assets,
    }


def verify_output(output: Path) -> int:
    manifest_path = output / MANIFEST_NAME
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"verification failed: cannot read {manifest_path}: {error}", file=sys.stderr)
        return 1
    missing = corrupt = 0
    for asset in manifest.get("assets", []):
        for emitted in asset.get("outputs", []):
            if not emitted.get("emitted"):
                continue
            path = output / Path(emitted["path"])
            if not path.is_file():
                missing += 1
            elif path.stat().st_size != emitted["size"] or sha256_file(path) != emitted["sha256"]:
                corrupt += 1
    print(f"verified {manifest.get('asset_count', 0)} assets: {missing} missing, {corrupt} mismatched")
    return int(missing > 0 or corrupt > 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("web-assets"),
                        help="destination directory (default: web-assets)")
    parser.add_argument("--manifest-only", action="store_true",
                        help="write only a complete manifest; do not emit asset files")
    parser.add_argument("--clean", action="store_true",
                        help="remove the output directory before building")
    parser.add_argument("--verify", action="store_true",
                        help="verify hashes and sizes in an existing manifest, without building")
    parser.add_argument("--include-reference-assets", action="store_true",
                        help="also package the Coded8bpp and 3DO cels reference-asset trees")
    parser.add_argument("--include-disabled-media", action="store_true",
                        help="also package disabled music, video, voice-over, and sample assets")
    parser.add_argument("--sound-effects-only", action="store_true",
                        help="convert only SF_Resources/Samples to browser WAV files")
    parser.add_argument("--streamed-media-only", action="store_true",
                        help="convert SDX2 music and voice-over to streamed Ogg Opus files")
    parser.add_argument("--cinematics-only", action="store_true",
                        help="convert 3DO Cinepak/SDX2 cinematics to streamed MP4 files")
    parser.add_argument("--ffmpeg", type=Path,
                        help="path to an FFmpeg executable with libopus support")
    parser.add_argument("--build-runtime", action="store_true",
                        help="generate the endian-normalized browser runtime mirror")
    parser.add_argument("--verify-runtime", action="store_true",
                        help="verify a generated runtime mirror against preserved raw assets")
    parser.add_argument("--ui-textures-only", action="store_true",
                        help="generate editable Scale3x Game.* and Message font textures")
    parser.add_argument("--ui-output", type=Path, default=Path("SF3000/WebPort/web/ui-assets"),
                        help="destination directory for --ui-textures-only")
    parser.add_argument("--refresh-ui-textures", action="store_true",
                        help="overwrite editable UI textures when generating them")
    parser.add_argument("--raw-root", type=Path, default=Path("web-assets/raw"),
                        help="preserved media-free asset root for runtime generation")
    parser.add_argument("--runtime-output", type=Path, default=Path("web-assets/runtime"),
                        help="generated runtime mirror destination")
    args = parser.parse_args()
    base = Path.cwd()
    output = (base / args.output).resolve() if not args.output.is_absolute() else args.output.resolve()
    raw_root = (base / args.raw_root).resolve() if not args.raw_root.is_absolute() else \
        args.raw_root.resolve()
    runtime_output = (base / args.runtime_output).resolve() if not args.runtime_output.is_absolute() else \
        args.runtime_output.resolve()
    ui_output = (base / args.ui_output).resolve() if not args.ui_output.is_absolute() else \
        args.ui_output.resolve()
    if args.build_runtime and args.verify_runtime:
        parser.error("--build-runtime and --verify-runtime cannot be used together")
    if (args.sound_effects_only or args.streamed_media_only or args.cinematics_only or
            args.ui_textures_only) and (
            args.build_runtime or args.verify_runtime or args.include_disabled_media or
            args.include_reference_assets):
        parser.error("single-purpose media conversion cannot be combined with other asset modes")
    if sum((args.sound_effects_only, args.streamed_media_only, args.cinematics_only,
            args.ui_textures_only)) > 1:
        parser.error("only one single-purpose media conversion mode may be selected")
    if args.refresh_ui_textures and not args.ui_textures_only:
        parser.error("--refresh-ui-textures requires --ui-textures-only")
    if args.ui_textures_only:
        try:
            validate_output_path(base, ui_output)
            ui_output.relative_to(raw_root)
            raise ConversionError("--ui-output must not be inside --raw-root")
        except ValueError:
            pass
        except ConversionError as error:
            print(f"conversion failed: {error}", file=sys.stderr)
            return 1
        try:
            ui_output.mkdir(parents=True, exist_ok=True)
            manifest = build_ui_texture_manifest(
                raw_root, ui_output, args.refresh_ui_textures
            )
        except ConversionError as error:
            print(f"conversion failed: {error}", file=sys.stderr)
            return 1
        manifest_path = ui_output / MANIFEST_NAME
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                                 encoding="utf-8")
        print(f"wrote browser textures: {len(manifest['game_sets'])} Game.* sets, "
              f"{len(manifest['world_sets'])} world sets, {len(manifest['sky'])} skies, "
              f"{len(manifest['backdrops'])} backdrops, {len(manifest['maps'])} maps, "
              f"{manifest_path}")
        return 0
    if args.build_runtime or args.verify_runtime:
        try:
            validate_output_path(base, runtime_output)
            runtime_output.relative_to(raw_root)
            raise ConversionError("--runtime-output must not be inside --raw-root")
        except ValueError:
            pass
        except ConversionError as error:
            print(f"conversion failed: {error}", file=sys.stderr)
            return 1
        if args.verify_runtime:
            return verify_runtime_output(raw_root, runtime_output)
        try:
            clean_output_path(runtime_output)
            runtime_output.mkdir(parents=True, exist_ok=True)
            manifest = build_runtime_manifest(base, raw_root, runtime_output)
        except ConversionError as error:
            print(f"conversion failed: {error}", file=sys.stderr)
            return 1
        manifest_path = runtime_output / MANIFEST_NAME
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                                 encoding="utf-8")
        print(f"wrote runtime mirror: {manifest['asset_count']} assets, {manifest_path}")
        return 0
    if args.verify:
        return verify_output(output)
    try:
        validate_output_path(base, output)
        if args.clean and output.exists():
            clean_output_path(output)
        output.mkdir(parents=True, exist_ok=True)
        if args.sound_effects_only:
            manifest = build_sound_effects_manifest(base, output, args.manifest_only)
            manifest_path = output / MANIFEST_NAME
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
            print(f"wrote sound effects and manifest: {manifest['asset_count']} sources, "
                  f"{manifest_path}")
            return 0
        if args.streamed_media_only:
            ffmpeg = None if args.manifest_only else resolve_ffmpeg(args.ffmpeg)
            manifest = build_streamed_media_manifest(base, output, args.manifest_only, ffmpeg)
            manifest_path = output / MANIFEST_NAME
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
            print(f"wrote streamed media and manifest: {manifest['asset_count']} sources, "
                  f"{manifest_path}")
            return 0
        if args.cinematics_only:
            ffmpeg = None if args.manifest_only else resolve_ffmpeg(args.ffmpeg)
            manifest = build_cinematics_manifest(base, output, args.manifest_only, ffmpeg)
            manifest_path = output / MANIFEST_NAME
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
            print(f"wrote cinematics and manifest: {manifest['asset_count']} sources, "
                  f"{manifest_path}")
            return 0
        source_roots = RUNTIME_SOURCE_ROOTS + (
            REFERENCE_SOURCE_ROOTS if args.include_reference_assets else ()
        )
        manifest = build_manifest(
            base,
            output,
            args.manifest_only,
            source_roots,
            args.include_disabled_media,
        )
    except ConversionError as error:
        print(f"conversion failed: {error}", file=sys.stderr)
        return 1
    manifest_path = output / MANIFEST_NAME
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    converted = sum(len(asset["outputs"]) - 1 for asset in manifest["assets"])
    mode = "manifest only" if args.manifest_only else "assets and manifest"
    print(f"wrote {mode}: {manifest['asset_count']} sources, {converted} converted outputs, {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
