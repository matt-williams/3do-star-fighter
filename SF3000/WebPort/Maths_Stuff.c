#include "sf_arm_port.h"
#include "sf_web_math.h"

#include <math.h>

#if defined(SF_WEB_PORT)
#include "sf_web_fixed_step.h"
#define SF_SIMULATION_DELTA(value) sf_web_fixed_step_scale_legacy_delta(value)
#else
#define SF_SIMULATION_DELTA(value) (value)
#endif

extern unsigned char poly_map[128][128];
extern sf_arm_i32 camera_x_rotation;
extern sf_arm_i32 camera_y_rotation;
extern sf_arm_i32 camera_z_rotation;

static sf_arm_vec3 sf_camera_rotations[3];

float sf_normalize_radians(float radians)
{
    const float turn = 6.28318530717958647692f;
    float normalized = remainderf(radians, turn);

    return normalized < 0.0f ? normalized + turn : normalized;
}

float sf_legacy_rotation_to_radians(long rotation)
{
    return sf_normalize_radians((float)(rotation & (SF_LEGACY_TURN_UNITS - 1L)) *
        (6.28318530717958647692f / (float)SF_LEGACY_TURN_UNITS));
}

long sf_radians_to_legacy_rotation(float radians)
{
    return (long)lroundf(sf_normalize_radians(radians) *
        ((float)SF_LEGACY_TURN_UNITS / 6.28318530717958647692f)) &
        (SF_LEGACY_TURN_UNITS - 1L);
}

float sf_heading_radians(float x, float y)
{
    return sf_normalize_radians(atan2f(-x, y));
}

long sf_sin_q12(long table_rotation)
{
    return (long)lroundf(sinf((float)(table_rotation &
        (SF_LEGACY_TABLE_TURN_UNITS - 1L)) *
        (6.28318530717958647692f / (float)SF_LEGACY_TABLE_TURN_UNITS)) *
        (float)SF_TRIG_Q12_ONE);
}

long sf_cos_q12(long table_rotation)
{
    return (long)lroundf(cosf((float)(table_rotation &
        (SF_LEGACY_TABLE_TURN_UNITS - 1L)) *
        (6.28318530717958647692f / (float)SF_LEGACY_TABLE_TURN_UNITS)) *
        (float)SF_TRIG_Q12_ONE);
}

long sf_hypot_q12(long x, long y)
{
    return (long)lroundf(hypotf((float)x, (float)y));
}

void sf_arm_rotate_coords_x(sf_arm_vec3 *coords, sf_arm_i32 rotation)
{
    sf_arm_i32 cosine = (sf_arm_i32)sf_cos_q12(rotation);
    sf_arm_i32 sine = (sf_arm_i32)sf_sin_q12(rotation);
    sf_arm_i32 x_cos = sf_arm_asr(sf_arm_mul(coords->x, cosine), 12);
    sf_arm_i32 y_sin = sf_arm_asr(sf_arm_mul(coords->y, sine), 12);
    sf_arm_i32 x_sin = sf_arm_asr(sf_arm_mul(coords->x, sine), 12);
    sf_arm_i32 y_cos = sf_arm_asr(sf_arm_mul(coords->y, cosine), 12);

    coords->x = sf_arm_sub(x_cos, y_sin);
    coords->y = sf_arm_add(x_sin, y_cos);
}

void sf_arm_rotate_coords_y(sf_arm_vec3 *coords, sf_arm_i32 rotation)
{
    sf_arm_i32 cosine = (sf_arm_i32)sf_cos_q12(rotation);
    sf_arm_i32 sine = (sf_arm_i32)sf_sin_q12(rotation);
    sf_arm_i32 y_cos = sf_arm_asr(sf_arm_mul(coords->y, cosine), 12);
    sf_arm_i32 z_sin = sf_arm_asr(sf_arm_mul(coords->z, sine), 12);
    sf_arm_i32 y_sin = sf_arm_asr(sf_arm_mul(coords->y, sine), 12);
    sf_arm_i32 z_cos = sf_arm_asr(sf_arm_mul(coords->z, cosine), 12);

    coords->y = sf_arm_sub(y_cos, z_sin);
    coords->z = sf_arm_add(y_sin, z_cos);
}

