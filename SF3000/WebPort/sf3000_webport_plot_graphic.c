#include "sf3000_webport_renderer.h"
#include "sf_web_world_renderer.h"
#include "sf_web_fixed_step.h"
#include "sf_web_math.h"
#include "../SFlib/Ship_Struct.h"

#include <float.h>
#include <math.h>

#define SF3000_WEBPORT_CLIP_DISTANCE 1280
#define SF3000_WEBPORT_SHIP_HEADER_BYTES 12
#define SF3000_WEBPORT_FLUFFY_CLOUD 58
#define SF3000_WEBPORT_FLUFFY_TREE 62
#define SF3000_WEBPORT_CLOUD_ATLAS_SLOT 20
#define SF3000_WEBPORT_TREE_ATLAS_SLOT 24
#define SF3000_WEBPORT_TARGET_LINE_WIDTH 2

long camera_x_rotation;
long camera_y_rotation;
long camera_z_rotation;
long camera_x_position;
long camera_y_position;
long camera_z_position = 1L << 28;
long camera_x_velocity;
long camera_y_velocity;
long camera_z_velocity;

long air_to_ground_scan_temp;
long air_to_ground_scan;
long air_to_ground_x;
long air_to_ground_y;
long air_to_ground_z;
long atg_selected;

long air_to_air_scan_temp;
long air_to_air_scan;
long air_to_air_x;
long air_to_air_y;
long air_to_air_z;
long ata_selected;

long collision_box_colour_adder;
long planet_1_x_pos;
long planet_1_y_pos;
long planet_1_z_pos;
long planet_2_x_pos;
long planet_2_y_pos;
long planet_2_z_pos;
long silly_x;
long silly_y;
long are_we_in_space_or_wot;

static sf3000_webport_pointer_resolver pointer_resolver;
static const int32_t *perspective_data;
static int32_t *graphic_rotated_data;
static int32_t *graphic_screen_data;
static int32_t *star_data;
static const uint8_t *landscape_heights;
static const uint8_t *poly_map_data;
static const uint8_t *sky_data;
static uint8_t *graphics_data;
static uint8_t *static_graphics_data;
static uint8_t *ships_data;
static uint8_t *explosion_bits_data;
static sf3000_webport_celdata *graphic_cel_quad;
static sf3000_webport_vec3 camera_basis[3];
static sf3000_webport_vec3 static_graphic_rotation[3];
static sf3000_webport_vec3 static_graphic_radar_rotation[3];
static sf3000_webport_vec3 ship_graphic_rotation[3];
static sf3000_webport_vec3 plot_graphic_position;
static int32_t plot_graphic_y_rotation;
static int32_t plot_graphic_z_rotation;
static const uint8_t *graphic_start;
static const uint8_t *ship_to_plot;
static int32_t static_object_type;
static int32_t static_object_grid_ref;
static int32_t plot_group_mask;
static int32_t shade_style;
static int32_t bit_palette = 3;
static int32_t i_am_a_ship;
static int32_t i_am_an_object;
static int32_t atg_truck_target;
static int render_world_geometry;
static long air_to_ground_render_scan;
static long air_to_air_render_scan;
static long collision_box_render_colour_adder;
static int32_t static_graphic_radar_phase;
static double air_to_ground_selection_angle;
static double air_to_air_selection_angle;
static uint64_t air_to_ground_selection_distance;
static uint64_t air_to_air_selection_distance;

extern ship_stack *players_ship;

static void *sf3000_webport_pointer(uint32_t address)
{
	if (address == 0) {
		return NULL;
	}
	return pointer_resolver != NULL ? pointer_resolver(address) :
		   (void *)(uintptr_t)address;
}

static void *sf3000_webport_read_pointer(const void *base, size_t offset)
{
	return sf3000_webport_pointer(
		(uint32_t)sf3000_webport_read_i32(base, offset));
}

void sf3000_webport_set_pointer_resolver(
	sf3000_webport_pointer_resolver resolver)
{
	pointer_resolver = resolver;
}

static int32_t sf3000_webport_cosine(int32_t angle)
{
	return (int32_t)sf_cos_q12((long)angle);
}

static int32_t sf3000_webport_sine(int32_t angle)
{
	return (int32_t)sf_sin_q12((long)angle);
}

static void sf3000_webport_rotate_x(sf3000_webport_vec3 *point,
				     int32_t angle)
{
	int32_t cosine = sf3000_webport_cosine(angle);
	int32_t sine = sf3000_webport_sine(angle);
	int32_t x_cosine = sf3000_webport_asr32(
		sf3000_webport_mul32(point->x, cosine), 12);
	int32_t y_sine = sf3000_webport_asr32(
		sf3000_webport_mul32(point->y, sine), 12);
	int32_t x_sine = sf3000_webport_asr32(
		sf3000_webport_mul32(point->x, sine), 12);
	int32_t y_cosine = sf3000_webport_asr32(
		sf3000_webport_mul32(point->y, cosine), 12);

	point->x = sf3000_webport_sub32(x_cosine, y_sine);
	point->y = sf3000_webport_add32(x_sine, y_cosine);
}

static void sf3000_webport_rotate_y(sf3000_webport_vec3 *point,
				     int32_t angle)
{
	int32_t cosine = sf3000_webport_cosine(angle);
	int32_t sine = sf3000_webport_sine(angle);
	int32_t y_cosine = sf3000_webport_asr32(
		sf3000_webport_mul32(point->y, cosine), 12);
	int32_t z_sine = sf3000_webport_asr32(
		sf3000_webport_mul32(point->z, sine), 12);
	int32_t y_sine = sf3000_webport_asr32(
		sf3000_webport_mul32(point->y, sine), 12);
	int32_t z_cosine = sf3000_webport_asr32(
		sf3000_webport_mul32(point->z, cosine), 12);

	point->y = sf3000_webport_sub32(y_cosine, z_sine);
	point->z = sf3000_webport_add32(y_sine, z_cosine);
}

static void sf3000_webport_rotate_z(sf3000_webport_vec3 *point,
				     int32_t angle)
{
	int32_t cosine = sf3000_webport_cosine(angle);
	int32_t sine = sf3000_webport_sine(angle);
	int32_t x_cosine = sf3000_webport_asr32(
		sf3000_webport_mul32(point->x, cosine), 12);
	int32_t z_sine = sf3000_webport_asr32(
		sf3000_webport_mul32(point->z, sine), 12);
	int32_t x_sine = sf3000_webport_asr32(
		sf3000_webport_mul32(point->x, sine), 12);
	int32_t z_cosine = sf3000_webport_asr32(
		sf3000_webport_mul32(point->z, cosine), 12);

	point->x = sf3000_webport_sub32(x_cosine, z_sine);
	point->z = sf3000_webport_add32(x_sine, z_cosine);
}

static void sf3000_webport_rotate_camera_angles(sf3000_webport_vec3 *point)
{
	sf3000_webport_rotate_x(point,
				sf3000_webport_i32(camera_x_rotation));
	sf3000_webport_rotate_y(point,
				sf3000_webport_i32(camera_y_rotation));
	sf3000_webport_rotate_z(point,
				sf3000_webport_i32(camera_z_rotation));
}

static void sf3000_webport_build_camera_basis(void)
{
	int32_t axis;

	for (axis = 0; axis < 3; ++axis) {
		camera_basis[axis].x = axis == 0 ? 4096 : 0;
		camera_basis[axis].y = axis == 1 ? 4096 : 0;
		camera_basis[axis].z = axis == 2 ? 4096 : 0;
		sf3000_webport_rotate_camera_angles(&camera_basis[axis]);
	}
}

void sf3000_webport_rotate_camera(sf3000_webport_vec3 *point)
{
	int32_t x = point->x;
	int32_t y = point->y;
	int32_t z = point->z;
	int32_t result_x;
	int32_t result_y;
	int32_t result_z;

	result_x = sf3000_webport_mul32(x, camera_basis[0].x);
	result_y = sf3000_webport_mul32(x, camera_basis[0].y);
	result_z = sf3000_webport_mul32(x, camera_basis[0].z);
	result_x = sf3000_webport_add32(
		result_x, sf3000_webport_mul32(y, camera_basis[1].x));
	result_y = sf3000_webport_add32(
		result_y, sf3000_webport_mul32(y, camera_basis[1].y));
	result_z = sf3000_webport_add32(
		result_z, sf3000_webport_mul32(y, camera_basis[1].z));
	result_x = sf3000_webport_add32(
		result_x, sf3000_webport_mul32(z, camera_basis[2].x));
	result_y = sf3000_webport_add32(
		result_y, sf3000_webport_mul32(z, camera_basis[2].y));
	result_z = sf3000_webport_add32(
		result_z, sf3000_webport_mul32(z, camera_basis[2].z));
	point->x = sf3000_webport_asr32(result_x, 12);
	point->y = sf3000_webport_asr32(result_y, 12);
	point->z = sf3000_webport_asr32(result_z, 12);
}

