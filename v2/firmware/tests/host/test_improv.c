#include <string.h>

#include "pt_improv_proto.h"
#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static const uint8_t WIFI_FRAME[] = {
    'I', 'M', 'P', 'R', 'O', 'V', 0x01, 0x03, 0x20,
    0x01, 0x1E,
    0x0C, 'M', 'y', 'W', 'i', 'r', 'e', 'l', 'e', 's', 's', 'A', 'P',
    0x10, 'm', 'y', 's', 'e', 'c', 'u', 'r', 'e', 'p', 'a', 's', 's', 'w', 'o', 'r', 'd',
    0xC1};

static const uint8_t URL_RESULT_FRAME[] = {
    'I', 'M', 'P', 'R', 'O', 'V', 0x01, 0x04, 0x23,
    0x01, 0x21, 0x20,
    'h', 't', 't', 'p', ':', '/', '/', 'p', 'y', 't', 'a', 'i', 'c', 'h', 'e', 'n', 'k', 'o', '-',
    '0', 'a', '1', 'b', '2', 'c', '.', 'l', 'o', 'c', 'a', 'l', '/',
    0x8C};

static uint8_t sum_of(const uint8_t *b, size_t n)
{
    uint8_t s = 0;
    for (size_t i = 0; i < n; i++) {
        s = (uint8_t)(s + b[i]);
    }
    return s;
}

static size_t rpc_frame(uint8_t command, const uint8_t *payload, size_t payload_len, uint8_t *out)
{
    uint8_t data[260];
    data[0] = command;
    data[1] = (uint8_t)payload_len;
    if (payload_len > 0) {
        memcpy(data + 2, payload, payload_len);
    }
    return pt_improv_build(PT_IMPROV_TYPE_RPC, data, payload_len + 2, out, PT_IMPROV_FRAME_MAX);
}

static void test_improv_parse_wifi_settings(void)
{
    pt_improv_cmd_t cmd;
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_improv_parse(WIFI_FRAME, sizeof(WIFI_FRAME), &cmd));
    TEST_ASSERT_EQUAL_UINT8(PT_IMPROV_TYPE_RPC, cmd.type);
    TEST_ASSERT_EQUAL_UINT8(PT_IMPROV_CMD_WIFI_SETTINGS, cmd.command);
    TEST_ASSERT_EQUAL_STRING("MyWirelessAP", cmd.ssid);
    TEST_ASSERT_EQUAL_STRING("mysecurepassword", cmd.password);
}

static void test_improv_bad_checksum_rejected(void)
{
    uint8_t frame[sizeof(WIFI_FRAME)];
    memcpy(frame, WIFI_FRAME, sizeof(frame));
    frame[sizeof(frame) - 1] = 0xC2;
    pt_improv_cmd_t cmd;
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_improv_parse(frame, sizeof(frame), &cmd));
    memcpy(frame, WIFI_FRAME, sizeof(frame));
    frame[14] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_improv_parse(frame, sizeof(frame), &cmd));
}

