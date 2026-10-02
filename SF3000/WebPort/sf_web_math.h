#ifndef SF_WEB_MATH_H
#define SF_WEB_MATH_H

#include <stdint.h>

/*
 * The original renderer's angular unit is one 20-bit turn.  Simulation code
 * uses radians; these adapters only preserve the old ABI at its boundaries.
 * The Q12 helpers replace the former on-disk Cosine/Tangent lookup tables.
 */
#define SF_LEGACY_TURN_UNITS 1048576L
#define SF_LEGACY_TABLE_TURN_UNITS 1024L
#define SF_TRIG_Q12_ONE 4096L

float sf_legacy_rotation_to_radians(long rotation);
long sf_radians_to_legacy_rotation(float radians);
float sf_normalize_radians(float radians);
float sf_heading_radians(float x, float y);
long sf_sin_q12(long table_rotation);
long sf_cos_q12(long table_rotation);
long sf_hypot_q12(long x, long y);

#endif
