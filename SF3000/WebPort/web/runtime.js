import { createWebGLRenderer } from "./webgl_renderer.js";

const CONTROL = {
  up: 0x00000001,
  down: 0x00000002,
  left: 0x00000004,
  right: 0x00000008,
  a: 0x00000010,
  b: 0x00000020,
  c: 0x00000040,
  start: 0x00000080,
  x: 0x00000100,
  leftShift: 0x00000200,
  rightShift: 0x00000400
};

const P_MODE_0_ALPHA = 255;
const P_MODE_1_ALPHA = 128;
const TEXTURE_ATLAS_SIZE = 2048;
const TEXTURE_ATLAS_PADDING = 1;
const NVRAM_STORAGE_PREFIX = "starfighter:nvram:";
const MAX_SOUND_VOICES = 6;
const SOUND_EFFECT_SOURCES = [
  "Laser.aiff", "Missile.aiff", "Thud.aiff", "Engine.aiff", "Beep.aiff",
  "Click.aiff", "Explosion.aiff", "Wind.aiff", "Chime.aiff", "LockOn.aiff",
  "PickUp.aiff", "Incoming.aiff", "Alarm.aiff", "BeamLaser.aiff"
];
const MUSIC_TRACK_SOURCES = [
  "Music/obie130.aifc", "Music/DeathBS.aifc", "Music/PlanetMa.aifc",
  "Music/Star1.aifc", "Music/StarHed.aifc", "Music/Freefall.aifc",
  "Music/Floating.aifc", "Music/Higher.aifc"
];
const MUSIC_PLAY_FROM_TRACK = 0x1;

function logNVRAMFailure(operation, error) {
  console.warn(`Star Fighter save storage ${operation} failed.`, error);
}

function encodeNVRAMData(bytes) {
  const chunks = [];
  const chunkSize = 0x8000;

  for (let offset = 0; offset < bytes.length; offset += chunkSize) {
    chunks.push(String.fromCharCode(...bytes.subarray(offset, offset + chunkSize)));
  }
  return btoa(chunks.join(""));
}

function decodeNVRAMData(encoded) {
  const binary = atob(encoded);
  const bytes = new Uint8Array(binary.length);

  for (let index = 0; index < binary.length; index += 1) {
    bytes[index] = binary.charCodeAt(index);
  }
  return bytes;
}

function clamp(value, minimum, maximum) {
  return Math.max(minimum, Math.min(maximum, value));
}

function createAudioMixer() {
  let audioContext;
  let masterGain;
  const busGains = new Map();
  const resumeListeners = new Set();

  function ensure() {
    if (audioContext !== undefined) return true;
    const AudioContextConstructor =
      globalThis.AudioContext ?? globalThis.webkitAudioContext;
    if (AudioContextConstructor === undefined) {
      console.error("Web Audio is unavailable; audio is disabled.");
      return false;
    }
    audioContext = new AudioContextConstructor();
    masterGain = audioContext.createGain();
    masterGain.connect(audioContext.destination);
    for (const name of ["effects", "music", "voice", "cinematic"]) {
      const gain = audioContext.createGain();
      gain.connect(masterGain);
      busGains.set(name, gain);
    }
    return true;
  }

  function resume() {
    if (!ensure()) return;
    const operation = audioContext.state === "suspended" ? audioContext.resume() :
      Promise.resolve();
    operation.then(() => {
      for (const listener of resumeListeners) listener();
    }).catch((error) => {
      console.error("Unable to resume Web Audio.", error);
    });
  }

  return {
    ensure,
    resume,
    context() {
      return ensure() ? audioContext : undefined;
    },
    bus(name) {
      return ensure() ? busGains.get(name) : undefined;
    },
    setBusVolume(name, volume) {
      if (!ensure()) return;
      busGains.get(name).gain.setValueAtTime(
        clamp(volume, 0, 128) / 128, audioContext.currentTime
      );
    },
    onResume(listener) {
      resumeListeners.add(listener);
    }
  };
}

