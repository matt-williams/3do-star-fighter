#include "sf_web_runtime.h"

#include "sf_web_port_renderer.h"
#include "sf_web_world_renderer.h"
#include "SF_ARMCell_portable.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

EM_JS(int32_t, sf_web_runtime_present_js, (const SFWebRenderQuad *commands,
                                           uint32_t command_count,
                                           const SFWebWorldQuad *world_commands,
                                           uint32_t world_command_count,
                                           const SFWebTerrainFrame *terrain_frame,
                                           const uint8_t *terrain_heights,
                                           const uint8_t *terrain_tiles,
                                           uint32_t terrain_x, uint32_t terrain_y,
                                           uint32_t terrain_width,
                                           uint32_t terrain_height,
                                           int32_t terrain_full), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
        runtime.present(commands, command_count, world_commands,
                        world_command_count, terrain_frame,
                        terrain_heights, terrain_tiles, terrain_x, terrain_y,
                        terrain_width, terrain_height, terrain_full);
        return 1;
    }
    return 0;
});

EM_JS(void, sf_web_runtime_set_status_js, (const char *status), {
    const documentStatus = document.getElementById("status");
    if (documentStatus !== null) {
        documentStatus.textContent = UTF8ToString(status);
    }
});

EM_JS(void, sf_web_runtime_set_backdrop_js, (const uint8_t *pixels), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
        runtime.setBackdrop(pixels);
    }
});

EM_JS(void, sf_web_runtime_clear_js, (uint32_t value), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
        runtime.clear(value);
    }
});

EM_JS(void, sf_web_runtime_fill_rect_js,
      (uint32_t colour, int32_t left, int32_t top,
       int32_t right, int32_t bottom), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
        runtime.fillRect(colour, left, top, right, bottom);
    }
});

EM_JS(void, sf_web_runtime_blur_screen_js, (), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
        runtime.blurScreen();
    }
});

EM_JS(void, sf_web_runtime_zoom_screen_js, (), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
        runtime.zoomScreen();
    }
});

EM_JS(void, sf_web_runtime_set_fade_js, (double opacity), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
        runtime.setFade(opacity);
    }
});

EM_JS(void, sf_web_runtime_set_text_font_js,
      (const uint8_t *data, uint32_t size), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
        runtime.setTextFont(data, size);
    }
});

EM_JS(void, sf_web_runtime_set_game_cels_js, (const char *name), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
        runtime.setGameCels(UTF8ToString(name));
    }
});

EM_JS(void, sf_web_runtime_set_world_resources_js, (const char *planet,
      const char *location, const char *variation, const char *sky), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
        runtime.setWorldResources(UTF8ToString(planet), UTF8ToString(location),
                                  UTF8ToString(variation), UTF8ToString(sky));
    }
});

EM_JS(void, sf_web_runtime_set_backdrop_name_js, (const char *name), {
    globalThis.SF3000WebPort?.setBackdropName(UTF8ToString(name));
});

EM_ASYNC_JS(int32_t, sf_web_runtime_load_mission_maps_js,
            (const char *location, const char *variation,
             uint8_t *height_destination, uint8_t *tile_destination), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime === undefined) {
        return 0;
    }
    return await runtime.loadMissionMaps(
        UTF8ToString(location), UTF8ToString(variation),
        height_destination, tile_destination
    );
});

EM_ASYNC_JS(int32_t, sf_web_runtime_load_mission_record_js,
            (char level, int32_t number, uint8_t *destination,
             uint32_t capacity), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : await runtime.loadMissionRecord(
        String.fromCharCode(level), number, destination, capacity
    );
});

EM_ASYNC_JS(int32_t, sf_web_runtime_load_mission_polygon_map_js,
            (const char *location, const char *variation,
             uint8_t *destination), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : await runtime.loadMissionPolygonMap(
        UTF8ToString(location), UTF8ToString(variation), destination
    );
});

EM_ASYNC_JS(int32_t, sf_web_runtime_load_backdrop_js, (const char *name), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 :
        await runtime.loadBackdrop(UTF8ToString(name));
});

EM_ASYNC_JS(int32_t, sf_web_runtime_load_world_materials_js,
            (const char *planet, uint8_t *destination, uint32_t capacity), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : await runtime.loadWorldMaterials(
        UTF8ToString(planet), destination, capacity
    );
});