int32_t sf3000_webport_perspective_scale(int32_t depth, int high_resolution)
{
	int32_t index;

	if (high_resolution) {
		index = sf3000_webport_asr32(depth & ~31, 5);
		return sf3000_webport_i32(pex_table_near[index]);
	}
	index = sf3000_webport_asr32(depth & ~63, 6);
	return perspective_data != NULL ? perspective_data[index] :
	       sf3000_webport_i32(pex_table[index]);
}

void sf3000_webport_set_quad(sf3000_webport_celdata *quad,
			      const int32_t points[8], int32_t shade)
{
	quad->x_pos0 = sf3000_webport_long(points[0]);
	quad->y_pos0 = sf3000_webport_long(points[1]);
	quad->x_pos1 = sf3000_webport_long(points[2]);
	quad->y_pos1 = sf3000_webport_long(points[3]);
	quad->x_pos2 = sf3000_webport_long(points[4]);
	quad->y_pos2 = sf3000_webport_long(points[5]);
	quad->x_pos3 = sf3000_webport_long(points[6]);
	quad->y_pos3 = sf3000_webport_long(points[7]);
	quad->shade = sf3000_webport_long(shade);
}

void sf3000_webport_clip_vec3(sf3000_webport_vec3 *first,
			       sf3000_webport_vec3 *second)
{
	sf3000_webport_vec3 near_point = *first;
	sf3000_webport_vec3 far_point = *second;
	int32_t scaler;

	if (near_point.y >= far_point.y) {
		sf3000_webport_vec3 swap = near_point;

		near_point = far_point;
		far_point = swap;
	}

	scaler = sf3000_webport_div32(
		sf3000_webport_lsl32(
			sf3000_webport_sub32(far_point.y,
					     SF3000_WEBPORT_CLIP_DISTANCE),
			10),
		sf3000_webport_sub32(far_point.y, near_point.y));
	near_point.x = sf3000_webport_sub32(
		far_point.x,
		sf3000_webport_asr32(
			sf3000_webport_mul32(
				sf3000_webport_sub32(far_point.x, near_point.x),
				scaler),
			10));
	near_point.z = sf3000_webport_sub32(
		far_point.z,
		sf3000_webport_asr32(
			sf3000_webport_mul32(
				sf3000_webport_sub32(far_point.z, near_point.z),
				scaler),
			10));
	near_point.y = SF3000_WEBPORT_CLIP_DISTANCE;
	*first = near_point;
	*second = far_point;
}

void clip_3d_line(long *x0, long *y0, long *z0, long *x1, long *y1,
		  long *z1)
{
	sf3000_webport_vec3 first;
	sf3000_webport_vec3 second;

	first.x = sf3000_webport_i32(*x0);
	first.y = sf3000_webport_i32(*y0);
	first.z = sf3000_webport_i32(*z0);
	second.x = sf3000_webport_i32(*x1);
	second.y = sf3000_webport_i32(*y1);
	second.z = sf3000_webport_i32(*z1);
	sf3000_webport_clip_vec3(&first, &second);
	*x0 = sf3000_webport_long(first.x);
	*y0 = sf3000_webport_long(first.y);
	*z0 = sf3000_webport_long(first.z);
	*x1 = sf3000_webport_long(second.x);
	*y1 = sf3000_webport_long(second.y);
	*z1 = sf3000_webport_long(second.z);
}

static void sf3000_webport_project(sf3000_webport_vec3 *point,
				    int high_resolution)
{
	int32_t scale = sf3000_webport_perspective_scale(point->y,
							   high_resolution);

	point->x = sf3000_webport_asr32(
		sf3000_webport_mul32(point->x, scale), 16);
	point->z = sf3000_webport_asr32(
		sf3000_webport_mul32(point->z, scale), 16);
}

static int sf3000_webport_outside_quad(const int32_t points[8])
{
#if defined(SF_WEB_PORT)
	(void)points;
	return 0;
#else
	return (points[1] > 120 && points[3] > 120 && points[5] > 120 &&
		points[7] > 120) ||
	       (points[1] < -120 && points[3] < -120 && points[5] < -120 &&
		points[7] < -120) ||
	       (points[0] < -160 && points[2] < -160 && points[4] < -160 &&
		points[6] < -160) ||
	       (points[0] > 160 && points[2] > 160 && points[4] > 160 &&
		points[6] > 160);
#endif
}

static int32_t sf3000_webport_vector_check(const int32_t points[8])
{
	int32_t first = sf3000_webport_mul32(
		sf3000_webport_sub32(points[2], points[0]),
		sf3000_webport_abs32(sf3000_webport_sub32(points[5], points[3])));
	int32_t second = sf3000_webport_mul32(
		sf3000_webport_sub32(points[4], points[2]),
		sf3000_webport_abs32(sf3000_webport_sub32(points[3], points[1])));

	if (points[5] < points[3]) {
		first = sf3000_webport_sub32(0, first);
	}
	if (points[3] < points[1]) {
		second = sf3000_webport_sub32(0, second);
	}
	return sf3000_webport_sub32(first, second);
}

static void sf3000_webport_apply_movement(sf3000_webport_vec3 *point,
					   const sf3000_webport_vec3 *movement,
					   uint8_t instruction)
{
	int32_t x;
	int32_t y;
	int32_t z;
	uint8_t shift;

	if (instruction == 0) {
		return;
	}

	shift = instruction & 31U;
	if (instruction & 64U) {
		x = sf3000_webport_asr32(movement->x, shift);
		y = sf3000_webport_asr32(movement->y, shift);
		z = sf3000_webport_asr32(movement->z, shift);
	} else {
		x = sf3000_webport_lsl32(movement->x, shift);
		y = sf3000_webport_lsl32(movement->y, shift);
		z = sf3000_webport_lsl32(movement->z, shift);
	}

	if (instruction & 128U) {
		point->x = sf3000_webport_sub32(point->x, x);
		point->y = sf3000_webport_sub32(point->y, y);
		point->z = sf3000_webport_sub32(point->z, z);
	} else {
		point->x = sf3000_webport_add32(point->x, x);
		point->y = sf3000_webport_add32(point->y, y);
		point->z = sf3000_webport_add32(point->z, z);
	}
}

static void sf3000_webport_store_node(size_t index,
				      const sf3000_webport_vec3 *point)
{
	graphic_rotated_data[index * 4] = point->x;
	graphic_rotated_data[index * 4 + 1] = point->y;
	graphic_rotated_data[index * 4 + 2] = point->z;
}

static void sf3000_webport_project_nodes(int32_t last_node,
					  int high_resolution)
{
	int32_t index;

	for (index = 0; index <= last_node; ++index) {
		sf3000_webport_vec3 point;

		point.x = graphic_rotated_data[index * 4];
		point.y = graphic_rotated_data[index * 4 + 1];
		point.z = graphic_rotated_data[index * 4 + 2];
		if (point.y > 0) {
			sf3000_webport_project(&point, high_resolution);
		}
		graphic_screen_data[index * 2] = point.x;
		graphic_screen_data[index * 2 + 1] = point.z;
	}
}

static void sf3000_webport_rotate_graphic(
	const sf3000_webport_vec3 rotation[3],
	const sf3000_webport_vec3 *centre)
{
	const uint8_t *movement;
	sf3000_webport_vec3 point = *centre;
	int32_t node_count;
	int32_t radar_nodes;
	int32_t first_phase_count;
	int32_t index = 0;
	int32_t phase;

	if (graphic_start == NULL || graphic_rotated_data == NULL ||
	    graphic_screen_data == NULL) {
		return;
	}

	node_count = graphic_start[33];
	radar_nodes = graphic_start[34] - 1;
	if (radar_nodes < 0) {
		radar_nodes = node_count;
	}
	radar_nodes = node_count - radar_nodes;
	first_phase_count = node_count - radar_nodes;
	movement = graphic_start + 40;

	for (phase = 0; phase <= first_phase_count; ++phase) {
		sf3000_webport_apply_movement(&point, &rotation[0], movement[0]);
		sf3000_webport_apply_movement(&point, &rotation[1], movement[1]);
		sf3000_webport_apply_movement(&point, &rotation[2], movement[2]);
		sf3000_webport_store_node((size_t)index++, &point);
		movement += 3;
	}

	if (radar_nodes > 0) {
		for (phase = 0; phase <= radar_nodes; ++phase) {
			sf3000_webport_apply_movement(
				&point, &static_graphic_radar_rotation[0], movement[0]);
			sf3000_webport_apply_movement(
				&point, &static_graphic_radar_rotation[1], movement[1]);
			sf3000_webport_apply_movement(
				&point, &static_graphic_radar_rotation[2], movement[2]);
			sf3000_webport_store_node((size_t)index++, &point);
			movement += 3;
		}
	}

	sf3000_webport_project_nodes(node_count, centre->y < 4096);
}

