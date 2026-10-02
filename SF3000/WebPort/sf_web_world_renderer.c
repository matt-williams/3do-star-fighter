#include "sf_web_world_renderer.h"

#include <string.h>

static SFWebWorldQuad sf_web_world_commands[SF_WEB_WORLD_COMMAND_CAPACITY];
static uint32_t sf_web_world_command_count;
static const uint8_t *sf_web_terrain_height_map;
static uint32_t sf_web_terrain_dirty_left;
static uint32_t sf_web_terrain_dirty_top;
static uint32_t sf_web_terrain_dirty_right;
static uint32_t sf_web_terrain_dirty_bottom;
static int32_t sf_web_terrain_full_upload;
static SFWebTerrainFrame sf_web_terrain_frame;
static SFWebRenderQuad sf_web_terrain_tile_materials[SF_WEB_TERRAIN_TILE_COUNT];
static uint8_t sf_web_terrain_tile_material_valid[SF_WEB_TERRAIN_TILE_COUNT];
static uint8_t sf_web_terrain_tile_material_dirty[SF_WEB_TERRAIN_TILE_COUNT];
static uint8_t sf_web_terrain_tile_material_required[SF_WEB_TERRAIN_TILE_COUNT];
static int32_t sf_web_terrain_material_full_collection_required;

static void sf_web_world_renderer_clear_terrain_dirty_region(void)
{
	sf_web_terrain_dirty_left = SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION;
	sf_web_terrain_dirty_top = SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION;
	sf_web_terrain_dirty_right = 0;
	sf_web_terrain_dirty_bottom = 0;
}

void sf_web_world_renderer_initialise(void)
{
	sf_web_world_command_count = 0;
	sf_web_terrain_height_map = NULL;
	sf_web_terrain_full_upload = 0;
	memset(&sf_web_terrain_frame, 0, sizeof(sf_web_terrain_frame));
	sf_web_terrain_frame.tile_materials = sf_web_terrain_tile_materials;
	sf_web_terrain_frame.tile_material_valid =
		sf_web_terrain_tile_material_valid;
	sf_web_terrain_frame.tile_material_dirty =
		sf_web_terrain_tile_material_dirty;
	sf_web_terrain_material_full_collection_required = 1;
	memset(sf_web_terrain_tile_material_valid, 0,
		sizeof(sf_web_terrain_tile_material_valid));
	memset(sf_web_terrain_tile_material_dirty, 0,
		sizeof(sf_web_terrain_tile_material_dirty));
	memset(sf_web_terrain_tile_material_required, 0,
		sizeof(sf_web_terrain_tile_material_required));
	sf_web_world_renderer_clear_terrain_dirty_region();
}

void sf_web_world_renderer_reset(void)
{
	sf_web_world_command_count = 0;
	sf_web_terrain_frame.active = 0;
}

int32_t sf_web_world_renderer_append_last_quad(const int32_t view_x[4],
	const int32_t view_y[4], const int32_t view_z[4])
{
	SFWebRenderQuad *material;
	SFWebWorldQuad *command;

	if (view_x == NULL || view_y == NULL || view_z == NULL ||
		sf_web_world_command_count >= SF_WEB_WORLD_COMMAND_CAPACITY)
		return -1;

	material = sf_web_renderer_last_command();
	if (material == NULL)
		return -1;

	material->encoding |= SF_WEB_RENDER_ENCODING_WORLD;
	command = &sf_web_world_commands[sf_web_world_command_count];
	command->material = *material;
	memcpy(command->view_x, view_x, sizeof(command->view_x));
	memcpy(command->view_y, view_y, sizeof(command->view_y));
	memcpy(command->view_z, view_z, sizeof(command->view_z));
	++sf_web_world_command_count;
	return (int32_t)(sf_web_world_command_count - 1);
}

int32_t sf_web_world_renderer_append_last_billboard(int32_t view_depth)
{
	const int32_t view_depths[4] = {
		view_depth, view_depth, view_depth, view_depth
	};

	return sf_web_world_renderer_append_last_projected_quad(view_depths);
}

