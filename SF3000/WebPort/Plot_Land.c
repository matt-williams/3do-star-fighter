#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "../SFlib/Plot_Graphic.h"
#include "../SFlib/SF_ARMCell.h"
#include "../SFlib/SF_ARMLink.h"
#if defined(SF_WEB_PORT)
#include "SF_ARMCell_portable.h"
#endif
#include "sf_web_world_renderer.h"

extern long test_mode;
extern void plot_static_from_grid(void *);
extern zsort_list plot_list;

long wave_counter;
long wave_counter2;
long wave_counter3;
long land_sort_offset;

typedef struct land_cel_prefix {
	long temp_cels;
	long x_pos0;
	long y_pos0;
	long x_pos1;
	long y_pos1;
	long x_pos2;
	long y_pos2;
	long x_pos3;
	long y_pos3;
	void *plotlist;
	long shade;
} land_cel_prefix;

typedef struct land_context {
	long *rotated_coords;
	long *screen_coords;
	uint8_t *landscape_heights;
	long *perspective_table;
	long *quick_height_table;
	uint8_t *sprite_map;
	void *cel_quad;
	uintptr_t space_mission;
	int32_t resolution;
	int32_t land_x_pos;
	int32_t land_y_pos;
	int32_t land_x_pos_outer;
	int32_t land_y_pos_outer;
	int32_t land_x_pos_outer_2;
	int32_t land_y_pos_outer_2;
	int32_t land_x_pos_outer_3;
	int32_t land_y_pos_outer_3;
	int32_t land_x_pos_outer_4;
	int32_t land_y_pos_outer_4;
	int32_t land_x_pos_outer_5;
	int32_t land_y_pos_outer_5;
	int32_t land_x_pos_outer_6;
	int32_t land_y_pos_outer_6;
	int32_t land_x_pos_outer_7;
	int32_t land_y_pos_outer_7;
} land_context;

typedef enum land_section_kind {
	LAND_SECTION_HIGH,
	LAND_SECTION_MID,
	LAND_SECTION_LOW,
	LAND_SECTION_FLAT
} land_section_kind;

#define WEB_TERRAIN_GRID_OFFSET 96

static land_context land;
static int32_t plot_list_pointer;
static int32_t plot_res_skip_middle;
static int32_t hill_shade_offset;
static int32_t low_land_size;

static int32_t i32(uint32_t value)
{
	if (value <= INT32_MAX)
		return (int32_t)value;
	return (int32_t)((int64_t)value - ((int64_t)UINT32_MAX + 1));
}

static int32_t long_i32(long value)
{
	return i32((uint32_t)value);
}

static long i32_long(int32_t value)
{
	return (long)value;
}

static int32_t add32(int32_t left, int32_t right)
{
	return i32((uint32_t)left + (uint32_t)right);
}

static int32_t sub32(int32_t left, int32_t right)
{
	return i32((uint32_t)left - (uint32_t)right);
}

static int32_t mul32(int32_t left, int32_t right)
{
	return i32((uint32_t)left * (uint32_t)right);
}

static int32_t lsl32(int32_t value, unsigned int shift)
{
	if (shift >= 32)
		return 0;
	return i32((uint32_t)value << shift);
}

static int32_t asr32(int32_t value, unsigned int shift)
{
	uint32_t bits;

	if (shift == 0)
		return value;
	if (shift >= 32)
		return value < 0 ? -1 : 0;

	bits = (uint32_t)value >> shift;
	if (value < 0)
		bits |= UINT32_MAX << (32 - shift);
	return i32(bits);
}

#if !defined(SF_WEB_PORT)
static int32_t neg32(int32_t value)
{
	return sub32(0, value);
}

static int32_t abs32(int32_t value)
{
	return value < 0 ? neg32(value) : value;
}
#endif

static int32_t wrap8(int32_t value)
{
	return (int32_t)((uint32_t)value & 255U);
}

static int32_t rotated_at(int32_t node, int32_t component)
{
	return long_i32(land.rotated_coords[(size_t)node * 3U + (size_t)component]);
}

static void set_rotated(int32_t node, int32_t x, int32_t y, int32_t z)
{
	size_t offset = (size_t)node * 3U;

	land.rotated_coords[offset] = i32_long(x);
	land.rotated_coords[offset + 1U] = i32_long(y);
	land.rotated_coords[offset + 2U] = i32_long(z);
}

static int32_t screen_at(int32_t node, int32_t component)
{
	return long_i32(land.screen_coords[(size_t)node * 2U + (size_t)component]);
}

static void set_screen(int32_t node, int32_t x, int32_t y)
{
	size_t offset = (size_t)node * 2U;

	land.screen_coords[offset] = i32_long(x);
	land.screen_coords[offset + 1U] = i32_long(y);
}

static int32_t height_at(int32_t x, int32_t y)
{
	return (int32_t)land.landscape_heights[(size_t)wrap8(x) +
		((size_t)wrap8(y) << 8)];
}

