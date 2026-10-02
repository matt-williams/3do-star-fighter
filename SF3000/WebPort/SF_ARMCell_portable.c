#include "SF_ARMCell_portable.h"

#include <limits.h>
#include <string.h>

#define SF_ARMCELL_CACHE_BYTES (64u * 64u * 2u)
#define SF_ARMCELL_CACHE_SLOTS 255u
#define SF_ARMCELL_STATE_LIMIT 32u
#define SF_ARMCELL_MAP512_ROW_BYTES ((512u * 6u) / 8u)
#define SF_CCB_PACKED UINT32_C(0x00000200)
#define SF_CCB_LOAD_PLUT UINT32_C(0x00800000)
#define SF_CCB_BACKGROUND_ZERO_OPAQUE UINT32_C(0x00004000)
typedef struct SFArmCellCommandState {
    const void *cel_data;
    uint32_t queue_index;
    uint8_t *source;
    uint32_t preamble0;
    uint32_t preamble1;
    int32_t x_scale;
    int32_t y_scale;
    uint8_t source_has_preamble;
    uint8_t is_game_cel;
} SFArmCellCommandState;

static SFWebRenderQuad *sf_web_renderer_queue;
static uint32_t sf_web_renderer_queue_capacity;
static uint32_t sf_web_renderer_queue_count;
static SFArmCellCommandState sf_armcell_command_states[SF_ARMCELL_STATE_LIMIT];

static const uint32_t sf_pixc_type1[32] = {
    UINT32_C(0x03011001), UINT32_C(0x08010B01), UINT32_C(0x08011801),
    UINT32_C(0x02010101), UINT32_C(0x100100D1), UINT32_C(0x10011301),
    UINT32_C(0x0B0108D1), UINT32_C(0x0B0101E1), UINT32_C(0x180110D1),
    UINT32_C(0x18011B01), UINT32_C(0x010118D1), UINT32_C(0x01010100),
    UINT32_C(0x010100C1), UINT32_C(0x00D103C1), UINT32_C(0x00D108C1),
    UINT32_C(0x130105E1), UINT32_C(0x130110C1), UINT32_C(0x08D10BC1),
    UINT32_C(0x08D118C1), UINT32_C(0x01E101C1), UINT32_C(0x10D113C1),
    UINT32_C(0x1B0109E1), UINT32_C(0x18D11BC1), UINT32_C(0x01000500),
    UINT32_C(0x00C100C0), UINT32_C(0x03C10DE1), UINT32_C(0x08C108C0),
    UINT32_C(0x05E109C1), UINT32_C(0x10C110C0), UINT32_C(0x0BC111E1),
    UINT32_C(0x18C118C0), UINT32_C(0x01C101C0)
};

static const uint32_t sf_pixc_type2[11] = {
    UINT32_C(0x03000300), UINT32_C(0x08000800), UINT32_C(0x07000700),
    UINT32_C(0x10001000), UINT32_C(0x0B000B00), UINT32_C(0x18001800),
    UINT32_C(0x0F000F00), UINT32_C(0x13001300), UINT32_C(0x17001700),
    UINT32_C(0x1B001B00), UINT32_C(0x1F001F00)
};

static const uint32_t sf_pixc_type3[11] = {
    UINT32_C(0x03800380), UINT32_C(0x08800880), UINT32_C(0x07800780),
    UINT32_C(0x10801080), UINT32_C(0x0B800B80), UINT32_C(0x18801880),
    UINT32_C(0x0F800F80), UINT32_C(0x13801380), UINT32_C(0x17801780),
    UINT32_C(0x1B801B80), UINT32_C(0x1F801F80)
};

static const uint32_t sf_pixc_type4[11] = {
    UINT32_C(0x03800391), UINT32_C(0x08800891), UINT32_C(0x07800791),
    UINT32_C(0x10801091), UINT32_C(0x0B800B91), UINT32_C(0x18801891),
    UINT32_C(0x0F800F91), UINT32_C(0x13801391), UINT32_C(0x17801791),
    UINT32_C(0x1B801B91), UINT32_C(0x1F801F91)
};

static uint32_t sf_load_be32(const uint8_t *value)
{
    return ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16) |
           ((uint32_t)value[2] << 8) | value[3];
}

static void sf_store_be32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static int32_t sf_i32(long value)
{
    return (int32_t)(uint32_t)value;
}

static int32_t sf_add32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left + (uint32_t)right);
}

static int32_t sf_lsl32(int32_t value, unsigned int amount)
{
    if (amount >= 32u)
        return 0;
    return (int32_t)((uint32_t)value << amount);
}

