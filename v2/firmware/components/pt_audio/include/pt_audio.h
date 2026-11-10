#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pt_core.h"

typedef enum { PT_CUE_WAKE, PT_CUE_END, PT_CUE_TEST } pt_cue_t;

pt_err_t pt_audio_start(void);
size_t pt_audio_read_mics(int16_t *left, int16_t *right, size_t frames, uint32_t timeout_ms);
size_t pt_audio_play(const int16_t *pcm24k, size_t n);
size_t pt_audio_play_space(void);
void pt_audio_play_end(void);
void pt_audio_stop_playback(void);
bool pt_audio_playing(void);
void pt_audio_set_volume(float volume);
void pt_audio_cue(pt_cue_t c);
uint32_t pt_audio_mic_overflows(void);