static void test_improv_build_rpc_result_url(void)
{
    const char *url = "http://pytaichenko-0a1b2c.local/";
    uint8_t data[64];
    size_t n = pt_improv_result(PT_IMPROV_CMD_WIFI_SETTINGS, &url, 1, data, sizeof(data));
    TEST_ASSERT_EQUAL_size_t(35, n);
    uint8_t frame[PT_IMPROV_FRAME_MAX];
    size_t m = pt_improv_build(PT_IMPROV_TYPE_RESULT, data, n, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_size_t(sizeof(URL_RESULT_FRAME), m);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(URL_RESULT_FRAME, frame, m);
}

static void test_improv_build_state_frame(void)
{
    uint8_t state = PT_IMPROV_STATE_PROVISIONED;
    uint8_t frame[16];
    size_t n = pt_improv_build(PT_IMPROV_TYPE_STATE, &state, 1, frame, sizeof(frame));
    const uint8_t expected[] = {'I', 'M', 'P', 'R', 'O', 'V', 0x01, 0x01, 0x01, 0x04, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, frame, n - 1);
    TEST_ASSERT_EQUAL_HEX8(sum_of(frame, n - 1), frame[n - 1]);
}

static void test_improv_build_rejects_small_buffer(void)
{
    uint8_t data[4] = {1, 2, 3, 4};
    uint8_t frame[14];
    TEST_ASSERT_EQUAL_size_t(0, pt_improv_build(PT_IMPROV_TYPE_STATE, data, 4, frame, 13));
    TEST_ASSERT_EQUAL_size_t(14, pt_improv_build(PT_IMPROV_TYPE_STATE, data, 4, frame, 14));
    TEST_ASSERT_EQUAL_size_t(0, pt_improv_build(PT_IMPROV_TYPE_STATE, data, 4, NULL, 14));
    uint8_t big[PT_IMPROV_FRAME_MAX + 8];
    uint8_t payload[256] = {0};
    TEST_ASSERT_EQUAL_size_t(0, pt_improv_build(PT_IMPROV_TYPE_STATE, payload, 256, big, sizeof(big)));
}

static void test_improv_result_layout(void)
{
    const char *info[] = {PT_IMPROV_FIRMWARE_NAME, "2.0.0", "ESP32-S3", "pytaichenko-0a1b2c"};
    uint8_t data[128];
    size_t n = pt_improv_result(PT_IMPROV_CMD_GET_INFO, info, 4, data, sizeof(data));
    TEST_ASSERT_EQUAL_STRING("Pytaichenko.Speaker", PT_IMPROV_FIRMWARE_NAME);
    TEST_ASSERT_EQUAL_size_t(2 + 4 + 19 + 5 + 8 + 18, n);
    TEST_ASSERT_EQUAL_UINT8(PT_IMPROV_CMD_GET_INFO, data[0]);
    TEST_ASSERT_EQUAL_UINT8(n - 2, data[1]);
    TEST_ASSERT_EQUAL_UINT8(19, data[2]);
    TEST_ASSERT_EQUAL_MEMORY("Pytaichenko.Speaker", data + 3, 19);
    TEST_ASSERT_EQUAL_UINT8(5, data[22]);
    TEST_ASSERT_EQUAL_MEMORY("2.0.0", data + 23, 5);
}

static void test_improv_empty_result(void)
{
    uint8_t data[4];
    size_t n = pt_improv_result(PT_IMPROV_CMD_GET_NETWORKS, NULL, 0, data, sizeof(data));
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_UINT8(PT_IMPROV_CMD_GET_NETWORKS, data[0]);
    TEST_ASSERT_EQUAL_UINT8(0, data[1]);
}

static void test_improv_result_rejects_overflow(void)
{
    char long_text[300];
    memset(long_text, 'a', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';
    const char *one[] = {long_text};
    uint8_t data[600];
    TEST_ASSERT_EQUAL_size_t(0, pt_improv_result(1, one, 1, data, sizeof(data)));
    long_text[200] = '\0';
    const char *two[] = {long_text, long_text};
    TEST_ASSERT_EQUAL_size_t(0, pt_improv_result(1, two, 2, data, sizeof(data)));
    const char *url = "http://x.local/";
    TEST_ASSERT_EQUAL_size_t(0, pt_improv_result(1, &url, 1, data, 17));
    TEST_ASSERT_EQUAL_size_t(18, pt_improv_result(1, &url, 1, data, 18));
}

static void test_improv_parse_command_without_data(void)
{
    uint8_t frame[PT_IMPROV_FRAME_MAX];
    size_t n = rpc_frame(PT_IMPROV_CMD_GET_STATE, NULL, 0, frame);
    TEST_ASSERT_EQUAL_size_t(12, n);
    pt_improv_cmd_t cmd;
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_improv_parse(frame, n, &cmd));
    TEST_ASSERT_EQUAL_UINT8(PT_IMPROV_TYPE_RPC, cmd.type);
    TEST_ASSERT_EQUAL_UINT8(PT_IMPROV_CMD_GET_STATE, cmd.command);
    TEST_ASSERT_EQUAL_STRING("", cmd.ssid);
    TEST_ASSERT_EQUAL_STRING("", cmd.password);
}

static void test_improv_parse_unknown_command_is_reported(void)
{
    uint8_t frame[PT_IMPROV_FRAME_MAX];
    size_t n = rpc_frame(0x42, NULL, 0, frame);
    pt_improv_cmd_t cmd;
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_improv_parse(frame, n, &cmd));
    TEST_ASSERT_EQUAL_UINT8(0x42, cmd.command);
}

