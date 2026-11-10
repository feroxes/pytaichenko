#include "pt_web.h"

#include <string.h>

extern const uint8_t _binary_index_html_gz_start[];
extern const uint8_t _binary_index_html_gz_end[];
extern const uint8_t _binary_app_js_gz_start[];
extern const uint8_t _binary_app_js_gz_end[];
extern const uint8_t _binary_style_css_gz_start[];
extern const uint8_t _binary_style_css_gz_end[];
extern const uint8_t _binary_favicon_svg_gz_start[];
extern const uint8_t _binary_favicon_svg_gz_end[];

#define FILE_COUNT 4

static pt_web_file_t files[FILE_COUNT];

__attribute__((constructor)) static void pt_web_init(void)
{
    const pt_web_file_t table[FILE_COUNT] = {
        {"/index.html", "text/html; charset=utf-8", _binary_index_html_gz_start,
         (size_t)(_binary_index_html_gz_end - _binary_index_html_gz_start)},
        {"/app.js", "text/javascript; charset=utf-8", _binary_app_js_gz_start,
         (size_t)(_binary_app_js_gz_end - _binary_app_js_gz_start)},
        {"/style.css", "text/css; charset=utf-8", _binary_style_css_gz_start,
         (size_t)(_binary_style_css_gz_end - _binary_style_css_gz_start)},
        {"/favicon.svg", "image/svg+xml", _binary_favicon_svg_gz_start,
         (size_t)(_binary_favicon_svg_gz_end - _binary_favicon_svg_gz_start)},
    };
    memcpy(files, table, sizeof(files));
}

const pt_web_file_t *pt_web_find(const char *path)
{
    if (path == NULL || path[0] != '/') {
        return NULL;
    }
    size_t n = strcspn(path, "?#");
    if (n == 1) {
        return &files[0];
    }
    for (size_t i = 0; i < FILE_COUNT; i++) {
        if (strlen(files[i].path) == n && strncmp(files[i].path, path, n) == 0) {
            return &files[i];
        }
    }
    return NULL;
}