void sf_arm_rotate_coords_z(sf_arm_vec3 *coords, sf_arm_i32 rotation)
{
    sf_arm_i32 cosine = (sf_arm_i32)sf_cos_q12(rotation);
    sf_arm_i32 sine = (sf_arm_i32)sf_sin_q12(rotation);
    sf_arm_i32 x_cos = sf_arm_asr(sf_arm_mul(coords->x, cosine), 12);
    sf_arm_i32 z_sin = sf_arm_asr(sf_arm_mul(coords->z, sine), 12);
    sf_arm_i32 x_sin = sf_arm_asr(sf_arm_mul(coords->x, sine), 12);
    sf_arm_i32 z_cos = sf_arm_asr(sf_arm_mul(coords->z, cosine), 12);

    coords->x = sf_arm_sub(x_cos, z_sin);
    coords->z = sf_arm_add(x_sin, z_cos);
}

void rotate_2d_node(void *data)
{
    sf_arm_i32 *node = (sf_arm_i32 *)data;
    sf_arm_vec3 coords = { node[0], node[1], node[2] };

    sf_arm_rotate_coords_x(&coords, node[3]);
    node[0] = coords.x;
    node[1] = coords.y;
}

/*
 * The ARM exports rotate_coords_{x,y,z}, divide, and
 * rotate_node_x_y_z_camera use an internal register ABI: some results are
 * returned in r1-r3 rather than through the C ABI.  ISO C cannot expose that
 * ABI to the unchanged renderer .s files.  WebPort callers use the fully
 * equivalent sf_arm_rotate_coords_* helpers above and sf_arm_rotate_camera
 * below; these compatibility symbols preserve the normal C result in r0.
 */
long rotate_coords_x(long x, long y, long z, long rotation)
{
    sf_arm_vec3 coords = { (sf_arm_i32)x, (sf_arm_i32)y, (sf_arm_i32)z };
    sf_arm_rotate_coords_x(&coords, (sf_arm_i32)rotation);
    return (long)coords.x;
}

long rotate_coords_y(long x, long y, long z, long rotation)
{
    sf_arm_vec3 coords = { (sf_arm_i32)x, (sf_arm_i32)y, (sf_arm_i32)z };
    sf_arm_rotate_coords_y(&coords, (sf_arm_i32)rotation);
    return (long)coords.y;
}

long rotate_coords_z(long x, long y, long z, long rotation)
{
    sf_arm_vec3 coords = { (sf_arm_i32)x, (sf_arm_i32)y, (sf_arm_i32)z };
    sf_arm_rotate_coords_z(&coords, (sf_arm_i32)rotation);
    return (long)coords.x;
}

void setup_camera_rotations(void)
{
    sf_arm_vec3 coords;

    coords.x = 4096;
    coords.y = 0;
    coords.z = 0;
    sf_arm_rotate_coords_x(&coords, camera_x_rotation);
    sf_arm_rotate_coords_y(&coords, camera_y_rotation);
    sf_arm_rotate_coords_z(&coords, camera_z_rotation);
    sf_camera_rotations[0] = coords;

    coords.x = 0;
    coords.y = 4096;
    coords.z = 0;
    sf_arm_rotate_coords_x(&coords, camera_x_rotation);
    sf_arm_rotate_coords_y(&coords, camera_y_rotation);
    sf_arm_rotate_coords_z(&coords, camera_z_rotation);
    sf_camera_rotations[1] = coords;

    coords.x = 0;
    coords.y = 0;
    coords.z = 4096;
    sf_arm_rotate_coords_x(&coords, camera_x_rotation);
    sf_arm_rotate_coords_y(&coords, camera_y_rotation);
    sf_arm_rotate_coords_z(&coords, camera_z_rotation);
    sf_camera_rotations[2] = coords;
}

void sf_arm_rotate_camera(sf_arm_vec3 *coords)
{
    sf_arm_i32 x = sf_arm_add(
        sf_arm_add(sf_arm_mul(coords->x, sf_camera_rotations[0].x),
                   sf_arm_mul(coords->y, sf_camera_rotations[1].x)),
        sf_arm_mul(coords->z, sf_camera_rotations[2].x));
    sf_arm_i32 y = sf_arm_add(
        sf_arm_add(sf_arm_mul(coords->x, sf_camera_rotations[0].y),
                   sf_arm_mul(coords->y, sf_camera_rotations[1].y)),
        sf_arm_mul(coords->z, sf_camera_rotations[2].y));
    sf_arm_i32 z = sf_arm_add(
        sf_arm_add(sf_arm_mul(coords->x, sf_camera_rotations[0].z),
                   sf_arm_mul(coords->y, sf_camera_rotations[1].z)),
        sf_arm_mul(coords->z, sf_camera_rotations[2].z));

    coords->x = sf_arm_asr(x, 12);
    coords->y = sf_arm_asr(y, 12);
    coords->z = sf_arm_asr(z, 12);
}