function createSoundEffects(mixer) {
  let audioContext;
  let masterGain;
  let initialised = false;
  let enabled = true;
  let masterVolume = 128;
  let loading;
  let loadGeneration = 0;
  const samples = new Array(SOUND_EFFECT_SOURCES.length);
  const voices = Array.from({ length: MAX_SOUND_VOICES }, () => ({}));

  function releaseVoice(voice) {
    if (voice.source === undefined) return;
    voice.source.disconnect();
    voice.leftGain.disconnect();
    voice.rightGain.disconnect();
    voice.merger.disconnect();
    voice.source = undefined;
  }

  function updateMasterVolume() {
    if (masterGain !== undefined) {
      masterGain.gain.setValueAtTime(
        clamp(masterVolume, 0, 128) / 128, audioContext.currentTime
      );
    }
  }

  function ensureAudioContext() {
    if (audioContext !== undefined) return true;
    if (!mixer.ensure()) return false;
    audioContext = mixer.context();
    masterGain = mixer.bus("effects");
    updateMasterVolume();
    return true;
  }

  function updateVoiceMix(voice) {
    const volume = clamp(voice.volume, 0, 127) / 128;
    const position = clamp(voice.stereoPosition, -16384, 16384) / 16384;
    const now = audioContext.currentTime;

    voice.leftGain.gain.setValueAtTime(volume * (1 - position), now);
    voice.rightGain.gain.setValueAtTime(volume * (1 + position), now);
  }

  function updateVoicePitch(voice) {
    const rate = Math.max(0, voice.baseRate * voice.pitchBend / 65536);

    voice.source.playbackRate.setValueAtTime(rate, audioContext.currentTime);
  }

  async function fetchSamples() {
    const manifestUrl = new URL("./audio/assets.manifest.json", import.meta.url);
    const response = await fetch(manifestUrl);
    if (!response.ok) {
      throw new Error(`Unable to load sound-effect manifest: ${response.status}`);
    }
    const manifest = await response.json();
    if (manifest.generator !== "tools/convert_assets.py --sound-effects-only") {
      throw new Error("Unsupported sound-effect manifest");
    }
    const bySource = new Map(manifest.assets.map((asset) => [asset.source_path, asset]));

    return Promise.all(SOUND_EFFECT_SOURCES.map(async (source, index) => {
      const asset = bySource.get(`SF_Resources/Samples/${source}`);
      const output = asset?.outputs?.find((candidate) =>
        candidate.type === "pcm-wav" && candidate.emitted === true);
      if (output === undefined) {
        throw new Error(`Converted sound effect is missing: ${source}`);
      }
      const sampleResponse = await fetch(new URL(output.path, manifestUrl));
      if (!sampleResponse.ok) {
        throw new Error(`Unable to load sound effect ${source}: ${sampleResponse.status}`);
      }
      const buffer = await audioContext.decodeAudioData(
        await sampleResponse.arrayBuffer()
      );
      return {
        buffer,
        loopStart: output.loop_start,
        loopEnd: output.loop_end,
        source: index
      };
    }));
  }

  function loadSamples() {
    if (loading !== undefined || !ensureAudioContext()) return;
    const generation = loadGeneration;
    loading = fetchSamples().then((loaded) => {
      if (generation === loadGeneration) {
        samples.splice(0, samples.length, ...loaded);
      }
    }).catch((error) => {
      if (generation === loadGeneration) {
        console.error("Unable to load Star Fighter sound effects.", error);
        loading = undefined;
      }
    });
  }

  function resume() {
    loadSamples();
    mixer.resume();
  }

  return {
    load() {
      loadSamples();
    },
    unload() {
      loadGeneration += 1;
      loading = undefined;
      for (const voice of voices) {
        if (voice.source !== undefined) {
          voice.source.stop();
          releaseVoice(voice);
        }
      }
      samples.fill(undefined);
      initialised = false;
    },
    initialise() {
      initialised = true;
      loadSamples();
    },
    terminate() {
      for (const voice of voices) {
        if (voice.source !== undefined) {
          voice.source.stop();
          releaseVoice(voice);
        }
      }
      initialised = false;
    },
    resume,
    play(sample, pitch, volume, stereoPosition) {
      if (!initialised || !enabled || audioContext?.state !== "running" ||
          sample < 0 || sample >= samples.length || samples[sample] === undefined) {
        return -1;
      }
      const voice = voices.find((candidate) => candidate.source === undefined);
      if (voice === undefined) return -1;

      const effect = samples[sample];
      const source = audioContext.createBufferSource();
      const leftGain = audioContext.createGain();
      const rightGain = audioContext.createGain();
      const merger = audioContext.createChannelMerger(2);
      source.buffer = effect.buffer;
      if (effect.loopStart !== undefined && effect.loopEnd !== undefined) {
        source.loop = true;
        source.loopStart = effect.loopStart / effect.buffer.sampleRate;
        source.loopEnd = effect.loopEnd / effect.buffer.sampleRate;
      }
      source.connect(leftGain).connect(merger, 0, 0);
      source.connect(rightGain).connect(merger, 0, 1);
      merger.connect(masterGain);
      Object.assign(voice, {
        source,
        leftGain,
        rightGain,
        merger,
        volume,
        stereoPosition,
        baseRate: 2 ** ((pitch - 60) / 12),
        pitchBend: 65536
      });
      updateVoiceMix(voice);
      updateVoicePitch(voice);
      source.onended = () => {
        if (voice.source === source) releaseVoice(voice);
      };
      source.start();
      return voices.indexOf(voice);
    },
    stop(channel) {
      if (!Number.isInteger(channel) || channel < 0 || channel >= voices.length ||
          voices[channel].source === undefined) {
        return;
      }
      voices[channel].source.stop();
      releaseVoice(voices[channel]);
    },
    pitchBend(channel, pitchBend) {
      if (!Number.isInteger(channel) || channel < 0 || channel >= voices.length ||
          voices[channel].source === undefined) {
        return 0;
      }
      voices[channel].pitchBend = pitchBend;
      updateVoicePitch(voices[channel]);
      return 1;
    },
    alter(channel, volume, stereoPosition) {
      if (!Number.isInteger(channel) || channel < 0 || channel >= voices.length ||
          voices[channel].source === undefined) {
        return;
      }
      voices[channel].volume = volume;
      voices[channel].stereoPosition = stereoPosition;
      updateVoiceMix(voices[channel]);
    },
    setMasterVolume(volume) {
      masterVolume = volume;
      updateMasterVolume();
    },
    setEnabled(value) {
      enabled = value !== 0;
    }
  };
}

function createStreamingPlayer(mixer, bus, label) {
  let element;
  let source;
  let desired = false;
  let ended = () => {};

  function ensure() {
    if (element !== undefined) return true;
    if (!mixer.ensure()) return false;
    element = document.createElement("audio");
    element.preload = "metadata";
    source = mixer.context().createMediaElementSource(element);
    source.connect(mixer.bus(bus));
    element.addEventListener("ended", () => {
      if (desired) {
        desired = false;
        ended();
      }
    });
    element.addEventListener("error", () => {
      if (desired) {
        console.error(`Unable to play Star Fighter ${label}.`, element.error);
      }
    });
    mixer.onResume(() => {
      if (desired) attemptPlay();
    });
    return true;
  }

  function attemptPlay() {
    if (!desired || element === undefined) return;
    element.play().catch((error) => {
      if (error.name !== "NotAllowedError" && error.name !== "AbortError") {
        console.error(`Unable to start Star Fighter ${label}.`, error);
      }
    });
  }

  return {
    setEndedListener(listener) {
      ended = listener;
    },
    play(url) {
      if (!ensure()) return;
      desired = true;
      if (element.src !== url) {
        element.pause();
        element.src = url;
        element.load();
      }
      attemptPlay();
    },
    stop() {
      desired = false;
      if (element === undefined) return;
      element.pause();
      element.currentTime = 0;
    },
    pause() {
      if (element !== undefined) element.pause();
    },
    resume() {
      attemptPlay();
    }
  };
}

