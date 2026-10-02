#include "../sf_3do_compat.h"
#include "../SF_ARMCell_portable.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void test_generated_map_bounds(void)
{
    enum {
        map512_size = 512 * 384,
        map128_size = 128 * 96,
        map32_size = 32 * 24,
        guard_size = 32
    };
    SFArmCellData cel_data = {0};
    unsigned char tiles[16] = {0};
    unsigned char *sprite_map = calloc(256 * 256, 1u);
    unsigned char *map512 = malloc(map512_size + guard_size);
    unsigned char *map128 = malloc(map128_size + guard_size);
    unsigned char *map32 = malloc(map32_size + guard_size);

    assert(sprite_map != NULL);
    assert(map512 != NULL);
    assert(map128 != NULL);
    assert(map32 != NULL);
    memset(map512, 0, map512_size + guard_size);
    memset(map128, 0, map128_size + guard_size);
    memset(map32, 0, map32_size + guard_size);
    memset(map512 + map512_size, 0xa5, guard_size);
    memset(map128 + map128_size, 0xa5, guard_size);
    memset(map32 + map32_size, 0xa5, guard_size);
    cel_data.cels4x4 = tiles;
    cel_data.cel_map512 = map512;
    cel_data.cel_map128 = map128;
    cel_data.cel_map32 = map32;

    arm_generatemaps(&cel_data, sprite_map);

    for (size_t index = 0; index < guard_size; ++index) {
        assert(map512[map512_size + index] == 0xa5);
        assert(map128[map128_size + index] == 0xa5);
        assert(map32[map32_size + index] == 0xa5);
    }
    free(map32);
    free(map128);
    free(map512);
    free(sprite_map);
}

static void test_map_command_source_layout(void)
{
    SFArmCellData cel_data = {0};
    SFWebRenderQuad commands[3] = {0};
    static unsigned char map512[512 * 384];
    static unsigned char map128[128 * 96];
    static unsigned char map32[32 * 24];

    cel_data.cel_map512 = map512;
    cel_data.cel_map128 = map128;
    cel_data.cel_map32 = map32;
    sf_web_renderer_initialise(commands, 3);

    arm_addcelfrom512map(&cel_data, 7, 4, 0);
    arm_addcelfrom128map(&cel_data, 6, 4, 0);
    arm_addcelfrom32map(&cel_data, 0, 0, 0);

    assert(commands[0].source == (uint32_t)(uintptr_t)(map512 + 4 * 768));
    assert(commands[0].encoding ==
           (SF_WEB_RENDER_ENCODING_INDEXED_6_MAP_512 | (84u << 8)));
    assert(commands[1].source == (uint32_t)(uintptr_t)(map128 + 2 * 96));
    assert(commands[1].encoding ==
           (SF_WEB_RENDER_ENCODING_INDEXED_6_MAP_128 | (18u << 8)));
    assert(commands[2].source == (uint32_t)(uintptr_t)map32);
    assert(commands[2].encoding == SF_WEB_RENDER_ENCODING_INDEXED_6_MAP_32);
}

static void store_be32(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)(value >> 24);
    destination[1] = (unsigned char)(value >> 16);
    destination[2] = (unsigned char)(value >> 8);
    destination[3] = (unsigned char)value;
}

static void test_packed_game_cel_destination_width(void)
{
    SFArmCellData cel_data = {0};
    SFWebRenderQuad commands[1] = {0};
    unsigned char game_cels[128] = {0};
    unsigned char *resource = game_cels + 4;
    unsigned char *source = resource + 72;
    uint32_t offset = 4u;

    memcpy(game_cels, &offset, sizeof(offset));
    store_be32(resource + 4, UINT32_C(0x00804200));
    store_be32(source, UINT32_C(0x00000003));
    source[4] = 0u;
    source[5] = 0xc4u;
    source[6] = 0x10u;
    cel_data.cel_game = game_cels;
    cel_data.x_pos0 = 0;
    cel_data.y_pos0 = 0;
    sf_web_renderer_initialise(commands, 1);

    arm_addgamecel(&cel_data, 0, 1024, 1024);

    assert(commands[0].width == 5u);
    assert(commands[0].x[0] == 160);
    assert(commands[0].x[1] == 165);
    assert(commands[0].encoding ==
           (SF_WEB_RENDER_ENCODING_INDEXED_4_PACKED |
            SF_WEB_RENDER_ENCODING_GAME_CEL |
            SF_WEB_RENDER_ENCODING_TRANSPARENT_ZERO));
    assert(commands[0].blend == SF_WEB_RENDER_BLEND_OPAQUE);
}

