#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "pt_otapick.h"

void setUp(void) {}
void tearDown(void) {}

#define BASE "https://github.com/feroxes/pytaichenko/releases/download/"

static char json[8192];
static size_t used;

static void begin(void)
{
    used = 0;
    json[used++] = '[';
}

static void add(const char *tag, bool draft, bool prerelease, const char *asset_name)
{
    if (used > 1) {
        json[used++] = ',';
    }
    used += (size_t)snprintf(json + used, sizeof(json) - used,
                             "{\"tag_name\":\"%s\",\"draft\":%s,\"prerelease\":%s,\"assets\":[{\"name\":\"other.bin\","
                             "\"browser_download_url\":\"" BASE "%s/other.bin\"}",
                             tag, draft ? "true" : "false", prerelease ? "true" : "false", tag);
    if (asset_name != NULL) {
        used += (size_t)snprintf(json + used, sizeof(json) - used,
                                 ",{\"name\":\"%s\",\"browser_download_url\":\"" BASE "%s/%s\"}", asset_name, tag,
                                 asset_name);
    }
    used += (size_t)snprintf(json + used, sizeof(json) - used, "]}");
}

static pt_err_t pick(int major, pt_release_t *out)
{
    json[used] = ']';
    json[used + 1] = '\0';
    memset(out, 0, sizeof(*out));
    return pt_ota_pick(json, used + 1, major, out);
}

static void test_version_cmp(void)
{
    TEST_ASSERT_EQUAL_INT(0, pt_version_cmp("2.0.0", "2.0.0"));
    TEST_ASSERT_EQUAL_INT(0, pt_version_cmp("v2.0.0", "2.0.0"));
    TEST_ASSERT_TRUE(pt_version_cmp("2.0.1", "2.0.0") > 0);
    TEST_ASSERT_TRUE(pt_version_cmp("2.0.0", "2.0.1") < 0);
    TEST_ASSERT_TRUE(pt_version_cmp("2.10.0", "2.9.9") > 0);
    TEST_ASSERT_TRUE(pt_version_cmp("3.0.0", "2.99.99") > 0);
    TEST_ASSERT_TRUE(pt_version_cmp("2.0", "2.0.1") < 0);
    TEST_ASSERT_TRUE(pt_version_cmp("2.0.0", "2.0.0-dev") > 0);
    TEST_ASSERT_TRUE(pt_version_cmp("2.0.0-dev", "2.0.0") < 0);
    TEST_ASSERT_TRUE(pt_version_cmp("2.0.1-dev", "2.0.0") > 0);
    TEST_ASSERT_EQUAL_INT(0, pt_version_cmp("2.0.0-dev", "2.0.0-dev"));
    TEST_ASSERT_TRUE(pt_version_cmp("2.0.0-rc2", "2.0.0-rc1") > 0);
    TEST_ASSERT_EQUAL_INT(0, pt_version_cmp(NULL, NULL));
}

static void test_ota_pick_highest_v2(void)
{
    begin();
    add("v2.0.0", false, false, "pytaichenko-2.0.0.ota.bin");
    add("v2.3.1", false, false, "pytaichenko-2.3.1.ota.bin");
    add("v2.10.0", false, false, "pytaichenko-2.10.0.ota.bin");
    add("v2.9.9", false, false, "pytaichenko-2.9.9.ota.bin");
    pt_release_t r;
    TEST_ASSERT_EQUAL_INT(PT_OK, pick(2, &r));
    TEST_ASSERT_EQUAL_STRING("v2.10.0", r.tag);
    TEST_ASSERT_EQUAL_STRING("2.10.0", r.version);
    TEST_ASSERT_EQUAL_STRING(BASE "v2.10.0/pytaichenko-2.10.0.ota.bin", r.url);
}

static void test_ota_pick_ignores_other_majors(void)
{
    begin();
    add("v3.0.0", false, false, "pytaichenko-3.0.0.ota.bin");
    add("v2.1.0", false, false, "pytaichenko-2.1.0.ota.bin");
    add("v1.0.1", false, false, "pytaichenko-1.0.1.ota.bin");
    pt_release_t r;
    TEST_ASSERT_EQUAL_INT(PT_OK, pick(2, &r));
    TEST_ASSERT_EQUAL_STRING("2.1.0", r.version);
    begin();
    add("v1.0.1", false, false, "pytaichenko-1.0.1.ota.bin");
    add("v3.0.0", false, false, "pytaichenko-3.0.0.ota.bin");
    TEST_ASSERT_EQUAL_INT(PT_ERR_NO_RELEASE, pick(2, &r));
}