int32_t sf_web_world_renderer_append_last_projected_quad(
	const int32_t view_depth[4])
{
	SFWebRenderQuad *material = sf_web_renderer_last_command();
	int32_t view_x[4];
	int32_t view_y[4];
	int32_t view_z[4];
	int32_t point;

	if (material == NULL || view_depth == NULL)
		return -1;

	for (point = 0; point < 4; ++point)
	{
		if (view_depth[point] <= 0)
			return -1;
		view_x[point] = (int32_t)((((int64_t)material->x[point] -
			SF_WEB_RENDER_WIDTH / 2) * view_depth[point]) /
			SF_WEB_LEGACY_PROJECTION_FOCAL_LENGTH);
		view_y[point] = view_depth[point];
		view_z[point] = (int32_t)((((int64_t)material->y[point] -
			SF_WEB_RENDER_HEIGHT / 2) * view_depth[point]) /
			SF_WEB_LEGACY_PROJECTION_FOCAL_LENGTH);
	}
	return sf_web_world_renderer_append_last_quad(view_x, view_y, view_z);
}

void sf_web_world_renderer_suppress_last_legacy_quad(void)
{
	SFWebRenderQuad *command = sf_web_renderer_last_command();

	if (command != NULL)
		command->encoding |= SF_WEB_RENDER_ENCODING_WORLD_BACKGROUND;
}

const SFWebWorldQuad *sf_web_world_renderer_commands(void)
{
	return sf_web_world_commands;
}

uint32_t sf_web_world_renderer_command_count(void)
{
	return sf_web_world_command_count;
}

void sf_web_world_renderer_set_terrain_height_map(const uint8_t *heights)
{
	if (sf_web_terrain_height_map == heights)
		return;

	sf_web_terrain_height_map = heights;
	sf_web_terrain_frame.heights = heights;
	sf_web_terrain_full_upload = heights != NULL;
	sf_web_world_renderer_clear_terrain_dirty_region();
}

void sf_web_world_renderer_invalidate_terrain_height_map(void)
{
	if (sf_web_terrain_height_map != NULL)
		sf_web_terrain_full_upload = 1;
}

void sf_web_world_renderer_mark_terrain_height_change(uint32_t x, uint32_t y)
{
	if (sf_web_terrain_height_map == NULL ||
		x >= SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION ||
		y >= SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION)
		return;

	if (x < sf_web_terrain_dirty_left)
		sf_web_terrain_dirty_left = x;
	if (y < sf_web_terrain_dirty_top)
		sf_web_terrain_dirty_top = y;
	if (x > sf_web_terrain_dirty_right)
		sf_web_terrain_dirty_right = x;
	if (y > sf_web_terrain_dirty_bottom)
		sf_web_terrain_dirty_bottom = y;
}

int32_t sf_web_world_renderer_terrain_state_upload(
	SFWebTerrainStateUpload *upload)
{
	if (upload == NULL || sf_web_terrain_height_map == NULL ||
		sf_web_terrain_frame.tiles == NULL ||
		sf_web_world_renderer_terrain_material_collection_required() != 0)
		return 0;

	upload->heights = sf_web_terrain_height_map;
	upload->tiles = sf_web_terrain_frame.tiles;
	upload->full = sf_web_terrain_full_upload;
	if (sf_web_terrain_full_upload)
	{
		upload->x = 0;
		upload->y = 0;
		upload->width = SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION;
		upload->height = SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION;
		return 1;
	}
	if (sf_web_terrain_dirty_left >= SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION)
		return 0;

	upload->x = sf_web_terrain_dirty_left;
	upload->y = sf_web_terrain_dirty_top;
	upload->width = sf_web_terrain_dirty_right -
		sf_web_terrain_dirty_left + 1;
	upload->height = sf_web_terrain_dirty_bottom -
		sf_web_terrain_dirty_top + 1;
	return 1;
}

void sf_web_world_renderer_acknowledge_terrain_state_upload(void)
{
	sf_web_terrain_full_upload = 0;
	sf_web_world_renderer_clear_terrain_dirty_region();
}

void sf_web_world_renderer_set_terrain_static_data(const uint8_t *tiles,
	const int32_t *height_offsets)
{
	if (sf_web_terrain_frame.tiles != tiles)
		sf_web_terrain_full_upload = tiles != NULL;
	sf_web_terrain_frame.tiles = tiles;
	sf_web_terrain_frame.height_offsets = height_offsets;
}

void sf_web_world_renderer_invalidate_terrain_tiles(void)
{
	if (sf_web_terrain_frame.tiles != NULL)
		sf_web_terrain_full_upload = 1;
}

void sf_web_world_renderer_invalidate_terrain_materials(void)
{
	memset(sf_web_terrain_tile_material_valid, 0,
		sizeof(sf_web_terrain_tile_material_valid));
	memset(sf_web_terrain_tile_material_dirty, 0,
		sizeof(sf_web_terrain_tile_material_dirty));
	++sf_web_terrain_frame.tile_material_generation;
	sf_web_terrain_material_full_collection_required = 1;
}