static void quick_get(int32_t height, int32_t *x, int32_t *y, int32_t *z)
{
	size_t offset = (size_t)height * 4U;

	*x = long_i32(land.quick_height_table[offset]);
	*y = long_i32(land.quick_height_table[offset + 1U]);
	*z = long_i32(land.quick_height_table[offset + 2U]);
}

static void quick_set(int32_t height, int32_t x, int32_t y, int32_t z)
{
	size_t offset = (size_t)height * 4U;

	land.quick_height_table[offset] = i32_long(x);
	land.quick_height_table[offset + 1U] = i32_long(y);
	land.quick_height_table[offset + 2U] = i32_long(z);
}

static void build_quick_height_table(void)
{
	int32_t step_x;
	int32_t step_y;
	int32_t step_z;
	int32_t x;
	int32_t y;
	int32_t z;
	int32_t odd_x;
	int32_t odd_y;
	int32_t odd_z;
	int32_t loop;

	quick_get(0, &step_x, &step_y, &step_z);

	x = 0;
	y = 0;
	z = 0;
	for (loop = 0; loop < 119; ++loop)
	{
		odd_x = add32(x, step_x);
		odd_y = add32(y, step_y);
		odd_z = add32(z, step_z);
		quick_set(17 + loop * 2, x, y, z);
		quick_set(18 + loop * 2, odd_x, odd_y, odd_z);
		x = add32(odd_x, step_x);
		y = add32(odd_y, step_y);
		z = add32(odd_z, step_z);
	}
	quick_set(255, x, y, z);

	x = i32(~(uint32_t)lsl32(step_x, 2));
	y = i32(~(uint32_t)lsl32(step_y, 2));
	z = i32(~(uint32_t)lsl32(step_z, 2));
	for (loop = 0; loop < 8; ++loop)
	{
		odd_x = add32(x, asr32(step_x, 1));
		odd_y = add32(y, asr32(step_y, 1));
		odd_z = add32(z, asr32(step_z, 1));
		quick_set(loop * 2, x, y, z);
		quick_set(loop * 2 + 1, odd_x, odd_y, odd_z);
		x = add32(odd_x, asr32(step_x, 1));
		y = add32(odd_y, asr32(step_y, 1));
		z = add32(odd_z, asr32(step_z, 1));
	}
	quick_set(16, x, y, z);
}

static int32_t fixed_outer_height(int32_t x, int32_t y, int32_t node_x,
	int32_t node_y, int edge_fix)
{
	if (edge_fix && (node_y == 0 || node_y == 16) && (x & 4) != 0)
		return asr32(add32(height_at(sub32(x, 4), y),
			height_at(add32(x, 4), y)), 1);

	if (edge_fix && (node_x == 0 || node_x == 16) && (y & 4) != 0)
		return asr32(add32(height_at(x, sub32(y, 4)),
			height_at(x, add32(y, 4))), 1);

	return height_at(x, y);
}

static void rotate_height_grid(int32_t first_node, int32_t start_x,
	int32_t start_y, int32_t offset_x, int32_t offset_y, int32_t offset_z,
	int32_t horizontal_x, int32_t horizontal_y, int32_t horizontal_z,
	int32_t vertical_x, int32_t vertical_y, int32_t vertical_z,
	int32_t map_step, unsigned int height_shift, int skip_middle, int edge_fix)
{
	int32_t node_y;
	int32_t node_x;
	int32_t grid_x;
	int32_t grid_y;
	int32_t height;
	int32_t height_x;
	int32_t height_y;
	int32_t height_z;
	int32_t row_x = offset_x;
	int32_t row_y = offset_y;
	int32_t row_z = offset_z;

	for (node_y = 0; node_y <= 16; ++node_y)
	{
		grid_y = wrap8(add32(start_y, mul32(node_y, map_step)));
		for (node_x = 0; node_x <= 16; ++node_x)
		{
			if (skip_middle == 0 && node_y >= 6 && node_y <= 10 &&
				node_x >= 6 && node_x <= 10)
				continue;

			grid_x = wrap8(add32(start_x, mul32(node_x, map_step)));
			height = fixed_outer_height(grid_x, grid_y, node_x, node_y, edge_fix);
			quick_get(height, &height_x, &height_y, &height_z);
			set_rotated(first_node + node_y * 17 + node_x,
				sub32(add32(row_x, mul32(node_x, lsl32(horizontal_x, 2))),
					asr32(height_x, height_shift)),
				sub32(add32(row_y, mul32(node_x, lsl32(horizontal_y, 2))),
					asr32(height_y, height_shift)),
				sub32(add32(row_z, mul32(node_x, lsl32(horizontal_z, 2))),
					asr32(height_z, height_shift)));
		}
		row_x = add32(row_x, lsl32(vertical_x, 2));
		row_y = add32(row_y, lsl32(vertical_y, 2));
		row_z = add32(row_z, lsl32(vertical_z, 2));
	}
}

