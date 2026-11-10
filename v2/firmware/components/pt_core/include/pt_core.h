#pragma once

typedef int pt_err_t;

enum {
    PT_OK = 0,
    PT_ERR_INVALID = -1,
    PT_ERR_NOMEM = -2,
    PT_ERR_PARSE = -3,
    PT_ERR_AUTH = -4,
    PT_ERR_BUSY = -5,
    PT_ERR_NOT_OURS = -6,
    PT_ERR_NO_RELEASE = -7,
    PT_ERR_IO = -8,
    PT_ERR_PENDING = -9,
    PT_ERR_TALKING = -10
};

#include <stddef.h>

const char *pt_version(void);
size_t pt_utf8_cut(const char *s, size_t len, size_t max);