static void test_improv_parse_rejects_bad_frames(void)
{
    pt_improv_cmd_t cmd;
    uint8_t frame[sizeof(WIFI_FRAME)];
    memcpy(frame, WIFI_FRAME, sizeof(frame));
    frame[0] = 'X';
    frame[sizeof(frame) - 1] = sum_of(frame, sizeof(frame) - 1);
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_improv_parse(frame, sizeof(frame), &cmd));
    memcpy(frame, WIFI_FRAME, sizeof(frame));
    frame[6] = 2;
    frame[sizeof(frame) - 1] = sum_of(frame, sizeof(frame) - 1);
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_improv_parse(frame, sizeof(frame), &cmd));
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_improv_parse(WIFI_FRAME, sizeof(WIFI_FRAME) - 1, &cmd));
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_improv_parse(WIFI_FRAME, 5, &cmd));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_improv_parse(NULL, 0, &cmd));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_improv_parse(WIFI_FRAME, sizeof(WIFI_FRAME), NULL));
}

static void test_improv_parse_rejects_bad_rpc_lengths(void)
{
    uint8_t frame[PT_IMPROV_FRAME_MAX];
    pt_improv_cmd_t cmd;
    uint8_t data[8] = {PT_IMPROV_CMD_GET_STATE, 5, 0, 0};
    size_t n = pt_improv_build(PT_IMPROV_TYPE_RPC, data, 4, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_improv_parse(frame, n, &cmd));
    uint8_t one = PT_IMPROV_CMD_GET_STATE;
    n = pt_improv_build(PT_IMPROV_TYPE_RPC, &one, 1, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_improv_parse(frame, n, &cmd));
    const uint8_t ssid_overrun[] = {0x05, 'a', 'b'};
    n = rpc_frame(PT_IMPROV_CMD_WIFI_SETTINGS, ssid_overrun, sizeof(ssid_overrun), frame);
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_improv_parse(frame, n, &cmd));
    const uint8_t no_password_len[] = {0x02, 'a', 'b'};
    n = rpc_frame(PT_IMPROV_CMD_WIFI_SETTINGS, no_password_len, sizeof(no_password_len), frame);
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_improv_parse(frame, n, &cmd));
    const uint8_t password_overrun[] = {0x01, 'a', 0x04, 'x', 'y'};
    n = rpc_frame(PT_IMPROV_CMD_WIFI_SETTINGS, password_overrun, sizeof(password_overrun), frame);
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_improv_parse(frame, n, &cmd));
    const uint8_t empty_ssid[] = {0x00, 0x00};
    n = rpc_frame(PT_IMPROV_CMD_WIFI_SETTINGS, empty_ssid, sizeof(empty_ssid), frame);
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_improv_parse(frame, n, &cmd));
    const uint8_t nul_in_ssid[] = {0x02, 'a', 0x00, 0x00};
    n = rpc_frame(PT_IMPROV_CMD_WIFI_SETTINGS, nul_in_ssid, sizeof(nul_in_ssid), frame);
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_improv_parse(frame, n, &cmd));
}

static void test_improv_parse_wifi_limits(void)
{
    uint8_t payload[1 + 33 + 1 + 65];
    uint8_t frame[PT_IMPROV_FRAME_MAX];
    pt_improv_cmd_t cmd;
    payload[0] = 32;
    memset(payload + 1, 's', 32);
    payload[33] = 64;
    memset(payload + 34, 'p', 64);
    size_t n = rpc_frame(PT_IMPROV_CMD_WIFI_SETTINGS, payload, 98, frame);
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_improv_parse(frame, n, &cmd));
    TEST_ASSERT_EQUAL_size_t(32, strlen(cmd.ssid));
    TEST_ASSERT_EQUAL_size_t(64, strlen(cmd.password));
    payload[0] = 33;
    memset(payload + 1, 's', 33);
    payload[34] = 0;
    n = rpc_frame(PT_IMPROV_CMD_WIFI_SETTINGS, payload, 35, frame);
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_improv_parse(frame, n, &cmd));
    payload[0] = 1;
    payload[2] = 65;
    memset(payload + 3, 'p', 65);
    n = rpc_frame(PT_IMPROV_CMD_WIFI_SETTINGS, payload, 68, frame);
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_improv_parse(frame, n, &cmd));
    const uint8_t open_network[] = {0x04, 'h', 'o', 'm', 'e', 0x00};
    n = rpc_frame(PT_IMPROV_CMD_WIFI_SETTINGS, open_network, sizeof(open_network), frame);
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_improv_parse(frame, n, &cmd));
    TEST_ASSERT_EQUAL_STRING("home", cmd.ssid);
    TEST_ASSERT_EQUAL_STRING("", cmd.password);
}