EM_ASYNC_JS(int32_t, sf_web_runtime_load_world_graphics_js,
            (const char *planet, uint8_t *destination, uint32_t capacity), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : await runtime.loadWorldGraphics(
        UTF8ToString(planet), destination, capacity
    );
});

EM_ASYNC_JS(int32_t, sf_web_runtime_load_game_cels_js,
            (const char *name, uint8_t *destination, uint32_t capacity), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : await runtime.loadGameCels(
        UTF8ToString(name), destination, capacity
    );
});

EM_ASYNC_JS(int32_t, sf_web_runtime_load_sky_js,
            (const char *name, uint8_t *destination, uint32_t capacity), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : await runtime.loadSky(
        UTF8ToString(name), destination, capacity
    );
});

EM_ASYNC_JS(int32_t, sf_web_runtime_load_monochrome_palette_js,
            (uint8_t *destination, uint32_t capacity), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : await runtime.loadMonochromePalette(
        destination, capacity
    );
});

EM_ASYNC_JS(int32_t, sf_web_runtime_load_text_js,
            (const char *language, const char *name, uint8_t *destination,
             uint32_t capacity, int32_t indexed), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : await runtime.loadText(
        UTF8ToString(language), UTF8ToString(name), destination, capacity, indexed
    );
});

EM_ASYNC_JS(int32_t, sf_web_runtime_load_default_configuration_js,
            (uint8_t *destination, uint32_t capacity), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 :
        await runtime.loadDefaultConfiguration(destination, capacity);
});

EM_ASYNC_JS(int32_t, sf_web_runtime_load_saved_configuration_js,
            (uint8_t *destination, uint32_t capacity), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 :
        await runtime.loadSavedConfiguration(destination, capacity);
});

EM_JS(int32_t, sf_web_runtime_save_configuration_js,
      (const uint8_t *source, uint32_t size), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : runtime.saveConfiguration(source, size);
});

EM_ASYNC_JS(int32_t, sf_web_runtime_load_world_metadata_js,
            (const char *world, uint8_t *destination, uint32_t capacity), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : await runtime.loadWorldMetadata(
        UTF8ToString(world), destination, capacity
    );
});

EM_ASYNC_JS(int32_t, sf_web_runtime_load_texture_animations_js,
            (const char *world, uint8_t *destination, uint32_t capacity), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : await runtime.loadTextureAnimations(
        UTF8ToString(world), destination, capacity
    );
});

EM_ASYNC_JS(int32_t, sf_web_runtime_load_message_font_metrics_js,
            (uint8_t *destination, uint32_t capacity), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : await runtime.loadMessageFontMetrics(
        destination, capacity
    );
});

EM_ASYNC_JS(int32_t, sf_web_runtime_load_alphabet_font_metrics_js,
            (uint8_t *destination, uint32_t capacity), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : await runtime.loadAlphabetFontMetrics(
        destination, capacity
    );
});

EM_JS(void, sf_web_runtime_reset_textures_js, (), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
        runtime.resetTextures();
    }
});

EM_JS(uint32_t, sf_web_runtime_control_pad_state_js, (), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : runtime.controlPadState();
});

EM_JS(int32_t, sf_web_runtime_nvram_size_js, (const char *name), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? -1 : runtime.nvramSize(UTF8ToString(name));
});

EM_JS(int32_t, sf_web_runtime_nvram_load_js,
      (const char *name, uint8_t *data, uint32_t capacity), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? -1 :
        runtime.nvramLoad(UTF8ToString(name), data, capacity);
});

EM_JS(int32_t, sf_web_runtime_nvram_store_js,
      (const char *name, const uint8_t *data, uint32_t size), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? -1 :
        runtime.nvramStore(UTF8ToString(name), data, size);
});

EM_JS(int32_t, sf_web_runtime_nvram_delete_js, (const char *name), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? -1 : runtime.nvramDelete(UTF8ToString(name));
});

EM_JS(int32_t, sf_web_runtime_nvram_list_js,
      (uint32_t index, char *name, uint32_t capacity), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? -1 : runtime.nvramList(index, name, capacity);
});

EM_JS(void, sf_web_runtime_sound_loadsamples_js, (), {
    globalThis.SF3000WebPort?.soundLoadSamples();
});