static void rotate_high_grid(int32_t start_x, int32_t start_y,
	int32_t offset_x, int32_t offset_y, int32_t offset_z,
	int32_t horizontal_x, int32_t horizontal_y, int32_t horizontal_z,
	int32_t vertical_x, int32_t vertical_y, int32_t vertical_z)
{
	int32_t node_y;
	int32_t node_x;
	int32_t grid_x;
	int32_t grid_y;
	int32_t height;
	int32_t height_x;
	int32_t height_y;
	int32_t height_z;
	int32_t row_x = offset_x;
	int32_t row_y = offset_y;
	int32_t row_z = offset_z;

	for (node_y = 0; node_y <= 32; ++node_y)
	{
		grid_y = wrap8(add32(start_y, node_y));
		for (node_x = 0; node_x <= 32; ++node_x)
		{
			grid_x = wrap8(add32(start_x, node_x));
			height = height_at(grid_x, grid_y);
			quick_get(height, &height_x, &height_y, &height_z);
			set_rotated(node_y * 33 + node_x,
				sub32(add32(row_x, mul32(node_x, horizontal_x)),
					asr32(height_x, 6)),
				sub32(add32(row_y, mul32(node_x, horizontal_y)),
					asr32(height_y, 6)),
				sub32(add32(row_z, mul32(node_x, horizontal_z)),
					asr32(height_z, 6)));
		}
		row_x = add32(row_x, vertical_x);
		row_y = add32(row_y, vertical_y);
		row_z = add32(row_z, vertical_z);
	}
}

static void rotate_flat_grid(int32_t first_node, int32_t offset_x,
	int32_t offset_y, int32_t offset_z, int32_t horizontal_x,
	int32_t horizontal_y, int32_t horizontal_z, int32_t vertical_x,
	int32_t vertical_y, int32_t vertical_z, int skip_middle)
{
	int32_t node_y;
	int32_t node_x;
	int32_t row_x = offset_x;
	int32_t row_y = offset_y;
	int32_t row_z = offset_z;

	for (node_y = 0; node_y <= 16; ++node_y)
	{
		for (node_x = 0; node_x <= 16; ++node_x)
		{
			if (skip_middle == 0 && node_y >= 6 && node_y <= 10 &&
				node_x >= 6 && node_x <= 10)
				continue;

			set_rotated(first_node + node_y * 17 + node_x,
				add32(row_x, mul32(node_x, lsl32(horizontal_x, 2))),
				add32(row_y, mul32(node_x, lsl32(horizontal_y, 2))),
				add32(row_z, mul32(node_x, lsl32(horizontal_z, 2))));
		}
		row_x = add32(row_x, lsl32(vertical_x, 2));
		row_y = add32(row_y, lsl32(vertical_y, 2));
		row_z = add32(row_z, lsl32(vertical_z, 2));
	}
}

static void make_land_perspective(int32_t resolution)
{
	static const int32_t node_offsets[4] = { 0, 1089, 1378, 1667 };
	static const int32_t node_counts[4] = { 2245, 1445, 1445, 1445 };
	int32_t node;
	int32_t first;
	int32_t last;
	int32_t x;
	int32_t y;
	int32_t z;
	int32_t scale;

	first = node_offsets[resolution];
	last = add32(first, node_counts[resolution]);
	for (node = first; node < last; ++node)
	{
		x = rotated_at(node, 0);
		y = rotated_at(node, 1);
		z = rotated_at(node, 2);
		if (y > 0)
		{
			scale = long_i32(land.perspective_table[(uint32_t)y >> 6]);
			x = asr32(mul32(x, scale), 16);
			z = asr32(mul32(z, scale), 16);
		}
		set_screen(node, x, z);
	}
}

