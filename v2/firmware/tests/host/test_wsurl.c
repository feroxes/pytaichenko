#include "unity.h"

#include <string.h>

#include "pt_wsurl.h"

void setUp(void) {}
void tearDown(void) {}

static void test_ws_url_encodes_model(void)
{
    char url[160];
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_ws_url("gpt-realtime-2.1-mini", url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING("wss://api.openai.com/v1/realtime?model=gpt-realtime-2.1-mini", url);
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_ws_url("a b&c=d/é~_.-", url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING("wss://api.openai.com/v1/realtime?model=a%20b%26c%3Dd%2F%C3%A9~_.-", url);
}

static void test_ws_url_rejects_bad_input_and_small_buffers(void)
{
    char url[160];
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ws_url(NULL, url, sizeof(url)));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ws_url("", url, sizeof(url)));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ws_url("gpt", NULL, sizeof(url)));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ws_url("gpt", url, 0));
    const char *full = "wss://api.openai.com/v1/realtime?model=gpt";
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_ws_url("gpt", url, strlen(full) + 1));
    TEST_ASSERT_EQUAL_STRING(full, url);
    char small[64];
    memset(small, 'x', sizeof(small));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ws_url("gpt", small, strlen(full)));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ws_url("\xC3\xA9", small, strlen(full) + 3));
}

static void test_ws_status_401_maps_to_bad_key(void)
{
    TEST_ASSERT_EQUAL_INT(PT_WS_BAD_KEY, pt_ws_status_from_http(401));
    TEST_ASSERT_EQUAL_INT(PT_WS_BAD_KEY, pt_ws_status_from_http(403));
    TEST_ASSERT_EQUAL_INT(PT_WS_NO_NETWORK, pt_ws_status_from_http(0));
    TEST_ASSERT_EQUAL_INT(PT_WS_REFUSED, pt_ws_status_from_http(500));
    TEST_ASSERT_EQUAL_INT(PT_WS_REFUSED, pt_ws_status_from_http(502));
    TEST_ASSERT_EQUAL_INT(PT_WS_REFUSED, pt_ws_status_from_http(503));
    TEST_ASSERT_EQUAL_INT(PT_WS_REFUSED, pt_ws_status_from_http(599));
    TEST_ASSERT_EQUAL_INT(PT_WS_REFUSED, pt_ws_status_from_http(429));
    TEST_ASSERT_EQUAL_INT(PT_WS_REFUSED, pt_ws_status_from_http(404));
    TEST_ASSERT_EQUAL_INT(PT_WS_OK, pt_ws_status_from_http(101));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ws_url_encodes_model);
    RUN_TEST(test_ws_url_rejects_bad_input_and_small_buffers);
    RUN_TEST(test_ws_status_401_maps_to_bad_key);
    return UNITY_END();
}