EM_JS(void, sf_web_runtime_sound_unloadsamples_js, (), {
    globalThis.SF3000WebPort?.soundUnloadSamples();
});

EM_JS(void, sf_web_runtime_sound_initialise_js, (), {
    globalThis.SF3000WebPort?.soundInitialise();
});

EM_JS(void, sf_web_runtime_sound_terminate_js, (), {
    globalThis.SF3000WebPort?.soundTerminate();
});

EM_JS(int32_t, sf_web_runtime_sound_play_js,
      (int32_t sample, int32_t pitch, int32_t volume, int32_t stereo_position), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? -1 :
        runtime.soundPlay(sample, pitch, volume, stereo_position);
});

EM_JS(void, sf_web_runtime_sound_stop_js, (int32_t channel), {
    globalThis.SF3000WebPort?.soundStop(channel);
});

EM_JS(int32_t, sf_web_runtime_sound_pitchbend_js,
      (int32_t channel, int32_t pitch_bend), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : runtime.soundPitchBend(channel, pitch_bend);
});

EM_JS(void, sf_web_runtime_sound_alter_js,
      (int32_t channel, int32_t volume, int32_t stereo_position), {
    globalThis.SF3000WebPort?.soundAlter(channel, volume, stereo_position);
});

EM_JS(void, sf_web_runtime_sound_set_master_volume_js, (int32_t volume), {
    globalThis.SF3000WebPort?.soundSetMasterVolume(volume);
});

EM_JS(void, sf_web_runtime_sound_set_enabled_js, (int32_t enabled), {
    globalThis.SF3000WebPort?.soundSetEnabled(enabled);
});

EM_JS(void, sf_web_runtime_music_initialise_js, (), {
    globalThis.SF3000WebPort?.musicInitialise();
});

EM_JS(void, sf_web_runtime_music_terminate_js, (), {
    globalThis.SF3000WebPort?.musicTerminate();
});

EM_JS(void, sf_web_runtime_music_reset_playlist_js, (), {
    globalThis.SF3000WebPort?.musicResetPlaylist();
});

EM_JS(void, sf_web_runtime_music_add_track_js, (int32_t track), {
    globalThis.SF3000WebPort?.musicAddTrack(track);
});

EM_JS(void, sf_web_runtime_music_take_track_js, (int32_t track), {
    globalThis.SF3000WebPort?.musicTakeTrack(track);
});

EM_JS(void, sf_web_runtime_music_play_js,
      (int32_t mode, int32_t track, int32_t tracks_left), {
    globalThis.SF3000WebPort?.musicPlay(mode, track, tracks_left);
});

EM_JS(void, sf_web_runtime_music_stop_js, (), {
    globalThis.SF3000WebPort?.musicStop();
});

EM_JS(void, sf_web_runtime_music_pause_js, (), {
    globalThis.SF3000WebPort?.musicPause();
});

EM_JS(void, sf_web_runtime_music_resume_js, (), {
    globalThis.SF3000WebPort?.musicResume();
});

EM_JS(void, sf_web_runtime_music_set_master_volume_js, (int32_t volume), {
    globalThis.SF3000WebPort?.musicSetMasterVolume(volume);
});

EM_JS(int32_t, sf_web_runtime_music_query_js, (), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? -1 : runtime.musicQuery();
});

EM_JS(void, sf_web_runtime_music_play_voice_js, (const char *path), {
    globalThis.SF3000WebPort?.musicPlayVoice(UTF8ToString(path));
});

EM_ASYNC_JS(int32_t, sf_web_runtime_video_play_js, (const char *path), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : await runtime.videoPlay(UTF8ToString(path));
});

EM_ASYNC_JS(void, sf_web_runtime_wait_vbl_js, (int32_t fields), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
        await runtime.waitForVbl(fields);
    }
});

EM_JS(void, sf_web_runtime_reset_frame_clock_js, (), {
    globalThis.SF3000WebPort?.resetFrameClock();
});

EM_JS(uint32_t, sf_web_runtime_take_elapsed_microseconds_js, (), {
    const runtime = globalThis.SF3000WebPort;
    return runtime === undefined ? 0 : runtime.takeElapsedMicroseconds();
});
#endif

void sf_web_runtime_initialise(void)
{
    sf_web_port_renderer_initialise();
    sf_web_port_renderer_begin_frame();
}

