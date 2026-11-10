#pragma once

#include <stddef.h>
#include <stdint.h>

#define PT_CHAN_BLOCK 160
#define PT_CHAN_BLOCKS 50
#define PT_CHAN_SWITCH_RATIO 2

typedef struct {
    uint64_t left[PT_CHAN_BLOCKS];
    uint64_t right[PT_CHAN_BLOCKS];
    uint64_t sum_left;
    uint64_t sum_right;
    uint64_t acc_left;
    uint64_t acc_right;
    size_t acc_frames;
    size_t pos;
    int current;
} pt_chan_t;

void pt_chan_init(pt_chan_t *c);
int pt_chan_update(pt_chan_t *c, const int16_t *left, const int16_t *right, size_t frames);
int pt_chan_current(const pt_chan_t *c);
