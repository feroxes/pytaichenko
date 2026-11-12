#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "unity.h"
#include "pt_api.h"

#define PASSWORD "correct horse battery"
#define NEW_PASSWORD "staple of the kitchen"
#define OPENAI_KEY "sk-MARKER-openai-0123456789abcdef"
#define DEVICE "pytaichenko-0d47e8"
#define COOKIE_NAME "pytaichenko_session"
#define FAILED_DELAY_MS 1000u

static pt_settings_t S;
static pt_log_t *L;
static pt_api_t *A;
static char base_hash[PT_PASSWORD_HASH_MAX];
static uint8_t counter;

static int save_calls;
static int save_fail;
static pt_settings_t saved;
static int apply_calls;
static pt_settings_t applied_old;
static pt_settings_t applied_now;
static int volume_calls;
static int sound_test_calls;
static float volume_value;
static uint32_t now_value;
static int sleep_calls;
static uint32_t sleep_ms_seen[16];
static int sleep_depth;
static int nested_status[8];
static int nested_count;
static int nested_burst;
static int probe_depth;
static int probing;
static int probe_right_status;
static int probe_wrong_status;

static int openai_calls;
static pt_ws_status_t openai_result;
static int openai_busy;
static int openai_nested;
static int openai_nested_status;
static char openai_seen_key[256];

static pt_http_req_t R;
static char body_buf[40000];

static void fake_random(void *ctx, uint8_t *buf, size_t len)
{
    (void)ctx;
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(counter++ * 7 + 3);
    }
}

static pt_err_t fake_save(void *ctx, const pt_settings_t *s)
{
    (void)ctx;
    save_calls++;
    if (save_fail) {
        return PT_ERR_INVALID;
    }
    memcpy(&saved, s, sizeof(saved));
    return PT_OK;
}

static void fake_apply(void *ctx, const pt_settings_t *old, const pt_settings_t *now)
{
    (void)ctx;
    apply_calls++;
    memcpy(&applied_old, old, sizeof(applied_old));
    memcpy(&applied_now, now, sizeof(applied_now));
}

static void fake_set_volume(void *ctx, float v)
{
    (void)ctx;
    volume_calls++;
    volume_value = v;
}

static void fake_sound_test(void *ctx)
{
    (void)ctx;
    sound_test_calls++;
}

static uint32_t fake_now(void *ctx)
{
    (void)ctx;
    return now_value;
}

static void fake_device_info(void *ctx, char *ip, size_t ip_len, int *rssi)
{
    (void)ctx;
    snprintf(ip, ip_len, "192.0.2.40");
    *rssi = -61;
}

static int health_calls;

static void fake_device_health(void *ctx, pt_device_health_t *out)
{
    (void)ctx;
    health_calls++;
    snprintf(out->mic_mode, sizeof(out->mic_mode), "two-mic bss");
    out->internal = (pt_heap_area_t){120000, 90000, 64000};
    out->psram = (pt_heap_area_t){7000000, 6500000, 6000000};
}

static bool log_contains(const char *text)
{
    char *json = pt_log_json(L, 0);
    cJSON *root = cJSON_Parse(json);
    free(json);
    bool found = false;
    const cJSON *line;
    cJSON_ArrayForEach(line, cJSON_GetObjectItemCaseSensitive(root, "lines"))
    {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(line, "text");
        if (cJSON_IsString(t) && strcmp(t->valuestring, text) == 0) {
            found = true;
        }
    }
    cJSON_Delete(root);
    return found;
}

static void go(pt_http_resp_t *resp);

static char openai_error[PT_API_ERROR_MAX];

static pt_ws_status_t fake_openai_test(void *ctx, char *model, char *voice, char *error)
{
    snprintf(error, PT_API_ERROR_MAX, "%s", openai_error);
    (void)ctx;
    openai_calls++;
    snprintf(openai_seen_key, sizeof(openai_seen_key), "%s", S.openai.api_key);
    if (openai_nested) {
        pt_http_req_t saved_req = R;
        R.method = "POST";
        R.path = "/api/openai/test";
        R.body = "{}";
        R.body_len = 2;
        pt_http_resp_t inner;
        go(&inner);
        openai_nested_status = inner.status;
        pt_http_resp_free(&inner);
        R = saved_req;
    }
    if (openai_result == PT_WS_OK) {
        snprintf(model, PT_API_MODEL_MAX, "gpt-realtime-2.1-mini");
        snprintf(voice, PT_API_VOICE_MAX, "marin");
    }
    return openai_result;
}

static bool fake_turn_active(void *ctx)
{
    (void)ctx;
    return openai_busy != 0;
}

static int ota_check_calls;
static pt_err_t ota_check_result;
static char ota_check_version[24];
static int ota_install_calls;
static pt_err_t ota_install_result;
static char ota_install_url[256];
static char ota_install_version[24];
static int up_begin_calls;
static pt_err_t up_begin_result;
static int up_write_calls;
static pt_err_t up_write_result;
static int up_write_fail_at;
static size_t up_written;
static int up_finish_calls;
static pt_err_t up_finish_result;
static int up_abort_calls;

static pt_err_t fake_ota_check(void *ctx, pt_release_t *latest)
{
    (void)ctx;
    ota_check_calls++;
    if (ota_check_result != PT_OK) {
        return ota_check_result;
    }
    snprintf(latest->version, sizeof(latest->version), "%s", ota_check_version);
    snprintf(latest->tag, sizeof(latest->tag), "v%s", ota_check_version);
    snprintf(latest->url, sizeof(latest->url), "https://github.com/feroxes/pytaichenko/releases/download/v%s/x.ota.bin",
             ota_check_version);
    return PT_OK;
}

static pt_err_t fake_ota_install(void *ctx, const char *url, const char *version)
{
    (void)ctx;
    ota_install_calls++;
    snprintf(ota_install_url, sizeof(ota_install_url), "%s", url);
    snprintf(ota_install_version, sizeof(ota_install_version), "%s", version);
    return ota_install_result;
}

static pt_err_t fake_up_begin(void *ctx)
{
    (void)ctx;
    up_begin_calls++;
    return up_begin_result;
}

static pt_err_t fake_up_write(void *ctx, const void *data, size_t n)
{
    (void)ctx;
    (void)data;
    up_write_calls++;
    if (up_write_fail_at > 0 && up_write_calls == up_write_fail_at) {
        return up_write_result;
    }
    up_written += n;
    return PT_OK;
}

static pt_err_t fake_up_finish(void *ctx)
{
    (void)ctx;
    up_finish_calls++;
    return up_finish_result;
}

static void fake_up_abort(void *ctx)
{
    (void)ctx;
    up_abort_calls++;
}

static void fake_sleep(void *ctx, uint32_t ms)
{
    (void)ctx;
    if (sleep_calls < 16) {
        sleep_ms_seen[sleep_calls] = ms;
    }
    sleep_calls++;
    if (nested_burst == 0 || probing) {
        return;
    }
    sleep_depth++;
    pt_http_req_t saved_req = R;
    if (probe_depth > 0 && sleep_depth == probe_depth) {
        probing = 1;
        R.client_ip = 0xC0A80199u;
        R.method = "POST";
        R.path = "/api/login";
        R.body = "{\"password\":\"" PASSWORD "\"}";
        R.body_len = strlen(R.body);
        pt_http_resp_t other;
        go(&other);
        probe_right_status = other.status;
        pt_http_resp_free(&other);
        R.body = "{\"password\":\"guess-other-ip\"}";
        R.body_len = strlen(R.body);
        go(&other);
        probe_wrong_status = other.status;
        pt_http_resp_free(&other);
        probing = 0;
        R = saved_req;
    }
    char saved_body[64];
    snprintf(saved_body, sizeof(saved_body), "{\"password\":\"guess-wrong-%d\"}", sleep_depth);
    R.method = "POST";
    R.path = "/api/login";
    R.body = saved_body;
    R.body_len = strlen(saved_body);
    pt_http_resp_t inner;
    go(&inner);
    if (nested_count < 8) {
        nested_status[nested_count++] = inner.status;
    }
    pt_http_resp_free(&inner);
    R = saved_req;
    sleep_depth--;
}

static void make_api(void)
{
    pt_api_env_t env = {
        .save = fake_save,
        .apply = fake_apply,
        .set_volume = fake_set_volume,
        .sound_test = fake_sound_test,
        .now_s = fake_now,
        .sleep_ms = fake_sleep,
        .device_info = fake_device_info,
        .device_health = fake_device_health,
        .random = fake_random,
        .openai_test = fake_openai_test,
        .turn_active = fake_turn_active,
        .ota_check = fake_ota_check,
        .ota_install = fake_ota_install,
        .ota_upload_begin = fake_up_begin,
        .ota_upload_write = fake_up_write,
        .ota_upload_finish = fake_up_finish,
        .ota_upload_abort = fake_up_abort,
        .ctx = NULL,
        .device_name = DEVICE,
    };
    A = pt_api_new(&S, L, &env);
    TEST_ASSERT_NOT_NULL(A);
}

static void fixture(bool with_password)
{
    pt_settings_defaults(&S, "uk");
    if (with_password) {
        snprintf(S.password_hash, sizeof(S.password_hash), "%s", base_hash);
    }
    L = pt_log_new(400, 200);
    TEST_ASSERT_NOT_NULL(L);
    make_api();
}

void setUp(void)
{
    counter = 0;
    health_calls = 0;
    save_calls = 0;
    save_fail = 0;
    apply_calls = 0;
    volume_calls = 0;
    sound_test_calls = 0;
    now_value = 1000;
    sleep_calls = 0;
    sleep_depth = 0;
    nested_count = 0;
    nested_burst = 0;
    probe_depth = 0;
    probing = 0;
    probe_right_status = 0;
    probe_wrong_status = 0;
    openai_calls = 0;
    openai_result = PT_WS_OK;
    openai_busy = 0;
    openai_error[0] = '\0';
    openai_nested = 0;
    openai_nested_status = 0;
    openai_seen_key[0] = '\0';
    ota_check_calls = 0;
    ota_check_result = PT_OK;
    snprintf(ota_check_version, sizeof(ota_check_version), "2.2.0");
    ota_install_calls = 0;
    ota_install_result = PT_OK;
    ota_install_url[0] = '\0';
    ota_install_version[0] = '\0';
    up_begin_calls = 0;
    up_begin_result = PT_OK;
    up_write_calls = 0;
    up_write_result = PT_OK;
    up_write_fail_at = 0;
    up_written = 0;
    up_finish_calls = 0;
    up_finish_result = PT_OK;
    up_abort_calls = 0;
    memset(&R, 0, sizeof(R));
    R.host = "192.168.1.50";
    R.client_ip = 0xC0A80132u;
    A = NULL;
    L = NULL;
}

