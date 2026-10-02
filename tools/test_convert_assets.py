import base64
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

from convert_assets import (ConversionError, cinepak_stream_to_avi, scale3x_rgba,
                            scale3x_rgba_tiles, font_atlas_pixels, grouped_records,
                            bilinear_scale, font_pixel_weights, gaussian_blur_foreground,
                            masked_bilinear_scale, packed_row_width, parse_grouped_game,
                            pack_ui_sprite_atlas,
                            game_cel_pmode_is_equivalent, rgba_pixels_from_indices,
                            matte_pyramid_planet,
                            normalise_game_pmode_alpha, scale_font_atlas, sdx2_to_pcm,
                            pack_ui_sprite_atlas_pages, scale_game_alphabet,
                            scale_game_sprite, unpremultiply_rgba,
                            unsharp_blend,
                            xbr_style_4x_rgba, scale_world_cel_32, backdrop_rgba_pixels,
                            build_runtime_manifest, build_ui_texture_manifest, map_rgba_pixels,
                            normalize_cel_offsets, normalize_graphics, sky_rgba_pixels,
                            ALPHABET_GLYPH_ADVANCES, font_glyph_sprites, convert_one,
                            texture_animation_document, write_ui_texture)


def pcm_values(data):
    return [int.from_bytes(data[offset:offset + 2], "little", signed=True)
            for offset in range(0, len(data), 2)]


def read_rgba_png(path: Path) -> tuple[bytes, int, int]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise AssertionError("expected a PNG file")
    chunks = []
    offset = 8
    while offset < len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        chunk_type = data[offset + 4:offset + 8]
        chunks.append((chunk_type, data[offset + 8:offset + 8 + size]))
        offset += size + 12
    header = next(payload for chunk_type, payload in chunks if chunk_type == b"IHDR")
    width, height, bit_depth, color_type, compression, filter_type, interlace = \
        struct.unpack(">IIBBBBB", header)
    if (bit_depth, color_type, compression, filter_type, interlace) != (8, 6, 0, 0, 0):
        raise AssertionError("expected a non-interlaced 8-bit RGBA PNG")
    scanlines = zlib.decompress(
        b"".join(payload for chunk_type, payload in chunks if chunk_type == b"IDAT")
    )
    row_size = width * 4
    pixels = bytearray()
    for row in range(height):
        start = row * (row_size + 1)
        if scanlines[start] != 0:
            raise AssertionError("expected unfiltered PNG scanlines")
        pixels.extend(scanlines[start + 1:start + row_size + 1])
    return bytes(pixels), width, height


def browser_font_atlas() -> dict:
    program = """
import { readFileSync } from "node:fs";
import { decodeMessageFontAtlas } from "./SF3000/WebPort/web/webgl_renderer.js";
const font = decodeMessageFontAtlas(readFileSync(process.argv[1]));
console.log(JSON.stringify({
  pixels: Buffer.from(font.pixels).toString("base64"),
  atlasWidth: font.atlasWidth,
  atlasHeight: font.atlasHeight,
  charWidth: font.charWidth,
  charHeight: font.charHeight,
  firstChar: font.firstChar,
  lastChar: font.lastChar
}));
"""
    result = subprocess.run(
        ["node", "--experimental-default-type=module", "--input-type=module",
         "--eval", program, "web-assets/raw/SF_Resources/Fonts/Message"],
        check=True, capture_output=True, text=True
    )
    font = json.loads(result.stdout)
    font["pixels"] = base64.b64decode(font["pixels"])
    return font


class TextureAnimationDocumentTests(unittest.TestCase):
    def test_document_preserves_manual_and_automatic_programs(self):
        manual = (
            0, 3, 7, 2, 2, 3, 1,
            4, 6, 7, 8,
            9, 1, 2, 3,
        )
        automatic = (
            1, 5, 2, 1, 1, 2, 0,
            227, 13, 14,
        )
        data = struct.pack(">3I", 2, 8, 68) + struct.pack(
            f">{len(manual) + len(automatic)}I", *(manual + automatic)
        )

        self.assertEqual(texture_animation_document(data), {
            "schema": "texture-animation-v1",
            "animations": [
                {
                    "enabled": False,
                    "initial_countdown": 3,
                    "frame_interval": 7,
                    "initial_frame": 2,
                    "frame_count": 3,
                    "loop_frame": 1,
                    "sprites": [
                        {"destination": 4, "frames": [6, 7, 8]},
                        {"destination": 9, "frames": [1, 2, 3]},
                    ],
                },
                {
                    "enabled": True,
                    "initial_countdown": 5,
                    "frame_interval": 2,
                    "initial_frame": 1,
                    "frame_count": 2,
                    "loop_frame": 0,
                    "sprites": [{"destination": 227, "frames": [13, 14]}],
                },
            ],
        })


class Sdx2DecoderTests(unittest.TestCase):
    def test_mono_exact_and_delta_codes(self):
        self.assertEqual(
            pcm_values(sdx2_to_pcm(bytes([0x00, 0x01, 0x03, 0xFF, 0x02, 0x03,
                                           0xFE, 0xFD]), 8, 1)),
            [0, 2, 20, 18, 8, 26, -8, -26],
        )

    def test_decoder_saturates_and_resets_history(self):
        self.assertEqual(
            pcm_values(sdx2_to_pcm(bytes([0x7F, 0x7F, 0x81, 0x80]), 4, 1)),
            [32258, 32767, 509, -32768],
        )

    def test_stereo_channels_have_independent_histories(self):
        self.assertEqual(
            pcm_values(sdx2_to_pcm(bytes([0x01, 0x02, 0x03, 0x03, 0x02, 0xFF,
                                           0x03, 0xFE]), 4, 2)),
            [2, 8, 20, 26, 8, 24, 26, -8],
        )


def stream_chunk(chunk_type, payload, time=0, subtype=None):
    if subtype is not None:
        payload = struct.pack(">II4s", time, 0, subtype) + payload
    chunk = chunk_type + struct.pack(">I", len(payload) + 8) + payload
    return chunk + b"\0" * ((-len(chunk)) & 3)