function createStreamedMedia(mixer) {
  let manifestPromise;
  let musicTrack = -1;
  let musicIndex = 0;
  let musicTracksLeft = -1;
  let musicGeneration = 0;
  let voiceGeneration = 0;
  const playlist = [];
  const music = createStreamingPlayer(mixer, "music", "music");
  const voice = createStreamingPlayer(mixer, "voice", "voice-over");

  async function assetUrl(resourcePath) {
    const manifestUrl = new URL("./media/assets.manifest.json", import.meta.url);
    if (manifestPromise === undefined) {
      manifestPromise = fetch(manifestUrl).then(async (response) => {
        if (!response.ok) {
          throw new Error(`Unable to load streamed-media manifest: ${response.status}`);
        }
        const manifest = await response.json();
        if (manifest.generator !== "tools/convert_assets.py --streamed-media-only") {
          throw new Error("Unsupported streamed-media manifest");
        }
        return new Map(manifest.assets.map((asset) =>
          [asset.source_path.toLowerCase(), asset]));
      });
    }
    const normalized = resourcePath.replaceAll("\\", "/");
    const marker = normalized.toLowerCase().indexOf("sf_resources/");
    const sourcePath = marker === -1 ? `SF_Resources/${normalized}` :
      normalized.slice(marker);
    const asset = (await manifestPromise).get(sourcePath.toLowerCase());
    const output = asset?.outputs?.find((candidate) =>
      candidate.type === "opus" && candidate.emitted === true);
    if (output === undefined) {
      throw new Error(`Converted streamed media is missing: ${sourcePath}`);
    }
    return new URL(output.path, manifestUrl).href;
  }

  function normalizedMusicIndex(index) {
    return ((index % playlist.length) + playlist.length) % playlist.length;
  }

  function startMusic() {
    if (playlist.length === 0 || musicTracksLeft === 0) {
      music.stop();
      musicTrack = -1;
      return;
    }
    musicIndex = normalizedMusicIndex(musicIndex);
    if (musicTracksLeft > 0) musicTracksLeft -= 1;
    musicTrack = playlist[musicIndex];
    const source = MUSIC_TRACK_SOURCES[musicTrack];
    const generation = ++musicGeneration;
    assetUrl(source).then((url) => {
      if (generation === musicGeneration && musicTrack >= 0) music.play(url);
    }).catch((error) => {
      if (generation === musicGeneration) {
        musicTrack = -1;
        console.error("Unable to load Star Fighter music.", error);
      }
    });
  }

  music.setEndedListener(() => {
    musicIndex += 1;
    startMusic();
  });

  return {
    initialise() {
      mixer.ensure();
    },
    terminate() {
      musicGeneration += 1;
      voiceGeneration += 1;
      music.stop();
      voice.stop();
      musicTrack = -1;
    },
    resetPlaylist() {
      playlist.length = 0;
      musicIndex = 0;
      musicTrack = -1;
    },
    addTrack(track) {
      if (track >= 0 && track < MUSIC_TRACK_SOURCES.length && !playlist.includes(track)) {
        playlist.push(track);
      }
    },
    takeTrack(track) {
      const index = playlist.indexOf(track);
      if (index === -1) return;
      playlist.splice(index, 1);
      if (playlist.length === 0) {
        music.stop();
        musicTrack = -1;
      } else if (index < musicIndex) {
        musicIndex -= 1;
      } else if (index === musicIndex && musicTrack >= 0) {
        startMusic();
      }
    },
    play(mode, trackValue, tracksLeft) {
      if (playlist.length === 0) {
        music.stop();
        musicTrack = -1;
        return;
      }
      if (mode === MUSIC_PLAY_FROM_TRACK) musicIndex = trackValue;
      else musicIndex += trackValue;
      musicTracksLeft = tracksLeft;
      startMusic();
    },
    stop() {
      musicGeneration += 1;
      music.stop();
      musicTrack = -1;
    },
    pause() {
      music.pause();
    },
    resume() {
      music.resume();
    },
    setMasterVolume(volume) {
      mixer.setBusVolume("music", volume);
      mixer.setBusVolume("voice", volume);
    },
    query() {
      return musicTrack;
    },
    playVoice(resourcePath) {
      const generation = ++voiceGeneration;
      assetUrl(resourcePath).then((url) => {
        if (generation === voiceGeneration) voice.play(url);
      }).catch((error) => {
        if (generation === voiceGeneration) {
          console.error("Unable to load Star Fighter voice-over.", error);
        }
      });
    }
  };
}

