# Browser asset conversion

Run the standard-library-only pipeline from the repository root:

```powershell
python tools\convert_assets.py --clean
python tools\convert_assets.py --verify
```

By default it writes `web-assets\assets.manifest.json` plus the non-media game
files under `SF_Resources`. It excludes `Music`, `Video`, `Voices`, and
`Samples` from the initial Wasm data package and does not package the archival
`Coded8bpp` or `3DO cels` reference trees. This keeps the initial browser
payload near 8.8 MiB. The source paths are UTF-8-byte escaped (`~HH`) in
emitted paths, making the package portable while the manifest retains the
original path.
`--clean` only removes an empty directory or one carrying this tool's
manifest, and output paths must be non-source directories inside the
repository.

Use `--manifest-only --output asset-conversion-check` to inspect the complete,
deterministic inventory without producing the large package.  This mode marks
outputs as `emitted: false`; it is not a deployable package.  `--verify`
checks every emitted file's manifest size and SHA-256 hash.

Use `--include-reference-assets` to also package `Coded8bpp` and `3DO cels`.
Use `--include-disabled-media` only when inspecting archival media; it adds
the currently excluded music, video, voice-over, and sample files.

For the Web Audio sound-effect path, use:

```powershell
python tools\convert_assets.py --sound-effects-only --clean --output SF3000\WebPort\web\audio
```

This emits only the fourteen PCM effect samples as WAV plus an asset manifest.
It records native AIFF sustain-loop markers so the browser can preserve engine,
wind, alarm, and beam-laser looping without downloading any music or voice data.

For streamed music and mission narration, install FFmpeg with the `libopus`
encoder, then run:

```powershell
python tools\convert_assets.py --streamed-media-only --clean --output SF3000\WebPort\web\media
```

This independently decodes the 3DO AIFC `SDX2` payloads into signed 16-bit
PCM, then writes Ogg Opus files at 96 kbps stereo for music and 40 kbps mono
for voice-over. The manifest retains original source paths, hashes, frame
metadata, and output hashes. `build-web.ps1` generates this directory by
default; `-SkipMedia` avoids re-encoding it during an incremental browser build,
and `-Ffmpeg <path>` supplies an FFmpeg executable not present on `PATH`. On
Windows, the converter automatically uses `wsl.exe -- ffmpeg` when no native
FFmpeg is installed.

For the 3DO Cinepak cinematics, run:

```powershell
python tools\convert_assets.py --cinematics-only --clean --output SF3000\WebPort\web\video
```

This validates and demuxes the `SHDR` stream's `FILM`/Cinepak frames and
`SNDS`/SDX2 audio packets, wraps them in a temporary in-memory AVI, then uses
FFmpeg to emit H.264 baseline/AAC MP4 files. The browser fetches these files
only while a cinematic is playing and uploads decoded frames to the WebGL
canvas; the native streams are never part of `starfighter.data`.

## WebAssembly runtime mirror

The browser runtime does not mutate original 3DO data. Generate its derived
asset tree from the immutable media-free input with:

```powershell
python tools\convert_assets.py --build-runtime
python tools\convert_assets.py --verify-runtime
```

The input remains under `web-assets\raw\SF_Resources`; the generated mirror is
`web-assets\runtime\SF_Resources`, retaining every logical path and file
partition. Its `assets.manifest.json` records source and output SHA-256 hashes,
the transformation schema/version, and exact byte ranges normalized for each
file. `--verify-runtime` regenerates every output in memory from its raw input
and rejects changed source data, stale output, schema changes, or hash
mismatches.

The current field-aware schemas are:

* `mission-data-v1`: mission counters, performance values, flight-path counts,
  special-ship offsets, null pointer slots, and case-normalized map identifiers
  needed by the browser's case-sensitive filesystem.
* `game-configuration-v1`, `cosine-table-v1`, and `tangent-table-v1`: typed
  32-bit configuration and math-table values.
* `cel-offset-table-v1` and `texture-animation-v1`: container offset tables
  and animation metadata only; all CEL content remains byte-identical.
