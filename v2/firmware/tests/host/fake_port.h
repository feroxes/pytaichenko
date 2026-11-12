#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pt_turn.h"

#define FP_MAX_CONN 16

typedef enum { FP_RT_OPEN, FP_RT_SEND, FP_RT_CLOSE, FP_PLAY, FP_PLAY_END, FP_DEVICE, FP_SET_VOLUME } fp_kind_t;

typedef struct {
    fp_kind_t kind;
    uint32_t at;
    int conn;
    char *json;
    char type[64];
    int16_t *pcm;
    size_t n;
    pt_dev_event_t ev;
    bool follow_up;
    bool has_code;
    char code[32];
    float volume;
    char voice[32];
    char model[64];
    float first_turn_s;
} fp_call_t;

typedef struct {
    bool manual;
    bool fail_connect;
    bool silent;
    bool reject_update;
    bool closed;
    bool live_reject;
} fp_conn_t;

typedef struct {
    void (*opened)(void *t, uint32_t now);
    void (*failed)(void *t, const char *why, uint32_t now);
    void (*message)(void *t, const char *json, size_t len, uint32_t now);
    void (*closed)(void *t, uint32_t now);
    void *t;
} fp_target_t;

typedef struct {
    pt_port_t port;
    fp_call_t *calls;
    size_t n;
    size_t cap;
    const uint32_t *clock;
    fp_conn_t conn[FP_MAX_CONN + 1];
    int opened;
    size_t pumped;
    fp_target_t target;
} fake_port_t;

void fp_init(fake_port_t *f, const uint32_t *clock, const fp_target_t *target);
void fp_free(fake_port_t *f);
void fp_pump(fake_port_t *f);
bool fp_live(const fake_port_t *f, int conn);
void fp_push(fake_port_t *f, const char *json);
void fp_connect(fake_port_t *f);

size_t fp_count(const fake_port_t *f, fp_kind_t kind);
size_t fp_count_conn(const fake_port_t *f, fp_kind_t kind, int conn);
size_t fp_sends(const fake_port_t *f, const char *type, int conn);
void fp_calls(const fake_port_t *f, int conn, char *out, size_t len);
size_t fp_appended(const fake_port_t *f, int conn, int16_t *out, size_t cap);
size_t fp_appended_len(const fake_port_t *f, int conn);
size_t fp_devices(const fake_port_t *f, pt_dev_event_t *out, size_t cap);
const fp_call_t *fp_device_at(const fake_port_t *f, size_t index);
const fp_call_t *fp_last(const fake_port_t *f, fp_kind_t kind);
size_t fp_played(const fake_port_t *f, int16_t *out, size_t cap);
size_t fp_played_before_end(const fake_port_t *f, size_t nth_end);
uint32_t fp_time_of(const fake_port_t *f, pt_dev_event_t ev, size_t nth);
size_t fp_device_count(const fake_port_t *f, pt_dev_event_t ev);
const fp_call_t *fp_nth(const fake_port_t *f, fp_kind_t kind, size_t nth);

char *fp_json_delta(const char *rid, const int16_t *pcm, size_t n);
char *fp_json_live_delta(const int16_t *pcm, size_t n);
