#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "pt_core.h"
#include "pt_settings.h"
#include "pt_tools.h"

#define ACTION_SHOWN_MAX 40
#define VOLUME_STEP 0.15
#define MIN_VOLUME 0.1
#define MAX_VOLUME 1.0

static const char VOLUME_DESCRIPTION[] =
        "Change the speaker volume of this device. Call it when the user asks to make the sound "
        "louder or quieter, or to set a specific volume level.";

static const char END_DESCRIPTION_UK[] =
        "Завершити розмову: мікрофон вимикається, пристрій знову чекає ключову фразу. "
        "Викликай, коли людина прощається чи просить зупинитися, або коли репліка явно не до тебе "
        "(люди говорять між собою).";

static const char END_DESCRIPTION_EN[] =
        "End the conversation: the microphone turns off and the device waits for its wake word again. "
        "Call it when the person says goodbye or asks to stop, or when what you hear is clearly not meant for you "
        "(people talking to each other).";

static bool attach(cJSON *parent, const char *key, cJSON *child)
{
    if (child == NULL) {
        return false;
    }
    if (parent == NULL || !cJSON_AddItemToObject(parent, key, child)) {
        cJSON_Delete(child);
        return false;
    }
    return true;
}

static bool push(cJSON *array, cJSON *child)
{
    if (child == NULL) {
        return false;
    }
    if (array == NULL || !cJSON_AddItemToArray(array, child)) {
        cJSON_Delete(child);
        return false;
    }
    return true;
}

static bool add_str(cJSON *parent, const char *key, const char *value)
{
    return cJSON_AddStringToObject(parent, key, value) != NULL;
}

static bool add_num(cJSON *parent, const char *key, double value)
{
    return cJSON_AddNumberToObject(parent, key, value) != NULL;
}

static cJSON *finish(cJSON *item, bool ok)
{
    if (!ok) {
        cJSON_Delete(item);
        return NULL;
    }
    return item;
}

static cJSON *string_array(const char *const *values, size_t n)
{
    cJSON *array = cJSON_CreateArray();
    bool ok = array != NULL;
    for (size_t i = 0; ok && i < n; i++) {
        ok = push(array, cJSON_CreateString(values[i]));
    }
    return finish(array, ok);
}

static cJSON *action_schema(void)
{
    static const char *const values[] = {"up", "down", "set"};
    cJSON *action = cJSON_CreateObject();
    bool ok = action != NULL;
    ok = ok && add_str(action, "type", "string");
    ok = ok && attach(action, "enum", string_array(values, 3));
    ok = ok && add_str(action, "description", "up = a bit louder, down = a bit quieter, set = exact level.");
    return finish(action, ok);
}

static cJSON *level_schema(void)
{
    cJSON *level = cJSON_CreateObject();
    bool ok = level != NULL;
    ok = ok && add_str(level, "type", "integer");
    ok = ok && add_num(level, "minimum", 0);
    ok = ok && add_num(level, "maximum", 100);
    ok = ok && add_str(level, "description", "Target volume in percent. Required when action is set.");
    return finish(level, ok);
}

static cJSON *volume_properties(void)
{
    cJSON *props = cJSON_CreateObject();
    bool ok = props != NULL;
    ok = ok && attach(props, "action", action_schema());
    ok = ok && attach(props, "level", level_schema());
    return finish(props, ok);
}

static cJSON *parameters(cJSON *props, cJSON *required)
{
    cJSON *params = cJSON_CreateObject();
    bool ok = params != NULL;
    ok = ok && add_str(params, "type", "object");
    ok = attach(params, "properties", props) && ok;
    ok = attach(params, "required", required) && ok;
    return finish(params, ok);
}

static cJSON *tool_object(const char *name, const char *description, cJSON *params)
{
    cJSON *tool = cJSON_CreateObject();
    bool ok = tool != NULL;
    ok = ok && add_str(tool, "type", "function");
    ok = ok && add_str(tool, "name", name);
    ok = ok && add_str(tool, "description", description);
    ok = attach(tool, "parameters", params) && ok;
    return finish(tool, ok);
}

static cJSON *volume_tool(void)
{
    static const char *const required[] = {"action"};
    return tool_object(PT_TOOL_VOLUME, VOLUME_DESCRIPTION,
                       parameters(volume_properties(), string_array(required, 1)));
}