static void test_improv_parse_other_type(void)
{
    uint8_t state = PT_IMPROV_STATE_READY;
    uint8_t frame[16];
    size_t n = pt_improv_build(PT_IMPROV_TYPE_STATE, &state, 1, frame, sizeof(frame));
    pt_improv_cmd_t cmd;
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_improv_parse(frame, n, &cmd));
    TEST_ASSERT_EQUAL_UINT8(PT_IMPROV_TYPE_STATE, cmd.type);
}

static void test_improv_wipe_clears_password(void)
{
    pt_improv_cmd_t cmd;
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_improv_parse(WIFI_FRAME, sizeof(WIFI_FRAME), &cmd));
    pt_improv_wipe(&cmd);
    pt_improv_cmd_t zero;
    memset(&zero, 0, sizeof(zero));
    TEST_ASSERT_EQUAL_MEMORY(&zero, &cmd, sizeof(cmd));
}

static size_t feed_all(pt_improv_rx_t *rx, const uint8_t *bytes, size_t n, size_t *done_at)
{
    size_t frames = 0;
    for (size_t i = 0; i < n; i++) {
        if (pt_improv_feed(rx, bytes[i], 0)) {
            frames++;
            if (done_at != NULL) {
                *done_at = i;
            }
        }
    }
    return frames;
}

static void test_improv_feed_finds_frame_among_console_text(void)
{
    uint8_t stream[200];
    const char *noise = "I (123) wifi: IMPRO started\r\nIMP";
    size_t pos = strlen(noise);
    memcpy(stream, noise, pos);
    memcpy(stream + pos, WIFI_FRAME, sizeof(WIFI_FRAME));
    size_t end = pos + sizeof(WIFI_FRAME);
    stream[end] = '\n';
    pt_improv_rx_t rx = {0};
    size_t done_at = 0;
    TEST_ASSERT_EQUAL_size_t(1, feed_all(&rx, stream, end + 1, &done_at));
    TEST_ASSERT_EQUAL_size_t(end - 1, done_at);
}

static void test_improv_feed_returns_whole_frame(void)
{
    pt_improv_rx_t rx = {0};
    bool done = false;
    for (size_t i = 0; i < sizeof(WIFI_FRAME); i++) {
        done = pt_improv_feed(&rx, WIFI_FRAME[i], 0);
    }
    TEST_ASSERT_TRUE(done);
    TEST_ASSERT_EQUAL_size_t(sizeof(WIFI_FRAME), rx.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WIFI_FRAME, rx.buf, rx.len);
}

static void test_improv_feed_back_to_back_frames(void)
{
    uint8_t stream[2 * sizeof(WIFI_FRAME) + 2];
    memcpy(stream, WIFI_FRAME, sizeof(WIFI_FRAME));
    stream[sizeof(WIFI_FRAME)] = '\n';
    memcpy(stream + sizeof(WIFI_FRAME) + 1, WIFI_FRAME, sizeof(WIFI_FRAME));
    stream[sizeof(stream) - 1] = '\n';
    pt_improv_rx_t rx = {0};
    TEST_ASSERT_EQUAL_size_t(2, feed_all(&rx, stream, sizeof(stream), NULL));
    uint8_t direct[2 * sizeof(WIFI_FRAME)];
    memcpy(direct, WIFI_FRAME, sizeof(WIFI_FRAME));
    memcpy(direct + sizeof(WIFI_FRAME), WIFI_FRAME, sizeof(WIFI_FRAME));
    memset(&rx, 0, sizeof(rx));
    TEST_ASSERT_EQUAL_size_t(2, feed_all(&rx, direct, sizeof(direct), NULL));
}