static void sf3000_webport_rotate_by_ship(sf3000_webport_vec3 *point,
					   const uint8_t *ship)
{
	int32_t angle;

	sf3000_webport_rotate_z(
		point, sf3000_webport_asr32(sf3000_webport_read_i32(ship, 20), 10));
	sf3000_webport_rotate_y(
		point, sf3000_webport_asr32(sf3000_webport_read_i32(ship, 16), 10));
	angle = sf3000_webport_sub32(
		sf3000_webport_i32(camera_x_rotation),
		sf3000_webport_asr32(sf3000_webport_read_i32(ship, 12), 10));
	if (angle < 0) {
		angle = sf3000_webport_add32(angle, 1024);
	}
	sf3000_webport_rotate_x(point, angle);
	sf3000_webport_rotate_y(point,
				 sf3000_webport_i32(camera_y_rotation));
	sf3000_webport_rotate_z(point,
				 sf3000_webport_i32(camera_z_rotation));
}

static void sf3000_webport_setup_ship_rotations(const uint8_t *ship,
						  int full_size)
{
	int32_t size = full_size ? 1024 : 512;
	int32_t axis;

	for (axis = 0; axis < 3; ++axis) {
		sf3000_webport_vec3 point = { 0, 0, 0 };

		if (axis == 0) {
			point.x = size;
		} else if (axis == 1) {
			point.y = size;
		} else if (full_size) {
			point.z = size;
		} else {
			point.x = sf3000_webport_i32(silly_y);
			point.y = sf3000_webport_i32(silly_x);
			point.z = size;
		}
		sf3000_webport_rotate_by_ship(&point, ship);
		ship_graphic_rotation[axis] = point;
	}
}

static void sf3000_webport_clip_quad(sf3000_webport_vec3 points[4])
{
	static const uint8_t neighbours[4][2] = {
		{ 1, 3 }, { 0, 2 }, { 1, 3 }, { 2, 0 }
	};
	int32_t index;

	for (index = 0; index < 4; ++index) {
		uint8_t first_neighbour = neighbours[index][0];
		uint8_t second_neighbour = neighbours[index][1];
		uint8_t selected;
		sf3000_webport_vec3 current;
		sf3000_webport_vec3 neighbour;

		if (points[index].y > SF3000_WEBPORT_CLIP_DISTANCE) {
			continue;
		}
		selected = points[first_neighbour].y > points[second_neighbour].y ?
				   first_neighbour :
				   second_neighbour;
		current = points[index];
		neighbour = points[selected];
		sf3000_webport_clip_vec3(&current, &neighbour);
		points[index] = current;
	}
}

static int32_t sf3000_webport_ship_shade(const uint8_t *link)
{
	int32_t shade;
	int32_t facet_cosine;
	int32_t camera_cosine;

	shade = sf3000_webport_cosine(
		sf3000_webport_lsl32(
			(sf3000_webport_sub32(plot_graphic_z_rotation, link[17])) &
				255,
			2));
	facet_cosine = sf3000_webport_abs32(
		sf3000_webport_cosine(sf3000_webport_lsl32(link[18], 2)));
	shade = sf3000_webport_asr32(
		sf3000_webport_mul32(shade, facet_cosine), 12);
	camera_cosine = sf3000_webport_abs32(sf3000_webport_cosine(
		sf3000_webport_lsl32(plot_graphic_y_rotation, 2)));
	shade = sf3000_webport_mul32(shade, camera_cosine);
	if (shade < 0) {
		shade = sf3000_webport_sub32(0, shade);
	}
	return sf3000_webport_add32(sf3000_webport_asr32(shade, 20), 17);
}

static void sf3000_webport_render_fluffy(const uint8_t *link)
{
	sf3000_webport_vec3 point;
	sf3000_webport_celdata *quad = graphic_cel_quad;
	int32_t type = link[16];
	int32_t sprite;
	int32_t atlas_slot;
	uint32_t atlas_transform = 0u;
	int32_t scale;
	int32_t corner;
	int32_t sprite_scale;
	int32_t shade;
	int32_t node = sf3000_webport_read_be_i32(link, 0);
	uint32_t command_count;

	if (quad == NULL || node < 0 || node >= 256) {
		return;
	}
	point.x = graphic_rotated_data[(size_t)node * 4];
	point.y = graphic_rotated_data[(size_t)node * 4 + 1];
	point.z = graphic_rotated_data[(size_t)node * 4 + 2];
	if (point.y < 8192) {
		return;
	}

	scale = sf3000_webport_perspective_scale(point.y, 0);
	if (type >= 252) {
		corner = 3 * 512;
		sprite_scale = sf3000_webport_lsl32(
			sf3000_webport_add32(scale, sf3000_webport_asr32(scale, 1)), 2);
		shade = 17;
		sprite = type - 252 + SF3000_WEBPORT_FLUFFY_TREE;
		atlas_slot = type - 252 + SF3000_WEBPORT_TREE_ATLAS_SLOT;
	} else {
		corner = 3 * 1024;
		sprite_scale = sf3000_webport_lsl32(
			sf3000_webport_add32(scale, sf3000_webport_asr32(scale, 1)), 3);
		shade = 4;
		sprite = type - 248 + SF3000_WEBPORT_FLUFFY_CLOUD;
		atlas_slot = type - 248 + SF3000_WEBPORT_CLOUD_ATLAS_SLOT;
		atlas_transform = (((uint32_t)point.x * UINT32_C(0x9e3779b9)) ^
			((uint32_t)point.y * UINT32_C(0x85ebca6b)) ^
			((uint32_t)point.z * UINT32_C(0xc2b2ae35))) >> 29;
	}
	quad->x_pos0 = sf3000_webport_long(sf3000_webport_asr32(
		sf3000_webport_mul32(sf3000_webport_sub32(point.x, corner), scale),
		16));
	quad->y_pos0 = sf3000_webport_long(sf3000_webport_asr32(
		sf3000_webport_mul32(sf3000_webport_sub32(point.z, corner), scale),
		16));
	quad->shade = sf3000_webport_long(shade);
	command_count = sf_web_renderer_command_count();
	arm_addgamecel(quad, sf3000_webport_long(sprite),
		       sf3000_webport_long(sprite_scale),
		       sf3000_webport_long(sprite_scale));
	if (render_world_geometry != 0 &&
	    sf_web_renderer_command_count() != command_count)
		sf_web_world_renderer_append_last_precise_particle_billboard(
			sf3000_webport_sub32(point.x, corner), point.y,
			sf3000_webport_sub32(point.z, corner), (uint32_t)atlas_slot,
			atlas_transform);
}

static void sf3000_webport_render_links(const uint8_t *links)
{
	int32_t count;
	int32_t index;

	if (links == NULL || graphic_cel_quad == NULL) {
		return;
	}

	count = sf3000_webport_read_be_i32(links, 0);
	for (index = 0; index <= count; ++index) {
		const uint8_t *link = links + 4 + ((size_t)index * 20);
		sf3000_webport_vec3 points3d[4];
		int32_t points[8];
		int32_t node[4];
		int32_t point_index;
		int all_in_front = 1;
		int any_beyond_clip = 0;
		int world_geometry = render_world_geometry != 0;
		int32_t shade;
		uint32_t command_count;

		if (link[16] >= 248) {
			sf3000_webport_render_fluffy(link);
			continue;
		}

		for (point_index = 0; point_index < 4; ++point_index) {
			node[point_index] =
				sf3000_webport_read_be_i32(link, (size_t)point_index * 4);
			if (node[point_index] < 0 || node[point_index] >= 256) {
				all_in_front = 0;
				break;
			}
			points3d[point_index].x =
				graphic_rotated_data[(size_t)node[point_index] * 4];
			points3d[point_index].y =
				graphic_rotated_data[(size_t)node[point_index] * 4 + 1];
			points3d[point_index].z =
				graphic_rotated_data[(size_t)node[point_index] * 4 + 2];
			if (!world_geometry) {
				if (points3d[point_index].y <
				    SF3000_WEBPORT_CLIP_DISTANCE) {
					all_in_front = 0;
				}
				if (points3d[point_index].y >
				    SF3000_WEBPORT_CLIP_DISTANCE) {
					any_beyond_clip = 1;
				}
			}
		}
		if (point_index != 4) {
			continue;
		}

		if (world_geometry) {
			memset(points, 0, sizeof(points));
		} else if (all_in_front) {
			for (point_index = 0; point_index < 4; ++point_index) {
				points[point_index * 2] =
					graphic_screen_data[(size_t)node[point_index] * 2];
				points[point_index * 2 + 1] =
					graphic_screen_data[(size_t)node[point_index] * 2 + 1];
			}
		} else if (any_beyond_clip) {
			sf3000_webport_clip_quad(points3d);
			for (point_index = 0; point_index < 4; ++point_index) {
				sf3000_webport_project(&points3d[point_index], 1);
				points[point_index * 2] = points3d[point_index].x;
				points[point_index * 2 + 1] = points3d[point_index].z;
			}
		} else {
			continue;
		}

		if (!world_geometry &&
		    (sf3000_webport_vector_check(points) < 0 ||
		    sf3000_webport_outside_quad(points))) {
			continue;
		}

		if (shade_style == 0) {
			shade = sf3000_webport_add32(
				sf3000_webport_lsl32(
					sf3000_webport_sub32(link[17], 17), 1),
				17);
		} else {
			shade = sf3000_webport_ship_shade(link);
			if (link[16] == 31) {
				shade = 4;
			}
		}
		sf3000_webport_set_quad(graphic_cel_quad, points, shade);
		command_count = sf_web_renderer_command_count();
		arm_addpolycel32(graphic_cel_quad,
				 sf3000_webport_long(link[16]));
		if (shade_style == 2) {
			arm_setpolycel32palette(graphic_cel_quad,
						 sf3000_webport_long(bit_palette));
		}
		if (world_geometry &&
		    sf_web_renderer_command_count() != command_count) {
			int32_t view_x[4];
			int32_t view_y[4];
			int32_t view_z[4];

			for (point_index = 0; point_index < 4; ++point_index) {
				view_x[point_index] = points3d[point_index].x;
				view_y[point_index] = points3d[point_index].y;
				view_z[point_index] = points3d[point_index].z;
			}
			sf_web_world_renderer_append_last_model_quad(view_x, view_y,
								     view_z);
		}
	}
}