static void test_ota_pick_skips_prerelease_and_draft(void)
{
    begin();
    add("v2.5.0", true, false, "pytaichenko-2.5.0.ota.bin");
    add("v2.4.0", false, true, "pytaichenko-2.4.0.ota.bin");
    add("v2.4.0-rc1", false, false, "pytaichenko-2.4.0-rc1.ota.bin");
    add("v2.2.0", false, false, "pytaichenko-2.2.0.ota.bin");
    pt_release_t r;
    TEST_ASSERT_EQUAL_INT(PT_OK, pick(2, &r));
    TEST_ASSERT_EQUAL_STRING("2.2.0", r.version);
}

static void test_ota_pick_no_asset_is_error(void)
{
    begin();
    add("v2.1.0", false, false, NULL);
    pt_release_t r;
    TEST_ASSERT_EQUAL_INT(PT_ERR_NO_RELEASE, pick(2, &r));
    begin();
    add("v2.1.0", false, false, "pytaichenko-2.0.0.ota.bin");
    TEST_ASSERT_EQUAL_INT(PT_ERR_NO_RELEASE, pick(2, &r));
}

static void test_ota_pick_newest_without_asset_falls_back_to_one_with_asset(void)
{
    begin();
    add("v2.2.0", false, false, NULL);
    add("v2.1.0", false, false, "pytaichenko-2.1.0.ota.bin");
    pt_release_t r;
    TEST_ASSERT_EQUAL_INT(PT_OK, pick(2, &r));
    TEST_ASSERT_EQUAL_STRING("2.1.0", r.version);
}

static void test_ota_pick_refuses_urls_outside_the_repository(void)
{
    begin();
    used += (size_t)snprintf(json + used, sizeof(json) - used,
                             "{\"tag_name\":\"v2.1.0\",\"draft\":false,\"prerelease\":false,\"assets\":[{\"name\":"
                             "\"pytaichenko-2.1.0.ota.bin\",\"browser_download_url\":"
                             "\"https://example.com/pytaichenko-2.1.0.ota.bin\"}]}");
    pt_release_t r;
    TEST_ASSERT_EQUAL_INT(PT_ERR_NO_RELEASE, pick(2, &r));
}

static void test_ota_pick_bad_input(void)
{
    pt_release_t r;
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_ota_pick("not json", 8, 2, &r));
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_ota_pick("{\"a\":1}", 7, 2, &r));
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_ota_pick("[1,2", 4, 2, &r));
    TEST_ASSERT_EQUAL_INT(PT_ERR_NO_RELEASE, pt_ota_pick("[]", 2, 2, &r));
    TEST_ASSERT_EQUAL_INT(PT_ERR_NO_RELEASE, pt_ota_pick("[1,{\"tag_name\":5},{}]", 21, 2, &r));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ota_pick(NULL, 0, 2, &r));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ota_pick("[]", 2, 2, NULL));
}

static void test_ota_pick_long_values_do_not_overflow(void)
{
    static char big[2048];
    char tag[200];
    memset(tag, '9', sizeof(tag));
    tag[0] = 'v';
    tag[1] = '2';
    tag[2] = '.';
    tag[sizeof(tag) - 1] = '\0';
    snprintf(big, sizeof(big), "[{\"tag_name\":\"%s\",\"assets\":[]}]", tag);
    pt_release_t r;
    TEST_ASSERT_EQUAL_INT(PT_ERR_NO_RELEASE, pt_ota_pick(big, strlen(big), 2, &r));
}

#define USER_URLS \
    "\"followers_url\":\"https://api.github.com/users/feroxes/followers\",\"following_url\":" \
    "\"https://api.github.com/users/feroxes/following{/other_user}\",\"gists_url\":" \
    "\"https://api.github.com/users/feroxes/gists{/gist_id}\",\"starred_url\":" \
    "\"https://api.github.com/users/feroxes/starred{/owner}{/repo}\",\"subscriptions_url\":" \
    "\"https://api.github.com/users/feroxes/subscriptions\",\"organizations_url\":" \
    "\"https://api.github.com/users/feroxes/orgs\",\"repos_url\":\"https://api.github.com/users/feroxes/repos\"," \
    "\"events_url\":\"https://api.github.com/users/feroxes/events{/privacy}\",\"received_events_url\":" \
    "\"https://api.github.com/users/feroxes/received_events\",\"type\":\"User\",\"site_admin\":false"