function createCinematicPlayer(mixer, renderer, status) {
  let element;
  let source;
  let active;
  let manifestPromise;
  let gamepadFrame;

  async function assetUrl(resourcePath) {
    const manifestUrl = new URL("./video/assets.manifest.json", import.meta.url);
    if (manifestPromise === undefined) {
      manifestPromise = fetch(manifestUrl).then(async (response) => {
        if (!response.ok) {
          throw new Error(`Unable to load cinematic manifest: ${response.status}`);
        }
        const manifest = await response.json();
        if (manifest.generator !== "tools/convert_assets.py --cinematics-only") {
          throw new Error("Unsupported cinematic manifest");
        }
        return new Map(manifest.assets.map((asset) =>
          [asset.source_path.toLowerCase(), asset]));
      });
    }
    const normalized = resourcePath.replaceAll("\\", "/");
    const marker = normalized.toLowerCase().indexOf("sf_resources/");
    const sourcePath = marker === -1 ? `SF_Resources/Video/${normalized}` :
      normalized.slice(marker);
    const asset = (await manifestPromise).get(sourcePath.toLowerCase());
    const output = asset?.outputs?.find((candidate) =>
      candidate.type === "h264-aac-mp4" && candidate.emitted === true);
    if (output === undefined) {
      throw new Error(`Converted cinematic is missing: ${sourcePath}`);
    }
    return new URL(output.path, manifestUrl).href;
  }

  function stopGamepadPoll() {
    if (gamepadFrame !== undefined) cancelAnimationFrame(gamepadFrame);
    gamepadFrame = undefined;
  }

  function finish(result, message) {
    const playback = active;
    if (playback === undefined) return;
    active = undefined;
    stopGamepadPoll();
    element.pause();
    renderer.stopCinematic();
    if (message !== undefined) status.textContent = message;
    else if (playback.awaitingAudioActivation) {
      status.textContent = "Running - arrows/WASD move, Z/X/C act, Enter starts";
    }
    playback.resolve(result);
  }

  function pollGamepad() {
    if (active === undefined) return;
    if (gamepadControls() !== 0) {
      finish(1);
      return;
    }
    gamepadFrame = requestAnimationFrame(pollGamepad);
  }

  function ensure() {
    if (element !== undefined) return true;
    if (!mixer.ensure()) return false;
    element = document.createElement("video");
    element.playsInline = true;
    element.preload = "auto";
    source = mixer.context().createMediaElementSource(element);
    source.connect(mixer.bus("cinematic"));
    element.addEventListener("ended", () => finish(0));
    element.addEventListener("error", () => {
      if (active !== undefined) {
        console.error("Unable to play Star Fighter cinematic.", element.error);
        finish(0, "Unable to play cinematic.");
      }
    });
    return true;
  }

  function beginMuted(playback) {
    if (active !== playback) return;
    element.muted = true;
    playback.awaitingAudioActivation = true;
    status.textContent = "Press any key/click to enable audio";
    element.play().catch((error) => {
      if (active === playback && error.name !== "AbortError") {
        console.error("Unable to start muted Star Fighter cinematic.", error);
        finish(0, "Unable to play cinematic.");
      }
    });
  }

  return {
    async play(resourcePath) {
      let url;
      try {
        url = await assetUrl(resourcePath);
      } catch (error) {
        console.error("Unable to load Star Fighter cinematic.", error);
        status.textContent = "Unable to load cinematic.";
        return 0;
      }
      if (!ensure()) return 0;
      return new Promise((resolve) => {
        const playback = { resolve, awaitingAudioActivation: false };
        active = playback;
        element.pause();
        element.muted = mixer.context().state !== "running";
        element.src = url;
        element.load();
        renderer.startCinematic(element);
        gamepadFrame = requestAnimationFrame(pollGamepad);
        if (element.muted) {
          beginMuted(playback);
          return;
        }
        element.play().catch((error) => {
          if (active !== playback || error.name === "AbortError") return;
          if (error.name === "NotAllowedError") {
            beginMuted(playback);
          } else {
            console.error("Unable to start Star Fighter cinematic.", error);
            finish(0, "Unable to play cinematic.");
          }
        });
      });
    },
    handleUserInput() {
      if (active === undefined) return false;
      if (active.awaitingAudioActivation) {
        const playback = active;
        mixer.resume();
        element.muted = false;
        element.play().then(() => {
          if (active === playback) {
            playback.awaitingAudioActivation = false;
            status.textContent = "Playing cinematic";
          }
        }).catch((error) => {
          if (active === playback && error.name !== "AbortError") {
            element.muted = true;
            status.textContent = "Press any key/click to enable audio";
            console.error("Unable to enable Star Fighter cinematic audio.", error);
          }
        });
        return true;
      }
      finish(1);
      return true;
    }
  };
}

const KEY_CONTROLS = new Map([
  ["ArrowUp", CONTROL.up],
  ["KeyW", CONTROL.up],
  ["ArrowDown", CONTROL.down],
  ["KeyS", CONTROL.down],
  ["ArrowLeft", CONTROL.left],
  ["KeyA", CONTROL.left],
  ["ArrowRight", CONTROL.right],
  ["KeyD", CONTROL.right],
  ["KeyZ", CONTROL.a],
  ["KeyX", CONTROL.b],
  ["KeyC", CONTROL.c],
  ["Enter", CONTROL.start],
  ["Space", CONTROL.x],
  ["ShiftLeft", CONTROL.leftShift],
  ["ShiftRight", CONTROL.rightShift]
]);