static int32_t sf_asr32(int32_t value, unsigned int amount)
{
    uint32_t bits;

    if (amount == 0u)
        return value;
    if (amount >= 32u)
        return value < 0 ? -1 : 0;
    if (value >= 0)
        return value >> amount;
    bits = ~(uint32_t)value;
    return (int32_t)~(bits >> amount);
}

static int32_t sf_clamp_shade(int32_t shade, int32_t maximum)
{
    if (shade < 0)
        return 0;
    return shade > maximum ? maximum : shade;
}

void sf_web_renderer_initialise(SFWebRenderQuad *commands, uint32_t capacity)
{
    sf_web_renderer_queue = commands;
    sf_web_renderer_queue_capacity = commands == NULL ? 0u : capacity;
    sf_web_renderer_queue_count = 0u;
    memset(sf_armcell_command_states, 0, sizeof(sf_armcell_command_states));
}

void sf_web_renderer_reset(void)
{
    sf_web_renderer_queue_count = 0u;
    memset(sf_armcell_command_states, 0, sizeof(sf_armcell_command_states));
}

int32_t sf_web_renderer_append(const SFWebRenderQuad *command)
{
    uint32_t index;

    if (command == NULL || sf_web_renderer_queue == NULL ||
        sf_web_renderer_queue_count >= sf_web_renderer_queue_capacity ||
        sf_web_renderer_queue_count > (uint32_t)INT32_MAX) {
        return -1;
    }
    index = sf_web_renderer_queue_count++;
    sf_web_renderer_queue[index] = *command;
    return (int32_t)index;
}

const SFWebRenderQuad *sf_web_renderer_commands(void)
{
    return sf_web_renderer_queue;
}

uint32_t sf_web_renderer_command_count(void)
{
    return sf_web_renderer_queue_count;
}

uint32_t sf_web_renderer_command_capacity(void)
{
    return sf_web_renderer_queue_capacity;
}

static SFArmCellCommandState *sf_command_state(const SFArmCellData *cel,
                                                int create)
{
    size_t index;

    for (index = 0; index < SF_ARMCELL_STATE_LIMIT; ++index) {
        if (sf_armcell_command_states[index].cel_data == cel)
            return &sf_armcell_command_states[index];
    }
    if (create != 0) {
        for (index = 0; index < SF_ARMCELL_STATE_LIMIT; ++index) {
            if (sf_armcell_command_states[index].cel_data == NULL) {
                sf_armcell_command_states[index].cel_data = cel;
                return &sf_armcell_command_states[index];
            }
        }
    }
    return NULL;
}

static int sf_prepare_emit(SFArmCellData *cel)
{
    if (cel == NULL || cel->temp_cels < 0)
        return 0;
    if ((uint32_t)cel->temp_cels >= SF_ARMCELL_MAX_TEMP_CELS)
        arm_interceptplot(cel);
    if (sf_web_renderer_queue == NULL ||
        sf_web_renderer_queue_count == sf_web_renderer_queue_capacity) {
        return 0;
    }
    return 1;
}

static void sf_emit(SFArmCellData *cel, const SFWebRenderQuad *command,
                    uint8_t *source, uint32_t preamble0, uint32_t preamble1,
                    int32_t x_scale, int32_t y_scale, int source_has_preamble,
                    int is_game_cel)
{
    int32_t index;
    SFArmCellCommandState *state;

    if (!sf_prepare_emit(cel))
        return;
    index = sf_web_renderer_append(command);
    if (index < 0)
        return;
    cel->temp_cels = sf_add32(cel->temp_cels, 1);
    state = sf_command_state(cel, 1);
    if (state == NULL)
        return;
    state->queue_index = (uint32_t)index;
    state->source = source;
    state->preamble0 = preamble0;
    state->preamble1 = preamble1;
    state->x_scale = x_scale;
    state->y_scale = y_scale;
    state->source_has_preamble = (uint8_t)source_has_preamble;
    state->is_game_cel = (uint8_t)is_game_cel;
}

static void sf_set_vertex(SFWebRenderQuad *command, size_t index,
                          int32_t x, int32_t y)
{
    command->x[index] = sf_add32(x, 160);
    command->y[index] = sf_add32(y, 120);
}

static void sf_set_polygon_vertices(SFWebRenderQuad *command,
                                    const SFArmCellData *cel)
{
    sf_set_vertex(command, 0u, cel->x_pos0, cel->y_pos0);
    sf_set_vertex(command, 1u, cel->x_pos1, cel->y_pos1);
    sf_set_vertex(command, 2u, cel->x_pos2, cel->y_pos2);
    sf_set_vertex(command, 3u, cel->x_pos3, cel->y_pos3);
}