void tearDown(void)
{
    pt_api_free(A);
    pt_log_free(L);
}

static void go(pt_http_resp_t *resp)
{
    pt_api_handle(A, &R, resp);
}

static void request(const char *method, const char *path, const char *body, pt_http_resp_t *resp)
{
    R.method = method;
    R.path = path;
    R.content_type = strcmp(method, "GET") == 0 ? NULL : "application/json";
    R.body = body;
    R.body_len = body != NULL ? strlen(body) : 0;
    go(resp);
}

static cJSON *parse_body(const pt_http_resp_t *resp)
{
    TEST_ASSERT_NOT_NULL(resp->body);
    cJSON *j = cJSON_Parse(resp->body);
    TEST_ASSERT_NOT_NULL_MESSAGE(j, resp->body);
    return j;
}

static void expect_error(const pt_http_resp_t *resp, int status, const char *code)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(status, resp->status, resp->body);
    cJSON *j = parse_body(resp);
    cJSON *e = cJSON_GetObjectItem(j, "error");
    TEST_ASSERT_TRUE(cJSON_IsString(e));
    TEST_ASSERT_EQUAL_STRING(code, e->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsString(cJSON_GetObjectItem(j, "message")));
    cJSON_Delete(j);
}

static void expect_error_free(pt_http_resp_t *resp, int status, const char *code)
{
    expect_error(resp, status, code);
    pt_http_resp_free(resp);
}

static bool bool_field(const pt_http_resp_t *resp, const char *name)
{
    cJSON *j = parse_body(resp);
    cJSON *v = cJSON_GetObjectItem(j, name);
    TEST_ASSERT_TRUE(cJSON_IsBool(v));
    bool out = cJSON_IsTrue(v);
    cJSON_Delete(j);
    return out;
}

static void cookie_of(const pt_http_resp_t *resp, char *out, size_t n)
{
    const char *semi = strchr(resp->set_cookie, ';');
    TEST_ASSERT_NOT_NULL(semi);
    TEST_ASSERT_TRUE((size_t)(semi - resp->set_cookie) < n);
    memcpy(out, resp->set_cookie, (size_t)(semi - resp->set_cookie));
    out[semi - resp->set_cookie] = '\0';
}

static void login(char *cookie, size_t n)
{
    pt_http_resp_t r;
    request("POST", "/api/login", "{\"password\":\"" PASSWORD "\"}", &r);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, r.status, r.body);
    cookie_of(&r, cookie, n);
    pt_http_resp_free(&r);
}

