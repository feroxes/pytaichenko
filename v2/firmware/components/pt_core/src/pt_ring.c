#include "pt_ring.h"

#define TRANSITION_MS 400u
#define BREATH_MAX 1.0f
#define BREATH_START 0.01f
#define BREATH_HALF_MS 2000u
#define THINK_MIN 0.03f
#define THINK_MAX 1.0f
#define THINK_HALF_MS 700u
#define ERROR_FLASHES 3u
#define ERROR_FLASH_MS 150u
#define RESET_FLASHES 5u
#define RESET_FLASH_MS 100u

static float ramp(float from, float to, uint32_t dt)
{
    if (dt >= TRANSITION_MS)
        return to;
    return from + (to - from) * ((float)dt / (float)TRANSITION_MS);
}

static float triangle(uint32_t phase, uint32_t half_ms)
{
    if (phase < half_ms)
        return (float)phase / (float)half_ms;
    return (float)(2u * half_ms - phase) / (float)half_ms;
}

static float flashes(uint32_t dt, uint32_t count, uint32_t flash_ms)
{
    uint32_t period = 2u * flash_ms;
    if (dt >= count * period)
        return 0.0f;
    uint32_t phase = dt % period;
    if (phase >= flash_ms)
        return 0.0f;
    return triangle(phase, flash_ms / 2u);
}

static float eased(float x)
{
    return x * x;
}

static float breathe(uint32_t dt)
{
    if (dt < BREATH_HALF_MS)
        return BREATH_START + (BREATH_MAX - BREATH_START) * eased((float)dt / (float)BREATH_HALF_MS);
    uint32_t phase = (dt - BREATH_HALF_MS) % (2u * BREATH_HALF_MS);
    return BREATH_MAX * eased(1.0f - triangle(phase, BREATH_HALF_MS));
}

static float think(uint32_t dt)
{
    uint32_t phase = dt % (2u * THINK_HALF_MS);
    return THINK_MIN + (THINK_MAX - THINK_MIN) * eased(1.0f - triangle(phase, THINK_HALF_MS));
}

void pt_ring_set(pt_ring_t *r, pt_ring_state_t s, uint32_t now_ms, float current)
{
    r->state = s;
    r->since_ms = now_ms;
    r->from = current;
}

float pt_ring_level(const pt_ring_t *r, uint32_t now_ms)
{
    uint32_t dt = now_ms - r->since_ms;
    switch (r->state) {
    case PT_RING_NOT_READY:
        return breathe(dt);
    case PT_RING_LISTENING:
    case PT_RING_SPEECH:
        return ramp(r->from, 1.0f, dt);
    case PT_RING_FOLLOW_UP:
        return ramp(r->from, 0.4f, dt);
    case PT_RING_ANSWERING:
        return ramp(r->from, 0.7f, dt);
    case PT_RING_THINKING:
        return ramp(r->from, think(dt), dt);
    case PT_RING_ERROR:
        return flashes(dt, ERROR_FLASHES, ERROR_FLASH_MS);
    case PT_RING_RESET:
        return flashes(dt, RESET_FLASHES, RESET_FLASH_MS);
    case PT_RING_OFF:
    default:
        return ramp(r->from, 0.0f, dt);
    }
}

float pt_ring_duty(float level)
{
    if (level <= 0.0f)
        return 0.0f;
    if (level >= 1.0f)
        return 1.0f;
    if (level < PT_RING_MIN_POWER)
        return PT_RING_MIN_POWER;
    return level;
}
