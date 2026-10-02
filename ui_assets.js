const GAME_CEL_ENCODING = 0x20000000;
const WORLD_ENCODING = 0x10000000;
const TARGET_OVERLAY_ENCODING = 0x00800000;
const STATIC_CEL_16_ENCODING = 12;
const STATIC_CEL_32_ENCODING = 13;
const GAME_CEL_SPRITE_MASK = 0x0000ff00;
const GAME_CEL_SPRITE_SHIFT = 8;
const STATIC_CEL_INDEX_MASK = 0x0000ff00;
const STATIC_CEL_INDEX_SHIFT = 8;

export function createBakedUiAssets(gl) {
  let gameSet = "";
  let gameAtlases;
  let gameAtlasLoading = false;
  let gameAtlasFailed = false;
  let fontImage;
  let fontMetrics;
  let fontMetricsPromise;
  let alphabetFont;
  let alphabetFontPromise;
  let renderer;
  let assetManifest;
  let assetManifestPromise;
  let worldSet = "";
  let skyName = "";
  let mapName = "";
  let backdropName = "";
  let worldAtlases = new Map();
  let sharedWorldAtlases = new Map();
  let worldLoading = false;
  let worldFailed = false;
  let worldAssetsPromise;
  let worldRequest = 0;
  let skyPixels;
  let terrainMaps;
  let backdropImage;
  const backdropLoads = new Map();
  const missionRecordLoads = new Map();
  const polygonMapLoads = new Map();
  const terrainMapLoads = new Map();
  const fontLoads = new Map();
  const textLoads = new Map();
  const textureAnimationLoads = new Map();
  let configurationPromise;
  let worldMetadataPromise;

  const manifestUrl = new URL("./ui-assets/assets.manifest.json", import.meta.url);

  function bakedGameTextureOptions(set, sprite) {
    const options = { usesTextureCoverage: true };
    if (set === "Pyramid" && sprite >= 0 && sprite < 16) {
      options.coverageBlendOperation = "alpha";
    }
    return options;
  }

  function createAtlasTexture(image, filter = gl.LINEAR) {
    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, true);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return texture;
  }

  function loadAssetManifest() {
    if (assetManifestPromise !== undefined) return assetManifestPromise;
    assetManifestPromise = fetch(manifestUrl).then(async (response) => {
      if (!response.ok) {
        throw new Error(`Unable to load texture asset manifest: ${response.status}`);
      }
      const manifest = await response.json();
      if (manifest.schema_version !== 1) {
        throw new Error("Texture asset manifest has an unsupported schema");
      }
      assetManifest = manifest;
      return manifest;
    });
    return assetManifestPromise;
  }

  function validateFontMetrics(name, font, metrics) {
    const messageFont = name === "Message";
    const expectedStyle = messageFont ? "text-weights" : "texture-coverage";
    const firstGlyph = messageFont ? 32 : 0;
    const glyphCount = messageFont ? 96 : 40;
    if (metrics.schema !== "font-metrics-v1" || metrics.image !== font.path ||
        !Number.isInteger(metrics.atlas_width) || metrics.atlas_width !== font.width ||
        !Number.isInteger(metrics.atlas_height) || metrics.atlas_height !== font.height ||
        !Number.isInteger(metrics.texture_scale) || metrics.texture_scale <= 0 ||
        metrics.render_style !== expectedStyle ||
        metrics.glyphs === null || typeof metrics.glyphs !== "object") {
      throw new Error(`${name} font metrics are invalid`);
    }
    if (messageFont && (!Number.isInteger(metrics.first_char) ||
                        metrics.first_char !== firstGlyph ||
                        !Number.isInteger(metrics.last_char) ||
                        metrics.last_char !== firstGlyph + glyphCount - 1 ||
                        !Number.isInteger(metrics.char_width) ||
                        metrics.char_width <= 0 ||
                        !Number.isInteger(metrics.char_height) ||
                        metrics.char_height <= 0 ||
                        !Number.isInteger(metrics.char_extra) ||
                        metrics.char_extra < 0 ||
                        !Number.isInteger(metrics.leading) ||
                        metrics.leading < 0)) {
      throw new Error(`${name} font layout metrics are invalid`);
    }
    const glyphKeys = Object.keys(metrics.glyphs);
    if (glyphKeys.length !== glyphCount) {
      throw new Error(`${name} font has an unexpected glyph count`);
    }
    for (let index = 0; index < glyphCount; index += 1) {
      const glyph = metrics.glyphs[String(firstGlyph + index)];
      const rect = glyph?.source_rect;
      if (!Array.isArray(rect) || rect.length !== 4 ||
          !rect.every(Number.isInteger) || rect[0] < 0 || rect[1] < 0 ||
          rect[2] <= 0 || rect[3] <= 0 ||
          rect[0] + rect[2] > metrics.atlas_width ||
          rect[1] + rect[3] > metrics.atlas_height ||
          !Number.isInteger(glyph.advance) || glyph.advance < 0 ||
          !Number.isInteger(glyph.draw_width) || glyph.draw_width <= 0 ||
          !Number.isInteger(glyph.draw_height) || glyph.draw_height <= 0 ||
          (messageFont && glyph.advance <= 0) ||
          rect[2] !== glyph.draw_width * metrics.texture_scale ||
          rect[3] !== glyph.draw_height * metrics.texture_scale) {
        throw new Error(`${name} font metrics contain an invalid glyph`);
      }
    }
  }

  function loadFont(name) {
    const cached = fontLoads.get(name);
    if (cached !== undefined) return cached;
    const load = loadAssetManifest().then(async (manifest) => {
      const font = manifest.fonts?.[name];
      if (font?.metrics?.schema !== "font-metrics-v1") {
        throw new Error(`${name} font is not in the manifest`);
      }
      const [metricsBytes, image] = await Promise.all([
        loadBinary(font.metrics, `${name} font metrics`),
        loadImage(font, `${name} font`)
      ]);
      const metrics = JSON.parse(new TextDecoder().decode(metricsBytes));
      validateFontMetrics(name, font, metrics);
      return { image, metrics };
    });
    const retryableLoad = load.catch((error) => {
      fontLoads.delete(name);
      throw error;
    });
    fontLoads.set(name, retryableLoad);
    return retryableLoad;
  }

  function loadMessageFontMetrics() {
    if (fontMetricsPromise !== undefined) return fontMetricsPromise;
    fontMetricsPromise = loadFont("Message").then(({ image, metrics }) => {
      fontImage = image;
      fontMetrics = metrics;
      applyFont();
      return metrics;
    }).catch((error) => {
      fontMetricsPromise = undefined;
      throw error;
    });
    return fontMetricsPromise;
  }

  function loadMessageFont() {
    return loadMessageFontMetrics();
  }

  function loadAlphabetFont() {
    if (alphabetFontPromise !== undefined) return alphabetFontPromise;
    alphabetFontPromise = loadFont("Alphabet").then(({ image, metrics }) => {
      alphabetFont = { texture: createAtlasTexture(image), metrics };
      return alphabetFont;
    }).catch((error) => {
      alphabetFontPromise = undefined;
      throw error;
    });
    return alphabetFontPromise;
  }

  async function loadImage(entry, description) {
    if (typeof entry?.path !== "string" ||
        !Number.isInteger(entry.width) || !Number.isInteger(entry.height) ||
        entry.width <= 0 || entry.height <= 0) {
      throw new Error(`${description} metadata is invalid`);
    }
    const image = new Image();
    image.src = new URL(entry.path, manifestUrl);
    await image.decode();
    if (image.naturalWidth !== entry.width || image.naturalHeight !== entry.height) {
      throw new Error(`${description} dimensions do not match its manifest`);
    }
    return image;
  }

  async function loadBinary(entry, description) {
    if (typeof entry?.path !== "string" || !Number.isInteger(entry.byte_length) ||
        entry.byte_length <= 0) {
      throw new Error(`${description} metadata is invalid`);
    }
    const response = await fetch(new URL(entry.path, manifestUrl));
    if (!response.ok) {
      throw new Error(`Unable to load ${description}: ${response.status}`);
    }
    const bytes = new Uint8Array(await response.arrayBuffer());
    if (bytes.length !== entry.byte_length) {
      throw new Error(`${description} length does not match its manifest`);
    }
    return bytes;
  }

  async function loadJson(entry, description) {
    const bytes = await loadBinary(entry, description);
    try {
      return JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
    } catch (error) {
      throw new Error(`${description} is not valid JSON: ${error.message}`);
    }
  }

  function loadText(language, name) {
    const identity = `${language}/${name}`;
    const cached = textLoads.get(identity);
    if (cached !== undefined) return cached;
    const load = loadAssetManifest().then(async (manifest) => {
      const entry = manifest.text?.[identity];
      const indexed = name === "Game" || name === "Menu" || name === "Title";
      const expectedSchema = indexed ? "legacy-text-indexed-v1" :
        "legacy-text-lines-v1";
      if (entry?.schema !== expectedSchema) {
        throw new Error(`Text resource ${identity} is not in the manifest`);
      }
      const strings = await loadJson(entry, `Text resource ${identity}`);
      if (!Array.isArray(strings) || strings.length === 0 ||
          !strings.every((line) => typeof line === "string" &&
            [...line].every((character) => {
              const code = character.codePointAt(0);
              return code >= 32 && code <= 126;
            })) || strings.length > ({
              Game: 128, Menu: 160, Title: 80
            }[name] ?? 128)) {
        throw new Error(`Text resource ${identity} contains invalid strings`);
      }
      return strings;
    });
    const retryableLoad = load.catch((error) => {
      textLoads.delete(identity);
      throw error;
    });
    textLoads.set(identity, retryableLoad);
    return retryableLoad;
  }

  function loadGameConfiguration() {
    if (configurationPromise !== undefined) return configurationPromise;
    configurationPromise = loadAssetManifest().then(async (manifest) => {
      const entry = manifest.configuration;
      if (entry?.schema !== "game-configuration-v1") {
        throw new Error("Game configuration is not in the manifest");
      }
      const configuration = await loadJson(entry, "Game configuration");
      if (configuration?.schema !== "game-configuration-v1") {
        throw new Error("Game configuration schema is invalid");
      }
      return configuration;
    }).catch((error) => {
      configurationPromise = undefined;
      throw error;
    });
    return configurationPromise;
  }

  function loadWorldMetadata() {
    if (worldMetadataPromise !== undefined) return worldMetadataPromise;
    worldMetadataPromise = loadAssetManifest().then(async (manifest) => {
      const entry = manifest.world_metadata;
      if (entry?.schema !== "world-metadata-v1") {
        throw new Error("World metadata is not in the manifest");
      }
      const document = await loadJson(entry, "World metadata");
      if (document?.schema !== "world-metadata-v1" ||
          document.worlds === null || typeof document.worlds !== "object") {
        throw new Error("World metadata schema is invalid");
      }
      return document.worlds;
    }).catch((error) => {
      worldMetadataPromise = undefined;
      throw error;
    });
    return worldMetadataPromise;
  }

  function loadTextureAnimations(world) {
    const cached = textureAnimationLoads.get(world);
    if (cached !== undefined) return cached;
    const load = loadAssetManifest().then(async (manifest) => {
      const entry = manifest.texture_animations?.[world];
      if (entry?.schema !== "texture-animation-v1") {
        throw new Error(`Texture animations ${world} are not in the manifest`);
      }
      const document = await loadJson(entry, `Texture animations ${world}`);
      if (document?.schema !== "texture-animation-v1" ||
          !Array.isArray(document.animations)) {
        throw new Error(`Texture animations ${world} have an invalid schema`);
      }
      return document;
    });
    const retryableLoad = load.catch((error) => {
      textureAnimationLoads.delete(world);
      throw error;
    });
    textureAnimationLoads.set(world, retryableLoad);
    return retryableLoad;
  }

  function imagePixels(image) {
    const canvas = document.createElement("canvas");
    canvas.width = image.naturalWidth;
    canvas.height = image.naturalHeight;
    const context = canvas.getContext("2d", { willReadFrequently: true });
    context.drawImage(image, 0, 0);
    return context.getImageData(0, 0, canvas.width, canvas.height).data;
  }

  function loadTerrainMaps(identity) {
    const cached = terrainMapLoads.get(identity);
    if (cached !== undefined) return cached;
    const load = loadAssetManifest().then(async (manifest) => {
      const maps = manifest.maps?.[identity];
      if (maps?.height === null || maps?.tile === null ||
          maps?.height === undefined || maps?.tile === undefined) {
        throw new Error(`Terrain maps ${identity} are not in the manifest`);
      }
      const [heightImage, tileImage] = await Promise.all([
        loadImage(maps.height, `Height map ${identity}`),
        loadImage(maps.tile, `Tile map ${identity}`)
      ]);
      if (heightImage.naturalWidth !== 256 || heightImage.naturalHeight !== 256 ||
          tileImage.naturalWidth !== 256 || tileImage.naturalHeight !== 256) {
        throw new Error(`Terrain maps ${identity} dimensions are invalid`);
      }
      return {
        height: imagePixels(heightImage),
        tile: imagePixels(tileImage)
      };
    });
    const retryableLoad = load.catch((error) => {
      terrainMapLoads.delete(identity);
      throw error;
    });
    terrainMapLoads.set(identity, retryableLoad);
    return retryableLoad;
  }

  function loadPolygonMap(identity) {
    const cached = polygonMapLoads.get(identity);
    if (cached !== undefined) return cached;
    const load = loadAssetManifest().then(async (manifest) => {
      const polygon = manifest.maps?.[identity]?.polygon;
      const image = await loadImage(polygon, `Polygon map ${identity}`);
      if (image.naturalWidth !== 128 || image.naturalHeight !== 128) {
        throw new Error(`Polygon map ${identity} dimensions are invalid`);
      }
      return imagePixels(image);
    });
    const retryableLoad = load.catch((error) => {
      polygonMapLoads.delete(identity);
      throw error;
    });
    polygonMapLoads.set(identity, retryableLoad);
    return retryableLoad;
  }

  function loadMissionRecord(level, number) {
    const identity = `${level}/${number}`;
    const cached = missionRecordLoads.get(identity);
    if (cached !== undefined) return cached;
    const load = loadAssetManifest().then((manifest) => {
      const mission = manifest.missions?.[identity];
      if (mission?.schema !== "mission-data-v1" || mission.byte_length !== 4924) {
        throw new Error(`Mission record ${identity} is not in the manifest`);
      }
      return loadBinary(mission, `Mission record ${identity}`);
    });
    const retryableLoad = load.catch((error) => {
      missionRecordLoads.delete(identity);
      throw error;
    });
    missionRecordLoads.set(identity, retryableLoad);
    return retryableLoad;
  }

  function loadWorldMaterials(world) {
    return loadAssetManifest().then(async (manifest) => {
      if (world !== worldSet) {
        throw new Error(`World material request ${world} does not match ${worldSet}`);
      }
      const entry = manifest.world_sets?.[world]?.material_descriptors;
      if (entry?.schema !== "static-cel-materials-v1" ||
          entry.byte_length !== 4108) {
        throw new Error(`World material descriptors ${world} are not in the manifest`);
      }
      const [descriptors] = await Promise.all([
        loadBinary(entry, `World material descriptors ${world}`),
        loadWorldAssets()
      ]);
      if (worldFailed) {
        throw new Error(`World texture set ${world} could not be loaded`);
      }
      return descriptors;
    });
  }

  function loadWorldGraphics(world) {
    return loadAssetManifest().then((manifest) => {
      const entry = manifest.world_graphics?.[world];
      if (entry?.schema !== "graphics-mixed-layout-v1") {
        throw new Error(`World graphics ${world} are not in the manifest`);
      }
      return loadBinary(entry, `World graphics ${world}`);
    });
  }

  function loadGameCels(name) {
    return loadAssetManifest().then((manifest) => {
      const entry = manifest.game_sets?.[name]?.legacy_data;
      if (entry?.schema !== "cel-offset-table-v1") {
        throw new Error(`Game CELs ${name} are not in the manifest`);
      }
      return loadBinary(entry, `Game CELs ${name}`);
    });
  }

  function loadSky(name) {
    return loadAssetManifest().then((manifest) => {
      const entry = manifest.sky?.[name]?.legacy_data;
      if (entry?.schema !== "identity-copy-v1") {
        throw new Error(`Sky data ${name} are not in the manifest`);
      }
      return loadBinary(entry, `Sky data ${name}`);
    });
  }

  function loadMonochromePalette() {
    return loadAssetManifest().then((manifest) => {
      const entry = manifest.compatibility?.monochrome_palette;
      if (entry?.schema !== "identity-copy-v1") {
        throw new Error("Monochrome palette is not in the manifest");
      }
      return loadBinary(entry, "Monochrome palette");
    });
  }

  function loadWorldAssets() {
    if (worldSet === "" || worldFailed) return Promise.resolve();
    if (worldLoading) return worldAssetsPromise;
    const requestedSet = worldSet;
    const requestedSky = skyName;
    const requestedMap = mapName;
    const requestedVersion = worldRequest;
    worldLoading = true;
    worldAssetsPromise = loadAssetManifest().then(async (manifest) => {
      const set = manifest.world_sets?.[requestedSet];
      if (set === undefined) {
        throw new Error(`World texture set ${requestedSet} is not in the manifest`);
      }
      const atlasEntries = [];
      for (const category of ["16", "32"]) {
        const categorySet = set[category];
        if (categorySet === undefined) continue;
        if (!Array.isArray(categorySet.atlases)) {
          throw new Error(`World texture set ${requestedSet}.${category} is invalid`);
        }
        atlasEntries.push(...categorySet.atlases.map((entry) => ({
          ...entry, category, scope: "world"
        })));
        const sharedSet = manifest.shared_world_atlases?.[category];
        if (sharedSet === undefined || !Array.isArray(sharedSet.atlases)) {
          throw new Error(`Shared world texture set ${category} is invalid`);
        }
        atlasEntries.push(...sharedSet.atlases
          .filter((entry) => !sharedWorldAtlases.has(entry.path))
          .map((entry) => ({ ...entry, category, scope: "shared" })));
      }
      const loadedAtlases = await Promise.all(atlasEntries.map(async (entry) => ({
        ...entry,
        image: await loadImage(entry, `World atlas ${entry.path}`)
      })));
      if (requestedVersion !== worldRequest) return;

      const loadedWorldAtlases = new Map();
      for (const entry of loadedAtlases) {
        const atlas = {
          texture: createAtlasTexture(entry.image, gl.NEAREST),
          pixels: imagePixels(entry.image),
          ...entry
        };
        if (entry.scope === "shared") {
          sharedWorldAtlases.set(entry.path, atlas);
        } else {
          loadedWorldAtlases.set(entry.path, atlas);
        }
      }
      worldAtlases = loadedWorldAtlases;

      const sky = manifest.sky?.[requestedSky];
      let nextSkyPixels;
      if (sky !== undefined) {
        nextSkyPixels = imagePixels(await loadImage(sky, `Sky ${requestedSky}`));
      }

      let nextTerrainMaps;
      const maps = manifest.maps?.[requestedMap];
      if (maps?.height !== null && maps?.tile !== null &&
          maps?.height !== undefined && maps?.tile !== undefined) {
        nextTerrainMaps = await loadTerrainMaps(requestedMap);
      }
      if (requestedVersion === worldRequest) {
        skyPixels = nextSkyPixels;
        terrainMaps = nextTerrainMaps;
      }
    }).catch((error) => {
      if (requestedVersion === worldRequest) {
        worldFailed = true;
        console.warn("Baked world textures are unavailable; using runtime textures.", error);
      }
    }).finally(() => {
      worldLoading = false;
      if (requestedVersion !== worldRequest) return loadWorldAssets();
    });
    return worldAssetsPromise;
  }

  function loadBackdropImage(name) {
    const cached = backdropLoads.get(name);
    if (cached !== undefined) return cached;
    const load = loadAssetManifest().then(async (manifest) => {
      const requestedBackdrop = name;
      const backdrop = manifest.backdrops?.[requestedBackdrop];
      if (backdrop === undefined) {
        throw new Error(`Backdrop ${requestedBackdrop} is not in the manifest`);
      }
      const image = await loadImage(backdrop, `Backdrop ${requestedBackdrop}`);
      if (image.naturalWidth !== 320 || image.naturalHeight !== 240) {
        throw new Error(`Backdrop ${requestedBackdrop} dimensions are invalid`);
      }
      return image;
    });
    const retryableLoad = load.catch((error) => {
      backdropLoads.delete(name);
      throw error;
    });
    backdropLoads.set(name, retryableLoad);
    return retryableLoad;
  }

  function requestBackdrop() {
    if (backdropName === "") return;
    loadBackdropImage(backdropName).then((image) => {
      if (backdropName !== "") backdropImage = image;
    }).catch((error) => {
      console.warn("Baked backdrop is unavailable; using runtime image.", error);
    });
  }

  function loadGameAtlas() {
    if (gameAtlases !== undefined || gameAtlasLoading || gameAtlasFailed) return;
    gameAtlasLoading = true;
    loadAssetManifest().then(async (manifest) => {
      if (!Array.isArray(manifest.game_atlases) || manifest.game_atlases.length === 0) {
        throw new Error("Game texture atlas metadata is invalid");
      }
      const atlases = await Promise.all(manifest.game_atlases.map(async (atlas) => {
        if (atlas.width <= 0 || atlas.height <= 0 || atlas.sprites === undefined) {
          throw new Error("Game texture atlas metadata is invalid");
        }
        const image = new Image();
        image.src = new URL(atlas.path, manifestUrl);
        await image.decode();
        if (image.naturalWidth !== atlas.width || image.naturalHeight !== atlas.height) {
          throw new Error("Game texture atlas dimensions do not match its manifest");
        }
        return { texture: createAtlasTexture(image), ...atlas };
      }));
      gameAtlases = new Map();
      for (const atlas of atlases) {
        for (const [key, region] of Object.entries(atlas.sprites)) {
          if (gameAtlases.has(key)) {
            throw new Error(`Game texture atlas has a duplicate sprite: ${key}`);
          }
          gameAtlases.set(key, { atlas, region });
        }
      }
    }).catch((error) => {
      gameAtlasFailed = true;
      console.warn("Baked Game texture atlas is unavailable; using runtime textures.", error);
    }).finally(() => {
      gameAtlasLoading = false;
    });
  }

  function gameTexture(set, sprite) {
    if (set === "Alphabet") {
      const glyph = alphabetFont?.metrics.glyphs[String(sprite)];
      if (glyph === undefined || alphabetFont === undefined) {
        loadAlphabetFont().catch((error) => {
          console.warn("Baked Alphabet font is unavailable", error);
        });
        return undefined;
      }
      const [x, y, width, height] = glyph.source_rect;
      return {
        texture: alphabetFont.texture,
        u: x / alphabetFont.metrics.atlas_width,
        v: y / alphabetFont.metrics.atlas_height,
        width: width / alphabetFont.metrics.atlas_width,
        height: height / alphabetFont.metrics.atlas_height,
        ...bakedGameTextureOptions(set, sprite)
      };
    }
    if (gameAtlases === undefined) return undefined;
    const entry = gameAtlases.get(`${set}:${sprite}`);
    if (entry === undefined) return undefined;
    const { atlas, region } = entry;
    return {
      texture: atlas.texture,
      u: region.x / atlas.width,
      v: region.y / atlas.height,
      width: region.width / atlas.width,
      height: region.height / atlas.height,
      ...bakedGameTextureOptions(set, sprite)
    };
  }

  function worldTexture(command) {
    const encoding = command.encoding & 0xff;
    if ((encoding !== STATIC_CEL_16_ENCODING &&
         encoding !== STATIC_CEL_32_ENCODING) || worldSet === "") {
      return undefined;
    }
    const category = encoding === STATIC_CEL_32_ENCODING ? "32" : "16";
    const index = (command.encoding & STATIC_CEL_INDEX_MASK) >> STATIC_CEL_INDEX_SHIFT;
    const categorySet = assetManifest?.world_sets?.[worldSet]?.[category];
    const paletteOverride = Number.isInteger(command.palette) &&
      command.palette > 0 && command.palette <= 256 ? command.palette - 1 : undefined;
    const material = paletteOverride === undefined ?
      categorySet?.materials?.[index] :
      categorySet?.debris_materials?.[String(index)]?.[String(paletteOverride)];
    if (material === undefined || material === null ||
        !Number.isInteger(material.slot) ||
        (material.scope !== "shared" && material.scope !== "world")) {
      return undefined;
    }
    const atlasSet = material.scope === "shared" ?
      assetManifest?.shared_world_atlases?.[category] : categorySet;
    const grid = atlasSet?.grid;
    if (grid === null || grid === undefined ||
        !Number.isInteger(grid.columns) || grid.columns <= 0 ||
        !Number.isInteger(grid.tile_count) || material.slot < 0 ||
        material.slot >= grid.tile_count) {
      return undefined;
    }
    const atlas = (material.scope === "shared" ? sharedWorldAtlases : worldAtlases)
      .get(grid.atlas);
    if (atlas === undefined) {
      loadWorldAssets();
      return undefined;
    }
    const region = {
      x: (material.slot % grid.columns) * grid.tile_width,
      y: Math.floor(material.slot / grid.columns) * grid.tile_height,
      width: grid.tile_width,
      height: grid.tile_height
    };
    return {
      texture: atlas.texture,
      u: region.x / atlas.image.naturalWidth,
      v: region.y / atlas.image.naturalHeight,
      width: region.width / atlas.image.naturalWidth,
      height: region.height / atlas.image.naturalHeight,
      usesTextureCoverage: true
    };
  }

  function skyGradient(memory, command) {
    if (skyPixels === undefined || command.width !== 1 || command.height === 0 ||
        command.height > 400 || command.palette < 0 ||
        command.palette + (command.height + 1) * 4 > memory.length) {
      return undefined;
    }
    const bands = new Uint32Array(memory.buffer, command.palette,
                                  command.height + 1);
    const pixels = new Uint8Array(command.height * 4);
    for (let y = 0; y < command.height; y += 1) {
      const position = bands[y + 1];
      const lower = (position >>> 10) * 4;
      const upper = Math.min((position >>> 10) + 1, 255) * 4;
      const fraction = (position & 1023) / 1024;
      const destination = y * 4;
      pixels[destination] = Math.round(
        skyPixels[lower] + (skyPixels[upper] - skyPixels[lower]) * fraction
      );
      pixels[destination + 1] = Math.round(
        skyPixels[lower + 1] + (skyPixels[upper + 1] - skyPixels[lower + 1]) * fraction
      );
      pixels[destination + 2] = Math.round(
        skyPixels[lower + 2] + (skyPixels[upper + 2] - skyPixels[lower + 2]) * fraction
      );
      pixels[destination + 3] = 255;
    }
    return { pixels, width: 1, height: command.height };
  }

  function terrainAtlas() {
    const local = assetManifest?.world_sets?.[worldSet]?.["16"];
    const shared = assetManifest?.shared_world_atlases?.["16"];
    if (!Array.isArray(local?.materials) || local.materials.length === 0 ||
        (local.grid === null && shared?.grid === null)) {
      return undefined;
    }
    const sharedAtlas = shared?.grid === null || shared?.grid === undefined ?
      undefined : sharedWorldAtlases.get(shared.grid.atlas);
    const worldAtlas = local.grid === null || local.grid === undefined ?
      undefined : worldAtlases.get(local.grid.atlas);
    if (sharedAtlas === undefined && worldAtlas === undefined) return undefined;
    return {
      shared: sharedAtlas === undefined ? undefined : {
        ...sharedAtlas, columns: shared.grid.columns
      },
      world: worldAtlas === undefined ? undefined : {
        ...worldAtlas, columns: local.grid.columns
      },
      materials: local.materials,
      generation: `${worldSet}:${shared?.grid?.atlas ?? ""}:${local.grid?.atlas ?? ""}`
    };
  }

  function applyFont() {
    if (renderer !== undefined && fontImage !== undefined && fontMetrics !== undefined) {
      renderer.setBakedTextFont(fontImage, fontMetrics);
    }
  }

  loadMessageFontMetrics().catch((error) => {
    console.warn("Baked Message font metrics are unavailable", error);
  });

  return {
    setRenderer(nextRenderer) {
      renderer = nextRenderer;
      applyFont();
    },
    applyFont,
    loadAlphabetFont,
    loadMessageFont,
    loadMessageFontMetrics,
    setGameCels(name) {
      gameSet = name;
      if (name === "Alphabet") {
        loadAlphabetFont().catch((error) => {
          console.warn("Baked Alphabet font is unavailable", error);
        });
      } else {
        loadGameAtlas();
      }
    },
    setWorldResources(planet, location, variation, sky) {
      worldRequest += 1;
      const worldChanged = worldSet !== planet;
      worldSet = planet;
      skyName = sky;
      mapName = `${location}/${variation}`;
      if (worldChanged || worldFailed) {
        worldAtlases = new Map();
      }
      skyPixels = undefined;
      terrainMaps = undefined;
      worldFailed = false;
      loadWorldAssets();
    },
    setBackdropName(name) {
      backdropName = name;
      backdropImage = undefined;
      requestBackdrop();
    },
    backdrop() {
      return backdropImage;
    },
    terrainMaps() {
      return terrainMaps;
    },
    loadMissionMaps(location, variation) {
      return loadTerrainMaps(`${location}/${variation}`);
    },
    loadMissionRecord,
    loadWorldGraphics,
    loadGameCels,
    loadSky,
    loadText,
    loadGameConfiguration,
    loadWorldMetadata,
    loadTextureAnimations,
    loadMonochromePalette,
    loadWorldMaterials,
    loadPolygonMap(location, variation) {
      return loadPolygonMap(`${location}/${variation}`);
    },
    loadBackdrop(name) {
      return loadBackdropImage(name).then((image) => {
        if (backdropName === name) backdropImage = image;
      });
    },
    terrainAtlas,
    skyGradient,
    textureFor(command) {
      const world = worldTexture(command);
      if (world !== undefined) return world;
      if (gameSet === "" ||
          (command.encoding & (GAME_CEL_ENCODING | WORLD_ENCODING |
                               TARGET_OVERLAY_ENCODING)) !== GAME_CEL_ENCODING) {
        return undefined;
      }
      const baseEncoding = command.encoding & 0xff;
      if ((baseEncoding === 0 || baseEncoding === 4) &&
          gameSet !== "Alphabet" &&
          command.palette !== ((command.source - 64) >>> 0)) {
        return undefined;
      }
      const sprite = (command.encoding & GAME_CEL_SPRITE_MASK) >> GAME_CEL_SPRITE_SHIFT;
      const texture = gameTexture(gameSet, sprite);
      if (texture !== undefined) return texture;
      loadGameAtlas();
      return undefined;
    },
    reset() {
      worldAtlases = new Map();
      skyPixels = undefined;
      terrainMaps = undefined;
    }
  };
}