static void test_state_before_setup(void)
{
    fixture(false);
    pt_http_resp_t r;
    request("GET", "/api/state", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    TEST_ASSERT_FALSE(bool_field(&r, "password_set"));
    TEST_ASSERT_FALSE(bool_field(&r, "logged_in"));
    TEST_ASSERT_EQUAL_STRING("", r.set_cookie);
    pt_http_resp_free(&r);
}

static void test_state_tells_which_screen_to_show(void)
{
    fixture(true);
    pt_http_resp_t r;
    request("GET", "/api/state", NULL, &r);
    TEST_ASSERT_TRUE(bool_field(&r, "password_set"));
    TEST_ASSERT_FALSE(bool_field(&r, "logged_in"));
    pt_http_resp_free(&r);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    request("GET", "/api/state", NULL, &r);
    TEST_ASSERT_TRUE(bool_field(&r, "password_set"));
    TEST_ASSERT_TRUE(bool_field(&r, "logged_in"));
    pt_http_resp_free(&r);
}

static void test_setup_sets_password_and_language(void)
{
    fixture(false);
    pt_http_resp_t r;
    request("POST", "/api/setup", "{\"password\":\"" PASSWORD "\",\"language\":\"en\"}", &r);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, r.status, r.body);
    TEST_ASSERT_TRUE(bool_field(&r, "password_set"));
    TEST_ASSERT_TRUE(bool_field(&r, "logged_in"));
    TEST_ASSERT_EQUAL_STRING_LEN(COOKIE_NAME "=", r.set_cookie, strlen(COOKIE_NAME "="));
    TEST_ASSERT_NOT_NULL(strstr(r.set_cookie, "; HttpOnly; SameSite=Strict; Path=/; Max-Age=2592000"));
    char cookie[80];
    cookie_of(&r, cookie, sizeof(cookie));
    TEST_ASSERT_TRUE(strlen(cookie) >= strlen(COOKIE_NAME "=") + 40);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(1, save_calls);
    TEST_ASSERT_EQUAL_INT(1, apply_calls);
    TEST_ASSERT_EQUAL_STRING("en", saved.assistant.language);
    TEST_ASSERT_EQUAL_STRING("en", S.assistant.language);
    TEST_ASSERT_EQUAL_STRING(pt_default_instructions("en"), S.assistant.instructions);
    TEST_ASSERT_EQUAL_STRING_LEN("pbkdf2-sha256$", S.password_hash, 14);
    TEST_ASSERT_EQUAL_STRING(S.password_hash, saved.password_hash);
    TEST_ASSERT_EQUAL_STRING("", applied_old.password_hash);
    TEST_ASSERT_TRUE(pt_password_verify(PASSWORD, S.password_hash));
    R.cookie = cookie;
    request("GET", "/api/settings", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
}

static void test_setup_without_language_keeps_default(void)
{
    fixture(false);
    pt_http_resp_t r;
    request("POST", "/api/setup", "{\"password\":\"\xD0\xBF\xD0\xB0\xD1\x80\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB8\xD0\xBA\"}", &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_STRING("uk", S.assistant.language);
    TEST_ASSERT_TRUE(pt_password_verify("\xD0\xBF\xD0\xB0\xD1\x80\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB8\xD0\xBA", S.password_hash));
}

static void test_setup_twice_rejected(void)
{
    fixture(true);
    pt_http_resp_t r;
    request("POST", "/api/setup", "{\"password\":\"" NEW_PASSWORD "\"}", &r);
    expect_error_free(&r, 409, "password_set");
    TEST_ASSERT_EQUAL_INT(0, save_calls);
    TEST_ASSERT_EQUAL_STRING(base_hash, S.password_hash);
}

static void test_setup_checks_the_password_and_fields(void)
{
    static const struct {
        const char *body;
        int status;
        const char *code;
    } cases[] = {
        {"{\"password\":\"short\"}", 400, "bad_password"},
        {"{\"password\":\"\"}", 400, "bad_password"},
        {"{\"password\":12345678}", 400, "bad_password"},
        {"{\"password\":null}", 400, "bad_password"},
        {"{}", 400, "bad_password"},
        {"{\"password\":\"\\ud800\\ud800\\ud800\\ud800\\ud800\\ud800\\ud800\\ud800\"}", 400, "bad_json"},
        {"{\"password\":\"\xff\xff\xff\xff\xff\xff\xff\xff\"}", 400, "bad_password"},
        {"{\"password\":\"" PASSWORD "\",\"hint\":\"x\"}", 400, "invalid"},
        {"{\"password\":\"" PASSWORD "\",\"language\":\"ru\"}", 400, "invalid"},
        {"{\"password\":\"" PASSWORD "\",\"language\":null}", 400, "invalid"},
        {"{\"password\":\"" PASSWORD "\",\"language\":[\"en\"]}", 400, "invalid"},
        {"[]", 400, "bad_json"},
        {"{", 400, "bad_json"},
        {"", 400, "bad_json"},
    };
    fixture(false);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        pt_http_resp_t r;
        request("POST", "/api/setup", cases[i].body, &r);
        TEST_ASSERT_EQUAL_STRING("", r.set_cookie);
        expect_error(&r, cases[i].status, cases[i].code);
        pt_http_resp_free(&r);
    }
    TEST_ASSERT_EQUAL_INT(0, save_calls);
    TEST_ASSERT_EQUAL_STRING("", S.password_hash);
}

static void test_setup_password_length_limits(void)
{
    fixture(false);
    char body[700];
    char pw[400];
    pt_http_resp_t r;
    memset(pw, 'x', 257);
    pw[257] = '\0';
    snprintf(body, sizeof(body), "{\"password\":\"%s\"}", pw);
    request("POST", "/api/setup", body, &r);
    expect_error_free(&r, 400, "bad_password");
    memset(pw, 'x', 7);
    pw[7] = '\0';
    snprintf(body, sizeof(body), "{\"password\":\"%s\"}", pw);
    request("POST", "/api/setup", body, &r);
    expect_error_free(&r, 400, "bad_password");
    request("POST", "/api/setup", "{\"password\":\"\xD0\xBF\xD0\xB0\xD1\x80\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB8\"}", &r);
    expect_error_free(&r, 400, "bad_password");
    TEST_ASSERT_EQUAL_INT(0, save_calls);
}

static void test_setup_save_failure_keeps_no_password(void)
{
    fixture(false);
    save_fail = 1;
    pt_http_resp_t r;
    request("POST", "/api/setup", "{\"password\":\"" PASSWORD "\"}", &r);
    TEST_ASSERT_EQUAL_STRING("", r.set_cookie);
    expect_error_free(&r, 500, "save_failed");
    TEST_ASSERT_EQUAL_STRING("", S.password_hash);
    TEST_ASSERT_EQUAL_INT(0, apply_calls);
}

static void test_login_with_the_right_password(void)
{
    fixture(true);
    pt_http_resp_t r;
    request("POST", "/api/login", "{\"password\":\"" PASSWORD "\"}", &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    TEST_ASSERT_TRUE(bool_field(&r, "logged_in"));
    TEST_ASSERT_NOT_NULL(strstr(r.set_cookie, "; HttpOnly; SameSite=Strict; Path=/; Max-Age=2592000"));
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(0, sleep_calls);
}

static void test_login_wrong_password_401_with_delay(void)
{
    fixture(true);
    pt_http_resp_t r;
    request("POST", "/api/login", "{\"password\":\"guess-wrong-123\"}", &r);
    TEST_ASSERT_EQUAL_STRING("", r.set_cookie);
    expect_error_free(&r, 401, "wrong_password");
    TEST_ASSERT_EQUAL_INT(1, sleep_calls);
    TEST_ASSERT_EQUAL_UINT32(FAILED_DELAY_MS, sleep_ms_seen[0]);
    request("POST", "/api/login", "{\"password\":\"" PASSWORD "\"}", &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(1, sleep_calls);
    char *json = pt_log_json(L, 0);
    TEST_ASSERT_NOT_NULL(strstr(json, "wrong password from 192.168.1.50"));
    TEST_ASSERT_NULL(strstr(json, "guess-wrong-123"));
    free(json);
}

static void test_login_queue_full_returns_429(void)
{
    fixture(true);
    nested_burst = 1;
    pt_http_resp_t r;
    request("POST", "/api/login", "{\"password\":\"guess-wrong-0\"}", &r);
    expect_error_free(&r, 401, "wrong_password");
    nested_burst = 0;
    TEST_ASSERT_EQUAL_INT(4, nested_count);
    TEST_ASSERT_EQUAL_INT(429, nested_status[0]);
    TEST_ASSERT_EQUAL_INT(401, nested_status[1]);
    TEST_ASSERT_EQUAL_INT(401, nested_status[2]);
    TEST_ASSERT_EQUAL_INT(401, nested_status[3]);
    TEST_ASSERT_EQUAL_INT(4, sleep_calls);
    request("POST", "/api/login", "{\"password\":\"" PASSWORD "\"}", &r);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, r.status, "all slots released");
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(4, sleep_calls);
}

static void test_a_flood_from_one_address_does_not_hold_up_other_addresses(void)
{
    fixture(true);
    nested_burst = 1;
    probe_depth = 4;
    pt_http_resp_t r;
    request("POST", "/api/login", "{\"password\":\"guess-wrong-0\"}", &r);
    expect_error_free(&r, 401, "wrong_password");
    nested_burst = 0;
    TEST_ASSERT_EQUAL_INT(4, nested_count);
    TEST_ASSERT_EQUAL_INT(429, nested_status[0]);
    TEST_ASSERT_EQUAL_INT(200, probe_right_status);
    TEST_ASSERT_EQUAL_INT(401, probe_wrong_status);
}

static void test_login_without_password_set(void)
{
    fixture(false);
    pt_http_resp_t r;
    request("POST", "/api/login", "{\"password\":\"" PASSWORD "\"}", &r);
    expect_error_free(&r, 409, "no_password");
    TEST_ASSERT_EQUAL_INT(0, sleep_calls);
}

static void test_login_body_checks(void)
{
    fixture(true);
    pt_http_resp_t r;
    char body[700];
    char pw[400];
    memset(pw, 'x', 257);
    pw[257] = '\0';
    snprintf(body, sizeof(body), "{\"password\":\"%s\"}", pw);
    request("POST", "/api/login", body, &r);
    expect_error_free(&r, 400, "invalid");
    request("POST", "/api/login", "{\"password\":5}", &r);
    expect_error_free(&r, 400, "invalid");
    request("POST", "/api/login", "{\"password\":\"x\",\"other\":1}", &r);
    expect_error_free(&r, 400, "invalid");
    request("POST", "/api/login", "[1]", &r);
    expect_error_free(&r, 400, "bad_json");
    request("POST", "/api/login", NULL, &r);
    expect_error_free(&r, 400, "bad_json");
    TEST_ASSERT_EQUAL_INT(0, sleep_calls);
}

static void test_deeply_nested_json_is_refused(void)
{
    fixture(true);
    pt_http_resp_t r;
    size_t n = 5000;
    memset(body_buf, '[', n);
    memset(body_buf + n, ']', n);
    body_buf[2 * n] = '\0';
    request("POST", "/api/login", body_buf, &r);
    expect_error_free(&r, 400, "bad_json");
}

static void test_settings_requires_login(void)
{
    fixture(true);
    static const char *paths[] = {"/api/settings", "/api/volume", "/api/log", "/api/device", "/api/logout",
                                  "/api/password", "/api/nope"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        pt_http_resp_t r;
        request("GET", paths[i], NULL, &r);
        expect_error_free(&r, 401, "login_required");
        request("PUT", paths[i], "{}", &r);
        expect_error_free(&r, 401, "login_required");
    }
    R.cookie = COOKIE_NAME "=forged-token";
    pt_http_resp_t r;
    request("GET", "/api/settings", NULL, &r);
    expect_error_free(&r, 401, "login_required");
    R.cookie = "other=1; " COOKIE_NAME "x=2";
    request("GET", "/api/settings", NULL, &r);
    expect_error_free(&r, 401, "login_required");
    TEST_ASSERT_EQUAL_INT(0, save_calls);
}

static void test_cookie_is_found_among_others(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    char header[200];
    snprintf(header, sizeof(header), "a=1; %s; b=2", cookie);
    R.cookie = header;
    pt_http_resp_t r;
    request("GET", "/api/settings", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
}

static void test_get_settings_hides_key(void)
{
    fixture(true);
    snprintf(S.openai.api_key, sizeof(S.openai.api_key), "%s", OPENAI_KEY);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("GET", "/api/settings", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    TEST_ASSERT_NULL(strstr(r.body, OPENAI_KEY));
    TEST_ASSERT_NULL(strstr(r.body, "pbkdf2"));
    TEST_ASSERT_NULL(strstr(r.body, "password_hash"));
    cJSON *j = parse_body(&r);
    cJSON *s = cJSON_GetObjectItem(j, "settings");
    TEST_ASSERT_TRUE(cJSON_IsObject(s));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(s, "password_set")));
    TEST_ASSERT_TRUE(cJSON_IsObject(cJSON_GetObjectItem(s, "openai")));
    TEST_ASSERT_TRUE(cJSON_IsObject(cJSON_GetObjectItem(s, "sound")));
    cJSON_Delete(j);
    pt_http_resp_free(&r);
}

static void test_put_settings_saves_and_applies(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("PUT", "/api/settings",
            "{\"openai\":{\"model\":\"gpt-x\",\"api_key\":\"" OPENAI_KEY "\"},\"timing\":{\"first_turn_s\":9}}", &r);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, r.status, r.body);
    TEST_ASSERT_NULL(strstr(r.body, OPENAI_KEY));
    cJSON *j = parse_body(&r);
    cJSON *openai = cJSON_GetObjectItem(cJSON_GetObjectItem(j, "settings"), "openai");
    TEST_ASSERT_EQUAL_STRING("gpt-x", cJSON_GetObjectItem(openai, "model")->valuestring);
    cJSON_Delete(j);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(1, save_calls);
    TEST_ASSERT_EQUAL_STRING("gpt-x", saved.openai.model);
    TEST_ASSERT_EQUAL_STRING(OPENAI_KEY, saved.openai.api_key);
    TEST_ASSERT_EQUAL_STRING(base_hash, saved.password_hash);
    TEST_ASSERT_EQUAL_INT(1, apply_calls);
    TEST_ASSERT_EQUAL_STRING("gpt-x", applied_now.openai.model);
    TEST_ASSERT_EQUAL_STRING("gpt-realtime-2.1-mini", applied_old.openai.model);
    TEST_ASSERT_EQUAL_STRING("", applied_old.openai.api_key);
    TEST_ASSERT_EQUAL_STRING(OPENAI_KEY, applied_now.openai.api_key);
    TEST_ASSERT_EQUAL_FLOAT(9.0f, S.timing.first_turn_s);
    TEST_ASSERT_EQUAL_STRING("gpt-x", S.openai.model);
}

static void test_put_settings_bad_field_400_names_field(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("PUT", "/api/settings", "{\"openai\":{\"model\":\"gpt-x\"},\"timing\":{\"first_turn_s\":-1}}", &r);
    TEST_ASSERT_EQUAL_INT(400, r.status);
    cJSON *j = parse_body(&r);
    TEST_ASSERT_EQUAL_STRING("invalid", cJSON_GetObjectItem(j, "error")->valuestring);
    TEST_ASSERT_EQUAL_STRING_LEN("timing.first_turn_s", cJSON_GetObjectItem(j, "message")->valuestring, 19);
    cJSON_Delete(j);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(0, save_calls);
    TEST_ASSERT_EQUAL_INT(0, apply_calls);
    TEST_ASSERT_EQUAL_STRING("gpt-realtime-2.1-mini", S.openai.model);
    request("PUT", "/api/settings", "{\"password_hash\":\"x\"}", &r);
    TEST_ASSERT_EQUAL_INT(400, r.status);
    pt_http_resp_free(&r);
    request("PUT", "/api/settings", "{\"devices\":[]}", &r);
    expect_error_free(&r, 400, "invalid");
}

static void test_put_settings_bad_json(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("PUT", "/api/settings", "{\"openai\":", &r);
    expect_error_free(&r, 400, "bad_json");
    request("PUT", "/api/settings", "[]", &r);
    expect_error_free(&r, 400, "bad_json");
    request("PUT", "/api/settings", NULL, &r);
    expect_error_free(&r, 400, "bad_json");
    TEST_ASSERT_EQUAL_INT(0, save_calls);
}

static void test_save_failure_500_save_failed_keeps_old(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    save_fail = 1;
    pt_http_resp_t r;
    request("PUT", "/api/settings", "{\"openai\":{\"model\":\"gpt-x\"}}", &r);
    expect_error_free(&r, 500, "save_failed");
    TEST_ASSERT_EQUAL_INT(1, save_calls);
    TEST_ASSERT_EQUAL_INT(0, apply_calls);
    TEST_ASSERT_EQUAL_STRING("gpt-realtime-2.1-mini", S.openai.model);
    save_fail = 0;
    request("GET", "/api/settings", NULL, &r);
    TEST_ASSERT_NULL(strstr(r.body, "gpt-x"));
    pt_http_resp_free(&r);
}

static void test_foreign_origin_403(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    static const char *origins[] = {"http://evil.test", "http://192.168.1.50:8080",
                                    "null", "ftp://192.168.1.50", "http://192.168.1.50.evil.test", ""};
    for (size_t i = 0; i < sizeof(origins) / sizeof(origins[0]); i++) {
        R.origin = origins[i];
        pt_http_resp_t r;
        request("PUT", "/api/settings", "{\"openai\":{\"model\":\"gpt-x\"}}", &r);
        TEST_ASSERT_EQUAL_STRING("", r.set_cookie);
        expect_error_free(&r, 403, "foreign_origin");
    }
    R.origin = NULL;
    static const char *sites[] = {"cross-site", "same-site", ""};
    for (size_t i = 0; i < sizeof(sites) / sizeof(sites[0]); i++) {
        R.sec_fetch_site = sites[i];
        pt_http_resp_t r;
        request("GET", "/api/settings", NULL, &r);
        expect_error_free(&r, 403, "foreign_origin");
    }
    TEST_ASSERT_EQUAL_INT(0, save_calls);
    R.sec_fetch_site = NULL;
    R.origin = "HTTP://192.168.1.50";
    R.sec_fetch_site = "same-origin";
    pt_http_resp_t r;
    request("PUT", "/api/settings", "{\"openai\":{\"model\":\"gpt-x\"}}", &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    R.origin = NULL;
    R.sec_fetch_site = "none";
    request("GET", "/api/settings", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
}

static void test_foreign_origin_beats_public_routes(void)
{
    fixture(false);
    R.origin = "http://evil.test";
    pt_http_resp_t r;
    request("POST", "/api/setup", "{\"password\":\"" PASSWORD "\"}", &r);
    TEST_ASSERT_EQUAL_STRING("", r.set_cookie);
    expect_error_free(&r, 403, "foreign_origin");
    TEST_ASSERT_EQUAL_INT(0, save_calls);
}

static void test_foreign_host_403(void)
{
    fixture(false);
    static const char *hosts[] = {"evil.test", "evil.test:80", "192.168.1.5:8080", DEVICE ".local:8080",
                                  "192.168.1.5.nip.io", "", "[::1]", "999.1.1.1", "other.local", "1.2.3",
                                  "1.2.3.4.5", "192.168.1.5:", "192.168.1.5:080", "localhost", DEVICE ".local.evil.test",
                                  "a" DEVICE ".local", "192.168.1.5:80:80"};
    for (size_t i = 0; i < sizeof(hosts) / sizeof(hosts[0]); i++) {
        R.host = hosts[i];
        pt_http_resp_t r;
        request("GET", "/api/state", NULL, &r);
        TEST_ASSERT_EQUAL_STRING("", r.set_cookie);
        expect_error_free(&r, 403, "foreign_host");
    }
    R.host = NULL;
    pt_http_resp_t r;
    request("GET", "/api/state", NULL, &r);
    expect_error_free(&r, 403, "foreign_host");
    R.host = "evil.test";
    request("POST", "/api/setup", "{\"password\":\"" PASSWORD "\"}", &r);
    expect_error_free(&r, 403, "foreign_host");
    TEST_ASSERT_EQUAL_INT(0, save_calls);
}

static void test_local_hosts_accepted(void)
{
    fixture(false);
    static const char *hosts[] = {"192.168.1.5", "192.168.1.5:80", "10.0.0.1", "0.0.0.0", "255.255.255.255:80",
                                  DEVICE ".local", DEVICE ".local:80", "PYTAICHENKO-0D47E8.LOCAL",
                                  "Pytaichenko-0d47e8.Local:80"};
    for (size_t i = 0; i < sizeof(hosts) / sizeof(hosts[0]); i++) {
        R.host = hosts[i];
        pt_http_resp_t r;
        request("GET", "/api/state", NULL, &r);
        TEST_ASSERT_EQUAL_INT_MESSAGE(200, r.status, hosts[i]);
        pt_http_resp_free(&r);
    }
}

static void test_origin_matches_host_with_port_and_name(void)
{
    fixture(false);
    R.host = DEVICE ".local";
    R.origin = "http://" DEVICE ".local";
    pt_http_resp_t r;
    request("GET", "/api/state", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    R.host = "192.168.1.5:80";
    R.origin = "http://192.168.1.5:80";
    request("GET", "/api/state", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    R.origin = "http://192.168.1.5";
    request("GET", "/api/state", NULL, &r);
    expect_error_free(&r, 403, "foreign_origin");
}

static void test_post_without_json_415(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    static const char *types[] = {NULL, "text/plain", "application/x-www-form-urlencoded",
                                  "multipart/form-data; boundary=x", "application/jsonx", "text/json"};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        pt_http_resp_t r;
        R.method = "PUT";
        R.path = "/api/settings";
        R.content_type = types[i];
        R.body = "{\"openai\":{\"model\":\"gpt-x\"}}";
        R.body_len = strlen(R.body);
        go(&r);
        expect_error_free(&r, 415, "json_required");
    }
    pt_http_resp_t r;
    R.method = "POST";
    R.path = "/api/logout";
    R.content_type = "text/plain";
    R.body = NULL;
    R.body_len = 0;
    go(&r);
    expect_error_free(&r, 415, "json_required");
    R.method = "POST";
    R.path = "/api/login";
    R.content_type = "application/x-www-form-urlencoded";
    R.body = "password=x";
    R.body_len = 10;
    go(&r);
    TEST_ASSERT_EQUAL_STRING("", r.set_cookie);
    expect_error_free(&r, 415, "json_required");
    TEST_ASSERT_EQUAL_INT(0, save_calls);
    R.method = "PUT";
    R.path = "/api/settings";
    R.content_type = "Application/JSON; charset=utf-8";
    R.body = "{\"openai\":{\"model\":\"gpt-x\"}}";
    R.body_len = strlen(R.body);
    go(&r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
}

static void test_body_too_large_413(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    memset(body_buf, ' ', 16385);
    body_buf[16385] = '\0';
    memcpy(body_buf, "{}", 2);
    pt_http_resp_t r;
    R.method = "PUT";
    R.path = "/api/settings";
    R.content_type = "application/json";
    R.body = body_buf;
    R.body_len = 16385;
    go(&r);
    expect_error_free(&r, 413, "too_large");
    R.body_len = 16384;
    go(&r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
}

static void test_logout_drops_session(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("POST", "/api/logout", "{}", &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    TEST_ASSERT_FALSE(bool_field(&r, "logged_in"));
    TEST_ASSERT_TRUE(bool_field(&r, "password_set"));
    TEST_ASSERT_EQUAL_STRING(COOKIE_NAME "=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0", r.set_cookie);
    pt_http_resp_free(&r);
    request("GET", "/api/settings", NULL, &r);
    expect_error_free(&r, 401, "login_required");
}

static void test_change_password_drops_other_sessions(void)
{
    fixture(true);
    char first[80];
    char second[80];
    login(first, sizeof(first));
    login(second, sizeof(second));
    R.cookie = first;
    pt_http_resp_t r;
    request("POST", "/api/password", "{\"current\":\"" PASSWORD "\",\"new\":\"" NEW_PASSWORD "\"}", &r);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, r.status, r.body);
    TEST_ASSERT_TRUE(bool_field(&r, "logged_in"));
    char fresh[80];
    cookie_of(&r, fresh, sizeof(fresh));
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(1, save_calls);
    TEST_ASSERT_TRUE(pt_password_verify(NEW_PASSWORD, S.password_hash));
    TEST_ASSERT_FALSE(pt_password_verify(PASSWORD, S.password_hash));
    TEST_ASSERT_EQUAL_STRING(S.password_hash, saved.password_hash);
    R.cookie = second;
    request("GET", "/api/settings", NULL, &r);
    expect_error_free(&r, 401, "login_required");
    R.cookie = first;
    request("GET", "/api/settings", NULL, &r);
    expect_error_free(&r, 401, "login_required");
    R.cookie = fresh;
    request("GET", "/api/settings", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(0, sleep_calls);
}

static void test_change_password_wrong_current_403(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("POST", "/api/password", "{\"current\":\"wrong-guess-1\",\"new\":\"" NEW_PASSWORD "\"}", &r);
    expect_error_free(&r, 403, "wrong_password");
    TEST_ASSERT_EQUAL_INT(1, sleep_calls);
    TEST_ASSERT_EQUAL_UINT32(FAILED_DELAY_MS, sleep_ms_seen[0]);
    TEST_ASSERT_EQUAL_INT(0, save_calls);
    TEST_ASSERT_EQUAL_STRING(base_hash, S.password_hash);
    request("GET", "/api/settings", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
}

static void test_change_password_checks_new_before_hashing(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("POST", "/api/password", "{\"current\":\"" PASSWORD "\",\"new\":\"short\"}", &r);
    expect_error_free(&r, 400, "bad_password");
    request("POST", "/api/password", "{\"current\":\"" PASSWORD "\"}", &r);
    expect_error_free(&r, 400, "bad_password");
    request("POST", "/api/password", "{\"current\":5,\"new\":\"" NEW_PASSWORD "\"}", &r);
    expect_error_free(&r, 400, "invalid");
    request("POST", "/api/password", "{\"current\":\"" PASSWORD "\",\"new\":\"" NEW_PASSWORD "\",\"x\":1}", &r);
    expect_error_free(&r, 400, "invalid");
    TEST_ASSERT_EQUAL_INT(0, save_calls);
    TEST_ASSERT_EQUAL_INT(0, sleep_calls);
}

static void test_change_password_save_failure_keeps_sessions(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    save_fail = 1;
    pt_http_resp_t r;
    request("POST", "/api/password", "{\"current\":\"" PASSWORD "\",\"new\":\"" NEW_PASSWORD "\"}", &r);
    TEST_ASSERT_EQUAL_STRING("", r.set_cookie);
    expect_error_free(&r, 500, "save_failed");
    TEST_ASSERT_EQUAL_STRING(base_hash, S.password_hash);
    request("GET", "/api/settings", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
}

static void test_volume_sets_and_saves(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("PUT", "/api/volume", "{\"volume\":0.35}", &r);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, r.status, r.body);
    cJSON *j = parse_body(&r);
    TEST_ASSERT_EQUAL_FLOAT(0.35f, (float)cJSON_GetObjectItem(j, "volume")->valuedouble);
    cJSON_Delete(j);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(1, save_calls);
    TEST_ASSERT_EQUAL_FLOAT(0.35f, saved.sound.volume);
    TEST_ASSERT_EQUAL_FLOAT(0.35f, S.sound.volume);
    TEST_ASSERT_EQUAL_INT(1, volume_calls);
    TEST_ASSERT_EQUAL_FLOAT(0.35f, volume_value);
    request("PUT", "/api/volume", "{\"volume\":0}", &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    request("PUT", "/api/volume", "{\"volume\":1}", &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, S.sound.volume);
}

static void test_volume_rejects_bad_values_and_failed_save(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    static const char *bad[] = {"{\"volume\":1.01}", "{\"volume\":-0.1}", "{\"volume\":\"0.5\"}",
                                "{\"volume\":null}", "{}", "{\"volume\":0.5,\"x\":1}", "{\"volume\":true}"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        pt_http_resp_t r;
        request("PUT", "/api/volume", bad[i], &r);
        expect_error_free(&r, 400, "invalid");
    }
    pt_http_resp_t r;
    request("PUT", "/api/volume", "[]", &r);
    expect_error_free(&r, 400, "bad_json");
    TEST_ASSERT_EQUAL_INT(0, save_calls);
    TEST_ASSERT_EQUAL_INT(0, volume_calls);
    save_fail = 1;
    request("PUT", "/api/volume", "{\"volume\":0.2}", &r);
    expect_error_free(&r, 500, "save_failed");
    TEST_ASSERT_EQUAL_INT(0, volume_calls);
    TEST_ASSERT_EQUAL_FLOAT(0.7f, S.sound.volume);
}

static void test_default_instructions_by_language(void)
{
    fixture(true);
    pt_http_resp_t r;
    request("GET", "/api/instructions?language=en", NULL, &r);
    expect_error_free(&r, 401, "login_required");
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    const char *langs[] = {"uk", "en", "pl", "tr"};
    for (size_t i = 0; i < sizeof(langs) / sizeof(langs[0]); i++) {
        char path[48];
        snprintf(path, sizeof(path), "/api/instructions?language=%s", langs[i]);
        request("GET", path, NULL, &r);
        TEST_ASSERT_EQUAL_INT(200, r.status);
        cJSON *j = parse_body(&r);
        TEST_ASSERT_EQUAL_STRING(pt_default_instructions(langs[i]), cJSON_GetStringValue(cJSON_GetObjectItem(j, "instructions")));
        cJSON_Delete(j);
        pt_http_resp_free(&r);
    }
    const char *bad[] = {"/api/instructions", "/api/instructions?language=ru", "/api/instructions?language=", "/api/instructions?lang=en"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        request("GET", bad[i], NULL, &r);
        expect_error_free(&r, 400, "invalid");
    }
}

static void test_log_after_seq(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_log_add(L, 5, "first");
    pt_log_add(L, 6, "second");
    pt_http_resp_t r;
    request("GET", "/api/log", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    cJSON *j = parse_body(&r);
    cJSON *lines = cJSON_GetObjectItem(j, "lines");
    TEST_ASSERT_TRUE(cJSON_GetArraySize(lines) >= 2);
    cJSON_Delete(j);
    pt_http_resp_free(&r);
    request("GET", "/api/log?after=0", NULL, &r);
    j = parse_body(&r);
    int total = cJSON_GetArraySize(cJSON_GetObjectItem(j, "lines"));
    cJSON_Delete(j);
    pt_http_resp_free(&r);
    char path[40];
    snprintf(path, sizeof(path), "/api/log?after=%d", total - 1);
    request("GET", path, NULL, &r);
    j = parse_body(&r);
    cJSON *lines2 = cJSON_GetObjectItem(j, "lines");
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetArraySize(lines2));
    cJSON *last = cJSON_GetArrayItem(lines2, 0);
    TEST_ASSERT_EQUAL_STRING("second", cJSON_GetObjectItem(last, "text")->valuestring);
    TEST_ASSERT_EQUAL_INT(6, cJSON_GetObjectItem(last, "t")->valueint);
    TEST_ASSERT_EQUAL_INT(total, cJSON_GetObjectItem(last, "seq")->valueint);
    cJSON_Delete(j);
    pt_http_resp_free(&r);
    snprintf(path, sizeof(path), "/api/log?after=%d", total);
    request("GET", path, NULL, &r);
    j = parse_body(&r);
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(cJSON_GetObjectItem(j, "lines")));
    cJSON_Delete(j);
    pt_http_resp_free(&r);
    request("GET", "/api/log?after=abc", NULL, &r);
    expect_error_free(&r, 400, "invalid");
    request("GET", "/api/log?after=", NULL, &r);
    expect_error_free(&r, 400, "invalid");
}

static void test_device_info(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("GET", "/api/device", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    cJSON *j = parse_body(&r);
    TEST_ASSERT_EQUAL_STRING(DEVICE, cJSON_GetObjectItem(j, "name")->valuestring);
    TEST_ASSERT_EQUAL_STRING(pt_version(), cJSON_GetObjectItem(j, "version")->valuestring);
    TEST_ASSERT_EQUAL_STRING("192.0.2.40", cJSON_GetObjectItem(j, "ip")->valuestring);
    TEST_ASSERT_EQUAL_INT(-61, cJSON_GetObjectItem(j, "rssi")->valueint);
    TEST_ASSERT_EQUAL_FLOAT(0.7f, (float)cJSON_GetObjectItem(j, "volume")->valuedouble);
    TEST_ASSERT_EQUAL_STRING("two-mic bss", cJSON_GetObjectItem(j, "mic_mode")->valuestring);
    const cJSON *heap = cJSON_GetObjectItem(j, "heap");
    const cJSON *internal = cJSON_GetObjectItem(heap, "internal");
    const cJSON *psram = cJSON_GetObjectItem(heap, "psram");
    TEST_ASSERT_EQUAL_INT(120000, cJSON_GetObjectItem(internal, "free")->valueint);
    TEST_ASSERT_EQUAL_INT(90000, cJSON_GetObjectItem(internal, "min_free")->valueint);
    TEST_ASSERT_EQUAL_INT(64000, cJSON_GetObjectItem(internal, "largest")->valueint);
    TEST_ASSERT_EQUAL_INT(7000000, cJSON_GetObjectItem(psram, "free")->valueint);
    TEST_ASSERT_EQUAL_INT(6500000, cJSON_GetObjectItem(psram, "min_free")->valueint);
    TEST_ASSERT_EQUAL_INT(6000000, cJSON_GetObjectItem(psram, "largest")->valueint);
    TEST_ASSERT_EQUAL_INT(1, health_calls);
    cJSON_Delete(j);
    pt_http_resp_free(&r);
    request("PUT", "/api/volume", "{\"volume\":0.35}", &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    request("GET", "/api/device", NULL, &r);
    j = parse_body(&r);
    TEST_ASSERT_EQUAL_FLOAT(0.35f, (float)cJSON_GetObjectItem(j, "volume")->valuedouble);
    cJSON_Delete(j);
    pt_http_resp_free(&r);
}

static void test_unknown_api_path_404(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("GET", "/api/nope", NULL, &r);
    expect_error_free(&r, 404, "not_found");
    request("GET", "/api", NULL, &r);
    expect_error_free(&r, 404, "not_found");
    request("POST", "/api/devices/kitchen", "{}", &r);
    expect_error_free(&r, 404, "not_found");
    request("GET", "/api/settings/extra", NULL, &r);
    expect_error_free(&r, 404, "not_found");
    request("GET", "/index.html", NULL, &r);
    expect_error_free(&r, 404, "not_found");
}

static void test_wrong_method_405(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("PATCH", "/api/settings", "{}", &r);
    expect_error_free(&r, 405, "method_not_allowed");
    request("GET", "/api/login", NULL, &r);
    expect_error_free(&r, 405, "method_not_allowed");
    request("DELETE", "/api/volume", "{}", &r);
    expect_error_free(&r, 405, "method_not_allowed");
}

static void test_secrets_never_reach_the_log(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("PUT", "/api/settings", "{\"openai\":{\"api_key\":\"" OPENAI_KEY "\"}}", &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    request("POST", "/api/password", "{\"current\":\"" PASSWORD "\",\"new\":\"" NEW_PASSWORD "\"}", &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    request("POST", "/api/login", "{\"password\":\"guess-wrong-123\"}", &r);
    pt_http_resp_free(&r);
    char *json = pt_log_json(L, 0);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NULL(strstr(json, "MARKER"));
    TEST_ASSERT_NULL(strstr(json, "pbkdf2"));
    TEST_ASSERT_NULL(strstr(json, PASSWORD));
    TEST_ASSERT_NULL(strstr(json, NEW_PASSWORD));
    TEST_ASSERT_NULL(strstr(json, "guess-wrong-123"));
    TEST_ASSERT_NOT_NULL(strstr(json, "settings changed"));
    free(json);
}

static void set_key(void)
{
    snprintf(S.openai.api_key, sizeof(S.openai.api_key), "%s", OPENAI_KEY);
}

static void test_openai_test_requires_login(void)
{
    fixture(true);
    set_key();
    pt_http_resp_t r;
    request("POST", "/api/openai/test", "{}", &r);
    expect_error_free(&r, 401, "login_required");
    TEST_ASSERT_EQUAL_INT(0, openai_calls);
}

static void test_openai_test_ok_shape(void)
{
    fixture(true);
    set_key();
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("POST", "/api/openai/test", "{}", &r);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, r.status, r.body);
    cJSON *j = parse_body(&r);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(j, "ok")));
    TEST_ASSERT_EQUAL_STRING("gpt-realtime-2.1-mini", cJSON_GetObjectItem(j, "model")->valuestring);
    TEST_ASSERT_EQUAL_STRING("marin", cJSON_GetObjectItem(j, "voice")->valuestring);
    cJSON_Delete(j);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(1, openai_calls);
    TEST_ASSERT_EQUAL_STRING(OPENAI_KEY, openai_seen_key);
    TEST_ASSERT_EQUAL_INT(0, save_calls);
}

static void test_openai_test_bad_key_error(void)
{
    fixture(true);
    set_key();
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    openai_result = PT_WS_BAD_KEY;
    request("POST", "/api/openai/test", "{}", &r);
    expect_error_free(&r, 502, "openai_bad_key");
    openai_result = PT_WS_NO_NETWORK;
    request("POST", "/api/openai/test", "{}", &r);
    expect_error_free(&r, 502, "openai_unreachable");
    openai_result = PT_WS_REFUSED;
    request("POST", "/api/openai/test", "{}", &r);
    expect_error_free(&r, 502, "openai_unreachable");
    openai_result = PT_WS_TIMEOUT;
    request("POST", "/api/openai/test", "{}", &r);
    expect_error_free(&r, 504, "openai_timeout");
    openai_result = PT_WS_REJECTED;
    request("POST", "/api/openai/test", "{}", &r);
    TEST_ASSERT_NULL(strstr(r.body, "openai_code"));
    expect_error_free(&r, 502, "openai_rejected");
    TEST_ASSERT_EQUAL_INT(5, openai_calls);
    snprintf(openai_error, sizeof(openai_error), "model_not_found: The model 'gpt-x' does not exist");
    request("POST", "/api/openai/test", "{}", &r);
    TEST_ASSERT_EQUAL_INT(502, r.status);
    cJSON *j = parse_body(&r);
    TEST_ASSERT_EQUAL_STRING("openai_rejected", cJSON_GetObjectItem(j, "error")->valuestring);
    TEST_ASSERT_EQUAL_STRING("model_not_found", cJSON_GetObjectItem(j, "openai_code")->valuestring);
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(j, "message"));
    cJSON_Delete(j);
    pt_http_resp_free(&r);
    TEST_ASSERT_TRUE(log_contains("OpenAI connection check: OpenAI did not accept the request: model_not_found: The model 'gpt-x' does not exist"));
}

static void test_openai_test_busy_during_conversation(void)
{
    fixture(true);
    set_key();
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    openai_busy = 1;
    request("POST", "/api/openai/test", "{}", &r);
    expect_error_free(&r, 409, "busy");
    TEST_ASSERT_EQUAL_INT(0, openai_calls);
    openai_busy = 0;
    request("POST", "/api/openai/test", "{}", &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(1, openai_calls);
}

static void test_sound_test_requires_login(void)
{
    fixture(true);
    pt_http_resp_t r;
    request("POST", "/api/sound/test", "{}", &r);
    expect_error_free(&r, 401, "login_required");
    TEST_ASSERT_EQUAL_INT(0, sound_test_calls);
}

static void test_sound_test_plays_and_logs(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("POST", "/api/sound/test", "{}", &r);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, r.status, r.body);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(1, sound_test_calls);
    TEST_ASSERT_TRUE(log_contains("sound test"));
}

static void test_sound_test_waits_for_a_conversation_and_takes_no_body(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    openai_busy = 1;
    request("POST", "/api/sound/test", "{}", &r);
    expect_error_free(&r, 409, "busy");
    openai_busy = 0;
    request("POST", "/api/sound/test", "{\"x\":1}", &r);
    expect_error_free(&r, 400, "invalid");
    request("GET", "/api/sound/test", NULL, &r);
    TEST_ASSERT_EQUAL_INT(405, r.status);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(0, sound_test_calls);
}

static void test_dropping_all_logins_logs_every_page_out(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("GET", "/api/settings", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    pt_api_drop_logins(A);
    request("GET", "/api/settings", NULL, &r);
    expect_error_free(&r, 401, "login_required");
    pt_api_drop_logins(NULL);
    R.cookie = NULL;
    request("GET", "/api/state", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    request("GET", "/api/settings", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
}

static void test_openai_test_and_update_wait_only_for_a_turn(void)
{
    fixture(true);
    set_key();
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("POST", "/api/openai/test", "{}", &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(1, openai_calls);
    openai_busy = 1;
    request("POST", "/api/openai/test", "{}", &r);
    expect_error_free(&r, 409, "busy");
    TEST_ASSERT_EQUAL_INT(1, openai_calls);
    request("POST", "/api/update", "{}", &r);
    expect_error_free(&r, 409, "update_busy");
    TEST_ASSERT_EQUAL_INT(0, ota_install_calls);
    openai_busy = 0;
    request("POST", "/api/update", "{}", &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(1, ota_install_calls);
}

static void test_openai_test_does_not_run_twice_at_once(void)
{
    fixture(true);
    set_key();
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    openai_nested = 1;
    request("POST", "/api/openai/test", "{}", &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(409, openai_nested_status);
    TEST_ASSERT_EQUAL_INT(1, openai_calls);
    openai_nested = 0;
    request("POST", "/api/openai/test", "{}", &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(2, openai_calls);
}

static void test_openai_test_without_key_or_with_a_body(void)
{
    fixture(true);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("POST", "/api/openai/test", "{}", &r);
    expect_error_free(&r, 400, "openai_no_key");
    TEST_ASSERT_EQUAL_INT(0, openai_calls);
    set_key();
    request("POST", "/api/openai/test", "{\"model\":\"x\"}", &r);
    expect_error_free(&r, 400, "invalid");
    request("POST", "/api/openai/test", "[]", &r);
    expect_error_free(&r, 400, "bad_json");
    request("GET", "/api/openai/test", NULL, &r);
    expect_error_free(&r, 405, "method_not_allowed");
    TEST_ASSERT_EQUAL_INT(0, openai_calls);
}

static void test_openai_test_without_a_transport_is_internal(void)
{
    fixture(true);
    pt_api_free(A);
    pt_api_env_t env = {
        .save = fake_save,
        .now_s = fake_now,
        .random = fake_random,
        .device_name = DEVICE,
    };
    A = pt_api_new(&S, L, &env);
    TEST_ASSERT_NOT_NULL(A);
    set_key();
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("POST", "/api/openai/test", "{}", &r);
    expect_error_free(&r, 500, "internal");
}

static void test_openai_test_logs_result_never_the_key(void)
{
    fixture(true);
    set_key();
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("POST", "/api/openai/test", "{}", &r);
    pt_http_resp_free(&r);
    openai_result = PT_WS_BAD_KEY;
    request("POST", "/api/openai/test", "{}", &r);
    pt_http_resp_free(&r);
    char *json = pt_log_json(L, 0);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NULL(strstr(json, "MARKER"));
    TEST_ASSERT_NOT_NULL(strstr(json, "OpenAI connection check: ok"));
    TEST_ASSERT_NOT_NULL(strstr(json, "OpenAI connection check: the key was refused"));
    free(json);
}

static void logged_in_as(char *cookie, size_t n)
{
    fixture(true);
    login(cookie, n);
    R.cookie = cookie;
}

static void upload_request(size_t len)
{
    R.method = "POST";
    R.path = "/api/update/upload";
    R.content_type = "application/octet-stream";
    R.body = NULL;
    R.body_len = len;
}

static void test_update_requires_login(void)
{
    fixture(true);
    pt_http_resp_t r;
    request("GET", "/api/update", NULL, &r);
    expect_error_free(&r, 401, "login_required");
    request("POST", "/api/update", "{}", &r);
    expect_error_free(&r, 401, "login_required");
    upload_request(1000);
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_error_free(&r, 401, "login_required");
    TEST_ASSERT_EQUAL_INT(0, ota_check_calls);
    TEST_ASSERT_EQUAL_INT(0, ota_install_calls);
    TEST_ASSERT_EQUAL_INT(0, up_begin_calls);
}

static void test_update_get_reports_the_latest_version(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    request("GET", "/api/update", NULL, &r);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, r.status, r.body);
    cJSON *j = parse_body(&r);
    TEST_ASSERT_EQUAL_STRING(pt_version(), cJSON_GetObjectItem(j, "current")->valuestring);
    TEST_ASSERT_EQUAL_STRING("2.2.0", cJSON_GetObjectItem(j, "latest")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(j, "available")));
    cJSON_Delete(j);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(1, ota_check_calls);
    TEST_ASSERT_EQUAL_INT(0, ota_install_calls);
}

static void test_update_get_when_nothing_is_newer(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    snprintf(ota_check_version, sizeof(ota_check_version), "%s", pt_version());
    pt_http_resp_t r;
    request("GET", "/api/update", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    cJSON *j = parse_body(&r);
    TEST_ASSERT_TRUE(cJSON_IsString(cJSON_GetObjectItem(j, "latest")));
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(j, "available")));
    cJSON_Delete(j);
    pt_http_resp_free(&r);
}

static void test_update_get_without_release_has_null_latest(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    ota_check_result = PT_ERR_NO_RELEASE;
    pt_http_resp_t r;
    request("GET", "/api/update", NULL, &r);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    cJSON *j = parse_body(&r);
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(j, "latest")));
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(j, "available")));
    TEST_ASSERT_EQUAL_STRING(pt_version(), cJSON_GetObjectItem(j, "current")->valuestring);
    cJSON_Delete(j);
    pt_http_resp_free(&r);
}

static void test_update_get_failure_is_update_failed(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    ota_check_result = PT_ERR_IO;
    pt_http_resp_t r;
    request("GET", "/api/update", NULL, &r);
    expect_error_free(&r, 502, "update_failed");
}

static void test_update_post_installs_the_latest(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    request("POST", "/api/update", "{}", &r);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, r.status, r.body);
    cJSON *j = parse_body(&r);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(j, "installing")));
    TEST_ASSERT_EQUAL_STRING("2.2.0", cJSON_GetObjectItem(j, "version")->valuestring);
    cJSON_Delete(j);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(1, ota_install_calls);
    TEST_ASSERT_EQUAL_STRING("https://github.com/feroxes/pytaichenko/releases/download/v2.2.0/x.ota.bin",
                             ota_install_url);
    TEST_ASSERT_EQUAL_STRING("2.2.0", ota_install_version);
}

static void test_update_post_with_nothing_newer_is_no_release(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    snprintf(ota_check_version, sizeof(ota_check_version), "%s", pt_version());
    pt_http_resp_t r;
    request("POST", "/api/update", "{}", &r);
    expect_error_free(&r, 404, "no_release");
    ota_check_result = PT_ERR_NO_RELEASE;
    request("POST", "/api/update", "{}", &r);
    expect_error_free(&r, 404, "no_release");
    TEST_ASSERT_EQUAL_INT(0, ota_install_calls);
}

static void test_update_post_refused_while_a_conversation_runs(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    openai_busy = 1;
    pt_http_resp_t r;
    request("POST", "/api/update", "{}", &r);
    expect_error_free(&r, 409, "update_busy");
    TEST_ASSERT_EQUAL_INT(0, ota_check_calls);
    TEST_ASSERT_EQUAL_INT(0, ota_install_calls);
}

static void test_update_post_while_a_file_is_uploading_says_so(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    upload_request(1000);
    TEST_ASSERT_TRUE(pt_api_upload_begin(A, &R, &r));
    R.method = "POST";
    R.path = "/api/update";
    R.content_type = "application/json";
    R.body = "{}";
    R.body_len = 2;
    request("POST", "/api/update", "{}", &r);
    TEST_ASSERT_EQUAL_INT(409, r.status);
    cJSON *j = parse_body(&r);
    TEST_ASSERT_EQUAL_STRING("update_busy", cJSON_GetObjectItem(j, "error")->valuestring);
    TEST_ASSERT_NOT_NULL(strstr(cJSON_GetObjectItem(j, "message")->valuestring, "uploaded"));
    cJSON_Delete(j);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(0, ota_check_calls);
    pt_api_upload_abort(A);
}

static void test_ota_rejects_wrong_project(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    ota_install_result = PT_ERR_NOT_OURS;
    pt_http_resp_t r;
    request("POST", "/api/update", "{}", &r);
    expect_error_free(&r, 400, "update_not_ours");
    ota_install_result = PT_ERR_IO;
    request("POST", "/api/update", "{}", &r);
    expect_error_free(&r, 502, "update_failed");
}

static void expect_busy_message(pt_http_resp_t *r, const char *word)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(409, r->status, r->body);
    cJSON *j = parse_body(r);
    TEST_ASSERT_EQUAL_STRING("update_busy", cJSON_GetObjectItem(j, "error")->valuestring);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(cJSON_GetObjectItem(j, "message")->valuestring, word), r->body);
    cJSON_Delete(j);
    pt_http_resp_free(r);
}

static void test_update_refused_while_restart_or_verification_is_pending(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    ota_install_result = PT_ERR_BUSY;
    request("POST", "/api/update", "{}", &r);
    expect_busy_message(&r, "restarting");
    ota_install_result = PT_ERR_PENDING;
    request("POST", "/api/update", "{}", &r);
    expect_busy_message(&r, "still being checked");
    upload_request(1000);
    up_begin_result = PT_ERR_BUSY;
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_busy_message(&r, "restarting");
    up_begin_result = PT_ERR_PENDING;
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_busy_message(&r, "still being checked");
    up_begin_result = PT_OK;
    TEST_ASSERT_TRUE(pt_api_upload_begin(A, &R, &r));
    pt_api_upload_abort(A);
}

static void test_update_refused_when_a_turn_starts_while_the_speaker_is_held(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    ota_install_result = PT_ERR_TALKING;
    request("POST", "/api/update", "{}", &r);
    expect_busy_message(&r, "conversation");
    upload_request(1000);
    up_begin_result = PT_ERR_TALKING;
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_busy_message(&r, "conversation");
    TEST_ASSERT_EQUAL_INT(0, up_abort_calls);
}

static void test_update_post_check_failures(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    ota_check_result = PT_ERR_IO;
    pt_http_resp_t r;
    request("POST", "/api/update", "{}", &r);
    expect_error_free(&r, 502, "update_failed");
    TEST_ASSERT_EQUAL_INT(0, ota_install_calls);
}

static void test_update_post_takes_an_empty_object_only(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    request("POST", "/api/update", "{\"url\":\"https://example.com/x.bin\"}", &r);
    expect_error_free(&r, 400, "invalid");
    request("POST", "/api/update", "[]", &r);
    expect_error_free(&r, 400, "bad_json");
    TEST_ASSERT_EQUAL_INT(0, ota_install_calls);
}

static void test_update_without_a_transport_is_internal(void)
{
    fixture(true);
    pt_api_free(A);
    pt_api_env_t env = {
        .save = fake_save, .now_s = fake_now, .random = fake_random, .device_name = DEVICE,
    };
    A = pt_api_new(&S, L, &env);
    char cookie[80];
    login(cookie, sizeof(cookie));
    R.cookie = cookie;
    pt_http_resp_t r;
    request("GET", "/api/update", NULL, &r);
    expect_error_free(&r, 500, "internal");
    request("POST", "/api/update", "{}", &r);
    expect_error_free(&r, 500, "internal");
    upload_request(100);
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_error_free(&r, 500, "internal");
}

static void test_update_is_logged_without_urls_or_secrets(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    request("POST", "/api/update", "{}", &r);
    pt_http_resp_free(&r);
    char *log = pt_log_json(L, 0);
    TEST_ASSERT_NOT_NULL(log);
    TEST_ASSERT_NOT_NULL(strstr(log, "2.2.0"));
    TEST_ASSERT_NULL(strstr(log, "github.com"));
    free(log);
}

static void test_upload_flow_streams_chunks_to_the_writer(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    upload_request(3000);
    TEST_ASSERT_TRUE(pt_api_upload_begin(A, &R, &r));
    TEST_ASSERT_EQUAL_INT(0, r.status);
    TEST_ASSERT_NULL(r.body);
    TEST_ASSERT_EQUAL_INT(1, up_begin_calls);
    static uint8_t chunk[1000];
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(pt_api_upload_chunk(A, chunk, sizeof(chunk), &r));
    }
    TEST_ASSERT_EQUAL_INT(3, up_write_calls);
    TEST_ASSERT_EQUAL_UINT32(3000, (uint32_t)up_written);
    TEST_ASSERT_EQUAL_INT(0, up_finish_calls);
    pt_api_upload_end(A, &r);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, r.status, r.body);
    cJSON *j = parse_body(&r);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(j, "installing")));
    cJSON_Delete(j);
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(1, up_finish_calls);
    TEST_ASSERT_EQUAL_INT(0, up_abort_calls);
}

