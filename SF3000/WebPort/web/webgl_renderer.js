const COMMAND_WORDS = 17;
const COMMAND_BYTES = COMMAND_WORDS * Uint32Array.BYTES_PER_ELEMENT;
const WORLD_COMMAND_WORDS = 29;
const WORLD_COMMAND_BYTES = WORLD_COMMAND_WORDS * Uint32Array.BYTES_PER_ELEMENT;
const TEXT_ENCODING = 10;
const SKY_ENCODING = 9;
const SKY_GRADIENT_ENCODING = 11;
const WORLD_ENCODING = 0x10000000;
const WORLD_BACKGROUND_ENCODING = 0x08000000;
const WORLD_BILLBOARD_ENCODING = 0x04000000;
const WORLD_PARTICLE_ENCODING = 0x02000000;
const WORLD_SKY_ENCODING = 0x01000000;
const TARGET_OVERLAY_ENCODING = 0x00800000;
const WORLD_MODEL_ENCODING = 0x00400000;
const PARTICLE_ATLAS_SLOT_SHIFT = 16;
const PARTICLE_ATLAS_SLOT_MASK = 0xff;
const PARTICLE_ATLAS_TRANSFORM_SHIFT = 24;
const PARTICLE_ATLAS_TRANSFORM_MASK = 0x07;
const PARTICLE_ATLAS_SLOT_COUNT = 28;
const PARTICLE_ATLAS_CELL_SIZE = 64;
const PARTICLE_ATLAS_CELL_BORDER = 1;
const TERRAIN_ENCODING = 0x40000000;
const CCB_PXOR = 0x00000800;
const CCB_USEAV = 0x00002000;
const LOGICAL_DISPLAY_WIDTH = 320;
const LOGICAL_DISPLAY_HEIGHT = 240;
const LEGACY_PROJECTION_FOCAL_LENGTH = 192;
const LEGACY_ASPECT_RATIO = LOGICAL_DISPLAY_WIDTH / LOGICAL_DISPLAY_HEIGHT;
const TERRAIN_HEIGHT_MAP_DIMENSION = 256;
const TERRAIN_GRID_DIMENSION = TERRAIN_HEIGHT_MAP_DIMENSION + 1;
const TERRAIN_TILE_COUNT = 256;
const TERRAIN_FRAME_WORDS = 20;
const TERRAIN_GRID_COPIES = 3;

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
uniform vec2 renderScale;
uniform vec2 renderOrigin;
uniform float usesTextureCoverage;
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
  vec2 point = vec2((gl_FragCoord.x - renderOrigin.x) / renderScale.x,
                    screenHeight -
                    (gl_FragCoord.y - renderOrigin.y) / renderScale.y) -
    fragmentP0;
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
  if (usesTextureCoverage != 0.0) {
    color = vec4(texel.rgb / texel.a * fragmentShade.x,
                 texel.a * fragmentOpacity.x);
    return;
  }
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
uniform float usesBakedWeights;
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
  if (usesBakedWeights != 0.0) {
    color = vec4(foreground * glyph.r + outline * glyph.g, glyph.a);
    return;
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

const worldVertexShaderSource = `#version 300 es
in vec3 viewPosition;
in vec2 textureCoordinate;
in vec2 sourceShade;
in vec2 sourceOpacity;
in float textureAlpha;
uniform float aspectRatio;
out vec2 fragmentTextureCoordinate;
out vec2 fragmentSourceShade;
out vec2 fragmentSourceOpacity;
out float fragmentTextureAlpha;
void main() {
  const float nearPlane = 1280.0;
  const float farPlane = 4194304.0;
  const float legacyAspectRatio = 4.0 / 3.0;
  const float legacyHorizontalScale = 1.2;
  const float legacyVerticalScale = 1.6;
  float depth = viewPosition.y;
  float horizontalScale = aspectRatio >= legacyAspectRatio ?
    legacyVerticalScale / aspectRatio : legacyHorizontalScale;
  float verticalScale = aspectRatio >= legacyAspectRatio ?
    legacyVerticalScale : legacyHorizontalScale * aspectRatio;
  float clipZ = depth * (farPlane + nearPlane) / (farPlane - nearPlane) -
    2.0 * farPlane * nearPlane / (farPlane - nearPlane);

  gl_Position = vec4(viewPosition.x * horizontalScale,
                     -viewPosition.z * verticalScale,
                     clipZ, depth);
  fragmentTextureCoordinate = textureCoordinate;
  fragmentSourceShade = sourceShade;
  fragmentSourceOpacity = sourceOpacity;
  fragmentTextureAlpha = textureAlpha;
}`;

const worldFragmentShaderSource = `#version 300 es
precision highp float;
uniform sampler2D texture0;
in vec2 fragmentTextureCoordinate;
in vec2 fragmentSourceShade;
in vec2 fragmentSourceOpacity;
in float fragmentTextureAlpha;
out vec4 color;
void main() {
  vec4 texel = texture(texture0, fragmentTextureCoordinate);
  if (texel.a == 0.0) discard;
  if (fragmentTextureAlpha > 0.5) {
    color = vec4(texel.rgb * fragmentSourceShade.x,
                 texel.a * fragmentSourceOpacity.x);
    return;
  }
  bool pMode1 = texel.a < 0.75;
  color = vec4(texel.rgb *
                 (pMode1 ? fragmentSourceShade.y : fragmentSourceShade.x),
               pMode1 ? fragmentSourceOpacity.y : fragmentSourceOpacity.x);
}`;

const terrainVertexShaderSource = `#version 300 es
precision highp float;
precision highp int;
precision highp isampler2D;
precision highp usampler2D;
in vec2 terrainCoordinate;
uniform usampler2D terrainState;
uniform isampler2D terrainHeightOffsets;
uniform ivec2 terrainMapStart;
uniform vec3 terrainOrigin;
uniform vec3 terrainHorizontal;
uniform vec3 terrainVertical;
uniform int terrainWavePhase;
uniform float aspectRatio;
out vec2 fragmentTerrainCoordinate;
out float fragmentTerrainLight;
out float fragmentFlatWaterLight;

float waveHeightAt(ivec2 coordinate) {
  int phase = (terrainWavePhase + coordinate.y * 128 +
    coordinate.x * 64) & 1023;
  return cos(float(phase) * 6.28318530718 / 1024.0) * 8.0 + 8.0;
}

float terrainVisualHeightAt(ivec2 coordinate) {
  int rawHeight = int(texelFetch(
    terrainState, coordinate & ivec2(255), 0
  ).r);
  return rawHeight <= 16 ? waveHeightAt(coordinate) : float(rawHeight);
}

vec3 terrainHeightOffsetAt(float height) {
  float clampedHeight = clamp(height, 0.0, 255.0);
  int lowerHeight = int(floor(clampedHeight));
  int upperHeight = min(lowerHeight + 1, 255);
  vec3 lowerOffset = vec3(texelFetch(
    terrainHeightOffsets, ivec2(lowerHeight, 0), 0
  ).xyz);
  vec3 upperOffset = vec3(texelFetch(
    terrainHeightOffsets, ivec2(upperHeight, 0), 0
  ).xyz);
  return mix(lowerOffset, upperOffset, fract(clampedHeight)) / 64.0;
}

float terrainVertexLight(ivec2 coordinate, bool water) {
  float height = terrainVisualHeightAt(coordinate);
  float northHeight = terrainVisualHeightAt(coordinate + ivec2(0, -1));
  float eastHeight = terrainVisualHeightAt(coordinate + ivec2(1, 0));
  float southHeight = terrainVisualHeightAt(coordinate + ivec2(0, 1));
  float westHeight = terrainVisualHeightAt(coordinate + ivec2(-1, 0));
  vec3 northOffset = terrainHeightOffsetAt(northHeight);
  vec3 eastOffset = terrainHeightOffsetAt(eastHeight);
  vec3 southOffset = terrainHeightOffsetAt(southHeight);
  vec3 westOffset = terrainHeightOffsetAt(westHeight);
  vec3 flatNormal = normalize(cross(terrainHorizontal, terrainVertical));
  vec3 surfaceNormal = normalize(cross(
    terrainHorizontal * 2.0 - eastOffset + westOffset,
    terrainVertical * 2.0 - southOffset + northOffset
  ));
  if (dot(surfaceNormal, flatNormal) < 0.0) surfaceNormal = -surfaceNormal;
  vec3 lightDirection = normalize(flatNormal +
    normalize(terrainHorizontal) * 0.45 +
    normalize(terrainVertical) * 0.35);
  float directionalLight = clamp(dot(surfaceNormal, lightDirection), 0.0, 1.0);
  float flatDirectionalLight = dot(flatNormal, lightDirection);
  float elevationRelief = -height;
  float centredSlope = westHeight - eastHeight + northHeight - southHeight;
  if (water) {
    elevationRelief *= 0.5;
    centredSlope *= 0.5;
  }
  float ambientLight = mix(0.25, 1.5,
    clamp((elevationRelief * 0.5 + 16.0) / 31.0, 0.0, 1.0));
  return clamp(ambientLight + centredSlope * 0.03 +
    (directionalLight - flatDirectionalLight) * 0.5, 0.25, 1.5);
}

float terrainFlatLightFromHeight(float height, float eastHeight,
                                 float southHeight, bool water) {
  float relief = height - eastHeight - southHeight;
  if (water) relief *= 0.5;
  float shade = relief * 0.5 + 16.0;
  return mix(0.25, 1.5, clamp(shade / 31.0, 0.0, 1.0));
}

float terrainFlatWaterLight(ivec2 coordinate) {
  float height = terrainVisualHeightAt(coordinate);
  float eastHeight = terrainVisualHeightAt(coordinate + ivec2(1, 0));
  float southHeight = terrainVisualHeightAt(coordinate + ivec2(0, 1));
  vec3 heightOffset = terrainHeightOffsetAt(height);
  vec3 eastOffset = terrainHeightOffsetAt(eastHeight);
  vec3 southOffset = terrainHeightOffsetAt(southHeight);
  vec3 flatNormal = normalize(cross(terrainHorizontal, terrainVertical));
  vec3 surfaceNormal = normalize(cross(
    terrainHorizontal - eastOffset + heightOffset,
    terrainVertical - southOffset + heightOffset
  ));
  if (dot(surfaceNormal, flatNormal) < 0.0) surfaceNormal = -surfaceNormal;
  vec3 lightDirection = normalize(flatNormal +
    normalize(terrainHorizontal) * 0.45 +
    normalize(terrainVertical) * 0.35);
  float directionalLight = clamp(dot(surfaceNormal, lightDirection), 0.0, 1.0);
  float legacyLight = terrainFlatLightFromHeight(
    height, eastHeight, southHeight, true
  );
  return clamp(legacyLight * mix(0.7, 1.3, directionalLight), 0.25, 1.5);
}

void main() {
  const float nearPlane = 1280.0;
  const float farPlane = 4194304.0;
  const float legacyAspectRatio = 4.0 / 3.0;
  const float legacyHorizontalScale = 1.2;
  const float legacyVerticalScale = 1.6;
  int copyX = gl_InstanceID % ${TERRAIN_GRID_COPIES} -
    ${(TERRAIN_GRID_COPIES - 1) / 2};
  int copyY = gl_InstanceID / ${TERRAIN_GRID_COPIES} -
    ${(TERRAIN_GRID_COPIES - 1) / 2};
  vec2 worldCoordinate = terrainCoordinate + vec2(copyX, copyY) *
    float(${TERRAIN_HEIGHT_MAP_DIMENSION});
  ivec2 mapCoordinate = (terrainMapStart + ivec2(worldCoordinate)) & 255;
  int rawHeight = int(texelFetch(terrainState, mapCoordinate, 0).r);
  float visualHeight = terrainVisualHeightAt(mapCoordinate);
  vec3 heightOffset = terrainHeightOffsetAt(visualHeight);
  fragmentTerrainLight = terrainVertexLight(mapCoordinate, rawHeight <= 16);
  fragmentFlatWaterLight = rawHeight <= 16 ?
    terrainFlatWaterLight(mapCoordinate) : 1.0;
  vec3 viewPosition = terrainOrigin +
    worldCoordinate.x * terrainHorizontal +
    worldCoordinate.y * terrainVertical - heightOffset;
  float horizontalScale = aspectRatio >= legacyAspectRatio ?
    legacyVerticalScale / aspectRatio : legacyHorizontalScale;
  float verticalScale = aspectRatio >= legacyAspectRatio ?
    legacyVerticalScale : legacyHorizontalScale * aspectRatio;
  float depth = viewPosition.y;
  float clipZ = depth * (farPlane + nearPlane) / (farPlane - nearPlane) -
    2.0 * farPlane * nearPlane / (farPlane - nearPlane);

  gl_Position = vec4(viewPosition.x * horizontalScale,
                     -viewPosition.z * verticalScale,
                     clipZ, depth);
  fragmentTerrainCoordinate = worldCoordinate + vec2(terrainMapStart);
}`;

const terrainFragmentShaderSource = `#version 300 es
precision highp float;
precision highp int;
precision highp usampler2D;
precision highp sampler2DArray;
uniform usampler2D terrainState;
uniform sampler2DArray terrainTileTextures;
uniform sampler2D terrainSharedTextureAtlas;
uniform sampler2D terrainWorldTextureAtlas;
uniform usampler2D terrainMaterialSlots;
uniform int terrainSharedAtlasColumns;
uniform int terrainWorldAtlasColumns;
uniform int useTerrainTextureAtlas;
uniform int terrainWavePhase;
uniform int terrainShadingMode;
in vec2 fragmentTerrainCoordinate;
in float fragmentTerrainLight;
in float fragmentFlatWaterLight;
out vec4 color;

int terrainRawHeightAt(ivec2 coordinate) {
  return int(texelFetch(terrainState, coordinate & ivec2(255), 0).r);
}

float waveHeightAt(ivec2 coordinate) {
  int phase = (terrainWavePhase + coordinate.y * 128 +
    coordinate.x * 64) & 1023;
  return cos(float(phase) * 6.28318530718 / 1024.0) * 8.0 + 8.0;
}

float terrainVisualHeightAt(ivec2 coordinate) {
  int rawHeight = terrainRawHeightAt(coordinate);
  return rawHeight <= 16 ? waveHeightAt(coordinate) : float(rawHeight);
}

float terrainFlatLight(ivec2 mapCoordinate) {
  float height = terrainVisualHeightAt(mapCoordinate);
  float eastHeight = terrainVisualHeightAt(mapCoordinate + ivec2(1, 0));
  float southHeight = terrainVisualHeightAt(mapCoordinate + ivec2(0, 1));
  float relief = height - eastHeight - southHeight;
  const float ambientShade = 16.0;
  const float darkestScale = 0.25;
  const float brightestScale = 1.5;
  float shade = relief * 0.5 + ambientShade;
  float legacyLight = mix(darkestScale, brightestScale,
                          clamp(shade / 31.0, 0.0, 1.0));
  float centredSlope = terrainVisualHeightAt(mapCoordinate + ivec2(-1, 0)) -
    eastHeight +
    terrainVisualHeightAt(mapCoordinate + ivec2(0, -1)) -
    southHeight;
  return clamp(legacyLight + centredSlope * 0.03,
               darkestScale, brightestScale);
}

void main() {
  ivec2 mapCoordinate = ivec2(floor(fragmentTerrainCoordinate)) & 255;
  int tile = int(texelFetch(terrainState, mapCoordinate, 0).g);
  vec2 tileCoordinate = fract(fragmentTerrainCoordinate);
  vec4 texel;
  if (useTerrainTextureAtlas != 0) {
    uvec2 material = texelFetch(terrainMaterialSlots, ivec2(tile, 0), 0).rg;
    int materialSlot = int(material.r);
    int columns = material.g == 0u ? terrainSharedAtlasColumns :
      terrainWorldAtlasColumns;
    int tileX = materialSlot % columns;
    int tileY = materialSlot / columns;
    vec2 pixel = vec2(float(tileX * 16), float(tileY * 16)) +
      vec2(tileCoordinate.x, 1.0 - tileCoordinate.y) * 15.0 + 0.5;
    texel = material.g == 0u ?
      texture(terrainSharedTextureAtlas,
              pixel / vec2(textureSize(terrainSharedTextureAtlas, 0))) :
      texture(terrainWorldTextureAtlas,
              pixel / vec2(textureSize(terrainWorldTextureAtlas, 0)));
  } else {
    texel = texture(terrainTileTextures,
                    vec3(tileCoordinate.x, 1.0 - tileCoordinate.y, float(tile)));
  }
  if (texel.a == 0.0) discard;
  float light = terrainShadingMode == 0 ?
    (terrainRawHeightAt(mapCoordinate) <= 16 ?
      fragmentFlatWaterLight : terrainFlatLight(mapCoordinate)) :
    fragmentTerrainLight;
  color = vec4(texel.rgb * light, 1.0);
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

function createTerrainMesh() {
  const vertices = new Float32Array(TERRAIN_GRID_DIMENSION *
                                    TERRAIN_GRID_DIMENSION * 2);
  const indices = new Uint32Array(TERRAIN_HEIGHT_MAP_DIMENSION *
                                  TERRAIN_HEIGHT_MAP_DIMENSION * 6);
  let vertex = 0;
  let index = 0;

  for (let y = 0; y < TERRAIN_GRID_DIMENSION; y += 1) {
    for (let x = 0; x < TERRAIN_GRID_DIMENSION; x += 1) {
      vertices[vertex++] = x;
      vertices[vertex++] = y;
    }
  }
  for (let y = 0; y < TERRAIN_HEIGHT_MAP_DIMENSION; y += 1) {
    for (let x = 0; x < TERRAIN_HEIGHT_MAP_DIMENSION; x += 1) {
      const topLeft = y * TERRAIN_GRID_DIMENSION + x;
      const topRight = topLeft + 1;
      const bottomLeft = topLeft + TERRAIN_GRID_DIMENSION;
      const bottomRight = bottomLeft + 1;

      indices[index++] = topLeft;
      indices[index++] = bottomLeft;
      indices[index++] = bottomRight;
      indices[index++] = topLeft;
      indices[index++] = bottomRight;
      indices[index++] = topRight;
    }
  }
  return { vertices, indices };
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

function decodeTextFontAtlas(memory, source, size) {
  const headerSize = 84;
  const columns = 16;

  if (size < headerSize || source > memory.length - size) {
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

  return {
    pixels,
    atlasWidth,
    atlasHeight,
    textureScale: 1,
    usesBakedWeights: false,
    charHeight,
    charWidth,
    charExtra,
    leading,
    firstChar,
    lastChar,
    glyphWidths
  };
}

export function decodeMessageFontAtlas(data) {
  const font = decodeTextFontAtlas(data, 0, data.length);
  if (font === null) {
    throw new Error("Unable to decode the 3DO Message font");
  }
  return font;
}

function createTextFontAtlas(gl, memory, source, size) {
  const font = decodeTextFontAtlas(memory, source, size);
  if (font === null) return null;

  const texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, font.atlasWidth, font.atlasHeight,
                0, gl.RGBA, gl.UNSIGNED_BYTE, font.pixels);
  return { texture, ...font };
}

export function createWebGLRenderer(canvas, textureForCommand, textForCommand,
                                    decodeTexture) {
  const gl = canvas.getContext("webgl2", {
    alpha: false,
    antialias: false,
    depth: true
  });
  if (gl === null) {
    throw new Error("WebGL 2 is required");
  }

  const program = createProgram(gl, vertexShaderSource, fragmentShaderSource);
  const textProgram = createProgram(gl, textVertexShaderSource,
                                    textFragmentShaderSource);
  const screenProgram = createProgram(gl, screenVertexShaderSource,
                                      screenFragmentShaderSource);
  const worldProgram = createProgram(gl, worldVertexShaderSource,
                                     worldFragmentShaderSource);
  const terrainProgram = createProgram(gl, terrainVertexShaderSource,
                                       terrainFragmentShaderSource);
  const vertices = gl.createBuffer();
  const textVertices = gl.createBuffer();
  const screenVertices = gl.createBuffer();
  const worldVertices = gl.createBuffer();
  const terrainVertices = gl.createBuffer();
  const terrainIndices = gl.createBuffer();
  const terrainStateTexture = gl.createTexture();
  const terrainTileTextures = gl.createTexture();
  const terrainMaterialSlotsTexture = gl.createTexture();
  const terrainHeightOffsetsTexture = gl.createTexture();
  const particleAtlasTexture = gl.createTexture();
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
  const renderScale = gl.getUniformLocation(program, "renderScale");
  const renderOrigin = gl.getUniformLocation(program, "renderOrigin");
  const usesTextureCoverage = gl.getUniformLocation(program, "usesTextureCoverage");
  const textPosition = gl.getAttribLocation(textProgram, "position");
  const textTextureCoordinate = gl.getAttribLocation(textProgram,
                                                      "textureCoordinate");
  const textForeground = gl.getUniformLocation(textProgram, "foreground");
  const textOutline = gl.getUniformLocation(textProgram, "outline");
  const textUsesBakedWeights = gl.getUniformLocation(textProgram, "usesBakedWeights");
  const screenPosition = gl.getAttribLocation(screenProgram, "position");
  const screenTextureCoordinate = gl.getAttribLocation(
    screenProgram, "textureCoordinate"
  );
  const screenFade = gl.getUniformLocation(screenProgram, "fade");
  const screenOpacity = gl.getUniformLocation(screenProgram, "opacity");
  const screenColourScale = gl.getUniformLocation(screenProgram, "colourScale");
  const worldPosition = gl.getAttribLocation(worldProgram, "viewPosition");
  const worldTextureCoordinate = gl.getAttribLocation(
    worldProgram, "textureCoordinate"
  );
  const worldSourceShade = gl.getAttribLocation(worldProgram, "sourceShade");
  const worldSourceOpacity = gl.getAttribLocation(worldProgram, "sourceOpacity");
  const worldTextureAlpha = gl.getAttribLocation(worldProgram, "textureAlpha");
  const worldAspectRatio = gl.getUniformLocation(worldProgram, "aspectRatio");
  const terrainCoordinate = gl.getAttribLocation(terrainProgram,
                                                  "terrainCoordinate");
  const terrainState = gl.getUniformLocation(terrainProgram, "terrainState");
  const terrainHeightOffsets = gl.getUniformLocation(
    terrainProgram, "terrainHeightOffsets"
  );
  const terrainMapStart = gl.getUniformLocation(terrainProgram,
                                                 "terrainMapStart");
  const terrainOrigin = gl.getUniformLocation(terrainProgram, "terrainOrigin");
  const terrainHorizontal = gl.getUniformLocation(terrainProgram,
                                                  "terrainHorizontal");
  const terrainVertical = gl.getUniformLocation(terrainProgram,
                                                "terrainVertical");
  const terrainWavePhase = gl.getUniformLocation(terrainProgram,
                                                 "terrainWavePhase");
  const terrainShadingMode = gl.getUniformLocation(terrainProgram,
                                                    "terrainShadingMode");
  const terrainAspectRatio = gl.getUniformLocation(terrainProgram,
                                                   "aspectRatio");
  const terrainTileTexturesUniform = gl.getUniformLocation(
    terrainProgram, "terrainTileTextures"
  );
  const terrainSharedTextureAtlas = gl.getUniformLocation(
    terrainProgram, "terrainSharedTextureAtlas"
  );
  const terrainWorldTextureAtlas = gl.getUniformLocation(
    terrainProgram, "terrainWorldTextureAtlas"
  );
  const terrainMaterialSlots = gl.getUniformLocation(
    terrainProgram, "terrainMaterialSlots"
  );
  const terrainSharedAtlasColumns = gl.getUniformLocation(
    terrainProgram, "terrainSharedAtlasColumns"
  );
  const terrainWorldAtlasColumns = gl.getUniformLocation(
    terrainProgram, "terrainWorldAtlasColumns"
  );
  const useTerrainTextureAtlas = gl.getUniformLocation(
    terrainProgram, "useTerrainTextureAtlas"
  );
  let textFont;
  let fade = 0;
  let worldFrameActive = false;
  let backdropTexture;
  let historyTexture;
  let presentationSurface;
  let historyAvailable = false;
  let frameBase;
  let frameClearColour;
  const pendingRectangles = [];
  let pendingTransition;
  const displayWidth = LOGICAL_DISPLAY_WIDTH;
  const displayHeight = LOGICAL_DISPLAY_HEIGHT;
  let renderWidth = canvas.width;
  let renderHeight = canvas.height;
  let canvasWidth = canvas.width;
  let canvasHeight = canvas.height;
  let logicalViewport = { x: 0, y: 0, width: renderWidth, height: renderHeight };
  let terrainStateUploadScratch = new Uint8Array(0);
  let terrainFrame;
  let terrainTileMaterialGeneration = -1;
  let terrainHeightOffsetsSource = 0;
  let terrainStateTextureReady = false;
  let useGouraudTerrainShading = true;
  let useDirectionalFlatModelShading = true;
  let modelLightDirection = [0.45, 0.8, 0.35];
  let terrainTileTexturesAllocated = false;
  let terrainTileTexturesReady = false;
  let bakedTerrainAtlas;
  let terrainAtlasGeneration = "";
  let particleAtlasReady = false;
  let useHighResolutionBillboards = true;
  let lastWorldStats = {
    commandCount: 0,
    drawCalls: 0,
    terrainCommands: 0,
    terrainDrawCalls: 0,
    terrainOnlyDrawCalls: 0,
    mixedTerrainDrawCalls: 0,
    persistentTerrainDrawCalls: 0
  };

  gl.bindTexture(gl.TEXTURE_2D, terrainStateTexture);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

  gl.bindTexture(gl.TEXTURE_2D, terrainHeightOffsetsTexture);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

  const particleAtlasImage = new Image();
  particleAtlasImage.addEventListener("load", () => {
    gl.bindTexture(gl.TEXTURE_2D, particleAtlasTexture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE,
                  particleAtlasImage);
    particleAtlasReady = true;
  });
  particleAtlasImage.src = new URL("./particle-atlas.png", import.meta.url);

  gl.bindTexture(gl.TEXTURE_2D_ARRAY, terrainTileTextures);
  gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

  gl.bindTexture(gl.TEXTURE_2D, terrainMaterialSlotsTexture);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

  const terrainMesh = createTerrainMesh();
  gl.bindBuffer(gl.ARRAY_BUFFER, terrainVertices);
  gl.bufferData(gl.ARRAY_BUFFER, terrainMesh.vertices, gl.STATIC_DRAW);
  gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, terrainIndices);
  gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, terrainMesh.indices, gl.STATIC_DRAW);

  function createPresentationTexture() {
    const texture = gl.createTexture();

    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, renderWidth, renderHeight, 0,
                  gl.RGBA, gl.UNSIGNED_BYTE, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return texture;
  }

  function createPresentationSurface() {
    const texture = createPresentationTexture();
    const framebuffer = gl.createFramebuffer();
    const depthBuffer = gl.createRenderbuffer();

    gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0,
                            gl.TEXTURE_2D, texture, 0);
    gl.bindRenderbuffer(gl.RENDERBUFFER, depthBuffer);
    gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT16,
                           renderWidth, renderHeight);
    gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT,
                               gl.RENDERBUFFER, depthBuffer);
    if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) {
      throw new Error("Unable to create a WebGL presentation surface");
    }
    return { texture, framebuffer, depthBuffer };
  }

  presentationSurface = createPresentationSurface();
  historyTexture = createPresentationTexture();
  const cinematicTexture = gl.createTexture();
  let cinematicVideo;
  let cinematicFrameCallback;

  gl.bindTexture(gl.TEXTURE_2D, cinematicTexture);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
                new Uint8Array([0, 0, 0, 255]));
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

  function updateTerrainState(memory, heights, tiles, x, y, width, height, full,
                              bakedMaps) {
    if (!Number.isInteger(heights) || !Number.isInteger(tiles) ||
        !Number.isInteger(x) ||
        !Number.isInteger(y) || !Number.isInteger(width) ||
        !Number.isInteger(height) || !Number.isInteger(full) ||
        heights <= 0 || tiles <= 0 ||
        x < 0 || y < 0 ||
        width <= 0 || height <= 0 ||
        x + width > TERRAIN_HEIGHT_MAP_DIMENSION ||
        y + height > TERRAIN_HEIGHT_MAP_DIMENSION ||
        heights + TERRAIN_HEIGHT_MAP_DIMENSION ** 2 > memory.length ||
        tiles + TERRAIN_HEIGHT_MAP_DIMENSION ** 2 > memory.length) {
      throw new Error("Invalid terrain-state upload");
    }
    if (bakedMaps !== undefined &&
        (!(bakedMaps.height instanceof Uint8ClampedArray) ||
         !(bakedMaps.tile instanceof Uint8ClampedArray) ||
         bakedMaps.height.length !== TERRAIN_HEIGHT_MAP_DIMENSION ** 2 * 4 ||
         bakedMaps.tile.length !== TERRAIN_HEIGHT_MAP_DIMENSION ** 2 * 4)) {
      throw new Error("Baked terrain maps are invalid");
    }

    const pixelCount = width * height;
    const byteCount = pixelCount * 2;
    if (terrainStateUploadScratch.length < byteCount) {
      terrainStateUploadScratch = new Uint8Array(byteCount);
    }
    for (let row = 0; row < height; row += 1) {
      const sourceOffset =
        (y + row) * TERRAIN_HEIGHT_MAP_DIMENSION + x;
      let destinationOffset = row * width * 2;
      for (let column = 0; column < width; column += 1) {
        const source = sourceOffset + column;
        if (full !== 0 && bakedMaps !== undefined) {
          terrainStateUploadScratch[destinationOffset] = bakedMaps.height[source * 4];
          terrainStateUploadScratch[destinationOffset + 1] = bakedMaps.tile[source * 4];
        } else {
          terrainStateUploadScratch[destinationOffset] = memory[heights + source];
          terrainStateUploadScratch[destinationOffset + 1] = memory[tiles + source];
        }
        destinationOffset += 2;
      }
    }

    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, terrainStateTexture);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    if (!terrainStateTextureReady) {
      if (full === 0 || width !== TERRAIN_HEIGHT_MAP_DIMENSION ||
          height !== TERRAIN_HEIGHT_MAP_DIMENSION) {
        throw new Error("Initial terrain-state upload must cover the map");
      }
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RG8UI,
                    TERRAIN_HEIGHT_MAP_DIMENSION,
                    TERRAIN_HEIGHT_MAP_DIMENSION, 0, gl.RG_INTEGER,
                    gl.UNSIGNED_BYTE, terrainStateUploadScratch.subarray(
                      0, byteCount
                    ));
      terrainStateTextureReady = true;
    } else {
      gl.texSubImage2D(gl.TEXTURE_2D, 0, x, y, width, height, gl.RG_INTEGER,
                       gl.UNSIGNED_BYTE,
                       terrainStateUploadScratch.subarray(0, byteCount));
    }
  }

  function terrainFrameFromWasm(memory, address) {
    if (!Number.isInteger(address) || address === 0 ||
        address + TERRAIN_FRAME_WORDS * Uint32Array.BYTES_PER_ELEMENT >
          memory.length) {
      return undefined;
    }
    const words = new Uint32Array(memory.buffer, address, TERRAIN_FRAME_WORDS);
    const signed = new Int32Array(memory.buffer, address, TERRAIN_FRAME_WORDS);
    return {
      heights: words[0],
      tiles: words[1],
      heightOffsets: words[2],
      tileMaterials: words[3],
      tileMaterialValid: words[4],
      tileMaterialDirty: words[5],
      tileMaterialGeneration: words[6],
      mapX: signed[7],
      mapY: signed[8],
      origin: [signed[9], signed[10], signed[11]],
      horizontal: [signed[12], signed[13], signed[14]],
      vertical: [signed[15], signed[16], signed[17]],
      wavePhase: signed[18],
      active: signed[19] !== 0
    };
  }

  function updateTerrainFrame(memory, address, nextBakedTerrainAtlas) {
    const frame = terrainFrameFromWasm(memory, address);
    if (frame === undefined || !frame.active) {
      terrainFrame = undefined;
      return;
    }
    if (frame.heights === 0 || frame.tiles === 0 || frame.heightOffsets === 0 ||
        frame.tileMaterials === 0 || frame.tileMaterialValid === 0 ||
        frame.tileMaterialDirty === 0 ||
        frame.heights + TERRAIN_HEIGHT_MAP_DIMENSION ** 2 > memory.length ||
        frame.tiles + TERRAIN_HEIGHT_MAP_DIMENSION ** 2 > memory.length ||
        frame.heightOffsets + TERRAIN_TILE_COUNT * 4 *
          Int32Array.BYTES_PER_ELEMENT > memory.length ||
        frame.tileMaterials + TERRAIN_TILE_COUNT * COMMAND_BYTES >
          memory.length ||
        frame.tileMaterialValid + TERRAIN_TILE_COUNT > memory.length ||
        frame.tileMaterialDirty + TERRAIN_TILE_COUNT > memory.length) {
      throw new Error("Invalid persistent terrain frame");
    }

    if (nextBakedTerrainAtlas !== undefined &&
        (!Array.isArray(nextBakedTerrainAtlas.materials) ||
         (nextBakedTerrainAtlas.shared === undefined &&
          nextBakedTerrainAtlas.world === undefined))) {
      throw new Error("Baked terrain atlas is invalid");
    }
    bakedTerrainAtlas = nextBakedTerrainAtlas;
    if (bakedTerrainAtlas === undefined && (!terrainTileTexturesAllocated ||
        frame.tileMaterialGeneration !== terrainTileMaterialGeneration)) {
      const materialValid = new Uint8Array(memory.buffer,
                                           frame.tileMaterialValid,
                                           TERRAIN_TILE_COUNT);
      const materialDirty = new Uint8Array(memory.buffer,
                                           frame.tileMaterialDirty,
                                           TERRAIN_TILE_COUNT);
      gl.activeTexture(gl.TEXTURE3);
      gl.bindTexture(gl.TEXTURE_2D_ARRAY, terrainTileTextures);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
      if (!terrainTileTexturesAllocated) {
        gl.texImage3D(gl.TEXTURE_2D_ARRAY, 0, gl.RGBA, 16, 16,
                      TERRAIN_TILE_COUNT, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        terrainTileTexturesAllocated = true;
      }
      for (let tile = 0; tile < TERRAIN_TILE_COUNT; tile += 1) {
        if (materialValid[tile] === 0 ||
            (materialDirty[tile] === 0 && terrainTileTexturesReady)) continue;
        const material = commandFromWasm(
          memory, frame.tileMaterials + tile * COMMAND_BYTES
        );
        const decoded = decodeTexture(material, memory);
        if (decoded === null || decoded.width !== 16 || decoded.height !== 16) {
          throw new Error(`Invalid terrain tile material ${tile}`);
        }
        gl.texSubImage3D(gl.TEXTURE_2D_ARRAY, 0, 0, 0, tile, 16, 16, 1,
                         gl.RGBA, gl.UNSIGNED_BYTE, decoded.pixels);
      }
      terrainTileMaterialGeneration = frame.tileMaterialGeneration;
      terrainTileTexturesReady = terrainTileTexturesAllocated;
    }

    gl.activeTexture(gl.TEXTURE4);
    gl.bindTexture(gl.TEXTURE_2D, terrainHeightOffsetsTexture);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
    const heightOffsets = new Int32Array(memory.buffer, frame.heightOffsets,
                                         TERRAIN_TILE_COUNT * 4);
    if (frame.heightOffsets !== terrainHeightOffsetsSource) {
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32I, TERRAIN_TILE_COUNT, 1, 0,
                    gl.RGBA_INTEGER, gl.INT, heightOffsets);
      terrainHeightOffsetsSource = frame.heightOffsets;
    } else {
      gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, TERRAIN_TILE_COUNT, 1,
                       gl.RGBA_INTEGER, gl.INT, heightOffsets);
    }
    terrainFrame = frame;
  }

  function uploadTerrainMaterialSlots(atlas) {
    if (atlas === undefined || terrainAtlasGeneration === atlas.generation) return;
    const slots = new Uint8Array(TERRAIN_TILE_COUNT * 2);
    const fallback = atlas.materials.find((material) => material !== null &&
      material !== undefined);

    if (fallback === undefined) {
      throw new Error("Baked terrain atlas has no material entries");
    }

    for (let tile = 0; tile < TERRAIN_TILE_COUNT; tile += 1) {
      const material = atlas.materials[tile] ?? fallback;
      if (
          !Number.isInteger(material.slot) || material.slot < 0 ||
          material.slot > 255 ||
          (material.scope !== "shared" && material.scope !== "world")) {
        throw new Error(`Baked terrain material ${tile} is invalid`);
      }
      slots[tile * 2] = material.slot;
      slots[tile * 2 + 1] = material.scope === "shared" ? 0 : 1;
    }
    gl.activeTexture(gl.TEXTURE7);
    gl.bindTexture(gl.TEXTURE_2D, terrainMaterialSlotsTexture);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RG8UI, TERRAIN_TILE_COUNT, 1, 0,
                  gl.RG_INTEGER, gl.UNSIGNED_BYTE, slots);
    terrainAtlasGeneration = atlas.generation;
  }

  function resizeRenderTargets() {
    const bounds = canvas.getBoundingClientRect();
    const maximumDimension = Math.min(
      gl.getParameter(gl.MAX_TEXTURE_SIZE),
      gl.getParameter(gl.MAX_RENDERBUFFER_SIZE)
    );
    const maximumViewport = gl.getParameter(gl.MAX_VIEWPORT_DIMS);
    const pixelRatio = globalThis.devicePixelRatio ?? 1;
    const desiredWidth = Math.max(1, Math.round(bounds.width * pixelRatio));
    const desiredHeight = Math.max(1, Math.round(bounds.height * pixelRatio));
    const canvasScale = Math.min(1, maximumViewport[0] / desiredWidth,
                                 maximumViewport[1] / desiredHeight,
                                 maximumDimension / desiredWidth,
                                 maximumDimension / desiredHeight);
    const nextCanvasWidth = Math.max(1, Math.floor(desiredWidth * canvasScale));
    const nextCanvasHeight = Math.max(1, Math.floor(desiredHeight * canvasScale));
    const viewportUnit = Math.max(1, Math.floor(Math.min(
      nextCanvasWidth / 4,
      nextCanvasHeight / 3,
      maximumDimension / 4,
      maximumDimension / 3
    )));
    const nextRenderWidth = nextCanvasWidth;
    const nextRenderHeight = nextCanvasHeight;
    const targetsChanged = nextRenderWidth !== renderWidth ||
      nextRenderHeight !== renderHeight;
    const canvasChanged = nextCanvasWidth !== canvasWidth ||
      nextCanvasHeight !== canvasHeight;

    if (!targetsChanged && !canvasChanged) return false;
    if (targetsChanged) {
      gl.deleteFramebuffer(presentationSurface.framebuffer);
      gl.deleteTexture(presentationSurface.texture);
      gl.deleteRenderbuffer(presentationSurface.depthBuffer);
      gl.deleteTexture(historyTexture);
      historyAvailable = false;
    }
    canvasWidth = nextCanvasWidth;
    canvasHeight = nextCanvasHeight;
    canvas.width = canvasWidth;
    canvas.height = canvasHeight;
    renderWidth = nextRenderWidth;
    renderHeight = nextRenderHeight;
    logicalViewport = {
      x: Math.floor((renderWidth - viewportUnit * 4) / 2),
      y: Math.floor((renderHeight - viewportUnit * 3) / 2),
      width: viewportUnit * 4,
      height: viewportUnit * 3
    };
    if (targetsChanged) {
      presentationSurface = createPresentationSurface();
      historyTexture = createPresentationTexture();
    }
    return true;
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

  function drawPresentationTexture(texture, fade = 0) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.disable(gl.SCISSOR_TEST);
    gl.disable(gl.BLEND);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.viewport(0, 0, canvasWidth, canvasHeight);
    if (texture !== undefined) {
      drawScreenTexture(texture, 0, 0, displayWidth, displayHeight, fade);
    }
    gl.enable(gl.SCISSOR_TEST);
  }

  function centeredViewport(contentWidth, contentHeight) {
    const scale = Math.min(canvasWidth / contentWidth, canvasHeight / contentHeight);
    const width = Math.max(1, Math.floor(contentWidth * scale));
    const height = Math.max(1, Math.floor(contentHeight * scale));
    return {
      x: Math.floor((canvasWidth - width) / 2),
      y: Math.floor((canvasHeight - height) / 2),
      width,
      height
    };
  }

  function drawCinematicTexture(texture, fade = 0) {
    const viewport = centeredViewport(
      cinematicVideo.videoWidth || displayWidth,
      cinematicVideo.videoHeight || 200
    );

    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.disable(gl.SCISSOR_TEST);
    gl.disable(gl.BLEND);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.viewport(viewport.x, viewport.y, viewport.width, viewport.height);
    drawScreenTexture(texture, 0, 0, displayWidth, displayHeight, fade);
    gl.enable(gl.SCISSOR_TEST);
  }

  function prepareDirectFrame() {
    gl.bindFramebuffer(gl.FRAMEBUFFER, presentationSurface.framebuffer);
    gl.disable(gl.SCISSOR_TEST);
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.viewport(logicalViewport.x, logicalViewport.y,
                logicalViewport.width, logicalViewport.height);
    if (frameBase !== undefined) {
      drawScreenTexture(frameBase, 0, 0, displayWidth, displayHeight);
    } else if (frameClearColour !== undefined) {
      gl.enable(gl.SCISSOR_TEST);
      gl.scissor(logicalViewport.x, logicalViewport.y,
                 logicalViewport.width, logicalViewport.height);
      gl.clearColor(...frameClearColour, 1);
      gl.clear(gl.COLOR_BUFFER_BIT);
      gl.disable(gl.SCISSOR_TEST);
    } else if (historyAvailable) {
      drawScreenTexture(historyTexture, 0, 0, displayWidth, displayHeight);
    }
    for (const rectangle of pendingRectangles) {
      const left = Math.max(0, Math.min(displayWidth, rectangle.left));
      const right = Math.max(0, Math.min(displayWidth, rectangle.right));
      const top = Math.max(0, Math.min(displayHeight, rectangle.top));
      const bottom = Math.max(0, Math.min(displayHeight, rectangle.bottom));
      if (left >= right || top >= bottom) continue;
      gl.enable(gl.SCISSOR_TEST);
      gl.scissor(logicalViewport.x + Math.floor(
                   left * logicalViewport.width / displayWidth
                 ),
                 logicalViewport.y + Math.floor(
                   (displayHeight - bottom) * logicalViewport.height / displayHeight
                 ),
                 Math.ceil((right - left) * logicalViewport.width / displayWidth),
                 Math.ceil((bottom - top) * logicalViewport.height / displayHeight));
      gl.clearColor(...rectangle.colour, 1);
      gl.clear(gl.COLOR_BUFFER_BIT);
      gl.disable(gl.SCISSOR_TEST);
    }
    gl.enable(gl.SCISSOR_TEST);
  }

  function snapshotPresentation() {
    gl.bindTexture(gl.TEXTURE_2D, historyTexture);
    gl.copyTexSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 0, 0, renderWidth, renderHeight);
    historyAvailable = true;
  }

  function drawCinematicFrame() {
    if (cinematicVideo === undefined) return;
    resizeRenderTargets();
    if (cinematicVideo.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA) {
      gl.bindTexture(gl.TEXTURE_2D, cinematicTexture);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE,
                    cinematicVideo);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
      drawCinematicTexture(cinematicTexture, fade);
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

  function applyTransition() {
    if (pendingTransition === undefined || !historyAvailable) return;
    gl.enable(gl.BLEND);
    gl.blendEquation(gl.FUNC_ADD);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    if (pendingTransition === "blur") {
      drawScreenTexture(historyTexture, 0, 0, displayWidth, displayHeight,
                        0, 0.75);
    } else {
      for (let extent = 8; extent <= 160; extent += 8) {
        const verticalExtent = extent * 120 / 160;
        drawScreenTexture(historyTexture, 160 - extent, 120 - verticalExtent,
                          extent * 2, verticalExtent * 2, 0, 0.125);
      }
    }
    gl.disable(gl.BLEND);
    pendingTransition = undefined;
  }

  gl.useProgram(program);
  gl.uniform1i(gl.getUniformLocation(program, "texture0"), 0);
  gl.uniform1f(screenHeight, displayHeight);
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
      gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE,
                           gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
    } else if (operation === "mix") {
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    } else if (operation === "alpha") {
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

  function worldCommandFromWasm(memory, address) {
    const material = commandFromWasm(memory, address);
    const view = new Float32Array(memory.buffer, address + COMMAND_BYTES, 12);

    return {
      material,
      viewX: [view[0], view[1], view[2], view[3]],
      viewY: [view[4], view[5], view[6], view[7]],
      viewZ: [view[8], view[9], view[10], view[11]]
    };
  }

  function normaliseVector(vector) {
    const length = Math.hypot(vector[0], vector[1], vector[2]);
    return length === 0 ? [0, 1, 0] :
      [vector[0] / length, vector[1] / length, vector[2] / length];
  }

  function crossProduct(left, right) {
    return [
      left[1] * right[2] - left[2] * right[1],
      left[2] * right[0] - left[0] * right[2],
      left[0] * right[1] - left[1] * right[0]
    ];
  }

  function updateModelLightDirection() {
    if (terrainFrame === undefined) {
      modelLightDirection = normaliseVector([0.45, 0.8, 0.35]);
      return;
    }

    const horizontal = normaliseVector(terrainFrame.horizontal);
    const vertical = normaliseVector(terrainFrame.vertical);
    const flatNormal = normaliseVector(crossProduct(horizontal, vertical));
    modelLightDirection = normaliseVector([
      flatNormal[0] + horizontal[0] * 0.45 + vertical[0] * 0.35,
      flatNormal[1] + horizontal[1] * 0.45 + vertical[1] * 0.35,
      flatNormal[2] + horizontal[2] * 0.45 + vertical[2] * 0.35
    ]);
  }

  function directionalFlatModelLight(world) {
    if (!useDirectionalFlatModelShading ||
        (world.material.encoding &
         (WORLD_BACKGROUND_ENCODING | WORLD_BILLBOARD_ENCODING)) !== 0) {
      return 1;
    }

    const horizontal = [
      world.viewX[1] - world.viewX[0],
      world.viewY[1] - world.viewY[0],
      world.viewZ[1] - world.viewZ[0]
    ];
    const vertical = [
      world.viewX[3] - world.viewX[0],
      world.viewY[3] - world.viewY[0],
      world.viewZ[3] - world.viewZ[0]
    ];
    const unnormalisedNormal = crossProduct(horizontal, vertical);
    const normalLength = Math.hypot(...unnormalisedNormal);
    if (normalLength === 0) return 1;
    const normal = unnormalisedNormal.map((component) => component / normalLength);
    const directional = Math.abs(
      normal[0] * modelLightDirection[0] +
      normal[1] * modelLightDirection[1] +
      normal[2] * modelLightDirection[2]
    );
    return 0.55 + directional * 0.9;
  }

  function worldRenderEntry(world, memory, shadeAsModel = true) {
    const particleSlot = (world.material.shade >>> PARTICLE_ATLAS_SLOT_SHIFT) &
      PARTICLE_ATLAS_SLOT_MASK;
    const particleTransform =
      (world.material.shade >>> PARTICLE_ATLAS_TRANSFORM_SHIFT) &
      PARTICLE_ATLAS_TRANSFORM_MASK;
    const image = (world.material.encoding & WORLD_PARTICLE_ENCODING) !== 0 &&
      useHighResolutionBillboards && particleAtlasReady &&
      particleSlot < PARTICLE_ATLAS_SLOT_COUNT ? {
        texture: particleAtlasTexture,
        u: (particleSlot * PARTICLE_ATLAS_CELL_SIZE +
            PARTICLE_ATLAS_CELL_BORDER) /
          (PARTICLE_ATLAS_SLOT_COUNT * PARTICLE_ATLAS_CELL_SIZE),
        v: PARTICLE_ATLAS_CELL_BORDER / PARTICLE_ATLAS_CELL_SIZE,
        width: (PARTICLE_ATLAS_CELL_SIZE - PARTICLE_ATLAS_CELL_BORDER * 2) /
          (PARTICLE_ATLAS_SLOT_COUNT * PARTICLE_ATLAS_CELL_SIZE),
        height: (PARTICLE_ATLAS_CELL_SIZE - PARTICLE_ATLAS_CELL_BORDER * 2) /
          PARTICLE_ATLAS_CELL_SIZE
      } : textureForCommand(world.material, memory);
    if (image === null) return null;

    const usesTextureAlpha = image.texture === particleAtlasTexture &&
      particleSlot >= 20 && particleSlot < 28;
    const operation = usesTextureAlpha && particleSlot >= 24 ?
      "alpha" : pixelOperation(world.material, false);
    const useAv = (world.material.ccbFlags & CCB_USEAV) !== 0;
    const pMode0Scale = sourceScale(pixcHalf(world.material, false), useAv);
    const pMode1Scale = sourceScale(pixcHalf(world.material, true), useAv);
    const scales = (scale) => {
      if (operation === "replace") return [scale, 1];
      if (operation === "add" || operation === "mix") return [1, scale];
      return [1, 1];
    };
    const firstScales = scales(pMode0Scale);
    const secondScales = scales(pMode1Scale);
    const texture = image.texture === undefined ?
      { texture: image, u: 0, v: 0, width: 1, height: 1 } : image;
    const directionalLight = shadeAsModel ? directionalFlatModelLight(world) : 1;
    return {
      world,
      operation,
      texture,
      particleTransform,
      usesTextureAlpha,
      sourceShade: [firstScales[0] * directionalLight,
                    secondScales[0] * directionalLight],
      sourceOpacity: [firstScales[1], secondScales[1]],
      destinationScale: operation === "diminish" ? pMode0Scale : 1,
      cullBackfaces: (world.material.encoding & WORLD_MODEL_ENCODING) !== 0
    };
  }

  function appendWorldQuadVertices(vertices, entry) {
    const indices = [0, 1, 2, 0, 2, 3];

    for (const index of indices) {
      let [u, v] = entry.textureCoordinates?.[index] ??
        [index === 1 || index === 2 ? 1 : 0, index >= 2 ? 1 : 0];
      if (entry.usesTextureAlpha) {
        if ((entry.particleTransform & 4) !== 0) u = 1 - u;
        if ((entry.particleTransform & 3) === 1) {
          [u, v] = [v, 1 - u];
        } else if ((entry.particleTransform & 3) === 2) {
          [u, v] = [1 - u, 1 - v];
        } else if ((entry.particleTransform & 3) === 3) {
          [u, v] = [1 - v, u];
        }
      }
      vertices.push(entry.world.viewX[index], entry.world.viewY[index],
                    entry.world.viewZ[index],
                    entry.texture.u + u * entry.texture.width,
                    entry.texture.v + v * entry.texture.height,
                    entry.sourceShade[0], entry.sourceShade[1],
                    entry.sourceOpacity[0], entry.sourceOpacity[1],
                    entry.usesTextureAlpha ? 1 : 0);
    }
  }

  function drawWorldBatch(entries, writeDepth = true, stats = undefined,
                          smoothTexture = false) {
    if (entries.length === 0) return;

    const entry = entries[0];
    const vertices = [];
    for (const batchEntry of entries) {
      appendWorldQuadVertices(vertices, batchEntry);
    }

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, entry.texture.texture);
    const useLinearFiltering = smoothTexture ||
      entry.texture.texture === particleAtlasTexture;
    if (useLinearFiltering) {
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    }
    gl.useProgram(worldProgram);
    gl.uniform1f(worldAspectRatio, renderWidth / renderHeight);
    configureBlend(entry.operation, entry.destinationScale);
    if (entry.cullBackfaces) {
      gl.enable(gl.CULL_FACE);
      gl.frontFace(gl.CW);
      gl.cullFace(gl.BACK);
    } else {
      gl.disable(gl.CULL_FACE);
    }
    gl.depthMask(writeDepth && entry.operation === "replace");
    gl.bindBuffer(gl.ARRAY_BUFFER, worldVertices);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(vertices), gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(worldPosition);
    gl.vertexAttribPointer(worldPosition, 3, gl.FLOAT, false,
                           10 * Float32Array.BYTES_PER_ELEMENT, 0);
    gl.enableVertexAttribArray(worldTextureCoordinate);
    gl.vertexAttribPointer(worldTextureCoordinate, 2, gl.FLOAT, false,
                           10 * Float32Array.BYTES_PER_ELEMENT,
                           3 * Float32Array.BYTES_PER_ELEMENT);
    gl.enableVertexAttribArray(worldSourceShade);
    gl.vertexAttribPointer(worldSourceShade, 2, gl.FLOAT, false,
                           10 * Float32Array.BYTES_PER_ELEMENT,
                           5 * Float32Array.BYTES_PER_ELEMENT);
    gl.enableVertexAttribArray(worldSourceOpacity);
    gl.vertexAttribPointer(worldSourceOpacity, 2, gl.FLOAT, false,
                           10 * Float32Array.BYTES_PER_ELEMENT,
                           7 * Float32Array.BYTES_PER_ELEMENT);
    gl.enableVertexAttribArray(worldTextureAlpha);
    gl.vertexAttribPointer(worldTextureAlpha, 1, gl.FLOAT, false,
                           10 * Float32Array.BYTES_PER_ELEMENT,
                           9 * Float32Array.BYTES_PER_ELEMENT);
    gl.drawArrays(gl.TRIANGLES, 0, vertices.length / 10);
    if (useLinearFiltering) {
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    }
    gl.disable(gl.CULL_FACE);
    gl.frontFace(gl.CCW);
    if (stats !== undefined) {
      let terrainCommands = 0;
      for (const batchEntry of entries) {
        if ((batchEntry.world.material.encoding & TERRAIN_ENCODING) !== 0) {
          terrainCommands += 1;
        }
      }

      stats.drawCalls += 1;
      if (terrainCommands !== 0) {
        stats.terrainCommands += terrainCommands;
        stats.terrainDrawCalls += 1;
        if (terrainCommands === entries.length) {
          stats.terrainOnlyDrawCalls += 1;
        } else {
          stats.mixedTerrainDrawCalls += 1;
        }
      }
    }
  }

  function drawWorldSky(memory, commandAddress, commandCount, stats) {
    const nearPlane = 1280.0;
    const skyDepth = nearPlane + 1.0;
    const aspectRatio = renderWidth / renderHeight;
    const horizontalScale = aspectRatio >= LEGACY_ASPECT_RATIO ?
      1.6 / aspectRatio : 1.2;
    const verticalScale = aspectRatio >= LEGACY_ASPECT_RATIO ?
      1.6 : 1.2 * aspectRatio;
    const skyOverscan = Math.hypot(
      LEGACY_PROJECTION_FOCAL_LENGTH / horizontalScale,
      LEGACY_PROJECTION_FOCAL_LENGTH / verticalScale
    ) / (LOGICAL_DISPLAY_HEIGHT / 2);
    const skyExtensionLength = skyOverscan * (LOGICAL_DISPLAY_HEIGHT / 2) *
      skyDepth / LEGACY_PROJECTION_FOCAL_LENGTH;

    for (let index = 0; index < commandCount; index += 1) {
      const material = commandFromWasm(
        memory, commandAddress + index * COMMAND_BYTES
      );
      const encoding = material.encoding & 0xff;
      if (encoding !== SKY_ENCODING && encoding !== SKY_GRADIENT_ENCODING) {
        continue;
      }

      const entry = worldRenderEntry({
        material,
        viewX: material.x.map((x) =>
          (x - LOGICAL_DISPLAY_WIDTH / 2) * skyOverscan * skyDepth /
          LEGACY_PROJECTION_FOCAL_LENGTH),
        viewY: [skyDepth, skyDepth, skyDepth, skyDepth],
        viewZ: material.y.map((y) =>
          (y - LOGICAL_DISPLAY_HEIGHT / 2) * skyOverscan * skyDepth /
          LEGACY_PROJECTION_FOCAL_LENGTH)
      }, memory, false);
      if (entry === null) continue;

      const horizonX = (entry.world.viewX[2] + entry.world.viewX[3] -
                        entry.world.viewX[0] - entry.world.viewX[1]) / 2;
      const horizonZ = (entry.world.viewZ[2] + entry.world.viewZ[3] -
                        entry.world.viewZ[0] - entry.world.viewZ[1]) / 2;
      const horizonLength = Math.hypot(horizonX, horizonZ);
      if (horizonLength !== 0) {
        const extensionX = horizonX * skyExtensionLength / horizonLength;
        const extensionZ = horizonZ * skyExtensionLength / horizonLength;
        const continuation = {
          ...entry,
          world: {
            material: entry.world.material,
            viewX: [
              entry.world.viewX[3],
              entry.world.viewX[2],
              entry.world.viewX[2] + extensionX,
              entry.world.viewX[3] + extensionX
            ],
            viewY: [skyDepth, skyDepth, skyDepth, skyDepth],
            viewZ: [
              entry.world.viewZ[3],
              entry.world.viewZ[2],
              entry.world.viewZ[2] + extensionZ,
              entry.world.viewZ[3] + extensionZ
            ]
          },
          textureCoordinates: [[0, 1], [1, 1], [1, 1], [0, 1]]
        };
        drawWorldBatch([continuation], false, stats, true);
      }
      drawWorldBatch([entry], false, stats, true);
    }
  }

  function drawWorldSkyBillboards(memory, commandAddress, commandCount, stats) {
    const batches = [];

    for (let index = 0; index < commandCount; index += 1) {
      const world = worldCommandFromWasm(
        memory, commandAddress + index * WORLD_COMMAND_BYTES
      );
      if ((world.material.encoding & WORLD_SKY_ENCODING) === 0) continue;

      const entry = worldRenderEntry(world, memory, false);
      if (entry === null) continue;
      const batch = batches.find((candidate) => sameWorldBatch(candidate[0], entry));
      if (batch === undefined) {
        batches.push([entry]);
      } else {
        batch.push(entry);
      }
    }
    for (const batch of batches) {
      drawWorldBatch(batch, false, stats);
    }
  }

  function drawPersistentTerrain(stats) {
    if (terrainFrame === undefined || !terrainStateTextureReady ||
        (bakedTerrainAtlas === undefined && !terrainTileTexturesReady)) return;

    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, terrainStateTexture);
    gl.activeTexture(gl.TEXTURE3);
    gl.bindTexture(gl.TEXTURE_2D_ARRAY, terrainTileTextures);
    if (bakedTerrainAtlas !== undefined) {
      gl.activeTexture(gl.TEXTURE5);
      gl.bindTexture(gl.TEXTURE_2D,
                     bakedTerrainAtlas.shared?.texture ??
                     bakedTerrainAtlas.world.texture);
      gl.activeTexture(gl.TEXTURE6);
      gl.bindTexture(gl.TEXTURE_2D,
                     bakedTerrainAtlas.world?.texture ??
                     bakedTerrainAtlas.shared.texture);
      uploadTerrainMaterialSlots(bakedTerrainAtlas);
    }
    gl.activeTexture(gl.TEXTURE4);
    gl.bindTexture(gl.TEXTURE_2D, terrainHeightOffsetsTexture);
    gl.useProgram(terrainProgram);
    gl.uniform1i(terrainState, 1);
    gl.uniform1i(terrainTileTexturesUniform, 3);
    gl.uniform1i(terrainSharedTextureAtlas, 5);
    gl.uniform1i(terrainWorldTextureAtlas, 6);
    gl.uniform1i(terrainMaterialSlots, 7);
    gl.uniform1i(terrainSharedAtlasColumns,
                 bakedTerrainAtlas?.shared?.columns ?? 1);
    gl.uniform1i(terrainWorldAtlasColumns,
                 bakedTerrainAtlas?.world?.columns ?? 1);
    gl.uniform1i(useTerrainTextureAtlas, bakedTerrainAtlas === undefined ? 0 : 1);
    gl.uniform1i(terrainHeightOffsets, 4);
    gl.uniform2i(terrainMapStart, terrainFrame.mapX, terrainFrame.mapY);
    gl.uniform3f(terrainOrigin, ...terrainFrame.origin);
    gl.uniform3f(terrainHorizontal, ...terrainFrame.horizontal);
    gl.uniform3f(terrainVertical, ...terrainFrame.vertical);
    gl.uniform1i(terrainWavePhase, terrainFrame.wavePhase);
    gl.uniform1i(terrainShadingMode, useGouraudTerrainShading ? 1 : 0);
    gl.uniform1f(terrainAspectRatio, renderWidth / renderHeight);
    configureBlend("replace");
    gl.depthMask(true);
    gl.bindBuffer(gl.ARRAY_BUFFER, terrainVertices);
    gl.enableVertexAttribArray(terrainCoordinate);
    gl.vertexAttribPointer(terrainCoordinate, 2, gl.FLOAT, false,
                           2 * Float32Array.BYTES_PER_ELEMENT, 0);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, terrainIndices);
    gl.drawElementsInstanced(gl.TRIANGLES, terrainMesh.indices.length,
                             gl.UNSIGNED_INT, 0,
                             TERRAIN_GRID_COPIES * TERRAIN_GRID_COPIES);
    stats.drawCalls += 1;
    stats.terrainDrawCalls += 1;
    stats.terrainOnlyDrawCalls += 1;
    stats.persistentTerrainDrawCalls += 1;
  }

  function sameWorldBatch(left, right) {
    return left.texture.texture === right.texture.texture &&
      left.operation === right.operation &&
      left.destinationScale === right.destinationScale &&
      left.cullBackfaces === right.cullBackfaces;
  }

  function drawWorld(memory, commandAddress, commandCount,
                     legacyCommandAddress, legacyCommandCount) {
    const stats = {
      commandCount,
      drawCalls: 0,
      terrainCommands: 0,
      terrainDrawCalls: 0,
      terrainOnlyDrawCalls: 0,
      mixedTerrainDrawCalls: 0,
      persistentTerrainDrawCalls: 0
    };

    lastWorldStats = stats;
    if (commandCount === 0 && terrainFrame === undefined) return false;

    updateModelLightDirection();
    gl.bindFramebuffer(gl.FRAMEBUFFER, presentationSurface.framebuffer);
    gl.viewport(0, 0, renderWidth, renderHeight);
    gl.disable(gl.SCISSOR_TEST);
    gl.disable(gl.BLEND);
    gl.enable(gl.DEPTH_TEST);
    gl.depthMask(true);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.disable(gl.DEPTH_TEST);
    drawWorldSky(memory, legacyCommandAddress, legacyCommandCount, stats);
    drawWorldSkyBillboards(memory, commandAddress, commandCount, stats);
    gl.enable(gl.DEPTH_TEST);
    drawPersistentTerrain(stats);
    const transparentCommands = [];
    const opaqueBatches = new Map();
    for (let index = 0; index < commandCount; index += 1) {
      const world = worldCommandFromWasm(
        memory, commandAddress + index * WORLD_COMMAND_BYTES
      );
      if ((world.material.encoding & WORLD_SKY_ENCODING) !== 0) continue;
      const entry = worldRenderEntry(world, memory);
      if (entry === null) continue;
      if (entry.operation === "replace") {
        let textureBatches = opaqueBatches.get(entry.texture.texture);
        if (textureBatches === undefined) {
          textureBatches = [[], []];
          opaqueBatches.set(entry.texture.texture, textureBatches);
        }
        const batch = textureBatches[entry.cullBackfaces ? 1 : 0];
        batch.push(entry);
      } else {
        transparentCommands.push(entry);
      }
    }
    for (const textureBatches of opaqueBatches.values()) {
      for (const batch of textureBatches) {
        drawWorldBatch(batch, true, stats);
      }
    }
    transparentCommands.sort((left, right) => {
      const leftDepth = left.world.viewY[0] + left.world.viewY[1] +
        left.world.viewY[2] + left.world.viewY[3];
      const rightDepth = right.world.viewY[0] + right.world.viewY[1] +
        right.world.viewY[2] + right.world.viewY[3];

      return rightDepth - leftDepth;
    });
    let transparentBatch = [];
    for (const entry of transparentCommands) {
      if (transparentBatch.length !== 0 &&
          !sameWorldBatch(transparentBatch[0], entry)) {
        drawWorldBatch(transparentBatch, false, stats);
        transparentBatch = [];
      }
      transparentBatch.push(entry);
    }
    drawWorldBatch(transparentBatch, false, stats);
    gl.depthMask(true);
    gl.disable(gl.DEPTH_TEST);
    gl.enable(gl.SCISSOR_TEST);
    return true;
  }

  const COMMAND_VERTEX_FLOATS = 19;
  const COMMAND_VERTEX_BYTES = COMMAND_VERTEX_FLOATS * Float32Array.BYTES_PER_ELEMENT;

  function renderEntry(command, image) {
    const geometry = fallbackGeometry(command);
    const texture = image.texture === undefined ?
      { texture: image, u: 0, v: 0, width: 1, height: 1 } : image;
    let hull = convexHull(command);
    const minX = Math.max(0, Math.floor(Math.min(...command.x)));
    const maxX = Math.min(displayWidth, Math.ceil(Math.max(...command.x)));
    const minY = Math.max(0, Math.floor(Math.min(...command.y)));
    const maxY = Math.min(displayHeight, Math.ceil(Math.max(...command.y)));
    if (hull === null || minX >= maxX || minY >= maxY) return null;
    const expandCoverage = !geometry.requiresFallback &&
      !isCollapsedQuadTriangle(command);
    if (expandCoverage) {
      hull = expandedConvexHull(hull, 0.5);
    }

    const sourceOperation = pixelOperation(command, false);
    const operation = texture.coverageBlendOperation ?? sourceOperation;
    const useAv = (command.ccbFlags & CCB_USEAV) !== 0;
    const pMode0Scale = sourceScale(pixcHalf(command, false), useAv);
    const pMode1Scale = sourceScale(pixcHalf(command, true), useAv);
    const scales = (scale) => {
      if (sourceOperation === "replace") return [scale, 1];
      if (sourceOperation === "add" || sourceOperation === "mix") return [1, scale];
      return [1, 1];
    };
    const firstScales = scales(pMode0Scale);
    const secondScales = scales(pMode1Scale);

    return {
      command,
      image: texture,
      usesTextureCoverage: texture.usesTextureCoverage === true,
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

  function drawBatch(entries, directToCanvas = false) {
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
    gl.uniform1f(usesTextureCoverage, first.usesTextureCoverage ? 1 : 0);
    configureBlend(first.operation, first.destinationScale);
    if (directToCanvas) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, presentationSurface.framebuffer);
      gl.viewport(logicalViewport.x, logicalViewport.y,
                  logicalViewport.width, logicalViewport.height);
      gl.disable(gl.SCISSOR_TEST);
    }
    gl.uniform2f(renderScale, logicalViewport.width / displayWidth,
                 logicalViewport.height / displayHeight);
    gl.uniform2f(renderOrigin, directToCanvas ? logicalViewport.x : 0,
                 directToCanvas ? logicalViewport.y : 0);
    if (!directToCanvas) {
      gl.scissor(Math.floor(minX * logicalViewport.width / displayWidth),
                 Math.floor((displayHeight - maxY) * logicalViewport.height / displayHeight),
                 Math.ceil((maxX - minX) * logicalViewport.width / displayWidth),
                 Math.ceil((maxY - minY) * logicalViewport.height / displayHeight));
    }
    gl.bindBuffer(gl.ARRAY_BUFFER, vertices);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(fillVertices), gl.DYNAMIC_DRAW);
    configureCommandAttributes();
    gl.drawArrays(gl.TRIANGLES, 0, fillVertices.length / COMMAND_VERTEX_FLOATS);
    if (directToCanvas) gl.enable(gl.SCISSOR_TEST);
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
      previous.destinationScale === entry.destinationScale &&
      previous.usesTextureCoverage === entry.usesTextureCoverage;
  }

  function drawText(command, text, directToCanvas = false) {
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
      const glyph = textFont.glyphs[String(code)];
      if (glyph !== undefined) {
        const [sourceX, sourceY, sourceWidth, sourceHeight] = glyph.source_rect;
        const width = glyph.draw_width;
        const left = penX;
        const right = left + width;
        const top = penY;
        const bottom = top + glyph.draw_height;
        const u0 = sourceX / textFont.atlasWidth;
        const u1 = (sourceX + sourceWidth) / textFont.atlasWidth;
        const v0 = sourceY / textFont.atlasHeight;
        const v1 = (sourceY + sourceHeight) / textFont.atlasHeight;

        vertices.push(
          left, top, u0, v0,
          right, top, u1, v0,
          right, bottom, u1, v1,
          left, top, u0, v0,
          right, bottom, u1, v1,
          left, bottom, u0, v1
        );
        penX += glyph.advance;
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
    gl.uniform3f(textOutline, 0, 0, textFont.usesBakedWeights ? 0 : 1 / 31);
    gl.uniform1f(textUsesBakedWeights, textFont.usesBakedWeights ? 1 : 0);
    const left = Math.max(0, command.x[0]);
    const right = Math.min(displayWidth, command.x[0] + command.width);
    if (left >= right) return;

    // PRE1 can reveal a TextCel progressively from its left edge.
    if (directToCanvas) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, presentationSurface.framebuffer);
      gl.viewport(logicalViewport.x, logicalViewport.y,
                  logicalViewport.width, logicalViewport.height);
    }
    gl.scissor(logicalViewport.x + Math.floor(
                 left * logicalViewport.width / displayWidth
               ),
               logicalViewport.y,
               Math.ceil((right - left) * logicalViewport.width / displayWidth),
               logicalViewport.height);
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
    setBakedTextFont(image, metrics) {
      if (metrics.schema !== "font-metrics-v1" ||
          image.naturalWidth !== metrics.atlas_width ||
          image.naturalHeight !== metrics.atlas_height ||
          metrics.render_style !== "text-weights" ||
          !Number.isInteger(metrics.char_height) || metrics.char_height <= 0 ||
          !Number.isInteger(metrics.char_extra) || metrics.char_extra < 0 ||
          !Number.isInteger(metrics.leading) || metrics.leading < 0) {
        throw new Error("Baked Message font dimensions do not match its metrics");
      }
      const texture = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE,
                    image);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      if (textFont !== undefined) {
        gl.deleteTexture(textFont.texture);
      }
      textFont = {
        texture,
        atlasWidth: image.naturalWidth,
        atlasHeight: image.naturalHeight,
        textureScale: metrics.texture_scale,
        charHeight: metrics.char_height,
        charExtra: metrics.char_extra,
        leading: metrics.leading,
        glyphs: metrics.glyphs,
        usesBakedWeights: true
      };
    },
    updateTerrainState,
    updateTerrainFrame,
    toggleTerrainShading() {
      useGouraudTerrainShading = !useGouraudTerrainShading;
      return useGouraudTerrainShading ? "Gouraud" : "flat";
    },
    toggleDirectionalFlatModelShading() {
      useDirectionalFlatModelShading = !useDirectionalFlatModelShading;
      return useDirectionalFlatModelShading ? "directional flat" : "legacy";
    },
    toggleHighResolutionBillboards() {
      useHighResolutionBillboards = !useHighResolutionBillboards;
      return useHighResolutionBillboards ? "high resolution" : "original";
    },
    worldStats() {
      return { ...lastWorldStats };
    },
    setBackdrop(memory, source) {
      const sourceBytes = (displayHeight / 2) * displayWidth * 4;
      if (!Number.isInteger(source) || source < 0 ||
          source + sourceBytes > memory.length) {
        throw new Error("Backdrop source is outside WebAssembly memory");
      }
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

      if (backdropTexture === undefined) {
        backdropTexture = gl.createTexture();
      }
      gl.bindTexture(gl.TEXTURE_2D, backdropTexture);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, displayWidth, displayHeight,
                    0, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      frameBase = backdropTexture;
      frameClearColour = undefined;
    },
    setBackdropImage(image) {
      if (image.naturalWidth !== displayWidth ||
          image.naturalHeight !== displayHeight) {
        throw new Error("Baked backdrop dimensions do not match the display");
      }
      if (backdropTexture === undefined) {
        backdropTexture = gl.createTexture();
      }
      gl.bindTexture(gl.TEXTURE_2D, backdropTexture);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      frameBase = backdropTexture;
      frameClearColour = undefined;
    },
    clear(value) {
      frameBase = undefined;
      frameClearColour = [
        ((value >>> 10) & 31) / 31,
        ((value >>> 5) & 31) / 31,
        (value & 31) / 31
      ];
    },
    fillRect(colour, left, top, right, bottom) {
      pendingRectangles.push({
        colour: [
          ((colour >>> 10) & 31) / 31,
          ((colour >>> 5) & 31) / 31,
          (colour & 31) / 31
        ],
        left,
        top,
        right,
        bottom
      });
    },
    blurScreen() {
      pendingTransition = "blur";
    },
    zoomScreen() {
      pendingTransition = "zoom";
    },
    setFade(opacity) {
      fade = Math.max(0, Math.min(1, opacity));
      resizeRenderTargets();
      drawPresentationTexture(historyAvailable ? historyTexture : undefined, fade);
    },
    startCinematic(video) {
      worldFrameActive = false;
      cinematicVideo = video;
      resizeRenderTargets();
      /*
       * The last history frame belongs to gameplay.  Do not reuse it for the
       * post-cinematic menu swap, and begin the video from black after its
       * caller has faded the gameplay view out.
       */
      historyAvailable = false;
      fade = 0;
      drawPresentationTexture(undefined);
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
      worldFrameActive = false;
    },
    submit,
    submitWasm(memory, commandAddress, commandCount, directToCanvas = false,
               directTargetOverlays = false) {
      let batch = [];
      const flush = () => {
        drawBatch(batch, directToCanvas);
        batch = [];
      };
      for (let index = 0; index < commandCount; index += 1) {
        const command = commandFromWasm(
          memory, commandAddress + index * COMMAND_BYTES
        );
        const isTargetOverlay =
          (command.encoding & TARGET_OVERLAY_ENCODING) !== 0;
        if ((isTargetOverlay && worldFrameActive !== directTargetOverlays) ||
            (!isTargetOverlay && directTargetOverlays)) {
          continue;
        }
        if ((command.encoding & WORLD_ENCODING) !== 0 ||
            (worldFrameActive &&
             (command.encoding & WORLD_BACKGROUND_ENCODING) !== 0) ||
            (worldFrameActive &&
             ((command.encoding & 0xff) === SKY_ENCODING ||
              (command.encoding & 0xff) === SKY_GRADIENT_ENCODING))) {
          continue;
        }
        if ((command.encoding & 0xff) === TEXT_ENCODING) {
          flush();
          drawText(command, textForCommand(command, memory), directToCanvas);
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
    present(memory, commandAddress, commandCount, worldCommandAddress,
            worldCommandCount) {
      resizeRenderTargets();
      worldFrameActive = drawWorld(memory, worldCommandAddress, worldCommandCount,
                                   commandAddress, commandCount);
      if (worldFrameActive) {
        this.submitWasm(memory, commandAddress, commandCount, true, true);
        this.submitWasm(memory, commandAddress, commandCount, true);
      } else {
        prepareDirectFrame();
        this.submitWasm(memory, commandAddress, commandCount, true, true);
        this.submitWasm(memory, commandAddress, commandCount, true);
      }
      applyTransition();
      snapshotPresentation();
      drawPresentationTexture(presentationSurface.texture, fade);
      frameBase = undefined;
      frameClearColour = undefined;
      pendingRectangles.length = 0;
    },
  };
}