void fast_rotation(void *misc_data, long resolution)
{
	long *misc = (long *)misc_data;
	int32_t res = long_i32(resolution);
	int32_t horizontal_x;
	int32_t horizontal_y;
	int32_t horizontal_z;
	int32_t vertical_x;
	int32_t vertical_y;
	int32_t vertical_z;

	if (res < 0 || res > 3)
		return;

	land.resolution = res;
	build_quick_height_table();

	horizontal_x = long_i32(misc[5]);
	horizontal_y = long_i32(misc[6]);
	horizontal_z = long_i32(misc[7]);
	vertical_x = long_i32(misc[8]);
	vertical_y = long_i32(misc[9]);
	vertical_z = long_i32(misc[10]);

#if defined(SF_WEB_PORT)
	/*
	 * The browser owns terrain projection.  Preserve the fixed-point height
	 * table for the shader, but avoid rotating/projecting legacy terrain grids.
	 */
	land.land_x_pos_outer = long_i32(misc[11]);
	land.land_y_pos_outer = long_i32(misc[12]);
	sf_web_world_renderer_set_terrain_frame(
		sub32(land.land_x_pos_outer, WEB_TERRAIN_GRID_OFFSET),
		sub32(land.land_y_pos_outer, WEB_TERRAIN_GRID_OFFSET),
		sub32(sub32(long_i32(misc[13]),
			mul32(WEB_TERRAIN_GRID_OFFSET, horizontal_x)),
			mul32(WEB_TERRAIN_GRID_OFFSET, vertical_x)),
		sub32(sub32(long_i32(misc[14]),
			mul32(WEB_TERRAIN_GRID_OFFSET, horizontal_y)),
			mul32(WEB_TERRAIN_GRID_OFFSET, vertical_y)),
		sub32(sub32(long_i32(misc[15]),
			mul32(WEB_TERRAIN_GRID_OFFSET, horizontal_z)),
			mul32(WEB_TERRAIN_GRID_OFFSET, vertical_z)),
		horizontal_x, horizontal_y, horizontal_z,
		vertical_x, vertical_y, vertical_z, long_i32(wave_counter));
	return;
#endif

	land.land_x_pos = long_i32(misc[0]);
	land.land_y_pos = long_i32(misc[1]);
	if (res == 0)
		rotate_high_grid(land.land_x_pos, land.land_y_pos,
			long_i32(misc[2]), long_i32(misc[3]), long_i32(misc[4]),
			horizontal_x, horizontal_y, horizontal_z,
			vertical_x, vertical_y, vertical_z);

	land.land_x_pos_outer = long_i32(misc[11]);
	land.land_y_pos_outer = long_i32(misc[12]);
	if (res <= 1)
		rotate_height_grid(1089, land.land_x_pos_outer, land.land_y_pos_outer,
			long_i32(misc[13]), long_i32(misc[14]), long_i32(misc[15]),
			horizontal_x, horizontal_y, horizontal_z,
			vertical_x, vertical_y, vertical_z, 4, 6, res, 1);

	land.land_x_pos_outer_2 = long_i32(misc[16]);
	land.land_y_pos_outer_2 = long_i32(misc[17]);
	if (res <= 2)
		rotate_height_grid(1378, land.land_x_pos_outer_2,
			land.land_y_pos_outer_2, long_i32(misc[18]), long_i32(misc[19]),
			long_i32(misc[20]), horizontal_x, horizontal_y, horizontal_z,
			vertical_x, vertical_y, vertical_z, 8, 7, res == 2, 0);

	land.land_x_pos_outer_3 = long_i32(misc[21]);
	land.land_y_pos_outer_3 = long_i32(misc[22]);
	rotate_flat_grid(1667, long_i32(misc[23]), long_i32(misc[24]),
		long_i32(misc[25]), horizontal_x, horizontal_y, horizontal_z,
		vertical_x, vertical_y, vertical_z, res == 3);

	land.land_x_pos_outer_4 = long_i32(misc[26]);
	land.land_y_pos_outer_4 = long_i32(misc[27]);
	rotate_flat_grid(1956, long_i32(misc[28]), long_i32(misc[29]),
		long_i32(misc[30]), horizontal_x, horizontal_y, horizontal_z,
		vertical_x, vertical_y, vertical_z, 0);

	if (res > 0)
	{
		land.land_x_pos_outer_5 = long_i32(misc[31]);
		land.land_y_pos_outer_5 = long_i32(misc[32]);
		rotate_flat_grid(2245, long_i32(misc[33]), long_i32(misc[34]),
			long_i32(misc[35]), horizontal_x, horizontal_y, horizontal_z,
			vertical_x, vertical_y, vertical_z, 0);
	}

	if (res > 1)
	{
		land.land_x_pos_outer_6 = long_i32(misc[36]);
		land.land_y_pos_outer_6 = long_i32(misc[37]);
		rotate_flat_grid(2534, long_i32(misc[38]), long_i32(misc[39]),
			long_i32(misc[40]), horizontal_x, horizontal_y, horizontal_z,
			vertical_x, vertical_y, vertical_z, 0);
	}

	if (res > 2)
	{
		land.land_x_pos_outer_7 = long_i32(misc[41]);
		land.land_y_pos_outer_7 = long_i32(misc[42]);
		rotate_flat_grid(2823, long_i32(misc[43]), long_i32(misc[44]),
			long_i32(misc[45]), horizontal_x, horizontal_y, horizontal_z,
			vertical_x, vertical_y, vertical_z, 0);
	}

	make_land_perspective(res);
}

static void set_cel_quad(const int32_t points[8], int32_t shade)
{
	land_cel_prefix *cel = (land_cel_prefix *)land.cel_quad;

	cel->x_pos0 = i32_long(points[0]);
	cel->y_pos0 = i32_long(points[1]);
	cel->x_pos1 = i32_long(points[2]);
	cel->y_pos1 = i32_long(points[3]);
	cel->x_pos2 = i32_long(points[4]);
	cel->y_pos2 = i32_long(points[5]);
	cel->x_pos3 = i32_long(points[6]);
	cel->y_pos3 = i32_long(points[7]);
	cel->shade = i32_long(shade);
}

static void load_section_points(land_section_kind kind, int32_t node,
	int32_t points[8])
{
	if (kind == LAND_SECTION_HIGH)
	{
		int32_t stride = 33;
		points[0] = screen_at(node + stride, 0);
		points[1] = screen_at(node + stride, 1);
		points[2] = screen_at(node + stride + 1, 0);
		points[3] = screen_at(node + stride + 1, 1);
		points[4] = screen_at(node + 1, 0);
		points[5] = screen_at(node + 1, 1);
		points[6] = screen_at(node, 0);
		points[7] = screen_at(node, 1);
	}
	else
	{
		points[0] = screen_at(node, 0);
		points[1] = screen_at(node, 1);
		points[2] = screen_at(node + 1, 0);
		points[3] = screen_at(node + 1, 1);
		points[4] = screen_at(node + 18, 0);
		points[5] = screen_at(node + 18, 1);
		points[6] = screen_at(node + 17, 0);
		points[7] = screen_at(node + 17, 1);
	}
}