static void test_upload_size_cap(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    upload_request(PT_API_UPLOAD_MAX + 1);
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_error_free(&r, 413, "too_large");
    TEST_ASSERT_EQUAL_INT(0, up_begin_calls);
    upload_request(0);
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_error_free(&r, 400, "invalid");
    TEST_ASSERT_EQUAL_INT(0, up_begin_calls);
    upload_request(PT_API_UPLOAD_MAX);
    TEST_ASSERT_TRUE(pt_api_upload_begin(A, &R, &r));
    TEST_ASSERT_EQUAL_INT(1, up_begin_calls);
    pt_api_upload_abort(A);
    TEST_ASSERT_EQUAL_INT(1, up_abort_calls);
}

static void test_upload_more_bytes_than_declared_is_refused(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    upload_request(1500);
    TEST_ASSERT_TRUE(pt_api_upload_begin(A, &R, &r));
    static uint8_t chunk[1000];
    TEST_ASSERT_TRUE(pt_api_upload_chunk(A, chunk, sizeof(chunk), &r));
    TEST_ASSERT_FALSE(pt_api_upload_chunk(A, chunk, sizeof(chunk), &r));
    expect_error_free(&r, 400, "invalid");
    TEST_ASSERT_EQUAL_INT(1, up_abort_calls);
    TEST_ASSERT_EQUAL_INT(1, up_write_calls);
}