static int32_t sf3000_webport_test_vectors(const uint8_t *vectors)
{
	int32_t count;
	int32_t index;
	int32_t result = 0;
	int32_t bit = 1;

	if (vectors == NULL) {
		return result;
	}
	count = sf3000_webport_read_be_i32(vectors, 0);
	for (index = 0; index <= count; ++index) {
		const uint8_t *vector = vectors + 4 + ((size_t)index * 12);
		int32_t nodes[3];
		int32_t points[8];
		int valid = 1;
		int32_t point_index;

		for (point_index = 0; point_index < 3; ++point_index) {
			nodes[point_index] =
				sf3000_webport_read_be_i32(vector, (size_t)point_index * 4);
			if (nodes[point_index] < 0 || nodes[point_index] >= 256 ||
			    graphic_rotated_data[(size_t)nodes[point_index] * 4 + 1] <
				    SF3000_WEBPORT_CLIP_DISTANCE) {
				valid = 0;
				break;
			}
			points[point_index * 2] =
				graphic_screen_data[(size_t)nodes[point_index] * 2];
			points[point_index * 2 + 1] =
				graphic_screen_data[(size_t)nodes[point_index] * 2 + 1];
		}
		if (valid && sf3000_webport_vector_check(points) < 0) {
			result |= bit;
		}
		bit = sf3000_webport_lsl32(bit, 1);
	}
	return result;
}

static void sf3000_webport_plot_group(int group)
{
	int32_t offset;

	if (graphic_start == NULL || (plot_group_mask & (1 << group)) != 0) {
		return;
	}
	offset = sf3000_webport_read_be_i32(graphic_start, (size_t)group * 4);
	if (offset != 0) {
		sf3000_webport_render_links(graphic_start + offset);
	}
}

static void sf3000_webport_plot_style(void)
{
	static const uint8_t early_order[][5] = {
		{ 1, 2, 255, 255, 255 },
		{ 4, 2, 5, 3, 1 },
		{ 255, 255, 255, 255, 255 },
		{ 1, 255, 255, 255, 255 },
		{ 4, 1, 2, 3, 255 },
		{ 255, 255, 255, 255, 255 },
		{ 255, 255, 255, 255, 255 },
		{ 4, 3, 2, 1, 255 }
	};
	static const uint8_t late_order[][5] = {
		{ 1, 2, 255, 255, 255 },
		{ 1, 3, 2, 4, 5 },
		{ 255, 255, 255, 255, 255 },
		{ 1, 255, 255, 255, 255 },
		{ 1, 2, 3, 4, 255 },
		{ 255, 255, 255, 255, 255 },
		{ 255, 255, 255, 255, 255 },
		{ 1, 2, 3, 4, 255 }
	};
	int32_t style;
	int32_t vectors = 0;
	int group;

	if (graphic_start == NULL) {
		return;
	}
	style = graphic_start[32];
	if (style < 0 || style > 7) {
		return;
	}
	if (style != 2) {
		int32_t offset = sf3000_webport_read_be_i32(graphic_start, 28);

		vectors = sf3000_webport_test_vectors(graphic_start + offset);
	}

	if (style == 2) {
		sf3000_webport_plot_group(0);
		sf3000_webport_plot_group(1);
		return;
	}
	if (style == 5) {
		sf3000_webport_plot_group(0);
		if (vectors & (1 << 2)) {
			sf3000_webport_plot_group(5);
		}
		if (vectors & (1 << 3)) {
			sf3000_webport_plot_group(6);
		}
		sf3000_webport_plot_group(4);
		if ((vectors & (1 << 2)) == 0) {
			sf3000_webport_plot_group(5);
		}
		if ((vectors & (1 << 3)) == 0) {
			sf3000_webport_plot_group(6);
		}
		if ((vectors & 1) == 0) {
			sf3000_webport_plot_group(1);
		}
		if ((vectors & 2) == 0) {
			sf3000_webport_plot_group(2);
		}
		sf3000_webport_plot_group(3);
		return;
	}
	if (style == 6) {
		if ((vectors & 1) == 0) {
			sf3000_webport_plot_group(0);
		}
		if ((vectors & 2) == 0) {
			sf3000_webport_plot_group(1);
		}
		if (vectors & 4) {
			sf3000_webport_plot_group(3);
		}
		sf3000_webport_plot_group(2);
		if (vectors & 2) {
			sf3000_webport_plot_group(1);
		}
		if ((vectors & 4) == 0) {
			sf3000_webport_plot_group(3);
		}
		if (vectors & 1) {
			sf3000_webport_plot_group(0);
		}
		return;
	}

	for (group = 0; group < 5 && early_order[style][group] != 255; ++group) {
		int selected = early_order[style][group];

		if (vectors & (1 << (selected - 1))) {
			sf3000_webport_plot_group(selected);
		}
	}
	sf3000_webport_plot_group(0);
	for (group = 0; group < 5 && late_order[style][group] != 255; ++group) {
		int selected = late_order[style][group];

		if ((vectors & (1 << (selected - 1))) == 0) {
			sf3000_webport_plot_group(selected);
		}
	}
}

static void sf3000_webport_draw_line(int32_t x0, int32_t y0, int32_t x1,
				      int32_t y1, int32_t colour,
				      int32_t shade)
{
	int32_t points[8];
	int32_t horizontal = 0;
	int32_t vertical = 0;

	if ((y0 > 120 && y1 > 120) || (y0 < -120 && y1 < -120) ||
	    (x0 < -160 && x1 < -160) || (x0 > 160 && x1 > 160)) {
		return;
	}
	if (sf3000_webport_abs32(sf3000_webport_sub32(x0, x1)) >
	    sf3000_webport_abs32(sf3000_webport_sub32(y0, y1))) {
		vertical = SF3000_WEBPORT_TARGET_LINE_WIDTH;
	} else {
		horizontal = SF3000_WEBPORT_TARGET_LINE_WIDTH;
	}
	points[0] = x0;
	points[1] = y0;
	points[2] = sf3000_webport_add32(x1, horizontal);
	points[3] = sf3000_webport_add32(y1, vertical);
	points[4] = sf3000_webport_add32(x0, horizontal);
	points[5] = sf3000_webport_add32(y0, vertical);
	points[6] = x1;
	points[7] = y1;
	sf3000_webport_set_quad(graphic_cel_quad, points, shade);
	arm_addmonocel(graphic_cel_quad, 0, sf3000_webport_long(colour), 2);
}

