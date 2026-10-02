# Star Fighter

This repository contains the source code and resources to the 1995 3DO
title Star Fighter.

* https://en.wikipedia.org/wiki/Star_Fighter_(video_game)
* https://en.wikipedia.org/wiki/Krisalis_Software

> _**Star Fighter**_ or _**Star Fighter 3000**_ is a 3D flight-based
> shoot-em-up developed and published by UK company Fednet Software,
> and released in 1994 for the [Acorn
> Archimedes](https://en.wikipedia.org/wiki/Acorn_Archimedes "Acorn
> Archimedes"). The gameplay is mission based and involves elements of
> strategy and planning. The player can order wingmen to fly in
> formation and attack specific
> targets.[[2]](https://en.wikipedia.org/wiki/Star_Fighter_(video_game)#cite_note-2)
>
> The 3DO version was developed by Tim Parry and Andrew Hutchings. It
> was developed after the original Acorn version was released. This
> version is slightly different from the original RISC OS game. The
> map screen is in 3D, not 2D as in the Acorn RISC OS version. Also,
> to upgrade the ship the player must collect a series of 3D shapes
> after blowing up certain objects. In the Acorn RISC OS version, the
> player collects and spends money on ship upgrades. Another
> difference is that the player can blast pathways through mountain
> ranges with the laser.

The source code was made available by Nathan Atkinson with permission
by Andrew Hutchings on 2024-04-24 after Mr. Atkinson was contacted by
3DO community member `shaun_3dohd` and asked about the possibility of
releasing the source.

## Contents

The code appears to be complete with most if not all media assets in
their final processed forms (CEL files for images, AIFC for audio, 3DO
Cinepak Stream files for video, etc.)

There are a number of miscellaneous utilities / tools present but
given the title was ported from Acorn Archimedes and RISC OS tools for
level design and original media creation are not included here (and
their whereabouts are unknown.)

The 3DO SDK ran on classic MacOS so all text files (.c, .s, .h,
makefiles, etc.) are in a classic MacOS format in the original archive
and therefore the [initial
commit.](https://github.com/trapexit/star_fighter_3do/tree/initial_commit)

The archive also included executables, object and debug files,
etc. which have been kept in the initial commit for thoroughness and
posterity.

## Future Plans

Like with other 3DO source code releases there is a intent to port the
title to build against the [3do-devkit
project](https://github.com/trapexit/3do-devkit) in order to provide a
convenient base for new development.

## WebAssembly port

`SF3000\WebPort` contains C11 replacements for each 3DO ARM renderer and
gameplay module. `SF3000\WebPort\CMakeLists.txt` explicitly selects those C
sources and does not include any `.s` file; configure it with Emscripten for a
wasm32 library build.

The initial web renderer keeps the game's ordered cel semantics by emitting
320x240 `SFWebRenderQuad` commands. `SF3000\WebPort\web` provides a WebGL 2
consumer that uses nearest-neighbour sampling and projective texture mapping.
Call `sf_web_port_renderer_initialise()` before entering the game loop and
`sf_web_port_renderer_begin_frame()` before each rendered frame; the browser
host reads the command buffer through the remaining `sf_web_port_renderer_*`
accessors.

Build the browser asset mirror and verify it with:

```powershell
python tools\convert_assets.py --build-runtime
python tools\convert_assets.py --verify-runtime
```

This preserves `web-assets\raw\SF_Resources` byte-for-byte and generates a
separate, media-free `web-assets\runtime\SF_Resources` tree with the same
logical paths. Structured 3DO records are normalized to wasm-native endian
order at build time, while CEL payloads, palettes, maps, and mixed-layout mesh
bitstreams remain raw. `build-web.ps1` regenerates this mirror by default.
Music, video, voice-over, samples, and the archival reference-asset trees
remain excluded from the Wasm data package. The short PCM sound effects are
separately converted to WAV, while music and briefing narration are converted
to streamed Ogg Opus files and fetched on demand by the browser audio runtime.
3DO Cinepak/SDX2 cinematics are demuxed and transcoded to H.264/AAC MP4 files
that are fetched on demand and presented through the WebGL canvas. The full
browser build requires FFmpeg with `libopus`, `libx264`, and AAC support; use
`-SkipMedia` for renderer-only incremental builds.

The full conversion contract, supported formats, and intentionally preserved
native containers are documented in `tools\ASSET_CONVERSION.md`.

### Browser saves

Web builds map the game's `/NVRAM` configuration and save slots to
origin-scoped `localStorage`. Save data remains in the native binary format,
base64-encoded under `starfighter:nvram:` keys. Clearing the site's browser
data removes these saves; this is local persistence only and does not provide
cloud synchronisation.