function createTextureAtlas(gl) {
  const pages = [];

  function createPage() {
    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, TEXTURE_ATLAS_SIZE,
                  TEXTURE_ATLAS_SIZE, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    const page = { texture, nextX: 0, nextY: 0, rowHeight: 0 };
    pages.push(page);
    return page;
  }

  function placement(page, width, height) {
    let x = page.nextX;
    let y = page.nextY;
    let rowHeight = page.rowHeight;
    if (x + width > TEXTURE_ATLAS_SIZE) {
      x = 0;
      y += rowHeight;
      rowHeight = 0;
    }
    if (y + height > TEXTURE_ATLAS_SIZE) return null;
    return { x, y, nextX: x + width, nextY: y,
             rowHeight: Math.max(rowHeight, height) };
  }

  function paddedPixels(decoded) {
    const width = decoded.width + TEXTURE_ATLAS_PADDING * 2;
    const height = decoded.height + TEXTURE_ATLAS_PADDING * 2;
    const pixels = new Uint8Array(width * height * 4);
    for (let y = 0; y < height; y += 1) {
      const sourceY = Math.max(0, Math.min(decoded.height - 1,
                                             y - TEXTURE_ATLAS_PADDING));
      for (let x = 0; x < width; x += 1) {
        const sourceX = Math.max(0, Math.min(decoded.width - 1,
                                               x - TEXTURE_ATLAS_PADDING));
        const source = (sourceY * decoded.width + sourceX) * 4;
        pixels.set(decoded.pixels.subarray(source, source + 4),
                   (y * width + x) * 4);
      }
    }
    return { pixels, width, height };
  }

  function upload(region, decoded) {
    const padded = paddedPixels(decoded);
    gl.bindTexture(gl.TEXTURE_2D, region.texture);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, region.x - TEXTURE_ATLAS_PADDING,
                     region.y - TEXTURE_ATLAS_PADDING, padded.width,
                     padded.height, gl.RGBA, gl.UNSIGNED_BYTE, padded.pixels);
  }

  return {
    store(decoded) {
      const width = decoded.width + TEXTURE_ATLAS_PADDING * 2;
      const height = decoded.height + TEXTURE_ATLAS_PADDING * 2;
      if (width > TEXTURE_ATLAS_SIZE || height > TEXTURE_ATLAS_SIZE) {
        return null;
      }
      let page;
      let slot;
      for (const candidate of pages) {
        slot = placement(candidate, width, height);
        if (slot !== null) {
          page = candidate;
          break;
        }
      }
      if (page === undefined) {
        page = createPage();
        slot = placement(page, width, height);
      }
      page.nextX = slot.nextX;
      page.nextY = slot.nextY;
      page.rowHeight = slot.rowHeight;
      const region = {
        texture: page.texture,
        x: slot.x + TEXTURE_ATLAS_PADDING,
        y: slot.y + TEXTURE_ATLAS_PADDING,
        u: (slot.x + TEXTURE_ATLAS_PADDING) / TEXTURE_ATLAS_SIZE,
        v: (slot.y + TEXTURE_ATLAS_PADDING) / TEXTURE_ATLAS_SIZE,
        width: decoded.width / TEXTURE_ATLAS_SIZE,
        height: decoded.height / TEXTURE_ATLAS_SIZE
      };
      upload(region, decoded);
      return region;
    },
    clear() {
      for (const page of pages) gl.deleteTexture(page.texture);
      pages.length = 0;
    }
  };
}

function rgb555ToRgba(memory, offset) {
  if (offset < 0 || offset + 1 >= memory.length) return null;
  const rgba = rgb555ValueToRgba((memory[offset] << 8) | memory[offset + 1]);
  rgba[3] = P_MODE_0_ALPHA;
  return rgba;
}

function rgb555ValueToRgba(value) {
  return [
    ((value >>> 10) & 31) * 255 / 31,
    ((value >>> 5) & 31) * 255 / 31,
    (value & 31) * 255 / 31,
    (value & 0x8000) === 0 ? P_MODE_0_ALPHA : P_MODE_1_ALPHA
  ];
}

function indexed4Pixels(memory, command) {
  if (command.width === 0 || command.height === 0) return null;
  const pixels = new Uint8Array(command.width * command.height * 4);
  const rowBytes = Math.max(8, Math.ceil(command.width / 2));
  let destination = 0;

  for (let y = 0; y < command.height; y += 1) {
    const row = command.source + 8 + y * rowBytes;
    for (let x = 0; x < command.width; x += 1) {
      const packed = memory[row + Math.floor(x / 2)];
      const index = x % 2 === 0 ? packed >>> 4 : packed & 15;
      const colour = rgb555ToRgba(memory, command.palette + index * 2);
      if (colour === null) return null;
      if (index === 0 && (command.encoding & 0x80000000) !== 0) {
        colour[3] = 0;
      }
      pixels.set(colour, destination);
      destination += 4;
    }
  }
  return { pixels, width: command.width, height: command.height };
}

function indexed6Pixels(memory, command) {
  const encoding = command.encoding & 0xff;
  if (command.width === 0 || command.height === 0 ||
      command.width > 512 || command.height > 512) {
    return null;
  }
  const pixels = new Uint8Array(command.width * command.height * 4);
  let source = command.source + 8;
  let rowBytes = Math.ceil(command.width * 6 / 32) * 4;
  let sourceBitOffset = 0;

  if (encoding === 6) {
    source = command.source;
    rowBytes = 384;
    sourceBitOffset = (command.encoding >>> 8) & 0xffff;
  } else if (encoding === 7) {
    source = command.source;
    rowBytes = 96;
    sourceBitOffset = (command.encoding >>> 8) & 0xffff;
  } else if (encoding === 8) {
    source = command.source;
    rowBytes = 24;
  }
  const sourceBytes = Math.ceil((sourceBitOffset + command.width * 6) / 8);
  const end = source + (command.height - 1) * rowBytes + sourceBytes;
  if (!Number.isSafeInteger(end) || source < 0 || end > memory.length) {
    return null;
  }
  let destination = 0;

  for (let y = 0; y < command.height; y += 1) {
    const row = source + y * rowBytes;
    for (let x = 0; x < command.width; x += 1) {
      const bit = sourceBitOffset + x * 6;
      const byte = row + Math.floor(bit / 8);
      const shift = 10 - (bit % 8);
      const value = (memory[byte] << 8) | memory[byte + 1];
      const index = (value >>> shift) & 63;
      const rgba = rgb555ToRgba(memory, command.palette + (index & 31) * 2);
      if (rgba === null) return null;
      if (index & 32) rgba[3] = P_MODE_1_ALPHA;
      if ((index & 31) === 0 && (command.encoding & 0x80000000) !== 0) {
        rgba[3] = 0;
      }
      pixels.set(rgba, destination);
      destination += 4;
    }
  }
  return { pixels, width: command.width, height: command.height };
}

