#include "../sf3000_webport_renderer.h"
#include "../sf_web_fixed_step.h"
#include "../sf_web_world_renderer.h"
#include "../../SFlib/Ship_Struct.h"

#include <stdio.h>
#include <stdlib.h>

long pex_table[16384];
long pex_table_near[2048];
sf3000_webport_celdata cel_quad;
extern char skyfile[1024];
void plot_static_graphic(long x, long y, long z, long type);
extern long planet_1_x_pos;
extern long planet_1_y_pos;
extern long planet_1_z_pos;
extern long planet_2_x_pos;
extern long planet_2_y_pos;
extern long planet_2_z_pos;
extern long are_we_in_space_or_wot;
extern long air_to_ground_scan_temp;
extern long air_to_ground_x;
extern long atg_selected;
extern long ata_selected;
void sf3000_webport_begin_target_selection(void);
void sf3000_webport_consider_static_target(void *grid_reference);
ship_stack *players_ship;

static long game_calls;
static long polygon_calls;
static long last_sprite;
static long last_scale_x;
static long last_scale_y;
static long last_shade;
static uint8_t *resolved_graphic;
static SFWebRenderQuad sky_command;
static long sky_commands;
static uint32_t renderer_command_count;
static long world_quad_calls;
static long world_billboard_calls;
static long world_projected_calls;
static long world_sky_particle_billboard_calls;
static uint32_t world_particle_atlas_slot;
static int32_t world_view_y[4];
static int32_t world_projected_depths[4];
static int32_t world_billboard_depth;

static void *resolve_graphic_address(uint32_t address)
{
	return address == 1 ? resolved_graphic : NULL;
}

static void fail(const char *message)
{
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}

static void set_word(void *base, size_t offset, int32_t value)
{
	memcpy((uint8_t *)base + offset, &value, sizeof(value));
}

static void set_be_word(void *base, size_t offset, uint32_t value)
{
	uint8_t *bytes = (uint8_t *)base + offset;

	bytes[0] = (uint8_t)(value >> 24);
	bytes[1] = (uint8_t)(value >> 16);
	bytes[2] = (uint8_t)(value >> 8);
	bytes[3] = (uint8_t)value;
}

void arm_addgamecel(void *quad, long sprite, long scale_x, long scale_y)
{
	sf3000_webport_celdata *data = quad;

	++game_calls;
	last_sprite = sprite;
	last_scale_x = scale_x;
	last_scale_y = scale_y;
	last_shade = data->shade;
	++renderer_command_count;
}

void arm_addpolycel32(void *quad, long sprite)
{
	sf3000_webport_celdata *data = quad;

	++polygon_calls;
	++renderer_command_count;
	last_sprite = sprite;
	last_shade = data->shade;
}

void arm_setpolycel32palette(void *quad, long palette)
{
	(void)quad;
	(void)palette;
}

void arm_addmonocel(void *quad, long colour, long transparent, long shade)
{
	(void)quad;
	(void)colour;
	(void)transparent;
	(void)shade;
}

void arm_interceptplot(void *quad)
{
	((sf3000_webport_celdata *)quad)->temp_cels = 0;
}

int32_t sf_web_renderer_append(const SFWebRenderQuad *command)
{
	sky_command = *command;
	++sky_commands;
	return 0;
}

uint32_t sf_web_renderer_command_count(void)
{
	return renderer_command_count;
}

int32_t sf_web_world_renderer_append_last_quad(const int32_t view_x[4],
	const int32_t view_y[4], const int32_t view_z[4])
{
	(void)view_x;
	(void)view_z;
	memcpy(world_view_y, view_y, sizeof(world_view_y));
	++world_quad_calls;
	return 0;
}

int32_t sf_web_world_renderer_append_last_model_quad(
	const int32_t view_x[4], const int32_t view_y[4],
	const int32_t view_z[4])
{
	return sf_web_world_renderer_append_last_quad(view_x, view_y, view_z);
}

int32_t sf_web_world_renderer_append_last_billboard(int32_t view_depth)
{
	world_billboard_depth = view_depth;
	++world_quad_calls;
	++world_billboard_calls;
	return 0;
}

int32_t sf_web_world_renderer_append_last_particle_billboard(
	int32_t view_depth, uint32_t atlas_slot)
{
	world_particle_atlas_slot = atlas_slot;
	return sf_web_world_renderer_append_last_billboard(view_depth);
}

