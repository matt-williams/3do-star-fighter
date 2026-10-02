#include "sf_arm_port.h"

extern unsigned char height_map[256][256];
extern void arm_addgamecel(void *cel_quad, long cel, long width, long height);

static sf_arm_i32 sf_arm_random_seed_1 = 1023;
static sf_arm_i32 sf_arm_random_seed_2 = 1023;
static sf_arm_i32 sf_arm_crossshade;

void arm_randominit(void)
{
    sf_arm_random_seed_1 = 1023;
    sf_arm_random_seed_2 = 1023;
}

long arm_random(void)
{
    sf_arm_i32 seed_1 = sf_arm_random_seed_1;
    sf_arm_i32 seed_2 = sf_arm_random_seed_2;

    seed_1 = (sf_arm_i32)((sf_arm_u32)seed_1 ^
                           sf_arm_ror((sf_arm_u32)seed_2,
                                      (sf_arm_u32)seed_2));
    seed_2 = sf_arm_add(seed_2, seed_1);
    sf_arm_random_seed_1 = seed_1;
    sf_arm_random_seed_2 = seed_2;
    return (long)((sf_arm_u32)seed_1 ^ (sf_arm_u32)seed_2);
}

long arm_randomvalue(long maximum)
{
    sf_arm_i32 limit = (sf_arm_i32)maximum;
    sf_arm_u32 random_value = (sf_arm_u32)arm_random();
    sf_arm_i32 value = 0;
    sf_arm_u32 bit;

    for (bit = 1U << 15U; bit != 0U; bit >>= 1U) {
        sf_arm_i32 candidate;

        if ((random_value & bit) == 0U) {
            continue;
        }
        candidate = sf_arm_add(value, (sf_arm_i32)bit);
        if (candidate <= limit) {
            value = candidate;
        }
    }
    return (long)value;
}

void arm_drawmaptargets(void *polygon_map, long map_zoom, void *cel_quad,
                        long map_offset)
{
    const unsigned char *map = (const unsigned char *)polygon_map;
    sf_arm_i32 zoom = (sf_arm_i32)map_zoom;
    sf_arm_i32 x_offset = (sf_arm_i32)((sf_arm_u32)map_offset & 255U);
    sf_arm_i32 y_offset = (sf_arm_i32)((sf_arm_u32)map_offset >> 8U);
    sf_arm_i32 shade_counter = sf_arm_add(sf_arm_crossshade, 1) & 255;
    sf_arm_i32 shade;
    int reference;

    sf_arm_crossshade = shade_counter;
    shade = (shade_counter & 4) == 0 ? 11 : 8;

    /*
     * cel_celdata's first fields are all 32-bit words in the source ABI:
     * x_pos0 at +4, y_pos0 at +8, and shade at +40.  Keeping those raw
     * writes avoids importing unavailable 3DO SDK CCB definitions.
     */
    sf_arm_store_i32((unsigned char *)cel_quad + 40, shade);
    for (reference = 16383; reference >= 0; --reference) {
        unsigned int type = map[reference];
        unsigned int x;
        unsigned int y;
        sf_arm_i32 screen_x;
        sf_arm_i32 screen_y;
        sf_arm_i32 height;

        if (type == 0U || type >= 60U) {
            continue;
        }

        x = (unsigned int)reference & 127U;
        y = (unsigned int)reference >> 7U;
        screen_x = sf_arm_lsl((sf_arm_i32)x, 25);
        screen_y = sf_arm_lsl((sf_arm_i32)y, 25);
        screen_x = sf_arm_sub(screen_x, sf_arm_lsl(x_offset, 24));
        screen_y = sf_arm_sub(screen_y, sf_arm_lsl(y_offset, 24));
        screen_x = sf_arm_asr(screen_x, (unsigned int)sf_arm_add(zoom, 20));
        screen_y = sf_arm_asr(screen_y, (unsigned int)sf_arm_add(zoom, 20));

        if (screen_x <= -128 || screen_y <= -128 ||
            screen_x >= 128 || screen_y >= 128) {
            continue;
        }

        screen_y = sf_arm_neg(screen_y);
        screen_x = sf_arm_sub(screen_x, sf_arm_asr(screen_y, 1));
        screen_x = sf_arm_add(sf_arm_asr(screen_x, 1),
                              sf_arm_asr(sf_arm_asr(screen_x, 1), 1));
        screen_y = sf_arm_add(sf_arm_asr(screen_y, 1),
                              sf_arm_asr(sf_arm_asr(screen_y, 1), 1));

        height = height_map[y << 1U][x << 1U];
        screen_y = sf_arm_sub(screen_y, sf_arm_asr(height, (unsigned int)zoom));
        screen_y = sf_arm_sub(screen_y, 7);
        sf_arm_store_i32((unsigned char *)cel_quad + 4, screen_x);
        sf_arm_store_i32((unsigned char *)cel_quad + 8, screen_y);
        arm_addgamecel(cel_quad, 145, 1024, 1024);
    }
}
