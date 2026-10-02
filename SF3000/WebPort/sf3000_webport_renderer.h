#ifndef SF3000_WEBPORT_RENDERER_H
#define SF3000_WEBPORT_RENDERER_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "../SFlib/Plot_Graphic.h"
#include "../SFlib/SF_ARMCell.h"
#include "../SFlib/SF_ARMSky.h"
#include "sf_web_renderer.h"

/*
 * The original renderer exchanges coordinates and graphic assets as signed
 * 32-bit ARM words.  Keep arithmetic in this representation even when the
 * host's long or pointer type is wider.
 */
typedef struct sf3000_webport_vec3 {
	int32_t x;
	int32_t y;
	int32_t z;
} sf3000_webport_vec3;

typedef struct sf3000_webport_celdata {
	int32_t temp_cels;
	int32_t x_pos0;
	int32_t y_pos0;
	int32_t x_pos1;
	int32_t y_pos1;
	int32_t x_pos2;
	int32_t y_pos2;
	int32_t x_pos3;
	int32_t y_pos3;
	void *plotlist;
	int32_t shade;
} sf3000_webport_celdata;

extern sf3000_webport_celdata cel_quad;

/*
 * Binary graphic files contain 32-bit relocated addresses.  WebAssembly uses
 * those addresses directly.  A native test host can install a resolver for
 * its synthetic address tokens before calling machine_code_constants().
 */
typedef void *(*sf3000_webport_pointer_resolver)(uint32_t address);
void sf3000_webport_set_pointer_resolver(
	sf3000_webport_pointer_resolver resolver);

extern long pex_table[16384];
extern long pex_table_near[2048];

extern void arm_interceptplot(void *);

static inline int32_t sf3000_webport_i32(long value)
{
	return (int32_t)(uint32_t)value;
}

static inline long sf3000_webport_long(int32_t value)
{
	return (long)value;
}

static inline int32_t sf3000_webport_add32(int32_t left, int32_t right)
{
	return (int32_t)((uint32_t)left + (uint32_t)right);
}

static inline int32_t sf3000_webport_sub32(int32_t left, int32_t right)
{
	return (int32_t)((uint32_t)left - (uint32_t)right);
}

static inline int32_t sf3000_webport_mul32(int32_t left, int32_t right)
{
	return (int32_t)((uint64_t)(uint32_t)left * (uint32_t)right);
}

static inline int32_t sf3000_webport_asr32(int32_t value, unsigned int bits)
{
	uint32_t unsigned_value;

	if (bits == 0) {
		return value;
	}

	unsigned_value = (uint32_t)value >> bits;
	if (value < 0) {
		unsigned_value |= UINT32_MAX << (32U - bits);
	}
	return (int32_t)unsigned_value;
}

static inline int32_t sf3000_webport_lsl32(int32_t value, unsigned int bits)
{
	return (int32_t)((uint32_t)value << bits);
}

static inline int32_t sf3000_webport_abs32(int32_t value)
{
	return value < 0 ? sf3000_webport_sub32(0, value) : value;
}

static inline int32_t sf3000_webport_div32(int32_t numerator,
						 int32_t denominator)
{
	int64_t quotient;

	if (denominator == 0) {
		return INT32_MIN;
	}

	quotient = (int64_t)numerator / (int64_t)denominator;
	return (int32_t)(uint32_t)quotient;
}

static inline int32_t sf3000_webport_read_i32(const void *base, size_t offset)
{
	int32_t value;

	memcpy(&value, (const uint8_t *)base + offset, sizeof(value));
	return value;
}

static inline int32_t sf3000_webport_read_be_i32(const void *base,
						  size_t offset)
{
	const uint8_t *bytes = (const uint8_t *)base + offset;
	uint32_t value = ((uint32_t)bytes[0] << 24) |
		((uint32_t)bytes[1] << 16) |
		((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];

	return (int32_t)value;
}

static inline void sf3000_webport_write_i32(void *base, size_t offset,
					      int32_t value)
{
	memcpy((uint8_t *)base + offset, &value, sizeof(value));
}

void sf3000_webport_rotate_camera(sf3000_webport_vec3 *point);
int32_t sf3000_webport_perspective_scale(int32_t depth, int high_resolution);
void sf3000_webport_set_quad(sf3000_webport_celdata *quad,
			      const int32_t points[8], int32_t shade);
void sf3000_webport_clip_vec3(sf3000_webport_vec3 *first,
			       sf3000_webport_vec3 *second);
void clip_3d_line(long *x0, long *y0, long *z0, long *x1, long *y1,
		  long *z1);
void plot_static_from_grid(void *grid_reference);

#endif