static void sf3000_webport_draw_collision_boxes(int ship_collision)
{
	static const uint8_t edges[12][2] = {
		{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 0, 6 }, { 1, 7 },
		{ 2, 4 }, { 3, 5 }, { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }
	};
	static const uint8_t corner_x[8] = { 0, 1, 1, 0, 1, 0, 0, 1 };
	static const uint8_t corner_y[8] = { 0, 0, 1, 1, 1, 1, 0, 0 };
	static const uint8_t corner_z[8] = { 0, 0, 0, 0, 1, 1, 1, 1 };
	const uint8_t *details;
	const uint8_t *collision;
	int32_t count;
	int32_t box;
	int32_t colour;

	if (graphic_cel_quad == NULL ||
	    (ship_collision && !i_am_a_ship) ||
	    (!ship_collision && !i_am_an_object)) {
		return;
	}
	if (ship_collision) {
		int32_t type = sf3000_webport_read_i32(ship_to_plot, 24);

		if (type >= 255) {
			return;
		}
		details = ships_data + ((size_t)type * 64);
	} else {
		details = static_graphics_data + ((size_t)static_object_type * 64);
	}
	collision = sf3000_webport_read_pointer(details, 16);
	if (collision == NULL) {
		return;
	}
	count = sf3000_webport_read_i32(collision, 0);
	collision += 16;
	colour = ship_collision ? 8 : 40;
	if (sf3000_webport_i32(are_we_in_space_or_wot) == 1) {
		colour = 8;
	}
	if (atg_truck_target) {
		colour = 40;
	}

	for (box = 0; box <= count; ++box, collision += 28) {
		sf3000_webport_vec3 corners[8];
		int32_t x0 = sf3000_webport_read_i32(collision, 0);
		int32_t y0 = sf3000_webport_read_i32(collision, 4);
		int32_t z0 = sf3000_webport_read_i32(collision, 8);
		int32_t x1 = sf3000_webport_read_i32(collision, 12);
		int32_t y1 = sf3000_webport_read_i32(collision, 16);
		int32_t z1 = sf3000_webport_read_i32(collision, 20);
		int32_t xi;
		int32_t yi;
		int32_t zi;
		int edge;

		for (edge = 0; edge < 8; ++edge) {
			xi = corner_x[edge] ? x1 : x0;
			yi = corner_y[edge] ? y1 : y0;
			zi = corner_z[edge] ? z1 : z0;
			corners[edge].x = sf3000_webport_asr32(
				sf3000_webport_mul32(
					sf3000_webport_asr32(xi, 12),
					sf3000_webport_add32(
						sf3000_webport_i32(
							collision_box_render_colour_adder),
						16)),
				4);
			corners[edge].y = sf3000_webport_asr32(
				sf3000_webport_mul32(
					sf3000_webport_asr32(yi, 12),
					sf3000_webport_add32(
						sf3000_webport_i32(
							collision_box_render_colour_adder),
						16)),
				4);
			corners[edge].z = sf3000_webport_sub32(0,
				sf3000_webport_asr32(
					sf3000_webport_mul32(
						sf3000_webport_asr32(zi, 12),
						sf3000_webport_add32(
							sf3000_webport_i32(
								collision_box_render_colour_adder),
							16)),
					4));
			if (ship_collision) {
				sf3000_webport_rotate_by_ship(&corners[edge],
							       ship_to_plot);
			} else {
				sf3000_webport_rotate_camera(&corners[edge]);
			}
			corners[edge].x = sf3000_webport_add32(
				corners[edge].x, plot_graphic_position.x);
			corners[edge].y = sf3000_webport_add32(
				corners[edge].y, plot_graphic_position.y);
			corners[edge].z = sf3000_webport_add32(
				corners[edge].z, plot_graphic_position.z);
		}
		for (edge = 0; edge < 12; ++edge) {
			sf3000_webport_vec3 first = corners[edges[edge][0]];
			sf3000_webport_vec3 second = corners[edges[edge][1]];

			if (first.y < 2048 || second.y < 2048) {
				continue;
			}
			sf3000_webport_project(&first, 0);
			sf3000_webport_project(&second, 0);
			sf3000_webport_draw_line(
				first.x, first.z, second.x, second.z, colour,
				sf3000_webport_add32(
					sf3000_webport_asr32(
						sf3000_webport_i32(
							collision_box_render_colour_adder),
						1),
					1));
		}
	}
}

static void sf3000_webport_plot_static_internal(
	int32_t x, int32_t y, int32_t z, int32_t type)
{
	sf3000_webport_vec3 centre = { x, y, z };
	const uint8_t *details;

	if (static_graphics_data == NULL || type < 0) {
		return;
	}
	plot_graphic_position = centre;
	static_object_type = type;
	i_am_an_object = type < 60;
	details = static_graphics_data + ((size_t)type * 64);
	graphic_start = sf3000_webport_read_pointer(details, 8);
	if (graphic_start == NULL) {
		return;
	}
	sf3000_webport_rotate_graphic(static_graphic_rotation, &centre);
	shade_style = 0;
	plot_group_mask = 0;
	render_world_geometry = 1;
	sf3000_webport_plot_style();
	render_world_geometry = 0;
}

static int32_t sf3000_webport_target_bounds_radius(int32_t clip_size)
{
	int32_t radius = sf3000_webport_asr32(clip_size, 12);

	return radius < 0 ? sf3000_webport_sub32(0, radius) : radius;
}

static int sf3000_webport_target_in_aim_cone(
	const sf3000_webport_vec3 *point, uint32_t horizontal_shift,
	uint32_t vertical_shift)
{
	int64_t depth;

	if (point == NULL) {
		return 0;
	}

	depth = point->y;
	if (depth < 0) {
		return 0;
	}
	if (depth < ((int64_t)sf3000_webport_abs32(point->x) <<
		     horizontal_shift) ||
	    depth < ((int64_t)sf3000_webport_abs32(point->z) <<
		     vertical_shift)) {
		return 0;
	}
	return 1;
}

static double sf3000_webport_target_selection_angle(
	const sf3000_webport_vec3 *point, int32_t bounds_radius)
{
	double horizontal_distance = hypot((double)point->x, (double)point->z);
	double centre_distance = hypot(horizontal_distance, (double)point->y);
	double centre_angle = atan2(horizontal_distance, (double)point->y);
	double radius_angle;

	if (centre_distance == 0.0) {
		return 0.0;
	}
	radius_angle = asin(fmin((double)bounds_radius / centre_distance, 1.0));
	return centre_angle > radius_angle ? centre_angle - radius_angle : 0.0;
}

static uint64_t sf3000_webport_target_player_distance_squared(
	int32_t x, int32_t y, int32_t z)
{
	int32_t player_x = players_ship != NULL ?
		sf3000_webport_i32(players_ship->x_pos) :
		sf3000_webport_i32(camera_x_position);
	int32_t player_y = players_ship != NULL ?
		sf3000_webport_i32(players_ship->y_pos) :
		sf3000_webport_i32(camera_y_position);
	int32_t player_z = players_ship != NULL ?
		sf3000_webport_i32(players_ship->z_pos) :
		sf3000_webport_i32(camera_z_position);
	int64_t delta_x = sf3000_webport_asr32(
		sf3000_webport_sub32(x, player_x), 12);
	int64_t delta_y = sf3000_webport_asr32(
		sf3000_webport_sub32(y, player_y), 12);
	int64_t delta_z = sf3000_webport_asr32(
		sf3000_webport_sub32(z, player_z), 12);

	return (uint64_t)(delta_x * delta_x + delta_y * delta_y +
			  delta_z * delta_z);
}

static int sf3000_webport_target_is_preferred(
	double angle, uint64_t player_distance, double best_angle,
	uint64_t best_player_distance)
{
	return angle < best_angle ||
	       (angle == best_angle && player_distance < best_player_distance);
}

void sf3000_webport_begin_target_selection(void)
{
	/*
	 * Collision boxes historically display the target selected during the
	 * preceding draw.  Preserve that frame boundary while calculating the
	 * next target during the simulation update.
	 */
	air_to_ground_render_scan = air_to_ground_scan;
	air_to_air_render_scan = air_to_air_scan;
	collision_box_render_colour_adder = collision_box_colour_adder;
	air_to_ground_scan_temp = (long)NULL;
	air_to_air_scan_temp = (long)NULL;
	air_to_ground_x = 1<<30;
	air_to_ground_y = 1<<30;
	air_to_ground_z = 1<<30;
	air_to_air_x = 1<<30;
	air_to_air_y = 1<<30;
	air_to_air_z = 1<<30;
	air_to_ground_selection_angle = DBL_MAX;
	air_to_air_selection_angle = DBL_MAX;
	air_to_ground_selection_distance = UINT64_MAX;
	air_to_air_selection_distance = UINT64_MAX;
}

int sf3000_webport_target_selection_prefers_air(void)
{
	if (air_to_air_scan_temp == (long)NULL) {
		return 0;
	}
	if (air_to_ground_scan_temp == (long)NULL) {
		return 1;
	}
	return air_to_air_selection_angle < air_to_ground_selection_angle ||
	       (air_to_air_selection_angle == air_to_ground_selection_angle &&
		air_to_air_selection_distance < air_to_ground_selection_distance);
}