static void load_section_view_points(land_section_kind kind, int32_t node,
	int32_t view_x[4], int32_t view_y[4], int32_t view_z[4])
{
	static const int32_t high_offsets[4] = { 33, 34, 1, 0 };
	static const int32_t other_offsets[4] = { 0, 1, 18, 17 };
	const int32_t *offsets = kind == LAND_SECTION_HIGH ? high_offsets :
		other_offsets;
	int32_t point;

	for (point = 0; point < 4; ++point)
	{
		int32_t point_node = node + offsets[point];

		view_x[point] = rotated_at(point_node, 0);
		view_y[point] = rotated_at(point_node, 1);
		view_z[point] = rotated_at(point_node, 2);
	}
}

#if !defined(SF_WEB_PORT)
static int coarse_section_visible(land_section_kind kind, int32_t node)
{
	int32_t y = rotated_at(node, 1);
	int32_t limit;
	int32_t horizontal_limit;
	int32_t other_limit;
	int32_t stride = kind == LAND_SECTION_HIGH ? 33 : 17;

	if (kind == LAND_SECTION_HIGH)
	{
		if (add32(y, 8192) < 0)
			return 0;
		limit = add32(y, 10240);
		other_limit = -8192;
	}
	else if (kind == LAND_SECTION_MID)
	{
		if (y < 8192 * 4)
			return 0;
		limit = add32(y, 10240 * 4);
		other_limit = 8192 * 4;
	}
	else
	{
		if (y < 8192)
			return 0;
		limit = add32(y, 10240 * 4);
		other_limit = 8192;
	}

	horizontal_limit = abs32(rotated_at(node, 0));
	if (limit < horizontal_limit)
		return 0;
	horizontal_limit = abs32(rotated_at(node, 2));
	if (limit < horizontal_limit)
		return 0;

	if ((kind == LAND_SECTION_HIGH && limit > 0) ||
		(kind == LAND_SECTION_MID && limit > 0) ||
		((kind == LAND_SECTION_LOW || kind == LAND_SECTION_FLAT) &&
			limit > 8192))
		return 1;

	if (kind == LAND_SECTION_HIGH)
	{
		if (add32(rotated_at(node + 1, 1), 8192) < 0 ||
			add32(rotated_at(node + stride, 1), 8192) < 0 ||
			add32(rotated_at(node + stride + 1, 1), 8192) < 0)
			return 0;
	}
	else if (rotated_at(node + 1, 1) < other_limit ||
		rotated_at(node + stride, 1) < other_limit ||
		rotated_at(node + stride + 1, 1) < other_limit)
	{
		return 0;
	}

	return 1;
}
#endif

static int section_has_projectable_vertices(land_section_kind kind,
	int32_t node)
{
	int32_t stride = kind == LAND_SECTION_HIGH ? 33 : 17;

	return rotated_at(node, 1) > 0 &&
		rotated_at(node + 1, 1) > 0 &&
		rotated_at(node + stride, 1) > 0 &&
		rotated_at(node + stride + 1, 1) > 0;
}

#if !defined(SF_WEB_PORT)
static int section_is_offscreen(const int32_t points[8])
{
	return (points[1] > 120 && points[3] > 120 &&
			points[5] > 120 && points[7] > 120) ||
		(points[1] < -120 && points[3] < -120 &&
			points[5] < -120 && points[7] < -120) ||
		(points[0] < -160 && points[2] < -160 &&
			points[4] < -160 && points[6] < -160) ||
		(points[0] > 160 && points[2] > 160 &&
			points[4] > 160 && points[6] > 160);
}

static int32_t vector_sign(const int32_t points[8], int first)
{
	if (first)
	{
		return sub32(
			mul32(sub32(points[2], points[0]), sub32(points[5], points[3])),
			mul32(sub32(points[4], points[2]), sub32(points[3], points[1])));
	}

	return sub32(
		mul32(sub32(points[6], points[4]), sub32(points[1], points[7])),
		mul32(sub32(points[0], points[6]), sub32(points[7], points[5])));
}

static int section_is_backfacing(land_section_kind kind,
	const int32_t points[8])
{
	int32_t first = vector_sign(points, 1);

	if (kind == LAND_SECTION_HIGH)
		return first < 0 && vector_sign(points, 0) < 0;

	return first >= 0 && vector_sign(points, 0) >= 0;
}
#endif