function readPackedBits(memory, state, count) {
  let value = 0;
  for (let bit = 0; bit < count; bit += 1) {
    if (state.offset + Math.floor(state.bit / 8) >= state.end) return null;
    value = (value << 1) | ((memory[state.offset + Math.floor(state.bit / 8)] >> (7 - (state.bit & 7))) & 1);
    state.bit += 1;
  }
  return value;
}

function indexed4PackedPixels(memory, command) {
  const bppFromCode = [0, 1, 2, 4, 6, 8, 16, 0];
  if (command.source < 0 || command.source + 3 >= memory.length) return null;
  const pre0 = ((memory[command.source] << 24) |
                (memory[command.source + 1] << 16) |
                (memory[command.source + 2] << 8) |
                memory[command.source + 3]) >>> 0;
  const bitsPerPixel = bppFromCode[pre0 & 7];
  const headerBytes = bitsPerPixel <= 6 ? 1 : 2;
  if (bitsPerPixel === 0) return null;

  let source = command.source + 4;
  const rows = [];

  for (let y = 0; y < command.height; y += 1) {
    if (source + headerBytes > memory.length) return null;
    const rowOffset = headerBytes === 1 ? memory[source] :
      (memory[source] << 8) | memory[source + 1];
    const rowSize = (rowOffset + 2) * 4;
    const rowEnd = source + rowSize;
    const state = { offset: source + headerBytes, bit: 0, end: rowEnd };
    const row = [];

    if (rowEnd > memory.length) return null;
    while (row.length < command.width) {
      const type = readPackedBits(memory, state, 2);
      if (type === null) return null;
      if (type === 0) {
        break;
      }
      const encodedCount = readPackedBits(memory, state, 6);
      if (encodedCount === null) return null;
      const count = encodedCount + 1;
      const repeatedIndex = type === 3 ?
        readPackedBits(memory, state, bitsPerPixel) : 0;
      if (repeatedIndex === null) return null;
      for (let index = 0; index < count; index += 1) {
        if (type === 2) {
          if (row.length < command.width) row.push(-1);
        } else {
          const paletteIndex = type === 3 ? repeatedIndex :
            readPackedBits(memory, state, bitsPerPixel);
          if (paletteIndex === null) return null;
          if (row.length < command.width) row.push(paletteIndex);
        }
      }
    }
    rows.push(row);
    source = rowEnd;
  }
  if (command.width === 0) return null;

  const pixels = new Uint8Array(command.width * command.height * 4);
  let destination = 0;
  for (const row of rows) {
    for (let x = 0; x < command.width; x += 1) {
      const paletteIndex = row[x];
      if (paletteIndex !== undefined && paletteIndex >= 0) {
        const paletteEntry = bitsPerPixel === 6 ? paletteIndex & 31 : paletteIndex;
        const colour = rgb555ToRgba(memory, command.palette + paletteEntry * 2);
        if (colour === null) return null;
        if (paletteEntry === 0 &&
            (command.encoding & 0x80000000) !== 0) {
          colour[3] = 0;
        } else if (bitsPerPixel === 6 && (paletteIndex & 32)) {
          colour[3] = P_MODE_1_ALPHA;
        }
        pixels.set(colour, destination);
      }
      destination += 4;
    }
  }
  return { pixels, width: command.width, height: command.height };
}

function direct16PackedPixels(memory, command) {
  let source = command.source + 4;
  const rows = [];

  for (let y = 0; y < command.height; y += 1) {
    if (source + 1 >= memory.length) return null;
    const rowOffset = (memory[source] << 8) | memory[source + 1];
    const rowEnd = source + (rowOffset + 2) * 4;
    const state = { offset: source + 2, bit: 0, end: rowEnd };
    const row = [];

    if (rowEnd > memory.length) return null;
    while (row.length < command.width) {
      const type = readPackedBits(memory, state, 2);
      if (type === null) return null;
      if (type === 0) break;

      const encodedCount = readPackedBits(memory, state, 6);
      if (encodedCount === null) return null;
      const count = encodedCount + 1;
      if (row.length + count > command.width) return null;
      if (type === 2) {
        row.push(...new Array(count).fill(null));
      } else if (type === 1) {
        for (let index = 0; index < count; index += 1) {
          const pixel = readPackedBits(memory, state, 16);
          if (pixel === null) return null;
          row.push(pixel);
        }
      } else {
        const pixel = readPackedBits(memory, state, 16);
        if (pixel === null) return null;
        row.push(...new Array(count).fill(pixel));
      }
    }
    if (row.length !== command.width) return null;
    rows.push(row);
    source = rowEnd;
  }

  const pixels = new Uint8Array(command.width * command.height * 4);
  let destination = 0;
  for (const row of rows) {
    for (const pixel of row) {
      if (pixel !== null) {
        const colour = rgb555ValueToRgba(pixel);
        if (pixel === 0 && (command.encoding & 0x80000000) !== 0) {
          colour[3] = 0;
        }
        pixels.set(colour, destination);
      }
      destination += 4;
    }
  }
  return { pixels, width: command.width, height: command.height };
}