static void test_upload_ending_early_is_refused(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    upload_request(2000);
    TEST_ASSERT_TRUE(pt_api_upload_begin(A, &R, &r));
    static uint8_t chunk[1000];
    TEST_ASSERT_TRUE(pt_api_upload_chunk(A, chunk, sizeof(chunk), &r));
    pt_api_upload_end(A, &r);
    expect_error_free(&r, 400, "invalid");
    TEST_ASSERT_EQUAL_INT(0, up_finish_calls);
    TEST_ASSERT_EQUAL_INT(1, up_abort_calls);
}

static void test_upload_not_our_image_stops_at_once(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    upload_request(5000);
    TEST_ASSERT_TRUE(pt_api_upload_begin(A, &R, &r));
    up_write_fail_at = 1;
    up_write_result = PT_ERR_NOT_OURS;
    static uint8_t chunk[1000];
    TEST_ASSERT_FALSE(pt_api_upload_chunk(A, chunk, sizeof(chunk), &r));
    expect_error_free(&r, 400, "update_not_ours");
    TEST_ASSERT_EQUAL_INT(1, up_abort_calls);
    TEST_ASSERT_FALSE(pt_api_upload_chunk(A, chunk, sizeof(chunk), &r));
    pt_http_resp_free(&r);
    TEST_ASSERT_EQUAL_INT(1, up_write_calls);
    TEST_ASSERT_EQUAL_INT(1, up_abort_calls);
}