static void test_game_cel_command_records_sprite(void)
{
    SFArmCellData cel_data = {0};
    SFWebRenderQuad commands[1] = {0};
    unsigned char game_cels[256] = {0};
    unsigned char *resource = game_cels + 112;
    unsigned char *source = resource + 72;
    uint32_t offsets[] = {8u, 112u};

    memcpy(game_cels, offsets, sizeof(offsets));
    store_be32(resource + 4, UINT32_C(0x00804200));
    store_be32(source, UINT32_C(0x00000003));
    source[4] = 0u;
    source[5] = 0x40u;
    source[6] = 0xfcu;
    source[7] = 0x8fu;
    source[8] = 0u;
    cel_data.cel_game = game_cels;
    sf_web_renderer_initialise(commands, 1);

    arm_addgamecel(&cel_data, 1, 1024, 1024);

    assert((commands[0].encoding & SF_WEB_RENDER_GAME_CEL_SPRITE_MASK) ==
           (UINT32_C(1) << SF_WEB_RENDER_GAME_CEL_SPRITE_SHIFT));
}

static void test_packed_game_cel_single_literal_width(void)
{
    SFArmCellData cel_data = {0};
    SFWebRenderQuad commands[1] = {0};
    unsigned char game_cels[128] = {0};
    unsigned char *resource = game_cels + 4;
    unsigned char *source = resource + 72;
    uint32_t offset = 4u;

    memcpy(game_cels, &offset, sizeof(offset));
    store_be32(resource + 4, UINT32_C(0x00804200));
    store_be32(source, UINT32_C(0x00000003));
    source[4] = 0u;
    source[5] = 0x40u;
    source[6] = 0xfcu;
    source[7] = 0x8fu;
    source[8] = 0u;
    cel_data.cel_game = game_cels;
    sf_web_renderer_initialise(commands, 1);

    arm_addgamecel(&cel_data, 0, 1024, 1024);

    assert(commands[0].width == 10u);
}

static void test_additive_game_cel_pixc(void)
{
    SFArmCellData cel_data = {0};
    SFWebRenderQuad commands[1] = {0};
    unsigned char game_cels[128] = {0};
    unsigned char *resource = game_cels + 4;
    unsigned char *source = resource + 72;
    uint32_t offset = 4u;

    memcpy(game_cels, &offset, sizeof(offset));
    store_be32(resource, UINT32_C(0x00800080));
    store_be32(resource + 4, UINT32_C(0x00804200));
    store_be32(source, UINT32_C(0x00000003));
    cel_data.cel_game = game_cels;
    sf_web_renderer_initialise(commands, 1);

    arm_addgamecel(&cel_data, 0, 1024, 1024);

    assert(commands[0].pixc == UINT32_C(0x03800380));
    assert(commands[0].ccb_flags == UINT32_C(0x00804200));
}

static void test_direct_packed_game_cel_destination_width(void)
{
    SFArmCellData cel_data = {0};
    SFWebRenderQuad commands[1] = {0};
    unsigned char game_cels[64] = {0};
    unsigned char *resource = game_cels + 4;
    unsigned char *source = resource + 8;
    uint32_t offset = 4u;

    memcpy(game_cels, &offset, sizeof(offset));
    store_be32(resource + 4, UINT32_C(0x00000200));
    store_be32(source, UINT32_C(0x000007de));
    source[6] = 0x9fu;
    cel_data.cel_game = game_cels;
    sf_web_renderer_initialise(commands, 1);

    arm_addgamecel(&cel_data, 0, 1024, 1024);

    assert(commands[0].width == 32u);
    assert(commands[0].height == 32u);
    assert(commands[0].encoding ==
           (SF_WEB_RENDER_ENCODING_DIRECT_16_PACKED |
            SF_WEB_RENDER_ENCODING_GAME_CEL |
            SF_WEB_RENDER_ENCODING_TRANSPARENT_ZERO));
}

