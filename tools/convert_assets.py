#!/usr/bin/env python3
"""Build a deterministic browser-asset package from the preserved 3DO media."""

from __future__ import annotations

import argparse
import hashlib
import json
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
FULL_SHEET_BYTES = 320 * 256 * 2
RAW_IMAGE_BYTES = 320 * 240 * 2
MANIFEST_NAME = "assets.manifest.json"
RUNTIME_SCHEMA_VERSION = 1
RUNTIME_GENERATOR = "tools/convert_assets.py --build-runtime"
SOUND_EFFECTS_GENERATOR = "tools/convert_assets.py --sound-effects-only"
STREAMED_MEDIA_GENERATOR = "tools/convert_assets.py --streamed-media-only"
CINEMATICS_GENERATOR = "tools/convert_assets.py --cinematics-only"


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
    if len(values) != width * height:
        raise ConversionError("direct-colour CEL has an unexpected pixel count")
    pixels = bytearray(width * height * 4)
    for index, word in enumerate(values):
        if word == -1:
            continue
        pixels[index * 4] = ((word >> 10) & 0x1F) * 255 // 31
        pixels[index * 4 + 1] = ((word >> 5) & 0x1F) * 255 // 31
        pixels[index * 4 + 2] = (word & 0x1F) * 255 // 31
        pixels[index * 4 + 3] = 255
    return png_rgba(width, height, bytes(pixels))


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
CCB_LDPLUT = 1 << 23


def cel_height(preamble0: int) -> int:
    return ((preamble0 >> 6) & 0x3FF) + 1


def cel_width(preamble1: int) -> int:
    return (preamble1 & 0x7FF) + 1


def decode_packed_rows(payload: bytes, height: int, bits_per_pixel: int,
                       expected_width: int | None = None) -> tuple[list[int], int]:
    """Decode the row-aligned RLE written by Get16Data/Get32Data."""
    if height <= 0 or height > 1024:
        raise ConversionError("packed CEL height is out of range")
    offset = 0
    pixels: list[int] = []
    width = expected_width
    for row_index in range(height):
        if offset >= len(payload):
            raise ConversionError("packed CEL ends before all rows")
        row_size = (payload[offset] + 2) * 4
        if row_size > len(payload) - offset:
            raise ConversionError("packed CEL row offset exceeds payload")
        reader = BitReader(payload[offset + 1:offset + row_size])
        row: list[int] = []
        while True:
            if width is not None and len(row) == width:
                break
            command = reader.read(2)
            if command == 0:
                if reader.read(2) != 0:
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
        if not reader.remaining_is_zero():
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
    if any(payload[offset:]):
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
    indices, width = decode_packed_rows(record[76:], height, bits_per_pixel)
    return indices, width, height, palette, f"indexed-{bits_per_pixel}bpp-packed"


def parse_grouped_game_direct(record: bytes) -> tuple[bytes, int, int]:
    if len(record) < 16:
        raise ConversionError("grouped game CEL record is too short")
    flags = struct.unpack(">I", record[4:8])[0]
    if flags & CCB_LDPLUT:
        raise ConversionError("grouped game CEL is not a direct-colour record")
    preamble0 = struct.unpack(">I", record[8:12])[0]
    if preamble0 & 7 != 6:
        raise ConversionError("grouped game CEL is not in the established 16bpp format")
    height = cel_height(preamble0)
    if flags & CCB_PACKED:
        values, width = decode_packed_rows(record[12:], height, 16)
        return direct_values_to_png(values, width, height), width, height
    preamble1 = struct.unpack(">I", record[12:16])[0]
    width = cel_width(preamble1)
    pixels = record[16:]
    if len(pixels) != width * height * 2:
        raise ConversionError("grouped direct-colour CEL payload length is inconsistent")
    return rgb555_to_png(pixels, width, height), width, height


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


def build_runtime_manifest(base: Path, source_root: Path, output: Path) -> dict:
    resources = source_root / "SF_Resources"
    if not resources.is_dir():
        raise ConversionError("--raw-root must contain SF_Resources")
    assets = []
    for source in source_files(resources):
        relative = source.relative_to(resources)
        if relative.parts[0] in DISABLED_MEDIA_DIRECTORIES:
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
                                 for directory in sorted(DISABLED_MEDIA_DIRECTORIES)],
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
    semantic_checks = (
        ("Missions/T/MISS_1", 3440, 6),
        ("Data/Cosine", 0, None),
        ("Data/Tangent", 4, None),
        ("Cels/Earth.32", 0, None),
        ("Animations/Earth.anim", 0, None),
        ("Graphics/EARTH", 0, None),
    )
    for relative_name, offset, expected in semantic_checks:
        source = source_root / "SF_Resources" / relative_name
        destination = output / "SF_Resources" / relative_name
        if not source.is_file() or not destination.is_file():
            print(f"verification failed: missing regression fixture {relative_name}",
                  file=sys.stderr)
            mismatched += 1
            continue
        original = source.read_bytes()
        emitted = destination.read_bytes()
        raw_value = struct.unpack_from(">I", original, offset)[0]
        runtime_value = struct.unpack_from("<I", emitted, offset)[0]
        if runtime_value != (raw_value if expected is None else expected):
            print(f"verification failed: normalized value mismatch in {relative_name}",
                  file=sys.stderr)
            mismatched += 1

    cel_source = (source_root / "SF_Resources" / "Cels/Earth.32").read_bytes()
    cel_runtime = (output / "SF_Resources" / "Cels/Earth.32").read_bytes()
    cel_table_bytes = struct.unpack_from(">I", cel_source, 0)[0]
    if cel_runtime[cel_table_bytes:] != cel_source[cel_table_bytes:]:
        print("verification failed: CEL payload bytes changed", file=sys.stderr)
        mismatched += 1

    graphics_relative = Path("Graphics/EARTH")
    graphics_source = (source_root / "SF_Resources" / graphics_relative).read_bytes()
    graphics_runtime = (output / "SF_Resources" / graphics_relative).read_bytes()
    _, _, normalized_words = normalize_graphics(graphics_source)
    for word in set(range(len(graphics_source) // 4)) - normalized_words:
        offset = word * 4
        if graphics_runtime[offset:offset + 4] != graphics_source[offset:offset + 4]:
            print("verification failed: graphics raw mesh or collision bytes changed",
                  file=sys.stderr)
            mismatched += 1
            break

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
    if args.build_runtime and args.verify_runtime:
        parser.error("--build-runtime and --verify-runtime cannot be used together")
    if (args.sound_effects_only or args.streamed_media_only or args.cinematics_only) and (
            args.build_runtime or args.verify_runtime or args.include_disabled_media or
            args.include_reference_assets):
        parser.error("single-purpose media conversion cannot be combined with other asset modes")
    if sum((args.sound_effects_only, args.streamed_media_only, args.cinematics_only)) > 1:
        parser.error("only one single-purpose media conversion mode may be selected")
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