static void test_upload_write_and_finish_failures(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    static uint8_t chunk[1000];
    upload_request(2000);
    TEST_ASSERT_TRUE(pt_api_upload_begin(A, &R, &r));
    up_write_fail_at = 1;
    up_write_result = PT_ERR_IO;
    TEST_ASSERT_FALSE(pt_api_upload_chunk(A, chunk, sizeof(chunk), &r));
    expect_error_free(&r, 500, "update_failed");
    up_write_fail_at = 0;
    TEST_ASSERT_TRUE(pt_api_upload_begin(A, &R, &r));
    TEST_ASSERT_TRUE(pt_api_upload_chunk(A, chunk, sizeof(chunk), &r));
    TEST_ASSERT_TRUE(pt_api_upload_chunk(A, chunk, sizeof(chunk), &r));
    up_finish_result = PT_ERR_NOT_OURS;
    pt_api_upload_end(A, &r);
    expect_error_free(&r, 400, "update_not_ours");
    TEST_ASSERT_TRUE(pt_api_upload_begin(A, &R, &r));
    TEST_ASSERT_TRUE(pt_api_upload_chunk(A, chunk, sizeof(chunk), &r));
    TEST_ASSERT_TRUE(pt_api_upload_chunk(A, chunk, sizeof(chunk), &r));
    up_finish_result = PT_ERR_IO;
    pt_api_upload_end(A, &r);
    expect_error_free(&r, 500, "update_failed");
}