static void test_packed_game_cel_transparency(void)
{
    SFArmCellData cel_data = {0};
    SFWebRenderQuad commands[1] = {0};
    unsigned char game_cels[64] = {0};
    unsigned char *resource = game_cels + 4;
    unsigned char *source = resource + 8;
    uint32_t offset = 4u;

    memcpy(game_cels, &offset, sizeof(offset));
    store_be32(resource + 4, UINT32_C(0x00004200));
    store_be32(source, UINT32_C(0x000007de));
    source[6] = 0x9fu;
    cel_data.cel_game = game_cels;
    sf_web_renderer_initialise(commands, 1);

    arm_addgamecel(&cel_data, 0, 1024, 1024);

    assert(commands[0].encoding ==
           (SF_WEB_RENDER_ENCODING_DIRECT_16_PACKED |
            SF_WEB_RENDER_ENCODING_GAME_CEL |
            SF_WEB_RENDER_ENCODING_TRANSPARENT_ZERO));
}

static void test_background_zero_opaque_direct_game_cel(void)
{
    SFArmCellData cel_data = {0};
    SFWebRenderQuad commands[1] = {0};
    unsigned char game_cels[64] = {0};
    unsigned char *resource = game_cels + 4;
    unsigned char *source = resource + 8;
    uint32_t offset = 4u;

    memcpy(game_cels, &offset, sizeof(offset));
    store_be32(resource + 4, UINT32_C(0x00004000));
    store_be32(source, 0u);
    store_be32(source + 4, 0u);
    cel_data.cel_game = game_cels;
    sf_web_renderer_initialise(commands, 1);

    arm_addgamecel(&cel_data, 0, 1024, 1024);

    assert(commands[0].encoding ==
           (SF_WEB_RENDER_ENCODING_DIRECT_16 |
            SF_WEB_RENDER_ENCODING_GAME_CEL));
}

static void test_opaque_polycel_blend(void)
{
    SFArmCellData cel_data = {0};
    SFWebRenderQuad commands[1] = {0};
    unsigned char cels[96] = {0};
    unsigned char *resource = cels + 4;
    uint32_t offset = 4u;

    memcpy(cels, &offset, sizeof(offset));
    store_be32(resource, UINT32_C(0x00000003));
    store_be32(resource + 4, 0u);
    cel_data.cel_list32 = cels;
    cel_data.shade = 17;
    sf_web_renderer_initialise(commands, 1);

    arm_addpolycel32(&cel_data, 0);

    assert(commands[0].blend == SF_WEB_RENDER_BLEND_OPAQUE);
    assert(commands[0].pixc == UINT32_C(0x08D10BC1));
    assert(commands[0].encoding == SF_WEB_RENDER_ENCODING_STATIC_CEL_32);
}

static void test_static_terrain_cel_marker(void)
{
    SFArmCellData cel_data = {0};
    SFWebRenderQuad material = {0};
    unsigned char cels[64] = {0};
    uint32_t offset = 4u;

    memcpy(cels, &offset, sizeof(offset));
    cel_data.cel_list16 = cels;

    assert(sf_armcell_terrain_material(&cel_data, 0, &material) == 0);
    assert(material.encoding ==
           (SF_WEB_RENDER_ENCODING_STATIC_CEL_16 |
            SF_WEB_RENDER_ENCODING_TERRAIN));
}