void sf3000_webport_consider_static_target(void *grid_reference)
{
	int32_t grid = (int32_t)(uint32_t)(uintptr_t)grid_reference;
	int32_t x_grid;
	int32_t y_grid;
	int32_t type;
	int32_t height;
	int32_t x;
	int32_t y;
	int32_t z;
	int32_t clipped_depth;
	int32_t bounds_radius;
	double selection_angle;
	uint64_t player_distance;
	const uint8_t *details;
	sf3000_webport_vec3 point;

	if (poly_map_data == NULL || landscape_heights == NULL ||
	    static_graphics_data == NULL) {
		return;
	}
	x_grid = grid & 127;
	y_grid = sf3000_webport_asr32(grid, 7);
	type = poly_map_data[(uint32_t)grid];
	details = static_graphics_data + ((size_t)type * 64);
	height = landscape_heights[(size_t)sf3000_webport_lsl32(x_grid, 1) +
				   (size_t)sf3000_webport_lsl32(y_grid, 9)];
	height = sf3000_webport_sub32(height, 17);
	if (height < 0) {
		height = 0;
	}
	if (type == 60) {
		height = sf3000_webport_add32(height, 64);
	}

	z = sf3000_webport_lsl32(height, 21);
	x = sf3000_webport_lsl32(x_grid, 25);
	y = sf3000_webport_lsl32(y_grid, 25);
	player_distance = sf3000_webport_target_player_distance_squared(x, y, z);
	x = sf3000_webport_sub32(x, sf3000_webport_i32(camera_x_position));
	y = sf3000_webport_sub32(y, sf3000_webport_i32(camera_y_position));
	z = sf3000_webport_sub32(sf3000_webport_i32(camera_z_position), z);
	point.x = sf3000_webport_asr32(x, 12);
	point.y = sf3000_webport_asr32(y, 12);
	point.z = sf3000_webport_asr32(z, 12);
	sf3000_webport_rotate_camera(&point);
	bounds_radius = sf3000_webport_target_bounds_radius(
		sf3000_webport_read_i32(details, 0));
	clipped_depth = sf3000_webport_add32(
		point.y, bounds_radius);
	if (clipped_depth < 0 || sf3000_webport_abs32(point.x) > clipped_depth ||
	    sf3000_webport_abs32(point.z) > clipped_depth) {
		return;
	}

	if ((sf3000_webport_i32(are_we_in_space_or_wot) == 0 &&
	     sf3000_webport_i32(atg_selected) != 0) ||
	    (sf3000_webport_i32(are_we_in_space_or_wot) != 0 &&
	     sf3000_webport_i32(ata_selected) != 0)) {
		if (sf3000_webport_target_in_aim_cone(&point, 2u, 1u)) {
			selection_angle = sf3000_webport_target_selection_angle(
				&point, bounds_radius);
			if (!sf3000_webport_target_is_preferred(
				    selection_angle, player_distance,
				    air_to_ground_selection_angle,
				    air_to_ground_selection_distance)) {
				return;
			}
			air_to_ground_x = sf3000_webport_long(
				(int32_t)(selection_angle * 1048576.0));
			air_to_ground_y = sf3000_webport_long(point.y);
			air_to_ground_z = sf3000_webport_long(point.z);
			air_to_ground_scan_temp = sf3000_webport_long(grid);
			air_to_ground_selection_angle = selection_angle;
			air_to_ground_selection_distance = player_distance;
		}
	}
}

void sf3000_webport_consider_ship_target(void *ship)
{
	const uint8_t *data = (const uint8_t *)ship +
			      SF3000_WEBPORT_SHIP_HEADER_BYTES;
	sf3000_webport_vec3 point;
	const uint8_t *details;
	int32_t type;
	int32_t category;
	int32_t clip_size;
	int32_t bounds_radius;
	double selection_angle;
	uint64_t player_distance;

	if (ship == NULL || ships_data == NULL) {
		return;
	}
	type = sf3000_webport_read_i32(data, 24);
	if (type >= 255) {
		clip_size = 8 << 24;
	} else {
		details = ships_data + ((size_t)type * 64);
		clip_size = sf3000_webport_read_i32(details, 0);
	}
	bounds_radius = sf3000_webport_target_bounds_radius(clip_size);
	player_distance = sf3000_webport_target_player_distance_squared(
		sf3000_webport_read_i32(data, 0),
		sf3000_webport_read_i32(data, 4),
		sf3000_webport_read_i32(data, 8));
	point.x = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_read_i32(data, 0),
				     sf3000_webport_i32(camera_x_position)), 12);
	point.y = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_read_i32(data, 4),
				     sf3000_webport_i32(camera_y_position)), 12);
	point.z = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_i32(camera_z_position),
				     sf3000_webport_read_i32(data, 8)), 12);
	sf3000_webport_rotate_camera(&point);
	clip_size = sf3000_webport_add32(point.y, bounds_radius);
	if (clip_size < 0 || sf3000_webport_abs32(point.x) > clip_size ||
	    sf3000_webport_abs32(point.z) > clip_size) {
		return;
	}

	category = sf3000_webport_asr32(type, 4);
	if (sf3000_webport_i32(atg_selected) == 0 &&
	    (sf3000_webport_i32(ata_selected) == 0 || category == 7)) {
		return;
	}
	if (sf3000_webport_target_in_aim_cone(&point, 2u, 2u) &&
	    type < 256 && category != 0 && category != 3 && category != 6) {
		selection_angle = sf3000_webport_target_selection_angle(
			&point, bounds_radius);
		if (!sf3000_webport_target_is_preferred(
			    selection_angle, player_distance,
			    air_to_air_selection_angle,
			    air_to_air_selection_distance)) {
			return;
		}
		air_to_air_x = sf3000_webport_long(
			(int32_t)(selection_angle * 1048576.0));
		air_to_air_y = sf3000_webport_long(point.y);
		air_to_air_z = sf3000_webport_long(point.z);
		air_to_air_scan_temp =
			(long)(intptr_t)(data - SF3000_WEBPORT_SHIP_HEADER_BYTES);
		air_to_air_selection_angle = selection_angle;
		air_to_air_selection_distance = player_distance;
	}
}

void plot_static_graphic(long x, long y, long z, long type)
{
	sf3000_webport_plot_static_internal(sf3000_webport_i32(x),
					    sf3000_webport_i32(y),
					    sf3000_webport_i32(z),
					    sf3000_webport_i32(type));
}

void plot_static_from_grid(void *grid_reference)
{
	int32_t grid = (int32_t)(uint32_t)(uintptr_t)grid_reference;
	int32_t x_grid;
	int32_t y_grid;
	int32_t type;
	int32_t height;
	int32_t x;
	int32_t y;
	int32_t z;
	sf3000_webport_vec3 point;
	const uint8_t *details;
	int32_t clipped_depth;

	i_am_a_ship = 0;
	i_am_an_object = 0;
	if (poly_map_data == NULL || landscape_heights == NULL ||
	    static_graphics_data == NULL) {
		return;
	}
	static_object_grid_ref = grid;
	x_grid = grid & 127;
	y_grid = sf3000_webport_asr32(grid, 7);
	type = poly_map_data[(uint32_t)grid];
	details = static_graphics_data + ((size_t)type * 64);
	height = landscape_heights[(size_t)sf3000_webport_lsl32(x_grid, 1) +
				   (size_t)sf3000_webport_lsl32(y_grid, 9)];
	height = sf3000_webport_sub32(height, 17);
	if (height < 0) {
		height = 0;
	}
	if (type == 60) {
		height = sf3000_webport_add32(height, 64);
	}

	z = sf3000_webport_lsl32(height, 21);
	x = sf3000_webport_lsl32(x_grid, 25);
	y = sf3000_webport_lsl32(y_grid, 25);
	x = sf3000_webport_sub32(x, sf3000_webport_i32(camera_x_position));
	y = sf3000_webport_sub32(y, sf3000_webport_i32(camera_y_position));
	z = sf3000_webport_sub32(sf3000_webport_i32(camera_z_position), z);
	point.x = sf3000_webport_asr32(x, 12);
	point.y = sf3000_webport_asr32(y, 12);
	point.z = sf3000_webport_asr32(z, 12);
	sf3000_webport_rotate_camera(&point);
	clipped_depth = sf3000_webport_add32(
		point.y,
		sf3000_webport_asr32(sf3000_webport_read_i32(details, 0), 12));
	if (clipped_depth < 0 || sf3000_webport_abs32(point.x) > clipped_depth ||
	    sf3000_webport_abs32(point.z) > clipped_depth) {
		return;
	}

	if ((sf3000_webport_i32(are_we_in_space_or_wot) == 0 &&
	     sf3000_webport_i32(atg_selected) != 0) ||
	    (sf3000_webport_i32(are_we_in_space_or_wot) != 0 &&
	     sf3000_webport_i32(ata_selected) != 0)) {
		if (grid == sf3000_webport_i32(air_to_ground_render_scan)) {
			sf3000_webport_plot_static_internal(
				point.x, point.y, point.z, type);
			sf3000_webport_draw_collision_boxes(0);
			return;
		}
	}
	sf3000_webport_plot_static_internal(point.x, point.y, point.z, type);
}

