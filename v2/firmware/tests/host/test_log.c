#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "unity.h"
#include "pt_log.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static cJSON *lines_of(char *json)
{
    TEST_ASSERT_NOT_NULL(json);
    cJSON *root = cJSON_Parse(json);
    free(json);
    TEST_ASSERT_NOT_NULL(root);
    cJSON *lines = cJSON_GetObjectItem(root, "lines");
    TEST_ASSERT_TRUE(cJSON_IsArray(lines));
    return root;
}

static const char *text_at(cJSON *root, int i)
{
    cJSON *item = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "lines"), i);
    return cJSON_GetObjectItem(item, "text")->valuestring;
}

static int seq_at(cJSON *root, int i)
{
    cJSON *item = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "lines"), i);
    return cJSON_GetObjectItem(item, "seq")->valueint;
}

static void test_log_keeps_last_lines(void)
{
    pt_log_t *log = pt_log_new(3, 40);
    TEST_ASSERT_NOT_NULL(log);
    for (int i = 1; i <= 5; i++) {
        pt_log_add(log, (uint32_t)(100 + i), "line %d", i);
    }
    cJSON *root = lines_of(pt_log_json(log, 0));
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetArraySize(cJSON_GetObjectItem(root, "lines")));
    TEST_ASSERT_EQUAL_STRING("line 3", text_at(root, 0));
    TEST_ASSERT_EQUAL_STRING("line 5", text_at(root, 2));
    TEST_ASSERT_EQUAL_INT(3, seq_at(root, 0));
    TEST_ASSERT_EQUAL_INT(5, seq_at(root, 2));
    cJSON *last = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "lines"), 2);
    TEST_ASSERT_EQUAL_INT(105, cJSON_GetObjectItem(last, "t")->valueint);
    cJSON_Delete(root);
    pt_log_free(log);
}

static void test_log_truncates_long_line_on_utf8_boundary(void)
{
    pt_log_t *log = pt_log_new(4, 5);
    pt_log_add(log, 1, "%s", "\xD0\x96\xD0\x96\xD0\x96\xD0\x96");
    pt_log_add(log, 2, "%s", "ab\xE2\x82\xAC" "cd");
    pt_log_add(log, 3, "%s", "abcdefgh");
    pt_log_add(log, 4, "%s", "abc\xF0\x9F\x98\x80");
    cJSON *root = lines_of(pt_log_json(log, 0));
    TEST_ASSERT_EQUAL_STRING("\xD0\x96\xD0\x96", text_at(root, 0));
    TEST_ASSERT_EQUAL_STRING("ab\xE2\x82\xAC", text_at(root, 1));
    TEST_ASSERT_EQUAL_STRING("abcde", text_at(root, 2));
    TEST_ASSERT_EQUAL_STRING("abc", text_at(root, 3));
    cJSON_Delete(root);
    pt_log_free(log);
}

