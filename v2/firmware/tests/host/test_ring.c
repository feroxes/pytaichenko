#include "unity.h"

#include "pt_ring.h"

void setUp(void) {}
void tearDown(void) {}

static float at(pt_ring_state_t s, float from, uint32_t t)
{
    pt_ring_t r;
    pt_ring_set(&r, s, 1000, from);
    return pt_ring_level(&r, 1000 + t);
}

void test_ring_not_ready_breathes_0_to_100_eased(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.01f, at(PT_RING_NOT_READY, 0.0f, 0));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.01f, at(PT_RING_NOT_READY, 1.0f, 0));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, at(PT_RING_NOT_READY, 0.0f, 2000));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, at(PT_RING_NOT_READY, 0.0f, 4000));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.2575f, at(PT_RING_NOT_READY, 0.0f, 1000));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, at(PT_RING_NOT_READY, 0.0f, 3000));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, at(PT_RING_NOT_READY, 0.0f, 5000));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, at(PT_RING_NOT_READY, 0.0f, 6000));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, at(PT_RING_NOT_READY, 0.0f, 7000));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, at(PT_RING_NOT_READY, 0.0f, 8000));
}

void test_ring_transition_400ms_linear(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.2f, at(PT_RING_LISTENING, 0.2f, 0));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.6f, at(PT_RING_LISTENING, 0.2f, 200));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, at(PT_RING_LISTENING, 0.2f, 400));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, at(PT_RING_LISTENING, 0.2f, 5000));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.7f, at(PT_RING_ANSWERING, 0.7f, 123));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.4f, at(PT_RING_FOLLOW_UP, 0.7f, 400));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, at(PT_RING_SPEECH, 0.0f, 400));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, at(PT_RING_OFF, 1.0f, 400));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, at(PT_RING_OFF, 1.0f, 200));
}

void test_ring_error_three_soft_flashes_then_off(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, at(PT_RING_ERROR, 0.7f, 0));
    for (int i = 0; i < 3; i++) {
        uint32_t base = (uint32_t)i * 300;
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, at(PT_RING_ERROR, 0.0f, base + 75));
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, at(PT_RING_ERROR, 0.0f, base + 150));
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, at(PT_RING_ERROR, 0.0f, base + 250));
    }
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, at(PT_RING_ERROR, 0.0f, 1000));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, at(PT_RING_ERROR, 0.0f, 60000));
}

void test_ring_reset_five_fast_flashes(void)
{
    for (int i = 0; i < 5; i++) {
        uint32_t base = (uint32_t)i * 200;
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, at(PT_RING_RESET, 0.0f, base + 50));
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, at(PT_RING_RESET, 0.0f, base + 100));
    }
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, at(PT_RING_RESET, 0.0f, 1000));
}

void test_ring_thinking_pulse_range(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.7f, at(PT_RING_THINKING, 0.7f, 0));
    float lo = 2.0f;
    float hi = -1.0f;
    for (uint32_t t = 400; t < 10000; t += 7) {
        float v = at(PT_RING_THINKING, 0.7f, t);
        if (v < lo)
            lo = v;
        if (v > hi)
            hi = v;
    }
    TEST_ASSERT_FLOAT_WITHIN(0.002f, 0.03f, lo);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, hi);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.03f, at(PT_RING_THINKING, 1.0f, 700));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, at(PT_RING_THINKING, 1.0f, 1400));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.2725f, at(PT_RING_THINKING, 1.0f, 1050));
}

void test_ring_min_power_floor(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pt_ring_duty(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pt_ring_duty(-0.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0.03f, pt_ring_duty(0.0001f));
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0.03f, pt_ring_duty(0.01f));
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0.05f, pt_ring_duty(0.05f));
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 1.0f, pt_ring_duty(1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 1.0f, pt_ring_duty(1.5f));
}

void test_ring_set_restarts_animation(void)
{
    pt_ring_t r;
    pt_ring_set(&r, PT_RING_NOT_READY, 0, 0.0f);
    pt_ring_set(&r, PT_RING_NOT_READY, 3000, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.01f, pt_ring_level(&r, 3000));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, pt_ring_level(&r, 5000));
}

void test_ring_clock_wraps(void)
{
    pt_ring_t r;
    pt_ring_set(&r, PT_RING_LISTENING, 0xFFFFFF00u, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, pt_ring_level(&r, 0xFFFFFF00u));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, pt_ring_level(&r, 0xFFFFFF00u + 400u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ring_not_ready_breathes_0_to_100_eased);
    RUN_TEST(test_ring_transition_400ms_linear);
    RUN_TEST(test_ring_error_three_soft_flashes_then_off);
    RUN_TEST(test_ring_reset_five_fast_flashes);
    RUN_TEST(test_ring_thinking_pulse_range);
    RUN_TEST(test_ring_min_power_floor);
    RUN_TEST(test_ring_set_restarts_animation);
    RUN_TEST(test_ring_clock_wraps);
    return UNITY_END();
}
