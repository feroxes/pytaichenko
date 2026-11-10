#include "pt_http.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "pt_shell.h"
#include "pt_web.h"

#define HTTP_STACK 12288
#define HTTP_SOCKETS 5
#define HTTP_RECV_TRIES 3
#define HTTP_DRAIN_MAX (64u * 1024u)
#define HTTP_DRAIN_MS 20000
#define HTTP_UPLOAD_CHUNK 4096u
#define UPLOAD_PATH "/api/update/upload"

static const char *TAG = "http";
static const char *const TOO_LARGE = "{\"error\":\"too_large\",\"message\":\"the request body is too large\"}";
static const char *const INTERNAL = "{\"error\":\"internal\",\"message\":\"out of memory\"}";

static pt_api_t *s_api;
static UBaseType_t s_stack_min = UINT32_MAX;

static void wipe_free(char *p, size_t n)
{
    if (p == NULL) {
        return;
    }
    volatile char *v = p;
    for (size_t i = 0; i < n; i++) {
        v[i] = 0;
    }
    free(p);
}

static const char *reason(int status)
{
    switch (status) {
    case 200:
        return "200 OK";
    case 400:
        return "400 Bad Request";
    case 401:
        return "401 Unauthorized";
    case 403:
        return "403 Forbidden";
    case 404:
        return "404 Not Found";
    case 405:
        return "405 Method Not Allowed";
    case 408:
        return "408 Request Timeout";
    case 409:
        return "409 Conflict";
    case 413:
        return "413 Payload Too Large";
    case 415:
        return "415 Unsupported Media Type";
    case 429:
        return "429 Too Many Requests";
    case 502:
        return "502 Bad Gateway";
    case 504:
        return "504 Gateway Timeout";
    default:
        return "500 Internal Server Error";
    }
}

static char *header(httpd_req_t *r, const char *name)
{
    size_t n = httpd_req_get_hdr_value_len(r, name);
    if (n == 0) {
        return NULL;
    }
    char *v = malloc(n + 1);
    if (v != NULL && httpd_req_get_hdr_value_str(r, name, v, n + 1) != ESP_OK) {
        free(v);
        v = NULL;
    }
    return v;
}

static uint32_t client_ip(httpd_req_t *r)
{
    int fd = httpd_req_to_sockfd(r);
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    if (fd < 0 || getpeername(fd, (struct sockaddr *)&addr, &len) != 0) {
        return 0;
    }
    if (addr.ss_family == AF_INET) {
        return ntohl(((const struct sockaddr_in *)&addr)->sin_addr.s_addr);
    }
#if CONFIG_LWIP_IPV6
    if (addr.ss_family == AF_INET6) {
        const uint8_t *b = ((const struct sockaddr_in6 *)&addr)->sin6_addr.s6_addr;
        static const uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
        if (memcmp(b, mapped, sizeof(mapped)) == 0) {
            return ((uint32_t)b[12] << 24) | ((uint32_t)b[13] << 16) | ((uint32_t)b[14] << 8) | (uint32_t)b[15];
        }
    }
#endif
    return 0;
}

static esp_err_t send_json(httpd_req_t *r, int status, const char *body, const char *cookie)
{
    httpd_resp_set_status(r, reason(status));
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (cookie != NULL && cookie[0] != '\0') {
        httpd_resp_set_hdr(r, "Set-Cookie", cookie);
    }
    return httpd_resp_send(r, body, HTTPD_RESP_USE_STRLEN);
}

static bool read_body(httpd_req_t *r, char **out)
{
    size_t n = r->content_len;
    char *buf = malloc(n + 1);
    if (buf == NULL) {
        return false;
    }
    size_t got = 0;
    int timeouts = 0;
    while (got < n) {
        int k = httpd_req_recv(r, buf + got, n - got);
        if (k == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < HTTP_RECV_TRIES) {
            continue;
        }
        if (k <= 0) {
            wipe_free(buf, n + 1);
            return false;
        }
        got += (size_t)k;
    }
    buf[n] = '\0';
    *out = buf;
    return true;
}

