#include "unity.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "pt_tools.h"

void setUp(void) {}
void tearDown(void) {}

static const char *const LANGUAGES[] = {"uk", "en"};
#define N_LANGUAGES (sizeof(LANGUAGES) / sizeof(LANGUAGES[0]))

static const char END_DESCRIPTION_UK[] =
        "Завершити розмову: мікрофон вимикається, пристрій знову чекає ключову фразу. "
        "Викликай, коли людина прощається чи просить зупинитися, або коли репліка явно не до тебе "
        "(люди говорять між собою).";

static const char END_DESCRIPTION_EN[] =
        "End the conversation: the microphone turns off and the device waits for its wake word again. "
        "Call it when the person says goodbye or asks to stop, or when what you hear is clearly not meant for you "
        "(people talking to each other).";

static cJSON *tools_for(const char *language)
{
    char *text = pt_tools_json(language);
    TEST_ASSERT_NOT_NULL(text);
    cJSON *list = cJSON_Parse(text);
    free(text);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_TRUE(cJSON_IsArray(list));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(list));
    return list;
}

static const char *str_of(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsString(item), key);
    return item->valuestring;
}

static void assert_number(float expected, const cJSON *item)
{
    TEST_ASSERT_TRUE(cJSON_IsNumber(item));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, expected, (float)item->valuedouble);
}

static void assert_volume(float expected, float current, const char *arguments)
{
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0001f, expected, pt_tools_next_volume(current, arguments, NULL, 0), arguments);
}

static void assert_rejected(const char *arguments)
{
    TEST_ASSERT_TRUE_MESSAGE(pt_tools_next_volume(0.5f, arguments, NULL, 0) < 0.0f, arguments ? arguments : "(null)");
}

void test_volume_tool_schema(void)
{
    cJSON *list = tools_for("uk");
    const cJSON *tool = cJSON_GetArrayItem(list, 0);
    TEST_ASSERT_EQUAL_STRING("function", str_of(tool, "type"));
    TEST_ASSERT_EQUAL_STRING("set_volume", str_of(tool, "name"));
    TEST_ASSERT_TRUE(strlen(str_of(tool, "description")) > 0);
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(tool, "parameters");
    TEST_ASSERT_EQUAL_STRING("object", str_of(params, "type"));
    const cJSON *required = cJSON_GetObjectItemCaseSensitive(params, "required");
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetArraySize(required));
    TEST_ASSERT_EQUAL_STRING("action", cJSON_GetArrayItem(required, 0)->valuestring);
    const cJSON *props = cJSON_GetObjectItemCaseSensitive(params, "properties");
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(props, "action");
    const cJSON *values = cJSON_GetObjectItemCaseSensitive(action, "enum");
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetArraySize(values));
    TEST_ASSERT_EQUAL_STRING("up", cJSON_GetArrayItem(values, 0)->valuestring);
    TEST_ASSERT_EQUAL_STRING("down", cJSON_GetArrayItem(values, 1)->valuestring);
    TEST_ASSERT_EQUAL_STRING("set", cJSON_GetArrayItem(values, 2)->valuestring);
    const cJSON *level = cJSON_GetObjectItemCaseSensitive(props, "level");
    TEST_ASSERT_EQUAL_STRING("integer", str_of(level, "type"));
    assert_number(0, cJSON_GetObjectItemCaseSensitive(level, "minimum"));
    assert_number(100, cJSON_GetObjectItemCaseSensitive(level, "maximum"));
    cJSON_Delete(list);
}

void test_end_tool_schema(void)
{
    const char *const descriptions[N_LANGUAGES] = {END_DESCRIPTION_UK, END_DESCRIPTION_EN};
    for (size_t i = 0; i < N_LANGUAGES; i++) {
        cJSON *list = tools_for(LANGUAGES[i]);
        const cJSON *tool = cJSON_GetArrayItem(list, 1);
        TEST_ASSERT_EQUAL_STRING("function", str_of(tool, "type"));
        TEST_ASSERT_EQUAL_STRING("end_conversation", str_of(tool, "name"));
        TEST_ASSERT_EQUAL_STRING(descriptions[i], str_of(tool, "description"));
        cJSON *expected = cJSON_Parse("{\"type\":\"object\",\"properties\":{},\"required\":[]}");
        TEST_ASSERT_TRUE(cJSON_Compare(cJSON_GetObjectItemCaseSensitive(tool, "parameters"), expected, 1));
        cJSON_Delete(expected);
        cJSON_Delete(list);
    }
}