* `planet-data-v1`: only the `comet_rate` field.
* `graphics-mixed-layout-v1`: typed graphics records and gameplay data,
  preserving mesh headers, movement instructions, links, vectors, and
  collision attribute quartets as original big-endian byte streams.
* `identity-copy-v1`: every remaining file is copied byte-for-byte.

`build-web.ps1` regenerates this mirror automatically when no explicit
`-AssetRoot` is supplied. An explicit asset root must already be a normalized
runtime tree; loading `web-assets\raw` directly is intentionally unsupported.

## Established conversions

The pipeline only decodes layouts demonstrated by the checked-in utility
sources:

* Uncompressed AIFF PCM is converted to browser-supported PCM WAV.
* `--streamed-media-only` decodes 3DO AIFC `SDX2` to PCM using its independent
  per-channel square/xact/delta reconstruction, then encodes music and
  voice-over as Ogg Opus.
* `SF_Resources\Images` raw 153,600-byte payloads and the verified
  153,636-byte `IMAG` container are converted from 320×240 big-endian RGB555
  to PNG. `Utility\Get_ImageRaw.c` establishes the 36-byte header removal and
  153,600-byte payload; the RGB555 channel masks are used by
  `Utility\Get32.c`.
* `Coded8bpp` CCB/PDAT CEL files of exactly 163,928 bytes are converted from
  their demonstrated 320×256 RGB555 payload at byte 88 to PNG.  The same
  utility reads that exact payload layout.
* Native CCB/PDAT CELs with an adjacent PLUT are strictly decoded as packed
  indexed 1-, 2-, 4-, or 6-bit images.  The decoder validates CCB flags,
  CCB/preamble dimensions, PDAT and PLUT lengths, every row offset, RLE
  command, palette index, and zero padding.  Command 2 produces transparent
  PNG pixels.
* `Coded8bpp\*\Unpacked\*.data` files of exactly 512 bytes are converted from
  the 16×16 RGB555 pixels written by `Utility\Get16Data.c`.
* Grouped `.16` and `.32` CEL containers use the big-endian offset table
  emitted by `Utility\Get16.c`/`Get32.c`; their indexed CEL records become
  PNG plus a per-image palette JSON.  Both packed and unpacked `.32` records
  are validated and decoded.
* Grouped game CEL containers use the offset table emitted by
  `Utility\GetTogetherGame1.c`.  Supported PLUT records become indexed PNG;
  established direct RGB555 records become PNG.  Raw `.4` containers become
  4×4 PNG tiles, using the matching 32-entry palette and the half-bright
  mode encoded by `Utility\Get4.c`.
* 64-byte `.pal` files become a 32-entry RGB555 JSON palette, matching the
  byte order written by `Utility\GetTogetherPalette.c`; verified 1,024-byte
  monochrome palettes become their 128-entry equivalents.
* The 24-byte `SF_Resources\Info\*.info` records become JSON using the
  `planet_data` layout in `SF3000\SFlib\SF_Mission.h` and byte layout written
  by `Utility\GetInfo.c`.
* Strict UTF-8 text is copied as canonical-LF UTF-8 text.

PNG rows retain the source's first-row order.  The old utilities establish
pixel encoding but not a browser display-origin convention, so the WebGL
loader must choose any required vertical flip explicitly.
Each indexed PNG's manifest entry has a `palette` path to its emitted palette
metadata.  All emitted entries have their own MIME type, type, size, and
SHA-256 hash.

## Intentional preservation / decoder TODOs

Everything is hashed and copied even when no safe decoder is available:

* the nine packed 8-bit `Free.cel` variants use a row encoding that is not
  emitted by the checked-in utilities, so they remain raw;
* the final `Warrior.32` record has descriptor value 5, outside the sizes
  emitted by `Utility\Get32Data.c`;
* exceptional game CEL records using unsupported packed-row or descriptor
  variants remain raw;
* recognized PCX and Photoshop PSD containers remain raw until their
  compression, palette, and channel layouts are verified;
* maps, sky, graphics, mission, font, and 3DO-logo data need established
  schemas before interpretation.

The manifest reports recognized containers and a specific
`decoder_requirement` for data that remains raw; it never labels such data as
a decoded browser format.