void sf_web_runtime_set_status(const char *status)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_set_status_js(status);
#else
    (void)status;
#endif
}

int32_t sf_web_runtime_load_world_materials(const char *planet)
{
#if defined(__EMSCRIPTEN__)
    uint32_t capacity;
    uint8_t *destination = sf_armcell_world_material_buffer(&capacity);
    int32_t result;

    if (planet == NULL || destination == NULL || capacity == 0u)
        return 0;
    result = sf_web_runtime_load_world_materials_js(planet, destination, capacity);
    return result == (int32_t)capacity &&
           sf_armcell_use_world_material_buffer(capacity) == 0;
#else
    (void)planet;
    return 0;
#endif
}

int32_t sf_web_runtime_load_world_graphics(const char *planet,
                                           uint8_t *destination,
                                           uint32_t capacity)
{
#if defined(__EMSCRIPTEN__)
    int32_t result;

    if (planet == NULL || destination == NULL || capacity == 0u)
        return 0;
    result = sf_web_runtime_load_world_graphics_js(planet, destination, capacity);
    return result > 0 && (uint32_t)result <= capacity;
#else
    (void)planet;
    (void)destination;
    (void)capacity;
    return 0;
#endif
}

int32_t sf_web_runtime_load_game_cels(const char *name, uint8_t *destination,
                                      uint32_t capacity)
{
#if defined(__EMSCRIPTEN__)
    int32_t result;

    if (name == NULL || destination == NULL || capacity == 0u)
        return 0;
    result = sf_web_runtime_load_game_cels_js(name, destination, capacity);
    return result > 0 && (uint32_t)result <= capacity;
#else
    (void)name;
    (void)destination;
    (void)capacity;
    return 0;
#endif
}

int32_t sf_web_runtime_load_sky(const char *name, uint8_t *destination,
                                uint32_t capacity)
{
#if defined(__EMSCRIPTEN__)
    int32_t result;

    if (name == NULL || destination == NULL || capacity == 0u)
        return 0;
    result = sf_web_runtime_load_sky_js(name, destination, capacity);
    return result > 0 && (uint32_t)result <= capacity;
#else
    (void)name;
    (void)destination;
    (void)capacity;
    return 0;
#endif
}

int32_t sf_web_runtime_load_monochrome_palette(uint8_t *destination,
                                               uint32_t capacity)
{
#if defined(__EMSCRIPTEN__)
    int32_t result;

    if (destination == NULL || capacity == 0u)
        return 0;
    result = sf_web_runtime_load_monochrome_palette_js(destination, capacity);
    return result > 0 && (uint32_t)result <= capacity;
#else
    (void)destination;
    (void)capacity;
    return 0;
#endif
}

int32_t sf_web_runtime_load_text(const char *language, const char *name,
                                 uint8_t *destination, uint32_t capacity,
                                 int32_t indexed)
{
#if defined(__EMSCRIPTEN__)
    if (language == NULL || name == NULL || destination == NULL || capacity == 0u)
        return 0;
    return sf_web_runtime_load_text_js(language, name, destination, capacity,
                                       indexed) == 1;
#else
    (void)language;
    (void)name;
    (void)destination;
    (void)capacity;
    (void)indexed;
    return 0;
#endif
}

int32_t sf_web_runtime_load_default_configuration(uint8_t *destination,
                                                  uint32_t capacity)
{
#if defined(__EMSCRIPTEN__)
    if (destination == NULL || capacity == 0u)
        return 0;
    return sf_web_runtime_load_default_configuration_js(destination, capacity) == 1;
#else
    (void)destination;
    (void)capacity;
    return 0;
#endif
}

int32_t sf_web_runtime_load_saved_configuration(uint8_t *destination,
                                                uint32_t capacity)
{
#if defined(__EMSCRIPTEN__)
    if (destination == NULL || capacity == 0u)
        return 0;
    return sf_web_runtime_load_saved_configuration_js(destination, capacity) == 1;
#else
    (void)destination;
    (void)capacity;
    return 0;
#endif
}

int32_t sf_web_runtime_save_configuration(const uint8_t *source,
                                          uint32_t size)
{
#if defined(__EMSCRIPTEN__)
    if (source == NULL || size == 0u)
        return 0;
    return sf_web_runtime_save_configuration_js(source, size) == 1;
#else
    (void)source;
    (void)size;
    return 0;
#endif
}

