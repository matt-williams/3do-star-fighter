const COMMAND_WORDS = 17;
const COMMAND_BYTES = COMMAND_WORDS * Uint32Array.BYTES_PER_ELEMENT;
const TEXT_ENCODING = 10;
const CCB_PXOR = 0x00000800;
const CCB_USEAV = 0x00002400;

const vertexShaderSource = `#version 300 es
in vec2 position;
in vec2 commandP0;
in vec2 commandP1;
in vec2 commandP2;
in vec2 commandP3;
in vec2 atlasOrigin;
in vec2 atlasScale;
in vec2 commandShade;
in vec2 commandOpacity;
in float commandEdgeCoverage;
out vec2 fragmentP0;
out vec2 fragmentP1;
out vec2 fragmentP2;
out vec2 fragmentP3;
out vec2 fragmentAtlasOrigin;
out vec2 fragmentAtlasScale;
out vec2 fragmentShade;
out vec2 fragmentOpacity;
out float fragmentEdgeCoverage;
void main() {
  gl_Position = vec4(
    (position - vec2(160.0, 120.0)) / vec2(160.0, -120.0),
    0.0,
    1.0
  );
  fragmentP0 = commandP0;
  fragmentP1 = commandP1;
  fragmentP2 = commandP2;
  fragmentP3 = commandP3;
  fragmentAtlasOrigin = atlasOrigin;
  fragmentAtlasScale = atlasScale;
  fragmentShade = commandShade;
  fragmentOpacity = commandOpacity;
  fragmentEdgeCoverage = commandEdgeCoverage;
}`;

const fragmentShaderSource = `#version 300 es
precision highp float;
uniform sampler2D texture0;
uniform float screenHeight;
in vec2 fragmentP0;
in vec2 fragmentP1;
in vec2 fragmentP2;
in vec2 fragmentP3;
in vec2 fragmentAtlasOrigin;
in vec2 fragmentAtlasScale;
in vec2 fragmentShade;
in vec2 fragmentOpacity;
in float fragmentEdgeCoverage;
out vec4 color;

float cross2(vec2 first, vec2 second) {
  return first.x * second.y - first.y * second.x;
}

void considerRoot(float u, vec2 point, vec2 firstEdge, vec2 secondEdge,
                  vec2 diagonal, inout bool hasRoot, inout vec2 selected) {
  float uvTolerance = fragmentEdgeCoverage > 0.5 ? 0.5 : 0.0001;
  if (u < -uvTolerance || u > 1.0 + uvTolerance) return;

  vec2 secondDirection = secondEdge + u * diagonal;
  float lengthSquared = dot(secondDirection, secondDirection);
  float edgeScale = max(1.0, max(dot(firstEdge, firstEdge),
                                 max(dot(secondEdge, secondEdge),
                                     dot(diagonal, diagonal))));
  if (lengthSquared <= edgeScale * 0.00000001) return;

  float v = dot(point - u * firstEdge, secondDirection) / lengthSquared;
  if (v < -uvTolerance || v > 1.0 + uvTolerance) return;

  vec2 candidate = clamp(vec2(u, v), 0.0, 1.0);
  if (!hasRoot || candidate.y > selected.y ||
      (candidate.y == selected.y && candidate.x > selected.x)) {
    selected = candidate;
    hasRoot = true;
  }
}

void main() {
  vec2 point = vec2(gl_FragCoord.x, screenHeight - gl_FragCoord.y) - fragmentP0;
  vec2 firstEdge = fragmentP1 - fragmentP0;
  vec2 secondEdge = fragmentP3 - fragmentP0;
  vec2 diagonal = fragmentP2 - fragmentP1 - fragmentP3 + fragmentP0;
  float quadratic = -cross2(firstEdge, diagonal);
  float linear = cross2(point, diagonal) - cross2(firstEdge, secondEdge);
  float constant = cross2(point, secondEdge);
  float coefficientScale = max(1.0, max(abs(quadratic),
                                        max(abs(linear), abs(constant))));
  bool hasRoot = false;
  vec2 uv = vec2(0.0);

  if (abs(quadratic) <= coefficientScale * 0.000001) {
    if (abs(linear) > coefficientScale * 0.000001) {
      considerRoot(-constant / linear, point, firstEdge, secondEdge,
                   diagonal, hasRoot, uv);
    }
  } else {
    float discriminant = linear * linear - 4.0 * quadratic * constant;
    float discriminantScale = linear * linear +
      abs(4.0 * quadratic * constant) + 1.0;
    if (discriminant >= -discriminantScale * 0.000001) {
      float root = sqrt(max(discriminant, 0.0));
      float divisor = 2.0 * quadratic;
      considerRoot((-linear - root) / divisor, point, firstEdge, secondEdge,
                   diagonal, hasRoot, uv);
      considerRoot((-linear + root) / divisor, point, firstEdge, secondEdge,
                   diagonal, hasRoot, uv);
    }
  }
  if (!hasRoot) discard;
  vec4 texel = texture(texture0, fragmentAtlasOrigin + uv * fragmentAtlasScale);
  if (texel.a == 0.0) discard;
  bool pMode1 = texel.a < 0.75;
  float sourceShade = pMode1 ? fragmentShade.y : fragmentShade.x;
  float sourceOpacity = pMode1 ? fragmentOpacity.y : fragmentOpacity.x;
  color = vec4(texel.rgb * sourceShade, sourceOpacity);
}`;