static void sf3000_webport_plot_distant_ship(const uint8_t *ship)
{
	sf3000_webport_vec3 point;
	sf3000_webport_celdata *quad = graphic_cel_quad;
	int32_t scale;
	int32_t type;
	int32_t sprite;
	uint32_t command_count;

	if (quad == NULL) {
		return;
	}
	point.x = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_read_i32(ship, 0),
				     sf3000_webport_i32(camera_x_position)), 12);
	point.y = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_read_i32(ship, 4),
				     sf3000_webport_i32(camera_y_position)), 12);
	point.z = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_i32(camera_z_position),
				     sf3000_webport_read_i32(ship, 8)), 12);
	sf3000_webport_rotate_camera(&point);
	if (point.y < 8192) {
		return;
	}
	sf3000_webport_project(&point, 0);
#if !defined(SF_WEB_PORT)
	if (point.x > 168 || point.x < -168 || point.z > 128 || point.z < -128) {
		return;
	}
#endif
	quad->x_pos0 = sf3000_webport_long(sf3000_webport_sub32(point.x, 4));
	quad->y_pos0 = sf3000_webport_long(sf3000_webport_sub32(point.z, 4));
	quad->shade = 12;
	type = sf3000_webport_asr32(sf3000_webport_read_i32(ship, 24), 4);
	if (type == 6) {
		sprite = 42;
	} else if (type == 4) {
		sprite = 43;
	} else {
		const uint8_t *special =
			sf3000_webport_read_pointer(ship, 112);

		sprite = special != NULL && special[2] != 0 ? 40 : 41;
	}
	scale = 1024;
	command_count = sf_web_renderer_command_count();
	arm_addgamecel(quad, sf3000_webport_long(sprite),
		       sf3000_webport_long(scale), sf3000_webport_long(scale));
	if (sf_web_renderer_command_count() != command_count)
		sf_web_world_renderer_append_last_billboard(point.y);
}

static void sf3000_webport_plot_ship_internal(const uint8_t *ship)
{
	sf3000_webport_vec3 centre;
	int32_t type;
	int32_t clip_size;
	const uint8_t *details;

	ship_to_plot = ship;
	type = sf3000_webport_read_i32(ship, 24);
	i_am_a_ship = type < 255;
	if (type >= 255) {
		clip_size = 8 << 24;
	} else {
		details = ships_data + ((size_t)type * 64);
		clip_size = sf3000_webport_read_i32(details, 0);
	}
	plot_graphic_y_rotation =
		sf3000_webport_asr32(sf3000_webport_read_i32(ship, 16), 12);
	plot_graphic_z_rotation = sf3000_webport_add32(
		sf3000_webport_asr32(sf3000_webport_read_i32(ship, 20), 12), 40);
	centre.x = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_read_i32(ship, 0),
				     sf3000_webport_i32(camera_x_position)), 12);
	centre.y = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_read_i32(ship, 4),
				     sf3000_webport_i32(camera_y_position)), 12);
	centre.z = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_i32(camera_z_position),
				     sf3000_webport_read_i32(ship, 8)), 12);
	sf3000_webport_rotate_camera(&centre);
	plot_graphic_position = centre;
	clip_size = sf3000_webport_add32(
		centre.y, sf3000_webport_asr32(clip_size, 12));
	if (clip_size < 0 || sf3000_webport_abs32(centre.x) > clip_size ||
	    sf3000_webport_abs32(centre.z) > clip_size) {
		return;
	}

	if (type < 256) {
		sf3000_webport_setup_ship_rotations(ship, 0);
		shade_style = 1;
		plot_group_mask = 0;
		graphic_start = sf3000_webport_read_pointer(
			ships_data + ((size_t)type * 64), 8);
	} else {
		if (type >= 512) {
			sf3000_webport_setup_ship_rotations(ship, 0);
			details = ships_data + ((size_t)(type - 512) * 64);
		} else {
			sf3000_webport_setup_ship_rotations(ship, 1);
			details = static_graphics_data + ((size_t)(type - 256) * 64);
		}
		shade_style = 1;
		plot_group_mask = sf3000_webport_read_i32(ship, 40);
		graphic_start = sf3000_webport_read_pointer(details, 8);
	}
	if (graphic_start != NULL) {
		sf3000_webport_rotate_graphic(ship_graphic_rotation, &centre);
		render_world_geometry = 1;
		sf3000_webport_plot_style();
		render_world_geometry = 0;
	}
}

static void sf3000_webport_draw_ship_target(void)
{
	int32_t type;
	int32_t category;

	atg_truck_target = 0;
	if (ship_to_plot == NULL) {
		return;
	}
	type = sf3000_webport_read_i32(ship_to_plot, 24);
	category = sf3000_webport_asr32(type, 4);
	if (sf3000_webport_i32(atg_selected) != 0 && category == 7) {
		atg_truck_target = 1;
	} else if (sf3000_webport_i32(ata_selected) == 0 || category == 7) {
		return;
	}

	if ((const uint8_t *)(intptr_t)air_to_air_render_scan +
		    SF3000_WEBPORT_SHIP_HEADER_BYTES ==
	    ship_to_plot) {
		sf3000_webport_draw_collision_boxes(1);
	}
}

void plot_ship_graphic(void *ship)
{
	const uint8_t *data = (const uint8_t *)ship +
			      SF3000_WEBPORT_SHIP_HEADER_BYTES;

	i_am_a_ship = 0;
	i_am_an_object = 0;
	ship_to_plot = data;
	if (sf3000_webport_read_i32(data, 108) == 0) {
		sf3000_webport_plot_distant_ship(data);
	} else {
		sf3000_webport_plot_ship_internal(data);
	}
	sf3000_webport_draw_ship_target();
}

void plot_bit(void *bit)
{
	const uint8_t *data = bit;
	sf3000_webport_vec3 centre;
	int32_t type;

	ship_to_plot = data;
	plot_graphic_y_rotation =
		sf3000_webport_asr32(sf3000_webport_read_i32(data, 16), 12);
	plot_graphic_z_rotation = sf3000_webport_add32(
		sf3000_webport_asr32(sf3000_webport_read_i32(data, 20), 12), 40);
	centre.x = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_read_i32(data, 0),
				     sf3000_webport_i32(camera_x_position)), 12);
	centre.y = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_read_i32(data, 4),
				     sf3000_webport_i32(camera_y_position)), 12);
	centre.z = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_i32(camera_z_position),
				     sf3000_webport_read_i32(data, 8)), 12);
	sf3000_webport_rotate_camera(&centre);
	if (centre.y < 0 || sf3000_webport_abs32(centre.x) > centre.y ||
	    sf3000_webport_abs32(centre.z) > centre.y) {
		return;
	}
	plot_graphic_position = centre;
	sf3000_webport_setup_ship_rotations(data, 0);
	type = sf3000_webport_read_i32(data, 24);
	graphic_start = sf3000_webport_read_pointer(
		explosion_bits_data + ((size_t)type * 64), 8);
	if (graphic_start == NULL) {
		return;
	}
	bit_palette = sf3000_webport_read_i32(data, 28);
	shade_style = 2;
	plot_group_mask = 0;
	sf3000_webport_rotate_graphic(ship_graphic_rotation, &centre);
	render_world_geometry = 1;
	sf3000_webport_plot_style();
	render_world_geometry = 0;
}

void plot_bit_graphic(void *bit)
{
	plot_bit((uint8_t *)bit + SF3000_WEBPORT_SHIP_HEADER_BYTES);
}

static void sf3000_webport_plot_star(const int32_t *star, int space)
{
	sf3000_webport_vec3 point = { star[0], star[1], star[2] };
	int32_t scale;
	int32_t type;
	int32_t shade;
	int32_t metadata = star[3];
	uint32_t command_count;

	sf3000_webport_rotate_camera(&point);
	if (point.y < 1024) {
		return;
	}
	scale = sf3000_webport_perspective_scale(point.y, 0);
	point.x = sf3000_webport_asr32(sf3000_webport_mul32(point.x, scale), 16);
	point.z = sf3000_webport_asr32(sf3000_webport_mul32(point.z, scale), 16);
#if !defined(SF_WEB_PORT)
	if (point.x > 180 || point.x < -180 || point.z > 140 || point.z < -140) {
		return;
	}
#endif
	type = sf3000_webport_asr32(metadata, 24);
	shade = space ? 12 :
		sf3000_webport_sub32(
			sf3000_webport_asr32(
				sf3000_webport_sub32(
					sf3000_webport_i32(camera_z_position),
					sf3000_webport_lsl32(
						sf3000_webport_read_be_i32(sky_data, 4),
						17)),
				24),
			sf3000_webport_asr32(metadata, 16) & 255);
	graphic_cel_quad->x_pos0 = sf3000_webport_long(point.x);
	graphic_cel_quad->y_pos0 = sf3000_webport_long(point.z);
	graphic_cel_quad->shade = sf3000_webport_long(shade);
	command_count = sf_web_renderer_command_count();
	arm_addgamecel(graphic_cel_quad, sf3000_webport_long(type), 1024, 1024);
	if (sf_web_renderer_command_count() != command_count) {
		if (type >= 0 && type <= 3) {
			sf_web_world_renderer_append_last_sky_particle_billboard(
				(uint32_t)type);
		} else {
			sf_web_world_renderer_append_last_billboard(point.y);
		}
	}
}