static size_t realistic_release(char *out, size_t cap, int minor, bool asset_last)
{
    static char notes[12000];
    size_t n = 0;
    for (int i = 0; n + 200 < sizeof(notes) - 1 && i < 110; i++) {
        n += (size_t)snprintf(notes + n, sizeof(notes) - n,
                              "- fixed \\\"tag_name\\\": \\\"v2.99.%d\\\" in {brackets} [and] \\u00e9\\r\\n", i);
    }
    char assets[6000];
    size_t a = 0;
    const char *names[3] = {"pytaichenko-2.%d.0.factory.bin", "pytaichenko-2.%d.0.ota.bin", "pytaichenko-2.%d.0-stl.zip"};
    for (int k = 0; k < 3; k++) {
        int which = asset_last ? k : 2 - k;
        char name[64];
        snprintf(name, sizeof(name), names[which], minor);
        a += (size_t)snprintf(assets + a, sizeof(assets) - a,
                              "%s{\"url\":\"https://api.github.com/repos/feroxes/pytaichenko/releases/assets/%d\","
                              "\"id\":%d,\"node_id\":\"RA_kwDOAAAA\",\"name\":\"%s\",\"label\":null,\"uploader\":{"
                              "\"login\":\"github-actions[bot]\",\"id\":41898282,\"avatar_url\":"
                              "\"https://avatars.githubusercontent.com/in/15368?v=4\",%s},\"content_type\":"
                              "\"application/octet-stream\",\"state\":\"uploaded\",\"size\":2255000,\"download_count\":3,"
                              "\"created_at\":\"2026-11-03T10:00:00Z\",\"updated_at\":\"2026-11-03T10:00:01Z\","
                              "\"browser_download_url\":\"" BASE "v2.%d.0/%s\"}",
                              k > 0 ? "," : "", 1000 + minor * 3 + k, 1000 + minor * 3 + k, name, USER_URLS, minor, name);
    }
    return (size_t)snprintf(out, cap,
                            "{\"url\":\"https://api.github.com/repos/feroxes/pytaichenko/releases/%d\",\"assets_url\":"
                            "\"https://api.github.com/repos/feroxes/pytaichenko/releases/%d/assets\",\"upload_url\":"
                            "\"https://uploads.github.com/repos/feroxes/pytaichenko/releases/%d/assets{?name,label}\","
                            "\"html_url\":\"https://github.com/feroxes/pytaichenko/releases/tag/v2.%d.0\",\"id\":%d,"
                            "\"author\":{\"login\":\"github-actions[bot]\",\"id\":41898282,\"node_id\":\"MDM6Qm90\","
                            "\"avatar_url\":\"https://avatars.githubusercontent.com/in/15368?v=4\",\"gravatar_id\":\"\",%s},"
                            "\"node_id\":\"RE_kwDOAAAA\",\"tag_name\":\"v2.%d.0\",\"target_commitish\":\"main\","
                            "\"name\":\"Pytaichenko 2.%d.0\",\"draft\":false,\"immutable\":false,\"prerelease\":false,"
                            "\"created_at\":\"2026-11-03T09:59:00Z\",\"updated_at\":\"2026-11-03T10:00:02Z\","
                            "\"published_at\":\"2026-11-03T10:00:02Z\",\"assets\":[%s],\"tarball_url\":"
                            "\"https://api.github.com/repos/feroxes/pytaichenko/tarball/v2.%d.0\",\"zipball_url\":"
                            "\"https://api.github.com/repos/feroxes/pytaichenko/zipball/v2.%d.0\",\"body\":\"%s\","
                            "\"reactions\":{\"url\":\"https://api.github.com/repos/feroxes/pytaichenko/releases/%d/reactions\","
                            "\"total_count\":0,\"+1\":0,\"-1\":0,\"laugh\":0,\"hooray\":0,\"confused\":0,\"heart\":0,"
                            "\"rocket\":0,\"eyes\":0}}",
                            minor, minor, minor, minor, minor, USER_URLS, minor, minor, assets, minor, minor, notes, minor);
}