const textVertexShaderSource = `#version 300 es
in vec2 position;
in vec2 textureCoordinate;
out vec2 glyphCoordinate;
void main() {
  gl_Position = vec4(
    (position - vec2(160.0, 120.0)) / vec2(160.0, -120.0),
    0.0,
    1.0
  );
  glyphCoordinate = textureCoordinate;
}`;

const textFragmentShaderSource = `#version 300 es
precision highp float;
uniform sampler2D fontAtlas;
uniform vec3 foreground;
uniform vec3 outline;
in vec2 glyphCoordinate;
out vec4 color;
vec3 rgb555(vec3 value) {
  return floor(value * 31.0 + 0.5);
}
vec3 scaledColour(vec3 source, float level) {
  vec3 result = floor(source * level / 8.0);
  if (result.r == 0.0 && result.g == 0.0 && result.b == 0.0) {
    result = vec3(0.0, 0.0, 1.0);
  }
  return result / 31.0;
}
void main() {
  vec4 glyph = texture(fontAtlas, glyphCoordinate);
  if (glyph.a == 0.0) {
    discard;
  }
  float intensity = floor(glyph.r * 255.0 + 0.5);
  float colourType = floor(glyph.g * 255.0 + 0.5);
  if (colourType == 0.0) {
    float level = intensity == 7.0 ? 8.0 : intensity + 1.0;
    color = vec4(scaledColour(rgb555(foreground), level), level / 8.0);
  } else if (colourType == 1.0) {
    float level = intensity == 7.0 ? 8.0 : intensity + 1.0;
    color = vec4(scaledColour(rgb555(outline), level), level / 8.0);
  } else {
    float level = intensity == 0.0 ? 0.0 :
      (intensity == 7.0 ? 8.0 : intensity + 1.0);
    vec3 result = floor(rgb555(foreground) * level / 8.0) +
      floor(rgb555(outline) * (8.0 - level) / 8.0);
    color = vec4(result / 31.0, 1.0);
  }
}`;

const screenVertexShaderSource = `#version 300 es
in vec2 position;
in vec2 textureCoordinate;
out vec2 sourceCoordinate;
void main() {
  gl_Position = vec4(
    (position - vec2(160.0, 120.0)) / vec2(160.0, -120.0),
    0.0,
    1.0
  );
  sourceCoordinate = textureCoordinate;
}`;

const screenFragmentShaderSource = `#version 300 es
precision highp float;
uniform sampler2D texture0;
uniform float fade;
uniform float opacity;
uniform float colourScale;
in vec2 sourceCoordinate;
out vec4 color;
void main() {
  vec4 source = texture(texture0, sourceCoordinate);
  color = vec4(source.rgb * colourScale * (1.0 - fade), source.a * opacity);
}`;

function createShader(gl, type, source) {
  const shader = gl.createShader(type);
  gl.shaderSource(shader, source);
  gl.compileShader(shader);

  if (gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    return shader;
  }

  const error = gl.getShaderInfoLog(shader);
  gl.deleteShader(shader);
  throw new Error(error);
}

function createProgram(gl, vertexSource, fragmentSource) {
  const program = gl.createProgram();
  gl.attachShader(program, createShader(gl, gl.VERTEX_SHADER, vertexSource));
  gl.attachShader(program, createShader(gl, gl.FRAGMENT_SHADER, fragmentSource));
  gl.linkProgram(program);

  if (gl.getProgramParameter(program, gl.LINK_STATUS)) {
    return program;
  }

  const error = gl.getProgramInfoLog(program);
  gl.deleteProgram(program);
  throw new Error(error);
}

function signedTriangleArea(command, first, second, third) {
  return (command.x[second] - command.x[first]) *
           (command.y[third] - command.y[first]) -
         (command.y[second] - command.y[first]) *
           (command.x[third] - command.x[first]);
}

function fallbackGeometry(command) {
  let orientation = 0;
  let hasOpposingOrientation = false;

  for (let index = 0; index < 4; index += 1) {
    const area = signedTriangleArea(
      command,
      index,
      (index + 1) % 4,
      (index + 2) % 4
    );
    if (area === 0) continue;
    const sign = Math.sign(area);
    if (orientation !== 0 && sign !== orientation) {
      hasOpposingOrientation = true;
    }
    orientation = sign;
  }
  if (hasOpposingOrientation) {
    return { requiresFallback: true };
  }
  if (orientation === 0) {
    return { requiresFallback: true };
  }
  return { requiresFallback: false };
}

function isCollapsedQuadTriangle(command) {
  const corners = new Set();
  let hasCollapsedEdge = false;

  for (let index = 0; index < 4; index += 1) {
    const next = (index + 1) % 4;
    corners.add(`${command.x[index]},${command.y[index]}`);
    if (command.x[index] === command.x[next] &&
        command.y[index] === command.y[next]) {
      hasCollapsedEdge = true;
    }
  }
  return hasCollapsedEdge && corners.size === 3;
}

