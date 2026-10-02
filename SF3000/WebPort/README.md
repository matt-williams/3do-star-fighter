# Star Fighter browser build

Build with an installed Emscripten SDK:

```powershell
.\SF3000\WebPort\build-web.ps1
```

The target emits `SF3000/WebPort/web/starfighter.js` and its companion Wasm
(and data file when assets are enabled), matching `web/index.html`. It preloads
the generated contents of `web-assets/runtime/SF_Resources` at the virtual filesystem
root, matching the compatibility layer's `$boot/SF_Resources/` translation.
Configuration rejects `Music`, `Video`, `Voices`, and `Samples`. Set
`-AssetRoot` to another normalized, media-free runtime root, or `-SkipAssets`
to build without assets. When no root is supplied, the build regenerates the
runtime mirror from `web-assets/raw` with `tools/convert_assets.py`.

CMake remains available for environments that provide it:

```sh
emcmake cmake -S SF3000/WebPort -B build/web -DSF_BUILD_BROWSER=ON
cmake --build build/web --target starfighter_web
```