function direct16Pixels(memory, command, sourceOffset = 8,
                        rowBytes = command.width * 2) {
  if (command.width === 0 || command.height === 0 ||
      command.width > 512 || command.height > 512) {
    return null;
  }
  const end = command.source + sourceOffset +
              (command.height - 1) * rowBytes + command.width * 2;
  if (!Number.isSafeInteger(end) || command.source < 0 || end > memory.length) {
    return null;
  }
  const pixels = new Uint8Array(command.width * command.height * 4);
  let destination = 0;

  for (let y = 0; y < command.height; y += 1) {
    const row = command.source + sourceOffset + y * rowBytes;
    for (let x = 0; x < command.width; x += 1) {
      const colour = rgb555ToRgba(memory, row + x * 2);
      if (colour === null) return null;
      if (colour[0] === 0 && colour[1] === 0 && colour[2] === 0 &&
          (command.encoding & 0x80000000) !== 0) {
        colour[3] = 0;
      }
      pixels.set(colour, destination);
      destination += 4;
    }
  }
  return { pixels, width: command.width, height: command.height };
}

function createIndexedTexture(gl, memory, command) {
  if (command.source === 0) return null;
  const encoding = command.encoding & 0xff;
  if (encoding === 0) {
    if (command.palette === 0) return null;
    return indexed4Pixels(memory, command);
  }
  if (encoding === 1 || encoding === 6 || encoding === 7 || encoding === 8) {
    if (command.palette === 0) return null;
    return indexed6Pixels(memory, command);
  }
  if (encoding === 4) {
    if (command.palette === 0) return null;
    return indexed4PackedPixels(memory, command);
  }
  if (encoding === 3) {
    return direct16PackedPixels(memory, command);
  }
  if (encoding === 2) {
    return direct16Pixels(memory, command);
  }
  if (encoding === 5) {
    return direct16Pixels(memory, command, 0);
  }
  if (encoding === 9) {
    return direct16Pixels(memory, command, 8, 8);
  }
  return null;
}

function gamepadControls() {
  let controls = 0;
  const gamepads = navigator.getGamepads?.() ?? [];
  const gamepad = [...gamepads].find((candidate) => candidate?.connected);
  if (gamepad === undefined) {
    return controls;
  }

  if (gamepad.axes[0] <= -0.5) controls |= CONTROL.left;
  if (gamepad.axes[0] >= 0.5) controls |= CONTROL.right;
  if (gamepad.axes[1] <= -0.5) controls |= CONTROL.up;
  if (gamepad.axes[1] >= 0.5) controls |= CONTROL.down;
  if (gamepad.buttons[0]?.pressed) controls |= CONTROL.a;
  if (gamepad.buttons[1]?.pressed) controls |= CONTROL.b;
  if (gamepad.buttons[2]?.pressed) controls |= CONTROL.c;
  if (gamepad.buttons[9]?.pressed) controls |= CONTROL.start;
  if (gamepad.buttons[8]?.pressed) controls |= CONTROL.x;
  if (gamepad.buttons[4]?.pressed) controls |= CONTROL.leftShift;
  if (gamepad.buttons[5]?.pressed) controls |= CONTROL.rightShift;
  return controls;
}