void rotate_node_x_y_z_camera(void *data)
{
    sf_arm_i32 *node = (sf_arm_i32 *)data;
    sf_arm_vec3 coords = { node[0], node[1], node[2] };

    sf_arm_rotate_camera(&coords);
    node[0] = coords.x;
    node[1] = coords.y;
    node[2] = coords.z;
}

void rotate_node_from_c(void *data)
{
    sf_arm_i32 *node = (sf_arm_i32 *)data;
    sf_arm_vec3 coords = {
        sf_arm_asr(node[0], 12),
        sf_arm_asr(node[1], 12),
        sf_arm_asr(node[2], 12)
    };

    sf_arm_rotate_coords_z(&coords, sf_arm_sub(1024, sf_arm_asr(node[5], 10)));
    sf_arm_rotate_coords_y(&coords, sf_arm_asr(node[4], 10));
    sf_arm_rotate_coords_x(&coords, sf_arm_asr(node[3], 10));

    node[0] = sf_arm_lsl(coords.x, 12);
    node[1] = sf_arm_neg(sf_arm_lsl(coords.y, 12));
    node[2] = sf_arm_lsl(coords.z, 12);
}

void oppo_rotate_node_from_c(void *data)
{
    sf_arm_i32 *node = (sf_arm_i32 *)data;
    sf_arm_vec3 coords = {
        sf_arm_asr(node[0], 12),
        sf_arm_asr(node[1], 12),
        sf_arm_asr(node[2], 12)
    };

    sf_arm_rotate_coords_x(&coords, sf_arm_asr(node[3], 10));
    sf_arm_rotate_coords_y(&coords, sf_arm_asr(node[4], 10));
    sf_arm_rotate_coords_z(&coords, sf_arm_sub(1024, sf_arm_asr(node[5], 10)));

    node[0] = sf_arm_lsl(coords.x, 12);
    node[1] = sf_arm_neg(sf_arm_lsl(coords.y, 12));
    node[2] = sf_arm_lsl(coords.z, 12);
}

void rotate_land_node_from_c(void *data)
{
    sf_arm_i32 *node = (sf_arm_i32 *)data;
    sf_arm_vec3 coords = { node[0], node[1], node[2] };

    sf_arm_rotate_coords_x(&coords, sf_arm_asr(node[3], 10));
    sf_arm_rotate_coords_y(&coords, sf_arm_asr(node[4], 10));
    sf_arm_rotate_coords_z(&coords, sf_arm_asr(node[5], 10));

    node[0] = coords.x;
    node[1] = coords.y;
    node[2] = coords.z;
}

typedef struct sf_arm_division {
    sf_arm_i32 quotient;
    sf_arm_i32 remainder;
} sf_arm_division;

static sf_arm_division sf_arm_divide(sf_arm_i32 numerator, sf_arm_i32 denominator)
{
    sf_arm_u32 quotient = 0;
    sf_arm_u32 bit = 1;
    sf_arm_u32 unsigned_numerator;
    sf_arm_u32 unsigned_denominator;
    sf_arm_u32 sign = ((sf_arm_u32)numerator ^ (sf_arm_u32)denominator) & 0x80000000U;

    numerator = sf_arm_abs(numerator);
    denominator = sf_arm_abs(denominator);
    unsigned_numerator = (sf_arm_u32)numerator;
    unsigned_denominator = (sf_arm_u32)denominator;
    if (denominator == 0) {
        /* ARM's "mvn r3, #&80000000" yields 0x7fffffff. */
        quotient = INT32_MAX;
    } else {
        while (unsigned_denominator < unsigned_numerator) {
            unsigned_denominator <<= 1U;
            bit <<= 1U;
        }

        while (bit != 0) {
            if (unsigned_numerator >= unsigned_denominator) {
                unsigned_numerator -= unsigned_denominator;
                quotient += bit;
            }
            bit >>= 1U;
            if (bit != 0) {
                unsigned_denominator >>= 1U;
            }
        }
    }

    if (sign != 0U) {
        quotient = 0U - quotient;
    }

    {
        sf_arm_division result = { (sf_arm_i32)quotient,
                                   (sf_arm_i32)unsigned_numerator };
        return result;
    }
}