class CinepakStreamTests(unittest.TestCase):
    def test_demuxes_cinepak_and_sdx2_into_avi(self):
        cinepak_header = struct.pack(">6I", 0, int.from_bytes(b"cvid", "big"), 200, 320, 12, 2)
        audio_header = struct.pack(
            ">11I", 0, 8, 29952, 16384, 0, 16, 22050, 2,
            int.from_bytes(b"SDX2", "big"), 2, 4
        )
        stream = (
            stream_chunk(b"FILM", cinepak_header, subtype=b"FHDR") +
            stream_chunk(b"SNDS", audio_header, subtype=b"SHDR") +
            stream_chunk(b"FILM", struct.pack(">2I", 20, 4) + b"abcd", subtype=b"FRME") +
            stream_chunk(b"SNDS", struct.pack(">I", 4) + b"\x00\x01\x02\x03", subtype=b"SSMP") +
            stream_chunk(b"FILM", struct.pack(">2I", 20, 4) + b"efgh", subtype=b"FRME")
        )

        avi, metadata = cinepak_stream_to_avi(stream)

        self.assertEqual(avi[:12], b"RIFF" + struct.pack("<I", len(avi) - 8) + b"AVI ")
        self.assertIn(b"cvid", avi)
        self.assertIn(b"00dc", avi)
        self.assertIn(b"01wb", avi)
        self.assertEqual(metadata, {
            "width": 320,
            "height": 200,
            "frame_rate": 12,
            "frames": 2,
            "audio_channels": 2,
            "audio_sample_rate": 22050,
            "audio_frames": 2,
        })

    def test_rejects_cinepak_frame_count_mismatch(self):
        cinepak_header = struct.pack(">6I", 0, int.from_bytes(b"cvid", "big"), 200, 320, 12, 2)
        audio_header = struct.pack(
            ">11I", 0, 8, 29952, 16384, 0, 16, 22050, 1,
            int.from_bytes(b"SDX2", "big"), 2, 1
        )
        stream = (
            stream_chunk(b"FILM", cinepak_header, subtype=b"FHDR") +
            stream_chunk(b"SNDS", audio_header, subtype=b"SHDR") +
            stream_chunk(b"FILM", struct.pack(">2I", 20, 4) + b"abcd", subtype=b"FRME") +
            stream_chunk(b"SNDS", struct.pack(">I", 1) + b"\x00", subtype=b"SSMP")
        )

        with self.assertRaisesRegex(ConversionError, "frame count"):
            cinepak_stream_to_avi(stream)