void test_session_tools_follow_the_language(void)
{
    cJSON *uk = tools_for("uk");
    cJSON *en = tools_for("en");
    for (size_t i = 0; i < N_LANGUAGES; i++) {
        cJSON *list = i == 0 ? uk : en;
        TEST_ASSERT_EQUAL_STRING("set_volume", str_of(cJSON_GetArrayItem(list, 0), "name"));
        TEST_ASSERT_EQUAL_STRING("end_conversation", str_of(cJSON_GetArrayItem(list, 1), "name"));
    }
    TEST_ASSERT_TRUE(cJSON_Compare(cJSON_GetArrayItem(uk, 0), cJSON_GetArrayItem(en, 0), 1));
    TEST_ASSERT_FALSE(cJSON_Compare(cJSON_GetArrayItem(uk, 1), cJSON_GetArrayItem(en, 1), 1));
    cJSON_Delete(uk);
    cJSON_Delete(en);
}

void test_end_tool_rejects_unknown_language(void)
{
    TEST_ASSERT_NULL(pt_tools_json("ru"));
    TEST_ASSERT_NULL(pt_tools_json("xx"));
    TEST_ASSERT_NULL(pt_tools_json(""));
    TEST_ASSERT_NULL(pt_tools_json(NULL));
}

void test_more_languages_get_the_english_end_tool(void)
{
    cJSON *en = tools_for("en");
    static const char *const codes[] = {"es", "de", "pl", "tr"};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        cJSON *other = tools_for(codes[i]);
        TEST_ASSERT_TRUE_MESSAGE(cJSON_Compare(en, other, 1), codes[i]);
        cJSON_Delete(other);
    }
    cJSON_Delete(en);
}

void test_next_volume(void)
{
    assert_volume(0.55f, 0.7f, "{\"action\":\"down\"}");
    assert_volume(1.0f, 0.95f, "{\"action\":\"up\"}");
    assert_volume(0.5f, 0.7f, "{\"action\":\"set\",\"level\":50}");
    assert_volume(0.1f, 0.7f, "{\"action\":\"set\",\"level\":0}");
    assert_rejected("{\"action\":\"louder\"}");
}

void test_next_volume_clamps_and_rounds(void)
{
    assert_volume(0.55f, 0.4f, "{\"action\":\"up\"}");
    assert_volume(0.1f, 0.1f, "{\"action\":\"down\"}");
    assert_volume(0.1f, 0.2f, "{\"action\":\"down\"}");
    assert_volume(1.0f, 0.5f, "{\"action\":\"set\",\"level\":100}");
    assert_volume(1.0f, 0.5f, "{\"action\":\"set\",\"level\":250}");
    assert_volume(0.1f, 0.5f, "{\"action\":\"set\",\"level\":-5}");
    assert_volume(0.33f, 0.5f, "{\"action\":\"set\",\"level\":33}");
    assert_volume(0.42f, 0.5f, "{\"action\":\"set\",\"level\":42.4}");
    assert_volume(0.65f, 0.5f, "{\"action\":\"up\",\"level\":10}");
}

void test_next_volume_rejects_bad_arguments(void)
{
    static const char *const bad[] = {
        "",
        "not json",
        "[]",
        "\"up\"",
        "{}",
        "{\"action\":null}",
        "{\"action\":\"UP\"}",
        "{\"action\":\"set\"}",
        "{\"action\":\"set\",\"level\":null}",
        "{\"action\":\"set\",\"level\":\"50\"}",
        "{\"action\":\"set\",\"level\":true}",
        "{\"action\":\"set\",\"level\":NaN}",
        "{\"action\":\"set\",\"level\":1e999}",
        "{\"action\":\"up\"} trailing",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        assert_rejected(bad[i]);
    }
    assert_rejected(NULL);
}

