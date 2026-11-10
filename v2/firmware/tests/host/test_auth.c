#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "pt_auth.h"

static uint8_t counter;

static void fake_random(void *ctx, uint8_t *buf, size_t len)
{
    (void)ctx;
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(counter++ * 7 + 3);
    }
}

void setUp(void)
{
    counter = 0;
}

void tearDown(void)
{
}

static void test_hash_then_verify(void)
{
    char h[PT_PASSWORD_HASH_MAX];
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_password_hash("correct horse", fake_random, NULL, h, sizeof(h)));
    TEST_ASSERT_TRUE(pt_password_verify("correct horse", h));
}

static void test_wrong_password_fails(void)
{
    char h[PT_PASSWORD_HASH_MAX];
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_password_hash("correct horse", fake_random, NULL, h, sizeof(h)));
    TEST_ASSERT_FALSE(pt_password_verify("correct hors", h));
    TEST_ASSERT_FALSE(pt_password_verify("", h));
}

static void test_hash_format_and_iterations(void)
{
    char h[PT_PASSWORD_HASH_MAX];
    char again[PT_PASSWORD_HASH_MAX];
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_password_hash("pw", fake_random, NULL, h, sizeof(h)));
    TEST_ASSERT_EQUAL_INT(0, strncmp(h, "pbkdf2-sha256$20000$", 20));
    TEST_ASSERT_EQUAL_INT(89, (int)strlen(h));
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_password_hash("pw", fake_random, NULL, again, sizeof(again)));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(h, again));
}

static void test_hash_buffer_too_small(void)
{
    char h[89];
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_password_hash("pw", fake_random, NULL, h, sizeof(h)));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_password_hash(NULL, fake_random, NULL, h, sizeof(h)));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_password_hash("pw", NULL, NULL, h, sizeof(h)));
}

static void test_verify_accepts_reference_hash(void)
{
    const char *ref = "pbkdf2-sha256$20000$AAECAwQFBgcICQoLDA0ODw==$UpgaygvzvEN5bTmV6fEvpKby2gGTtE5pl+MD+bjs/oc=";
    TEST_ASSERT_TRUE(pt_password_verify("p\xc3\xa4ss", ref));
    TEST_ASSERT_FALSE(pt_password_verify("pass", ref));
}

static void test_verify_rejects_garbage_and_scrypt(void)
{
    char h[PT_PASSWORD_HASH_MAX];
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_password_hash("pw", fake_random, NULL, h, sizeof(h)));
    TEST_ASSERT_FALSE(pt_password_verify("pw", ""));
    TEST_ASSERT_FALSE(pt_password_verify("pw", "scrypt$16384$8$1$AAAA$AAAA"));
    TEST_ASSERT_FALSE(pt_password_verify("pw", "pbkdf2-sha256$20000$"));
    TEST_ASSERT_FALSE(pt_password_verify("pw", "pbkdf2-sha256$20000$$"));
    TEST_ASSERT_FALSE(pt_password_verify("pw", "pbkdf2-sha256$0$AAAA$AAAA"));
    TEST_ASSERT_FALSE(pt_password_verify("pw", "pbkdf2-sha256$x$AAAA$AAAA"));
    TEST_ASSERT_FALSE(pt_password_verify("pw", "pbkdf2-sha256$20000$AAAA$AAAA$AAAA"));
    TEST_ASSERT_FALSE(pt_password_verify("pw", "pbkdf2-sha256$20000$!!!!$????"));
    char cut[PT_PASSWORD_HASH_MAX];
    memcpy(cut, h, sizeof(cut));
    cut[strlen(cut) - 5] = '\0';
    TEST_ASSERT_FALSE(pt_password_verify("pw", cut));
    TEST_ASSERT_FALSE(pt_password_verify(NULL, h));
    TEST_ASSERT_FALSE(pt_password_verify("pw", NULL));
}

static void test_logins_expire(void)
{
    pt_logins_t *s = pt_logins_new(100, 10, fake_random, NULL);
    TEST_ASSERT_NOT_NULL(s);
    char t[PT_LOGIN_TOKEN_MAX + 1];
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_logins_create(s, 1000, t, sizeof(t)));
    TEST_ASSERT_EQUAL_INT(43, (int)strlen(t));
    TEST_ASSERT_EQUAL_INT(0, (int)strcspn(t, "+/=") - 43);
    TEST_ASSERT_TRUE(pt_logins_valid(s, t, 1099));
    TEST_ASSERT_FALSE(pt_logins_valid(s, t, 1100));
    TEST_ASSERT_FALSE(pt_logins_valid(s, t, 1050));
    pt_logins_free(s);
}

static void test_logins_limit_drops_oldest(void)
{
    pt_logins_t *s = pt_logins_new(1000, 3, fake_random, NULL);
    char t[4][PT_LOGIN_TOKEN_MAX + 1];
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT(PT_OK, pt_logins_create(s, 10 + (uint32_t)i, t[i], sizeof(t[i])));
    }
    TEST_ASSERT_FALSE(pt_logins_valid(s, t[0], 20));
    for (int i = 1; i < 4; i++) {
        TEST_ASSERT_TRUE(pt_logins_valid(s, t[i], 20));
    }
    pt_logins_free(s);
}

