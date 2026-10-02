#include "../sf_arm_port.h"
#include "../sf_web_renderer.h"
#include "../sf_web_world_renderer.h"
#include "../../SFlib/SF_ARMBurn.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char sprite_map[256][256];
unsigned char height_map[256][256];

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

int main(void)
{
	unsigned char explosion_sprites[20] = { 0 };
	unsigned char terrain_sprites[256 * 16] = { 0 };
	unsigned char terrain_map[512 * 384] = { 0 };
	unsigned char terrain_cache[64 * 128] = { 0 };
	SFWebTerrainStateUpload upload;
	SFWebRenderQuad material = { 0 };

	explosion_sprites[0] = 42;
	explosion_sprites[17] = 99;
	sf_web_world_renderer_initialise();
	armburn_initialise(explosion_sprites);
	if (!sf_web_world_renderer_terrain_tile_material_required(42) ||
		sf_web_world_renderer_terrain_tile_material_required(99)) {
		fail("only impact sprite IDs should be preloaded as terrain materials");
	}
	sf_web_world_renderer_set_terrain_height_map((const uint8_t *)height_map);
	sf_web_world_renderer_set_terrain_static_data((const uint8_t *)sprite_map,
		NULL);
	material.width = 16;
	material.height = 16;
	if (sf_web_world_renderer_set_terrain_tile_material(0, &material) != 0 ||
		sf_web_world_renderer_set_terrain_tile_material(42, &material) != 0)
		fail("could not preload required terrain materials");
	sf_web_world_renderer_finish_terrain_material_collection();
	if (sf_web_world_renderer_terrain_state_upload(&upload) == 0 ||
		upload.full == 0) {
		fail("initial terrain state was not available");
	}
	sf_web_world_renderer_acknowledge_terrain_state_upload();
	sf_web_world_renderer_acknowledge_terrain_material_upload();

	sf_armburn_bind_buffers(terrain_sprites, terrain_map, terrain_cache);
	armburn_resetexplosions();

	sprite_map[20][10] = 6;
	armburn_addexplosion(10, 20, 0);
	armburn_updateexplosions(sprite_map, NULL);

	if (sprite_map[20][10] != 42 ||
		sf_web_world_renderer_terrain_tile_material_required(42) ||
		sf_web_world_renderer_terrain_material_full_collection_required() != 0 ||
		sf_web_world_renderer_terrain_state_upload(&upload) == 0 ||
		upload.full != 0 || upload.x != 10 || upload.y != 20 ||
		upload.width != 1 || upload.height != 1 ||
		upload.heights != (const uint8_t *)height_map ||
		upload.tiles != (const uint8_t *)sprite_map) {
		fail("impact animation did not mark a precise combined terrain update");
	}
	sf_web_world_renderer_acknowledge_terrain_state_upload();

	armburn_updateexplosions(sprite_map, NULL);
	if (sf_web_world_renderer_terrain_state_upload(&upload) != 0)
		fail("unchanged impact animation dirtied terrain state");

	return 0;
}