static const uint8_t *sf_indexed_resource(const uint8_t *list, int32_t index)
{
    uint32_t offset;

    if (list == NULL || index < 0)
        return NULL;
    memcpy(&offset, list + (size_t)index * sizeof(offset), sizeof(offset));
    return list + offset;
}

static uint32_t sf_source_width(const uint8_t *source)
{
    return (sf_load_be32(source + 4u) & UINT32_C(0x7FF)) + 1u;
}

static uint32_t sf_source_height(const uint8_t *source)
{
    return ((sf_load_be32(source) >> 6) & UINT32_C(0x3FF)) + 1u;
}

static unsigned int sf_packed_bits_per_pixel(const uint8_t *source)
{
    static const unsigned int bits_per_pixel[] = { 0u, 1u, 2u, 4u,
                                                    6u, 8u, 16u, 0u };

    if (source == NULL)
        return 0u;
    return bits_per_pixel[sf_load_be32(source) & UINT32_C(0x7)];
}

static uint32_t sf_read_packed_bits(const uint8_t *data, size_t *bit,
                                    size_t end, unsigned int count)
{
    uint32_t value = 0u;
    unsigned int index;

    for (index = 0u; index < count; ++index) {
        size_t byte = *bit >> 3u;

        if (byte >= end)
            return UINT32_MAX;
        value = (value << 1u) |
                ((data[byte] >> (7u - (*bit & 7u))) & UINT32_C(1));
        ++*bit;
    }
    return value;
}

static uint32_t sf_packed_source_width(const uint8_t *source,
                                       unsigned int bits_per_pixel)
{
    size_t bit;
    size_t end;
    uint32_t width = 0u;
    uint32_t row_offset;

    /*
     * Packed CCBs replace PRE1 with the first scanline packet.  The CEL
     * engine gets its width from that packet, so derive it before calculating
     * the destination rectangle.
     */
    if (source == NULL)
        return 0u;
    if (bits_per_pixel < 8u) {
        row_offset = source[4];
        bit = 8u;
    } else {
        row_offset = ((uint32_t)source[4] << 8u) | source[5];
        bit = 16u;
    }
    end = ((size_t)row_offset + 2u) * 4u;

    for (;;) {
        uint32_t type = sf_read_packed_bits(source + 4u, &bit, end, 2u);
        uint32_t count;

        if (type == UINT32_MAX)
            return 0u;
        if (type == 0u)
            return width;
        count = sf_read_packed_bits(source + 4u, &bit, end, 6u);
        if (count == UINT32_MAX || width > UINT32_MAX - (count + 1u))
            return 0u;
        width += count + 1u;
        if (type == 1u &&
            sf_read_packed_bits(source + 4u, &bit, end,
                                (unsigned int)(count + 1u) * bits_per_pixel) ==
                UINT32_MAX)
            return 0u;
        if (type == 3u &&
            sf_read_packed_bits(source + 4u, &bit, end, bits_per_pixel) ==
                UINT32_MAX)
            return 0u;
    }
}

static uint32_t sf_pointer32(const void *pointer)
{
    return (uint32_t)(uintptr_t)pointer;
}

static void sf_set_game_vertices(SFWebRenderQuad *command,
                                 const SFArmCellData *cel,
                                 int32_t x_scale, int32_t y_scale)
{
    int32_t x0 = sf_add32(cel->x_pos0, 160);
    int32_t y0 = sf_add32(cel->y_pos0, 120);
    int32_t width;
    int32_t height;

    width = (int32_t)((int64_t)command->width * x_scale / 1024);
    height = (int32_t)((int64_t)command->height * y_scale / 1024);
    command->x[0] = x0;
    command->y[0] = y0;
    command->x[1] = sf_add32(x0, width);
    command->y[1] = y0;
    command->x[2] = sf_add32(x0, width);
    command->y[2] = sf_add32(y0, height);
    command->x[3] = x0;
    command->y[3] = sf_add32(y0, height);
}

static SFWebRenderQuad *sf_last_command(SFArmCellData *cel,
                                        SFArmCellCommandState **state_out)
{
    SFArmCellCommandState *state;

    if (cel == NULL || cel->temp_cels <= 0)
        return NULL;
    state = sf_command_state(cel, 0);
    if (state == NULL || state->queue_index >= sf_web_renderer_queue_count)
        return NULL;
    if (state_out != NULL)
        *state_out = state;
    return &sf_web_renderer_queue[state->queue_index];
}

