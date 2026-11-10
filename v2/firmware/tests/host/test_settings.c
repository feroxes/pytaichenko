#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "pt_settings.h"
#include "expected_prompts.h"

static pt_settings_t *S;

void setUp(void)
{
    S = malloc(sizeof(*S));
    TEST_ASSERT_NOT_NULL(S);
    pt_settings_defaults(S, "uk");
}

void tearDown(void)
{
    free(S);
}

static pt_err_t patch(pt_settings_t *s, const char *json, char *field, size_t n)
{
    return pt_settings_patch(s, json, strlen(json), field, n);
}

static void test_defaults_match_hub(void)
{
    TEST_ASSERT_EQUAL_STRING("gpt-realtime-2.1-mini", S->openai.model);
    TEST_ASSERT_EQUAL_STRING("marin", S->openai.voice);
    TEST_ASSERT_EQUAL_STRING("low", S->openai.reasoning_effort);
    TEST_ASSERT_EQUAL_STRING("", S->openai.api_key);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, S->openai.vad_threshold);
    TEST_ASSERT_EQUAL_INT(600, S->openai.silence_ms);
    TEST_ASSERT_FALSE(S->openai.log_heard);
    TEST_ASSERT_EQUAL_STRING("uk", S->assistant.language);
    TEST_ASSERT_EQUAL_FLOAT(8.0f, S->timing.first_turn_s);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, S->timing.follow_up_s);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, S->timing.max_speech_s);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, S->timing.response_timeout_s);
    TEST_ASSERT_EQUAL_FLOAT(180.0f, S->timing.answer_max_s);
    TEST_ASSERT_EQUAL_FLOAT(300.0f, S->timing.session_idle_s);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, S->timing.mic_stop_grace_s);
    TEST_ASSERT_EQUAL_FLOAT(0.4f, S->timing.mic_holdoff_s);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, S->timing.ready_check_s);
    TEST_ASSERT_EQUAL_INT(20, S->timing.max_follow_ups);
    TEST_ASSERT_EQUAL_FLOAT(0.7f, S->sound.volume);
    TEST_ASSERT_EQUAL_STRING("hey_jarvis", S->sound.wake_word);
    TEST_ASSERT_EQUAL_FLOAT(12.0f, S->sound.mic_gain_db);
    TEST_ASSERT_EQUAL_FLOAT(200.0f, S->sound.low_cut_hz);
    TEST_ASSERT_EQUAL_FLOAT(6.0f, S->sound.loudness_gain_db);
    for (int i = 0; i < PT_EQ_BANDS; i++) {
        TEST_ASSERT_EQUAL_FLOAT(0.0f, S->sound.eq_db[i]);
    }
    TEST_ASSERT_EQUAL_FLOAT(250.0f, PT_EQ_HZ[0]);
    TEST_ASSERT_EQUAL_FLOAT(600.0f, PT_EQ_HZ[1]);
    TEST_ASSERT_EQUAL_FLOAT(1500.0f, PT_EQ_HZ[2]);
    TEST_ASSERT_EQUAL_FLOAT(3500.0f, PT_EQ_HZ[3]);
    TEST_ASSERT_EQUAL_FLOAT(8000.0f, PT_EQ_HZ[4]);
    TEST_ASSERT_EQUAL_STRING("", S->password_hash);
}

static void test_default_instructions_follow_language(void)
{
    TEST_ASSERT_EQUAL_STRING(EXPECTED_PROMPT_UK, pt_default_instructions("uk"));
    TEST_ASSERT_EQUAL_STRING(EXPECTED_PROMPT_EN, pt_default_instructions("en"));
    TEST_ASSERT_NULL(pt_default_instructions("xx"));
    TEST_ASSERT_EQUAL_STRING(EXPECTED_PROMPT_UK, S->assistant.instructions);
    pt_settings_defaults(S, "en");
    TEST_ASSERT_EQUAL_STRING("en", S->assistant.language);
    TEST_ASSERT_EQUAL_STRING(EXPECTED_PROMPT_EN, S->assistant.instructions);
    pt_settings_defaults(S, "xx");
    TEST_ASSERT_EQUAL_STRING("uk", S->assistant.language);
    TEST_ASSERT_TRUE(strlen(EXPECTED_PROMPT_UK) <= 8000);
}