class Scale3xTests(unittest.TestCase):
    def test_preserves_flat_rgba_regions(self):
        source = bytes([12, 34, 56, 78]) * 4

        scaled, width, height = scale3x_rgba(source, 2, 2)

        self.assertEqual((width, height), (6, 6))
        self.assertEqual(scaled, bytes([12, 34, 56, 78]) * 36)

    def test_expands_a_corner_edge(self):
        black = bytes([0, 0, 0, 0])
        white = bytes([255, 255, 255, 255])
        source = (
            black + white + white +
            black + white + white +
            black + black + black
        )

        scaled, width, _height = scale3x_rgba(source, 3, 3)

        self.assertEqual(scaled[(3 * width + 3) * 4:(3 * width + 4) * 4], white)
        self.assertEqual(scaled[(4 * width + 3) * 4:(4 * width + 4) * 4], black)

    def test_preserves_font_shader_channel_values(self):
        pixels = bytes([
            1, 0, 0, 255, 7, 3, 0, 255,
            0, 1, 0, 255, 4, 2, 0, 255,
        ])

        scaled, _width, _height = scale3x_rgba(pixels, 2, 2)

        source_values = {
            pixels[offset:offset + 4] for offset in range(0, len(pixels), 4)
        }
        output_values = {
            scaled[offset:offset + 4] for offset in range(0, len(scaled), 4)
        }
        self.assertTrue(output_values.issubset(source_values))

    def test_keeps_glyph_cells_independent(self):
        black = bytes([0, 0, 0, 0])
        red = bytes([7, 3, 0, 255])
        source = black + red

        scaled, width, height = scale3x_rgba_tiles(source, 2, 1, 1, 1)

        self.assertEqual((width, height), (6, 3))
        self.assertEqual(scaled[:12], black * 3)
        self.assertEqual(scaled[12:24], red * 3)

    def test_font_semantic_scaling_preserves_weight_invariants(self):
        transparent = bytes([0, 0, 0, 0])
        foreground = bytes([7, 0, 0, 255])
        outline = bytes([3, 1, 0, 255])
        source = (
            transparent + foreground + transparent +
            outline + foreground + outline +
            transparent + outline + transparent
        )
        alternate_shading = (
            transparent + outline + transparent +
            foreground + outline + foreground +
            transparent + foreground + transparent
        )

        scaled, width, height = scale_font_atlas(source, 3, 3, 3, 3)
        alternate, alternate_width, alternate_height = scale_font_atlas(
            alternate_shading, 3, 3, 3, 3
        )

        self.assertEqual((width, height), (12, 12))
        self.assertEqual((alternate_width, alternate_height), (12, 12))
        self.assertNotEqual(scaled, alternate)
        self.assertTrue(all(
            scaled[offset] + scaled[offset + 1] <= 256
            for offset in range(0, len(scaled), 4)
        ))
        self.assertTrue(all(
            alternate[offset] + alternate[offset + 1] <= 256
            for offset in range(0, len(alternate), 4)
        ))
        self.assertTrue(all(value == 0 for value in scaled[2::4]))
        self.assertTrue(all(value == 0 for value in alternate[2::4]))

    def test_font_pixel_weights_match_original_text_pixel_classes(self):
        self.assertEqual(font_pixel_weights(bytes([3, 0, 0, 255])), (0.5, 0.0, 0.5))
        self.assertEqual(font_pixel_weights(bytes([3, 1, 0, 255])), (0.0, 0.5, 0.5))
        self.assertEqual(font_pixel_weights(bytes([3, 2, 0, 255])), (0.5, 0.5, 1.0))
        self.assertEqual(font_pixel_weights(bytes([0, 2, 0, 255])), (0.0, 1.0, 1.0))

    def test_foreground_blur_excludes_nonforeground_samples(self):
        blurred = gaussian_blur_foreground(
            [0.0, 0.5, 1.0],
            [False, True, True],
            3, 1
        )

        self.assertAlmostEqual(blurred[1], 2 / 3)
        self.assertAlmostEqual(blurred[2], 7 / 8)

    def test_bilinear_scale_preserves_low_resolution_foreground_values(self):
        scaled = bilinear_scale([0.0, 1.0], 2, 1, 4)

        self.assertEqual(len(scaled), 32)
        self.assertGreater(scaled[3], 0.0)
        self.assertLess(scaled[3], 1.0)

    def test_masked_bilinear_scale_excludes_nonforeground_samples(self):
        scaled = masked_bilinear_scale([0.0, 1.0], [False, True], 2, 1, 4)

        self.assertIn(1.0, scaled)
        self.assertTrue(all(value in (0.0, 1.0) for value in scaled))

    def test_unsharp_blend_restores_detail_without_exceeding_coverage(self):
        sharpened = unsharp_blend([0.75, 0.0, 1.0], [0.5, 0.5, 0.5])

        self.assertEqual(sharpened, [0.8125, 0.0, 1.0])

    def test_xbr_style_rasterizer_blends_semantic_diagonal_corners(self):
        transparent = bytes([0, 0, 0, 0])
        foreground = bytes([255, 0, 0, 255])
        outline = bytes([0, 255, 0, 255])
        source = (
            foreground + foreground + transparent +
            foreground + outline + transparent +
            transparent + transparent + transparent
        )

        scaled, width, height = xbr_style_4x_rgba(source, 3, 3)

        self.assertEqual((width, height), (12, 12))
        self.assertTrue(any(
            0 < scaled[offset] < 255 and 0 < scaled[offset + 1] < 255
            for offset in range(0, len(scaled), 4)
        ))

    def test_ui_sprite_atlas_preserves_sprite_pixels_and_gutters(self):
        first = bytes([255, 0, 0, 255])
        second = bytes([0, 255, 0, 255]) * 2

        atlas, width, height, sprites = pack_ui_sprite_atlas(
            {4: (first, 1, 1), 9: (second, 2, 1)}, atlas_width=16, padding=2
        )

        self.assertEqual((width, height), (16, 5))
        self.assertEqual(sprites["4"]["width"], 1)
        self.assertEqual(sprites["4"]["height"], 1)
        self.assertEqual(sprites["9"]["width"], 2)
        self.assertEqual(sprites["9"]["height"], 1)
        first_offset = (sprites["4"]["y"] * width + sprites["4"]["x"]) * 4
        second_offset = (sprites["9"]["y"] * width + sprites["9"]["x"]) * 4
        self.assertEqual(atlas[first_offset:first_offset + 4], first)
        self.assertEqual(atlas[second_offset:second_offset + 8], second)
        self.assertEqual(
            atlas[(sprites["4"]["y"] * width + sprites["4"]["x"] - 1) * 4:
                  (sprites["4"]["y"] * width + sprites["4"]["x"]) * 4],
            bytes(4)
        )

    def test_ui_sprite_atlas_uses_deterministic_compact_maxrects_placement(self):
        sprites = {
            "large": (bytes([255, 0, 0, 255]) * 36, 6, 6),
            "first": (bytes([0, 255, 0, 255]) * 16, 4, 4),
            "second": (bytes([0, 0, 255, 255]) * 16, 4, 4),
        }

        first = pack_ui_sprite_atlas(sprites, atlas_width=10, padding=0)
        second = pack_ui_sprite_atlas(sprites, atlas_width=10, padding=0)

        self.assertEqual(first, second)
        _atlas, width, height, placements = first
        self.assertEqual((width, height), (10, 8))
        for key, placement in placements.items():
            for other_key, other in placements.items():
                if key >= other_key:
                    continue
                self.assertTrue(
                    placement["x"] + placement["width"] <= other["x"] or
                    other["x"] + other["width"] <= placement["x"] or
                    placement["y"] + placement["height"] <= other["y"] or
                    other["y"] + other["height"] <= placement["y"]
                )

    def test_ui_sprite_atlas_pages_split_before_the_webgl_limit(self):
        first = bytes([255, 0, 0, 255]) * 12
        second = bytes([0, 255, 0, 255]) * 12

        pages = pack_ui_sprite_atlas_pages(
            {"a": (first, 3, 4), "b": (second, 3, 4)},
            atlas_width=8, atlas_height=8, padding=1
        )

        self.assertEqual(len(pages), 2)
        self.assertEqual(pages[0][3]["a"], {"x": 1, "y": 1, "width": 3, "height": 4})
        self.assertEqual(pages[1][3]["b"], {"x": 1, "y": 1, "width": 3, "height": 4})


class PackedCelTests(unittest.TestCase):
    def test_literal_runs_contribute_to_packed_sprite_width(self):
        rows = bytes([0, 0x40, 0xFC, 0x8F, 0, 0, 0, 0])

        self.assertEqual(packed_row_width(rows, 4), 10)