int32_t sf_web_runtime_load_world_metadata(const char *world,
                                           uint8_t *destination,
                                           uint32_t capacity)
{
#if defined(__EMSCRIPTEN__)
    if (world == NULL || destination == NULL || capacity == 0u)
        return 0;
    return sf_web_runtime_load_world_metadata_js(world, destination, capacity) == 1;
#else
    (void)world;
    (void)destination;
    (void)capacity;
    return 0;
#endif
}

int32_t sf_web_runtime_load_texture_animations(const char *world,
                                                uint8_t *destination,
                                                uint32_t capacity)
{
#if defined(__EMSCRIPTEN__)
    if (world == NULL || destination == NULL || capacity == 0u)
        return 0;
    return sf_web_runtime_load_texture_animations_js(world, destination,
                                                     capacity) == 1;
#else
    (void)world;
    (void)destination;
    (void)capacity;
    return 0;
#endif
}

int32_t sf_web_runtime_load_message_font_metrics(uint8_t *destination,
                                                 uint32_t capacity)
{
#if defined(__EMSCRIPTEN__)
    int32_t result;

    if (destination == NULL || capacity == 0u)
        return 0;
    result = sf_web_runtime_load_message_font_metrics_js(destination, capacity);
    return result > 0 && (uint32_t)result <= capacity ? result : 0;
#else
    (void)destination;
    (void)capacity;
    return 0;
#endif
}

int32_t sf_web_runtime_load_alphabet_font_metrics(uint8_t *destination,
                                                  uint32_t capacity)
{
#if defined(__EMSCRIPTEN__)
    int32_t result;

    if (destination == NULL || capacity == 0u)
        return 0;
    result = sf_web_runtime_load_alphabet_font_metrics_js(destination, capacity);
    return result > 0 && (uint32_t)result <= capacity ? result : 0;
#else
    (void)destination;
    (void)capacity;
    return 0;
#endif
}

void sf_web_runtime_set_backdrop(const void *pixels)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_set_backdrop_js((const uint8_t *)pixels);
#else
    (void)pixels;
#endif
}

void sf_web_runtime_clear(uint32_t value)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_clear_js(value);
#else
    (void)value;
#endif
}

void sf_web_runtime_fill_rect(uint32_t colour, int32_t left,
                              int32_t top, int32_t right, int32_t bottom)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_fill_rect_js(colour, left, top, right, bottom);
#else
    (void)colour;
    (void)left;
    (void)top;
    (void)right;
    (void)bottom;
#endif
}

void sf_web_runtime_blur_screen(void)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_blur_screen_js();
#endif
}

void sf_web_runtime_zoom_screen(void)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_zoom_screen_js();
#endif
}

void sf_web_runtime_present(void)
{
#if defined(__EMSCRIPTEN__)
    SFWebTerrainStateUpload terrain_upload;
    int32_t has_terrain_upload =
        sf_web_world_renderer_terrain_state_upload(&terrain_upload);
    int32_t presented;

    presented = sf_web_runtime_present_js(
        sf_web_port_renderer_command_buffer(),
        sf_web_port_renderer_command_count(),
        sf_web_world_renderer_commands(),
        sf_web_world_renderer_command_count(),
        sf_web_world_renderer_terrain_frame(),
        has_terrain_upload != 0 ? terrain_upload.heights : NULL,
        has_terrain_upload != 0 ? terrain_upload.tiles : NULL,
        has_terrain_upload != 0 ? terrain_upload.x : 0,
        has_terrain_upload != 0 ? terrain_upload.y : 0,
        has_terrain_upload != 0 ? terrain_upload.width : 0,
        has_terrain_upload != 0 ? terrain_upload.height : 0,
        has_terrain_upload != 0 ? terrain_upload.full : 0);
    if (has_terrain_upload != 0 && presented != 0)
        sf_web_world_renderer_acknowledge_terrain_state_upload();
    if (presented != 0 && sf_web_world_renderer_terrain_frame()->active != 0)
        sf_web_world_renderer_acknowledge_terrain_material_upload();
#else
#endif
    sf_web_port_renderer_begin_frame();
}

