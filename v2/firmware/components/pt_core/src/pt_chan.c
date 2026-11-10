#include "pt_chan.h"

#include <string.h>

static void close_block(pt_chan_t *c)
{
    c->sum_left -= c->left[c->pos];
    c->sum_right -= c->right[c->pos];
    c->left[c->pos] = c->acc_left;
    c->right[c->pos] = c->acc_right;
    c->sum_left += c->acc_left;
    c->sum_right += c->acc_right;
    c->pos = (c->pos + 1) % PT_CHAN_BLOCKS;
    c->acc_left = 0;
    c->acc_right = 0;
    c->acc_frames = 0;
    if (c->current == 0 && c->sum_right > c->sum_left * PT_CHAN_SWITCH_RATIO) {
        c->current = 1;
    } else if (c->current == 1 && c->sum_left > c->sum_right * PT_CHAN_SWITCH_RATIO) {
        c->current = 0;
    }
}

void pt_chan_init(pt_chan_t *c)
{
    memset(c, 0, sizeof *c);
}

int pt_chan_update(pt_chan_t *c, const int16_t *left, const int16_t *right, size_t frames)
{
    for (size_t i = 0; i < frames; i++) {
        c->acc_left += (uint64_t)((int32_t)left[i] * (int32_t)left[i]);
        c->acc_right += (uint64_t)((int32_t)right[i] * (int32_t)right[i]);
        if (++c->acc_frames == PT_CHAN_BLOCK) close_block(c);
    }
    return c->current;
}

int pt_chan_current(const pt_chan_t *c)
{
    return c->current;
}