long divide(long numerator, long denominator)
{
    return (long)sf_arm_divide((sf_arm_i32)numerator,
                               (sf_arm_i32)denominator).quotient;
}

long find_rotation(long x_value, long y_value)
{
    sf_arm_i32 x = (sf_arm_i32)x_value;
    sf_arm_i32 y = (sf_arm_i32)y_value;

    if (x == 0 && y == 0) {
        return 0;
    }

    return sf_radians_to_legacy_rotation(sf_heading_radians((float)x,
        (float)y));
}

static sf_arm_i32 sf_arm_planar_distance(sf_arm_i32 x, sf_arm_i32 y)
{
    return (sf_arm_i32)sf_hypot_q12(x, y);
}

long find_2d_distance(long x, long y)
{
    return (long)sf_arm_planar_distance((sf_arm_i32)x, (sf_arm_i32)y);
}

int target_finder(void *data)
{
    sf_arm_i32 *target = (sf_arm_i32 *)data;
    sf_arm_i32 x = sf_arm_asr(sf_arm_sub(target[3], target[0]), 14);
    sf_arm_i32 y = sf_arm_asr(sf_arm_sub(target[1], target[4]), 14);
    sf_arm_i32 z = sf_arm_asr(sf_arm_sub(target[2], target[5]), 14);
    sf_arm_i32 x_rotation = (sf_arm_i32)find_rotation(x, y);
    sf_arm_i32 planar = sf_arm_planar_distance(x, y);
    sf_arm_i32 y_rotation = (sf_arm_i32)find_rotation(z, planar);
    target[6] = x_rotation;
    target[7] = y_rotation;
    target[8] = sf_arm_lsl((sf_arm_i32)lroundf(hypotf((float)planar,
        (float)z) * (float)SF_TRIG_Q12_ONE), 2);
    return 0;
}

int mc_smoke_mover(void *data)
{
    /*
     * smoke_stack starts with the original 12-byte link_header.  This raw
     * offset is intentional: wasm32/arm32 keeps the legacy source ABI.
     */
    sf_arm_i32 *smoke = (sf_arm_i32 *)((unsigned char *)data + 12);

    smoke[3] = sf_arm_add(smoke[3], (sf_arm_i32)SF_SIMULATION_DELTA(
        -sf_arm_asr(smoke[3], 6)));
    smoke[4] = sf_arm_add(smoke[4], (sf_arm_i32)SF_SIMULATION_DELTA(
        -sf_arm_asr(smoke[4], 6)));
    smoke[5] = sf_arm_add(smoke[5], (sf_arm_i32)SF_SIMULATION_DELTA(
        -sf_arm_asr(smoke[5], 6)));
    smoke[0] = sf_arm_add(smoke[0], (sf_arm_i32)SF_SIMULATION_DELTA(smoke[3]));
    smoke[1] = sf_arm_add(smoke[1], (sf_arm_i32)SF_SIMULATION_DELTA(smoke[4]));
    smoke[2] = sf_arm_add(smoke[2], (sf_arm_i32)SF_SIMULATION_DELTA(smoke[5]));
    smoke[6] = sf_arm_add(smoke[6], (sf_arm_i32)SF_SIMULATION_DELTA(-1));
    return 0;
}

static int sf_arm_scan_poly_map(long x_value, long y_value, void *results,
                                unsigned int maximum_type,
                                unsigned int span)
{
    unsigned int start_x = (unsigned int)x_value & ~3U;
    unsigned int start_y = (unsigned int)y_value & 127U;
    sf_arm_i32 *output = (sf_arm_i32 *)results;
    unsigned int row;
    unsigned int column;

    for (row = 0; row < span; ++row) {
        unsigned int y = (start_y + row) & 127U;
        for (column = 0; column < span; ++column) {
            unsigned int x = (start_x + column) & 127U;
            unsigned int reference = y * 128U + x;
            unsigned int type = poly_map[y][x];

            if (type != 0U && type <= maximum_type) {
                *output++ = (sf_arm_i32)reference;
            }
        }
    }
    *output = -1;
    return 0;
}

int scan_poly_map(long x, long y, void *results)
{
    return sf_arm_scan_poly_map(x, y, results, 8U, 32U);
}

int scan_poly_map_2(long x, long y, void *results)
{
    return sf_arm_scan_poly_map(x, y, results, 247U, 32U);
}

int scan_poly_map_3(long x, long y, void *results)
{
    return sf_arm_scan_poly_map(x, y, results, 247U, 64U);
}
