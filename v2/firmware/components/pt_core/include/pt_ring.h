#pragma once

#include <stdint.h>

#define PT_RING_MIN_POWER 0.03f

typedef enum {
    PT_RING_OFF,
    PT_RING_NOT_READY,
    PT_RING_LISTENING,
    PT_RING_FOLLOW_UP,
    PT_RING_SPEECH,
    PT_RING_THINKING,
    PT_RING_ANSWERING,
    PT_RING_ERROR,
    PT_RING_RESET
} pt_ring_state_t;

typedef struct {
    pt_ring_state_t state;
    uint32_t since_ms;
    float from;
} pt_ring_t;

void pt_ring_set(pt_ring_t *r, pt_ring_state_t s, uint32_t now_ms, float current);
float pt_ring_level(const pt_ring_t *r, uint32_t now_ms);
float pt_ring_duty(float level);