void arm_addpolycel16(void *cel_data, long texture)
{
    SFArmCellData *cel = (SFArmCellData *)cel_data;
    SFWebRenderQuad command = {0};
    const uint8_t *resource;
    const uint8_t *source;
    uint32_t preamble0;
    uint32_t preamble1;

    if (cel == NULL)
        return;
    resource = sf_indexed_resource(cel->cel_list16, sf_i32(texture));
    if (resource == NULL)
        return;

    /* The flags load advances r3 before SF_ARMCell.s adds its 32-byte offset. */
    source = resource + 36u;
    preamble0 = sf_load_be32(source);
    preamble1 = sf_load_be32(source + 4u);
    command.source = sf_pointer32(source);
    command.palette = sf_pointer32(resource + 4u);
    command.shade = sf_clamp_shade(cel->shade, 31);
    command.width = 16u;
    command.height = 16u;
    command.blend = SF_WEB_RENDER_BLEND_OPAQUE;
    command.encoding = SF_WEB_RENDER_ENCODING_INDEXED_4 |
                       SF_WEB_RENDER_ENCODING_TERRAIN;
    command.pixc = sf_pixc_type1[command.shade];
    command.ccb_flags = sf_load_be32(resource);
    sf_set_polygon_vertices(&command, cel);
    sf_emit(cel, &command, (uint8_t *)source, preamble0, preamble1, 0, 0,
            1, 0);
}

void arm_addpolycel32(void *cel_data, long texture)
{
    SFArmCellData *cel = (SFArmCellData *)cel_data;
    SFWebRenderQuad command = {0};
    const uint8_t *resource;
    const uint8_t *source;
    uint32_t descriptor;
    uint32_t bit_size;
    uint32_t extent;
    uint32_t transparency;
    int32_t shade;

    if (cel == NULL)
        return;
    resource = sf_indexed_resource(cel->cel_list32, sf_i32(texture));
    if (resource == NULL)
        return;

    descriptor = sf_load_be32(resource);
    bit_size = descriptor & UINT32_C(0xFF);
    if (bit_size > 13u)
        return;
    extent = UINT32_C(4) << bit_size;
    transparency = descriptor >> 8;
    shade = sf_clamp_shade(cel->shade, transparency == 0u ? 31 : 10);
    source = resource + 40u;
    command.source = sf_pointer32(source);
    command.palette = sf_pointer32(resource + 8u);
    command.shade = shade;
    command.width = extent;
    command.height = extent;
    command.blend = transparency == 0u ? SF_WEB_RENDER_BLEND_OPAQUE :
                    (transparency == 1u ? SF_WEB_RENDER_BLEND_ADDITIVE :
                                         SF_WEB_RENDER_BLEND_MIX);
    command.encoding = (sf_load_be32(resource + 4u) & SF_CCB_PACKED) != 0u ?
                       SF_WEB_RENDER_ENCODING_INDEXED_4_PACKED :
                       SF_WEB_RENDER_ENCODING_INDEXED_4;
    command.pixc = transparency == 0u ? sf_pixc_type1[shade] :
                   (transparency == 1u ? sf_pixc_type3[shade] :
                                       sf_pixc_type4[shade]);
    command.ccb_flags = sf_load_be32(resource + 4u);
    sf_set_polygon_vertices(&command, cel);
    sf_emit(cel, &command, (uint8_t *)source, sf_load_be32(source),
            sf_load_be32(source + 4u), 0, 0, 1, 0);
}

void arm_setpolycel32palette(void *cel_data, long palette)
{
    SFArmCellData *cel = (SFArmCellData *)cel_data;
    SFWebRenderQuad *command;
    const uint8_t *resource;

    if (cel == NULL)
        return;
    command = sf_last_command(cel, NULL);
    if (command == NULL)
        return;
    resource = sf_indexed_resource(cel->cel_list32, sf_i32(palette));
    if (resource == NULL)
        return;
    command->palette = sf_pointer32(resource + 8u);
}