static void test_ota_pick_realistic_payload(void)
{
    static char big[200 * 1024];
    size_t n = 0;
    big[n++] = '[';
    static char one[24 * 1024];
    for (int minor = 9; minor >= 0; minor--) {
        size_t k = realistic_release(one, sizeof(one), minor, minor % 2 == 0);
        TEST_ASSERT_TRUE(k > 8000 && k < sizeof(one) - 1);
        if (n > 1) {
            big[n++] = ',';
        }
        TEST_ASSERT_TRUE(n + k + 2 < sizeof(big));
        memcpy(big + n, one, k);
        n += k;
    }
    big[n++] = ']';
    TEST_ASSERT_TRUE_MESSAGE(n > 100u * 1024u, "payload is realistic in size");
    TEST_ASSERT_TRUE_MESSAGE(n < 160u * 1024u, "payload fits the device cap");
    pt_release_t r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_ota_pick(big, n, 2, &r));
    TEST_ASSERT_EQUAL_STRING("v2.9.0", r.tag);
    TEST_ASSERT_EQUAL_STRING("2.9.0", r.version);
    TEST_ASSERT_EQUAL_STRING(BASE "v2.9.0/pytaichenko-2.9.0.ota.bin", r.url);
    TEST_ASSERT_EQUAL_INT(PT_ERR_NO_RELEASE, pt_ota_pick(big, n, 3, &r));
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_ota_pick(big, n - 1, 2, &r));
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_ota_pick(big, n / 2, 2, &r));
}

static void test_ota_pick_does_not_depend_on_key_order_and_ignores_text_in_strings(void)
{
    const char *doc =
        "[{\"body\":\"\\\"tag_name\\\":\\\"v2.9.0\\\" ]}\",\"assets\":[{\"browser_download_url\":\"" BASE
        "v2.4.0/pytaichenko-2.4.0.ota.bin\",\"name\":\"pytaichenko-2.4.0.ota.bin\"}],\"prerelease\":false,"
        "\"tag_name\":\"v2.4.0\",\"draft\":false}]";
    pt_release_t r;
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_ota_pick(doc, strlen(doc), 2, &r));
    TEST_ASSERT_EQUAL_STRING("2.4.0", r.version);
    TEST_ASSERT_EQUAL_STRING(BASE "v2.4.0/pytaichenko-2.4.0.ota.bin", r.url);
}

static void image_head(uint8_t *h, const char *project, const char *version)
{
    memset(h, 0, PT_OTA_HEAD_LEN);
    h[0] = 0xE9;
    h[32] = 0x32;
    h[33] = 0x54;
    h[34] = 0xCD;
    h[35] = 0xAB;
    snprintf((char *)h + 32 + 16, 32, "%s", version);
    snprintf((char *)h + 32 + 48, 32, "%s", project);
}

static void test_check_head_accepts_our_image(void)
{
    uint8_t h[PT_OTA_HEAD_LEN];
    image_head(h, "pytaichenko", "2.0.0");
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_ota_check_head(h, sizeof(h)));
    image_head(h, "pytaichenko", "2.7.1-dev");
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_ota_check_head(h, sizeof(h)));
}

static void test_check_head_rejects_foreign_images(void)
{
    uint8_t h[PT_OTA_HEAD_LEN];
    image_head(h, "pytaichenko", "1.0.0");
    TEST_ASSERT_EQUAL_INT(PT_ERR_NOT_OURS, pt_ota_check_head(h, sizeof(h)));
    image_head(h, "pytaichenko", "3.0.0");
    TEST_ASSERT_EQUAL_INT(PT_ERR_NOT_OURS, pt_ota_check_head(h, sizeof(h)));
    image_head(h, "other", "2.0.0");
    TEST_ASSERT_EQUAL_INT(PT_ERR_NOT_OURS, pt_ota_check_head(h, sizeof(h)));
    image_head(h, "pytaichenko2", "2.0.0");
    TEST_ASSERT_EQUAL_INT(PT_ERR_NOT_OURS, pt_ota_check_head(h, sizeof(h)));
    image_head(h, "pytaichenko", "2.0.0");
    h[0] = 0x00;
    TEST_ASSERT_EQUAL_INT(PT_ERR_NOT_OURS, pt_ota_check_head(h, sizeof(h)));
    image_head(h, "pytaichenko", "2.0.0");
    h[33] = 0;
    TEST_ASSERT_EQUAL_INT(PT_ERR_NOT_OURS, pt_ota_check_head(h, sizeof(h)));
    memset(h, 0xFF, sizeof(h));
    TEST_ASSERT_EQUAL_INT(PT_ERR_NOT_OURS, pt_ota_check_head(h, sizeof(h)));
    image_head(h, "pytaichenko", "2.0.0");
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ota_check_head(h, sizeof(h) - 1));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ota_check_head(NULL, sizeof(h)));
}

