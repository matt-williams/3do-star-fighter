#ifndef SF_WEB_WORLD_RENDERER_H
#define SF_WEB_WORLD_RENDERER_H

#include <stdint.h>

#include "sf_web_renderer.h"

#define SF_WEB_WORLD_COMMAND_CAPACITY 16384u
#define SF_WEB_WORLD_FAR_DEPTH 1048575
#define SF_WEB_LEGACY_PROJECTION_FOCAL_LENGTH 192
#define SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION 256u
#define SF_WEB_TERRAIN_TILE_COUNT 256u

typedef struct SFWebWorldQuad {
	SFWebRenderQuad material;
	int32_t view_x[4];
	int32_t view_y[4];
	int32_t view_z[4];
} SFWebWorldQuad;

typedef struct SFWebTerrainStateUpload {
	const uint8_t *heights;
	const uint8_t *tiles;
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	int32_t full;
} SFWebTerrainStateUpload;

typedef struct SFWebTerrainFrame {
	const uint8_t *heights;
	const uint8_t *tiles;
	const int32_t *height_offsets;
	const SFWebRenderQuad *tile_materials;
	const uint8_t *tile_material_valid;
	const uint8_t *tile_material_dirty;
	uint32_t tile_material_generation;
	int32_t map_x;
	int32_t map_y;
	int32_t origin_x;
	int32_t origin_y;
	int32_t origin_z;
	int32_t horizontal_x;
	int32_t horizontal_y;
	int32_t horizontal_z;
	int32_t vertical_x;
	int32_t vertical_y;
	int32_t vertical_z;
	int32_t wave_phase;
	int32_t active;
} SFWebTerrainFrame;

_Static_assert(sizeof(SFWebWorldQuad) == 116,
	"SFWebWorldQuad ABI must remain 116 bytes");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(SFWebTerrainFrame) == 80,
	"SFWebTerrainFrame ABI must remain 80 bytes on wasm32");
#endif

void sf_web_world_renderer_initialise(void);
void sf_web_world_renderer_reset(void);
int32_t sf_web_world_renderer_append_last_quad(const int32_t view_x[4],
	const int32_t view_y[4], const int32_t view_z[4]);
int32_t sf_web_world_renderer_append_last_billboard(int32_t view_depth);
int32_t sf_web_world_renderer_append_last_projected_quad(
	const int32_t view_depth[4]);
void sf_web_world_renderer_suppress_last_legacy_quad(void);
const SFWebWorldQuad *sf_web_world_renderer_commands(void);
uint32_t sf_web_world_renderer_command_count(void);
void sf_web_world_renderer_set_terrain_height_map(const uint8_t *heights);
void sf_web_world_renderer_invalidate_terrain_height_map(void);
void sf_web_world_renderer_mark_terrain_height_change(uint32_t x, uint32_t y);
int32_t sf_web_world_renderer_terrain_state_upload(
	SFWebTerrainStateUpload *upload);
void sf_web_world_renderer_acknowledge_terrain_state_upload(void);
void sf_web_world_renderer_set_terrain_static_data(const uint8_t *tiles,
	const int32_t *height_offsets);
void sf_web_world_renderer_invalidate_terrain_tiles(void);
void sf_web_world_renderer_invalidate_terrain_materials(void);
void sf_web_world_renderer_mark_terrain_tile_change(uint32_t x, uint32_t y,
	uint32_t tile);
void sf_web_world_renderer_require_terrain_tile_material(uint32_t tile);
int32_t sf_web_world_renderer_terrain_material_collection_required(void);
int32_t sf_web_world_renderer_terrain_material_full_collection_required(void);
int32_t sf_web_world_renderer_terrain_tile_material_required(uint32_t tile);
void sf_web_world_renderer_finish_terrain_material_collection(void);
int32_t sf_web_world_renderer_set_terrain_tile_material(uint32_t tile,
	const SFWebRenderQuad *material);
void sf_web_world_renderer_acknowledge_terrain_material_upload(void);
void sf_web_world_renderer_set_terrain_frame(int32_t map_x, int32_t map_y,
	int32_t origin_x, int32_t origin_y, int32_t origin_z,
	int32_t horizontal_x, int32_t horizontal_y, int32_t horizontal_z,
	int32_t vertical_x, int32_t vertical_y, int32_t vertical_z,
	int32_t wave_phase);
void sf_web_world_renderer_disable_terrain(void);
const SFWebTerrainFrame *sf_web_world_renderer_terrain_frame(void);

#endif