void arm_addgamecel(void *cel_data, long sprite, long x_scale, long y_scale)
{
    SFArmCellData *cel = (SFArmCellData *)cel_data;
    SFWebRenderQuad command = {0};
    const uint8_t *resource;
    const uint8_t *source;
    uint32_t pixc;
    uint32_t flags;
    int32_t shade;
    int32_t x_scale32 = sf_i32(x_scale);
    int32_t y_scale32 = sf_i32(y_scale);

    if (cel == NULL)
        return;
    resource = sf_indexed_resource(cel->cel_game, sf_i32(sprite));
    if (resource == NULL)
        return;

    pixc = sf_load_be32(resource);
    flags = sf_load_be32(resource + 4u);
    source = resource + 8u;
    if ((flags & SF_CCB_LOAD_PLUT) != 0u)
        source += 64u;
    shade = sf_clamp_shade(cel->shade, 10);
    command.source = sf_pointer32(source);
    command.palette = (flags & SF_CCB_LOAD_PLUT) != 0u ?
        sf_pointer32(resource + 8u) : 0u;
    command.shade = shade;
    command.width = sf_source_width(source);
    command.height = sf_source_height(source);
    if ((flags & SF_CCB_PACKED) != 0u)
        command.width = sf_packed_source_width(
            source, sf_packed_bits_per_pixel(source));
    command.encoding = ((flags & SF_CCB_LOAD_PLUT) != 0u ?
        ((flags & SF_CCB_PACKED) != 0u ?
         SF_WEB_RENDER_ENCODING_INDEXED_4_PACKED :
         SF_WEB_RENDER_ENCODING_INDEXED_4) :
        ((flags & SF_CCB_PACKED) != 0u ?
         SF_WEB_RENDER_ENCODING_DIRECT_16_PACKED :
         SF_WEB_RENDER_ENCODING_DIRECT_16)) |
        SF_WEB_RENDER_ENCODING_GAME_CEL;
    /*
     * Palette-loaded game CELs encode transparent backgrounds as index zero.
     * Packed CELs also encode transparent runs directly in their bitstream.
     * Direct, unpacked RGB CELs retain CCB_BGND's zero-colour behavior.
     */
    if ((flags & SF_CCB_LOAD_PLUT) != 0u ||
        (flags & SF_CCB_PACKED) != 0u ||
        (flags & SF_CCB_BACKGROUND_ZERO_OPAQUE) == 0u)
        command.encoding |= SF_WEB_RENDER_ENCODING_TRANSPARENT_ZERO;
    command.blend = SF_WEB_RENDER_BLEND_OPAQUE;
    command.pixc = pixc | sf_pixc_type2[shade];
    command.ccb_flags = flags;
    sf_set_game_vertices(&command, cel, x_scale32, y_scale32);
    sf_emit(cel, &command, (uint8_t *)source, sf_load_be32(source),
            sf_load_be32(source + 4u), x_scale32, y_scale32, 1, 1);
}

void arm_setgamecelpalette(void *cel_data, long palette)
{
    SFArmCellData *cel = (SFArmCellData *)cel_data;
    SFWebRenderQuad *command;
    const uint8_t *resource;

    if (cel == NULL)
        return;
    command = sf_last_command(cel, NULL);
    if (command == NULL)
        return;
    resource = sf_indexed_resource(cel->cel_game, sf_i32(palette));
    if (resource == NULL)
        return;
    command->palette = sf_pointer32(resource);
}

void arm_addmonocel(void *cel_data, long unused, long colour, long plot_type)
{
    SFArmCellData *cel = (SFArmCellData *)cel_data;
    SFWebRenderQuad command = {0};
    int32_t colour32 = sf_i32(colour);
    int32_t type = sf_i32(plot_type);
    int32_t shade;

    (void)unused;
    if (cel == NULL || cel->cel_palette == NULL || colour32 < 0)
        return;

    shade = sf_clamp_shade(cel->shade, type <= 0 ? 31 : 10);
    command.source = sf_pointer32(cel->cel_palette + (size_t)colour32 * 8u);
    command.palette = 0u;
    command.shade = shade;
    command.width = 2u;
    command.height = 2u;
    command.blend = type <= 0 ? SF_WEB_RENDER_BLEND_OPAQUE :
                    (type == 1 ? SF_WEB_RENDER_BLEND_MIX :
                                 SF_WEB_RENDER_BLEND_ADDITIVE);
    command.encoding = SF_WEB_RENDER_ENCODING_DIRECT_16_RAW;
    command.pixc = type <= 0 ? sf_pixc_type1[shade] :
                   (type == 1 ? sf_pixc_type4[shade] : sf_pixc_type3[shade]);
    command.ccb_flags = UINT32_C(0x3F664530);
    sf_set_polygon_vertices(&command, cel);
    sf_emit(cel, &command, NULL, UINT32_C(0x1E), 0u, 0, 0, 0, 0);
}

void arm_celinitialisecreation(void *creation, long count)
{
    uint8_t *block = (uint8_t *)creation;
    int32_t remaining = sf_i32(count) - 1;

    if (block == NULL || remaining <= 0)
        return;

    /*
     * SF_ARMCell.s decrements once before entering its loop, so the final
     * creation block is intentionally left unused (the cache has 255 slots).
     */
    --remaining;
    while (remaining >= 0) {
        sf_store_be32(block, UINT32_C(0x000003C4));
        sf_store_be32(block + 4u, UINT32_C(0x0100100F));
        block += 264u;
        --remaining;
    }
}