static const struct {
    const char *code;
    const char *name;
} MORE_LANGUAGES[] = {
    {"es", "Spanish"}, {"fr", "French"}, {"de", "German"}, {"it", "Italian"}, {"pt", "Portuguese"},
    {"pl", "Polish"}, {"nl", "Dutch"}, {"tr", "Turkish"},
};

static void test_more_languages_get_english_instructions_naming_the_language(void)
{
    char want[64];
    for (size_t i = 0; i < sizeof(MORE_LANGUAGES) / sizeof(MORE_LANGUAGES[0]); i++) {
        const char *p = pt_default_instructions(MORE_LANGUAGES[i].code);
        TEST_ASSERT_NOT_NULL_MESSAGE(p, MORE_LANGUAGES[i].code);
        snprintf(want, sizeof(want), "Answer in %s straight to the point", MORE_LANGUAGES[i].name);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(p, want), MORE_LANGUAGES[i].code);
        TEST_ASSERT_NOT_NULL(strstr(p, "end_conversation"));
        TEST_ASSERT_TRUE(strlen(p) < sizeof(S->assistant.instructions));
        pt_settings_defaults(S, MORE_LANGUAGES[i].code);
        TEST_ASSERT_EQUAL_STRING(MORE_LANGUAGES[i].code, S->assistant.language);
        TEST_ASSERT_EQUAL_STRING(p, S->assistant.instructions);
    }
    TEST_ASSERT_NULL(pt_default_instructions("ru"));
    TEST_ASSERT_NULL(pt_default_instructions("zh"));
    TEST_ASSERT_NULL(pt_default_instructions(""));
}

static void test_json_round_trip_keeps_secrets(void)
{
    strcpy(S->openai.api_key, "sk-test-fake-key");
    strcpy(S->password_hash, "pbkdf2$fake$hash");
    strcpy(S->openai.voice, "cedar");
    S->openai.reasoning_effort[0] = '\0';
    S->openai.vad_threshold = 0.65f;
    S->openai.log_heard = true;
    S->timing.mic_holdoff_s = 0.35f;
    S->timing.max_follow_ups = 7;
    strcpy(S->assistant.language, "en");
    strcpy(S->assistant.instructions, "Say \"hi\" \\ then\nstop. Привіт");
    strcpy(S->sound.wake_word, "okay_nabu");
    S->sound.volume = 0.3f;
    S->sound.eq_db[2] = -3.5f;
    S->sound.eq_db[4] = 12.0f;

    char *json = pt_settings_to_json(S);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "sk-test-fake-key"));
    TEST_ASSERT_NOT_NULL(strstr(json, "pbkdf2$fake$hash"));

    pt_settings_t *t = malloc(sizeof(*t));
    TEST_ASSERT_NOT_NULL(t);
    pt_settings_defaults(t, "uk");
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_settings_from_json(t, json, strlen(json)));
    free(json);

    TEST_ASSERT_EQUAL_STRING("sk-test-fake-key", t->openai.api_key);
    TEST_ASSERT_EQUAL_STRING("pbkdf2$fake$hash", t->password_hash);
    TEST_ASSERT_EQUAL_STRING("cedar", t->openai.voice);
    TEST_ASSERT_EQUAL_STRING("", t->openai.reasoning_effort);
    TEST_ASSERT_EQUAL_FLOAT(0.65f, t->openai.vad_threshold);
    TEST_ASSERT_TRUE(t->openai.log_heard);
    TEST_ASSERT_EQUAL_FLOAT(0.35f, t->timing.mic_holdoff_s);
    TEST_ASSERT_EQUAL_INT(7, t->timing.max_follow_ups);
    TEST_ASSERT_EQUAL_STRING("en", t->assistant.language);
    TEST_ASSERT_EQUAL_STRING("Say \"hi\" \\ then\nstop. Привіт", t->assistant.instructions);
    TEST_ASSERT_EQUAL_STRING("okay_nabu", t->sound.wake_word);
    TEST_ASSERT_EQUAL_FLOAT(0.3f, t->sound.volume);
    TEST_ASSERT_EQUAL_FLOAT(-3.5f, t->sound.eq_db[2]);
    TEST_ASSERT_EQUAL_FLOAT(12.0f, t->sound.eq_db[4]);
    TEST_ASSERT_EQUAL_INT(0, memcmp(&t->timing, &S->timing, sizeof(S->timing)));
    free(t);
}