static void test_upload_begin_failure_and_busy(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    up_begin_result = PT_ERR_IO;
    upload_request(1000);
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_error_free(&r, 500, "update_failed");
    up_begin_result = PT_OK;
    openai_busy = 1;
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_error_free(&r, 409, "update_busy");
    openai_busy = 0;
    TEST_ASSERT_TRUE(pt_api_upload_begin(A, &R, &r));
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_error_free(&r, 409, "update_busy");
    pt_api_upload_abort(A);
    pt_api_upload_abort(A);
    TEST_ASSERT_EQUAL_INT(1, up_abort_calls);
}

static void test_upload_needs_octet_stream_and_post(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    upload_request(1000);
    R.content_type = "application/json";
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_error_free(&r, 415, "octet_required");
    R.content_type = NULL;
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_error_free(&r, 415, "octet_required");
    R.content_type = "Application/Octet-Stream; x=y";
    TEST_ASSERT_TRUE(pt_api_upload_begin(A, &R, &r));
    pt_api_upload_abort(A);
    R.method = "PUT";
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_error_free(&r, 405, "method_not_allowed");
    R.method = "POST";
    R.path = "/api/update/other";
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_error_free(&r, 404, "not_found");
    TEST_ASSERT_EQUAL_INT(1, up_begin_calls);
}