static int32_t section_shade(land_section_kind kind, int32_t x, int32_t y,
	int32_t height)
{
	int32_t x_step;
	int32_t y_step;
	int32_t shade;

	if (kind == LAND_SECTION_HIGH)
	{
		x_step = 1;
		y_step = 1;
	}
	else if (kind == LAND_SECTION_MID)
	{
		x_step = 4;
		y_step = 4;
	}
	else
	{
		x_step = 8;
		y_step = 8;
	}

	shade = add32(sub32(height, height_at(add32(x, x_step), y)),
		sub32(height, height_at(x, add32(y, y_step))));
	if (kind == LAND_SECTION_HIGH && height <= 16)
		shade = sub32(shade, asr32(shade, 1));

	if (kind == LAND_SECTION_HIGH)
		shade = asr32(shade, 1);
	else if (kind == LAND_SECTION_MID)
		shade = asr32(shade, 3);
	else
		shade = asr32(shade, 4);

	return add32(shade, hill_shade_offset);
}

static void plot_land_section(land_section_kind kind, int32_t node,
	int32_t grid_x, int32_t grid_y, int32_t flat_shade, int32_t flat_res)
{
	int32_t points[8];
	int32_t height;
	int32_t shade;
#if !defined(SF_WEB_PORT)
	int is_backfacing;
#endif
	uint32_t command_count;
	int32_t view_x[4];
	int32_t view_y[4];
	int32_t view_z[4];

	/*
	 * The original ARM path leaves nodes behind the camera unprojected.
	 * That is harmless only while the section's anchor keeps every corner
	 * in front of the camera; the wide high-altitude LOD cells can straddle
	 * the camera plane and otherwise produce invalid screen-space CELs.
	 */
	if (!section_has_projectable_vertices(kind, node))
		return;

	if (kind == LAND_SECTION_HIGH)
	{
		grid_x = wrap8(grid_x);
		grid_y = wrap8(grid_y);
		height = height_at(grid_x, grid_y);
	}
	else if (kind == LAND_SECTION_FLAT)
	{
		grid_x = wrap8(grid_x);
		grid_y = wrap8(grid_y);
		height = height_at(grid_x, grid_y);
	}
	else
	{
		grid_x = (int32_t)((uint32_t)grid_x & 252U);
		grid_y = (int32_t)((uint32_t)grid_y & 252U);
		height = height_at(grid_x, grid_y);
	}

#if defined(SF_WEB_PORT)
	/*
	 * The browser terrain pass samples this map directly and derives water
	 * waves in its vertex shader. Do not regenerate a CEL/world quad here.
	 */
	return;
#endif

	load_section_points(kind, node, points);
#if !defined(SF_WEB_PORT)
	is_backfacing = kind != LAND_SECTION_FLAT &&
		section_is_backfacing(kind, points);
	if (is_backfacing || section_is_offscreen(points))
		return;
#endif

	if (kind == LAND_SECTION_FLAT)
		shade = flat_shade;
	else
		shade = section_shade(kind, grid_x, grid_y, height);
	set_cel_quad(points, shade);
	command_count = sf_web_renderer_command_count();

	if (kind == LAND_SECTION_HIGH)
	{
		arm_addpolycel16(land.cel_quad,
			(long)land.sprite_map[(size_t)grid_x + ((size_t)grid_y << 8)]);
	}
	else if (kind == LAND_SECTION_MID)
	{
		arm_add4cel4(land.cel_quad, land.sprite_map,
			(long)grid_x, (long)grid_y);
	}
	else if (kind == LAND_SECTION_LOW)
	{
		arm_addcelfrom512map(land.cel_quad,
			(long)grid_x, (long)grid_y, (long)low_land_size);
	}
	else if (flat_res == 1)
	{
		arm_addcelfrom128map(land.cel_quad,
			(long)grid_x, (long)grid_y, (long)low_land_size);
	}
	else if (flat_res < 1)
	{
		arm_addcelfrom32map(land.cel_quad,
			(long)grid_x, (long)grid_y, (long)low_land_size);
	}
	else
	{
		arm_addcelfrom512map(land.cel_quad,
			(long)grid_x, (long)grid_y, (long)low_land_size);
	}

	if (sf_web_renderer_command_count() != command_count)
	{
		load_section_view_points(kind, node, view_x, view_y, view_z);
		sf_web_world_renderer_append_last_quad(view_x, view_y, view_z);
	}
}

#if defined(SF_WEB_PORT)
static void prepare_web_terrain_materials(void)
{
	SFWebRenderQuad material;
	uint8_t collected[SF_WEB_TERRAIN_TILE_COUNT] = { 0 };
	int32_t full_collection;
	size_t map_offset;
	uint32_t tile;

	if (!sf_web_world_renderer_terrain_material_collection_required() ||
		land.sprite_map == NULL)
		return;

	full_collection =
		sf_web_world_renderer_terrain_material_full_collection_required();
	if (full_collection != 0)
	{
		for (map_offset = 0; map_offset <
			SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION *
			SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION; ++map_offset)
		{
			tile = land.sprite_map[map_offset];
			collected[tile] = 1;
		}
	}
	for (tile = 0; tile < SF_WEB_TERRAIN_TILE_COUNT; ++tile)
	{
		if (collected[tile] == 0 &&
			!sf_web_world_renderer_terrain_tile_material_required(tile))
			continue;
		material = (SFWebRenderQuad){ 0 };
		if (sf_armcell_terrain_material(land.cel_quad, (long)tile,
			&material) == 0)
			(void)sf_web_world_renderer_set_terrain_tile_material(tile,
				&material);
	}
	sf_web_world_renderer_finish_terrain_material_collection();
}
#endif