int32_t sf_web_world_renderer_append_last_precise_particle_billboard(
	int32_t view_x, int32_t view_y, int32_t view_z, uint32_t atlas_slot,
	uint32_t atlas_transform)
{
	(void)view_x;
	(void)view_z;
	(void)atlas_transform;
	world_particle_atlas_slot = atlas_slot;
	return sf_web_world_renderer_append_last_billboard(view_y);
}

int32_t sf_web_world_renderer_append_last_sky_particle_billboard(
	uint32_t atlas_slot)
{
	++world_sky_particle_billboard_calls;
	world_particle_atlas_slot = atlas_slot;
	return sf_web_world_renderer_append_last_billboard(
		SF_WEB_WORLD_FAR_DEPTH);
}

int32_t sf_web_world_renderer_append_last_projected_quad(
	const int32_t view_depth[4])
{
	memcpy(world_projected_depths, view_depth, sizeof(world_projected_depths));
	++world_quad_calls;
	++world_projected_calls;
	return 0;
}

static void test_line_clipping(void)
{
	long x0 = 0;
	long y0 = 0;
	long z0 = 0;
	long x1 = 100;
	long y1 = 2560;
	long z1 = 100;

	clip_3d_line(&x0, &y0, &z0, &x1, &y1, &z1);
	if (x0 != 50 || y0 != 1280 || z0 != 50 ||
	    x1 != 100 || y1 != 2560 || z1 != 100) {
		fail("clip_3d_line did not preserve the ARM fixed-point intersection");
	}
}

static void test_perspective_scale(void)
{
	pex_table[100] = 123456;
	pex_table[400] = 654321;
	pex_table_near[100] = 234567;
	pex_table_near[400] = 765432;

	if (sf3000_webport_perspective_scale(6400, 0) != 123456) {
		fail("far perspective scale did not use the depth/64 table index");
	}
	if (sf3000_webport_perspective_scale(3200, 1) != 234567) {
		fail("near perspective scale did not use the depth/32 table index");
	}
}

static void test_sky_plot(void)
{
	int32_t colour = INT32_C(0x1f7c1f7c);
	int index;

	memset(skyfile, 0, sizeof(skyfile));
	for (index = 0; index < 256; ++index) {
		set_be_word(skyfile, 8 + ((size_t)index * 4), (uint32_t)colour);
	}
	memset(&cel_quad, 0, sizeof(cel_quad));
	cel_quad.x_pos0 = -160;
	cel_quad.y_pos0 = -120;
	cel_quad.x_pos1 = 160;
	cel_quad.y_pos1 = -120;
	cel_quad.x_pos2 = 160;
	cel_quad.y_pos2 = 120;
	cel_quad.x_pos3 = -160;
	cel_quad.y_pos3 = 120;

	arm_rendersky(0, 3);
	arm_plotsky(&cel_quad);
	if (cel_quad.temp_cels != 1 || sky_commands != 1 ||
	    sky_command.source == 0 || sky_command.palette == 0 ||
	    sky_command.width != 1 ||
	    sky_command.height != 3 ||
	    sky_command.blend != SF_WEB_RENDER_BLEND_OPAQUE ||
	    sky_command.encoding != SF_WEB_RENDER_ENCODING_SKY_GRADIENT ||
	    sky_command.shade != 7 ||
	    sky_command.x[0] != 0 || sky_command.y[0] != 0 ||
	    sky_command.x[1] != 320 || sky_command.y[1] != 0 ||
	    sky_command.x[2] != 320 || sky_command.y[2] != 240 ||
	    sky_command.x[3] != 0 || sky_command.y[3] != 240) {
		fail("arm_plotsky did not enqueue its unencoded sky strip");
	}
}