static cJSON *end_tool(const char *description)
{
    return tool_object(PT_TOOL_END, description, parameters(cJSON_CreateObject(), cJSON_CreateArray()));
}

cJSON *pt_tools_object(const char *language)
{
    const char *description;
    if (language == NULL) {
        return NULL;
    }
    if (strcmp(language, "uk") == 0) {
        description = END_DESCRIPTION_UK;
    } else if (pt_default_instructions(language) != NULL) {
        description = END_DESCRIPTION_EN;
    } else {
        return NULL;
    }
    cJSON *list = cJSON_CreateArray();
    cJSON *volume = volume_tool();
    cJSON *end = end_tool(description);
    bool ok = push(list, volume);
    ok = push(list, end) && ok;
    return finish(list, ok);
}

char *pt_tools_json(const char *language)
{
    cJSON *list = pt_tools_object(language);
    if (list == NULL) {
        return NULL;
    }
    char *text = cJSON_PrintUnformatted(list);
    cJSON_Delete(list);
    return text;
}

static void set_why(char *why, size_t why_len, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static void set_why(char *why, size_t why_len, const char *fmt, ...)
{
    if (why == NULL || why_len == 0) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(why, why_len, fmt, ap);
    va_end(ap);
    if (n >= 0 && (size_t)n >= why_len) {
        why[pt_utf8_cut(why, (size_t)n, why_len - 1)] = '\0';
    }
}

static size_t shown_len(const char *text)
{
    return pt_utf8_cut(text, strlen(text), ACTION_SHOWN_MAX);
}

static void unknown_action(char *why, size_t why_len, const cJSON *action)
{
    if (action == NULL || cJSON_IsNull(action)) {
        set_why(why, why_len, "unknown set_volume action: None");
    } else if (cJSON_IsString(action) && action->valuestring != NULL) {
        set_why(why, why_len, "unknown set_volume action: '%.*s'", (int)shown_len(action->valuestring),
                action->valuestring);
    } else {
        char *text = cJSON_PrintUnformatted(action);
        const char *shown = text != NULL ? text : "?";
        set_why(why, why_len, "unknown set_volume action: %.*s", (int)shown_len(shown), shown);
        free(text);
    }
}

float pt_tools_next_volume(float current, const char *arguments, char *why, size_t why_len)
{
    if (why != NULL && why_len > 0) {
        why[0] = '\0';
    }
    cJSON *args = arguments != NULL ? cJSON_ParseWithLengthOpts(arguments, strlen(arguments) + 1, NULL, 1) : NULL;
    if (args == NULL) {
        set_why(why, why_len, "set_volume arguments are not valid JSON");
        return -1.0f;
    }
    if (!cJSON_IsObject(args)) {
        cJSON_Delete(args);
        set_why(why, why_len, "set_volume arguments must be a JSON object");
        return -1.0f;
    }
    bool valid = false;
    double target = 0.0;
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(args, "action");
    const char *name = cJSON_IsString(action) ? action->valuestring : NULL;
    if (name != NULL && strcmp(name, "up") == 0) {
        target = (double)current + VOLUME_STEP;
        valid = true;
    } else if (name != NULL && strcmp(name, "down") == 0) {
        target = (double)current - VOLUME_STEP;
        valid = true;
    } else if (name != NULL && strcmp(name, "set") == 0) {
        const cJSON *level = cJSON_GetObjectItemCaseSensitive(args, "level");
        if (cJSON_IsNumber(level) && isfinite(level->valuedouble)) {
            target = level->valuedouble / 100.0;
            valid = true;
        } else {
            set_why(why, why_len, "set_volume needs a numeric level for action set");
        }
    } else {
        unknown_action(why, why_len, action);
    }
    cJSON_Delete(args);
    if (!valid) {
        return -1.0f;
    }
    if (target > MAX_VOLUME) {
        target = MAX_VOLUME;
    } else if (target < MIN_VOLUME) {
        target = MIN_VOLUME;
    }
    return (float)(round(target * 100.0) / 100.0);
}

bool pt_tools_ends_conversation(const pt_tool_call_t *calls, size_t n)
{
    if (calls == NULL) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (strcmp(calls[i].name, PT_TOOL_END) == 0) {
            return true;
        }
    }
    return false;
}
