import unittest
import struct

from convert_assets import ConversionError, cinepak_stream_to_avi, sdx2_to_pcm


def pcm_values(data):
    return [int.from_bytes(data[offset:offset + 2], "little", signed=True)
            for offset in range(0, len(data), 2)]


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


if __name__ == "__main__":
    unittest.main()