static void test_smoke_and_laser(void)
{
	uint8_t smoke[48] = { 0 };
	uint8_t laser[48] = { 0 };

	camera_x_position = 0;
	camera_y_position = 0;
	camera_z_position = 0;
	game_calls = 0;
	renderer_command_count = 0;
	world_billboard_calls = 0;
	world_projected_calls = 0;
	world_particle_atlas_slot = UINT32_MAX;
	set_word(smoke, 12 + 4, 9000 << 12);
	set_word(smoke, 12 + 24, 2);
	set_word(smoke, 12 + 28, 78);
	set_word(smoke, 12 + 32, 0);
	plot_smoke(smoke);
	if (game_calls != 1 || last_sprite != 78 || last_scale_x != 32768 ||
	    last_scale_y != 32768 || last_shade != 2 ||
	    world_billboard_calls != 1 || world_particle_atlas_slot != 12) {
		fail("plot_smoke did not select the small-thruster cel and shade");
	}

	polygon_calls = 0;
	set_word(laser, 12 + 4, 9000 << 12);
	set_word(laser, 12 + 12, 100 << 12);
	set_word(laser, 12 + 16, 10000 << 12);
	set_word(laser, 12 + 24, 1);
	plot_laser(laser);
	if (polygon_calls != 1 || last_sprite != 158 || last_shade != 25 ||
	    world_projected_calls != 1 || world_projected_depths[0] != 10000 ||
	    world_projected_depths[1] != 9000 ||
	    world_projected_depths[2] != 9000 ||
	    world_projected_depths[3] != 10000) {
		fail("plot_laser did not generate the expected laser polygon");
	}
}

static void test_stars(void)
{
	static int32_t stars[128][4];
	static int32_t sky[256];
	void *constants[16] = { 0 };

	memset(stars, 0, sizeof(stars));
	stars[0][1] = 2048;
	stars[0][3] = 1 << 24;
	constants[5] = &cel_quad;
	constants[12] = stars;
	constants[14] = sky;
	machine_code_constants(constants);
	camera_x_rotation = 0;
	camera_y_rotation = 0;
	camera_z_rotation = 0;
	camera_z_position = 2L << 24;
	setup_rotations();

	game_calls = 0;
	renderer_command_count = 0;
	world_billboard_calls = 0;
	world_sky_particle_billboard_calls = 0;
	world_particle_atlas_slot = UINT32_MAX;
	plot_stars();
	if (game_calls != 1 || last_sprite != 1 || last_shade != 2 ||
	    world_billboard_calls != 1 ||
	    world_sky_particle_billboard_calls != 1 ||
	    world_particle_atlas_slot != 1 ||
	    world_billboard_depth != SF_WEB_WORLD_FAR_DEPTH) {
		fail("plot_stars did not project the visible star with its shade");
	}

	stars[0][0] = 1 << 15;
	stars[0][1] = 1 << 15;
	stars[0][2] = 1 << 15;
	camera_x_velocity = 1 << 14;
	camera_y_velocity = 0;
	camera_z_velocity = 1 << 14;
	are_we_in_space_or_wot = 0;
	sf_web_fixed_step_simulation_reset();
	for (int step = 0; step < 5; ++step) {
		sf_web_fixed_step_begin_simulation_step();
		sf3000_webport_advance_world_animation_state();
	}
	if (stars[0][0] != (1 << 15) || stars[0][1] != (1 << 15) ||
	    stars[0][2] != (1 << 15)) {
		fail("ground stars advanced with the camera");
	}

	are_we_in_space_or_wot = 1;
	for (int step = 0; step < 5; ++step) {
		sf_web_fixed_step_begin_simulation_step();
		sf3000_webport_advance_world_animation_state();
	}
	if (stars[0][0] != 1 || stars[0][1] != 0 || stars[0][2] != -1) {
		fail("world animation update did not advance space stars once");
	}
	plot_space_stars();
	if (stars[0][0] != 1 || stars[0][1] != 0 || stars[0][2] != -1) {
		fail("plot_space_stars advanced simulation state");
	}

	game_calls = 0;
	renderer_command_count = 0;
	world_billboard_calls = 0;
	planet_1_x_pos = 200;
	planet_1_y_pos = 8192;
	planet_1_z_pos = 0;
	planet_2_x_pos = 0;
	planet_2_y_pos = 8192;
	planet_2_z_pos = 0;
	plot_planets();
	if (game_calls != 2 || last_sprite != 15 || world_billboard_calls != 2 ||
	    world_billboard_depth != SF_WEB_WORLD_FAR_DEPTH) {
		fail("plot_planets did not emit celestial sprites at world far depth");
	}
}