function convexHull(command) {
  const uniquePoints = [];
  for (let index = 0; index < 4; index += 1) {
    const point = { x: command.x[index], y: command.y[index] };
    if (!uniquePoints.some((other) =>
      other.x === point.x && other.y === point.y)) {
      uniquePoints.push(point);
    }
  }
  if (uniquePoints.length < 3) return null;

  uniquePoints.sort((first, second) =>
    first.x === second.x ? first.y - second.y : first.x - second.x);
  const cross = (origin, first, second) =>
    (first.x - origin.x) * (second.y - origin.y) -
    (first.y - origin.y) * (second.x - origin.x);
  const buildHalf = (points) => {
    const half = [];
    for (const point of points) {
      while (half.length >= 2 &&
             cross(half[half.length - 2], half[half.length - 1], point) <= 0) {
        half.pop();
      }
      half.push(point);
    }
    return half;
  };
  const lower = buildHalf(uniquePoints);
  const upper = buildHalf([...uniquePoints].reverse());

  lower.pop();
  upper.pop();
  const hull = lower.concat(upper);
  return hull.length < 3 ? null : hull;
}

function expandedConvexHull(hull, distance) {
  let signedArea = 0;
  const expanded = [];

  for (let index = 0; index < hull.length; index += 1) {
    const next = hull[(index + 1) % hull.length];
    signedArea += hull[index].x * next.y - next.x * hull[index].y;
  }
  for (let index = 0; index < hull.length; index += 1) {
    const previous = hull[(index + hull.length - 1) % hull.length];
    const current = hull[index];
    const next = hull[(index + 1) % hull.length];
    const previousDelta = {
      x: current.x - previous.x,
      y: current.y - previous.y
    };
    const nextDelta = {
      x: next.x - current.x,
      y: next.y - current.y
    };
    const previousLength = Math.hypot(previousDelta.x, previousDelta.y);
    const nextLength = Math.hypot(nextDelta.x, nextDelta.y);
    if (previousLength === 0 || nextLength === 0) return hull;

    const normal = (delta, length) => signedArea > 0 ?
      { x: delta.y * distance / length, y: -delta.x * distance / length } :
      { x: -delta.y * distance / length, y: delta.x * distance / length };
    const previousNormal = normal(previousDelta, previousLength);
    const nextNormal = normal(nextDelta, nextLength);
    const first = {
      x: previous.x + previousNormal.x,
      y: previous.y + previousNormal.y
    };
    const second = {
      x: current.x + previousNormal.x,
      y: current.y + previousNormal.y
    };
    const third = {
      x: current.x + nextNormal.x,
      y: current.y + nextNormal.y
    };
    const denominator = previousDelta.x * nextDelta.y -
      previousDelta.y * nextDelta.x;
    if (Math.abs(denominator) < 0.0001) {
      expanded.push({
        x: current.x + (previousNormal.x + nextNormal.x) * 0.5,
        y: current.y + (previousNormal.y + nextNormal.y) * 0.5
      });
      continue;
    }
    const deltaX = third.x - first.x;
    const deltaY = third.y - first.y;
    const factor = (deltaX * nextDelta.y - deltaY * nextDelta.x) /
      denominator;
    expanded.push({
      x: first.x + previousDelta.x * factor,
      y: first.y + previousDelta.y * factor
    });
  }
  return expanded;
}

function commandFromWasm(memory, address) {
  const words = new Uint32Array(memory.buffer, address, COMMAND_WORDS);
  const signed = new Int32Array(memory.buffer, address, COMMAND_WORDS);
  return {
    source: words[0],
    palette: words[1],
    x: [signed[2], signed[3], signed[4], signed[5]],
    y: [signed[6], signed[7], signed[8], signed[9]],
    shade: signed[10],
    width: words[11],
    height: words[12],
    blend: words[13],
    encoding: words[14],
    pixc: words[15],
    ccbFlags: words[16]
  };
}