static void plot_sorted_graphics(int32_t search_to)
{
	zsort_item *item;
	int32_t reference;

	search_to = add32(search_to, long_i32(land_sort_offset));
	if (search_to < 0)
		search_to = 0;
	if (search_to > 127)
		search_to = 127;

	while (plot_list_pointer >= search_to)
	{
		item = (zsort_item *)plot_list.offset[plot_list_pointer];
		plot_list.offset[plot_list_pointer] = 0;
		--plot_list_pointer;

		while (item != 0)
		{
			reference = long_i32(item->ref);
			switch (reference)
			{
			case 0:
				plot_ship_graphic(item->graphic_address);
				break;
			case 1:
				plot_smoke(item->graphic_address);
				break;
			case 2:
				plot_laser(item->graphic_address);
				break;
			case 3:
				plot_bit_graphic(item->graphic_address);
				break;
			case 4:
				plot_static_from_grid(item->graphic_address);
				break;
			default:
				break;
			}
			item = (zsort_item *)item->next_address;
		}
	}
}

static void plot_sorted_land_grid(land_section_kind kind, int32_t first_node,
	int32_t start_x, int32_t start_y, int32_t step, int32_t initial_shade,
	int skip_middle)
{
	int32_t top_left = first_node;
	int32_t high_detail = kind == LAND_SECTION_HIGH;
	int32_t top_right;
	int32_t bottom_left;
	int32_t bottom_right;
	int32_t outer;
	int32_t stride;

	top_right = first_node + (high_detail ? 31 : 15);
	bottom_left = high_detail ? top_right +
		992 :
		first_node + 255;
	bottom_right = bottom_left + (high_detail ? 31 : 15);
	outer = high_detail ? 31 : 15;
	stride = high_detail ? 33 : 17;
	int32_t x = start_x;
	int32_t y = start_y;
	int32_t offset;
	int32_t layer_count;

	hill_shade_offset = initial_shade;
	for (;;)
	{
		plot_land_section(kind, top_left, x, y, 0, 0);
		plot_land_section(kind, top_right, add32(x, mul32(outer, step)), y, 0, 0);
		plot_land_section(kind, bottom_left, x, add32(y, mul32(outer, step)), 0, 0);
		plot_land_section(kind, bottom_right, add32(x, mul32(outer, step)),
			add32(y, mul32(outer, step)), 0, 0);

		layer_count = asr32(outer, 1);
		for (offset = 1; offset <= layer_count; ++offset)
		{
			plot_land_section(kind, top_left + offset,
				add32(x, mul32(offset, step)), y, 0, 0);
			plot_land_section(kind, top_left + offset * stride, x,
				add32(y, mul32(offset, step)), 0, 0);
			plot_land_section(kind, top_right - offset,
				sub32(add32(x, mul32(outer, step)), mul32(offset, step)), y, 0, 0);
			plot_land_section(kind, top_right + offset * stride,
				add32(x, mul32(outer, step)), add32(y, mul32(offset, step)), 0, 0);
			plot_land_section(kind, bottom_left + offset,
				add32(x, mul32(offset, step)), add32(y, mul32(outer, step)), 0, 0);
			plot_land_section(kind, bottom_left - offset * stride, x,
				sub32(add32(y, mul32(outer, step)), mul32(offset, step)), 0, 0);
			plot_land_section(kind, bottom_right - offset,
				sub32(add32(x, mul32(outer, step)), mul32(offset, step)),
				add32(y, mul32(outer, step)), 0, 0);
			plot_land_section(kind, bottom_right - offset * stride,
				add32(x, mul32(outer, step)),
				sub32(add32(y, mul32(outer, step)), mul32(offset, step)), 0, 0);
		}

		if (high_detail)
			plot_sorted_graphics(asr32(outer, 1));
		else if (kind == LAND_SECTION_MID)
			plot_sorted_graphics(lsl32(outer, 1));
		else
			plot_sorted_graphics(lsl32(outer, 2));

		outer -= 2;
		if (high_detail)
		{
			if (outer < 0)
				return;
			if (outer == 17)
				hill_shade_offset = 17;
		}
		else
		{
			if (skip_middle == 0 && outer < 6)
				return;
			if (skip_middle != 0 && outer < 1)
				return;
			if (outer == 9)
				hill_shade_offset = kind == LAND_SECTION_MID ? 13 : 9;
		}

		top_left += stride + 1;
		top_right += stride - 1;
		bottom_left -= stride - 1;
		bottom_right -= stride + 1;
		x = add32(x, step);
		y = add32(y, step);
	}
}