static void test_check_head_names_without_terminator_are_refused(void)
{
    uint8_t h[PT_OTA_HEAD_LEN];
    image_head(h, "pytaichenko", "2.0.0");
    memset(h + 32 + 48, 'p', 32);
    TEST_ASSERT_EQUAL_INT(PT_ERR_NOT_OURS, pt_ota_check_head(h, sizeof(h)));
    image_head(h, "pytaichenko", "2.0.0");
    memset(h + 32 + 16, '2', 32);
    TEST_ASSERT_EQUAL_INT(PT_ERR_NOT_OURS, pt_ota_check_head(h, sizeof(h)));
}

static void test_check_desc(void)
{
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_ota_check_desc("pytaichenko", "2.0.0"));
    TEST_ASSERT_EQUAL_INT(PT_ERR_NOT_OURS, pt_ota_check_desc("pytaichenko", "1.9.0"));
    TEST_ASSERT_EQUAL_INT(PT_ERR_NOT_OURS, pt_ota_check_desc("pytaiko", "2.0.0"));
    TEST_ASSERT_EQUAL_INT(PT_ERR_NOT_OURS, pt_ota_check_desc(NULL, "2.0.0"));
}

static void test_check_app_desc_needs_terminated_fields(void)
{
    char project[PT_OTA_DESC_FIELD];
    char version[PT_OTA_DESC_FIELD];
    memset(project, 0, sizeof(project));
    memset(version, 0, sizeof(version));
    memcpy(project, "pytaichenko", 11);
    memcpy(version, "2.1.0", 5);
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_ota_check_app_desc(project, version));
    memset(version, '2', sizeof(version));
    TEST_ASSERT_EQUAL_INT(PT_ERR_NOT_OURS, pt_ota_check_app_desc(project, version));
    memset(version, 0, sizeof(version));
    memcpy(version, "2.1.0", 5);
    memset(project, 'p', sizeof(project));
    TEST_ASSERT_EQUAL_INT(PT_ERR_NOT_OURS, pt_ota_check_app_desc(project, version));
    TEST_ASSERT_EQUAL_INT(PT_ERR_NOT_OURS, pt_ota_check_app_desc(NULL, version));
    TEST_ASSERT_EQUAL_INT(PT_ERR_NOT_OURS, pt_ota_check_app_desc(project, NULL));
}

static void test_ota_rejects_version_mismatch(void)
{
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_ota_check_version("2.1.0", "2.1.0"));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ota_check_version("2.0.0", "2.1.0"));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ota_check_version("2.1.0-dev", "2.1.0"));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ota_check_version("2.1.0", "2.1.0-dev"));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ota_check_version("2.1.01", "2.1.0"));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ota_check_version("v2.1.0", "2.1.0"));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ota_check_version("", ""));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ota_check_version(NULL, "2.1.0"));
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, pt_ota_check_version("2.1.0", NULL));
}

static void test_url_ok(void)
{
    TEST_ASSERT_TRUE(pt_ota_url_ok(BASE "v2.0.0/pytaichenko-2.0.0.ota.bin"));
    TEST_ASSERT_FALSE(pt_ota_url_ok("http://github.com/feroxes/pytaichenko/releases/download/v2.0.0/a.bin"));
    TEST_ASSERT_FALSE(pt_ota_url_ok("https://github.com/other/pytaichenko/releases/download/v2.0.0/a.bin"));
    TEST_ASSERT_FALSE(pt_ota_url_ok("https://github.com.evil.example/feroxes/pytaichenko/releases/download/x"));
    TEST_ASSERT_FALSE(pt_ota_url_ok(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_version_cmp);
    RUN_TEST(test_ota_pick_highest_v2);
    RUN_TEST(test_ota_pick_ignores_other_majors);
    RUN_TEST(test_ota_pick_skips_prerelease_and_draft);
    RUN_TEST(test_ota_pick_no_asset_is_error);
    RUN_TEST(test_ota_pick_newest_without_asset_falls_back_to_one_with_asset);
    RUN_TEST(test_ota_pick_refuses_urls_outside_the_repository);
    RUN_TEST(test_ota_pick_bad_input);
    RUN_TEST(test_ota_pick_long_values_do_not_overflow);
    RUN_TEST(test_ota_pick_realistic_payload);
    RUN_TEST(test_ota_pick_does_not_depend_on_key_order_and_ignores_text_in_strings);
    RUN_TEST(test_check_head_accepts_our_image);
    RUN_TEST(test_check_head_rejects_foreign_images);
    RUN_TEST(test_check_head_names_without_terminator_are_refused);
    RUN_TEST(test_check_desc);
    RUN_TEST(test_check_app_desc_needs_terminated_fields);
    RUN_TEST(test_ota_rejects_version_mismatch);
    RUN_TEST(test_url_ok);
    return UNITY_END();
}
