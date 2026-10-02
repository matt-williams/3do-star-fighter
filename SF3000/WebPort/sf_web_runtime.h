#ifndef SF_WEB_RUNTIME_H
#define SF_WEB_RUNTIME_H

#include <stdint.h>

void sf_web_runtime_initialise(void);
void sf_web_runtime_set_status(const char *status);
void sf_web_runtime_set_backdrop(const void *pixels);
void sf_web_runtime_clear(uint32_t value);
void sf_web_runtime_fill_rect(uint32_t colour, int32_t left, int32_t top,
                              int32_t right, int32_t bottom);
void sf_web_runtime_blur_screen(void);
void sf_web_runtime_zoom_screen(void);
void sf_web_runtime_present(void);
void sf_web_runtime_fade_to_black(int32_t frames);
void sf_web_runtime_fade_from_black(int32_t frames);
void sf_web_runtime_set_text_font(const uint8_t *data, uint32_t size);
void sf_web_runtime_set_game_cels(const char *name);
void sf_web_runtime_set_world_resources(const char *planet, const char *location,
                                        const char *variation, const char *sky);
int32_t sf_web_runtime_load_world_materials(const char *planet);
int32_t sf_web_runtime_load_world_graphics(const char *planet,
                                           uint8_t *destination,
                                           uint32_t capacity);
int32_t sf_web_runtime_load_game_cels(const char *name, uint8_t *destination,
                                      uint32_t capacity);
int32_t sf_web_runtime_load_sky(const char *name, uint8_t *destination,
                                uint32_t capacity);
int32_t sf_web_runtime_load_monochrome_palette(uint8_t *destination,
                                               uint32_t capacity);
int32_t sf_web_runtime_load_text(const char *language, const char *name,
                                 uint8_t *destination, uint32_t capacity,
                                 int32_t indexed);
int32_t sf_web_runtime_load_default_configuration(uint8_t *destination,
                                                  uint32_t capacity);
int32_t sf_web_runtime_load_saved_configuration(uint8_t *destination,
                                                uint32_t capacity);
int32_t sf_web_runtime_save_configuration(const uint8_t *source,
                                          uint32_t size);
int32_t sf_web_runtime_load_world_metadata(const char *world,
                                           uint8_t *destination,
                                           uint32_t capacity);
int32_t sf_web_runtime_load_texture_animations(const char *world,
                                                uint8_t *destination,
                                                uint32_t capacity);
int32_t sf_web_runtime_load_message_font_metrics(uint8_t *destination,
                                                 uint32_t capacity);
int32_t sf_web_runtime_load_alphabet_font_metrics(uint8_t *destination,
                                                  uint32_t capacity);
int32_t sf_web_runtime_load_mission_maps(const char *location,
                                         const char *variation,
                                         uint8_t *height_destination,
                                         uint8_t *tile_destination);
int32_t sf_web_runtime_load_mission_record(char level, int32_t number,
                                           uint8_t *destination,
                                           uint32_t capacity);
int32_t sf_web_runtime_load_mission_polygon_map(const char *location,
                                                 const char *variation,
                                                 uint8_t *destination);
int32_t sf_web_runtime_load_backdrop(const char *name);
void sf_web_runtime_set_backdrop_name(const char *name);
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
