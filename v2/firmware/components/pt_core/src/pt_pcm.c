#include "pt_pcm.h"

#include <math.h>
#include <string.h>

#define PT_VOLUME_RANGE_DB 40.0f

float pt_volume_gain(float volume)
{
    if (!(volume > 0.0f))
        return 0.0f;
    if (volume > 1.0f)
        volume = 1.0f;
    return powf(10.0f, PT_VOLUME_RANGE_DB * (volume - 1.0f) / 20.0f);
}

void pt_pcm_apply_gain(int16_t *pcm, size_t n, float gain)
{
    for (size_t i = 0; i < n; i++) {
        float v = roundf((float)pcm[i] * gain);
        if (v > 32767.0f)
            v = 32767.0f;
        else if (v < -32768.0f)
            v = -32768.0f;
        pcm[i] = (int16_t)v;
    }
}

size_t pt_pcm_unpack_i2s32_stereo(const int32_t *in, size_t frames, int16_t *left, int16_t *right)
{
    for (size_t i = 0; i < frames; i++) {
        left[i] = (int16_t)(in[2 * i] >> 16);
        right[i] = (int16_t)(in[2 * i + 1] >> 16);
    }
    return frames;
}

struct pt_pcm_queue {
    int16_t *buf;
    size_t cap;
    size_t head;
    size_t len;
};

pt_pcm_queue_t *pt_pcm_queue_new(size_t cap_samples, void *(*alloc)(size_t))
{
    if (cap_samples == 0 || cap_samples > (SIZE_MAX - sizeof(pt_pcm_queue_t)) / sizeof(int16_t))
        return NULL;
    pt_pcm_queue_t *q = alloc(sizeof *q + cap_samples * sizeof(int16_t));
    if (!q)
        return NULL;
    q->buf = (int16_t *)(q + 1);
    q->cap = cap_samples;
    q->head = 0;
    q->len = 0;
    return q;
}

size_t pt_pcm_queue_push(pt_pcm_queue_t *q, const int16_t *pcm, size_t n)
{
    size_t room = q->cap - q->len;
    if (n > room)
        n = room;
    size_t tail = (q->head + q->len) % q->cap;
    size_t first = q->cap - tail;
    if (first > n)
        first = n;
    memcpy(q->buf + tail, pcm, first * sizeof(int16_t));
    memcpy(q->buf, pcm + first, (n - first) * sizeof(int16_t));
    q->len += n;
    return n;
}

size_t pt_pcm_queue_pop(pt_pcm_queue_t *q, int16_t *out, size_t n)
{
    if (n > q->len)
        n = q->len;
    size_t first = q->cap - q->head;
    if (first > n)
        first = n;
    memcpy(out, q->buf + q->head, first * sizeof(int16_t));
    memcpy(out + first, q->buf, (n - first) * sizeof(int16_t));
    q->head = (q->head + n) % q->cap;
    q->len -= n;
    return n;
}

size_t pt_pcm_queue_len(const pt_pcm_queue_t *q)
{
    return q->len;
}

size_t pt_pcm_queue_free(const pt_pcm_queue_t *q)
{
    return q->cap - q->len;
}

void pt_pcm_queue_clear(pt_pcm_queue_t *q)
{
    q->head = 0;
    q->len = 0;
}

void pt_pcm_queue_delete(pt_pcm_queue_t *q, void (*dealloc)(void *))
{
    if (q)
        dealloc(q);
}

static const float UP2_COEF[PT_UP2_TAPS] = {
    -9.223257769e-05f, 3.448370328e-04f, -8.406545786e-04f, 1.701917418e-03f,
    -3.081938989e-03f, 5.168766519e-03f, -8.192291519e-03f, 1.243962924e-02f,
    -1.828842781e-02f, 2.627895668e-02f, -3.727497484e-02f, 5.285060341e-02f,
    -7.635196077e-02f, 1.165158559e-01f, -2.055640688e-01f, 6.343859837e-01f,
    6.343859837e-01f, -2.055640688e-01f, 1.165158559e-01f, -7.635196077e-02f,
    5.285060341e-02f, -3.727497484e-02f, 2.627895668e-02f, -1.828842781e-02f,
    1.243962924e-02f, -8.192291519e-03f, 5.168766519e-03f, -3.081938989e-03f,
    1.701917418e-03f, -8.406545786e-04f, 3.448370328e-04f, -9.223257769e-05f,
};

void pt_up2_reset(pt_up2_t *s)
{
    memset(s->hist, 0, sizeof(s->hist));
}

static int16_t clamp16(float v)
{
    v = roundf(v);
    if (v > 32767.0f)
        return 32767;
    if (v < -32768.0f)
        return -32768;
    return (int16_t)v;
}

size_t pt_up2_run(pt_up2_t *s, const int16_t *in, size_t n, int16_t *out)
{
    for (size_t m = 0; m < n; m++) {
        memmove(s->hist + 1, s->hist, (PT_UP2_TAPS - 1) * sizeof(s->hist[0]));
        s->hist[0] = in[m];
        float acc = 0.0f;
        for (size_t j = 0; j < PT_UP2_TAPS; j++)
            acc += UP2_COEF[j] * (float)s->hist[j];
        out[2 * m] = clamp16(acc);
        out[2 * m + 1] = s->hist[PT_UP2_DELAY];
    }
    return 2 * n;
}

#define TEST_SOUND_RATE 24000.0f
#define TEST_SOUND_AMP 7300.0f
#define TEST_SOUND_FADE 240

size_t pt_test_sound(int16_t *out, size_t cap)
{
    static const float notes[] = {523.25f, 659.25f, 783.99f, 1046.5f};
    if (out == NULL || cap < PT_TEST_SOUND_SAMPLES)
        return 0;
    size_t at = 0;
    for (size_t k = 0; k < 4; k++) {
        size_t len = k < 3 ? PT_TEST_SOUND_NOTE : PT_TEST_SOUND_LAST;
        float step = 2.0f * (float)M_PI * notes[k] / TEST_SOUND_RATE;
        for (size_t i = 0; i < len; i++) {
            float env = 1.0f;
            if (i < TEST_SOUND_FADE)
                env = (float)i / TEST_SOUND_FADE;
            else if (len - 1 - i < TEST_SOUND_FADE)
                env = (float)(len - 1 - i) / TEST_SOUND_FADE;
            out[at + i] = (int16_t)lroundf(TEST_SOUND_AMP * env * sinf(step * (float)i));
        }
        at += len;
    }
    return at;
}
