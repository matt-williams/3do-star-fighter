function pngOutput(asset) {
  return asset.outputs.find((output) => output.mime === "image/png");
}

export async function loadAssetManifest(url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Unable to load asset manifest: ${response.status}`);
  }

  const manifest = await response.json();
  if (manifest.schema_version !== 1 || manifest.generator !== "tools/convert_assets.py") {
    throw new Error("Unsupported asset manifest");
  }

  const assets = new Map(manifest.assets.map((asset) => [asset.source_path, asset]));
  const root = new URL(".", response.url);
  const textures = new Map();

  return {
    loadTexture(gl, sourcePath) {
      if (textures.has(sourcePath)) {
        return textures.get(sourcePath);
      }

      const texture = (async () => {
        const asset = assets.get(sourcePath);
        const output = asset === undefined ? undefined : pngOutput(asset);
        if (output === undefined || output.emitted !== true) {
          return null;
        }

        const image = new Image();
        image.src = new URL(output.path, root);
        await image.decode();

        const texture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
        gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
        gl.texImage2D(
          gl.TEXTURE_2D,
          0,
          gl.RGBA,
          gl.RGBA,
          gl.UNSIGNED_BYTE,
          image
        );
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        return texture;
      })();
      textures.set(sourcePath, texture);
      return texture;
    }
  };
}