static void test_upload_checks_host_and_origin(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    upload_request(1000);
    R.host = "evil.example.com";
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_error_free(&r, 403, "foreign_host");
    R.host = "192.168.1.50";
    R.origin = "http://evil.example.com";
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_error_free(&r, 403, "foreign_origin");
    R.origin = NULL;
    R.sec_fetch_site = "cross-site";
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    expect_error_free(&r, 403, "foreign_origin");
    R.sec_fetch_site = "same-origin";
    R.origin = "http://192.168.1.50";
    TEST_ASSERT_TRUE(pt_api_upload_begin(A, &R, &r));
    pt_api_upload_abort(A);
    TEST_ASSERT_EQUAL_INT(1, up_begin_calls);
}

static void test_upload_refusals_drain_only_for_logged_in_requests(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    upload_request(1000);
    R.cookie = NULL;
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    TEST_ASSERT_FALSE(r.drain_refused_body);
    expect_error_free(&r, 401, "login_required");
    R.cookie = cookie;
    R.host = "evil.example.com";
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    TEST_ASSERT_FALSE(r.drain_refused_body);
    expect_error_free(&r, 403, "foreign_host");
    R.host = "192.168.1.50";
    R.origin = "http://evil.example.com";
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    TEST_ASSERT_FALSE(r.drain_refused_body);
    expect_error_free(&r, 403, "foreign_origin");
    R.origin = NULL;
    upload_request(PT_API_UPLOAD_MAX + 1);
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    TEST_ASSERT_TRUE(r.drain_refused_body);
    expect_error_free(&r, 413, "too_large");
    upload_request(1000);
    openai_busy = 1;
    TEST_ASSERT_FALSE(pt_api_upload_begin(A, &R, &r));
    TEST_ASSERT_TRUE(r.drain_refused_body);
    expect_error_free(&r, 409, "update_busy");
    openai_busy = 0;
    TEST_ASSERT_TRUE(pt_api_upload_begin(A, &R, &r));
    pt_api_upload_abort(A);
}

static void test_upload_route_is_not_served_as_a_small_json_request(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    request("POST", "/api/update/upload", "{}", &r);
    expect_error_free(&r, 404, "not_found");
    TEST_ASSERT_EQUAL_INT(0, up_begin_calls);
}

static void test_upload_free_with_active_upload_aborts_it(void)
{
    char cookie[80];
    logged_in_as(cookie, sizeof(cookie));
    pt_http_resp_t r;
    upload_request(1000);
    TEST_ASSERT_TRUE(pt_api_upload_begin(A, &R, &r));
    pt_api_free(A);
    A = NULL;
    TEST_ASSERT_EQUAL_INT(1, up_abort_calls);
}

static void test_resp_free_is_safe_twice(void)
{
    pt_http_resp_t r = {0};
    pt_http_resp_free(&r);
    pt_http_resp_free(NULL);
    fixture(true);
    request("GET", "/api/state", NULL, &r);
    pt_http_resp_free(&r);
    TEST_ASSERT_NULL(r.body);
    pt_http_resp_free(&r);
}

int main(void)
{
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_password_hash(PASSWORD, fake_random, NULL, base_hash, sizeof(base_hash)));
    UNITY_BEGIN();
    RUN_TEST(test_state_before_setup);
    RUN_TEST(test_state_tells_which_screen_to_show);
    RUN_TEST(test_setup_sets_password_and_language);
    RUN_TEST(test_setup_without_language_keeps_default);
    RUN_TEST(test_setup_twice_rejected);
    RUN_TEST(test_setup_checks_the_password_and_fields);
    RUN_TEST(test_setup_password_length_limits);
    RUN_TEST(test_setup_save_failure_keeps_no_password);
    RUN_TEST(test_login_with_the_right_password);
    RUN_TEST(test_login_wrong_password_401_with_delay);
    RUN_TEST(test_login_queue_full_returns_429);
    RUN_TEST(test_a_flood_from_one_address_does_not_hold_up_other_addresses);
    RUN_TEST(test_login_without_password_set);
    RUN_TEST(test_login_body_checks);
    RUN_TEST(test_deeply_nested_json_is_refused);
    RUN_TEST(test_settings_requires_login);
    RUN_TEST(test_cookie_is_found_among_others);
    RUN_TEST(test_get_settings_hides_key);
    RUN_TEST(test_put_settings_saves_and_applies);
    RUN_TEST(test_put_settings_bad_field_400_names_field);
    RUN_TEST(test_put_settings_bad_json);
    RUN_TEST(test_save_failure_500_save_failed_keeps_old);
    RUN_TEST(test_foreign_origin_403);
    RUN_TEST(test_foreign_origin_beats_public_routes);
    RUN_TEST(test_foreign_host_403);
    RUN_TEST(test_local_hosts_accepted);
    RUN_TEST(test_origin_matches_host_with_port_and_name);
    RUN_TEST(test_post_without_json_415);
    RUN_TEST(test_body_too_large_413);
    RUN_TEST(test_logout_drops_session);
    RUN_TEST(test_change_password_drops_other_sessions);
    RUN_TEST(test_change_password_wrong_current_403);
    RUN_TEST(test_change_password_checks_new_before_hashing);
    RUN_TEST(test_change_password_save_failure_keeps_sessions);
    RUN_TEST(test_volume_sets_and_saves);
    RUN_TEST(test_volume_rejects_bad_values_and_failed_save);
    RUN_TEST(test_default_instructions_by_language);
    RUN_TEST(test_log_after_seq);
    RUN_TEST(test_device_info);
    RUN_TEST(test_unknown_api_path_404);
    RUN_TEST(test_wrong_method_405);
    RUN_TEST(test_secrets_never_reach_the_log);
    RUN_TEST(test_sound_test_requires_login);
    RUN_TEST(test_sound_test_plays_and_logs);
    RUN_TEST(test_sound_test_waits_for_a_conversation_and_takes_no_body);
    RUN_TEST(test_openai_test_requires_login);
    RUN_TEST(test_openai_test_ok_shape);
    RUN_TEST(test_openai_test_bad_key_error);
    RUN_TEST(test_openai_test_busy_during_conversation);
    RUN_TEST(test_openai_test_and_update_wait_only_for_a_turn);
    RUN_TEST(test_dropping_all_logins_logs_every_page_out);
    RUN_TEST(test_openai_test_does_not_run_twice_at_once);
    RUN_TEST(test_openai_test_without_key_or_with_a_body);
    RUN_TEST(test_openai_test_without_a_transport_is_internal);
    RUN_TEST(test_openai_test_logs_result_never_the_key);
    RUN_TEST(test_update_requires_login);
    RUN_TEST(test_update_get_reports_the_latest_version);
    RUN_TEST(test_update_get_when_nothing_is_newer);
    RUN_TEST(test_update_get_without_release_has_null_latest);
    RUN_TEST(test_update_get_failure_is_update_failed);
    RUN_TEST(test_update_post_installs_the_latest);
    RUN_TEST(test_update_post_with_nothing_newer_is_no_release);
    RUN_TEST(test_update_post_refused_while_a_conversation_runs);
    RUN_TEST(test_update_post_while_a_file_is_uploading_says_so);
    RUN_TEST(test_ota_rejects_wrong_project);
    RUN_TEST(test_update_post_check_failures);
    RUN_TEST(test_update_refused_while_restart_or_verification_is_pending);
    RUN_TEST(test_update_refused_when_a_turn_starts_while_the_speaker_is_held);
    RUN_TEST(test_update_post_takes_an_empty_object_only);
    RUN_TEST(test_update_without_a_transport_is_internal);
    RUN_TEST(test_update_is_logged_without_urls_or_secrets);
    RUN_TEST(test_upload_flow_streams_chunks_to_the_writer);
    RUN_TEST(test_upload_size_cap);
    RUN_TEST(test_upload_more_bytes_than_declared_is_refused);
    RUN_TEST(test_upload_ending_early_is_refused);
    RUN_TEST(test_upload_not_our_image_stops_at_once);
    RUN_TEST(test_upload_write_and_finish_failures);
    RUN_TEST(test_upload_begin_failure_and_busy);
    RUN_TEST(test_upload_needs_octet_stream_and_post);
    RUN_TEST(test_upload_checks_host_and_origin);
    RUN_TEST(test_upload_refusals_drain_only_for_logged_in_requests);
    RUN_TEST(test_upload_route_is_not_served_as_a_small_json_request);
    RUN_TEST(test_upload_free_with_active_upload_aborts_it);
    RUN_TEST(test_resp_free_is_safe_twice);
    return UNITY_END();
}
