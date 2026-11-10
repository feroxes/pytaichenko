#pragma once

#include <stdbool.h>
#include <stddef.h>

struct cJSON;

#define PT_TOOL_VOLUME "set_volume"
#define PT_TOOL_END "end_conversation"

typedef struct {
    char call_id[64];
    char name[32];
    char *arguments;
} pt_tool_call_t;

struct cJSON *pt_tools_object(const char *language);
char *pt_tools_json(const char *language);
float pt_tools_next_volume(float current, const char *arguments, char *why, size_t why_len);
bool pt_tools_ends_conversation(const pt_tool_call_t *calls, size_t n);