export function createStarFighterRuntime(canvas, status) {
  let keyboardControls = 0;
  let module;
  let fallbackTexture;
  let textFont;
  const textures = new Map();
  const gl = canvas.getContext("webgl2");
  const textureAtlas = createTextureAtlas(gl);
  const frameTextureAtlas = createTextureAtlas(gl);
  const audioMixer = createAudioMixer();
  const soundEffects = createSoundEffects(audioMixer);
  const streamedMedia = createStreamedMedia(audioMixer);
  fallbackTexture = textureAtlas.store({
    width: 1,
    height: 1,
    pixels: new Uint8Array([255, 255, 255, 255])
  });
  const renderer = createWebGLRenderer(canvas, (command, memory) => {
    const encoding = command.encoding & 0xff;
    const isTransientTexture = encoding === 1 || encoding === 9;
    const key = `${command.source}:${command.palette}:${command.width}:${command.height}:${command.encoding}`;
    const decoded = createIndexedTexture(gl, memory, command);
    if (decoded === null) return fallbackTexture;
    if (isTransientTexture) {
      return frameTextureAtlas.store(decoded) ?? fallbackTexture;
    }

    const cached = textures.get(key);
    if (cached !== undefined) return cached;
    const region = textureAtlas.store(decoded);
    if (region === null) return fallbackTexture;
    textures.set(key, region);
    return region;
  }, (command) => module.UTF8ToString(command.source));
  const cinematics = createCinematicPlayer(audioMixer, renderer, status);

  window.addEventListener("keydown", (event) => {
    if (cinematics.handleUserInput()) {
      event.preventDefault();
      return;
    }
    audioMixer.resume();
    const control = KEY_CONTROLS.get(event.code);
    if (control === undefined) return;
    keyboardControls |= control;
    event.preventDefault();
  });
  window.addEventListener("keyup", (event) => {
    const control = KEY_CONTROLS.get(event.code);
    if (control === undefined) return;
    keyboardControls &= ~control;
    event.preventDefault();
  });
  window.addEventListener("blur", () => {
    keyboardControls = 0;
  });
  window.addEventListener("pointerdown", (event) => {
    if (cinematics.handleUserInput()) {
      event.preventDefault();
      event.stopPropagation();
    }
  }, true);
  canvas.addEventListener("pointerdown", () => {
    audioMixer.resume();
  });

  return {
    attachModule(nextModule) {
      module = nextModule;
      if (textFont !== undefined) {
        renderer.setTextFont(module.HEAPU8, textFont.source, textFont.size);
      }
      status.textContent = "Running - arrows/WASD move, Z/X/C act, Enter starts";
    },
    controlPadState() {
      return keyboardControls | gamepadControls();
    },
    soundLoadSamples() {
      soundEffects.load();
    },
    soundUnloadSamples() {
      soundEffects.unload();
    },
    soundInitialise() {
      soundEffects.initialise();
    },
    soundTerminate() {
      soundEffects.terminate();
    },
    soundPlay(sample, pitch, volume, stereoPosition) {
      return soundEffects.play(sample, pitch, volume, stereoPosition);
    },
    soundStop(channel) {
      soundEffects.stop(channel);
    },
    soundPitchBend(channel, pitchBend) {
      return soundEffects.pitchBend(channel, pitchBend);
    },
    soundAlter(channel, volume, stereoPosition) {
      soundEffects.alter(channel, volume, stereoPosition);
    },
    soundSetMasterVolume(volume) {
      soundEffects.setMasterVolume(volume);
    },
    soundSetEnabled(enabled) {
      soundEffects.setEnabled(enabled);
    },
    musicInitialise() {
      streamedMedia.initialise();
    },
    musicTerminate() {
      streamedMedia.terminate();
    },
    musicResetPlaylist() {
      streamedMedia.resetPlaylist();
    },
    musicAddTrack(track) {
      streamedMedia.addTrack(track);
    },
    musicTakeTrack(track) {
      streamedMedia.takeTrack(track);
    },
    musicPlay(mode, track, tracksLeft) {
      streamedMedia.play(mode, track, tracksLeft);
    },
    musicStop() {
      streamedMedia.stop();
    },
    musicPause() {
      streamedMedia.pause();
    },
    musicResume() {
      streamedMedia.resume();
    },
    musicSetMasterVolume(volume) {
      streamedMedia.setMasterVolume(volume);
    },
    musicQuery() {
      return streamedMedia.query();
    },
    musicPlayVoice(path) {
      streamedMedia.playVoice(path);
    },
    videoPlay(path) {
      return cinematics.play(path);
    },
    nvramSize(name) {
      try {
        const encoded = localStorage.getItem(NVRAM_STORAGE_PREFIX + name);
        return encoded === null ? -1 : decodeNVRAMData(encoded).length;
      } catch (error) {
        logNVRAMFailure("read", error);
        return -1;
      }
    },
    nvramLoad(name, destination, capacity) {
      try {
        const encoded = localStorage.getItem(NVRAM_STORAGE_PREFIX + name);
        if (encoded === null) return -1;

        const bytes = decodeNVRAMData(encoded);
        if (bytes.length > capacity || module === undefined ||
            destination < 0 || destination + bytes.length > module.HEAPU8.length) {
          return -1;
        }
        module.HEAPU8.set(bytes, destination);
        return bytes.length;
      } catch (error) {
        logNVRAMFailure("read", error);
        return -1;
      }
    },
    nvramStore(name, source, size) {
      try {
        if (module === undefined || source < 0 || size < 0 ||
            source + size > module.HEAPU8.length) {
          return -1;
        }
        const bytes = module.HEAPU8.slice(source, source + size);
        localStorage.setItem(NVRAM_STORAGE_PREFIX + name, encodeNVRAMData(bytes));
        return 0;
      } catch (error) {
        logNVRAMFailure("write", error);
        return -1;
      }
    },
    nvramDelete(name) {
      try {
        localStorage.removeItem(NVRAM_STORAGE_PREFIX + name);
        return 0;
      } catch (error) {
        logNVRAMFailure("delete", error);
        return -1;
      }
    },
    nvramList(index, destination, capacity) {
      try {
        if (module === undefined || destination < 0 || capacity === 0 ||
            destination + capacity > module.HEAPU8.length) {
          return -1;
        }
        const names = [];
        for (let storageIndex = 0; storageIndex < localStorage.length;
             storageIndex += 1) {
          const key = localStorage.key(storageIndex);
          if (key !== null && key.startsWith(NVRAM_STORAGE_PREFIX)) {
            names.push(key.slice(NVRAM_STORAGE_PREFIX.length));
          }
        }
        names.sort();
        if (index >= names.length) return -1;

        const bytes = new TextEncoder().encode(names[index]);
        if (bytes.length + 1 > capacity) return -1;
        module.HEAPU8.set(bytes, destination);
        module.HEAPU8[destination + bytes.length] = 0;
        return bytes.length;
      } catch (error) {
        logNVRAMFailure("list", error);
        return -1;
      }
    },
    setTextFont(source, size) {
      textFont = { source, size };
      if (module !== undefined) {
        renderer.setTextFont(module.HEAPU8, source, size);
      }
    },
    resetTextures() {
      textures.clear();
      textureAtlas.clear();
      frameTextureAtlas.clear();
      fallbackTexture = textureAtlas.store({
        width: 1,
        height: 1,
        pixels: new Uint8Array([255, 255, 255, 255])
      });
    },
    copyVram(bank, source) {
      if (module === undefined || source === 0) return;
      renderer.copyVram(bank, module.HEAPU8, source);
    },
    clearBank(bank, value) {
      renderer.clearBank(bank, value);
    },
    fillRect(bank, colour, left, top, right, bottom) {
      renderer.fillRect(bank, colour, left, top, right, bottom);
    },
    queueScreenCel(target, source, x, y, hdx, vdy, pixc, ccbFlags) {
      renderer.queueScreenCel(target, source, x, y, hdx, vdy, pixc, ccbFlags);
    },
    setFade(opacity) {
      renderer.setFade(opacity);
    },
    present(commandAddress, commandCount, bank) {
      if (module === undefined) return;
      frameTextureAtlas.clear();
      renderer.present(module.HEAPU8, commandAddress, commandCount, bank);
    }
  };
}