void arm_initialisecache(void *cel_data)
{
    SFArmCellData *cel = (SFArmCellData *)cel_data;
    size_t index;

    if (cel == NULL || cel->cache_lookup == NULL || cel->cache_free == NULL)
        return;

    memset(cel->cache_lookup, 0, SF_ARMCELL_CACHE_BYTES);
    for (index = 0; index < SF_ARMCELL_CACHE_SLOTS; ++index)
        cel->cache_free[index] = (uint8_t)index;
    cel->cache_freecount = (int32_t)SF_ARMCELL_CACHE_SLOTS;
}

void arm_updatecache(void *cel_data)
{
    SFArmCellData *cel = (SFArmCellData *)cel_data;
    size_t index;
    int32_t free_count;

    if (cel == NULL || cel->cache_lookup == NULL || cel->cache_free == NULL)
        return;

    free_count = cel->cache_freecount;
    for (index = 0; index < SF_ARMCELL_CACHE_BYTES; index += 2u) {
        uint8_t *entry = cel->cache_lookup + index;

        if (entry[0] == 0u)
            continue;
        --entry[0];
        if (entry[0] == 0u) {
            if (free_count < (int32_t)SF_ARMCELL_CACHE_SLOTS)
                cel->cache_free[free_count++] = entry[1];
            entry[1] = 0u;
        }
    }
    cel->cache_freecount = free_count;
}

static void sf_compose_4x4(uint8_t *destination, const uint8_t *map,
                            int32_t map_x, int32_t map_y,
                            const uint8_t *tiles)
{
    int32_t tile_row;

    for (tile_row = 3; tile_row >= 0; --tile_row) {
        const uint8_t *map_row = map + map_x + (map_y + tile_row) * 256;
        uint8_t *output = destination + (size_t)tile_row * 48u;
        int32_t source_row;

        for (source_row = 0; source_row < 4; ++source_row) {
            const uint8_t *tile0 = tiles + (size_t)map_row[0] * 16u +
                                   (size_t)(3 - source_row) * 4u;
            const uint8_t *tile1 = tiles + (size_t)map_row[1] * 16u +
                                   (size_t)(3 - source_row) * 4u;
            const uint8_t *tile2 = tiles + (size_t)map_row[2] * 16u +
                                   (size_t)(3 - source_row) * 4u;
            const uint8_t *tile3 = tiles + (size_t)map_row[3] * 16u +
                                   (size_t)(3 - source_row) * 4u;
            uint32_t first = sf_load_be32(tile0);
            uint32_t second = sf_load_be32(tile1);
            uint32_t third = sf_load_be32(tile2);
            uint32_t fourth = sf_load_be32(tile3);

            sf_store_be32(output, first | (second >> 24));
            sf_store_be32(output + 4u, (second << 8) | (third >> 16));
            sf_store_be32(output + 8u, (third << 16) | (fourth >> 8));
            output += 12u;
        }
    }
}

void arm_add4cel4(void *cel_data, void *map, long x, long y)
{
    SFArmCellData *cel = (SFArmCellData *)cel_data;
    SFWebRenderQuad command = {0};
    int32_t map_x = sf_i32(x);
    int32_t map_y = sf_i32(y);
    int32_t cache_x;
    int32_t cache_y;
    uint8_t *cache_entry;
    uint8_t source_slot;
    uint8_t *source;

    if (cel == NULL || map == NULL || cel->cache_lookup == NULL ||
        cel->cache_free == NULL || cel->cel_creation == NULL ||
        cel->cels4x4 == NULL)
        return;
    if (!sf_prepare_emit(cel))
        return;

    cache_x = sf_asr32(map_x, 2u);
    cache_y = sf_asr32(map_y, 2u);
    if (cache_x < 0 || cache_x >= 64 || cache_y < 0 || cache_y >= 64)
        return;
    cache_entry = cel->cache_lookup + (size_t)cache_x * 2u +
                  (size_t)cache_y * 128u;
    if (cache_entry[0] != 0u) {
        cache_entry[0] = 2u;
        source_slot = cache_entry[1];
    } else {
        if (cel->cache_freecount <= 0)
            return;
        --cel->cache_freecount;
        source_slot = cel->cache_free[cel->cache_freecount];
        cache_entry[0] = 2u;
        cache_entry[1] = source_slot;
        source = cel->cel_creation + (size_t)source_slot * 264u;
        sf_compose_4x4(source + 8u, (const uint8_t *)map, map_x, map_y,
                       cel->cels4x4);
    }

    source = cel->cel_creation + (size_t)source_slot * 264u;
    command.source = sf_pointer32(source);
    command.palette = sf_pointer32(cel->cel_codedpalette);
    command.shade = sf_clamp_shade(cel->shade, 31);
    command.width = 16u;
    command.height = 16u;
    command.blend = SF_WEB_RENDER_BLEND_OPAQUE;
    command.encoding = SF_WEB_RENDER_ENCODING_INDEXED_6 |
                       SF_WEB_RENDER_ENCODING_TERRAIN;
    command.pixc = sf_pixc_type1[command.shade];
    command.ccb_flags = UINT32_C(0x3FA64430);
    sf_set_polygon_vertices(&command, cel);
    sf_emit(cel, &command, source, sf_load_be32(source),
            sf_load_be32(source + 4u), 0, 0, 1, 0);
}