static void note_stack(void)
{
    UBaseType_t free_now = uxTaskGetStackHighWaterMark(NULL);
    if (free_now < s_stack_min) {
        s_stack_min = free_now;
        ESP_LOGI(TAG, "httpd stack: %u bytes never used", (unsigned)free_now);
    }
}

static esp_err_t serve_api(httpd_req_t *r)
{
    if (r->content_len > PT_HTTP_MAX_BODY) {
        send_json(r, 413, TOO_LARGE, NULL);
        return r->content_len <= HTTP_DRAIN_MAX ? ESP_OK : ESP_FAIL;
    }
    char *body = NULL;
    if (!read_body(r, &body)) {
        return ESP_FAIL;
    }
    char *host = header(r, "Host");
    char *origin = header(r, "Origin");
    char *fetch_site = header(r, "Sec-Fetch-Site");
    char *content_type = header(r, "Content-Type");
    char *cookie = header(r, "Cookie");
    pt_http_req_t req = {
        .method = http_method_str((enum http_method)r->method),
        .path = r->uri,
        .host = host,
        .origin = origin,
        .sec_fetch_site = fetch_site,
        .content_type = content_type,
        .cookie = cookie,
        .client_ip = client_ip(r),
        .body = body,
        .body_len = r->content_len,
    };
    pt_http_resp_t resp;
    pt_shell_settings_lock();
    pt_api_handle(s_api, &req, &resp);
    pt_shell_settings_unlock();
    wipe_free(body, r->content_len + 1);
    free(host);
    free(origin);
    free(fetch_site);
    free(content_type);
    wipe_free(cookie, cookie != NULL ? strlen(cookie) : 0);
    esp_err_t e = resp.body != NULL ? send_json(r, resp.status, resp.body, resp.set_cookie)
                                    : send_json(r, 500, INTERNAL, NULL);
    pt_http_resp_free(&resp);
    memset(resp.set_cookie, 0, sizeof(resp.set_cookie));
    note_stack();
    return e;
}

static bool discard_body(httpd_req_t *r, size_t left)
{
    char *buf = malloc(HTTP_UPLOAD_CHUNK);
    if (buf == NULL) {
        return false;
    }
    int timeouts = 0;
    bool ok = true;
    int64_t deadline_us = esp_timer_get_time() + (int64_t)HTTP_DRAIN_MS * 1000;
    while (left > 0) {
        int k = httpd_req_recv(r, buf, left < HTTP_UPLOAD_CHUNK ? left : HTTP_UPLOAD_CHUNK);
        if (k == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < HTTP_RECV_TRIES) {
            continue;
        }
        if (k <= 0 || esp_timer_get_time() > deadline_us) {
            ok = false;
            break;
        }
        timeouts = 0;
        left -= (size_t)k;
    }
    free(buf);
    return ok;
}

static esp_err_t finish_upload(httpd_req_t *r, pt_http_resp_t *resp, size_t unread, size_t drain_limit)
{
    bool drained = unread == 0 || (unread <= drain_limit && discard_body(r, unread));
    esp_err_t e = resp->body != NULL ? send_json(r, resp->status, resp->body, NULL) : send_json(r, 500, INTERNAL, NULL);
    pt_http_resp_free(resp);
    note_stack();
    return drained ? e : ESP_FAIL;
}