static void assert_why(const char *expected, const char *arguments)
{
    char why[96];
    memset(why, 'x', sizeof why);
    TEST_ASSERT_TRUE(pt_tools_next_volume(0.5f, arguments, why, sizeof why) < 0.0f);
    TEST_ASSERT_EQUAL_STRING(expected, why);
}

void test_next_volume_explains_why_it_rejected(void)
{
    assert_why("set_volume arguments are not valid JSON", "not json");
    assert_why("set_volume arguments are not valid JSON", "");
    assert_why("set_volume arguments are not valid JSON", NULL);
    assert_why("set_volume arguments must be a JSON object", "[]");
    assert_why("set_volume arguments must be a JSON object", "\"up\"");
    assert_why("unknown set_volume action: None", "{}");
    assert_why("unknown set_volume action: None", "{\"action\":null}");
    assert_why("unknown set_volume action: 'louder'", "{\"action\":\"louder\"}");
    assert_why("unknown set_volume action: 'UP'", "{\"action\":\"UP\"}");
    assert_why("unknown set_volume action: 5", "{\"action\":5}");
    assert_why("unknown set_volume action: '" "\xD0" "\xB7" "\xD1" "\x80" "\xD0" "\xBE" "\xD0" "\xB1" "\xD0" "\xB8" "\xD1" "\x82" "\xD0" "\xB8" "\x20" "\xD1" "\x82" "\xD0" "\xB8" "\xD1" "\x85" "\xD1" "\x96" "\xD1" "\x88" "\xD0" "\xB5" "\x20" "\xD0" "\xB1" "\xD1" "\x83" "\xD0" "\xB4" "\xD1" "\x8C" "\x20" "\xD0" "\xBB" "'",
               "{\"action\":\"" "\xD0" "\xB7" "\xD1" "\x80" "\xD0" "\xBE" "\xD0" "\xB1" "\xD0" "\xB8" "\xD1" "\x82" "\xD0" "\xB8" "\x20" "\xD1" "\x82" "\xD0" "\xB8" "\xD1" "\x85" "\xD1" "\x96" "\xD1" "\x88" "\xD0" "\xB5" "\x20" "\xD0" "\xB1" "\xD1" "\x83" "\xD0" "\xB4" "\xD1" "\x8C" "\x20" "\xD0" "\xBB" "\xD0" "\xB0" "\xD1" "\x81" "\xD0" "\xBA" "\xD0" "\xB0" "\x20" "\xD0" "\xB1" "\xD1" "\x83" "\xD0" "\xB4" "\xD1" "\x8C" "\"}");
    assert_why("set_volume needs a numeric level for action set", "{\"action\":\"set\"}");
    assert_why("set_volume needs a numeric level for action set", "{\"action\":\"set\",\"level\":\"50\"}");
    assert_why("set_volume needs a numeric level for action set", "{\"action\":\"set\",\"level\":true}");
    assert_why("set_volume needs a numeric level for action set", "{\"action\":\"set\",\"level\":1e999}");
}

void test_next_volume_why_is_cleared_on_success_and_truncated(void)
{
    char why[96] = "stale";
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.65f, pt_tools_next_volume(0.5f, "{\"action\":\"up\"}", why, sizeof why));
    TEST_ASSERT_EQUAL_STRING("", why);
    char tiny[16];
    TEST_ASSERT_TRUE(pt_tools_next_volume(0.5f, "{\"action\":\"sideways-and-more\"}", tiny, sizeof tiny) < 0.0f);
    TEST_ASSERT_EQUAL_size_t(15, strlen(tiny));
    char small[32];
    TEST_ASSERT_TRUE(pt_tools_next_volume(0.5f, "{\"action\":\"\xD0\xB7\xD1\x80\xD0\xBE\"}", small, sizeof small) < 0.0f);
    TEST_ASSERT_EQUAL_size_t(30, strlen(small));
    TEST_ASSERT_TRUE(pt_tools_next_volume(0.5f, "{}", NULL, 0) < 0.0f);
    TEST_ASSERT_TRUE(pt_tools_next_volume(0.5f, "{}", tiny, 0) < 0.0f);
}

