#if defined(PLOT_LAND_PORT_TEST)

#include <assert.h>
#include <string.h>

#include "Plot_Land.c"

long test_mode;
zsort_list plot_list;
#if defined(SF_WEB_PORT)
static int32_t terrain_frame_count;
static int32_t terrain_wave_phase;
static int32_t terrain_map_x;
static int32_t terrain_map_y;
#endif

void arm_addpolycel16(void *cel, long texture)
{
	(void)cel;
	(void)texture;
}

void arm_add4cel4(void *cel, void *map, long x, long y)
{
	(void)cel;
	(void)map;
	(void)x;
	(void)y;
}

void arm_addcelfrom512map(void *cel, long x, long y, long size)
{
	(void)cel;
	(void)x;
	(void)y;
	(void)size;
}

void arm_addcelfrom128map(void *cel, long x, long y, long size)
{
	(void)cel;
	(void)x;
	(void)y;
	(void)size;
}

void arm_addcelfrom32map(void *cel, long x, long y, long size)
{
	(void)cel;
	(void)x;
	(void)y;
	(void)size;
}

uint32_t sf_web_renderer_command_count(void)
{
	return 0;
}

int32_t sf_web_world_renderer_append_last_quad(const int32_t view_x[4],
	const int32_t view_y[4], const int32_t view_z[4])
{
	(void)view_x;
	(void)view_y;
	(void)view_z;
	return 0;
}

#if defined(SF_WEB_PORT)
void sf_web_world_renderer_set_terrain_height_map(const uint8_t *heights)
{
	(void)heights;
}

void sf_web_world_renderer_set_terrain_static_data(const uint8_t *tiles,
	const int32_t *height_offsets)
{
	(void)tiles;
	(void)height_offsets;
}

int32_t sf_web_world_renderer_terrain_material_collection_required(void)
{
	return 0;
}

int32_t sf_web_world_renderer_terrain_material_full_collection_required(void)
{
	return 0;
}

int32_t sf_web_world_renderer_terrain_tile_material_required(uint32_t tile)
{
	(void)tile;
	return 0;
}

int32_t sf_armcell_terrain_material(void *cel_data, long texture,
	SFWebRenderQuad *material)
{
	(void)cel_data;
	(void)texture;
	(void)material;
	return -1;
}

int32_t sf_web_world_renderer_set_terrain_tile_material(uint32_t tile,
	const SFWebRenderQuad *material)
{
	(void)tile;
	(void)material;
	return -1;
}

void sf_web_world_renderer_finish_terrain_material_collection(void)
{
}

void sf_web_world_renderer_disable_terrain(void)
{
}

void sf_web_world_renderer_set_terrain_frame(int32_t map_x, int32_t map_y,
	int32_t origin_x, int32_t origin_y, int32_t origin_z,
	int32_t horizontal_x, int32_t horizontal_y, int32_t horizontal_z,
	int32_t vertical_x, int32_t vertical_y, int32_t vertical_z,
	int32_t wave_phase)
{
	(void)origin_x;
	(void)origin_y;
	(void)origin_z;
	(void)horizontal_x;
	(void)horizontal_y;
	(void)horizontal_z;
	(void)vertical_x;
	(void)vertical_y;
	(void)vertical_z;
	terrain_frame_count += 1;
	terrain_map_x = map_x;
	terrain_map_y = map_y;
	terrain_wave_phase = wave_phase;
}
#endif

void plot_ship_graphic(void *item)
{
	(void)item;
}

void plot_smoke(void *item)
{
	(void)item;
}

void plot_laser(void *item)
{
	(void)item;
}

void plot_bit_graphic(void *item)
{
	(void)item;
}

void plot_static_from_grid(void *item)
{
	(void)item;
}

int main(void)
{
	static const int32_t visible[8] = { -160, -120, 160, -120, 160, 120, -160, 120 };
	static const int32_t off_right[8] = { 161, -1, 162, -1, 162, 1, 161, 1 };
	static const int32_t reversed[8] = { -160, -120, -160, 120, 160, 120, 160, -120 };
	static const int32_t degenerate[8] = { 0, 0, 1, 0, 2, 0, 0, 1 };
	static long rotated[2245 * 3];
	static long screen[2245 * 2];
	static uint8_t heights[256 * 256];
	static long perspective[1];
	static long quick_heights[256 * 4];
	static long cosine[1024];
	static long misc[46];

	assert(add32(INT32_MAX, 1) == INT32_MIN);
	assert(sub32(INT32_MIN, 1) == INT32_MAX);
	assert(mul32(INT32_MAX, 2) == -2);
	assert(asr32(-3, 1) == -2);
	assert(wrap8(-1) == 255);
#if !defined(SF_WEB_PORT)
	assert(section_is_offscreen(visible) == 0);
	assert(section_is_offscreen(off_right) != 0);
	assert(section_is_backfacing(LAND_SECTION_HIGH, visible) == 0);
	assert(section_is_backfacing(LAND_SECTION_MID, visible) != 0);
	assert(section_is_backfacing(LAND_SECTION_HIGH, reversed) != 0);
	assert(section_is_backfacing(LAND_SECTION_LOW, reversed) == 0);
	assert(section_is_backfacing(LAND_SECTION_MID, degenerate) != 0);
#endif

	memset(heights, 18, sizeof(heights));
	memset(cosine, 0, sizeof(cosine));
	land.landscape_heights = heights;
	quick_heights[0] = 64;
	land.rotated_coords = rotated;
	land.screen_coords = screen;
	land.landscape_heights = heights;
	land.perspective_table = perspective;
	land.quick_height_table = quick_heights;
	misc[11] = 110;
	misc[12] = 210;
	misc[13] = 1000;
	wave_counter = 1008;
	fast_rotation(misc, 0);
#if defined(SF_WEB_PORT)
	assert(terrain_frame_count == 1);
	assert(terrain_map_x == 14);
	assert(terrain_map_y == 114);
	assert(terrain_wave_phase == 1008);
	assert(height_at(100, 100) == 18);
#else
	assert(rotated[1089 * 3] == 999);
	set_rotated(0, 0, 1, 0);
	set_rotated(1, 0, 1, 0);
	set_rotated(33, 0, 1, 0);
	set_rotated(34, 0, 1, 0);
	assert(section_has_projectable_vertices(LAND_SECTION_HIGH, 0) != 0);
	set_rotated(34, 0, 0, 0);
	assert(section_has_projectable_vertices(LAND_SECTION_HIGH, 0) == 0);
#endif
	return 0;
}

#endif