static void test_logins_create_purges_expired_first(void)
{
    pt_logins_t *s = pt_logins_new(10, 2, fake_random, NULL);
    char a[PT_LOGIN_TOKEN_MAX + 1], b[PT_LOGIN_TOKEN_MAX + 1], c[PT_LOGIN_TOKEN_MAX + 1];
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_logins_create(s, 0, a, sizeof(a)));
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_logins_create(s, 8, b, sizeof(b)));
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_logins_create(s, 12, c, sizeof(c)));
    TEST_ASSERT_TRUE(pt_logins_valid(s, b, 12));
    TEST_ASSERT_TRUE(pt_logins_valid(s, c, 12));
    pt_logins_free(s);
}

static void test_logins_token_buffer_too_small(void)
{
    pt_logins_t *s = pt_logins_new(10, 2, fake_random, NULL);
    char t[43];
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_logins_create(s, 0, t, sizeof(t)));
    pt_logins_free(s);
}

static void test_session_drop(void)
{
    pt_logins_t *s = pt_logins_new(1000, 5, fake_random, NULL);
    char a[PT_LOGIN_TOKEN_MAX + 1], b[PT_LOGIN_TOKEN_MAX + 1];
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_logins_create(s, 0, a, sizeof(a)));
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_logins_create(s, 0, b, sizeof(b)));
    pt_logins_drop(s, a);
    pt_logins_drop(s, NULL);
    pt_logins_drop(s, "unknown");
    TEST_ASSERT_FALSE(pt_logins_valid(s, a, 1));
    TEST_ASSERT_TRUE(pt_logins_valid(s, b, 1));
    pt_logins_clear(s);
    TEST_ASSERT_FALSE(pt_logins_valid(s, b, 1));
    TEST_ASSERT_FALSE(pt_logins_valid(s, "", 1));
    TEST_ASSERT_FALSE(pt_logins_valid(s, NULL, 1));
    pt_logins_free(s);
}

static void test_throttle_fifth_in_queue_busy(void)
{
    pt_throttle_t *t = pt_throttle_new(3, 1000);
    TEST_ASSERT_NOT_NULL(t);
    uint32_t ip = 0xC0A80301u;
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT(PT_OK, pt_throttle_enter(t, ip));
    }
    TEST_ASSERT_EQUAL_INT(PT_ERR_BUSY, pt_throttle_enter(t, ip));
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_throttle_enter(t, ip + 1));
    pt_throttle_leave(t, ip);
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_throttle_enter(t, ip));
    TEST_ASSERT_EQUAL_INT(PT_ERR_BUSY, pt_throttle_enter(t, ip));
    pt_throttle_free(t);
}

static void test_throttle_slot_released_when_empty(void)
{
    pt_throttle_t *t = pt_throttle_new(3, 1000);
    for (uint32_t ip = 1; ip < 200; ip++) {
        TEST_ASSERT_EQUAL_INT(PT_OK, pt_throttle_enter(t, ip));
        pt_throttle_leave(t, ip);
    }
    pt_throttle_free(t);
}

static void test_failed_check_delays(void)
{
    pt_throttle_t *t = pt_throttle_new(3, 1234);
    TEST_ASSERT_EQUAL_UINT32(1234, pt_throttle_check_done(t, false));
    TEST_ASSERT_EQUAL_UINT32(0, pt_throttle_check_done(t, true));
    TEST_ASSERT_EQUAL_UINT32(0, pt_throttle_check_done(NULL, false));
    pt_throttle_free(t);
}

static void test_slot_held_until_leave_after_failed_check(void)
{
    pt_throttle_t *t = pt_throttle_new(3, 1000);
    uint32_t ip = 7;
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT(PT_OK, pt_throttle_enter(t, ip));
    }
    TEST_ASSERT_EQUAL_UINT32(1000, pt_throttle_check_done(t, false));
    TEST_ASSERT_EQUAL_INT(PT_ERR_BUSY, pt_throttle_enter(t, ip));
    pt_throttle_leave(t, ip);
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_throttle_enter(t, ip));
    TEST_ASSERT_EQUAL_INT(PT_ERR_BUSY, pt_throttle_enter(t, ip));
    pt_throttle_free(t);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hash_then_verify);
    RUN_TEST(test_wrong_password_fails);
    RUN_TEST(test_hash_format_and_iterations);
    RUN_TEST(test_hash_buffer_too_small);
    RUN_TEST(test_verify_accepts_reference_hash);
    RUN_TEST(test_verify_rejects_garbage_and_scrypt);
    RUN_TEST(test_logins_expire);
    RUN_TEST(test_logins_limit_drops_oldest);
    RUN_TEST(test_logins_create_purges_expired_first);
    RUN_TEST(test_logins_token_buffer_too_small);
    RUN_TEST(test_session_drop);
    RUN_TEST(test_throttle_fifth_in_queue_busy);
    RUN_TEST(test_throttle_slot_released_when_empty);
    RUN_TEST(test_failed_check_delays);
    RUN_TEST(test_slot_held_until_leave_after_failed_check);
    return UNITY_END();
}