static void test_monochrome_cel_target_overlay(void)
{
    SFArmCellData cel_data = {0};
    SFWebRenderQuad commands[1] = {0};
    unsigned char palette[72] = {0};

    cel_data.cel_palette = palette;
    cel_data.shade = 4;
    sf_web_renderer_initialise(commands, 1);

    arm_addmonocel(&cel_data, 0, 8, 2);

    assert(commands[0].source == (uint32_t)(uintptr_t)(palette + 64));
    assert(commands[0].width == 2u);
    assert(commands[0].height == 2u);
    assert(commands[0].blend == SF_WEB_RENDER_BLEND_ADDITIVE);
    assert(commands[0].encoding ==
           (SF_WEB_RENDER_ENCODING_DIRECT_16_RAW |
            SF_WEB_RENDER_ENCODING_TARGET_OVERLAY));
}

static void test_font_metrics_data_layout(void)
{
    enum {
        first_char = 32,
        last_char = 33,
        char_info_offset = 84,
        font_size = char_info_offset + 2 * 4
    };
    uint8 *data = AllocMem(font_size, MEMTYPE_ANY);
    FontDescriptor *font;
    TextCel *text_cel;
    long width;
    long height;

    assert(data != NULL);
    store_be32(data, UINT32_C(0x464f4e54));
    store_be32(data + 4, font_size);
    store_be32(data + 8, 1u);
    store_be32(data + 16, 14u);
    store_be32(data + 20, 11u);
    store_be32(data + 24, 5u);
    store_be32(data + 28, first_char);
    store_be32(data + 32, last_char);
    store_be32(data + 36, 1u);
    store_be32(data + 48, 2u);
    store_be32(data + 52, char_info_offset);
    store_be32(data + 56, 8u);
    store_be32(data + 60, font_size);
    store_be32(data + char_info_offset, 4u);
    store_be32(data + char_info_offset + 4, 7u);

    font = LoadFontData(data, font_size);
    assert(font != NULL);
    text_cel = CreateTextCel(font, 0, 0, 0);
    assert(text_cel != NULL);
    UpdateTextInCel(text_cel, TRUE, " !\n!");
    GetTextCelSize(text_cel, &width, &height);
    assert(width == 12L);
    assert(height == 30L);
    DeleteTextCel(text_cel);
    UnloadFont(font);
}

static void test_alphabet_font_metrics_commands(void)
{
    SFArmCellData cel_data = {0};
    SFWebRenderQuad commands[1] = {0};
    uint8 metrics[SF_ARMCELL_ALPHABET_GLYPH_COUNT *
                  SF_ARMCELL_ALPHABET_METRIC_BYTES] = {0};

    for (size_t index = 0; index < SF_ARMCELL_ALPHABET_GLYPH_COUNT; ++index) {
        metrics[index * SF_ARMCELL_ALPHABET_METRIC_BYTES] = 8u;
        metrics[index * SF_ARMCELL_ALPHABET_METRIC_BYTES + 1u] = 9u;
        metrics[index * SF_ARMCELL_ALPHABET_METRIC_BYTES + 2u] = 16u;
    }
    assert(sf_armcell_set_alphabet_metrics(metrics, sizeof(metrics)) == 0);
    sf_armcell_set_alphabet_active(1);
    sf_web_renderer_initialise(commands, 1);

    arm_addgamecel(&cel_data, 10, 1024, 1024);

    assert(commands[0].source == 0u);
    assert(commands[0].width == 9u);
    assert(commands[0].height == 16u);
    assert(commands[0].encoding ==
           (SF_WEB_RENDER_ENCODING_INDEXED_4 |
            SF_WEB_RENDER_ENCODING_GAME_CEL |
            SF_WEB_RENDER_ENCODING_TRANSPARENT_ZERO |
            (UINT32_C(10) << SF_WEB_RENDER_GAME_CEL_SPRITE_SHIFT)));
    sf_armcell_set_alphabet_active(0);
}

