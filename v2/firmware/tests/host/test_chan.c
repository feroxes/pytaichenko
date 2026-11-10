#include "unity.h"

#include <stdint.h>

#include "pt_chan.h"

#define SECOND 16000
#define HALF_SECOND (SECOND / 2)

void setUp(void) {}
void tearDown(void) {}

static void feed(pt_chan_t *c, int16_t l, int16_t r, size_t frames)
{
    int16_t lb[PT_CHAN_BLOCK];
    int16_t rb[PT_CHAN_BLOCK];
    for (size_t i = 0; i < PT_CHAN_BLOCK; i++) {
        lb[i] = (i & 1) ? l : (int16_t)-l;
        rb[i] = (i & 1) ? r : (int16_t)-r;
    }
    while (frames) {
        size_t n = frames < PT_CHAN_BLOCK ? frames : PT_CHAN_BLOCK;
        pt_chan_update(c, lb, rb, n);
        frames -= n;
    }
}

void test_chan_starts_on_left(void)
{
    pt_chan_t c;
    pt_chan_init(&c);
    TEST_ASSERT_EQUAL_INT(0, pt_chan_current(&c));
    TEST_ASSERT_EQUAL_INT(0, pt_chan_update(&c, NULL, NULL, 0));
}

void test_chan_equal_stays_left(void)
{
    pt_chan_t c;
    pt_chan_init(&c);
    feed(&c, 1000, 1000, SECOND);
    TEST_ASSERT_EQUAL_INT(0, pt_chan_current(&c));
}

void test_chan_louder_right_wins(void)
{
    pt_chan_t c;
    pt_chan_init(&c);
    feed(&c, 500, 2000, HALF_SECOND);
    TEST_ASSERT_EQUAL_INT(1, pt_chan_current(&c));
}

void test_chan_louder_left_wins_back(void)
{
    pt_chan_t c;
    pt_chan_init(&c);
    feed(&c, 500, 2000, HALF_SECOND);
    TEST_ASSERT_EQUAL_INT(1, pt_chan_current(&c));
    feed(&c, 2000, 500, HALF_SECOND + PT_CHAN_BLOCK);
    TEST_ASSERT_EQUAL_INT(0, pt_chan_current(&c));
}

void test_chan_under_3_db_does_not_switch(void)
{
    pt_chan_t c;
    pt_chan_init(&c);
    feed(&c, 1000, 1300, SECOND);
    TEST_ASSERT_EQUAL_INT(0, pt_chan_current(&c));
    feed(&c, 1000, 1500, SECOND);
    TEST_ASSERT_EQUAL_INT(1, pt_chan_current(&c));
    feed(&c, 1300, 1000, SECOND);
    TEST_ASSERT_EQUAL_INT(1, pt_chan_current(&c));
}

void test_chan_window_forgets_after_half_second(void)
{
    pt_chan_t c;
    pt_chan_init(&c);
    feed(&c, 500, 20000, HALF_SECOND);
    TEST_ASSERT_EQUAL_INT(1, pt_chan_current(&c));
    feed(&c, 200, 100, HALF_SECOND / 2);
    TEST_ASSERT_EQUAL_INT(1, pt_chan_current(&c));
    feed(&c, 200, 100, HALF_SECOND / 2);
    TEST_ASSERT_EQUAL_INT(0, pt_chan_current(&c));
}

void test_chan_uneven_calls_match_whole_blocks(void)
{
    pt_chan_t a, b;
    pt_chan_init(&a);
    pt_chan_init(&b);
    int16_t l[SECOND], r[SECOND];
    for (size_t i = 0; i < SECOND; i++) {
        l[i] = (int16_t)((i * 37) % 2000 - 1000);
        r[i] = (int16_t)(((i * 91) % 6000 - 3000) * (i > 8000 ? 2 : 1));
    }
    pt_chan_update(&a, l, r, SECOND);
    size_t sizes[] = {1, 7, 160, 161, 513, 3};
    size_t pos = 0, k = 0;
    while (pos < SECOND) {
        size_t n = sizes[k++ % 6];
        if (n > SECOND - pos) n = SECOND - pos;
        pt_chan_update(&b, l + pos, r + pos, n);
        pos += n;
    }
    TEST_ASSERT_EQUAL_INT(pt_chan_current(&a), pt_chan_current(&b));
    TEST_ASSERT_EQUAL_UINT64(a.sum_left, b.sum_left);
    TEST_ASSERT_EQUAL_UINT64(a.sum_right, b.sum_right);
}

void test_chan_full_scale_does_not_overflow(void)
{
    pt_chan_t c;
    pt_chan_init(&c);
    feed(&c, -32768, -32768, SECOND);
    TEST_ASSERT_EQUAL_INT(0, pt_chan_current(&c));
    feed(&c, 100, 32767, SECOND);
    TEST_ASSERT_EQUAL_INT(1, pt_chan_current(&c));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_chan_starts_on_left);
    RUN_TEST(test_chan_equal_stays_left);
    RUN_TEST(test_chan_louder_right_wins);
    RUN_TEST(test_chan_louder_left_wins_back);
    RUN_TEST(test_chan_under_3_db_does_not_switch);
    RUN_TEST(test_chan_window_forgets_after_half_second);
    RUN_TEST(test_chan_uneven_calls_match_whole_blocks);
    RUN_TEST(test_chan_full_scale_does_not_overflow);
    return UNITY_END();
}
