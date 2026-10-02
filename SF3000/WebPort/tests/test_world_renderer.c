#include "../sf_web_world_renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SFWebRenderQuad last_command;

SFWebRenderQuad *sf_web_renderer_last_command(void)
{
	return &last_command;
}

static void fail(const char *message)
{
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}

int main(void)
{
	const SFWebWorldQuad *commands;
	SFWebTerrainStateUpload upload;
	const SFWebTerrainFrame *terrain_frame;
	SFWebRenderQuad terrain_material = { 0 };
	uint8_t terrain_heights[SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION *
		SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION];
	uint8_t terrain_tiles[SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION *
		SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION];
	int32_t terrain_height_offsets[SF_WEB_TERRAIN_TILE_COUNT * 4];

	memset(&last_command, 0, sizeof(last_command));
	last_command.x[0] = 160;
	last_command.y[0] = 120;
	last_command.x[1] = 352;
	last_command.y[1] = 120;
	last_command.x[2] = 352;
	last_command.y[2] = 312;
	last_command.x[3] = 160;
	last_command.y[3] = 312;

	sf_web_world_renderer_initialise();
	if (sf_web_world_renderer_append_last_billboard(100) != 0)
		fail("could not append billboard");

	commands = sf_web_world_renderer_commands();
	if (sf_web_world_renderer_command_count() != 1 ||
	    (last_command.encoding & SF_WEB_RENDER_ENCODING_WORLD) == 0 ||
	    commands[0].view_x[0] != 0 || commands[0].view_x[1] != 100 ||
	    commands[0].view_z[0] != 0 || commands[0].view_z[2] != 100 ||
	    commands[0].view_y[0] != 100 || commands[0].view_y[3] != 100) {
		fail("billboard did not use the legacy projection focal length");
	}

	sf_web_world_renderer_set_terrain_height_map(terrain_heights);
	sf_web_world_renderer_set_terrain_static_data(terrain_tiles,
		terrain_height_offsets);
	sf_web_world_renderer_finish_terrain_material_collection();
	if (sf_web_world_renderer_terrain_state_upload(&upload) == 0 ||
	    upload.heights != terrain_heights || upload.tiles != terrain_tiles ||
	    upload.full == 0 ||
	    upload.x != 0 || upload.y != 0 ||
	    upload.width != SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION ||
	    upload.height != SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION) {
		fail("initial terrain upload was not the complete terrain state");
	}
	sf_web_world_renderer_acknowledge_terrain_state_upload();
	if (sf_web_world_renderer_terrain_state_upload(&upload) != 0)
		fail("acknowledged terrain upload remained dirty");

	sf_web_world_renderer_set_terrain_frame(-128, -127, -100, -200, -300,
		1, 2, 3, 4, 5, 6, 1008);
	terrain_frame = sf_web_world_renderer_terrain_frame();
	if (terrain_frame->wave_phase != 1008 ||
	    sf_web_world_renderer_terrain_state_upload(&upload) != 0) {
		fail("terrain wave phase changed the terrain-state upload");
	}

	sf_web_world_renderer_mark_terrain_height_change(40, 80);
	sf_web_world_renderer_mark_terrain_height_change(43, 84);
	if (sf_web_world_renderer_terrain_state_upload(&upload) == 0 ||
	    upload.full != 0 || upload.x != 40 || upload.y != 80 ||
	    upload.width != 4 || upload.height != 5) {
		fail("terrain changes did not coalesce into one upload region");
	}
	sf_web_world_renderer_acknowledge_terrain_state_upload();

	terrain_material.source = 1234;
	terrain_material.palette = 5678;
	terrain_material.width = 16;
	terrain_material.height = 16;
	if (sf_web_world_renderer_set_terrain_tile_material(42,
		&terrain_material) != 0) {
		fail("could not record terrain tile material");
	}
	sf_web_world_renderer_finish_terrain_material_collection();
	if (sf_web_world_renderer_terrain_frame()->tile_material_dirty[42] == 0) {
		fail("terrain material did not mark its texture-array layer dirty");
	}
	sf_web_world_renderer_acknowledge_terrain_material_upload();
	if (sf_web_world_renderer_terrain_frame()->tile_material_dirty[42] != 0) {
		fail("terrain material dirty state was not acknowledged");
	}
	terrain_tiles[83 * SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION + 42] = 42;
	sf_web_world_renderer_mark_terrain_tile_change(42, 83, 42);
	if (sf_web_world_renderer_terrain_material_collection_required() != 0 ||
	    sf_web_world_renderer_terrain_state_upload(&upload) == 0 ||
	    upload.full != 0 || upload.x != 42 || upload.y != 83 ||
	    upload.width != 1 || upload.height != 1) {
		fail("terrain tile change was not a precise combined-state upload");
	}
	sf_web_world_renderer_acknowledge_terrain_state_upload();

	sf_web_world_renderer_mark_terrain_height_change(40, 80);
	terrain_tiles[84 * SF_WEB_TERRAIN_HEIGHT_MAP_DIMENSION + 43] = 42;
	sf_web_world_renderer_mark_terrain_tile_change(43, 84, 42);
	if (sf_web_world_renderer_terrain_state_upload(&upload) == 0 ||
	    upload.full != 0 || upload.x != 40 || upload.y != 80 ||
	    upload.width != 4 || upload.height != 5) {
		fail("height and tile changes did not share one upload region");
	}
	sf_web_world_renderer_acknowledge_terrain_state_upload();

	sf_web_world_renderer_set_terrain_frame(-128, -127, -100, -200, -300,
		1, 2, 3, 4, 5, 6, 1008);
	terrain_frame = sf_web_world_renderer_terrain_frame();
	if (terrain_frame->active == 0 || terrain_frame->heights != terrain_heights ||
		terrain_frame->tiles != terrain_tiles ||
		terrain_frame->height_offsets != terrain_height_offsets ||
		terrain_frame->tile_material_valid[42] == 0 ||
		terrain_frame->tile_materials[42].source != terrain_material.source ||
		terrain_frame->map_x != -128 || terrain_frame->map_y != -127 ||
		terrain_frame->origin_z != -300 ||
		terrain_frame->vertical_z != 6 ||
		terrain_frame->wave_phase != 1008 ||
		terrain_frame->tile_material_generation == 0 ||
		sf_web_world_renderer_terrain_material_collection_required() != 0) {
		fail("persistent terrain frame was not configured");
	}
	sf_web_world_renderer_reset();
	if (sf_web_world_renderer_terrain_frame()->active != 0)
		fail("terrain frame remained active after the frame reset");
	return 0;
}