class MessageFontRegressionTests(unittest.TestCase):
    raw_path = Path("web-assets/raw/SF_Resources/Fonts/Message")
    baked_path = Path("SF3000/WebPort/web/ui-assets/font/Message.png")
    manifest_path = Path("SF3000/WebPort/web/ui-assets/assets.manifest.json")

    @unittest.skipUnless(raw_path.is_file() and baked_path.is_file() and manifest_path.is_file(),
                         "Message font assets are not present")
    def test_baked_font_matches_browser_decoder_and_glyph_boundaries(self):
        python_pixels, width, height, metadata = font_atlas_pixels(
            self.raw_path.read_bytes()
        )
        browser = browser_font_atlas()

        self.assertEqual((width, height), (browser["atlasWidth"], browser["atlasHeight"]))
        self.assertEqual(python_pixels, browser["pixels"])

        for character in ("a", "T"):
            glyph = ord(character) - metadata["first_char"]
            self.assertGreaterEqual(glyph, 0)
            self.assertLessEqual(ord(character), metadata["last_char"])
            base_x = (glyph % 16) * metadata["char_width"]
            base_y = (glyph // 16) * metadata["char_height"]
            glyph_alpha = [
                python_pixels[((base_y + y) * width + base_x + x) * 4 + 3]
                for y in range(metadata["char_height"])
                for x in range(metadata["char_width"])
            ]
            self.assertIn(255, glyph_alpha, f"{character} must remain visible")

        expected, expected_width, expected_height = scale_font_atlas(
            python_pixels, width, height, metadata["char_width"], metadata["char_height"]
        )
        baked_pixels, baked_width, baked_height = read_rgba_png(self.baked_path)
        manifest = json.loads(self.manifest_path.read_text())
        font = manifest["fonts"]["Message"]
        metrics = json.loads(
            (self.manifest_path.parent / font["metrics"]["path"]).read_text()
        )
        self.assertEqual((baked_width, baked_height),
                         (metrics["atlas_width"], metrics["atlas_height"]))
        self.assertEqual(font_glyph_sprites(expected, expected_width, metadata),
                         self._atlas_glyphs(baked_pixels, baked_width, metrics))

    @staticmethod
    def _atlas_glyphs(pixels, atlas_width, metrics):
        glyphs = {}
        for index, glyph in metrics["glyphs"].items():
            source_x, source_y, glyph_width, glyph_height = glyph["source_rect"]
            packed = bytearray(glyph_width * glyph_height * 4)
            for row in range(glyph_height):
                source = ((source_y + row) * atlas_width + source_x) * 4
                destination = row * glyph_width * 4
                packed[destination:destination + glyph_width * 4] = \
                    pixels[source:source + glyph_width * 4]
            glyphs[int(index) - metrics["first_char"]] = (
                bytes(packed), glyph_width, glyph_height
            )
        return glyphs


class AlphabetCelRegressionTests(unittest.TestCase):
    raw_path = Path("web-assets/raw/SF_Resources/Cels/Game.Alphabet")
    baked_path = Path("SF3000/WebPort/web/ui-assets/font/Alphabet.png")
    manifest_path = Path("SF3000/WebPort/web/ui-assets/assets.manifest.json")

    def test_pmode_alpha_becomes_ordinary_opaque_coverage(self):
        source = bytes([
            31, 0, 0, 128, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,
        ])

        baked, _width, _height = scale_game_alphabet(source, 2, 2)

        self.assertEqual(max(baked[3::4]), 255)

    def test_equivalent_pmode_alpha_is_normalised_before_scaling(self):
        source = bytes([20, 40, 60, 128, 0, 0, 0, 0])

        normalised = normalise_game_pmode_alpha(source)

        self.assertEqual(normalised, bytes([20, 40, 60, 255, 0, 0, 0, 0]))

    def test_game_cel_pmode_audit_rejects_differing_pixc_halves(self):
        self.assertTrue(game_cel_pmode_is_equivalent(0x00800080, 0x3FAE4300))
        self.assertFalse(game_cel_pmode_is_equivalent(0x00800000, 0x3FAE4300))

    def test_unpremultiply_rgba_preserves_straight_colour_and_clears_zero_alpha(self):
        pixels = bytes([25, 50, 100, 128, 99, 99, 99, 0])

        straight = unpremultiply_rgba(pixels)

        self.assertEqual(straight, bytes([50, 100, 199, 128, 0, 0, 0, 0]))

    def test_planet_matte_softens_only_the_dark_outer_band(self):
        width = height = 9
        pixels = bytearray(width * height * 4)
        for y in range(height):
            for x in range(width):
                distance = math.hypot(x - 4, y - 4)
                if distance > 4:
                    continue
                offset = (y * width + x) * 4
                level = 24 if distance > 3 else 180
                pixels[offset:offset + 4] = bytes([level, level, level, 255])

        matted = matte_pyramid_planet(bytes(pixels), width, height)
        edge = (4 * width + 8) * 4
        centre = (4 * width + 4) * 4

        self.assertEqual(matted[edge:edge + 3], pixels[edge:edge + 3])
        self.assertLess(matted[edge + 3], 255)
        self.assertEqual(matted[centre + 3], 255)

    def test_common_game_scaling_uses_fourfold_xbr_dimensions(self):
        source = bytes([
            255, 0, 0, 255, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,
        ])

        scaled, width, height = scale_game_sprite(source, 2, 2)

        self.assertEqual((width, height), (8, 8))
        self.assertTrue(any(0 < alpha < 255 for alpha in scaled[3::4]))

    @unittest.skipUnless(raw_path.is_file() and baked_path.is_file() and manifest_path.is_file(),
                         "Alphabet CEL assets are not present")
    def test_uppercase_t_bake_preserves_literal_run_width(self):
        indices, width, height, palette, _encoding = parse_grouped_game(
            grouped_records(self.raw_path.read_bytes())[19]
        )
        source_pixels = rgba_pixels_from_indices(
            indices, width, height, palette, True, True
        )
        expected, expected_width, expected_height = scale_game_alphabet(
            source_pixels, width, height
        )
        baked_pixels, baked_width, baked_height = read_rgba_png(self.baked_path)
        manifest = json.loads(self.manifest_path.read_text())
        metrics = json.loads(
            (self.manifest_path.parent /
             manifest["fonts"]["Alphabet"]["metrics"]["path"]).read_text()
        )
        source_x, source_y, glyph_width, glyph_height = \
            metrics["glyphs"]["19"]["source_rect"]
        packed = bytearray(glyph_width * glyph_height * 4)
        for row in range(glyph_height):
            source = ((source_y + row) * baked_width + source_x) * 4
            destination = row * glyph_width * 4
            packed[destination:destination + glyph_width * 4] = \
                baked_pixels[source:source + glyph_width * 4]

        self.assertEqual((width, height), (8, 16))
        self.assertEqual((glyph_width, glyph_height), (expected_width, expected_height))
        self.assertEqual(bytes(packed), expected)
        self.assertIn(255, packed[3::4])


class UiAssetOutputTests(unittest.TestCase):
    def test_preserves_editable_pngs_but_rewrites_generated_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            texture = output / "font" / "Message.png"
            metadata = output / "font" / "Message.metrics.json"
            texture.parent.mkdir()
            texture.write_bytes(b"editable texture")
            metadata.write_bytes(b"{\r\n}\r\n")

            texture_entry = write_ui_texture(
                output, Path("font") / "Message.png", b"generated texture", False
            )
            metadata_entry = write_ui_texture(
                output, Path("font") / "Message.metrics.json", b"{\n}\n", False
            )

            self.assertEqual(texture.read_bytes(), b"editable texture")
            self.assertEqual(texture_entry["size"], len(b"editable texture"))
            self.assertEqual(metadata.read_bytes(), b"{\n}\n")
            self.assertEqual(metadata_entry["size"], len(b"{\n}\n"))


class WorldTextureConversionTests(unittest.TestCase):
    raw_font_path = Path("web-assets/raw/SF_Resources/Fonts/Message")

    @staticmethod
    def grouped(records: list[bytes]) -> bytes:
        table_length = len(records) * 4
        offsets = []
        offset = table_length
        for record in records:
            offsets.append(offset)
            offset += len(record)
        return struct.pack(f">{len(offsets)}I", *offsets) + b"".join(records)

    @staticmethod
    def palette(count: int, colour: int) -> bytes:
        return b"".join(struct.pack(">H", colour if index == 1 else 0)
                        for index in range(count))

    @classmethod
    def grouped_16(cls, colour: int) -> bytes:
        return (
            struct.pack(">I", 0) + cls.palette(16, colour) +
            struct.pack(">II", ((16 - 1) << 6) | 3, 16 - 1) +
            bytes([0x10]) + bytes(127)
        )

    @classmethod
    def grouped_32(cls, colour: int) -> bytes:
        return (
            struct.pack(">II", 0, 0) + cls.palette(16, colour) +
            struct.pack(">II", ((4 - 1) << 6) | 3, 4 - 1) +
            bytes([0x10]) + bytes(31)
        )

    @unittest.skipUnless(raw_font_path.is_file(), "Message font asset is not present")
    def test_manifest_builds_deterministic_world_atlases_and_browser_images(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "raw"
            resources = root / "SF_Resources"
            cels = resources / "Cels"
            palettes = resources / "Palettes"
            sky = resources / "Sky"
            images = resources / "Images"
            map_directory = resources / "Missions" / "Maps" / "ACADEMY1" / "BLNKBLNK"
            for directory in (cels, palettes, sky, images, map_directory,
                              resources / "Fonts", resources / "Data",
                              resources / "Info", resources / "Text" / "English",
                              resources / "Animations"):
                directory.mkdir(parents=True, exist_ok=True)
            (resources / "Fonts" / "Message").write_bytes(self.raw_font_path.read_bytes())
            configuration = bytearray(556)
            configuration[0] = 2
            configuration[4] = 1
            configuration[56:60] = b"ACE\0"
            struct.pack_into(">i", configuration, 24, -12)
            struct.pack_into(">i", configuration, 96, 12345)
            (resources / "Data" / "CONFIG").write_bytes(configuration)
            metadata = bytearray(24)
            struct.pack_into(">I", metadata, 0, 0xABCDEF01)
            metadata[17] = 1
            struct.pack_into(">I", metadata, 20, 800)
            (resources / "Info" / "Planet.info").write_bytes(metadata)
            (resources / "Text" / "English" / "Game").write_bytes(
                b"| Header\r\n000 Game text\r\n002 Later text\r\n#\r\n"
            )
            (resources / "Text" / "English" / "Menu").write_bytes(
                b"000 Menu text\r\n#\r\n"
            )
            (resources / "Text" / "English" / "Title").write_bytes(
                b"000 Title text\r\n#\r\n"
            )
            (resources / "Animations" / "Planet.anim").write_bytes(
                struct.pack(">11I", 1, 4, 1, 0, 0, 1, 0, 1, 0, 0, 0)
            )

            red = 0x7C00
            green = 0x03E0
            blue = 0x001F
            (cels / "Planet.16").write_bytes(self.grouped((
                self.grouped_16(red), self.grouped_16(green)
            )))
            (cels / "Shared.16").write_bytes(self.grouped((
                self.grouped_16(red),
            )))
            (cels / "Planet.32").write_bytes(self.grouped((
                self.grouped_32(blue), struct.pack(">I", 4) + bytes(43)
            )))
            (cels / "Planet.4").write_bytes(
                struct.pack(">4I", 1 << 26, 0, 0, 0)
            )
            (palettes / "Planet.pal").write_bytes(self.palette(32, green))

            sky_data = bytearray(struct.pack(">II", 0x1234, 0x5678))
            for index in range(254):
                sky_data.extend(struct.pack(">HH", index, index))
            (sky / "Planet").write_bytes(sky_data)
            backdrop = bytearray(320 * 240 * 2)
            struct.pack_into(">H", backdrop, 0, red)
            struct.pack_into(">H", backdrop, len(backdrop) - 2, blue)
            (images / "Menu").write_bytes(backdrop)
            height_map = bytes(range(256)) * 256
            tile_map = bytes(reversed(range(256))) * 256
            polygon_map = bytes(range(128)) * 128
            (map_directory / "H_MAP").write_bytes(height_map)
            (map_directory / "P_MAP").write_bytes(polygon_map)
            (map_directory / "S_MAP").write_bytes(tile_map + bytes((8, 13)))
            invalid_map = resources / "Missions" / "Maps" / "ACADEMY1" / "INVALID"
            invalid_map.mkdir()
            (invalid_map / "S_MAP").write_bytes(bytes(1))
            mission_directory = resources / "Missions" / "A"
            mission_directory.mkdir()
            (mission_directory / "MISS_1").write_bytes(bytes(4924))

            first = build_ui_texture_manifest(root, Path(temporary) / "first", True)
            second = build_ui_texture_manifest(root, Path(temporary) / "second", True)
            self.assertEqual(first, second)
            self.assertEqual(
                first["schema_paths"]["texture-animation-v1"],
                "../../schemas/texture-animation-v1.schema.json"
            )
            message_font = first["fonts"]["Message"]
            font_metrics = json.loads(
                (Path(temporary) / "first" / message_font["metrics"]["path"]).read_text()
            )
            self.assertEqual(font_metrics["schema"], "font-metrics-v1")
            self.assertEqual(font_metrics["image"], message_font["path"])
            self.assertEqual(len(font_metrics["glyphs"]), 96)
            self.assertEqual(
                font_metrics["glyphs"]["65"]["advance"],
                font_atlas_pixels(self.raw_font_path.read_bytes())[3]["glyph_widths"][65 - 32]
            )
            self.assertEqual(
                json.loads((Path(temporary) / "first" /
                            first["text"]["English/Game"]["path"]).read_text()),
                ["Game text", "", "Later text"]
            )
            configuration_asset = json.loads(
                (Path(temporary) / "first" / first["configuration"]["path"]).read_text()
            )
            self.assertEqual(configuration_asset["schema"], "game-configuration-v1")
            self.assertEqual(configuration_asset["stick_bounds"]["x_min"], -12)
            self.assertEqual(configuration_asset["pilot"], "ACE")
            self.assertEqual(configuration_asset["high_scores"][0][0]["score"], 12345)
            self.assertEqual(first["world_metadata"]["schema"],
                             "world-metadata-v1")
            world_metadata = json.loads(
                (Path(temporary) / "first" / first["world_metadata"]["path"]).read_text()
            )
            self.assertEqual(world_metadata["worlds"]["Planet"]["explosion1_data1"],
                             0xABCDEF01)
            self.assertEqual(world_metadata["worlds"]["Planet"]["space_mission"], 1)
            self.assertEqual(world_metadata["worlds"]["Planet"]["comet_rate"], 800)
            animation_asset = json.loads(
                (Path(temporary) / "first" /
                 first["texture_animations"]["Planet"]["path"]).read_text()
            )
            self.assertEqual(animation_asset, {
                "schema": "texture-animation-v1",
                "animations": [{
                    "enabled": True,
                    "initial_countdown": 0,
                    "frame_interval": 0,
                    "initial_frame": 0,
                    "frame_count": 1,
                    "loop_frame": 0,
                    "sprites": [{"destination": 0, "frames": [0]}],
                }],
            })

            world = first["world_sets"]["Planet"]
            self.assertEqual(set(world), {"16", "32", "material_descriptors"})
            self.assertEqual(world["material_descriptors"]["schema"],
                             "static-cel-materials-v1")
            self.assertEqual(world["material_descriptors"]["byte_length"], 4108)
            self.assertEqual(len(world["32"]["unconverted"]), 1)
            self.assertEqual(world["16"]["grid"], {
                "atlas": "world/Planet/16/atlas-00.png",
                "columns": 16,
                "tile_width": 16,
                "tile_height": 16,
                "tile_count": 1,
            })
            self.assertEqual(world["16"]["materials"], [
                {"scope": "shared", "slot": 0},
                {"scope": "world", "slot": 0},
            ])
            self.assertEqual(first["shared_world_atlases"]["16"]["grid"], {
                "atlas": "world/shared/16/atlas-00.png",
                "columns": 16,
                "tile_width": 16,
                "tile_height": 16,
                "tile_count": 1,
            })
            self.assertEqual(world["32"]["grid"]["tile_width"], 32)
            self.assertEqual(world["32"]["grid"]["tile_height"], 32)
            atlas_pixels, atlas_width, _atlas_height = read_rgba_png(
                Path(temporary) / "first" / world["16"]["grid"]["atlas"]
            )
            self.assertEqual(atlas_pixels[:4], bytes([0, 255, 0, 255]))
            shared_pixels, _, _ = read_rgba_png(
                Path(temporary) / "first" /
                first["shared_world_atlases"]["16"]["grid"]["atlas"]
            )
            self.assertEqual(shared_pixels[:4], bytes([255, 0, 0, 255]))

            sky_pixels, sky_width, sky_height = read_rgba_png(
                Path(temporary) / "first" / first["sky"]["Planet"]["path"]
            )
            self.assertEqual((sky_width, sky_height), (256, 1))
            self.assertEqual(sky_pixels[:4], bytes([0, 0, 0, 255]))
            self.assertEqual(sky_pixels[253 * 4:254 * 4], bytes([0, 57, 238, 255]))
            self.assertEqual(sky_pixels[254 * 4:], bytes([0, 0, 0, 255]) * 2)

            backdrop_pixels, backdrop_width, backdrop_height = read_rgba_png(
                Path(temporary) / "first" / first["backdrops"]["Menu"]["path"]
            )
            self.assertEqual((backdrop_width, backdrop_height), (320, 240))
            self.assertEqual(backdrop_pixels[:4], bytes([255, 0, 0, 255]))
            self.assertEqual(backdrop_pixels[-4:], bytes([0, 0, 255, 255]))

            maps = first["maps"]["ACADEMY1/BLNKBLNK"]
            height_pixels, map_width, map_height = read_rgba_png(
                Path(temporary) / "first" / maps["height"]["path"]
            )
            tile_pixels, _, _ = read_rgba_png(Path(temporary) / "first" / maps["tile"]["path"])
            polygon_pixels, polygon_width, polygon_height = read_rgba_png(
                Path(temporary) / "first" / maps["polygon"]["path"]
            )
            self.assertEqual((map_width, map_height), (256, 256))
            self.assertEqual(height_pixels[:8], bytes([0, 0, 0, 255, 1, 0, 0, 255]))
            self.assertEqual(tile_pixels[:8], bytes([255, 0, 0, 255, 254, 0, 0, 255]))
            self.assertEqual((polygon_width, polygon_height), (128, 128))
            self.assertEqual(polygon_pixels[:8], bytes([0, 0, 0, 255, 1, 0, 0, 255]))
            self.assertEqual(maps["tile"]["source_size"], 256 * 256)
            invalid = first["maps"]["ACADEMY1/INVALID"]
            self.assertIsNone(invalid["height"])
            self.assertIsNone(invalid["tile"])
            self.assertEqual(len(invalid["unconverted"]), 3)
            mission = first["missions"]["A/1"]
            self.assertEqual(mission["schema"], "mission-data-v1")
            self.assertEqual(mission["byte_length"], 4924)
            self.assertEqual(
                (Path(temporary) / "first" / mission["path"]).read_bytes(),
                bytes(4924)
            )

    @unittest.skipUnless(
        Path("web-assets/raw/SF_Resources/Graphics/EARTH").is_file() and
        Path("web-assets/raw/SF_Resources/Cels/Game.Earth").is_file(),
        "graphics and Game CEL source assets are not present"
    )
    def test_manifest_content_addresses_legacy_graphics_payloads(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "ui"
            manifest = build_ui_texture_manifest(Path("web-assets/raw"), output, True)

            earth = manifest["world_graphics"]["Earth"]
            ice = manifest["world_graphics"]["Ice"]
            expected_graphics, graphics_schema, _words = normalize_graphics(
                Path("web-assets/raw/SF_Resources/Graphics/EARTH").read_bytes()
            )
            self.assertEqual(earth["schema"], graphics_schema)
            self.assertEqual(earth["path"], ice["path"])
            self.assertEqual(earth["byte_length"], len(expected_graphics))
            self.assertEqual((output / earth["path"]).read_bytes(), expected_graphics)

            game = manifest["game_sets"]["Earth"]["legacy_data"]
            expected_game, game_schema, _words = normalize_cel_offsets(
                Path("web-assets/raw/SF_Resources/Cels/Game.Earth").read_bytes()
            )
            self.assertEqual(game["schema"], game_schema)
            self.assertEqual(game["byte_length"], len(expected_game))
            self.assertEqual((output / game["path"]).read_bytes(), expected_game)
            for name, game_set in manifest["game_sets"].items():
                legacy_data = game_set.get("legacy_data")
                if legacy_data is None:
                    continue
                capacity = 192 * 1024 if name == "Pyramid" else 44 * 1024
                self.assertLessEqual(
                    legacy_data["byte_length"], capacity,
                    f"Game.{name} exceeds its browser CEL buffer"
                )

            sky = manifest["sky"]["Earth"]["legacy_data"]
            self.assertEqual(sky["byte_length"], 1024)
            self.assertEqual(
                (output / sky["path"]).read_bytes(),
                Path("web-assets/raw/SF_Resources/Sky/Earth").read_bytes()
            )
            self.assertEqual(
                manifest["compatibility"]["monochrome_palette"]["byte_length"], 1024
            )
            alphabet = manifest["fonts"]["Alphabet"]
            alphabet_metrics = json.loads((output / alphabet["metrics"]["path"]).read_text())
            self.assertEqual(alphabet_metrics["schema"], "font-metrics-v1")
            self.assertEqual(alphabet_metrics["render_style"], "texture-coverage")
            self.assertEqual(alphabet_metrics["glyphs"]["0"]["advance"],
                             ALPHABET_GLYPH_ADVANCES[0])
            self.assertEqual(alphabet_metrics["glyphs"]["38"]["advance"],
                             ALPHABET_GLYPH_ADVANCES[38])
            self.assertNotIn("legacy_data", manifest["game_sets"]["Alphabet"])
            self.assertEqual(manifest["game_sets"]["Alphabet"]["sprites"], {})

    def test_sky_backdrop_and_map_helpers_validate_and_preserve_pixels(self):
        sky = bytearray(8)
        for index in range(254):
            sky.extend(struct.pack(">HH", 0x7C00 if index == 0 else 0x001F,
                                   0x7C00 if index == 0 else 0x001F))
        sky_pixels = sky_rgba_pixels(sky)
        self.assertEqual(sky_pixels[:4], bytes([255, 0, 0, 255]))
        self.assertEqual(sky_pixels[253 * 4:254 * 4], bytes([0, 0, 255, 255]))
        self.assertEqual(sky_pixels[-8:], bytes([0, 0, 0, 255]) * 2)
        invalid_sky = bytearray(sky)
        struct.pack_into(">H", invalid_sky, 8, 0x8000)
        with self.assertRaisesRegex(ConversionError, "high-bit"):
            sky_rgba_pixels(invalid_sky)

        backdrop = bytearray(320 * 240 * 2)
        struct.pack_into(">H", backdrop, 2, 0x03E0)
        struct.pack_into(">H", backdrop, 4, 0x7C00)
        pixels = backdrop_rgba_pixels(backdrop)
        self.assertEqual(pixels[320 * 4:320 * 4 + 4], bytes([0, 255, 0, 255]))
        self.assertEqual(pixels[4:8], bytes([255, 0, 0, 255]))
        with self.assertRaisesRegex(ConversionError, "320x240"):
            backdrop_rgba_pixels(bytes(backdrop[:-1]))

        source = bytes(range(256)) * 256
        map_pixels = map_rgba_pixels(source)
        self.assertEqual(map_pixels[:8], bytes([0, 0, 0, 255, 1, 0, 0, 255]))
        self.assertEqual(map_pixels[-4:], bytes([255, 0, 0, 255]))
        self.assertEqual(map_rgba_pixels(source + bytes((8, 13))), map_pixels)
        with self.assertRaisesRegex(ConversionError, "256x256"):
            map_rgba_pixels(source[:-1])

        world_pixels = bytes([0, 0, 255, 255]) * 64
        scaled_world, scaled_width, scaled_height = scale_world_cel_32(
            world_pixels, 8, 8
        )
        self.assertEqual((scaled_width, scaled_height), (32, 32))
        self.assertEqual(scaled_world[:4], bytes([0, 0, 255, 255]))

    def test_runtime_manifest_excludes_browser_native_world_cels(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "raw"
            cels = root / "SF_Resources" / "Cels"
            maps = root / "SF_Resources" / "Missions" / "Maps" / "MAP" / "VAR"
            graphics = root / "SF_Resources" / "Graphics"
            palettes = root / "SF_Resources" / "Palettes"
            sky = root / "SF_Resources" / "Sky"
            fonts = root / "SF_Resources" / "Fonts"
            data = root / "SF_Resources" / "Data"
            info = root / "SF_Resources" / "Info"
            text = root / "SF_Resources" / "Text" / "English"
            animations = root / "SF_Resources" / "Animations"
            cels.mkdir(parents=True)
            maps.mkdir(parents=True)
            graphics.mkdir()
            palettes.mkdir()
            sky.mkdir()
            fonts.mkdir()
            data.mkdir()
            info.mkdir()
            text.mkdir(parents=True)
            animations.mkdir()
            (cels / "Planet.4").write_bytes(bytes(16))
            (cels / "Game.Planet").write_bytes(bytes(16))
            (maps / "H_MAP").write_bytes(bytes(256 * 256))
            (maps / "S_MAP").write_bytes(bytes(256 * 256))
            (graphics / "PLANET").write_bytes(bytes(16))
            (palettes / "Planet.pal").write_bytes(bytes(16))
            (sky / "Planet").write_bytes(bytes(16))
            (fonts / "Message").write_bytes(bytes(16))
            (data / "Cosine").write_bytes(bytes(16))
            (data / "Tangent").write_bytes(bytes(16))
            (data / "Palette").write_bytes(bytes(16))
            (info / "Planet.info").write_bytes(bytes(24))
            (text / "Game").write_bytes(b"000 Text\r\n#\r\n")
            (animations / "Planet.anim").write_bytes(bytes(16))
            runtime = Path(temporary) / "runtime"
            stale = runtime / "SF_Resources"
            (stale / "Cels").mkdir(parents=True)
            (stale / "Graphics").mkdir()
            (stale / "Palettes").mkdir()
            (stale / "Sky").mkdir()
            (stale / "Fonts").mkdir()
            (stale / "Data").mkdir()
            (stale / "Info").mkdir()
            (stale / "Text" / "English").mkdir(parents=True)
            (stale / "Animations").mkdir()
            (stale / "Cels" / "Game.Planet").write_bytes(bytes(16))
            (stale / "Graphics" / "PLANET").write_bytes(bytes(16))
            (stale / "Palettes" / "Planet.pal").write_bytes(bytes(16))
            (stale / "Sky" / "Planet").write_bytes(bytes(16))
            (stale / "Fonts" / "Message").write_bytes(bytes(16))
            (stale / "Data" / "Cosine").write_bytes(bytes(16))
            (stale / "Data" / "Tangent").write_bytes(bytes(16))
            (stale / "Data" / "Palette").write_bytes(bytes(16))
            (stale / "Info" / "Planet.info").write_bytes(bytes(16))
            (stale / "Text" / "English" / "Game").write_bytes(bytes(16))
            (stale / "Animations" / "Planet.anim").write_bytes(bytes(16))

            manifest = build_runtime_manifest(Path(temporary), root,
                                              runtime)

            self.assertEqual(manifest["assets"], [])
            self.assertEqual(manifest["excluded_file_patterns"],
                             ["SF_Resources/Fonts/*",
                              "SF_Resources/Cels/*.4",
                              "SF_Resources/Cels/*.16",
                              "SF_Resources/Cels/*.32",
                              "SF_Resources/Cels/Game.*",
                              "SF_Resources/Animations/*",
                              "SF_Resources/Data/*",
                              "SF_Resources/Data/Cosine",
                              "SF_Resources/Data/Tangent",
                              "SF_Resources/Info/*",
                              "SF_Resources/Text/*"])
            self.assertFalse((stale / "Cels" / "Game.Planet").exists())
            self.assertFalse((stale / "Graphics" / "PLANET").exists())
            self.assertFalse((stale / "Palettes" / "Planet.pal").exists())
            self.assertFalse((stale / "Sky" / "Planet").exists())
            self.assertFalse((stale / "Fonts" / "Message").exists())
            self.assertFalse((stale / "Data" / "Cosine").exists())
            self.assertFalse((stale / "Data" / "Tangent").exists())
            self.assertFalse((stale / "Data" / "Palette").exists())
            self.assertFalse((stale / "Info" / "Planet.info").exists())
            self.assertFalse((stale / "Text" / "English" / "Game").exists())
            self.assertFalse((stale / "Animations" / "Planet.anim").exists())

    def test_text_resources_convert_to_json_string_arrays(self):
        with tempfile.TemporaryDirectory() as temporary:
            source_root = Path(temporary) / "SF_Resources"
            text_root = source_root / "Text" / "English"
            text_root.mkdir(parents=True)
            game = text_root / "Game"
            briefing = text_root / "T" / "BRIEF_1"
            briefing.parent.mkdir()
            game.write_bytes(b"| Header\r\n000 One\r\n002 Three\r\n#\r\n")
            briefing.write_bytes(b"First line\r\n\r\nLast line\r\n#\r\n")

            output = Path(temporary) / "output"
            game_result = convert_one("SF_Resources", source_root, game, output, False)
            briefing_result = convert_one("SF_Resources", source_root, briefing,
                                          output, False)

            game_output = game_result["outputs"][1]
            briefing_output = briefing_result["outputs"][1]
            self.assertEqual(game_output["mime"], "application/json")
            self.assertEqual(game_output["type"], "legacy-text-indexed-v1")
            self.assertEqual(game_output["strings"], 3)
            self.assertEqual(json.loads((output / game_output["path"]).read_text()),
                             ["One", "", "Three"])
            self.assertEqual(briefing_output["type"], "legacy-text-lines-v1")
            self.assertEqual(json.loads((output / briefing_output["path"]).read_text()),
                             ["First line", "", "Last line"])


if __name__ == "__main__":
    unittest.main()