static void test_static_graphic(void)
{
	static int32_t rotated[256][4];
	static int32_t screen[256][2];
	static uint8_t graphics[256];
	static uint8_t graphic[80];
	static uint8_t heights[8192];
	static uint8_t poly_map[1025];
	void *constants[16] = { 0 };

	memset(rotated, 0, sizeof(rotated));
	memset(screen, 0, sizeof(screen));
	memset(graphics, 0, sizeof(graphics));
	memset(graphic, 0, sizeof(graphic));
	memset(heights, 0, sizeof(heights));
	memset(poly_map, 0, sizeof(poly_map));
	set_word(graphics, 0, 64);
	set_word(graphics, 4, 128);
	set_word(graphics, 8, 192);
	set_word(graphics, 64 + 8, 1);
	set_be_word(graphic, 0, 52);
	graphic[32] = 2;
	graphic[33] = 3;
	graphic[43] = 0x43;
	graphic[48] = 0x43;
	graphic[49] = 0xc3;
	set_be_word(graphic, 52, 0);
	set_be_word(graphic, 56, 0);
	set_be_word(graphic, 60, 1);
	set_be_word(graphic, 64, 2);
	set_be_word(graphic, 68, 3);
	graphic[72] = 0;
	graphic[73] = 17;

	resolved_graphic = graphic;
	sf3000_webport_set_pointer_resolver(resolve_graphic_address);
	constants[1] = pex_table;
	constants[3] = heights;
	constants[5] = &cel_quad;
	constants[10] = rotated;
	constants[11] = screen;
	constants[13] = poly_map;
	constants[15] = graphics;
	machine_code_constants(constants);
	camera_x_rotation = 0;
	camera_y_rotation = 0;
	camera_z_rotation = 0;
	setup_rotations();

	atg_selected = 1;
	ata_selected = 0;
	are_we_in_space_or_wot = 0;
	camera_x_position = 0;
	camera_y_position = 0;
	camera_z_position = 0;
	set_word(graphics, 64, 0);
	sf3000_webport_begin_target_selection();
	sf3000_webport_consider_static_target((void *)(uintptr_t)513);
	sf3000_webport_consider_static_target((void *)(uintptr_t)1024);
	if (air_to_ground_scan_temp != 1024) {
		fail("target selection did not retain the most centred target");
	}

	set_word(graphics, 64, 16384 << 12);
	sf3000_webport_begin_target_selection();
	sf3000_webport_consider_static_target((void *)(uintptr_t)513);
	sf3000_webport_consider_static_target((void *)(uintptr_t)1024);
	if (air_to_ground_scan_temp != 513) {
		fail("target selection did not use player distance to break an angle tie");
	}

	sf3000_webport_begin_target_selection();
	sf3000_webport_consider_static_target((void *)(uintptr_t)514);
	if (air_to_ground_scan_temp != 0 || air_to_ground_x != (1 << 30)) {
		fail("target selection admitted a centre outside the original aim cone");
	}
	atg_selected = 0;
	camera_z_position = 1L << 28;

	polygon_calls = 0;
	renderer_command_count = 0;
	world_quad_calls = 0;
	plot_static_graphic(0, 8192, 0, 0);
	if (polygon_calls != 1 || last_sprite != 0 || last_shade != 17 ||
	    world_quad_calls != 1 || world_view_y[0] <= 0) {
		fail("plot_static_graphic did not emit its visible face to the world queue");
	}

	polygon_calls = 0;
	plot_static_graphic(0, 512, 0, 0);
	if (polygon_calls != 1 || last_sprite != 0 || last_shade != 17 ||
	    world_quad_calls != 2 || world_view_y[0] >= 1280) {
		fail("plot_static_graphic discarded a near-plane face from the world queue");
	}

	heights[512] = 145;
	polygon_calls = 0;
	plot_static_from_grid((void *)(uintptr_t)128);
	if (polygon_calls != 1 || last_sprite != 0 || last_shade != 17 ||
	    world_quad_calls != 3) {
		fail("plot_static_from_grid did not emit its visible face to the world queue");
	}

	set_be_word(graphic, 56, 0);
	set_be_word(graphic, 60, 3);
	set_be_word(graphic, 64, 2);
	set_be_word(graphic, 68, 1);
	polygon_calls = 0;
	plot_static_graphic(0, 8192, 0, 0);
	if (polygon_calls != 1 || world_quad_calls != 4) {
		fail("plot_static_graphic did not queue a back-facing world face");
	}
}

int main(void)
{
	size_t index;

	for (index = 0; index < 16384; ++index) {
		pex_table[index] = 65536;
	}
	for (index = 0; index < 2048; ++index) {
		pex_table_near[index] = 65536;
	}

	test_perspective_scale();
	test_line_clipping();
	test_sky_plot();
	test_static_graphic();
	test_stars();
	test_smoke_and_laser();
	return 0;
}
