#include "pt_dsp.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PT_PI 3.14159265358979323846
#define CHUNK 128
#define RESAMPLER_PRECISION_BITS 20
#define RESAMPLER_MAX_FACTOR 8
#define HIGHPASS_Q1 0.5412f
#define HIGHPASS_Q2 1.3066f
#define LOUDNESS_KNEE 0.7f
#define EQ_Q 1.0f
#define EQ_NYQUIST_LIMIT 0.49f
#define MIC_HIGHPASS_HZ 250.0f
#define DB_TO_AMPLITUDE(db) powf(10.0f, (db) / 20.0f)

static int16_t to_i16(float v)
{
    if (v >= 32767.0f) return 32767;
    if (v <= -32768.0f) return -32768;
    return (int16_t)lrintf(v);
}

static float biquad_step(pt_biquad_t *f, float x)
{
    float y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}

void pt_biquad_highpass(pt_biquad_t *f, int rate, float hz, float q)
{
    double w0 = 2.0 * PT_PI * (double)hz / (double)rate;
    double alpha = sin(w0) / (2.0 * (double)q);
    double cos_w0 = cos(w0);
    double a0 = 1.0 + alpha;
    double b0 = (1.0 + cos_w0) / 2.0 / a0;
    f->b0 = (float)b0;
    f->b1 = (float)(-2.0 * b0);
    f->b2 = (float)b0;
    f->a1 = (float)(-2.0 * cos_w0 / a0);
    f->a2 = (float)((1.0 - alpha) / a0);
    f->z1 = 0.0f;
    f->z2 = 0.0f;
}

void pt_biquad_peaking(pt_biquad_t *f, int rate, float hz, float q, float gain_db)
{
    double a = pow(10.0, (double)gain_db / 40.0);
    double w0 = 2.0 * PT_PI * (double)hz / (double)rate;
    double alpha = sin(w0) / (2.0 * (double)q);
    double cos_w0 = cos(w0);
    double a0 = 1.0 + alpha / a;
    f->b0 = (float)((1.0 + alpha * a) / a0);
    f->b1 = (float)(-2.0 * cos_w0 / a0);
    f->b2 = (float)((1.0 - alpha * a) / a0);
    f->a1 = (float)(-2.0 * cos_w0 / a0);
    f->a2 = (float)((1.0 - alpha / a) / a0);
    f->z1 = 0.0f;
    f->z2 = 0.0f;
}

void pt_biquad_run(pt_biquad_t *f, const int16_t *in, int16_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) out[i] = to_i16(biquad_step(f, (float)in[i]));
}

void pt_hp4_init(pt_hp4_t *f, int rate, float hz)
{
    pt_biquad_highpass(&f->s[0], rate, hz, HIGHPASS_Q1);
    pt_biquad_highpass(&f->s[1], rate, hz, HIGHPASS_Q2);
}

void pt_hp4_run(pt_hp4_t *f, const int16_t *in, int16_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        float y = biquad_step(&f->s[0], (float)in[i]);
        out[i] = to_i16(biquad_step(&f->s[1], y));
    }
}

void pt_loudness_init(pt_loudness_t *l, float gain_db)
{
    l->gain = DB_TO_AMPLITUDE(gain_db);
    l->knee = LOUDNESS_KNEE;
}

void pt_loudness_run(const pt_loudness_t *l, const int16_t *in, int16_t *out, size_t n)
{
    float room = 1.0f - l->knee;
    for (size_t i = 0; i < n; i++) {
        float x = (float)in[i] / 32768.0f * l->gain;
        float level = fabsf(x);
        if (level > l->knee) x = copysignf(l->knee + room * tanhf((level - l->knee) / room), x);
        out[i] = to_i16(x * 32767.0f);
    }
}

static double linear_to_db(double x)
{
    return log10(x) * 20.0;
}

static double sine_phi(double x)
{
    return ((2.0517e-07 * x - 1.1303e-04) * x + .023154) * x + .55924;
}

