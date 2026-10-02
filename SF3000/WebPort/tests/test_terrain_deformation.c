#include "../sf_web_renderer.h"
#include "../sf_web_world_renderer.h"
#include "../../SFlib/Explosion.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char height_map[256][256];
unsigned char sprite_map[256][256];
char poly_map[128][128];
char collision_map[128][128];

#if defined(_MSC_VER)
long which_graphics_set;
long *static_graphics_adr;
long *ships_adr;
ship_list ships;
explosion_list explosions;

void *armlink_addtolist(void *list)
{
	(void)list;
	return NULL;
}

void *armlink_sorttolist(void *list, long sortword)
{
	(void)list;
	(void)sortword;
	return NULL;
}

void armlink_deleteitem(void *item, void *list)
{
	(void)item;
	(void)list;
}

void status_groundhit(long x, long y)
{
	(void)x;
	(void)y;
}

void message_addscore(long score)
{
	(void)score;
}

void add_bit(long a, long b, long c, long d, long e, long f, long g,
	long h, long i, long j, long k, long l)
{
	(void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
	(void)g; (void)h; (void)i; (void)j; (void)k; (void)l;
}

void add_smoke(long a, long b, long c, long d, long e, long f, long g,
	long h)
{
	(void)a; (void)b; (void)c; (void)d;
	(void)e; (void)f; (void)g; (void)h;
}

void make_sound(long x, long y, long z, long sound)
{
	(void)x;
	(void)y;
	(void)z;
	(void)sound;
}

void add_bonus(long a, long b, long c, long d, long e, long f, long g)
{
	(void)a; (void)b; (void)c; (void)d;
	(void)e; (void)f; (void)g;
}

void control_registerdeath(void *ship)
{
	(void)ship;
}

void rotate_node_from_c(void *node)
{
	(void)node;
}

long check_collision(long x, long y, long z)
{
	(void)x;
	(void)y;
	(void)z;
	return 0;
}
#endif

void armburn_addexplosion(long x, long y, long type)
{
	(void)x;
	(void)y;
	(void)type;
}

long arm_random(void)
{
	return 0;
}

void explosion_check_this_graphic(long x, long y)
{
	(void)x;
	(void)y;
}

SFWebRenderQuad *sf_web_renderer_last_command(void)
{
	return NULL;
}

static void fail(const char *message)
{
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}

static long terrain_coordinate(uint32_t coordinate)
{
	return (long)((coordinate << 24) - (1u << 23));
}

static void expect_upload(uint32_t x, uint32_t y, uint32_t width,
	uint32_t height)
{
	SFWebTerrainStateUpload upload;

	if (sf_web_world_renderer_terrain_state_upload(&upload) == 0 ||
		upload.full != 0 || upload.x != x || upload.y != y ||
		upload.width != width || upload.height != height ||
		upload.heights != (const uint8_t *)height_map ||
		upload.tiles != (const uint8_t *)sprite_map) {
		fail("terrain deformation did not produce the expected state rectangle");
	}
	sf_web_world_renderer_acknowledge_terrain_state_upload();
}

int main(void)
{
	SFWebTerrainStateUpload upload;

	memset(height_map, 17, sizeof(height_map));
	height_map[10][20] = 50;
	height_map[14][24] = 50;
	height_map[70][69] = 120;
	height_map[70][70] = 100;
	height_map[90][90] = 20;

	sf_web_world_renderer_initialise();
	sf_web_world_renderer_set_terrain_height_map((const uint8_t *)height_map);
	sf_web_world_renderer_set_terrain_static_data((const uint8_t *)sprite_map,
		NULL);
	sf_web_world_renderer_finish_terrain_material_collection();
	if (sf_web_world_renderer_terrain_state_upload(&upload) == 0 ||
		upload.full == 0) {
		fail("initial terrain map was not uploaded");
	}
	sf_web_world_renderer_acknowledge_terrain_state_upload();

	dent_ground(terrain_coordinate(20), terrain_coordinate(10), 1);
	dent_ground(terrain_coordinate(24), terrain_coordinate(14), 1);
	if (height_map[10][20] != 49 || height_map[14][24] != 49)
		fail("ground dents did not change terrain heights");
	expect_upload(20, 10, 5, 5);

	dent_ground(terrain_coordinate(20), terrain_coordinate(10), 0);
	if (sf_web_world_renderer_terrain_state_upload(&upload) != 0)
		fail("unchanged ground height dirtied the terrain texture");

	dent_ground(terrain_coordinate(70), terrain_coordinate(70), 20);
	if (height_map[70][70] != 80 || height_map[70][69] != 96)
		fail("recursive ground dent did not update neighbouring terrain");
	expect_upload(69, 70, 2, 1);

	undent_ground(terrain_coordinate(90), terrain_coordinate(90), 5);
	if (height_map[90][90] != 25)
		fail("ground undent did not change terrain height");
	expect_upload(90, 90, 1, 1);

	undent_ground(terrain_coordinate(90), terrain_coordinate(90), 0);
	if (sf_web_world_renderer_terrain_state_upload(&upload) != 0)
		fail("unchanged ground undent dirtied the terrain texture");
	return 0;
}
