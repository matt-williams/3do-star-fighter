#if defined(PLOT_LAND_PORT_TEST)

#include <assert.h>
#include <string.h>

#include "Plot_Land.c"

long test_mode;
zsort_list plot_list;

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
	assert(section_is_offscreen(visible) == 0);
	assert(section_is_offscreen(off_right) != 0);
	assert(section_is_backfacing(LAND_SECTION_HIGH, visible) == 0);
	assert(section_is_backfacing(LAND_SECTION_MID, visible) != 0);
	assert(section_is_backfacing(LAND_SECTION_HIGH, reversed) != 0);
	assert(section_is_backfacing(LAND_SECTION_LOW, reversed) == 0);
	assert(section_is_backfacing(LAND_SECTION_MID, degenerate) != 0);

	memset(heights, 18, sizeof(heights));
	quick_heights[0] = 64;
	land.rotated_coords = rotated;
	land.screen_coords = screen;
	land.landscape_heights = heights;
	land.perspective_table = perspective;
	land.quick_height_table = quick_heights;
	land.cosine_table = cosine;
	misc[13] = 1000;
	fast_rotation(misc, 0);
	assert(rotated[1089 * 3] == 999);
	set_rotated(0, 0, 1, 0);
	set_rotated(1, 0, 1, 0);
	set_rotated(33, 0, 1, 0);
	set_rotated(34, 0, 1, 0);
	assert(section_has_projectable_vertices(LAND_SECTION_HIGH, 0) != 0);
	set_rotated(34, 0, 0, 0);
	assert(section_has_projectable_vertices(LAND_SECTION_HIGH, 0) == 0);
	return 0;
}

#endif