static void test_json_numbers_are_short(void)
{
    char *json = pt_settings_to_json(S);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"volume\":0.7,"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"mic_holdoff_s\":0.4,"));
    free(json);
}

static void test_public_json_hides_key_and_hash(void)
{
    strcpy(S->openai.api_key, "sk-test-fake-key");
    strcpy(S->password_hash, "pbkdf2$fake$hash");
    char *json = pt_settings_public_json(S);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NULL(strstr(json, "sk-test-fake-key"));
    TEST_ASSERT_NULL(strstr(json, "pbkdf2$fake$hash"));
    TEST_ASSERT_NULL(strstr(json, "password_hash"));
    TEST_ASSERT_NULL(strstr(json, "\"api_key\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"api_key_set\":true"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"sound\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"timing\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"assistant\""));
    free(json);

    S->openai.api_key[0] = '\0';
    json = pt_settings_public_json(S);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"api_key_set\":false"));
    free(json);
}

static void test_from_json_truncated_leaves_settings_unchanged(void)
{
    strcpy(S->openai.api_key, "sk-test-fake-key");
    char *json = pt_settings_to_json(S);
    TEST_ASSERT_NOT_NULL(json);
    pt_settings_t *t = malloc(sizeof(*t));
    TEST_ASSERT_NOT_NULL(t);
    pt_settings_defaults(t, "en");
    strcpy(t->openai.model, "keep-me");
    pt_settings_t *before = malloc(sizeof(*before));
    memcpy(before, t, sizeof(*t));
    size_t len = strlen(json);
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_settings_from_json(t, json, len / 2));
    TEST_ASSERT_EQUAL_INT(0, memcmp(before, t, sizeof(*t)));
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_settings_from_json(t, "", 0));
    TEST_ASSERT_EQUAL_INT(0, memcmp(before, t, sizeof(*t)));
    free(json);
    free(before);
    free(t);
}

static void test_from_json_missing_fields_take_defaults(void)
{
    pt_settings_t *t = malloc(sizeof(*t));
    TEST_ASSERT_NOT_NULL(t);
    strcpy(t->openai.model, "garbage");
    const char *json = "{\"openai\":{\"voice\":\"cedar\"}}";
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_settings_from_json(t, json, strlen(json)));
    TEST_ASSERT_EQUAL_STRING("cedar", t->openai.voice);
    TEST_ASSERT_EQUAL_STRING("gpt-realtime-2.1-mini", t->openai.model);
    TEST_ASSERT_EQUAL_STRING("uk", t->assistant.language);
    TEST_ASSERT_EQUAL_STRING(EXPECTED_PROMPT_UK, t->assistant.instructions);
    TEST_ASSERT_EQUAL_FLOAT(0.7f, t->sound.volume);
    TEST_ASSERT_EQUAL_INT(20, t->timing.max_follow_ups);

    json = "{\"assistant\":{\"language\":\"en\"}}";
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_settings_from_json(t, json, strlen(json)));
    TEST_ASSERT_EQUAL_STRING(EXPECTED_PROMPT_EN, t->assistant.instructions);

    json = "{}";
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_settings_from_json(t, json, strlen(json)));
    TEST_ASSERT_EQUAL_STRING("uk", t->assistant.language);
    free(t);
}

static void test_from_json_rejects_bad_content(void)
{
    const char *bad[] = {
        "[1]", "{\"openai\":{\"nope\":1}}", "{\"nope\":1}", "{\"version\":2}",
        "{\"openai\":{\"vad_threshold\":2}}", "{} trailing",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        pt_err_t e = pt_settings_from_json(S, bad[i], strlen(bad[i]));
        TEST_ASSERT_TRUE_MESSAGE(e == PT_ERR_INVALID || e == PT_ERR_PARSE, bad[i]);
    }
    TEST_ASSERT_EQUAL_STRING("gpt-realtime-2.1-mini", S->openai.model);
}