static esp_err_t serve_upload(httpd_req_t *r)
{
    char *host = header(r, "Host");
    char *origin = header(r, "Origin");
    char *fetch_site = header(r, "Sec-Fetch-Site");
    char *content_type = header(r, "Content-Type");
    char *cookie = header(r, "Cookie");
    pt_http_req_t req = {
        .method = http_method_str((enum http_method)r->method),
        .path = r->uri,
        .host = host,
        .origin = origin,
        .sec_fetch_site = fetch_site,
        .content_type = content_type,
        .cookie = cookie,
        .client_ip = client_ip(r),
        .body = NULL,
        .body_len = r->content_len,
    };
    pt_http_resp_t resp;
    pt_shell_settings_lock();
    bool accepted = pt_api_upload_begin(s_api, &req, &resp);
    pt_shell_settings_unlock();
    free(host);
    free(origin);
    free(fetch_site);
    free(content_type);
    wipe_free(cookie, cookie != NULL ? strlen(cookie) : 0);
    if (!accepted) {
        return finish_upload(r, &resp, r->content_len, resp.drain_refused_body ? PT_API_UPLOAD_MAX : 0);
    }
    char *buf = malloc(HTTP_UPLOAD_CHUNK);
    if (buf == NULL) {
        pt_shell_settings_lock();
        pt_api_upload_abort(s_api);
        pt_shell_settings_unlock();
        return ESP_FAIL;
    }
    size_t left = r->content_len;
    int timeouts = 0;
    bool failed = false;
    while (left > 0) {
        int k = httpd_req_recv(r, buf, left < HTTP_UPLOAD_CHUNK ? left : HTTP_UPLOAD_CHUNK);
        if (k == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < HTTP_RECV_TRIES) {
            continue;
        }
        if (k <= 0) {
            pt_shell_settings_lock();
            pt_api_upload_abort(s_api);
            pt_shell_settings_unlock();
            free(buf);
            return ESP_FAIL;
        }
        timeouts = 0;
        left -= (size_t)k;
        pt_shell_settings_lock();
        bool ok = pt_api_upload_chunk(s_api, buf, (size_t)k, &resp);
        pt_shell_settings_unlock();
        if (!ok) {
            failed = true;
            break;
        }
    }
    free(buf);
    if (!failed) {
        pt_shell_settings_lock();
        pt_api_upload_end(s_api, &resp);
        pt_shell_settings_unlock();
    }
    return finish_upload(r, &resp, left, PT_API_UPLOAD_MAX);
}

static esp_err_t serve_file(httpd_req_t *r)
{
    const pt_web_file_t *f = r->method == HTTP_GET ? pt_web_find(r->uri) : NULL;
    if (f == NULL) {
        httpd_resp_set_status(r, reason(404));
        httpd_resp_set_type(r, "text/plain");
        return httpd_resp_send(r, "not found", HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_set_type(r, f->type);
    httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(r, "Cache-Control", "no-cache");
    return httpd_resp_send(r, (const char *)f->gz, (ssize_t)f->len);
}

static bool is_api(const char *uri)
{
    size_t n = strcspn(uri, "?");
    return (n == 4 && strncmp(uri, "/api", 4) == 0) || strncmp(uri, "/api/", 5) == 0;
}

static bool is_upload(const httpd_req_t *r)
{
    size_t n = strcspn(r->uri, "?");
    return r->method == HTTP_POST && n == strlen(UPLOAD_PATH) && strncmp(r->uri, UPLOAD_PATH, n) == 0;
}

static esp_err_t dispatch(httpd_req_t *r)
{
    if (is_upload(r)) {
        return serve_upload(r);
    }
    return is_api(r->uri) ? serve_api(r) : serve_file(r);
}

pt_err_t pt_http_start(pt_api_t *api)
{
    if (api == NULL) {
        return PT_ERR_INVALID;
    }
    s_api = api;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = HTTP_STACK;
    config.max_open_sockets = HTTP_SOCKETS;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    static const httpd_method_t methods[] = {HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE, HTTP_PATCH, HTTP_OPTIONS};
    config.max_uri_handlers = sizeof(methods) / sizeof(methods[0]);
    httpd_handle_t server = NULL;
    esp_err_t e = httpd_start(&server, &config);
    if (e != ESP_OK) {
        pt_shell_log("web page server could not start (%s)", esp_err_to_name(e));
        return PT_ERR_INVALID;
    }
    for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
        const httpd_uri_t uri = {.uri = "/*", .method = methods[i], .handler = dispatch, .user_ctx = NULL};
        if (httpd_register_uri_handler(server, &uri) != ESP_OK) {
            httpd_stop(server);
            pt_shell_log("web page server could not start");
            return PT_ERR_INVALID;
        }
    }
    return PT_OK;
}
