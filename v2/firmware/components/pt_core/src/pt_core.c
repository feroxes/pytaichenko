#include "pt_core.h"

#ifndef PT_VERSION
#error "PT_VERSION must come from v2/firmware/VERSION"
#endif

const char *pt_version(void)
{
    return PT_VERSION;
}

size_t pt_utf8_cut(const char *s, size_t len, size_t max)
{
    if (len <= max) {
        return len;
    }
    size_t back = 0;
    size_t i = max;
    while (i > 0 && back < 3 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) {
        i--;
        back++;
    }
    if (i == 0) {
        return max;
    }
    unsigned char lead = (unsigned char)s[i - 1];
    size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (max - (i - 1) < need) {
        return i - 1;
    }
    return max;
}