void plot_stars(void)
{
	int32_t index;

	if (sky_data == NULL || star_data == NULL || graphic_cel_quad == NULL ||
	    sf3000_webport_sub32(sf3000_webport_i32(camera_z_position),
				  sf3000_webport_lsl32(
					  sf3000_webport_read_be_i32(sky_data, 4),
					  17)) < 0) {
		return;
	}
	for (index = 0; index < 128; ++index) {
		sf3000_webport_plot_star(star_data + (index * 4), 0);
	}
}

void plot_space_stars(void)
{
	int32_t index;

	if (star_data == NULL || graphic_cel_quad == NULL) {
		return;
	}
	for (index = 0; index < 128; ++index) {
		sf3000_webport_plot_star(star_data + (index * 4), 1);
	}
}

void sf3000_webport_advance_world_animation_state(void)
{
	int32_t index;
	int32_t radar_delta;

	radar_delta = sf_web_fixed_step_scale_legacy_delta(32);

	static_graphic_radar_phase = sf3000_webport_sub32(
		static_graphic_radar_phase, radar_delta);
	if (static_graphic_radar_phase < 0) {
		static_graphic_radar_phase = sf3000_webport_add32(
			static_graphic_radar_phase, 1024);
	}

	if (!sf_web_fixed_step_is_reference_tick() || star_data == NULL ||
	    sf3000_webport_i32(are_we_in_space_or_wot) == 0) {
		return;
	}
	for (index = 0; index < 128; ++index) {
		int32_t *star = star_data + (index * 4);

		star[0] = sf3000_webport_add32(
			star[0], sf3000_webport_asr32(
					 sf3000_webport_i32(camera_x_velocity), 14)) &
			  0xffff;
		star[1] = sf3000_webport_add32(
			star[1], sf3000_webport_asr32(
					 sf3000_webport_i32(camera_y_velocity), 14)) &
			  0xffff;
		star[2] = sf3000_webport_sub32(
			star[2], sf3000_webport_asr32(
					 sf3000_webport_i32(camera_z_velocity), 14)) &
			  0xffff;
		star[0] = sf3000_webport_sub32(star[0], 1 << 15);
		star[1] = sf3000_webport_sub32(star[1], 1 << 15);
		star[2] = sf3000_webport_sub32(star[2], 1 << 15);
	}
}

static void sf3000_webport_plot_planet(int32_t x, int32_t y, int32_t z,
					int32_t sprite)
{
	sf3000_webport_vec3 point = { x, y, z };
	uint32_t command_count;

	if (graphic_cel_quad == NULL) {
		return;
	}
	sf3000_webport_rotate_camera(&point);
	if (point.y < 8192) {
		return;
	}
	sf3000_webport_project(&point, 0);
	graphic_cel_quad->x_pos0 =
		sf3000_webport_long(sf3000_webport_sub32(point.x, 24));
	graphic_cel_quad->y_pos0 =
		sf3000_webport_long(sf3000_webport_sub32(point.z, 24));
	graphic_cel_quad->shade = 12;
	command_count = sf_web_renderer_command_count();
	arm_addgamecel(graphic_cel_quad, sf3000_webport_long(sprite), 1024, 1024);
	if (sf_web_renderer_command_count() != command_count)
		sf_web_world_renderer_append_last_billboard(SF_WEB_WORLD_FAR_DEPTH);
}

void plot_planets(void)
{
	sf3000_webport_plot_planet(sf3000_webport_i32(planet_1_x_pos),
				   sf3000_webport_i32(planet_1_y_pos),
				   sf3000_webport_i32(planet_1_z_pos), 14);
	sf3000_webport_plot_planet(sf3000_webport_i32(planet_2_x_pos),
				   sf3000_webport_i32(planet_2_y_pos),
				   sf3000_webport_i32(planet_2_z_pos), 15);
}

void rotate_sky_node(void *node_data)
{
	rotate_node *node = node_data;
	sf3000_webport_vec3 point = {
		sf3000_webport_i32(node->x_pos),
		sf3000_webport_i32(node->y_pos),
		sf3000_webport_i32(node->z_pos)
	};

	sf3000_webport_rotate_y(&point,
				sf3000_webport_i32(camera_y_rotation));
	sf3000_webport_rotate_z(&point,
				sf3000_webport_i32(camera_z_rotation));
	node->x_pos = sf3000_webport_long(point.x);
	node->y_pos = sf3000_webport_long(point.y);
	node->z_pos = sf3000_webport_long(point.z);
}

void rotate_sky(void *sky)
{
	int32_t *data = sky;

	data[1] = 240;
	data[0] = sf3000_webport_asr32(
		sf3000_webport_i32(camera_z_position), 17);
}

void setup_rotations(void)
{
	int32_t axis;

	sf3000_webport_build_camera_basis();
	for (axis = 0; axis < 3; ++axis) {
		sf3000_webport_vec3 point = { 0, 0, 0 };
		sf3000_webport_vec3 radar_point = { 0, 0, 0 };

		if (axis == 0) {
			point.x = radar_point.x = 1024;
		} else if (axis == 1) {
			point.y = radar_point.y = 1024;
		} else {
			point.x = sf3000_webport_i32(silly_x);
			point.y = sf3000_webport_i32(silly_y);
			point.z = 1024;
			radar_point = point;
		}
		sf3000_webport_rotate_camera(&point);
		static_graphic_rotation[axis] = point;
	}

	for (axis = 0; axis < 3; ++axis) {
		sf3000_webport_vec3 point = { 0, 0, 0 };
		int32_t angle = sf3000_webport_sub32(
			sf3000_webport_i32(camera_x_rotation),
			static_graphic_radar_phase);

		if (angle < 0) {
			angle = sf3000_webport_add32(angle, 1024);
		}
		if (axis == 0) {
			point.x = 1024;
		} else if (axis == 1) {
			point.y = 1024;
		} else {
			point.x = sf3000_webport_i32(silly_x);
			point.y = sf3000_webport_i32(silly_y);
			point.z = 1024;
		}
		sf3000_webport_rotate_x(&point, angle);
		sf3000_webport_rotate_y(
			&point, sf3000_webport_i32(camera_y_rotation));
		sf3000_webport_rotate_z(
			&point, sf3000_webport_i32(camera_z_rotation));
		static_graphic_radar_rotation[axis] = point;
	}
}

void machine_code_constants(void *constants)
{
	void **data = constants;

	if (data == NULL) {
		return;
	}
	perspective_data = data[1];
	landscape_heights = data[3];
	graphic_cel_quad = data[5];
	graphic_rotated_data = data[10];
	graphic_screen_data = data[11];
	star_data = data[12];
	poly_map_data = data[13];
	sky_data = data[14];
	graphics_data = data[15];
	if (graphics_data != NULL) {
		static_graphics_data =
			graphics_data + sf3000_webport_read_i32(graphics_data, 0);
		ships_data = graphics_data +
			     sf3000_webport_read_i32(graphics_data, 4);
		explosion_bits_data = graphics_data +
				      sf3000_webport_read_i32(graphics_data, 8);
	}
	sf3000_webport_build_camera_basis();
}

void plot_spinning_ship(long unused_x, long unused_y)
{
	static int32_t spinning_ship[7];

	(void)unused_x;
	(void)unused_y;
	spinning_ship[1] = (1 << 27) - (1 << 25) - (1 << 25);
	spinning_ship[6] = (1 << 5) + 2;
	spinning_ship[3] = sf3000_webport_sub32(spinning_ship[3], 256);
	if (spinning_ship[3] < 0) {
		spinning_ship[3] = sf3000_webport_add32(spinning_ship[3], 1024 * 1024);
	}
	spinning_ship[4] = sf3000_webport_sub32(spinning_ship[4], 3 * 1024);
	if (spinning_ship[4] < 0) {
		spinning_ship[4] = sf3000_webport_add32(spinning_ship[4], 1024 * 1024);
	}
	spinning_ship[5] = sf3000_webport_sub32(spinning_ship[5], 7 * 1024);
	if (spinning_ship[5] < 0) {
		spinning_ship[5] = sf3000_webport_add32(spinning_ship[5], 1024 * 1024);
	}
	camera_x_position = 0;
	camera_y_position = 0;
	camera_z_position = 0;
	camera_x_rotation = 0;
	camera_y_rotation = 0;
	camera_z_rotation = 0;
	sf3000_webport_build_camera_basis();
	sf3000_webport_plot_ship_internal((const uint8_t *)spinning_ship);
}
