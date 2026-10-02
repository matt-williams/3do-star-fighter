#include "sf_arm_port.h"

/*
 * The asset packer converts animation metadata and CEL offset tables into
 * wasm-native words before this module receives them. CEL payloads remain
 * big-endian because the WebGL decoder consumes their original bitstreams.
 */
static unsigned char *sf_armtex_animation(unsigned char *offsets,
                                          unsigned int number)
{
    return offsets + sf_arm_load_i32(offsets + number * 4U);
}

static void sf_armtex_apply(unsigned char *animation, sf_arm_i32 frame)
{
    sf_arm_i32 sprite_count = sf_arm_load_i32(animation + 12);
    sf_arm_i32 frames_per_sprite = sf_arm_load_i32(animation + 20);
    unsigned char *sprite = animation + 28;
    sf_arm_i32 index;

    for (index = 0; index < sprite_count; ++index) {
        void *destination =
            sf_arm_address32((sf_arm_u32)sf_arm_load_i32(sprite));

        sprite += 4;
        sf_arm_store_i32(destination, sf_arm_load_i32(sprite + frame * 4));
        sprite += (size_t)frames_per_sprite * 4U;
    }
}

void armtex_initialise(void *animation_data, void *texture_data)
{
    unsigned char *offsets = (unsigned char *)animation_data + 4;
    sf_arm_u32 texture_base = sf_arm_pointer32(texture_data);
    sf_arm_i32 animation_count = sf_arm_load_i32(animation_data);
    sf_arm_i32 animation_number;

    for (animation_number = animation_count - 1; animation_number >= 0;
         --animation_number) {
        unsigned char *animation =
            sf_armtex_animation(offsets, (unsigned int)animation_number);
        sf_arm_i32 sprite_count = sf_arm_load_i32(animation + 12);
        sf_arm_i32 frames_per_sprite = sf_arm_load_i32(animation + 20);
        unsigned char *sprite = animation + 28;
        sf_arm_i32 sprite_number;

        for (sprite_number = 0; sprite_number < sprite_count; ++sprite_number) {
            sf_arm_i32 texture_index = sf_arm_load_i32(sprite);
            sf_arm_u32 destination =
                texture_base + ((sf_arm_u32)texture_index << 2U);
            sf_arm_i32 frame;

            sf_arm_store_i32(sprite, (sf_arm_i32)destination);
            sprite += 4;
            for (frame = 0; frame < frames_per_sprite; ++frame) {
                sf_arm_i32 source_index = sf_arm_load_i32(sprite);
                sf_arm_i32 texture_address = sf_arm_load_i32(
                    (unsigned char *)texture_data +
                    ((sf_arm_u32)source_index << 2U));

                sf_arm_store_i32(sprite, texture_address);
                sprite += 4;
            }
        }
    }
}

void armtex_reset(void *animation_data)
{
    unsigned char *offsets = (unsigned char *)animation_data + 4;
    sf_arm_i32 animation_count = sf_arm_load_i32(animation_data);
    sf_arm_i32 animation_number;

    for (animation_number = animation_count - 1; animation_number >= 0;
         --animation_number) {
        unsigned char *animation =
            sf_armtex_animation(offsets, (unsigned int)animation_number);

        sf_arm_store_i32(animation + 4, sf_arm_load_i32(animation + 8));
        sf_arm_store_i32(animation + 16, 0);
        sf_armtex_apply(animation, 0);
    }
}

void armtex_updateall(void *animation_data)
{
    unsigned char *offsets = (unsigned char *)animation_data + 4;
    sf_arm_i32 animation_count = sf_arm_load_i32(animation_data);
    sf_arm_i32 animation_number;

    for (animation_number = animation_count - 1; animation_number >= 0;
         --animation_number) {
        unsigned char *animation =
            sf_armtex_animation(offsets, (unsigned int)animation_number);
        sf_arm_i32 countdown;
        sf_arm_i32 frame;
        sf_arm_i32 maximum;

        if (sf_arm_load_i32(animation) == 0) {
            continue;
        }

        countdown = sf_arm_sub(sf_arm_load_i32(animation + 4), 1);
        if (countdown >= 0) {
            sf_arm_store_i32(animation + 4, countdown);
            continue;
        }
        sf_arm_store_i32(animation + 4, sf_arm_load_i32(animation + 8));

        frame = sf_arm_add(sf_arm_load_i32(animation + 16), 1);
        maximum = sf_arm_load_i32(animation + 20);
        if (frame >= maximum) {
            frame = sf_arm_load_i32(animation + 24);
        }
        sf_arm_store_i32(animation + 16, frame);
        sf_armtex_apply(animation, frame);
    }
}

void armtex_update(void *animation_data, long animation_number, long direction)
{
    unsigned char *offsets = (unsigned char *)animation_data + 4;
    unsigned char *animation =
        sf_armtex_animation(offsets, (unsigned int)(sf_arm_i32)animation_number);
    sf_arm_i32 countdown = sf_arm_sub(sf_arm_load_i32(animation + 4), 1);
    sf_arm_i32 frame;
    sf_arm_i32 maximum;
    sf_arm_i32 loop;

    if (countdown >= 0) {
        sf_arm_store_i32(animation + 4, countdown);
        return;
    }
    sf_arm_store_i32(animation + 4, sf_arm_load_i32(animation + 8));

    frame = sf_arm_add(sf_arm_load_i32(animation + 16), (sf_arm_i32)direction);
    maximum = sf_arm_load_i32(animation + 20);
    loop = sf_arm_load_i32(animation + 24);
    if (frame < 0) {
        frame = sf_arm_sub(loop, 1);
        if (frame < 0) {
            frame = 0;
        }
    }
    if (frame >= maximum) {
        frame = loop;
    }
    sf_arm_store_i32(animation + 16, frame);
    sf_armtex_apply(animation, frame);
}