static void sf_pack_coded6_row(uint8_t *destination, const uint8_t *pixels,
                               size_t pixel_count)
{
    size_t pixel;
    size_t word = 0;
    int bits_remaining = 32;
    uint32_t accumulator = 0;

    for (pixel = 0; pixel < pixel_count; ++pixel) {
        uint32_t value = pixels[pixel] & UINT32_C(0x3F);
        bits_remaining -= 6;
        if (bits_remaining >= 0) {
            accumulator |= value << bits_remaining;
        } else {
            accumulator |= value >> -bits_remaining;
            sf_store_be32(destination + word * 4u, accumulator);
            ++word;
            bits_remaining += 32;
            accumulator = value << bits_remaining;
        }
    }
    sf_store_be32(destination + word * 4u, accumulator);
}

static uint8_t sf_tile_pixel(const uint8_t *tiles, uint8_t tile,
                             unsigned int line, unsigned int sample)
{
    uint32_t row = sf_load_be32(tiles + (size_t)tile * 16u + line * 4u);

    return (uint8_t)((row >> (26u - sample * 12u)) & UINT32_C(0x3F));
}

void arm_generatemaps(void *cel_data, void *sprite_map)
{
    SFArmCellData *cel = (SFArmCellData *)cel_data;
    const uint8_t *map = (const uint8_t *)sprite_map;
    uint8_t row[512];
    size_t y;
    size_t x;

    if (cel == NULL || map == NULL || cel->cels4x4 == NULL ||
        cel->cel_map512 == NULL || cel->cel_map128 == NULL ||
        cel->cel_map32 == NULL)
        return;

    for (y = 0; y < 512u; ++y) {
        for (x = 0; x < 256u; ++x) {
            uint8_t tile = map[(y >> 1u) * 256u + x];
            unsigned int line = (y & 1u) == 0u ? 2u : 0u;

            row[x * 2u] = sf_tile_pixel(cel->cels4x4, tile, line, 0u);
            row[x * 2u + 1u] = sf_tile_pixel(cel->cels4x4, tile, line, 1u);
        }
        sf_pack_coded6_row(cel->cel_map512 + y * SF_ARMCELL_MAP512_ROW_BYTES,
                           row, 512u);
    }

    for (y = 0; y < 128u; ++y) {
        for (x = 0; x < 128u; ++x) {
            uint8_t tile = map[y * 2u * 256u + x * 2u];
            row[x] = sf_tile_pixel(cel->cels4x4, tile, 0u, 0u);
        }
        sf_pack_coded6_row(cel->cel_map128 + y * 96u, row, 128u);
    }

    for (y = 0; y < 32u; ++y) {
        for (x = 0; x < 32u; ++x) {
            uint8_t tile = map[y * 8u * 256u + x * 8u];
            row[x] = sf_tile_pixel(cel->cels4x4, tile, 0u, 0u);
        }
        sf_pack_coded6_row(cel->cel_map32 + y * 24u, row, 32u);
    }
}

static void sf_add_map_cel(SFArmCellData *cel, const uint8_t *source,
                           uint32_t source_width,
                           uint32_t preamble0, uint32_t preamble1,
                           SFWebRenderEncoding encoding,
                           uint32_t source_bit_offset)
{
    SFWebRenderQuad command = {0};

    command.source = sf_pointer32(source);
    command.palette = sf_pointer32(cel->cel_codedpalette);
    command.shade = sf_clamp_shade(cel->shade, 31);
    command.width = source_width;
    command.height = source_width;
    command.blend = SF_WEB_RENDER_BLEND_OPAQUE;
    command.encoding = (uint32_t)encoding | (source_bit_offset << 8u) |
                       SF_WEB_RENDER_ENCODING_TERRAIN;
    command.pixc = sf_pixc_type1[command.shade];
    command.ccb_flags = UINT32_C(0x3FE64430);
    sf_set_polygon_vertices(&command, cel);
    sf_emit(cel, &command, NULL, preamble0, preamble1, 0, 0, 0, 0);
}