static double sine_pow(double x)
{
    return log(.5) / log(sin(x * .5));
}

static double inverse_response(double drop_db, double attenuation)
{
    double x = sine_phi(attenuation);
    double drop = pow(10.0, drop_db / 20.0);
    double s = drop > .5 ? 1 - drop : drop;
    x = asin(pow(s, 1 / sine_pow(x))) / x;
    return drop > .5 ? x : 1 - x;
}

static double kaiser_beta(double att, double transition)
{
    static const double coefs[][4] = {
        {-6.784957e-10, 1.02856e-05, 0.1087556, -0.8988365 + .001},
        {-6.897885e-10, 1.027433e-05, 0.10876, -0.8994658 + .002},
        {-1.000683e-09, 1.030092e-05, 0.1087677, -0.9007898 + .003},
        {-3.654474e-10, 1.040631e-05, 0.1087085, -0.8977766 + .006},
        {8.106988e-09, 6.983091e-06, 0.1091387, -0.9172048 + .015},
        {9.519571e-09, 7.272678e-06, 0.1090068, -0.9140768 + .025},
        {-5.626821e-09, 1.342186e-05, 0.1083999, -0.9065452 + .05},
        {-9.965946e-08, 5.073548e-05, 0.1040967, -0.7672778 + .085},
        {1.604808e-07, -5.856462e-05, 0.1185998, -1.34824 + .1},
        {-1.511964e-07, 6.363034e-05, 0.1064627, -0.9876665 + .18},
    };
    const int count = (int)(sizeof coefs / sizeof coefs[0]);
    double realm = log(transition / .0005) / log(2.);
    int i0 = (int)realm, i1 = 1 + (int)realm;
    i0 = i0 < 0 ? 0 : i0 > count - 1 ? count - 1 : i0;
    i1 = i1 < 0 ? 0 : i1 > count - 1 ? count - 1 : i1;
    const double *c0 = coefs[i0];
    const double *c1 = coefs[i1];
    double b0 = ((c0[0] * att + c0[1]) * att + c0[2]) * att + c0[3];
    double b1 = ((c1[0] * att + c1[1]) * att + c1[2]) * att + c1[3];
    return b0 + (b1 - b0) * (realm - (int)realm);
}

