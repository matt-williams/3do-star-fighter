#include "sf_arm_port.h"
#include "sf_web_fixed_step.h"
#include "sf_web_world_renderer.h"

#include <stddef.h>

extern unsigned char sprite_map[256][256];
extern unsigned char height_map[256][256];

#define SF_ARMBURN_MAX_PIECES 512U
#define SF_ARMBURN_ACTIVE_LIMIT 508U

typedef struct sf_armburn_piece {
    unsigned char timer;
    unsigned char next_sprite;
    unsigned char previous_sprite;
    unsigned char sprite_set;
    sf_arm_u32 map_offset;
} sf_armburn_piece;

typedef struct sf_armburn_offset {
    sf_arm_i32 map_offset;
    unsigned char timer;
} sf_armburn_offset;

static const sf_armburn_offset sf_armburn_shapes[][26] = {
    { { 0, 0 }, { 65536, 255 } },
    { { 256, 0 }, { -1, 0 }, { 1, 1 }, { -256, 0 }, { 0, 1 },
      { 65536, 255 } },
    { { 512, 3 }, { -2, 3 }, { 2, 3 }, { -512, 3 }, { 255, 2 },
      { 257, 2 }, { -257, 2 }, { -255, 2 }, { 256, 1 }, { -1, 1 },
      { 1, 1 }, { -256, 1 }, { 0, 0 }, { 65536, 255 } },
    { { 511, 4 }, { 513, 4 }, { 254, 4 }, { 258, 4 }, { -258, 4 },
      { -254, 4 }, { -513, 4 }, { -511, 4 }, { 512, 3 }, { -2, 3 },
      { 2, 3 }, { -512, 3 }, { 255, 2 }, { 257, 2 }, { -257, 2 },
      { -255, 2 }, { 256, 1 }, { -1, 1 }, { 1, 1 }, { -256, 1 },
      { 0, 0 }, { 65536, 255 } }
};

static unsigned char sf_armburn_sprites[20];
static sf_armburn_piece sf_armburn_pieces[SF_ARMBURN_MAX_PIECES];
static unsigned int sf_armburn_current;

/*
 * cel_celdata contains 32-bit address slots at offsets 52, 68, and 76 in
 * the source ABI.  A native 64-bit host cannot decode those slots.  The
 * WebPort startup may bind ordinary host buffers with this adapter; wasm32
 * and arm32 use the original cel_quad layout directly.
 */
static unsigned char *sf_armburn_host_sprites;
static unsigned char *sf_armburn_host_map512;
static unsigned char *sf_armburn_host_cache;

void sf_armburn_bind_buffers(void *sprites4x4, void *map512, void *cache)
{
    sf_armburn_host_sprites = (unsigned char *)sprites4x4;
    sf_armburn_host_map512 = (unsigned char *)map512;
    sf_armburn_host_cache = (unsigned char *)cache;
}

static unsigned char *sf_armburn_quad_pointer(void *cel_quad, size_t offset,
                                              unsigned char *host_value)
{
#if UINTPTR_MAX > UINT32_MAX
    (void)cel_quad;
    (void)offset;
    return host_value;
#else
    return (unsigned char *)sf_arm_address32(
        (sf_arm_u32)sf_arm_load_i32((unsigned char *)cel_quad + offset));
#endif
}

static unsigned char sf_armburn_sprite_set(sf_arm_u32 offset)
{
    unsigned int x = offset & 255U;
    unsigned int y = offset >> 8U;

    if (height_map[y][x] < 17U ||
        height_map[y][(x + 1U) & 255U] < 17U ||
        height_map[(y + 1U) & 255U][x] < 17U ||
        height_map[(y + 1U) & 255U][(x + 1U) & 255U] < 17U) {
        return 8U;
    }
    return 0U;
}

void armburn_resetexplosions(void)
{
    sf_armburn_current = 0;
}

void armburn_initialise(void *planet_info)
{
    unsigned int sprite_index;

    memcpy(sf_armburn_sprites, planet_info, sizeof(sf_armburn_sprites));
    for (sprite_index = 0; sprite_index < 16U;
         ++sprite_index) {
        if (sf_armburn_sprites[sprite_index] != 255U) {
            sf_web_world_renderer_require_terrain_tile_material(
                sf_armburn_sprites[sprite_index]);
        }
    }
}

void armburn_addexplosion(long x_position, long y_position, long size)
{
    sf_arm_u32 map_origin = ((sf_arm_u32)x_position +
                             ((sf_arm_u32)y_position << 8U)) & 65535U;
    const sf_armburn_offset *shape =
        sf_armburn_shapes[(sf_arm_u32)size & 3U];
    unsigned int shape_index;

    for (shape_index = 0; shape[shape_index].map_offset != 65536;
         ++shape_index) {
        sf_arm_u32 offset;
        sf_armburn_piece *piece;
        unsigned char set;
        unsigned int sprite_index;

        if (sf_armburn_current >= SF_ARMBURN_ACTIVE_LIMIT) {
            return;
        }

        offset = (map_origin + (sf_arm_u32)shape[shape_index].map_offset) & 65535U;
        piece = &sf_armburn_pieces[sf_armburn_current++];
        piece->timer = shape[shape_index].timer;
        piece->next_sprite = 0;
        piece->previous_sprite = sprite_map[offset >> 8U][offset & 255U];
        set = sf_armburn_sprites[16] == 0U
                  ? ((sprite_map[map_origin >> 8U][map_origin & 255U] & 1U) != 0U
                         ? 8U
                         : 0U)
                  : sf_armburn_sprite_set(offset);
        piece->sprite_set = set;
        piece->map_offset = offset;

        for (sprite_index = 0; sprite_index < 4U; ++sprite_index) {
            if (piece->previous_sprite == sf_armburn_sprites[set + sprite_index]) {
                --sf_armburn_current;
                break;
            }
        }
    }
}

