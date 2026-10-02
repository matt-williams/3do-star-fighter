#ifndef SF_WEB_RUNTIME_H
#define SF_WEB_RUNTIME_H

#include <stdint.h>

void sf_web_runtime_initialise(void);
void sf_web_runtime_set_status(const char *status);
void sf_web_runtime_copy_vram(uint32_t bank, const void *pixels);
void sf_web_runtime_clear_bank(uint32_t bank, uint32_t value);
void sf_web_runtime_fill_rect(uint32_t bank, uint32_t colour, int32_t left,
                              int32_t top, int32_t right, int32_t bottom);
void sf_web_runtime_queue_screen_cel(uint32_t target_bank, uint32_t source_bank,
                                     int32_t x, int32_t y, int32_t hdx,
                                     int32_t vdy, uint32_t pixc,
                                     uint32_t ccb_flags);
void sf_web_runtime_present(uint32_t bank);
void sf_web_runtime_fade_to_black(int32_t frames);
void sf_web_runtime_fade_from_black(int32_t frames);
void sf_web_runtime_set_text_font(const uint8_t *data, uint32_t size);
void sf_web_runtime_reset_textures(void);
uint32_t sf_web_runtime_control_pad_state(void);
void sf_web_runtime_wait_vbl(int32_t fields);
void sf_web_runtime_reset_frame_clock(void);
uint32_t sf_web_runtime_take_elapsed_microseconds(void);
int32_t sf_web_runtime_nvram_size(const char *name);
int32_t sf_web_runtime_nvram_load(const char *name, uint8_t *data,
                                  uint32_t capacity);
int32_t sf_web_runtime_nvram_store(const char *name, const uint8_t *data,
                                   uint32_t size);
int32_t sf_web_runtime_nvram_delete(const char *name);
int32_t sf_web_runtime_nvram_list(uint32_t index, char *name,
                                  uint32_t capacity);
void sf_web_runtime_sound_loadsamples(void);
void sf_web_runtime_sound_unloadsamples(void);
void sf_web_runtime_sound_initialise(void);
void sf_web_runtime_sound_terminate(void);
int32_t sf_web_runtime_sound_play(int32_t sample, int32_t pitch,
                                  int32_t volume, int32_t stereo_position);
void sf_web_runtime_sound_stop(int32_t channel);
int32_t sf_web_runtime_sound_pitchbend(int32_t channel, int32_t pitch_bend);
void sf_web_runtime_sound_alter(int32_t channel, int32_t volume,
                                int32_t stereo_position);
void sf_web_runtime_sound_set_master_volume(int32_t volume);
void sf_web_runtime_sound_set_enabled(int32_t enabled);
void sf_web_runtime_music_initialise(void);
void sf_web_runtime_music_terminate(void);
void sf_web_runtime_music_reset_playlist(void);
void sf_web_runtime_music_add_track(int32_t track);
void sf_web_runtime_music_take_track(int32_t track);
void sf_web_runtime_music_play(int32_t mode, int32_t track, int32_t tracks_left);
void sf_web_runtime_music_stop(void);
void sf_web_runtime_music_pause(void);
void sf_web_runtime_music_resume(void);
void sf_web_runtime_music_set_master_volume(int32_t volume);
int32_t sf_web_runtime_music_query(void);
void sf_web_runtime_music_play_voice(const char *path);
int32_t sf_web_runtime_video_play(const char *path);

#endif