static void test_patch_unknown_key_names_field(void)
{
    char field[64] = "";
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, patch(S, "{\"openai\":{\"modle\":\"x\"}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("openai.modle", field);
    TEST_ASSERT_EQUAL_STRING("gpt-realtime-2.1-mini", S->openai.model);

    field[0] = '\0';
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, patch(S, "{\"devices\":[]}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("devices", field);

    field[0] = '\0';
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, patch(S, "{\"password_hash\":\"x\"}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("password_hash", field);

    field[0] = '\0';
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, patch(S, "{\"sound\":{\"bass\":1}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("sound.bass", field);

    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, patch(S, "{\"sound\":{\"bass\":1}}", NULL, 0));
}

static void test_patch_invalid_json_is_parse_error(void)
{
    char field[64] = "x";
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, patch(S, "{\"openai\":", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("", field);
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, patch(S, "[]", field, sizeof(field)));
}

static void test_patch_out_of_range_names_field_and_changes_nothing(void)
{
    pt_settings_t *before = malloc(sizeof(*before));
    memcpy(before, S, sizeof(*S));
    char field[64] = "";
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID,
        patch(S, "{\"openai\":{\"voice\":\"cedar\"},\"timing\":{\"mic_stop_grace_s\":0.2}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("timing.mic_stop_grace_s", field);
    TEST_ASSERT_EQUAL_INT(0, memcmp(before, S, sizeof(*S)));

    struct { const char *json, *name; } cases[] = {
        {"{\"openai\":{\"vad_threshold\":1.5}}", "openai.vad_threshold"},
        {"{\"openai\":{\"vad_threshold\":-0.1}}", "openai.vad_threshold"},
        {"{\"openai\":{\"silence_ms\":0}}", "openai.silence_ms"},
        {"{\"openai\":{\"silence_ms\":1.5}}", "openai.silence_ms"},
        {"{\"openai\":{\"silence_ms\":\"600\"}}", "openai.silence_ms"},
        {"{\"openai\":{\"log_heard\":1}}", "openai.log_heard"},
        {"{\"openai\":{\"model\":\"  \"}}", "openai.model"},
        {"{\"openai\":{\"voice\":\"01234567890123456789012345678901\"}}", "openai.voice"},
        {"{\"timing\":{\"follow_up_s\":0}}", "timing.follow_up_s"},
        {"{\"timing\":{\"mic_holdoff_s\":-1}}", "timing.mic_holdoff_s"},
        {"{\"timing\":{\"max_follow_ups\":-1}}", "timing.max_follow_ups"},
        {"{\"timing\":{\"max_follow_ups\":2.5}}", "timing.max_follow_ups"},
        {"{\"assistant\":{\"language\":\"ru\"}}", "assistant.language"},
        {"{\"assistant\":{\"language\":\"xx\"}}", "assistant.language"},
        {"{\"sound\":{\"volume\":1.1}}", "sound.volume"},
        {"{\"sound\":{\"mic_gain_db\":31}}", "sound.mic_gain_db"},
        {"{\"sound\":{\"low_cut_hz\":79}}", "sound.low_cut_hz"},
        {"{\"sound\":{\"low_cut_hz\":401}}", "sound.low_cut_hz"},
        {"{\"sound\":{\"loudness_gain_db\":13}}", "sound.loudness_gain_db"},
        {"{\"sound\":{\"eq_db\":[0,0,0,0,13]}}", "sound.eq_db"},
        {"{\"sound\":{\"eq_db\":[0,0,0,0]}}", "sound.eq_db"},
        {"{\"sound\":{\"eq_db\":[0,0,0,0,\"a\"]}}", "sound.eq_db"},
        {"{\"openai\":5}", "openai"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        field[0] = '\0';
        TEST_ASSERT_EQUAL_INT_MESSAGE(PT_ERR_INVALID, patch(S, cases[i].json, field, sizeof(field)), cases[i].json);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(cases[i].name, field, cases[i].json);
        TEST_ASSERT_EQUAL_INT(0, memcmp(before, S, sizeof(*S)));
    }
    free(before);
}

static void test_patch_applies_valid_fields(void)
{
    char field[64];
    const char *json =
        "{\"openai\":{\"model\":\" gpt-x \\n\",\"reasoning_effort\":null,\"vad_threshold\":0.8,\"log_heard\":true},"
        "\"timing\":{\"mic_holdoff_s\":0,\"max_follow_ups\":0},"
        "\"sound\":{\"volume\":1,\"wake_word\":\"hey_mycroft\",\"eq_db\":[1,-2,3,-4,5]}}";
    TEST_ASSERT_EQUAL_INT(PT_OK, patch(S, json, field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("gpt-x", S->openai.model);
    TEST_ASSERT_EQUAL_STRING("", S->openai.reasoning_effort);
    TEST_ASSERT_EQUAL_FLOAT(0.8f, S->openai.vad_threshold);
    TEST_ASSERT_TRUE(S->openai.log_heard);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, S->timing.mic_holdoff_s);
    TEST_ASSERT_EQUAL_INT(0, S->timing.max_follow_ups);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, S->sound.volume);
    TEST_ASSERT_EQUAL_STRING("hey_mycroft", S->sound.wake_word);
    TEST_ASSERT_EQUAL_FLOAT(-4.0f, S->sound.eq_db[3]);
    TEST_ASSERT_EQUAL_STRING("marin", S->openai.voice);
}

static void test_patch_ready_check_s(void)
{
    char field[64] = "";
    TEST_ASSERT_EQUAL_INT(PT_OK, patch(S, "{\"timing\":{\"ready_check_s\":3.5}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_FLOAT(3.5f, S->timing.ready_check_s);
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, patch(S, "{\"timing\":{\"ready_check_s\":0}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("timing.ready_check_s", field);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, S->timing.ready_check_s);
    pt_settings_t *b = malloc(sizeof(*b));
    memcpy(b, S, sizeof(*S));
    b->timing.ready_check_s = 2.0f;
    TEST_ASSERT_TRUE(pt_settings_session_changed(S, b));
    free(b);
    char *json = pt_settings_public_json(S);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"ready_check_s\":3.5"));
    free(json);
}

static void test_patch_empty_key_keeps_old_key(void)
{
    strcpy(S->openai.api_key, "sk-test-fake-key");
    char field[64];
    TEST_ASSERT_EQUAL_INT(PT_OK, patch(S, "{\"openai\":{\"api_key\":\"\"}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("sk-test-fake-key", S->openai.api_key);
    TEST_ASSERT_EQUAL_INT(PT_OK, patch(S, "{\"openai\":{\"api_key\":\" \\n \"}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("sk-test-fake-key", S->openai.api_key);
}

static void test_patch_trims_pasted_key(void)
{
    char field[64];
    TEST_ASSERT_EQUAL_INT(PT_OK, patch(S, "{\"openai\":{\"api_key\":\"  sk-test-fake-new\\r\\n\"}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("sk-test-fake-new", S->openai.api_key);
    char tooLong[400];
    memset(tooLong, 'k', sizeof(tooLong));
    tooLong[sizeof(tooLong) - 1] = '\0';
    char json[500];
    snprintf(json, sizeof(json), "{\"openai\":{\"api_key\":\"%s\"}}", tooLong);
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, patch(S, json, field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("openai.api_key", field);
    TEST_ASSERT_EQUAL_STRING("sk-test-fake-new", S->openai.api_key);
}

static void test_instructions_over_limit_rejected_not_truncated(void)
{
    static char json[9000];
    size_t pos = (size_t)snprintf(json, sizeof(json), "{\"assistant\":{\"instructions\":\"");
    for (int i = 0; i < 4000; i++) {
        memcpy(json + pos, "\xD0\xB0", 2);
        pos += 2;
    }
    char field[64] = "";
    strcpy(json + pos, "\"}}");
    TEST_ASSERT_EQUAL_INT(PT_OK, patch(S, json, field, sizeof(field)));
    TEST_ASSERT_EQUAL_UINT(8000, strlen(S->assistant.instructions));

    pt_settings_defaults(S, "uk");
    strcpy(json + pos, "x\"}}");
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, patch(S, json, field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("assistant.instructions", field);
    TEST_ASSERT_EQUAL_STRING(EXPECTED_PROMPT_UK, S->assistant.instructions);
}

static void test_instructions_follow_every_language_change(void)
{
    char field[64];
    TEST_ASSERT_EQUAL_INT(PT_OK, patch(S, "{\"assistant\":{\"language\":\"en\"}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING(EXPECTED_PROMPT_EN, S->assistant.instructions);

    TEST_ASSERT_EQUAL_INT(PT_OK, patch(S, "{\"assistant\":{\"instructions\":\"Own text\"}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_INT(PT_OK, patch(S, "{\"assistant\":{\"language\":\"en\"}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("Own text", S->assistant.instructions);
    TEST_ASSERT_EQUAL_INT(PT_OK, patch(S, "{\"assistant\":{\"language\":\"uk\"}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING(EXPECTED_PROMPT_UK, S->assistant.instructions);

    TEST_ASSERT_EQUAL_INT(PT_OK, patch(S, "{\"assistant\":{\"instructions\":\"Свій текст\"}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_INT(PT_OK, patch(S, "{\"assistant\":{\"language\":\"pl\"}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING(pt_default_instructions("pl"), S->assistant.instructions);

    TEST_ASSERT_EQUAL_INT(PT_OK, patch(S, "{\"assistant\":{\"language\":\"uk\",\"instructions\":\"Own text\"}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("Own text", S->assistant.instructions);

    TEST_ASSERT_EQUAL_INT(PT_OK, patch(S, "{\"assistant\":{\"instructions\":null}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING(EXPECTED_PROMPT_UK, S->assistant.instructions);

    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, patch(S, "{\"assistant\":{\"instructions\":\"  \\n\"}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("assistant.instructions", field);
}

static void test_wake_word_must_be_known(void)
{
    char field[64] = "";
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, patch(S, "{\"sound\":{\"wake_word\":\"alexa\"}}", field, sizeof(field)));
    TEST_ASSERT_EQUAL_STRING("sound.wake_word", field);
    TEST_ASSERT_EQUAL_STRING("hey_jarvis", S->sound.wake_word);
    const char *ok[] = {"hey_jarvis", "okay_nabu", "hey_mycroft"};
    for (int i = 0; i < 3; i++) {
        char json[96];
        snprintf(json, sizeof(json), "{\"sound\":{\"wake_word\":\"%s\"}}", ok[i]);
        TEST_ASSERT_EQUAL_INT(PT_OK, patch(S, json, field, sizeof(field)));
        TEST_ASSERT_EQUAL_STRING(ok[i], S->sound.wake_word);
    }
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID, patch(S, "{\"sound\":{\"wake_word\":5}}", field, sizeof(field)));
}

static void test_session_changed_by_model_not_by_volume(void)
{
    pt_settings_t *b = malloc(sizeof(*b));
    memcpy(b, S, sizeof(*S));
    TEST_ASSERT_FALSE(pt_settings_session_changed(S, b));

    b->sound.volume = 0.2f;
    strcpy(b->sound.wake_word, "okay_nabu");
    b->sound.eq_db[1] = 4.0f;
    b->sound.mic_gain_db = 3.0f;
    strcpy(b->password_hash, "x");
    TEST_ASSERT_FALSE(pt_settings_session_changed(S, b));

    memcpy(b, S, sizeof(*S));
    strcpy(b->openai.model, "other");
    TEST_ASSERT_TRUE(pt_settings_session_changed(S, b));

    memcpy(b, S, sizeof(*S));
    strcpy(b->openai.api_key, "sk-test-fake-key");
    TEST_ASSERT_TRUE(pt_settings_session_changed(S, b));

    memcpy(b, S, sizeof(*S));
    b->timing.follow_up_s = 11.0f;
    TEST_ASSERT_TRUE(pt_settings_session_changed(S, b));

    memcpy(b, S, sizeof(*S));
    b->timing.max_follow_ups = 3;
    TEST_ASSERT_TRUE(pt_settings_session_changed(S, b));

    memcpy(b, S, sizeof(*S));
    strcat(b->assistant.instructions, " more");
    TEST_ASSERT_TRUE(pt_settings_session_changed(S, b));

    memcpy(b, S, sizeof(*S));
    b->openai.log_heard = true;
    TEST_ASSERT_TRUE(pt_settings_session_changed(S, b));
    free(b);
}

static pt_err_t load_stored(pt_settings_t *s, const char *json, int *dropped)
{
    return pt_settings_load_stored(s, json, strlen(json), dropped);
}

static void test_stored_unknown_keys_keep_the_rest(void)
{
    const char *json =
        "{\"version\":1,\"future\":{\"a\":1},\"openai\":{\"api_key\":\"sk-test-fake-key\",\"voice\":\"cedar\","
        "\"newfield\":true},\"password_hash\":\"pbkdf2$abc\",\"sound\":{\"volume\":0.4,\"extra\":1}}";
    int dropped = -1;
    TEST_ASSERT_EQUAL_INT(PT_OK, load_stored(S, json, &dropped));
    TEST_ASSERT_EQUAL_INT(0, dropped);
    TEST_ASSERT_EQUAL_STRING("sk-test-fake-key", S->openai.api_key);
    TEST_ASSERT_EQUAL_STRING("cedar", S->openai.voice);
    TEST_ASSERT_EQUAL_STRING("pbkdf2$abc", S->password_hash);
    TEST_ASSERT_EQUAL_FLOAT(0.4f, S->sound.volume);
}

static void test_stored_out_of_range_resets_only_that_field(void)
{
    const char *json =
        "{\"version\":1,\"openai\":{\"voice\":\"cedar\",\"vad_threshold\":7,\"silence_ms\":-3},"
        "\"timing\":{\"first_turn_s\":12,\"mic_stop_grace_s\":0.1},\"sound\":{\"wake_word\":\"nope\",\"volume\":0.3,"
        "\"eq_db\":[1,2,3]}}";
    int dropped = 0;
    TEST_ASSERT_EQUAL_INT(PT_OK, load_stored(S, json, &dropped));
    TEST_ASSERT_EQUAL_INT(5, dropped);
    TEST_ASSERT_EQUAL_STRING("cedar", S->openai.voice);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, S->openai.vad_threshold);
    TEST_ASSERT_EQUAL_INT(600, S->openai.silence_ms);
    TEST_ASSERT_EQUAL_FLOAT(12.0f, S->timing.first_turn_s);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, S->timing.mic_stop_grace_s);
    TEST_ASSERT_EQUAL_STRING("hey_jarvis", S->sound.wake_word);
    TEST_ASSERT_EQUAL_FLOAT(0.3f, S->sound.volume);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, S->sound.eq_db[0]);
}

static void test_stored_version_mismatch_still_loads(void)
{
    const char *json = "{\"version\":9,\"openai\":{\"api_key\":\"sk-test-fake-key\"},\"password_hash\":\"h\"}";
    int dropped = 0;
    TEST_ASSERT_EQUAL_INT(PT_OK, load_stored(S, json, &dropped));
    TEST_ASSERT_EQUAL_INT(0, dropped);
    TEST_ASSERT_EQUAL_STRING("sk-test-fake-key", S->openai.api_key);
    TEST_ASSERT_EQUAL_STRING("h", S->password_hash);
    json = "{\"version\":\"x\",\"password_hash\":\"h2\"}";
    TEST_ASSERT_EQUAL_INT(PT_OK, load_stored(S, json, NULL));
    TEST_ASSERT_EQUAL_STRING("h2", S->password_hash);
}

static void test_stored_secrets_survive_broken_sections(void)
{
    const char *json =
        "{\"version\":1,\"openai\":{\"api_key\":\"sk-test-fake-key\",\"model\":5,\"vad_threshold\":\"x\"},"
        "\"assistant\":{\"language\":\"xx\",\"instructions\":7},\"timing\":[1],\"sound\":{\"volume\":9},"
        "\"password_hash\":\"pbkdf2$abc\"}";
    int dropped = 0;
    TEST_ASSERT_EQUAL_INT(PT_OK, load_stored(S, json, &dropped));
    TEST_ASSERT_EQUAL_INT(6, dropped);
    TEST_ASSERT_EQUAL_STRING("sk-test-fake-key", S->openai.api_key);
    TEST_ASSERT_EQUAL_STRING("pbkdf2$abc", S->password_hash);
    TEST_ASSERT_EQUAL_STRING("gpt-realtime-2.1-mini", S->openai.model);
    TEST_ASSERT_EQUAL_STRING("uk", S->assistant.language);
    TEST_ASSERT_EQUAL_STRING(EXPECTED_PROMPT_UK, S->assistant.instructions);
    TEST_ASSERT_EQUAL_FLOAT(0.7f, S->sound.volume);
}

static void test_stored_language_taken_when_valid(void)
{
    const char *json = "{\"assistant\":{\"language\":\"en\"},\"password_hash\":\"h\"}";
    TEST_ASSERT_EQUAL_INT(PT_OK, load_stored(S, json, NULL));
    TEST_ASSERT_EQUAL_STRING("en", S->assistant.language);
    TEST_ASSERT_EQUAL_STRING(EXPECTED_PROMPT_EN, S->assistant.instructions);
}

static void test_stored_non_object_and_truncated_are_errors(void)
{
    strcpy(S->openai.model, "keep-me");
    int dropped = 5;
    TEST_ASSERT_NOT_EQUAL(PT_OK, load_stored(S, "[1]", &dropped));
    TEST_ASSERT_NOT_EQUAL(PT_OK, load_stored(S, "\"x\"", &dropped));
    TEST_ASSERT_NOT_EQUAL(PT_OK, load_stored(S, "", &dropped));
    TEST_ASSERT_NOT_EQUAL(PT_OK, load_stored(S, "{} trailing", &dropped));
    strcpy(S->openai.api_key, "sk-test-fake-key");
    char *json = pt_settings_to_json(S);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL_INT(PT_ERR_PARSE, pt_settings_load_stored(S, json, strlen(json) / 2, &dropped));
    TEST_ASSERT_EQUAL_STRING("keep-me", S->openai.model);
    free(json);
}

static void test_stored_round_trip_has_nothing_dropped(void)
{
    strcpy(S->openai.api_key, "sk-test-fake-key");
    strcpy(S->password_hash, "pbkdf2$abc");
    S->sound.eq_db[2] = 3.5f;
    char *json = pt_settings_to_json(S);
    TEST_ASSERT_NOT_NULL(json);
    pt_settings_t *t = malloc(sizeof(*t));
    TEST_ASSERT_NOT_NULL(t);
    int dropped = -1;
    TEST_ASSERT_EQUAL_INT(PT_OK, pt_settings_load_stored(t, json, strlen(json), &dropped));
    TEST_ASSERT_EQUAL_INT(0, dropped);
    TEST_ASSERT_EQUAL_INT(0, memcmp(S, t, sizeof(*t)));
    free(t);
    free(json);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_match_hub);
    RUN_TEST(test_default_instructions_follow_language);
    RUN_TEST(test_more_languages_get_english_instructions_naming_the_language);
    RUN_TEST(test_json_round_trip_keeps_secrets);
    RUN_TEST(test_json_numbers_are_short);
    RUN_TEST(test_public_json_hides_key_and_hash);
    RUN_TEST(test_from_json_truncated_leaves_settings_unchanged);
    RUN_TEST(test_from_json_missing_fields_take_defaults);
    RUN_TEST(test_from_json_rejects_bad_content);
    RUN_TEST(test_patch_unknown_key_names_field);
    RUN_TEST(test_patch_invalid_json_is_parse_error);
    RUN_TEST(test_patch_out_of_range_names_field_and_changes_nothing);
    RUN_TEST(test_patch_applies_valid_fields);
    RUN_TEST(test_patch_ready_check_s);
    RUN_TEST(test_patch_empty_key_keeps_old_key);
    RUN_TEST(test_patch_trims_pasted_key);
    RUN_TEST(test_instructions_over_limit_rejected_not_truncated);
    RUN_TEST(test_instructions_follow_every_language_change);
    RUN_TEST(test_wake_word_must_be_known);
    RUN_TEST(test_session_changed_by_model_not_by_volume);
    RUN_TEST(test_stored_unknown_keys_keep_the_rest);
    RUN_TEST(test_stored_out_of_range_resets_only_that_field);
    RUN_TEST(test_stored_version_mismatch_still_loads);
    RUN_TEST(test_stored_secrets_survive_broken_sections);
    RUN_TEST(test_stored_language_taken_when_valid);
    RUN_TEST(test_stored_non_object_and_truncated_are_errors);
    RUN_TEST(test_stored_round_trip_has_nothing_dropped);
    return UNITY_END();
}
