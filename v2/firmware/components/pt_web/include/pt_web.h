#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *path, *type;
    const uint8_t *gz;
    size_t len;
} pt_web_file_t;

const pt_web_file_t *pt_web_find(const char *path);
