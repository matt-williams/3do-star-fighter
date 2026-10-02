#include "sf_arm_port.h"
#include "../SFlib/SF_ARMLink.h"

#include <stddef.h>

extern unsigned char height_map[256][256];
extern unsigned char poly_map[128][128];
extern sf_arm_i32 graphics_data[19000];
extern void oppo_rotate_node_from_c(void *data);

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(link_header) == 12, "link_header must retain its ARM ABI");
#endif

sf_arm_i32 test_coll_count;
sf_arm_i32 bonus_collision_ref;

static sf_arm_i32 sf_space_mission;
static unsigned char *sf_static_object_start;
static unsigned char *sf_ship_start;

typedef struct sf_arm_collision_ship_layout {
    link_header header;
    long x_pos;
    long y_pos;
    long z_pos;
    long x_rot;
    long y_rot;
    long z_rot;
    long type;
    long collision_size;
    long who_owns_me;
    long what_hit_me;
} sf_arm_collision_ship_layout;

typedef struct ship_stack ship_stack;

static sf_arm_i32 sf_collision_height(sf_arm_i32 x_position,
                                      sf_arm_i32 y_position)
{
    unsigned int x = ((sf_arm_u32)x_position) >> 24U;
    unsigned int y = ((sf_arm_u32)y_position) >> 24U;
    sf_arm_i32 top_left = (sf_arm_i32)height_map[y][x] - 17;
    sf_arm_i32 top_right;
    sf_arm_i32 bottom_right;
    sf_arm_i32 bottom_left;
    sf_arm_i32 x_fraction;
    sf_arm_i32 y_fraction;
    sf_arm_i32 left_delta;
    sf_arm_i32 right_delta;
    sf_arm_i32 result;

    if (top_left < 0) {
        top_left = 0;
    }
    top_right = (sf_arm_i32)height_map[y][(x + 1U) & 255U] - 17;
    if (top_right < 0) {
        top_right = 0;
    }
    bottom_right =
        (sf_arm_i32)height_map[(y + 1U) & 255U][(x + 1U) & 255U] - 17;
    if (bottom_right < 0) {
        bottom_right = 0;
    }
    bottom_left = (sf_arm_i32)height_map[(y + 1U) & 255U][x] - 17;
    if (bottom_left < 0) {
        bottom_left = 0;
    }

    x_fraction = (sf_arm_i32)(((sf_arm_u32)x_position << 8U) >> 22U);
    y_fraction = (sf_arm_i32)(((sf_arm_u32)y_position << 8U) >> 22U);
    left_delta = sf_arm_sub(bottom_left, top_left);
    right_delta = sf_arm_sub(bottom_right, top_right);

    result = sf_arm_add(
        sf_arm_mul(sf_arm_mul(left_delta, y_fraction),
                   sf_arm_sub(1024, x_fraction)),
        sf_arm_mul(sf_arm_mul(right_delta, y_fraction), x_fraction));
    result = sf_arm_add(
        result,
        sf_arm_add(sf_arm_mul(sf_arm_mul(sf_arm_sub(top_right, top_left),
                                        x_fraction),
                              sf_arm_sub(1024, y_fraction)),
                   sf_arm_mul(sf_arm_mul(sf_arm_sub(bottom_right, bottom_left),
                                        x_fraction),
                              y_fraction)));
    result = sf_arm_add(sf_arm_lsl(result, 1), sf_arm_lsl(top_left, 21));
    return result < 0 ? 0 : result;
}

long find_ground_height(long x_position, long y_position)
{
    return (long)sf_collision_height((sf_arm_i32)x_position,
                                     (sf_arm_i32)y_position);
}

static sf_arm_i32 sf_collision_boxes(sf_arm_i32 x, sf_arm_i32 y, sf_arm_i32 z,
                                     const unsigned char *collision_data)
{
    sf_arm_i32 boxes = sf_arm_load_i32(collision_data);
    const unsigned char *box = collision_data + 12;

    for (;;) {
        sf_arm_i32 x_minimum = sf_arm_load_i32(box + 4);
        sf_arm_i32 y_minimum = sf_arm_load_i32(box + 8);
        sf_arm_i32 z_minimum = sf_arm_load_i32(box + 12);
        sf_arm_i32 x_maximum = sf_arm_load_i32(box + 16);
        sf_arm_i32 y_maximum = sf_arm_load_i32(box + 20);
        sf_arm_i32 z_maximum = sf_arm_load_i32(box + 24);

        if (x >= x_minimum && y >= y_minimum && z >= z_minimum &&
            x < x_maximum && y < y_maximum && z < z_maximum) {
            bonus_collision_ref = *(const unsigned char *)box;
            return sf_arm_add(bonus_collision_ref, 1);
        }
        if (boxes-- == 0) {
            break;
        }
        box += 28;
    }

    bonus_collision_ref = 0;
    return 0;
}

static sf_arm_i32 sf_collision_with_hills(sf_arm_i32 x, sf_arm_i32 y,
                                          sf_arm_i32 z)
{
    sf_arm_i32 height = sf_collision_height(x, y);

    if (sf_space_mission == 1) {
        return 0;
    }
    return height > z ? ~height : 0;
}

