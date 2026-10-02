#include "sf3000_webport_renderer.h"

#define SF3000_WEBPORT_POLY_CLIP_DISTANCE 1280
#define SF3000_WEBPORT_LASER_HEADER_BYTES 12

void plot_laser(void *laser)
{
	const uint8_t *data = (const uint8_t *)laser +
			      SF3000_WEBPORT_LASER_HEADER_BYTES;
	sf3000_webport_vec3 end;
	sf3000_webport_vec3 start;
	sf3000_webport_celdata *quad;
	int32_t end_scale;
	int32_t start_scale;
	int32_t end_x;
	int32_t end_y;
	int32_t start_x;
	int32_t start_y;
	int32_t dx;
	int32_t dy;
	int32_t width_x = -128;
	int32_t width_y = 128;
	int32_t end_width_x;
	int32_t end_width_y;
	int32_t start_width_x;
	int32_t start_width_y;
	int32_t points[8];
	int32_t type;

	start.x = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_read_i32(data, 0),
				     sf3000_webport_i32(camera_x_position)), 12);
	start.y = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_read_i32(data, 4),
				     sf3000_webport_i32(camera_y_position)), 12);
	start.z = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_i32(camera_z_position),
				     sf3000_webport_read_i32(data, 8)), 12);
	sf3000_webport_rotate_camera(&start);

	if (sf3000_webport_abs32(start.x) >
		    sf3000_webport_add32(start.y, 2048) ||
	    sf3000_webport_abs32(start.z) >
		    sf3000_webport_add32(start.y, 2048)) {
		return;
	}

	end.x = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_read_i32(data, 12),
				     sf3000_webport_i32(camera_x_position)), 12);
	end.y = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_read_i32(data, 16),
				     sf3000_webport_i32(camera_y_position)), 12);
	end.z = sf3000_webport_asr32(
		sf3000_webport_sub32(sf3000_webport_i32(camera_z_position),
				     sf3000_webport_read_i32(data, 20)), 12);
	sf3000_webport_rotate_camera(&end);

	if (end.y < SF3000_WEBPORT_POLY_CLIP_DISTANCE) {
		if (start.y < SF3000_WEBPORT_POLY_CLIP_DISTANCE) {
			return;
		}
		sf3000_webport_clip_vec3(&end, &start);
	} else if (start.y < SF3000_WEBPORT_POLY_CLIP_DISTANCE) {
		sf3000_webport_clip_vec3(&end, &start);
	}

	end_scale = sf3000_webport_perspective_scale(end.y, 0);
	start_scale = sf3000_webport_perspective_scale(start.y, 0);
	end_x = sf3000_webport_asr32(sf3000_webport_mul32(end.x, end_scale), 16);
	end_y = sf3000_webport_asr32(sf3000_webport_mul32(end.z, end_scale), 16);
	start_x =
		sf3000_webport_asr32(sf3000_webport_mul32(start.x, start_scale), 16);
	start_y =
		sf3000_webport_asr32(sf3000_webport_mul32(start.z, start_scale), 16);

	dx = sf3000_webport_sub32(end_x, start_x);
	dy = sf3000_webport_sub32(end_y, start_y);
	if (sf3000_webport_abs32(dx) >
	    sf3000_webport_lsl32(sf3000_webport_abs32(dy), 1)) {
		width_x = 0;
	}
	if (sf3000_webport_abs32(dy) >
	    sf3000_webport_lsl32(sf3000_webport_abs32(dx), 1)) {
		width_y = 0;
	}
	if (dx < 0) {
		width_y = sf3000_webport_sub32(0, width_y);
	}
	if (dy < 0) {
		width_x = sf3000_webport_sub32(0, width_x);
	}

	end_width_x =
		sf3000_webport_asr32(sf3000_webport_mul32(width_x, end_scale), 17);
	end_width_y =
		sf3000_webport_asr32(sf3000_webport_mul32(width_y, end_scale), 17);
	start_width_x =
		sf3000_webport_asr32(sf3000_webport_mul32(width_x, start_scale), 17);
	start_width_y =
		sf3000_webport_asr32(sf3000_webport_mul32(width_y, start_scale), 17);
	if (end_width_x == 0) {
		end_width_x = 1;
	}
	if (end_width_y == 0) {
		end_width_y = 1;
	}
	if (start_width_x == 0) {
		start_width_x = 1;
	}
	if (start_width_y == 0) {
		start_width_y = 1;
	}

	points[0] = sf3000_webport_add32(end_x, end_width_x);
	points[1] = sf3000_webport_add32(end_y, end_width_y);
	points[2] = sf3000_webport_add32(start_x, start_width_x);
	points[3] = sf3000_webport_add32(start_y, start_width_y);
	points[4] = sf3000_webport_sub32(start_x, start_width_x);
	points[5] = sf3000_webport_sub32(start_y, start_width_y);
	points[6] = sf3000_webport_sub32(end_x, end_width_x);
	points[7] = sf3000_webport_sub32(end_y, end_width_y);

	if ((points[1] > 120 && points[3] > 120 && points[5] > 120 &&
	     points[7] > 120) ||
	    (points[1] < -120 && points[3] < -120 && points[5] < -120 &&
	     points[7] < -120) ||
	    (points[0] < -160 && points[2] < -160 && points[4] < -160 &&
	     points[6] < -160) ||
	    (points[0] > 160 && points[2] > 160 && points[4] > 160 &&
	     points[6] > 160)) {
		return;
	}

	quad = &cel_quad;
	sf3000_webport_set_quad(quad, points, 25);
	type = sf3000_webport_read_i32(data, 24) & 7;
	arm_addpolycel32(quad, sf3000_webport_long(159 - type));
}
