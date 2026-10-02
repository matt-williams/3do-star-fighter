#ifndef SF_WEB_RENDERER_H
#define SF_WEB_RENDERER_H

#include <stdint.h>

#define SF_WEB_RENDER_WIDTH 320
#define SF_WEB_RENDER_HEIGHT 240
#define SF_WEB_RENDER_ENCODING_BASE_MASK UINT32_C(0x000000ff)
#define SF_WEB_RENDER_ENCODING_TRANSPARENT_ZERO UINT32_C(0x80000000)
#define SF_WEB_RENDER_ENCODING_TERRAIN UINT32_C(0x40000000)
#define SF_WEB_RENDER_ENCODING_GAME_CEL UINT32_C(0x20000000)

typedef enum SFWebRenderBlend {
	SF_WEB_RENDER_BLEND_OPAQUE,
	SF_WEB_RENDER_BLEND_MIX,
	SF_WEB_RENDER_BLEND_ADDITIVE
} SFWebRenderBlend;

typedef enum SFWebRenderEncoding {
	SF_WEB_RENDER_ENCODING_INDEXED_4,
	SF_WEB_RENDER_ENCODING_INDEXED_6,
	SF_WEB_RENDER_ENCODING_DIRECT_16,
	SF_WEB_RENDER_ENCODING_DIRECT_16_PACKED,
	SF_WEB_RENDER_ENCODING_INDEXED_4_PACKED,
	SF_WEB_RENDER_ENCODING_DIRECT_16_RAW,
	SF_WEB_RENDER_ENCODING_INDEXED_6_MAP_512,
	SF_WEB_RENDER_ENCODING_INDEXED_6_MAP_128,
	SF_WEB_RENDER_ENCODING_INDEXED_6_MAP_32,
	SF_WEB_RENDER_ENCODING_DIRECT_16_SKY,
	SF_WEB_RENDER_ENCODING_TEXT
} SFWebRenderEncoding;

typedef struct SFWebRenderQuad {
	uint32_t source;
	uint32_t palette;
	int32_t x[4];
	int32_t y[4];
	int32_t shade;
	uint32_t width;
	uint32_t height;
	uint32_t blend;
	uint32_t encoding;
	uint32_t pixc;
	uint32_t ccb_flags;
} SFWebRenderQuad;

_Static_assert(sizeof(SFWebRenderQuad) == 68, "SFWebRenderQuad ABI must remain 68 bytes");

void sf_web_renderer_initialise(SFWebRenderQuad *commands, uint32_t capacity);
void sf_web_renderer_reset(void);
int32_t sf_web_renderer_append(const SFWebRenderQuad *command);
const SFWebRenderQuad *sf_web_renderer_commands(void);
uint32_t sf_web_renderer_command_count(void);
uint32_t sf_web_renderer_command_capacity(void);

#endif