static void sf_follow_collision_pointer(unsigned int *x, unsigned int *y,
                                        unsigned int *type)
{
    for (;;) {
        switch (*type) {
        case 248U:
            ++*x;
            ++*y;
            break;
        case 249U:
            ++*y;
            break;
        case 250U:
            --*x;
            ++*y;
            break;
        case 251U:
            --*x;
            break;
        case 252U:
            --*x;
            --*y;
            break;
        case 253U:
            --*y;
            break;
        case 254U:
            ++*x;
            --*y;
            break;
        default:
            ++*x;
            break;
        }
        *x &= 127U;
        *y &= 127U;
        *type = poly_map[*y][*x];
        if (*type < 248U) {
            return;
        }
    }
}

void setup_collision_constants(long space_mission)
{
    sf_space_mission = (sf_arm_i32)space_mission;
    sf_static_object_start =
        (unsigned char *)graphics_data + sf_arm_load_i32(graphics_data);
    sf_ship_start = (unsigned char *)graphics_data + sf_arm_load_i32(graphics_data + 1);
}

long check_collision(long x_position, long y_position, long z_position)
{
    sf_arm_i32 x = (sf_arm_i32)x_position;
    sf_arm_i32 y = (sf_arm_i32)y_position;
    sf_arm_i32 z = (sf_arm_i32)z_position;
    unsigned int map_x;
    unsigned int map_y;
    unsigned int type;

    if (z > (1 << 29)) {
        return 0;
    }

    map_x = (((sf_arm_u32)x + (1U << 24U)) >> 25U) & 127U;
    map_y = (((sf_arm_u32)y + (1U << 24U)) >> 25U) & 127U;
    type = poly_map[map_y][map_x];
    if (type == 0U) {
        return (long)sf_collision_with_hills(x, y, z);
    }

    if (type >= 248U) {
        sf_follow_collision_pointer(&map_x, &map_y, &type);
    }
    if (type == 0U || type >= 64U) {
        return (long)sf_collision_with_hills(x, y, z);
    }

    {
        sf_arm_i32 static_height =
            (sf_arm_i32)height_map[(map_y << 1U) & 255U][(map_x << 1U) & 255U] - 17;
        const unsigned char *details;
        const unsigned char *collision_data;
        sf_arm_i32 hit;

        if (static_height < 0) {
            static_height = 0;
        }
        details = sf_static_object_start + type * 64U;
        /*
         * collision_adr is a relocated 32-bit pointer in graphics_data.
         * This exact binary resource convention requires a 32-bit target
         * (the intended wasm32 build); a 64-bit resource loader must provide
         * a matching low-address relocation layer.
         */
        collision_data = (const unsigned char *)sf_arm_address32(
            (sf_arm_u32)sf_arm_load_i32(details + 16));
        hit = sf_collision_boxes(
            sf_arm_sub(x, sf_arm_lsl((sf_arm_i32)map_x, 25)),
            sf_arm_sub(y, sf_arm_lsl((sf_arm_i32)map_y, 25)),
            sf_arm_sub(z, sf_arm_lsl(static_height, 21)),
            collision_data);
        if (hit != 0) {
            return (long)(map_y * 128U + map_x);
        }
    }

    return (long)sf_collision_with_hills(x, y, z);
}

long big_ship_collision_check(ship_stack *big_ship_address,
                              ship_stack *small_ship_address)
{
    sf_arm_collision_ship_layout *big_ship =
        (sf_arm_collision_ship_layout *)big_ship_address;
    sf_arm_collision_ship_layout *small_ship =
        (sf_arm_collision_ship_layout *)small_ship_address;
    sf_arm_i32 rotated_node[6];
    const unsigned char *details;
    const unsigned char *collision_data;

    rotated_node[0] = sf_arm_sub((sf_arm_i32)big_ship->x_pos,
                                 (sf_arm_i32)small_ship->x_pos);
    rotated_node[1] = sf_arm_sub((sf_arm_i32)big_ship->y_pos,
                                 (sf_arm_i32)small_ship->y_pos);
    rotated_node[2] = sf_arm_sub((sf_arm_i32)small_ship->z_pos,
                                 (sf_arm_i32)big_ship->z_pos);
    rotated_node[3] = (sf_arm_i32)big_ship->x_rot;
    rotated_node[4] = sf_arm_sub(1024 * 1024, (sf_arm_i32)big_ship->y_rot);
    rotated_node[5] = (sf_arm_i32)big_ship->z_rot;
    oppo_rotate_node_from_c(rotated_node);

    details = sf_ship_start + (size_t)(sf_arm_u32)(sf_arm_i32)big_ship->type * 64U;
    collision_data = (const unsigned char *)sf_arm_address32(
        (sf_arm_u32)sf_arm_load_i32(details + 16));
    return (long)sf_collision_boxes(rotated_node[0], rotated_node[1],
                                    rotated_node[2], collision_data);
}