static void test_improv_feed_skips_wrong_version(void)
{
    uint8_t stream[sizeof(WIFI_FRAME) + 8];
    const uint8_t bad[] = {'I', 'M', 'P', 'R', 'O', 'V', 0x07, 'x'};
    memcpy(stream, bad, sizeof(bad));
    memcpy(stream + sizeof(bad), WIFI_FRAME, sizeof(WIFI_FRAME));
    pt_improv_rx_t rx = {0};
    TEST_ASSERT_EQUAL_size_t(1, feed_all(&rx, stream, sizeof(stream), NULL));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WIFI_FRAME, rx.buf, sizeof(WIFI_FRAME));
}

static void test_improv_partial_frame_dropped_after_100ms(void)
{
    pt_improv_rx_t rx = {0};
    size_t half = sizeof(WIFI_FRAME) / 2;
    uint32_t t = 1000;
    for (size_t i = 0; i < half; i++) {
        TEST_ASSERT_FALSE(pt_improv_feed(&rx, WIFI_FRAME[i], t));
    }
    t += 101;
    bool done = false;
    for (size_t i = half; i < sizeof(WIFI_FRAME); i++) {
        done = pt_improv_feed(&rx, WIFI_FRAME[i], t);
    }
    TEST_ASSERT_FALSE(done);

    memset(&rx, 0, sizeof(rx));
    t = 5000;
    for (size_t i = 0; i < half; i++) {
        pt_improv_feed(&rx, WIFI_FRAME[i], t);
    }
    t += 100;
    for (size_t i = half; i < sizeof(WIFI_FRAME); i++) {
        done = pt_improv_feed(&rx, WIFI_FRAME[i], t);
    }
    TEST_ASSERT_TRUE(done);

    memset(&rx, 0, sizeof(rx));
    t = 0xFFFFFFC0u;
    for (size_t i = 0; i < half; i++) {
        pt_improv_feed(&rx, WIFI_FRAME[i], t);
    }
    t += 90;
    for (size_t i = half; i < sizeof(WIFI_FRAME); i++) {
        done = pt_improv_feed(&rx, WIFI_FRAME[i], t);
    }
    TEST_ASSERT_TRUE(done);
}

static void test_improv_stale_bytes_do_not_join_next_frame(void)
{
    pt_improv_rx_t rx = {0};
    uint32_t t = 100;
    for (size_t i = 0; i < 12; i++) {
        pt_improv_feed(&rx, WIFI_FRAME[i], t);
    }
    t += 500;
    bool done = false;
    for (size_t i = 0; i < sizeof(WIFI_FRAME); i++) {
        done = pt_improv_feed(&rx, WIFI_FRAME[i], t);
    }
    TEST_ASSERT_TRUE(done);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WIFI_FRAME, rx.buf, sizeof(WIFI_FRAME));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_improv_parse_wifi_settings);
    RUN_TEST(test_improv_bad_checksum_rejected);
    RUN_TEST(test_improv_build_rpc_result_url);
    RUN_TEST(test_improv_build_state_frame);
    RUN_TEST(test_improv_build_rejects_small_buffer);
    RUN_TEST(test_improv_result_layout);
    RUN_TEST(test_improv_empty_result);
    RUN_TEST(test_improv_result_rejects_overflow);
    RUN_TEST(test_improv_parse_command_without_data);
    RUN_TEST(test_improv_parse_unknown_command_is_reported);
    RUN_TEST(test_improv_parse_rejects_bad_frames);
    RUN_TEST(test_improv_parse_rejects_bad_rpc_lengths);
    RUN_TEST(test_improv_parse_wifi_limits);
    RUN_TEST(test_improv_parse_other_type);
    RUN_TEST(test_improv_wipe_clears_password);
    RUN_TEST(test_improv_feed_finds_frame_among_console_text);
    RUN_TEST(test_improv_feed_returns_whole_frame);
    RUN_TEST(test_improv_feed_back_to_back_frames);
    RUN_TEST(test_improv_feed_skips_wrong_version);
    RUN_TEST(test_improv_partial_frame_dropped_after_100ms);
    RUN_TEST(test_improv_stale_bytes_do_not_join_next_frame);
    return UNITY_END();
}
