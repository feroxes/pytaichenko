#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "pt_core.h"

#define PT_EQ_BANDS 5

typedef struct {
    char api_key[256];
    char model[64];
    char voice[32];
    char reasoning_effort[16];
    float vad_threshold;
    int silence_ms;
    bool log_heard;
} pt_openai_t;

typedef struct {
    char language[4];
    char instructions[8192];
} pt_assistant_t;

typedef struct {
    float first_turn_s, follow_up_s, max_speech_s, response_timeout_s, answer_max_s,
        session_idle_s, mic_stop_grace_s, mic_holdoff_s, ready_check_s;
    int max_follow_ups;
} pt_timing_t;

typedef struct {
    float volume;
    char wake_word[16];
    float mic_gain_db;
    float low_cut_hz;
    float loudness_gain_db;
    float eq_db[PT_EQ_BANDS];
} pt_sound_t;

typedef struct {
    pt_openai_t openai;
    pt_assistant_t assistant;
    pt_timing_t timing;
    pt_sound_t sound;
    char password_hash[160];
} pt_settings_t;

extern const float PT_EQ_HZ[PT_EQ_BANDS];

void pt_settings_defaults(pt_settings_t *s, const char *language);
const char *pt_default_instructions(const char *language);
pt_err_t pt_settings_from_json(pt_settings_t *s, const char *json, size_t len);
pt_err_t pt_settings_load_stored(pt_settings_t *s, const char *json, size_t len, int *dropped);
char *pt_settings_to_json(const pt_settings_t *s);
char *pt_settings_public_json(const pt_settings_t *s);
pt_err_t pt_settings_patch(pt_settings_t *s, const char *json, size_t len, char *field, size_t field_len);
bool pt_settings_session_changed(const pt_settings_t *a, const pt_settings_t *b);
