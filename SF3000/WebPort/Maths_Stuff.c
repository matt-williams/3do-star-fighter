#include "sf_arm_port.h"

/*
 * These globals retain their original ARM/3DO storage formats.  In
 * particular tangent_table is a byte array containing 1,025 little-endian
 * words, despite the historic C declaration using char.
 */
extern sf_arm_i32 cosine_table[2048];
extern unsigned char tangent_table[4100];
extern unsigned char poly_map[128][128];
extern sf_arm_i32 camera_x_rotation;
extern sf_arm_i32 camera_y_rotation;
extern sf_arm_i32 camera_z_rotation;

static sf_arm_vec3 sf_camera_rotations[3];

static sf_arm_i32 sf_tangent(unsigned int index)
{
    const unsigned char *entry = tangent_table + index * 4U;

    return (sf_arm_i32)((sf_arm_u32)entry[0] |
                        ((sf_arm_u32)entry[1] << 8U) |
                        ((sf_arm_u32)entry[2] << 16U) |
                        ((sf_arm_u32)entry[3] << 24U));
}

void sf_arm_rotate_coords_x(sf_arm_vec3 *coords, sf_arm_i32 rotation)
{
    sf_arm_i32 cosine = cosine_table[(sf_arm_u32)rotation];
    sf_arm_i32 sine = cosine_table[768U + (sf_arm_u32)rotation];
    sf_arm_i32 x_cos = sf_arm_asr(sf_arm_mul(coords->x, cosine), 12);
    sf_arm_i32 y_sin = sf_arm_asr(sf_arm_mul(coords->y, sine), 12);
    sf_arm_i32 x_sin = sf_arm_asr(sf_arm_mul(coords->x, sine), 12);
    sf_arm_i32 y_cos = sf_arm_asr(sf_arm_mul(coords->y, cosine), 12);

    coords->x = sf_arm_sub(x_cos, y_sin);
    coords->y = sf_arm_add(x_sin, y_cos);
}

void sf_arm_rotate_coords_y(sf_arm_vec3 *coords, sf_arm_i32 rotation)
{
    sf_arm_i32 cosine = cosine_table[(sf_arm_u32)rotation];
    sf_arm_i32 sine = cosine_table[768U + (sf_arm_u32)rotation];
    sf_arm_i32 y_cos = sf_arm_asr(sf_arm_mul(coords->y, cosine), 12);
    sf_arm_i32 z_sin = sf_arm_asr(sf_arm_mul(coords->z, sine), 12);
    sf_arm_i32 y_sin = sf_arm_asr(sf_arm_mul(coords->y, sine), 12);
    sf_arm_i32 z_cos = sf_arm_asr(sf_arm_mul(coords->z, cosine), 12);

    coords->y = sf_arm_sub(y_cos, z_sin);
    coords->z = sf_arm_add(y_sin, z_cos);
}

void sf_arm_rotate_coords_z(sf_arm_vec3 *coords, sf_arm_i32 rotation)
{
    sf_arm_i32 cosine = cosine_table[(sf_arm_u32)rotation];
    sf_arm_i32 sine = cosine_table[768U + (sf_arm_u32)rotation];
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
    sf_arm_i32 absolute_x;
    sf_arm_i32 absolute_y;
    sf_arm_i32 tangent;
    sf_arm_i32 ratio;

    if (x == 0 && y == 0) {
        return 0;
    }

    absolute_x = sf_arm_abs(x);
    absolute_y = sf_arm_abs(y);
    if (absolute_x <= absolute_y) {
        ratio = sf_arm_divide(sf_arm_lsl(absolute_x, 10), absolute_y).quotient;
        tangent = sf_tangent((unsigned int)ratio);
        if (y > 0) {
            tangent = x < 0 ? tangent : sf_arm_neg(tangent);
            return (long)(tangent < 0 ? sf_arm_add(tangent, 1024 * 1024) : tangent);
        }
        return (long)(x >= 0 ? sf_arm_add(tangent, 512 * 1024)
                             : sf_arm_sub(512 * 1024, tangent));
    }

    ratio = sf_arm_divide(sf_arm_lsl(absolute_y, 10), absolute_x).quotient;
    tangent = sf_tangent((unsigned int)ratio);
    if (x < 0) {
        return (long)(y < 0 ? sf_arm_add(tangent, 256 * 1024)
                            : sf_arm_sub(256 * 1024, tangent));
    }
    return (long)(y >= 0 ? sf_arm_add(tangent, 256 * 3 * 1024)
                         : sf_arm_sub(256 * 3 * 1024, tangent));
}

static sf_arm_i32 sf_arm_planar_distance(sf_arm_i32 x, sf_arm_i32 y)
{
    sf_arm_i32 rotation = (sf_arm_i32)find_rotation(x, y);
    sf_arm_i32 cosine = sf_arm_abs(cosine_table[(sf_arm_u32)sf_arm_asr(rotation, 10)]);
    sf_arm_i32 sine = sf_arm_abs(cosine_table[768U + (sf_arm_u32)sf_arm_asr(rotation, 10)]);
    sf_arm_i32 scaled_y = sf_arm_mul(sf_arm_abs(y), cosine);
    sf_arm_i32 scaled_x = sf_arm_mul(sf_arm_abs(x), sine);

    return sf_arm_asr(sf_arm_add(scaled_y, scaled_x), 12);
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
    sf_arm_i32 cosine = sf_arm_abs(cosine_table[(sf_arm_u32)sf_arm_asr(y_rotation, 10)]);
    sf_arm_i32 sine = sf_arm_abs(cosine_table[768U + (sf_arm_u32)sf_arm_asr(y_rotation, 10)]);

    target[6] = x_rotation;
    target[7] = y_rotation;
    target[8] = sf_arm_lsl(sf_arm_add(sf_arm_mul(planar, cosine),
                                      sf_arm_mul(sf_arm_abs(z), sine)), 2);
    return 0;
}

int mc_smoke_mover(void *data)
{
    /*
     * smoke_stack starts with the original 12-byte link_header.  This raw
     * offset is intentional: wasm32/arm32 keeps the legacy source ABI.
     */
    sf_arm_i32 *smoke = (sf_arm_i32 *)((unsigned char *)data + 12);

    smoke[3] = sf_arm_sub(smoke[3], sf_arm_asr(smoke[3], 6));
    smoke[4] = sf_arm_sub(smoke[4], sf_arm_asr(smoke[4], 6));
    smoke[5] = sf_arm_sub(smoke[5], sf_arm_asr(smoke[5], 6));
    smoke[0] = sf_arm_add(smoke[0], smoke[3]);
    smoke[1] = sf_arm_add(smoke[1], smoke[4]);
    smoke[2] = sf_arm_add(smoke[2], smoke[5]);
    smoke[6] = sf_arm_sub(smoke[6], 1);
    return 0;
}

static int sf_arm_scan_poly_map(long x_value, long y_value, void *results,
                                unsigned int maximum_type)
{
    unsigned int start_x = (unsigned int)x_value & ~3U;
    unsigned int start_y = (unsigned int)y_value & 127U;
    sf_arm_i32 *output = (sf_arm_i32 *)results;
    unsigned int row;
    unsigned int column;

    for (row = 0; row < 32U; ++row) {
        unsigned int y = (start_y + row) & 127U;
        for (column = 0; column < 32U; ++column) {
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
    return sf_arm_scan_poly_map(x, y, results, 8U);
}

int scan_poly_map_2(long x, long y, void *results)
{
    return sf_arm_scan_poly_map(x, y, results, 247U);
}