function createTextFontAtlas(gl, memory, source, size) {
  const headerSize = 84;
  const columns = 16;

  if (source === 0 || size < headerSize || source > memory.length - size) {
    return null;
  }

  const view = new DataView(memory.buffer, source, size);
  const readWord = (offset) => view.getUint32(offset, false);
  const chunkId = readWord(0);
  const chunkSize = readWord(4);
  const charHeight = readWord(16);
  const charWidth = readWord(20);
  const bitsPerPixel = readWord(24);
  const firstChar = readWord(28);
  const lastChar = readWord(32);
  const charExtra = readWord(36);
  const leading = readWord(48);
  const charInfoOffset = readWord(52);
  const charInfoSize = readWord(56);
  const charDataOffset = readWord(60);
  const charDataSize = readWord(64);

  if (chunkId !== 0x464f4e54 || chunkSize !== size || bitsPerPixel !== 5 ||
      charHeight === 0 || charWidth === 0 || firstChar > lastChar ||
      charInfoSize < (lastChar - firstChar + 1) * 4 ||
      charInfoOffset > size || charInfoSize > size - charInfoOffset ||
      charDataOffset > size || charDataSize > size - charDataOffset) {
    return null;
  }

  const glyphCount = lastChar - firstChar + 1;
  const atlasWidth = charWidth * columns;
  const atlasHeight = charHeight * Math.ceil(glyphCount / columns);
  const pixels = new Uint8Array(atlasWidth * atlasHeight * 4);
  const glyphWidths = new Uint8Array(glyphCount);

  const writePixel = (baseX, baseY, x, y, value) => {
    if (x >= charWidth || y >= charHeight || value === 0) return;
    const offset = ((baseY + y) * atlasWidth + baseX + x) * 4;

    pixels[offset] = value & 7;
    pixels[offset + 1] = value >>> 3;
    pixels[offset + 3] = 255;
  };

  for (let index = 0; index < glyphCount; index += 1) {
    const info = readWord(charInfoOffset + index * 4);
    const glyphOffset = info >>> 10;
    const glyphWidth = info & 0xff;
    const nextOffset = index + 1 < glyphCount ?
      readWord(charInfoOffset + (index + 1) * 4) >>> 10 : charDataSize;
    const baseX = (index % columns) * charWidth;
    const baseY = Math.floor(index / columns) * charHeight;
    let sourceOffset = charDataOffset + glyphOffset;
    const sourceEnd = charDataOffset + nextOffset;
    let x = 0;
    let y = 0;

    glyphWidths[index] = glyphWidth;
    if (glyphWidth === 0 || glyphOffset > nextOffset ||
        nextOffset > charDataSize) {
      continue;
    }

    const advance = () => {
      x += 1;
      if (x === glyphWidth) {
        x = 0;
        y += 1;
        return true;
      }
      return false;
    };
    const copyPixel = (lineDelta) => {
      const sourceY = y - lineDelta;

      if (x < charWidth && y < charHeight && sourceY >= 0) {
        const sourcePixel = ((baseY + sourceY) * atlasWidth + baseX + x) * 4;
        const destination = ((baseY + y) * atlasWidth + baseX + x) * 4;
        pixels.set(pixels.subarray(sourcePixel, sourcePixel + 4), destination);
      }
      advance();
    };

    while (y < charHeight && sourceOffset + 1 < sourceEnd) {
      const packet = view.getUint16(sourceOffset, false);
      sourceOffset += 2;
      if ((packet & 0x8000) !== 0) {
        for (const shift of [10, 5, 0]) {
          writePixel(baseX, baseY, x, y, (packet >>> shift) & 31);
          advance();
          if (y === charHeight) break;
        }
      } else if ((packet & 0xf000) === 0x4000) {
        y += packet & 0xff;
        x = 0;
      } else if ((packet & 0xf000) === 0x2000) {
        const count = (packet >>> 5) & 31;
        const value = packet & 31;
        for (let run = 0; run < count && y < charHeight; run += 1) {
          writePixel(baseX, baseY, x, y, value);
          advance();
        }
      } else if ((packet & 0xf000) === 0x1000) {
        const count = (packet >>> 3) & 31;
        const lineDelta = (packet & 7) + 1;
        for (let run = 0; run < count && y < charHeight; run += 1) {
          copyPixel(lineDelta);
        }
      } else if (packet === 0) {
        x = 0;
        y += 1;
      } else {
        for (const shift of [5, 0]) {
          writePixel(baseX, baseY, x, y, (packet >>> shift) & 31);
          if (advance() || y === charHeight) break;
        }
      }
    }
  }

  const texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, atlasWidth, atlasHeight, 0,
                gl.RGBA, gl.UNSIGNED_BYTE, pixels);
  return {
    texture,
    pixels,
    atlasWidth,
    atlasHeight,
    charHeight,
    charWidth,
    charExtra,
    leading,
    firstChar,
    lastChar,
    glyphWidths
  };
}