#if !defined(SF_WEB_PORT)
static void plot_flat_grid(int32_t first_node, int32_t start_x, int32_t start_y,
	int32_t step, int32_t shade, int32_t flat_res, int skip_middle)
{
	int32_t y;
	int32_t x;

	for (y = 0; y < 16; ++y)
	{
		for (x = 0; x < 16; ++x)
		{
			if (skip_middle == 0 && y >= 4 && y <= 9 && x >= 5 && x <= 10)
				continue;
			plot_land_section(LAND_SECTION_FLAT, first_node + y * 17 + x,
				add32(start_x, mul32(x, step)), add32(start_y, mul32(y, step)),
				shade, flat_res);
		}
	}
}

static void flat_low_res_sorted_land_plotter(void)
{
	int32_t res = land.resolution;

	plot_res_skip_middle = res == 3;
	low_land_size = 1;
	plot_flat_grid(1667, land.land_x_pos_outer_3, land.land_y_pos_outer_3,
		16, 5, 2, plot_res_skip_middle);

	plot_res_skip_middle = 0;
	low_land_size = 2;
	plot_flat_grid(1956, land.land_x_pos_outer_4, land.land_y_pos_outer_4,
		32, 3, 1, plot_res_skip_middle);

	if (res == 0)
		return;

	low_land_size = 3;
	plot_flat_grid(2245, land.land_x_pos_outer_5, land.land_y_pos_outer_5,
		64, 2, 1, plot_res_skip_middle);

	if (res == 1)
		return;

	low_land_size = 4;
	plot_flat_grid(2534, land.land_x_pos_outer_6, land.land_y_pos_outer_6,
		128, 1, 1, plot_res_skip_middle);

	if (res == 2)
		return;

	low_land_size = 5;
	plot_flat_grid(2823, land.land_x_pos_outer_7, land.land_y_pos_outer_7,
		256, 0, 0, plot_res_skip_middle);
}
#endif

void machine_code_flat_land_plot(void)
{
#if defined(SF_WEB_PORT)
	return;
#else
	if (land.space_mission != 1U)
		flat_low_res_sorted_land_plotter();
#endif
}

void machine_code_land_plot(void)
{
	plot_list_pointer = 127;

	if (long_i32(test_mode) == 1)
		land.space_mission = 1U;

#if defined(SF_WEB_PORT)
	if (land.space_mission == 1U)
	{
		sf_web_world_renderer_disable_terrain();
		plot_sorted_graphics(-257);
		return;
	}
	prepare_web_terrain_materials();
	plot_sorted_graphics(-257);
	return;
#endif

	if (land.space_mission != 1U)
		plot_sorted_graphics(64);
	else
		plot_sorted_graphics(-257);

	plot_res_skip_middle = 0;
	if (land.space_mission == 1U)
		return;

	if (land.resolution == 0)
	{
		low_land_size = 0;
		plot_sorted_land_grid(LAND_SECTION_LOW, 1378,
			land.land_x_pos_outer_2, land.land_y_pos_outer_2, 8, 7,
			plot_res_skip_middle);
		plot_sorted_land_grid(LAND_SECTION_MID, 1089,
			land.land_x_pos_outer, land.land_y_pos_outer, 4, 11,
			plot_res_skip_middle);
		plot_sorted_land_grid(LAND_SECTION_HIGH, 0,
			land.land_x_pos, land.land_y_pos, 1, 15, plot_res_skip_middle);
	}
	else if (land.resolution == 1)
	{
		low_land_size = 0;
		plot_sorted_land_grid(LAND_SECTION_LOW, 1378,
			land.land_x_pos_outer_2, land.land_y_pos_outer_2, 8, 7,
			plot_res_skip_middle);
		plot_res_skip_middle = 1;
		plot_sorted_land_grid(LAND_SECTION_MID, 1089,
			land.land_x_pos_outer, land.land_y_pos_outer, 4, 11,
			plot_res_skip_middle);
	}
	else if (land.resolution == 2)
	{
		low_land_size = 0;
		plot_res_skip_middle = 1;
		plot_sorted_land_grid(LAND_SECTION_LOW, 1378,
			land.land_x_pos_outer_2, land.land_y_pos_outer_2, 8, 7,
			plot_res_skip_middle);
	}

	plot_sorted_graphics(-257);
}

void plot_land_constants(void *constants)
{
	void *const *values = (void *const *)constants;

	/*
	 * Slot 9 is a legacy pointer-valued integer flag, not an address.  It is
	 * intentionally read as uintptr_t so the original wasm32 table ABI stays
	 * intact without truncating host pointers in the other slots.
	 */
	land.rotated_coords = (long *)values[0];
	land.screen_coords = (long *)values[1];
	land.landscape_heights = (uint8_t *)values[2];
#if defined(SF_WEB_PORT)
	sf_web_world_renderer_set_terrain_height_map(land.landscape_heights);
#endif
	land.perspective_table = (long *)values[3];
	land.quick_height_table = (long *)values[4];
	land.sprite_map = (uint8_t *)values[6];
	land.cel_quad = values[7];
	land.space_mission = (uintptr_t)values[9];
#if defined(SF_WEB_PORT)
	sf_web_world_renderer_set_terrain_static_data(land.sprite_map,
		(const int32_t *)land.quick_height_table);
#endif
}