static pt_tool_call_t call(const char *name)
{
    pt_tool_call_t c;
    memset(&c, 0, sizeof c);
    strcpy(c.call_id, "c1");
    strcpy(c.name, name);
    return c;
}

void test_ends_conversation_when_any_call_is_end(void)
{
    pt_tool_call_t calls[] = {call("set_volume"), call("end_conversation")};
    TEST_ASSERT_TRUE(pt_tools_ends_conversation(calls, 2));
    TEST_ASSERT_TRUE(pt_tools_ends_conversation(calls + 1, 1));
}

void test_ends_conversation_without_end_call(void)
{
    pt_tool_call_t calls[] = {call("set_volume"), call("end_conversation_now")};
    TEST_ASSERT_FALSE(pt_tools_ends_conversation(calls, 2));
    TEST_ASSERT_FALSE(pt_tools_ends_conversation(calls, 1));
    TEST_ASSERT_FALSE(pt_tools_ends_conversation(calls, 0));
    TEST_ASSERT_FALSE(pt_tools_ends_conversation(NULL, 0));
}

static int alloc_budget;
static int alloc_live;
static int alloc_denied;

static void *budget_malloc(size_t n)
{
    if (alloc_budget <= 0) {
        alloc_denied++;
        return NULL;
    }
    alloc_budget--;
    alloc_live++;
    return malloc(n);
}

static void budget_free(void *p)
{
    if (p != NULL) {
        alloc_live--;
        free(p);
    }
}

void test_tools_object_survives_allocation_failures(void)
{
    for (size_t l = 0; l < N_LANGUAGES; l++) {
        cJSON *reference = tools_for(LANGUAGES[l]);
        cJSON_Hooks hooks = {budget_malloc, budget_free};
        bool saw_failure = false;
        bool saw_success = false;
        for (int budget = 0; budget < 200; budget++) {
            alloc_budget = budget;
            alloc_live = 0;
            alloc_denied = 0;
            cJSON_InitHooks(&hooks);
            cJSON *list = pt_tools_object(LANGUAGES[l]);
            if (alloc_denied > 0) {
                saw_failure = true;
                TEST_ASSERT_NULL_MESSAGE(list, "partial tool list returned after allocation failure");
            } else {
                saw_success = true;
                TEST_ASSERT_NOT_NULL(list);
                TEST_ASSERT_TRUE(cJSON_Compare(list, reference, 1));
            }
            cJSON_Delete(list);
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, alloc_live, "leak after pt_tools_object");

            alloc_budget = budget;
            alloc_denied = 0;
            char *text = pt_tools_json(LANGUAGES[l]);
            if (alloc_denied > 0) {
                TEST_ASSERT_NULL(text);
            } else {
                TEST_ASSERT_NOT_NULL(text);
            }
            if (text != NULL) {
                free(text);
            }
            cJSON_InitHooks(NULL);
        }
        TEST_ASSERT_TRUE(saw_failure);
        TEST_ASSERT_TRUE(saw_success);
        cJSON_Delete(reference);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_volume_tool_schema);
    RUN_TEST(test_end_tool_schema);
    RUN_TEST(test_session_tools_follow_the_language);
    RUN_TEST(test_end_tool_rejects_unknown_language);
    RUN_TEST(test_more_languages_get_the_english_end_tool);
    RUN_TEST(test_next_volume);
    RUN_TEST(test_next_volume_clamps_and_rounds);
    RUN_TEST(test_next_volume_rejects_bad_arguments);
    RUN_TEST(test_next_volume_explains_why_it_rejected);
    RUN_TEST(test_next_volume_why_is_cleared_on_success_and_truncated);
    RUN_TEST(test_tools_object_survives_allocation_failures);
    RUN_TEST(test_ends_conversation_when_any_call_is_end);
    RUN_TEST(test_ends_conversation_without_end_call);
    return UNITY_END();
}