void arm_addcelfrom512map(void *cel_data, long x, long y, long size)
{
    SFArmCellData *cel = (SFArmCellData *)cel_data;
    int32_t x32 = sf_i32(x);
    int32_t y32 = sf_i32(y);
    int32_t size32 = sf_i32(size);
    int32_t doubled_x;
    uint32_t extent;
    uint32_t preamble0;
    uint32_t preamble1;
    uint16_t skip_x;
    const uint8_t *source;

    if (cel == NULL || cel->cel_map512 == NULL || x32 < 0 || y32 < 0 ||
        size32 < 0 || size32 > 5)
        return;

    doubled_x = sf_lsl32(x32, 1u);
    skip_x = (uint16_t)(doubled_x & 15);
    source = cel->cel_map512 + (size_t)sf_asr32(doubled_x, 4u) * 12u +
             (size_t)y32 * 768u;
    extent = (UINT32_C(1) << (unsigned int)(size32 + 4)) - 1u;
    preamble0 = UINT32_C(0x40000004) | (extent << 6) |
                ((uint32_t)skip_x << 24);
    preamble1 = UINT32_C(0x5E000000) | (extent + skip_x);
    sf_add_map_cel(cel, source, extent + 1u, preamble0, preamble1,
                   SF_WEB_RENDER_ENCODING_INDEXED_6_MAP_512,
                   (uint32_t)skip_x * 6u);
}

void arm_addcelfrom128map(void *cel_data, long x, long y, long size)
{
    SFArmCellData *cel = (SFArmCellData *)cel_data;
    int32_t x32 = sf_i32(x);
    int32_t y32 = sf_i32(y);
    int32_t size32 = sf_i32(size);
    int32_t reduced_x;
    int32_t reduced_y;
    uint32_t extent;
    uint32_t preamble0;
    uint32_t preamble1;
    uint16_t skip_x;
    const uint8_t *source;

    if (cel == NULL || cel->cel_map128 == NULL || x32 < 0 || y32 < 0 ||
        size32 < 0 || size32 > 5)
        return;

    reduced_x = sf_asr32(x32, 1u);
    reduced_y = sf_asr32(y32, 1u);
    skip_x = (uint16_t)(reduced_x & 15);
    source = cel->cel_map128 + (size_t)sf_asr32(reduced_x, 4u) * 12u +
             (size_t)reduced_y * 96u;
    extent = (UINT32_C(1) << (unsigned int)(size32 + 2)) - 1u;
    preamble0 = UINT32_C(0x40000004) | (extent << 6) |
                ((uint32_t)skip_x << 24);
    preamble1 = UINT32_C(0x16000000) | (extent + skip_x);
    sf_add_map_cel(cel, source, extent + 1u, preamble0, preamble1,
                   SF_WEB_RENDER_ENCODING_INDEXED_6_MAP_128,
                   (uint32_t)skip_x * 6u);
}

void arm_addcelfrom32map(void *cel_data, long unused_x, long unused_y,
                         long unused_size)
{
    SFArmCellData *cel = (SFArmCellData *)cel_data;

    (void)unused_x;
    (void)unused_y;
    (void)unused_size;
    if (cel == NULL || cel->cel_map32 == NULL)
        return;
    sf_add_map_cel(cel, cel->cel_map32, 32u, UINT32_C(0x400007C4),
                   UINT32_C(0x0400001F),
                   SF_WEB_RENDER_ENCODING_INDEXED_6_MAP_32, 0u);
}

void arm_interceptplot(void *cel_data)
{
    SFArmCellData *cel = (SFArmCellData *)cel_data;

    if (cel == NULL || cel->temp_cels <= 0)
        return;
    /*
     * The browser drains the shared queue at the frame boundary.  Keep the
     * pending commands in FIFO order while matching the legacy list reset.
     */
    cel->temp_cels = 0;
}

void arm_setcel_hw(void *cel_data, long width, long height)
{
    SFArmCellData *cel = (SFArmCellData *)cel_data;
    SFWebRenderQuad *command;
    SFArmCellCommandState *state = NULL;
    int32_t width32 = sf_i32(width);
    int32_t height32 = sf_i32(height);

    if (cel == NULL)
        return;
    command = sf_last_command(cel, &state);
    if (command == NULL)
        return;

    if (width32 > 0) {
        state->preamble1 =
            (state->preamble1 & UINT32_C(0xFFFFF800)) |
            ((uint32_t)width32 - 1u);
        command->width = (uint32_t)width32;
    }
    if (height32 > 0) {
        state->preamble0 =
            (state->preamble0 & UINT32_C(0xFFFF003F)) |
            (((uint32_t)height32 - 1u) << 6);
        command->height = (uint32_t)height32;
    }
    if (state->source_has_preamble != 0u && state->source != NULL) {
        sf_store_be32(state->source, state->preamble0);
        sf_store_be32(state->source + 4u, state->preamble1);
    }
    if (state->is_game_cel != 0u)
        sf_set_game_vertices(command, cel, state->x_scale, state->y_scale);
}