static void sf_armburn_draw_six_pixels(unsigned char **destination,
                                       unsigned char *mask,
                                       unsigned char coded_six)
{
    unsigned char input_mask;

    for (input_mask = 32U; input_mask != 0U; input_mask >>= 1U) {
        if ((coded_six & input_mask) != 0U) {
            **destination |= *mask;
        } else {
            **destination &= (unsigned char)~*mask;
        }
        *mask >>= 1U;
        if (*mask == 0U) {
            ++*destination;
            *mask = 128U;
        }
    }
}

void armburn_updateexplosions(void *current_sprite_map, void *cel_quad)
{
    unsigned char *sprites4x4;
    unsigned char *map512;
    unsigned char *cache;
    unsigned char *map = (unsigned char *)current_sprite_map;
    unsigned int remaining;
    int found_live = 0;

    if (sf_armburn_current == 0U) {
        return;
    }
#if defined(SF_WEB_PORT)
    if (sf_web_fixed_step_scale_legacy_delta(1) == 0) {
        return;
    }
#endif

#if UINTPTR_MAX > UINT32_MAX
    sprites4x4 = sf_armburn_quad_pointer(cel_quad, 52U, sf_armburn_host_sprites);
    map512 = sf_armburn_quad_pointer(cel_quad, 68U, sf_armburn_host_map512);
    cache = sf_armburn_quad_pointer(cel_quad, 76U, sf_armburn_host_cache);
#else
    sprites4x4 = sf_armburn_quad_pointer(cel_quad, 52U, NULL);
    map512 = sf_armburn_quad_pointer(cel_quad, 68U, NULL);
    cache = sf_armburn_quad_pointer(cel_quad, 76U, NULL);
#endif
    if (sprites4x4 == NULL || map512 == NULL || cache == NULL) {
        /*
         * The state machine and sprite-map work are faithfully represented
         * above, but 64-bit builds need sf_armburn_bind_buffers because the
         * original cel_quad pointer slots cannot hold native addresses.
         */
        return;
    }

    remaining = sf_armburn_current;
    while (remaining != 0U) {
        sf_armburn_piece *piece;
        sf_arm_u32 offset;
        unsigned int x;
        unsigned int y;
        sf_arm_i32 mapped_position;
        unsigned int sprite_number;
        unsigned char sprite;
        unsigned char *sprite_data;
        unsigned char *destination;
        unsigned char mask;
        sf_arm_u32 row_word;

        --remaining;
        piece = &sf_armburn_pieces[remaining];
        if (piece->timer == 255U) {
            if (!found_live) {
                sf_armburn_current = remaining;
            }
            continue;
        }

        found_live = 1;
        if (piece->timer == 0U) {
            piece->timer = 1U;
        } else {
            --piece->timer;
            continue;
        }

        offset = piece->map_offset;
        x = offset & 255U;
        y = offset >> 8U;
        mapped_position = sf_arm_asr(
            sf_arm_mul(sf_arm_add(sf_arm_lsl((sf_arm_i32)y, 10),
                                  sf_arm_lsl((sf_arm_i32)x, 1)), 48), 3);
        sprite_number = piece->next_sprite;
        if (sprite_number >= 4U) {
            sprite_number = (x ^ ((unsigned int)((uintptr_t)piece >> 3U)) ^
                             (offset >> 8U)) & 3U;
            sprite_number += 4U;
            piece->timer = 255U;
        } else {
            ++piece->next_sprite;
        }

        sprite = sf_armburn_sprites[piece->sprite_set + sprite_number];
        if (sprite == 255U) {
            sprite = piece->previous_sprite;
        }
        if (map[offset] != sprite) {
            map[offset] = sprite;
            sf_web_world_renderer_mark_terrain_tile_change(x, y, sprite);
        }

        sprite_data = sprites4x4 + (size_t)sprite * 16U;
        destination = map512 + ((sf_arm_u32)mapped_position >> 3U);
        mask = (unsigned char)(1U << (7U - ((sf_arm_u32)mapped_position & 7U)));
        row_word = (sf_arm_u32)sf_arm_load_i32(sprite_data);
        sf_armburn_draw_six_pixels(&destination, &mask,
                                   (unsigned char)(row_word >> 26U));
        sf_armburn_draw_six_pixels(&destination, &mask,
                                   (unsigned char)((row_word >> 14U) & 63U));

        destination = map512 + ((sf_arm_u32)mapped_position >> 3U) + 384U;
        mask = (unsigned char)(1U << (7U - ((sf_arm_u32)mapped_position & 7U)));
        row_word = (sf_arm_u32)sf_arm_load_i32(sprite_data + 8);
        sf_armburn_draw_six_pixels(&destination, &mask,
                                   (unsigned char)(row_word >> 26U));
        sf_armburn_draw_six_pixels(&destination, &mask,
                                   (unsigned char)((row_word >> 14U) & 63U));

        {
            unsigned int cache_column = ((offset >> 1U) & 126U);
            unsigned int cache_row = offset >> 10U;
            unsigned char *cache_entry = cache + cache_row * 128U + cache_column;

            if (*cache_entry > 0U) {
                *cache_entry = 1U;
            }
        }
    }

}