int main(void)
{
    BlockFile block_file;
    CCB ccb = {0};
    ControlPadEventData pad_data;
    FontDescriptor font = {0};
    uint8 font_data[1024] = {0};
    Item io_request;
    Item vbl_request;
    ScreenContext screen_context;
    SignalMask signal;
    TextCel *text_cel;
    SFWebRenderQuad text_commands[1] = {0};
    GrafCon gcon = {0};
    char buffer[6] = {0};
    char translated[128];
    long text_height;
    long text_width;
    uint64 initial_frames;
    void *memory;

    assert(sizeof(int32) == 4u);
    assert(sizeof(uint32) == 4u);
#if defined(__EMSCRIPTEN__)
    assert(sf_3do_set_asset_root("/SF_Resources") == 0);
#else
    assert(sf_3do_set_asset_root("SF3000/WebPort/tests/assets") == 0);
#endif
    assert(sf_3do_translate_path("$boot/SF_Resources/fixture.bin", translated,
                                 sizeof(translated)) == 0);
#if defined(__EMSCRIPTEN__)
    assert(strcmp(translated, "/SF_Resources/fixture.bin") == 0);
#else
    assert(strcmp(translated, "SF3000/WebPort/tests/assets/fixture.bin") == 0);
#endif
    assert(sf_3do_translate_path("$boot/SF_Resources/../outside", translated,
                                 sizeof(translated)) == SF3DO_ERR_INVALID);

    memory = AllocMem(16, MEMTYPE_CEL);
    assert(memory != NULL);
    assert(((unsigned char *)memory)[0] == 0u);
    FreeMem(memory, 16);

    assert(OpenBlockFile("$boot/SF_Resources/fixture.bin", &block_file) == 0);
    assert(block_file.fStatus.fs_ByteCount >= 5);
    io_request = CreateBlockFileIOReq(block_file.fDevice, 0);
    assert(io_request > 0);
    assert(AsynchReadBlockFile(&block_file, io_request, buffer, 5, 0) == 0);
    assert(WaitReadDoneBlockFile(io_request) == 0);
    assert(memcmp(buffer, "asset", 5u) == 0);
    assert(DeleteIOReq(io_request) == 0);
    CloseBlockFile(&block_file);
    assert(OpenDiskFile("/NVRAM/StarFighter.Config") == SF3DO_ERR_UNSUPPORTED);

    sf_3do_set_control_pad_state(ControlA | ControlLeft);
    assert(GetControlPad(1, FALSE, &pad_data) == 0);
    assert(pad_data.cped_ButtonBits == (ControlA | ControlLeft));
    signal = (SignalMask)AllocSignal(0);
    assert(signal != 0u);
    assert(SendSignal(1, signal) == 0);
    assert(WaitSignal(signal) == signal);
    assert(WaitSignal(signal) == 0u);
    assert(FreeSignal((int32)signal) == 0);

    assert(OpenGraphics(&screen_context, 2) == 1);
    assert(screen_context.sc_Bitmaps[0] != NULL);
#if defined(__EMSCRIPTEN__)
    assert(screen_context.sc_Bitmaps[0]->bm_Buffer == NULL);
    assert(screen_context.sc_nFrameBufferPages == 0);
#else
    SetVRAMPages(0, screen_context.sc_Bitmaps[0]->bm_Buffer,
                 UINT32_C(0x11223344), 1, 0);
    assert(((uint32 *)screen_context.sc_Bitmaps[0]->bm_Buffer)[0] ==
           UINT32_C(0x11223344));
#endif
    initial_frames = sf_3do_frame_count();
    vbl_request = GetVBLIOReq();
    assert(vbl_request > 0);
    assert(WaitVBL(vbl_request, 3) == 0);
    assert(sf_3do_frame_count() == initial_frames + 3u);
    assert(DeleteIOReq(vbl_request) == 0);
    DrawCels(screen_context.sc_BitmapItems[0], &ccb);

    font.fd_Data = font_data;
    font.fd_Size = sizeof(font_data);
    font.fd_CharHeight = 14u;
    font.fd_FirstChar = 0u;
    font.fd_LastChar = 255u;
    font.fd_CharExtra = 1u;
    font.fd_CharInfoOffset = 0u;
    store_be32(font_data + ('O' * 4), 7u);
    store_be32(font_data + ('K' * 4), 6u);
    text_cel = CreateTextCel(&font, 0, 0, 0);
    assert(text_cel != NULL);
    UpdateTextInCel(text_cel, TRUE, "OK");
    SetTextCelColor(text_cel, 0, (int32)MakeRGB15(31, 0, 0));
    GetTextCelSize(text_cel, &text_width, &text_height);
    assert(text_width == 14L);
    assert(text_height == 14L);
    SetTextCelCoords(text_cel, -2, 3);
    assert(text_cel->tc_CCB->ccb_XPos == -131072);
    assert(text_cel->tc_CCB->ccb_YPos == 196608);
    sf_web_renderer_initialise(text_commands, 1);
    DrawCels(screen_context.sc_BitmapItems[0], text_cel->tc_CCB);
    assert(sf_web_renderer_command_count() == 1u);
    assert(text_commands[0].source != (uint32)(uintptr_t)text_cel->tc_RenderText);
    assert(strcmp((const char *)(uintptr_t)text_commands[0].source, "OK") == 0);
    UpdateTextInCel(text_cel, TRUE, "NO");
    assert(strcmp((const char *)(uintptr_t)text_commands[0].source, "OK") == 0);
    text_cel->tc_userData = (void *)(uintptr_t)MakeRGB15(31, 31, 0);
    text_cel->tc_CCB->ccb_PRE1 =
        (text_cel->tc_CCB->ccb_PRE1 & ~UINT32_C(0x3ff)) | 3u;
    sf_web_renderer_initialise(text_commands, 1);
    DrawCels(screen_context.sc_BitmapItems[0], text_cel->tc_CCB);
    assert(sf_web_renderer_command_count() == 1u);
    assert(strcmp((const char *)(uintptr_t)text_commands[0].source, "NO") == 0);
    assert(text_commands[0].palette == MakeRGB15(31, 0, 0));
    assert(text_commands[0].x[0] == -2);
    assert(text_commands[0].y[0] == 3);
    assert(text_commands[0].width == 3u);
    assert(text_commands[0].height == 14u);
    assert(text_commands[0].blend == SF_WEB_RENDER_BLEND_MIX);
    assert(text_commands[0].encoding == SF_WEB_RENDER_ENCODING_TEXT);
    text_cel->tc_CCB->ccb_Flags |= CCB_SKIP;
    DrawCels(screen_context.sc_BitmapItems[0], text_cel->tc_CCB);
    assert(sf_web_renderer_command_count() == 1u);
    DeleteTextCel(text_cel);
    sf_web_renderer_initialise(text_commands, 1);
    gcon.gc_FGPen = (int32)MakeRGB15(0, 31, 0);
    gcon.gc_PenX = 4;
    gcon.gc_PenY = 5;
    DrawTextString(&font, &gcon, screen_context.sc_BitmapItems[0], "OK");
    assert(sf_web_renderer_command_count() == 1u);
    assert(text_commands[0].palette == MakeRGB15(0, 31, 0));
    assert(text_commands[0].x[0] == 4);
    assert(text_commands[0].y[0] == 5);
    CloseGraphics(&screen_context);

    assert(InitDataAcq(1) == SF3DO_ERR_UNSUPPORTED);
    assert(NewDataAcq(NULL, "$boot/SF_Resources/Video/Intro", 0) ==
           SF3DO_ERR_UNSUPPORTED);
    test_generated_map_bounds();
    test_map_command_source_layout();
    test_packed_game_cel_destination_width();
    test_game_cel_command_records_sprite();
    test_packed_game_cel_single_literal_width();
    test_additive_game_cel_pixc();
    test_direct_packed_game_cel_destination_width();
    test_packed_game_cel_transparency();
    test_background_zero_opaque_direct_game_cel();
    test_opaque_polycel_blend();
    test_static_terrain_cel_marker();
    test_monochrome_cel_target_overlay();
    test_font_metrics_data_layout();
    test_alphabet_font_metrics_commands();
    return 0;
}