static void add_through_va(pt_log_t *log, uint32_t now_s, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static void add_through_va(pt_log_t *log, uint32_t now_s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    pt_log_vadd(log, now_s, fmt, ap);
    va_end(ap);
}

static void test_log_vadd_formats_and_cuts_on_utf8_boundary(void)
{
    pt_log_t *log = pt_log_new(4, 7);
    add_through_va(log, 1, "n=%d %s", 42, "ok");
    add_through_va(log, 2, "%s", "\xD0\x96\xD0\x96\xD0\x96\xD0\x96");
    add_through_va(log, 3, "ab%s", "\xE2\x82\xAC" "cd");
    add_through_va(log, 4, "%d:%s", 7, "\xF0\x9F\x98\x80\xF0\x9F\x98\x80");
    cJSON *root = lines_of(pt_log_json(log, 0));
    TEST_ASSERT_EQUAL_STRING("n=42 ok", text_at(root, 0));
    TEST_ASSERT_EQUAL_STRING("\xD0\x96\xD0\x96\xD0\x96", text_at(root, 1));
    TEST_ASSERT_EQUAL_STRING("ab\xE2\x82\xAC" "cd", text_at(root, 2));
    TEST_ASSERT_EQUAL_STRING("7:\xF0\x9F\x98\x80", text_at(root, 3));
    cJSON_Delete(root);
    pt_log_free(log);
    add_through_va(NULL, 1, "x");
}

static void test_log_line_that_fits_exactly_is_kept(void)
{
    pt_log_t *log = pt_log_new(2, 4);
    pt_log_add(log, 1, "%s", "\xD0\x96\xD0\x96");
    pt_log_add(log, 2, "%s", "abcd");
    cJSON *root = lines_of(pt_log_json(log, 0));
    TEST_ASSERT_EQUAL_STRING("\xD0\x96\xD0\x96", text_at(root, 0));
    TEST_ASSERT_EQUAL_STRING("abcd", text_at(root, 1));
    cJSON_Delete(root);
    pt_log_free(log);
}

static void test_log_json_after(void)
{
    pt_log_t *log = pt_log_new(10, 40);
    cJSON *root = lines_of(pt_log_json(log, 0));
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(cJSON_GetObjectItem(root, "lines")));
    cJSON_Delete(root);
    for (int i = 1; i <= 4; i++) {
        pt_log_add(log, 0, "n%d", i);
    }
    root = lines_of(pt_log_json(log, 2));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(cJSON_GetObjectItem(root, "lines")));
    TEST_ASSERT_EQUAL_STRING("n3", text_at(root, 0));
    cJSON_Delete(root);
    root = lines_of(pt_log_json(log, 4));
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(cJSON_GetObjectItem(root, "lines")));
    cJSON_Delete(root);
    root = lines_of(pt_log_json(log, 99));
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(cJSON_GetObjectItem(root, "lines")));
    cJSON_Delete(root);
    pt_log_free(log);
}

static void test_log_escapes_text(void)
{
    pt_log_t *log = pt_log_new(2, 40);
    pt_log_add(log, 0, "quote \" and \\ and \n newline");
    cJSON *root = lines_of(pt_log_json(log, 0));
    TEST_ASSERT_EQUAL_STRING("quote \" and \\ and \n newline", text_at(root, 0));
    cJSON_Delete(root);
    pt_log_free(log);
}

static void test_log_escapes_controls_cyrillic_and_invalid_bytes(void)
{
    pt_log_t *log = pt_log_new(8, 120);
    pt_log_add(log, 0, "%s", "q\" b\\ t\t r\r n\n \x01\x1f \x7f end");
    pt_log_add(log, 0, "%s", "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD1\x96\xD1\x82 \xE2\x82\xAC \xF0\x9F\x98\x80");
    pt_log_add(log, 0, "%s", "bad \xFF\xC3 \xE2\x82 tail");
    pt_log_add(log, 0, "%s", "");
    char *json = pt_log_json(log, 0);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\\u0001\\u001f"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\\u0009 r\\u000d n\\u000a"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\xD0\x9F\xD1\x80\xD0\xB8"));
    for (const char *p = json; *p != '\0'; p++) {
        TEST_ASSERT_TRUE((unsigned char)*p >= 0x20);
    }
    cJSON *root = lines_of(json);
    TEST_ASSERT_EQUAL_STRING("q\" b\\ t\t r\r n\n \x01\x1f \x7f end", text_at(root, 0));
    TEST_ASSERT_EQUAL_STRING("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD1\x96\xD1\x82 \xE2\x82\xAC \xF0\x9F\x98\x80", text_at(root, 1));
    TEST_ASSERT_EQUAL_STRING("bad \xEF\xBF\xBD\xEF\xBF\xBD \xEF\xBF\xBD\xEF\xBF\xBD tail", text_at(root, 2));
    TEST_ASSERT_EQUAL_STRING("", text_at(root, 3));
    cJSON_Delete(root);
    pt_log_free(log);
}