export function createWebGLRenderer(canvas, textureForCommand, textForCommand) {
  const gl = canvas.getContext("webgl2", {
    alpha: false,
    antialias: false,
    depth: false
  });
  if (gl === null) {
    throw new Error("WebGL 2 is required");
  }

  const program = createProgram(gl, vertexShaderSource, fragmentShaderSource);
  const textProgram = createProgram(gl, textVertexShaderSource,
                                    textFragmentShaderSource);
  const screenProgram = createProgram(gl, screenVertexShaderSource,
                                      screenFragmentShaderSource);
  const vertices = gl.createBuffer();
  const textVertices = gl.createBuffer();
  const screenVertices = gl.createBuffer();
  const position = gl.getAttribLocation(program, "position");
  const commandP0 = gl.getAttribLocation(program, "commandP0");
  const commandP1 = gl.getAttribLocation(program, "commandP1");
  const commandP2 = gl.getAttribLocation(program, "commandP2");
  const commandP3 = gl.getAttribLocation(program, "commandP3");
  const atlasOrigin = gl.getAttribLocation(program, "atlasOrigin");
  const atlasScale = gl.getAttribLocation(program, "atlasScale");
  const commandShade = gl.getAttribLocation(program, "commandShade");
  const commandOpacity = gl.getAttribLocation(program, "commandOpacity");
  const commandEdgeCoverage = gl.getAttribLocation(program,
                                                    "commandEdgeCoverage");
  const screenHeight = gl.getUniformLocation(program, "screenHeight");
  const textPosition = gl.getAttribLocation(textProgram, "position");
  const textTextureCoordinate = gl.getAttribLocation(textProgram,
                                                      "textureCoordinate");
  const textForeground = gl.getUniformLocation(textProgram, "foreground");
  const textOutline = gl.getUniformLocation(textProgram, "outline");
  const screenPosition = gl.getAttribLocation(screenProgram, "position");
  const screenTextureCoordinate = gl.getAttribLocation(
    screenProgram, "textureCoordinate"
  );
  const screenFade = gl.getUniformLocation(screenProgram, "fade");
  const screenOpacity = gl.getUniformLocation(screenProgram, "opacity");
  const screenColourScale = gl.getUniformLocation(screenProgram, "colourScale");
  let textFont;
  const screenOperations = [];
  let fade = 0;
  // The game begins rendering into bank 0 while bank 1 is the blank display.
  let displayedBank = 1;
  const displayWidth = canvas.width;
  const displayHeight = canvas.height;

  function createScreenBank() {
    const texture = gl.createTexture();
    const framebuffer = gl.createFramebuffer();

    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, displayWidth, displayHeight, 0,
                  gl.RGBA, gl.UNSIGNED_BYTE, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0,
                            gl.TEXTURE_2D, texture, 0);
    if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) {
      throw new Error("Unable to create a WebGL screen bank");
    }
    return { texture, framebuffer };
  }

  const screenBanks = [createScreenBank(), createScreenBank()];
  const cinematicTexture = gl.createTexture();
  let cinematicVideo;
  let cinematicFrameCallback;

  gl.bindTexture(gl.TEXTURE_2D, cinematicTexture);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
                new Uint8Array([0, 0, 0, 255]));
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

  function normaliseBank(bank) {
    return bank >= 0 && bank < screenBanks.length ? bank : 0;
  }

  function bindScreenBank(bank) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, screenBanks[normaliseBank(bank)].framebuffer);
    gl.viewport(0, 0, displayWidth, displayHeight);
    gl.scissor(0, 0, displayWidth, displayHeight);
  }

  function clearScreenBank(bank, red = 0, green = 0, blue = 0) {
    bindScreenBank(bank);
    gl.disable(gl.SCISSOR_TEST);
    gl.clearColor(red, green, blue, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.enable(gl.SCISSOR_TEST);
  }

  function drawScreenTexture(texture, left, top, width, height, fade = 0,
                             opacity = 1, colourScale = 1) {
    const right = left + width;
    const bottom = top + height;

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.useProgram(screenProgram);
    gl.bindBuffer(gl.ARRAY_BUFFER, screenVertices);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
      left, top, 0, 1,
      right, top, 1, 1,
      right, bottom, 1, 0,
      left, top, 0, 1,
      right, bottom, 1, 0,
      left, bottom, 0, 0
    ]), gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(screenPosition);
    gl.vertexAttribPointer(screenPosition, 2, gl.FLOAT, false,
                           4 * Float32Array.BYTES_PER_ELEMENT, 0);
    gl.enableVertexAttribArray(screenTextureCoordinate);
    gl.vertexAttribPointer(screenTextureCoordinate, 2, gl.FLOAT, false,
                           4 * Float32Array.BYTES_PER_ELEMENT,
                           2 * Float32Array.BYTES_PER_ELEMENT);
    gl.uniform1f(screenFade, fade);
    gl.uniform1f(screenOpacity, opacity);
    gl.uniform1f(screenColourScale, colourScale);
    gl.drawArrays(gl.TRIANGLES, 0, 6);
  }

  function drawBankToCanvas(bank, fade) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, displayWidth, displayHeight);
    gl.disable(gl.SCISSOR_TEST);
    gl.disable(gl.BLEND);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    if (cinematicVideo === undefined) {
      drawScreenTexture(screenBanks[normaliseBank(bank)].texture, 0, 0,
                        displayWidth, displayHeight, fade);
    } else {
      drawScreenTexture(cinematicTexture, 0, 20, displayWidth, 200, fade);
    }
    gl.enable(gl.SCISSOR_TEST);
  }

  function drawCinematicFrame() {
    if (cinematicVideo === undefined) return;
    if (cinematicVideo.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA) {
      gl.bindTexture(gl.TEXTURE_2D, cinematicTexture);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE,
                    cinematicVideo);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
      drawBankToCanvas(displayedBank, fade);
    }
  }

  function scheduleCinematicFrame() {
    if (cinematicVideo === undefined) return;
    if (typeof cinematicVideo.requestVideoFrameCallback === "function") {
      cinematicFrameCallback = cinematicVideo.requestVideoFrameCallback(() => {
        drawCinematicFrame();
        scheduleCinematicFrame();
      });
      return;
    }
    cinematicFrameCallback = requestAnimationFrame(() => {
      drawCinematicFrame();
      scheduleCinematicFrame();
    });
  }

  function applyScreenOperations() {
    for (const operation of screenOperations) {
      if (operation.target === operation.source) continue;

      bindScreenBank(operation.target);
      const command = { pixc: operation.pixc, ccbFlags: operation.ccbFlags };
      const half = pixcHalf(command, false);
      const source = sourceScale(half,
                                 (operation.ccbFlags & CCB_USEAV) !== 0);
      const operationType = pixelOperation(command, false);
      configureBlend(operationType,
                     operationType === "diminish" ? source : 1);
      drawScreenTexture(screenBanks[operation.source].texture, operation.x,
                        operation.y, operation.width, operation.height, 0,
                        operationType === "add" || operationType === "mix" ?
                          source : 1,
                        operationType === "replace" ? source : 1);
    }
    screenOperations.length = 0;
  }

  gl.useProgram(program);
  gl.uniform1i(gl.getUniformLocation(program, "texture0"), 0);
  gl.uniform1f(screenHeight, canvas.height);
  gl.useProgram(textProgram);
  gl.uniform1i(gl.getUniformLocation(textProgram, "fontAtlas"), 0);
  gl.useProgram(program);
  gl.disable(gl.DEPTH_TEST);
  gl.disable(gl.CULL_FACE);
  gl.enable(gl.SCISSOR_TEST);
  gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);

  function pixcHalf(command, pMode1) {
    return pMode1 ? command.pixc >>> 16 : command.pixc & 0xffff;
  }

  function sourceScale(half, useAv) {
    const divisor = [16, 2, 4, 8][(half >>> 8) & 3];
    const firstSource = (half >>> 15) & 1;
    const secondSource = (half >>> 6) & 3;
    const first = firstSource === 0 ? ((half >>> 10) & 7) + 1 : 0;
    let second = secondSource === 3 ? 1 : 0;

    if (useAv && second !== 0) {
      // AV's SF2 field applies only to the PDC second-source contribution.
      const secondDivisor = [1, 2, 4][(half >>> 4) & 3] || 1;
      second /= secondDivisor;
    }
    return (first / divisor + second) / ((half & 1) === 0 ? 1 : 2);
  }

  function pixelOperation(command, pMode1) {
    const half = pixcHalf(command, pMode1);
    const firstSource = (half >>> 15) & 1;
    const secondSource = (half >>> 6) & 3;
    const useAv = (command.ccbFlags & CCB_USEAV) !== 0;
    const invertedSecondSource = useAv && (half & 2) !== 0;

    if ((command.ccbFlags & CCB_PXOR) !== 0) return "replace";
    if (invertedSecondSource && secondSource === 2) {
      return firstSource === 0 ? "subtract" : "reverseSubtract";
    }
    if (firstSource !== 0 && secondSource === 3) return "diminish";
    if (secondSource === 2) return useAv ? "mix" : "add";
    return "replace";
  }

  function configureBlend(operation, destinationScale = 1) {
    gl.disable(gl.BLEND);
    gl.blendEquation(gl.FUNC_ADD);
    if (operation === "replace") return;

    gl.enable(gl.BLEND);
    if (operation === "add") {
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE);
    } else if (operation === "mix") {
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    } else if (operation === "diminish") {
      gl.blendColor(0, 0, 0, destinationScale);
      gl.blendFunc(gl.ONE, gl.CONSTANT_ALPHA);
    } else {
      gl.blendFunc(gl.ONE, gl.ONE);
      gl.blendEquation(operation === "subtract" ?
                       gl.FUNC_SUBTRACT : gl.FUNC_REVERSE_SUBTRACT);
    }
  }

  const COMMAND_VERTEX_FLOATS = 19;
  const COMMAND_VERTEX_BYTES = COMMAND_VERTEX_FLOATS * Float32Array.BYTES_PER_ELEMENT;

  function renderEntry(command, image) {
    const geometry = fallbackGeometry(command);
    let hull = convexHull(command);
    const minX = Math.max(0, Math.floor(Math.min(...command.x)));
    const maxX = Math.min(canvas.width, Math.ceil(Math.max(...command.x)));
    const minY = Math.max(0, Math.floor(Math.min(...command.y)));
    const maxY = Math.min(canvas.height, Math.ceil(Math.max(...command.y)));
    if (hull === null || minX >= maxX || minY >= maxY) return null;
    const expandCoverage = !geometry.requiresFallback &&
      !isCollapsedQuadTriangle(command);
    if (expandCoverage) {
      hull = expandedConvexHull(hull, 0.5);
    }

    const operation = pixelOperation(command, false);
    const useAv = (command.ccbFlags & CCB_USEAV) !== 0;
    const pMode0Scale = sourceScale(pixcHalf(command, false), useAv);
    const pMode1Scale = sourceScale(pixcHalf(command, true), useAv);
    const scales = (scale) => {
      if (operation === "replace") return [scale, 1];
      if (operation === "add" || operation === "mix") return [1, scale];
      return [1, 1];
    };
    const firstScales = scales(pMode0Scale);
    const secondScales = scales(pMode1Scale);

    return {
      command,
      image: image.texture === undefined ?
        { texture: image, u: 0, v: 0, width: 1, height: 1 } : image,
      geometry,
      hull,
      minX,
      maxX,
      minY,
      maxY,
      operation,
      destinationScale: operation === "diminish" ? pMode0Scale : 1,
      edgeCoverage: expandCoverage ? 1 : 0,
      shade: [firstScales[0], secondScales[0]],
      opacity: [firstScales[1], secondScales[1]]
    };
  }

  function appendCommandVertex(vertices, point, entry) {
    const { command, image, shade, opacity } = entry;
    vertices.push(
      point.x, point.y,
      command.x[0], command.y[0], command.x[1], command.y[1],
      command.x[2], command.y[2], command.x[3], command.y[3],
      image.u, image.v, image.width, image.height,
      shade[0], shade[1], opacity[0], opacity[1], entry.edgeCoverage
    );
  }

  function appendTriangles(vertices, entry) {
    for (let index = 1; index + 1 < entry.hull.length; index += 1) {
      appendCommandVertex(vertices, entry.hull[0], entry);
      appendCommandVertex(vertices, entry.hull[index], entry);
      appendCommandVertex(vertices, entry.hull[index + 1], entry);
    }
  }

  function configureCommandAttributes() {
    const attributes = [
      [position, 0], [commandP0, 2], [commandP1, 4], [commandP2, 6],
      [commandP3, 8], [atlasOrigin, 10], [atlasScale, 12],
      [commandShade, 14], [commandOpacity, 16]
    ];
    for (const [attribute, offset] of attributes) {
      gl.enableVertexAttribArray(attribute);
      gl.vertexAttribPointer(attribute, 2, gl.FLOAT, false,
                             COMMAND_VERTEX_BYTES,
                             offset * Float32Array.BYTES_PER_ELEMENT);
    }
    gl.enableVertexAttribArray(commandEdgeCoverage);
    gl.vertexAttribPointer(commandEdgeCoverage, 1, gl.FLOAT, false,
                           COMMAND_VERTEX_BYTES,
                           18 * Float32Array.BYTES_PER_ELEMENT);
  }

  function drawBatch(entries) {
    if (entries.length === 0) return;

    const fillVertices = [];
    let minX = displayWidth;
    let maxX = 0;
    let minY = displayHeight;
    let maxY = 0;
    for (const entry of entries) {
      appendTriangles(fillVertices, entry);
      minX = Math.min(minX, entry.minX);
      maxX = Math.max(maxX, entry.maxX);
      minY = Math.min(minY, entry.minY);
      maxY = Math.max(maxY, entry.maxY);
    }
    if (fillVertices.length === 0) return;

    const first = entries[0];
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, first.image.texture);
    gl.useProgram(program);
    configureBlend(first.operation, first.destinationScale);
    gl.scissor(minX, canvas.height - maxY, maxX - minX, maxY - minY);
    gl.bindBuffer(gl.ARRAY_BUFFER, vertices);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(fillVertices), gl.DYNAMIC_DRAW);
    configureCommandAttributes();
    gl.drawArrays(gl.TRIANGLES, 0, fillVertices.length / COMMAND_VERTEX_FLOATS);
  }

  function draw(command, image) {
    const entry = renderEntry(command, image);
    if (entry !== null) drawBatch([entry]);
  }

  function canAppendToBatch(batch, entry) {
    if (batch.length === 0) return true;
    const previous = batch[0];

    return previous.image.texture === entry.image.texture &&
      previous.operation === entry.operation &&
      previous.destinationScale === entry.destinationScale;
  }

  function drawText(command, text) {
    if (textFont === undefined || text === "" || command.width === 0) return;

    const vertices = [];
    let penX = command.x[0];
    let penY = command.y[0];
    let charactersOnLine = 0;
    for (const character of text) {
      const code = character.charCodeAt(0);
      if (code === 10) {
        penX = command.x[0];
        penY += textFont.charHeight + textFont.leading;
        charactersOnLine = 0;
        continue;
      }

      if (charactersOnLine > 0) {
        penX += textFont.charExtra;
      }
      if (code >= textFont.firstChar && code <= textFont.lastChar) {
        const index = code - textFont.firstChar;
        const width = textFont.glyphWidths[index];
        const left = penX;
        const right = left + width;
        const top = penY;
        const bottom = top + textFont.charHeight;
        const u0 = ((index % 16) * textFont.charWidth) / textFont.atlasWidth;
        const u1 = (width + (index % 16) * textFont.charWidth) /
                   textFont.atlasWidth;
        const v0 = (Math.floor(index / 16) * textFont.charHeight) /
                   textFont.atlasHeight;
        const v1 = (textFont.charHeight + Math.floor(index / 16) *
                    textFont.charHeight) / textFont.atlasHeight;

        vertices.push(
          left, top, u0, v0,
          right, top, u1, v0,
          right, bottom, u1, v1,
          left, top, u0, v0,
          right, bottom, u1, v1,
          left, bottom, u0, v1
        );
        penX = right;
      }
      charactersOnLine += 1;
    }
    if (vertices.length === 0) return;

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, textFont.texture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.enable(gl.BLEND);
    gl.blendEquation(gl.FUNC_ADD);
    gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
    gl.useProgram(textProgram);
    gl.bindBuffer(gl.ARRAY_BUFFER, textVertices);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(vertices), gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(textPosition);
    gl.vertexAttribPointer(textPosition, 2, gl.FLOAT, false,
                           4 * Float32Array.BYTES_PER_ELEMENT, 0);
    gl.enableVertexAttribArray(textTextureCoordinate);
    gl.vertexAttribPointer(textTextureCoordinate, 2, gl.FLOAT, false,
                           4 * Float32Array.BYTES_PER_ELEMENT,
                           2 * Float32Array.BYTES_PER_ELEMENT);
    gl.uniform3f(textForeground,
                 ((command.palette >>> 10) & 31) / 31,
                 ((command.palette >>> 5) & 31) / 31,
                 (command.palette & 31) / 31);
    gl.uniform3f(textOutline, 0, 0, 1 / 31);
    const left = Math.max(0, command.x[0]);
    const right = Math.min(canvas.width, command.x[0] + command.width);
    if (left >= right) return;

    // PRE1 can reveal a TextCel progressively from its left edge.
    gl.scissor(left, 0, right - left, canvas.height);
    gl.drawArrays(gl.TRIANGLES, 0, vertices.length / 4);
  }

  function submit(command, memory) {
    if ((command.encoding & 0xff) === TEXT_ENCODING) {
      drawText(command, textForCommand(command, memory));
      return;
    }
    const texture = textureForCommand(command, memory);
    if (texture !== null) {
      draw(command, texture);
    }
  }

  return {
    setTextFont(memory, source, size) {
      const nextFont = createTextFontAtlas(gl, memory, source, size);

      if (nextFont === null) {
        throw new Error("Unable to decode the 3DO Message font");
      }
      if (textFont !== undefined) {
        gl.deleteTexture(textFont.texture);
      }
      textFont = nextFont;
    },
    copyVram(bank, memory, source) {
      const pixels = new Uint8Array(displayWidth * displayHeight * 4);
      for (let y = 0; y < displayHeight; y += 1) {
        for (let x = 0; x < displayWidth; x += 1) {
          const destination = (y * displayWidth + x) * 4;
          const offset = source + (((y >>> 1) * displayWidth + x) * 4) +
                         ((y & 1) * 2);
          const value = (memory[offset] << 8) | memory[offset + 1];
          pixels[destination] = ((value >>> 10) & 31) * 255 / 31;
          pixels[destination + 1] = ((value >>> 5) & 31) * 255 / 31;
          pixels[destination + 2] = (value & 31) * 255 / 31;
          pixels[destination + 3] = 255;
        }
      }

      const texture = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, displayWidth, displayHeight,
                    0, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      clearScreenBank(bank);
      draw({
        x: [0, displayWidth, displayWidth, 0],
        y: [0, 0, displayHeight, displayHeight],
        shade: 7,
        blend: 0,
        encoding: 0,
        pixc: 0x1f001f00,
        ccbFlags: 0
      }, texture);
      gl.deleteTexture(texture);
    },
    clearBank(bank, value) {
      clearScreenBank(bank, ((value >>> 10) & 31) / 31,
                      ((value >>> 5) & 31) / 31, (value & 31) / 31);
    },
    fillRect(bank, colour, left, top, right, bottom) {
      const clippedLeft = Math.max(0, Math.min(displayWidth, left));
      const clippedRight = Math.max(0, Math.min(displayWidth, right));
      const clippedTop = Math.max(0, Math.min(displayHeight, top));
      const clippedBottom = Math.max(0, Math.min(displayHeight, bottom));
      if (clippedLeft >= clippedRight || clippedTop >= clippedBottom) return;

      bindScreenBank(bank);
      gl.scissor(clippedLeft, displayHeight - clippedBottom,
                 clippedRight - clippedLeft, clippedBottom - clippedTop);
      gl.clearColor(((colour >>> 10) & 31) / 31,
                    ((colour >>> 5) & 31) / 31, (colour & 31) / 31, 1);
      gl.clear(gl.COLOR_BUFFER_BIT);
      gl.scissor(0, 0, displayWidth, displayHeight);
    },
    queueScreenCel(target, source, x, y, hdx, vdy, pixc, ccbFlags) {
      const width = displayWidth * hdx / 1048576;
      const height = displayHeight * vdy / 65536;
      if (width <= 0 || height <= 0) return;
      screenOperations.push({
        target: normaliseBank(target),
        source: normaliseBank(source),
        x: x / 65536,
        y: y / 65536,
        width,
        height,
        pixc,
        ccbFlags
      });
    },
    setFade(opacity) {
      fade = Math.max(0, Math.min(1, opacity));
      drawBankToCanvas(displayedBank, fade);
    },
    startCinematic(video) {
      cinematicVideo = video;
      drawBankToCanvas(displayedBank, fade);
      drawCinematicFrame();
      scheduleCinematicFrame();
    },
    stopCinematic() {
      if (cinematicVideo !== undefined &&
          typeof cinematicVideo.cancelVideoFrameCallback === "function" &&
          cinematicFrameCallback !== undefined) {
        cinematicVideo.cancelVideoFrameCallback(cinematicFrameCallback);
      } else if (cinematicFrameCallback !== undefined) {
        cancelAnimationFrame(cinematicFrameCallback);
      }
      cinematicFrameCallback = undefined;
      cinematicVideo = undefined;
      drawBankToCanvas(displayedBank, fade);
    },
    submit,
    submitWasm(memory, commandAddress, commandCount) {
      let batch = [];
      const flush = () => {
        drawBatch(batch);
        batch = [];
      };
      for (let index = 0; index < commandCount; index += 1) {
        const command = commandFromWasm(
          memory, commandAddress + index * COMMAND_BYTES
        );
        if ((command.encoding & 0xff) === TEXT_ENCODING) {
          flush();
          drawText(command, textForCommand(command, memory));
          continue;
        }
        const image = textureForCommand(command, memory);
        if (image === null) continue;
        const entry = renderEntry(command, image);
        if (entry === null) continue;
        if (!canAppendToBatch(batch, entry)) flush();
        batch.push(entry);
      }
      flush();
    },
    present(memory, commandAddress, commandCount, bank) {
      bindScreenBank(bank);
      this.submitWasm(memory, commandAddress, commandCount);
      applyScreenOperations();
      displayedBank = normaliseBank(bank);
      drawBankToCanvas(displayedBank, fade);
    },
  };
}