static void sf_web_runtime_fade(int32_t frames, int32_t to_black)
{
    int32_t step;

    if (frames <= 0) {
        return;
    }
    for (step = 1; step <= frames; ++step) {
        double progress = (double)step / (double)frames;
#if defined(__EMSCRIPTEN__)
        sf_web_runtime_set_fade_js(to_black != 0 ? progress : 1.0 - progress);
#else
        (void)progress;
#endif
        sf_web_runtime_wait_vbl(1);
    }
}

void sf_web_runtime_fade_to_black(int32_t frames)
{
    sf_web_runtime_fade(frames, 1);
}

void sf_web_runtime_fade_from_black(int32_t frames)
{
    sf_web_runtime_fade(frames, 0);
}

void sf_web_runtime_set_text_font(const uint8_t *data, uint32_t size)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_set_text_font_js(data, size);
#else
    (void)data;
    (void)size;
#endif
}

void sf_web_runtime_set_game_cels(const char *name)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_set_game_cels_js(name);
#else
    (void)name;
#endif
}

void sf_web_runtime_set_world_resources(const char *planet, const char *location,
                                        const char *variation, const char *sky)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_set_world_resources_js(planet, location, variation, sky);
#else
    (void)planet;
    (void)location;
    (void)variation;
    (void)sky;
#endif
}

void sf_web_runtime_set_backdrop_name(const char *name)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_set_backdrop_name_js(name);
#else
    (void)name;
#endif
}

int32_t sf_web_runtime_load_mission_maps(const char *location,
                                         const char *variation,
                                         uint8_t *height_destination,
                                         uint8_t *tile_destination)
{
#if defined(__EMSCRIPTEN__)
    return sf_web_runtime_load_mission_maps_js(location, variation,
                                               height_destination,
                                               tile_destination);
#else
    (void)location;
    (void)variation;
    (void)height_destination;
    (void)tile_destination;
    return 0;
#endif
}

int32_t sf_web_runtime_load_mission_record(char level, int32_t number,
                                           uint8_t *destination,
                                           uint32_t capacity)
{
#if defined(__EMSCRIPTEN__)
    return sf_web_runtime_load_mission_record_js(level, number, destination,
                                                  capacity);
#else
    (void)level;
    (void)number;
    (void)destination;
    (void)capacity;
    return 0;
#endif
}

int32_t sf_web_runtime_load_mission_polygon_map(const char *location,
                                                 const char *variation,
                                                 uint8_t *destination)
{
#if defined(__EMSCRIPTEN__)
    return sf_web_runtime_load_mission_polygon_map_js(location, variation,
                                                       destination);
#else
    (void)location;
    (void)variation;
    (void)destination;
    return 0;
#endif
}

int32_t sf_web_runtime_load_backdrop(const char *name)
{
#if defined(__EMSCRIPTEN__)
    return sf_web_runtime_load_backdrop_js(name);
#else
    (void)name;
    return 0;
#endif
}

void sf_web_runtime_reset_textures(void)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_reset_textures_js();
#endif
}

uint32_t sf_web_runtime_control_pad_state(void)
{
#if defined(__EMSCRIPTEN__)
    return sf_web_runtime_control_pad_state_js();
#else
    return 0u;
#endif
}

void sf_web_runtime_reset_frame_clock(void)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_reset_frame_clock_js();
#endif
}

uint32_t sf_web_runtime_take_elapsed_microseconds(void)
{
#if defined(__EMSCRIPTEN__)
    return sf_web_runtime_take_elapsed_microseconds_js();
#else
    return 0u;
#endif
}

int32_t sf_web_runtime_nvram_size(const char *name)
{
#if defined(__EMSCRIPTEN__)
    return sf_web_runtime_nvram_size_js(name);
#else
    (void)name;
    return -1;
#endif
}

int32_t sf_web_runtime_nvram_load(const char *name, uint8_t *data,
                                  uint32_t capacity)
{
#if defined(__EMSCRIPTEN__)
    return sf_web_runtime_nvram_load_js(name, data, capacity);
#else
    (void)name;
    (void)data;
    (void)capacity;
    return -1;
#endif
}

int32_t sf_web_runtime_nvram_store(const char *name, const uint8_t *data,
                                   uint32_t size)
{
#if defined(__EMSCRIPTEN__)
    return sf_web_runtime_nvram_store_js(name, data, size);
#else
    (void)name;
    (void)data;
    (void)size;
    return -1;
#endif
}