static void test_log_full_log_parses_back(void)
{
    pt_log_t *log = pt_log_new(400, 160);
    TEST_ASSERT_NOT_NULL(log);
    for (int i = 1; i <= 900; i++) {
        pt_log_add(log, (uint32_t)(3600 + i), "line %d \"q\" \\ \xD0\x96 tab\t%0100d", i, i);
    }
    char *json = pt_log_json(log, 0);
    TEST_ASSERT_NOT_NULL(json);
    cJSON *root = lines_of(json);
    cJSON *arr = cJSON_GetObjectItem(root, "lines");
    TEST_ASSERT_EQUAL_INT(400, cJSON_GetArraySize(arr));
    for (int i = 0; i < 400; i++) {
        char want[200];
        int seq = 501 + i;
        snprintf(want, sizeof(want), "line %d \"q\" \\ \xD0\x96 tab\t%0100d", seq, seq);
        TEST_ASSERT_EQUAL_INT(seq, seq_at(root, i));
        TEST_ASSERT_EQUAL_STRING(want, text_at(root, i));
        cJSON *item = cJSON_GetArrayItem(arr, i);
        TEST_ASSERT_EQUAL_INT(3600 + seq, cJSON_GetObjectItem(item, "t")->valueint);
    }
    cJSON_Delete(root);
    root = lines_of(pt_log_json(log, 895));
    TEST_ASSERT_EQUAL_INT(5, cJSON_GetArraySize(cJSON_GetObjectItem(root, "lines")));
    cJSON_Delete(root);
    pt_log_free(log);
}

static void test_log_empty_json_shape(void)
{
    pt_log_t *log = pt_log_new(3, 10);
    char *json = pt_log_json(log, 0);
    TEST_ASSERT_EQUAL_STRING("{\"lines\":[]}", json);
    free(json);
    pt_log_add(log, 7, "a");
    json = pt_log_json(log, 0);
    TEST_ASSERT_EQUAL_STRING("{\"lines\":[{\"seq\":1,\"t\":7,\"text\":\"a\"}]}", json);
    free(json);
    pt_log_free(log);
}

static void test_log_rejects_bad_sizes(void)
{
    TEST_ASSERT_NULL(pt_log_new(0, 10));
    TEST_ASSERT_NULL(pt_log_new(10, 0));
}

static int lock_depth;
static int lock_calls;
static int unlock_calls;

static void count_lock(void *ctx)
{
    TEST_ASSERT_EQUAL_PTR(&lock_calls, ctx);
    TEST_ASSERT_EQUAL_INT(0, lock_depth);
    lock_depth++;
    lock_calls++;
}

static void count_unlock(void *ctx)
{
    TEST_ASSERT_EQUAL_PTR(&lock_calls, ctx);
    TEST_ASSERT_EQUAL_INT(1, lock_depth);
    lock_depth--;
    unlock_calls++;
}

static void test_log_lock_wraps_add_and_json(void)
{
    pt_log_t *log = pt_log_new(4, 32);
    pt_log_set_lock(log, count_lock, count_unlock, &lock_calls);
    pt_log_add(log, 1, "one %d", 1);
    TEST_ASSERT_EQUAL_INT(1, lock_calls);
    TEST_ASSERT_EQUAL_INT(1, unlock_calls);
    cJSON *root = lines_of(pt_log_json(log, 0));
    TEST_ASSERT_EQUAL_STRING("one 1", text_at(root, 0));
    cJSON_Delete(root);
    TEST_ASSERT_EQUAL_INT(2, lock_calls);
    TEST_ASSERT_EQUAL_INT(2, unlock_calls);
    TEST_ASSERT_EQUAL_INT(0, lock_depth);
    pt_log_set_lock(log, NULL, NULL, NULL);
    pt_log_add(log, 2, "two");
    TEST_ASSERT_EQUAL_INT(2, lock_calls);
    pt_log_free(log);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_log_keeps_last_lines);
    RUN_TEST(test_log_truncates_long_line_on_utf8_boundary);
    RUN_TEST(test_log_vadd_formats_and_cuts_on_utf8_boundary);
    RUN_TEST(test_log_line_that_fits_exactly_is_kept);
    RUN_TEST(test_log_json_after);
    RUN_TEST(test_log_escapes_text);
    RUN_TEST(test_log_escapes_controls_cyrillic_and_invalid_bytes);
    RUN_TEST(test_log_full_log_parses_back);
    RUN_TEST(test_log_empty_json_shape);
    RUN_TEST(test_log_rejects_bad_sizes);
    RUN_TEST(test_log_lock_wraps_add_and_json);
    return UNITY_END();
}
