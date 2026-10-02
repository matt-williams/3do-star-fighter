#include "sf_web_runtime.h"

#include "sf_web_port_renderer.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

EM_JS(void, sf_web_runtime_present_js, (const SFWebRenderQuad *commands,
                                        uint32_t command_count, uint32_t bank), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
        runtime.present(commands, command_count, bank);
    }
});

EM_JS(void, sf_web_runtime_set_status_js, (const char *status), {
    const documentStatus = document.getElementById("status");
    if (documentStatus !== null) {
        documentStatus.textContent = UTF8ToString(status);
    }
});

EM_JS(void, sf_web_runtime_copy_vram_js, (uint32_t bank, const uint8_t *pixels), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
        runtime.copyVram(bank, pixels);
    }
});

EM_JS(void, sf_web_runtime_clear_bank_js, (uint32_t bank, uint32_t value), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
        runtime.clearBank(bank, value);
    }
});

EM_JS(void, sf_web_runtime_fill_rect_js,
      (uint32_t bank, uint32_t colour, int32_t left, int32_t top,
       int32_t right, int32_t bottom), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
        runtime.fillRect(bank, colour, left, top, right, bottom);
    }
});

EM_JS(void, sf_web_runtime_queue_screen_cel_js,
      (uint32_t target_bank, uint32_t source_bank, int32_t x, int32_t y,
       int32_t hdx, int32_t vdy, uint32_t pixc, uint32_t ccb_flags), {
    const runtime = globalThis.SF3000WebPort;
    if (runtime !== undefined) {
       runtime.queueScreenCel(target_bank, source_bank, x, y, hdx, vdy, pixc,
                              ccb_flags);
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

void sf_web_runtime_copy_vram(uint32_t bank, const void *pixels)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_copy_vram_js(bank, (const uint8_t *)pixels);
#else
    (void)bank;
    (void)pixels;
#endif
}

void sf_web_runtime_clear_bank(uint32_t bank, uint32_t value)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_clear_bank_js(bank, value);
#else
    (void)bank;
    (void)value;
#endif
}

void sf_web_runtime_fill_rect(uint32_t bank, uint32_t colour, int32_t left,
                              int32_t top, int32_t right, int32_t bottom)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_fill_rect_js(bank, colour, left, top, right, bottom);
#else
    (void)bank;
    (void)colour;
    (void)left;
    (void)top;
    (void)right;
    (void)bottom;
#endif
}

void sf_web_runtime_queue_screen_cel(uint32_t target_bank, uint32_t source_bank,
                                     int32_t x, int32_t y, int32_t hdx,
                                     int32_t vdy, uint32_t pixc,
                                     uint32_t ccb_flags)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_queue_screen_cel_js(target_bank, source_bank, x, y, hdx,
                                        vdy, pixc, ccb_flags);
#else
    (void)target_bank;
    (void)source_bank;
    (void)x;
    (void)y;
    (void)hdx;
    (void)vdy;
    (void)pixc;
    (void)ccb_flags;
#endif
}

void sf_web_runtime_present(uint32_t bank)
{
#if defined(__EMSCRIPTEN__)
    sf_web_runtime_present_js(sf_web_port_renderer_command_buffer(),
                              sf_web_port_renderer_command_count(), bank);
#else
    (void)bank;
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
    emscripten_sleep(fields > 0 ? fields * 1000 / 60 : 0);
#else
    (void)fields;
#endif
}