int32_t sf_web_runtime_nvram_delete(const char *name)
{
#if defined(__EMSCRIPTEN__)
    return sf_web_runtime_nvram_delete_js(name);
#else
    (void)name;
    return -1;
#endif
}

int32_t sf_web_runtime_nvram_list(uint32_t index, char *name,
                                  uint32_t capacity)
{
#if defined(__EMSCRIPTEN__)
    return sf_web_runtime_nvram_list_js(index, name, capacity);
#else
    (void)index;
    (void)name;
    (void)capacity;
    return -1;
#endif
}

void sf_web_runtime_sound_loadsamples(void)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_sound_loadsamples_js();
#endif
}

void sf_web_runtime_sound_unloadsamples(void)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_sound_unloadsamples_js();
#endif
}

void sf_web_runtime_sound_initialise(void)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_sound_initialise_js();
#endif
}

void sf_web_runtime_sound_terminate(void)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_sound_terminate_js();
#endif
}

int32_t sf_web_runtime_sound_play(int32_t sample, int32_t pitch,
                                  int32_t volume, int32_t stereo_position)
{
#if defined(__EMSCRIPTEN__)
    return sf_web_runtime_sound_play_js(sample, pitch, volume, stereo_position);
#else
    (void)sample;
    (void)pitch;
    (void)volume;
    (void)stereo_position;
    return -1;
#endif
}

void sf_web_runtime_sound_stop(int32_t channel)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_sound_stop_js(channel);
#else
    (void)channel;
#endif
}

int32_t sf_web_runtime_sound_pitchbend(int32_t channel, int32_t pitch_bend)
{
#if defined(__EMSCRIPTEN__)
    return sf_web_runtime_sound_pitchbend_js(channel, pitch_bend);
#else
    (void)channel;
    (void)pitch_bend;
    return 0;
#endif
}

void sf_web_runtime_sound_alter(int32_t channel, int32_t volume,
                                int32_t stereo_position)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_sound_alter_js(channel, volume, stereo_position);
#else
    (void)channel;
    (void)volume;
    (void)stereo_position;
#endif
}

void sf_web_runtime_sound_set_master_volume(int32_t volume)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_sound_set_master_volume_js(volume);
#else
    (void)volume;
#endif
}

void sf_web_runtime_sound_set_enabled(int32_t enabled)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_sound_set_enabled_js(enabled);
#else
    (void)enabled;
#endif
}

void sf_web_runtime_music_initialise(void)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_music_initialise_js();
#endif
}

void sf_web_runtime_music_terminate(void)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_music_terminate_js();
#endif
}

void sf_web_runtime_music_reset_playlist(void)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_music_reset_playlist_js();
#endif
}

void sf_web_runtime_music_add_track(int32_t track)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_music_add_track_js(track);
#else
    (void)track;
#endif
}

void sf_web_runtime_music_take_track(int32_t track)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_music_take_track_js(track);
#else
    (void)track;
#endif
}

void sf_web_runtime_music_play(int32_t mode, int32_t track, int32_t tracks_left)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_music_play_js(mode, track, tracks_left);
#else
    (void)mode;
    (void)track;
    (void)tracks_left;
#endif
}

void sf_web_runtime_music_stop(void)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_music_stop_js();
#endif
}

void sf_web_runtime_music_pause(void)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_music_pause_js();
#endif
}

void sf_web_runtime_music_resume(void)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_music_resume_js();
#endif
}

void sf_web_runtime_music_set_master_volume(int32_t volume)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_music_set_master_volume_js(volume);
#else
    (void)volume;
#endif
}

int32_t sf_web_runtime_music_query(void)
{
#if defined(__EMSCRIPTEN__)
    return sf_web_runtime_music_query_js();
#else
    return -1;
#endif
}

void sf_web_runtime_music_play_voice(const char *path)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_music_play_voice_js(path);
#else
    (void)path;
#endif
}

int32_t sf_web_runtime_video_play(const char *path)
{
#if defined(__EMSCRIPTEN__)
    return sf_web_runtime_video_play_js(path);
#else
    (void)path;
    return 0;
#endif
}

void sf_web_runtime_wait_vbl(int32_t fields)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_wait_vbl_js(fields);
#else
    (void)fields;
#endif
}