void sf_web_world_renderer_mark_terrain_tile_change(uint32_t x, uint32_t y,
	uint32_t tile)
{
	if (sf_web_terrain_frame.tiles == NULL ||
		x >= SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION ||
		y >= SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION ||
		tile >= SF_WEB_TERRAIN_TILE_COUNT)
		return;

	if (x < sf_web_terrain_dirty_left)
		sf_web_terrain_dirty_left = x;
	if (y < sf_web_terrain_dirty_top)
		sf_web_terrain_dirty_top = y;
	if (x > sf_web_terrain_dirty_right)
		sf_web_terrain_dirty_right = x;
	if (y > sf_web_terrain_dirty_bottom)
		sf_web_terrain_dirty_bottom = y;
	sf_web_world_renderer_require_terrain_tile_material(tile);
}

void sf_web_world_renderer_require_terrain_tile_material(uint32_t tile)
{
	if (tile >= SF_WEB_TERRAIN_TILE_COUNT ||
		sf_web_terrain_tile_material_valid[tile] != 0)
		return;

	sf_web_terrain_tile_material_required[tile] = 1;
}

int32_t sf_web_world_renderer_terrain_material_collection_required(void)
{
	uint32_t tile;

	if (sf_web_terrain_material_full_collection_required != 0)
		return 1;
	for (tile = 0; tile < SF_WEB_TERRAIN_TILE_COUNT; ++tile)
	{
		if (sf_web_terrain_tile_material_required[tile] != 0)
			return 1;
	}
	return 0;
}

int32_t sf_web_world_renderer_terrain_material_full_collection_required(void)
{
	return sf_web_terrain_material_full_collection_required;
}

int32_t sf_web_world_renderer_terrain_tile_material_required(uint32_t tile)
{
	return tile < SF_WEB_TERRAIN_TILE_COUNT &&
		sf_web_terrain_tile_material_required[tile] != 0;
}

void sf_web_world_renderer_finish_terrain_material_collection(void)
{
	uint32_t tile;

	sf_web_terrain_material_full_collection_required = 0;
	for (tile = 0; tile < SF_WEB_TERRAIN_TILE_COUNT; ++tile)
	{
		if (sf_web_terrain_tile_material_valid[tile] != 0)
			sf_web_terrain_tile_material_required[tile] = 0;
	}
}

int32_t sf_web_world_renderer_set_terrain_tile_material(uint32_t tile,
	const SFWebRenderQuad *material)
{
	if (material == NULL || tile >= SF_WEB_TERRAIN_TILE_COUNT)
		return -1;

	sf_web_terrain_tile_materials[tile] = *material;
	sf_web_terrain_tile_material_valid[tile] = 1;
	sf_web_terrain_tile_material_dirty[tile] = 1;
	++sf_web_terrain_frame.tile_material_generation;
	return 0;
}

void sf_web_world_renderer_acknowledge_terrain_material_upload(void)
{
	memset(sf_web_terrain_tile_material_dirty, 0,
		sizeof(sf_web_terrain_tile_material_dirty));
}

void sf_web_world_renderer_set_terrain_frame(int32_t map_x, int32_t map_y,
	int32_t origin_x, int32_t origin_y, int32_t origin_z,
	int32_t horizontal_x, int32_t horizontal_y, int32_t horizontal_z,
	int32_t vertical_x, int32_t vertical_y, int32_t vertical_z,
	int32_t wave_phase)
{
	sf_web_terrain_frame.map_x = map_x;
	sf_web_terrain_frame.map_y = map_y;
	sf_web_terrain_frame.origin_x = origin_x;
	sf_web_terrain_frame.origin_y = origin_y;
	sf_web_terrain_frame.origin_z = origin_z;
	sf_web_terrain_frame.horizontal_x = horizontal_x;
	sf_web_terrain_frame.horizontal_y = horizontal_y;
	sf_web_terrain_frame.horizontal_z = horizontal_z;
	sf_web_terrain_frame.vertical_x = vertical_x;
	sf_web_terrain_frame.vertical_y = vertical_y;
	sf_web_terrain_frame.vertical_z = vertical_z;
	sf_web_terrain_frame.wave_phase = wave_phase;
	sf_web_terrain_frame.active = sf_web_terrain_frame.heights != NULL &&
		sf_web_terrain_frame.tiles != NULL &&
		sf_web_terrain_frame.height_offsets != NULL;
}

void sf_web_world_renderer_disable_terrain(void)
{
	sf_web_terrain_frame.active = 0;
}

const SFWebTerrainFrame *sf_web_world_renderer_terrain_frame(void)
{
	return &sf_web_terrain_frame;
}