static double bessel_i0(double x)
{
    double quarter = x * x / 4.0;
    double term = 1.0, sum = 1.0;
    for (int k = 1; k < 500; k++) {
        term *= quarter / ((double)k * (double)k);
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}

static double *design_lowpass(double passband, double stopband, double nyquist, double attenuation, int *taps)
{
    double fp = passband / nyquist, fs = stopband / nyquist;
    double transition = .5 * (fs - fp);
    if (transition > .5 * fs) transition = .5 * fs;
    double cutoff = fs - transition;
    double beta = kaiser_beta(attenuation, transition * .5 / cutoff);
    double span = ((.0007528358 - 1.577737e-05 * beta) * beta + .6248022) * beta + .06186902;
    int estimate = (int)ceil(span / transition + 1);
    int n = (estimate + 4 - 2) / 4 * 4 + 1;
    int m = n - 1;
    double *h = malloc((size_t)n * sizeof *h);
    if (!h) return NULL;
    double scale = 1.0 / bessel_i0(beta);
    double inverse_span = 1.0 / (.5 * m + .5);
    for (int i = 0; i <= m / 2; i++) {
        double z = i - .5 * m, x = z * PT_PI, y = z * inverse_span;
        double v = x != 0 ? sin(cutoff * x) / x : cutoff;
        h[i] = v * bessel_i0(beta * sqrt(1 - y * y)) * scale;
        if (m - i != i) h[m - i] = h[i];
    }
    *taps = n;
    return h;
}

struct pt_resampler {
    int up, down;
    bool passthrough;
    int taps, per_phase, centre;
    float *phases;
    float *hist;
    size_t hist_cap;
    long long base;
    long long fed;
    long long consumed;
    long long next_out;
    long long emitted;
    bool flushed;
};

static int gcd_int(int a, int b)
{
    while (b) {
        int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

pt_resampler_t *pt_resampler_new(int in_rate, int out_rate)
{
    if (in_rate <= 0 || out_rate <= 0) return NULL;
    int g = gcd_int(in_rate, out_rate);
    int up = out_rate / g, down = in_rate / g;
    if (up > RESAMPLER_MAX_FACTOR || down > RESAMPLER_MAX_FACTOR) return NULL;
    pt_resampler_t *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->up = up;
    r->down = down;
    if (up == 1 && down == 1) {
        r->passthrough = true;
        return r;
    }
    double rejection = RESAMPLER_PRECISION_BITS * linear_to_db(2.0);
    double passband = 1 - .05 / (1 - inverse_response(-3., rejection));
    double attenuation = (RESAMPLER_PRECISION_BITS + 1) * linear_to_db(2.0);
    int taps = 0;
    double *h = design_lowpass(passband, 1.0, (double)(up > down ? up : down), attenuation, &taps);
    if (!h) {
        free(r);
        return NULL;
    }
    r->taps = taps;
    r->centre = taps / 2;
    r->per_phase = (taps + up - 1) / up;
    r->phases = calloc((size_t)up * (size_t)r->per_phase, sizeof *r->phases);
    r->hist_cap = (size_t)r->per_phase + CHUNK + 2;
    r->hist = calloc(r->hist_cap, sizeof *r->hist);
    if (!r->phases || !r->hist) {
        free(h);
        pt_resampler_free(r);
        return NULL;
    }
    for (int phase = 0; phase < up; phase++)
        for (int k = 0; k < r->per_phase; k++) {
            int tap = phase + up * k;
            r->phases[phase * r->per_phase + (r->per_phase - 1 - k)] = tap < taps ? (float)(h[tap] * up) : 0.0f;
        }
    free(h);
    r->base = -(long long)(r->per_phase - 1);
    return r;
}

size_t pt_resampler_out_cap(const pt_resampler_t *r, size_t n)
{
    if (r->passthrough) return n;
    return (n * (size_t)r->up + (size_t)r->down - 1) / (size_t)r->down + 2;
}

static void resampler_trim(pt_resampler_t *r)
{
    long long t = (long long)r->down * r->next_out + r->centre;
    long long keep_from = t / r->up - r->per_phase + 1;
    if (keep_from <= r->base) return;
    long long shift = keep_from - r->base;
    long long len = r->fed - r->base;
    if (shift > len) shift = len;
    memmove(r->hist, r->hist + shift, (size_t)(len - shift) * sizeof *r->hist);
    r->base += shift;
}

static size_t resampler_feed(pt_resampler_t *r, const int16_t *in, size_t n, int16_t *out, size_t cap, size_t limit)
{
    resampler_trim(r);
    size_t len = (size_t)(r->fed - r->base);
    if (len + n > r->hist_cap) return 0;
    float *tail = r->hist + len;
    if (in)
        for (size_t i = 0; i < n; i++) tail[i] = (float)in[i];
    else
        memset(tail, 0, n * sizeof *tail);
    r->fed += (long long)n;

    size_t written = 0;
    while (written < cap && written < limit) {
        long long t = (long long)r->down * r->next_out + r->centre;
        long long q = t / r->up;
        if (q >= r->fed) break;
        const float *coef = r->phases + (size_t)(t % r->up) * (size_t)r->per_phase;
        const float *x = r->hist + (q - r->per_phase + 1 - r->base);
        float acc = 0.0f;
        for (int j = 0; j < r->per_phase; j++) acc += coef[j] * x[j];
        out[written++] = to_i16(acc);
        r->next_out++;
    }
    r->emitted += (long long)written;
    return written;
}

size_t pt_resampler_run(pt_resampler_t *r, const int16_t *in, size_t n, int16_t *out, size_t cap)
{
    if (r->flushed) return 0;
    if (r->passthrough) {
        size_t k = n < cap ? n : cap;
        if (k && in != out) memmove(out, in, k * sizeof *in);
        r->consumed += (long long)k;
        return k;
    }
    size_t total = 0;
    for (size_t at = 0; at < n; at += CHUNK) {
        size_t k = n - at < CHUNK ? n - at : CHUNK;
        if (cap - total < (size_t)(k * (size_t)r->up / (size_t)r->down) + 2) break;
        r->consumed += (long long)k;
        total += resampler_feed(r, in + at, k, out + total, cap - total, (size_t)-1);
    }
    return total;
}

size_t pt_resampler_flush(pt_resampler_t *r, int16_t *out, size_t cap)
{
    if (r->flushed || r->passthrough) return 0;
    r->flushed = true;
    long long target = (2 * r->consumed * r->up + r->down) / (2 * (long long)r->down);
    size_t total = 0;
    while (r->emitted < target && total < cap) {
        size_t limit = (size_t)(target - r->emitted);
        long long fed_before = r->fed;
        total += resampler_feed(r, NULL, CHUNK, out + total, cap - total, limit);
        if (r->fed == fed_before) break;
    }
    return total;
}

void pt_resampler_free(pt_resampler_t *r)
{
    if (!r) return;
    free(r->phases);
    free(r->hist);
    free(r);
}

static size_t run_cap(const pt_resampler_t *r, size_t n)
{
    if (r->passthrough) return n;
    return n * (size_t)r->up / (size_t)r->down + 2 * (n / CHUNK + 1) + 8;
}

float pt_db_to_gain(float db)
{
    return DB_TO_AMPLITUDE(db);
}

void pt_gain_apply(int16_t *buf, size_t n, float gain)
{
    if (!buf || fabsf(gain - 1.0f) < 1e-6f) return;
    for (size_t i = 0; i < n; i++) buf[i] = to_i16((float)buf[i] * gain);
}

int pt_peak_abs(const int16_t *buf, size_t n)
{
    int peak = 0;
    for (size_t i = 0; buf && i < n; i++) {
        int level = abs((int)buf[i]);
        if (level > peak) peak = level;
    }
    return peak;
}

void pt_peak_text(int peak, char *out, size_t len)
{
    if (peak > 0)
        snprintf(out, len, "peak %d (%.1f dBFS)", peak, 20.0 * log10((double)peak / 32768.0));
    else
        snprintf(out, len, "peak 0 (silence)");
}

struct pt_mic_path {
    int in_rate;
    long long holdoff;
    long long seen;
    int peak;
    pt_hp4_t highpass;
    pt_resampler_t *resampler;
};

pt_mic_path_t *pt_mic_path_new(int in_rate, int out_rate, const pt_settings_t *s)
{
    pt_mic_path_t *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->in_rate = in_rate;
    p->holdoff = (long long)((double)s->timing.mic_holdoff_s * in_rate + 1e-3);
    pt_hp4_init(&p->highpass, in_rate, MIC_HIGHPASS_HZ);
    p->resampler = pt_resampler_new(in_rate, out_rate);
    if (!p->resampler) {
        free(p);
        return NULL;
    }
    return p;
}

size_t pt_mic_path_run(pt_mic_path_t *p, const int16_t *in, size_t n, int16_t *out, size_t cap)
{
    long long before = p->seen;
    p->seen += (long long)n;
    if (before < p->holdoff) {
        size_t skip = (size_t)(p->holdoff - before);
        if (skip >= n) return 0;
        in += skip;
        n -= skip;
    }
    size_t total = 0;
    int16_t block[CHUNK];
    for (size_t at = 0; at < n; at += CHUNK) {
        size_t k = n - at < CHUNK ? n - at : CHUNK;
        pt_hp4_run(&p->highpass, in + at, block, k);
        for (size_t i = 0; i < k; i++) {
            int level = abs((int)block[i]);
            if (level > p->peak) p->peak = level;
        }
        size_t produced = pt_resampler_run(p->resampler, block, k, out + total, cap - total);
        total += produced;
    }
    return total;
}

size_t pt_mic_path_out_cap(const pt_mic_path_t *p, size_t n)
{
    return run_cap(p->resampler, n);
}

void pt_mic_path_summary(const pt_mic_path_t *p, char *out, size_t len)
{
    char peak[40];
    pt_peak_text(p->peak, peak, sizeof peak);
    snprintf(out, len, "%.2f s, %s", (double)p->seen / (double)p->in_rate, peak);
}

void pt_mic_path_free(pt_mic_path_t *p)
{
    if (!p) return;
    pt_resampler_free(p->resampler);
    free(p);
}

struct pt_speaker_path {
    pt_resampler_t *resampler;
    bool low_cut;
    pt_hp4_t highpass;
    bool eq_on[PT_EQ_BANDS];
    pt_biquad_t eq[PT_EQ_BANDS];
    pt_loudness_t loudness;
};

pt_speaker_path_t *pt_speaker_path_new(int in_rate, int out_rate, const pt_settings_t *s)
{
    pt_speaker_path_t *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->resampler = pt_resampler_new(in_rate, out_rate);
    if (!p->resampler) {
        free(p);
        return NULL;
    }
    float nyquist_limit = (float)out_rate * EQ_NYQUIST_LIMIT;
    p->low_cut = s->sound.low_cut_hz > 0.0f && s->sound.low_cut_hz < nyquist_limit;
    if (p->low_cut) pt_hp4_init(&p->highpass, out_rate, s->sound.low_cut_hz);
    for (int i = 0; i < PT_EQ_BANDS; i++) {
        p->eq_on[i] = fabsf(s->sound.eq_db[i]) > 1e-6f && PT_EQ_HZ[i] < nyquist_limit;
        if (p->eq_on[i]) pt_biquad_peaking(&p->eq[i], out_rate, PT_EQ_HZ[i], EQ_Q, s->sound.eq_db[i]);
    }
    pt_loudness_init(&p->loudness, s->sound.loudness_gain_db);
    return p;
}

static void speaker_shape(pt_speaker_path_t *p, int16_t *buf, size_t n)
{
    for (size_t at = 0; at < n; at += CHUNK) {
        size_t k = n - at < CHUNK ? n - at : CHUNK;
        int16_t *block = buf + at;
        for (size_t i = 0; i < k; i++) {
            float x = (float)block[i];
            if (p->low_cut) {
                x = biquad_step(&p->highpass.s[0], x);
                x = biquad_step(&p->highpass.s[1], x);
            }
            for (int b = 0; b < PT_EQ_BANDS; b++)
                if (p->eq_on[b]) x = biquad_step(&p->eq[b], x);
            block[i] = to_i16(x);
        }
        pt_loudness_run(&p->loudness, block, block, k);
    }
}

size_t pt_speaker_path_run(pt_speaker_path_t *p, const int16_t *in, size_t n, int16_t *out, size_t cap)
{
    size_t produced = pt_resampler_run(p->resampler, in, n, out, cap);
    speaker_shape(p, out, produced);
    return produced;
}

size_t pt_speaker_path_out_cap(const pt_speaker_path_t *p, size_t n)
{
    return run_cap(p->resampler, n);
}

size_t pt_speaker_path_flush(pt_speaker_path_t *p, int16_t *out, size_t cap)
{
    size_t produced = pt_resampler_flush(p->resampler, out, cap);
    speaker_shape(p, out, produced);
    return produced;
}

void pt_speaker_path_free(pt_speaker_path_t *p)
{
    if (!p) return;
    pt_resampler_free(p->resampler);
    free(p);
}
