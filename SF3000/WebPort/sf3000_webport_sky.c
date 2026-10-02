#include "sf3000_webport_renderer.h"

char skyfile[1024];

static uint32_t sky_gradient_bands[400];
static int32_t sky_render_height;

static uint32_t sf3000_webport_clamp_sky_gradient_band(
	int32_t colour_band, int32_t maximum_band)
{
	uint32_t maximum_position = (uint32_t)maximum_band << 10;

	if (colour_band <= 0) {
		return 0;
	}
	if ((uint32_t)colour_band >= maximum_position) {
		return maximum_position;
	}
	return (uint32_t)colour_band;
}

void arm_rendersky(long height, long horizon)
{
	const uint8_t *sky = (const uint8_t *)skyfile;
	int32_t colour_step;
	int32_t colour_band = 0;
	int32_t maximum_band;
	int32_t row;
	int32_t scaled_height = sf3000_webport_i32(height);

	row = sf3000_webport_i32(horizon);
	if (row <= 0) {
		sky_render_height = row;
		return;
	}

	maximum_band = sf3000_webport_add32(
		sf3000_webport_asr32(scaled_height, 7),
		sf3000_webport_asr32(sf3000_webport_asr32(scaled_height, 7), 1));
	maximum_band = sf3000_webport_add32(maximum_band, 48);
	if (maximum_band > 255) {
		maximum_band = 255;
	}

	colour_step = sf3000_webport_add32(scaled_height,
					   sf3000_webport_read_be_i32(sky, 0));

	while (row >= 400) {
		colour_band = sf3000_webport_add32(
			colour_band, sf3000_webport_asr32(colour_step, 1));
		colour_step = sf3000_webport_sub32(
			colour_step, sf3000_webport_asr32(colour_step, 5));
		--row;
	}

	sky_render_height = row;
	while (row > 0) {
		sky_gradient_bands[row] = sf3000_webport_clamp_sky_gradient_band(
			colour_band, maximum_band);
		--row;
		colour_band = sf3000_webport_add32(
			colour_band, sf3000_webport_asr32(colour_step, 1));
		if (colour_step > 128) {
			colour_step = sf3000_webport_sub32(
				colour_step, sf3000_webport_asr32(colour_step, 5));
		}
	}
}

void arm_plotsky(void *cel_data)
{
	sf3000_webport_celdata *quad = cel_data;
	SFWebRenderQuad command = { 0 };
	int32_t points[8];
	int32_t height;

	if (sky_render_height <= 0) {
		return;
	}

	if (quad->temp_cels >= 1024) {
		arm_interceptplot(cel_data);
	}

	height = sky_render_height;
	points[0] = sf3000_webport_i32(quad->x_pos0);
	points[1] = sf3000_webport_i32(quad->y_pos0);
	points[2] = sf3000_webport_i32(quad->x_pos1);
	points[3] = sf3000_webport_i32(quad->y_pos1);
	points[4] = sf3000_webport_i32(quad->x_pos2);
	points[5] = sf3000_webport_i32(quad->y_pos2);
	points[6] = sf3000_webport_i32(quad->x_pos3);
	points[7] = sf3000_webport_i32(quad->y_pos3);

	command.source = (uint32_t)(uintptr_t)skyfile;
	command.palette = (uint32_t)(uintptr_t)sky_gradient_bands;
	command.x[0] = sf3000_webport_add32(points[0], 160);
	command.y[0] = sf3000_webport_add32(points[1], 120);
	command.x[1] = sf3000_webport_add32(points[2], 160);
	command.y[1] = sf3000_webport_add32(points[3], 120);
	command.x[2] = sf3000_webport_add32(points[4], 160);
	command.y[2] = sf3000_webport_add32(points[5], 120);
	command.x[3] = sf3000_webport_add32(points[6], 160);
	command.y[3] = sf3000_webport_add32(points[7], 120);
	command.shade = 7;
	command.width = 1;
	command.height = (uint32_t)height;
	command.blend = SF_WEB_RENDER_BLEND_OPAQUE;
	command.encoding = SF_WEB_RENDER_ENCODING_SKY_GRADIENT;
	command.pixc = UINT32_C(0x1F001F00);
	if (sf_web_renderer_append(&command) >= 0) {
		++quad->temp_cels;
	}
}
