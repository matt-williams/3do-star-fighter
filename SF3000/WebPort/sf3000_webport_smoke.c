#include "sf3000_webport_renderer.h"
#include "sf_web_world_renderer.h"

#define SF3000_WEBPORT_SMOKE_HEADER_BYTES 12

static const int32_t smoke_clip_distances[] = {
	1024, 8192, 12 * 1024, 8192, 8192, 1024, 8192, 8192, 1024, 2048,
	8192, 8192, 6 * 1024, 4096, 1024, 4096, 4096, 8192, 8192
};

void plot_smoke(void *smoke)
{
	const uint8_t *data = (const uint8_t *)smoke +
			      SF3000_WEBPORT_SMOKE_HEADER_BYTES;
	sf3000_webport_vec3 point;
	sf3000_webport_celdata *quad;
	int32_t type = sf3000_webport_read_i32(data, 32);
	int32_t scale;
	int32_t corner = 128;
	int32_t sprite_scale;
	int32_t sprite;
	int32_t shade;
	int32_t counter;
	int32_t factor;
	int32_t points[8];
	uint32_t command_count;

	if ((uint32_t)type >=
	    sizeof(smoke_clip_distances) / sizeof(smoke_clip_distances[0])) {
		return;
	}

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

	if (point.y < smoke_clip_distances[type] ||
	    sf3000_webport_abs32(point.x) > point.y ||
	    sf3000_webport_abs32(point.z) > point.y) {
		return;
	}

	scale = sf3000_webport_perspective_scale(point.y, 0);
	sprite_scale = sf3000_webport_asr32(scale, 1);
	sprite = sf3000_webport_read_i32(data, 28);
	counter = sf3000_webport_read_i32(data, 24);
	shade = counter;

	switch (type) {
	case 0:
		shade = sf3000_webport_add32(
			counter, sf3000_webport_asr32(
					 sf3000_webport_sub32(sprite, 78), 1));
		break;
	case 5:
	case 8:
		shade = sf3000_webport_add32(counter, 4);
		break;
	case 9:
		sprite = sf3000_webport_add32(sprite,
					      sf3000_webport_sub32(7, counter));
		shade = 12;
		corner = sf3000_webport_lsl32(corner, 5);
		sprite_scale = sf3000_webport_lsl32(sprite_scale, 3);
		break;
	case 6:
	case 7:
	case 18:
		factor = sf3000_webport_add32(sf3000_webport_lsl32(counter, 1), 8);
		corner = sf3000_webport_mul32(corner, factor);
		sprite_scale = sf3000_webport_mul32(sprite_scale, factor);
		if (type == 6 || type == 7) {
			corner = sf3000_webport_asr32(corner, 1);
			sprite_scale = sf3000_webport_asr32(sprite_scale, 1);
		}
		if (type == 7 || type == 18) {
			shade = 12;
		}
		break;
	case 14:
	case 16:
		factor = sf3000_webport_add32(sf3000_webport_lsl32(counter, 1), 8);
		corner = sf3000_webport_asr32(
			sf3000_webport_mul32(corner, factor), 3);
		sprite_scale = sf3000_webport_asr32(
			sf3000_webport_mul32(sprite_scale, factor), 3);
		shade = 12;
		break;
	case 2:
		factor = sf3000_webport_add32(sf3000_webport_lsl32(counter, 1), 16);
		corner = sf3000_webport_mul32(corner, factor);
		sprite_scale = sf3000_webport_mul32(sprite_scale, factor);
		corner = sf3000_webport_sub32(
			corner, sf3000_webport_asr32(corner, 2));
		sprite_scale = sf3000_webport_sub32(
			sprite_scale, sf3000_webport_asr32(sprite_scale, 2));
		break;
	case 12:
	case 15:
		factor = sf3000_webport_add32(sf3000_webport_lsl32(counter, 1), 16);
		corner = sf3000_webport_asr32(
			sf3000_webport_mul32(corner, factor), 2);
		sprite_scale = sf3000_webport_asr32(
			sf3000_webport_mul32(sprite_scale, factor), 2);
		break;
	case 13:
	case 17:
		factor = sf3000_webport_add32(sf3000_webport_lsl32(counter, 1), 16);
		corner = sf3000_webport_mul32(corner, factor);
		sprite_scale = sf3000_webport_mul32(sprite_scale, factor);
		break;
	case 11:
		factor = sf3000_webport_add32(
			sf3000_webport_lsl32(sf3000_webport_sub32(16, counter), 2),
			32);
		corner = sf3000_webport_asr32(
			sf3000_webport_mul32(corner, factor), 1);
		sprite_scale = sf3000_webport_asr32(
			sf3000_webport_mul32(sprite_scale, factor), 1);
		shade = 12;
		break;
	default:
		/* Types 1, 3, 4, and 10 use the ordinary explosion scaler. */
		factor = sf3000_webport_add32(sf3000_webport_lsl32(counter, 1), 16);
		corner = sf3000_webport_asr32(
			sf3000_webport_mul32(corner, factor), 1);
		sprite_scale = sf3000_webport_asr32(
			sf3000_webport_mul32(sprite_scale, factor), 1);
		break;
	}

	points[0] = sf3000_webport_asr32(
		sf3000_webport_mul32(
			sf3000_webport_sub32(point.x, corner), scale), 16);
	points[1] = sf3000_webport_asr32(
		sf3000_webport_mul32(
			sf3000_webport_sub32(point.z, corner), scale), 16);
	quad = &cel_quad;
	quad->x_pos0 = sf3000_webport_long(points[0]);
	quad->y_pos0 = sf3000_webport_long(points[1]);
	quad->shade = sf3000_webport_long(shade);
	command_count = sf_web_renderer_command_count();
	arm_addgamecel(quad, sf3000_webport_long(sprite),
		       sf3000_webport_long(sprite_scale),
		       sf3000_webport_long(sprite_scale));
	if (sf_web_renderer_command_count() != command_count)
		sf_web_world_renderer_append_last_billboard(point.y);
}
