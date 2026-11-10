#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pt_core.h"

#define PT_PASSWORD_HASH_MAX 96
#define PT_LOGIN_TOKEN_MAX 44
#define PT_LOGIN_TTL_S (30u * 24u * 3600u)
#define PT_LOGIN_LIMIT 100u
#define PT_THROTTLE_QUEUED 3u

typedef void (*pt_random_fn)(void *ctx, uint8_t *buf, size_t len);

pt_err_t pt_password_hash(const char *password, pt_random_fn rnd, void *ctx, char *out, size_t out_len);
bool pt_password_verify(const char *password, const char *stored);

typedef struct pt_logins pt_logins_t;

pt_logins_t *pt_logins_new(uint32_t ttl_s, size_t limit, pt_random_fn rnd, void *ctx);
void pt_logins_free(pt_logins_t *s);
pt_err_t pt_logins_create(pt_logins_t *s, uint32_t now_s, char *token, size_t token_len);
bool pt_logins_valid(pt_logins_t *s, const char *token, uint32_t now_s);
void pt_logins_drop(pt_logins_t *s, const char *token);
void pt_logins_clear(pt_logins_t *s);

typedef struct pt_throttle pt_throttle_t;

pt_throttle_t *pt_throttle_new(size_t max_queued, uint32_t failed_delay_ms);
void pt_throttle_free(pt_throttle_t *t);
pt_err_t pt_throttle_enter(pt_throttle_t *t, uint32_t client_ip);
uint32_t pt_throttle_check_done(const pt_throttle_t *t, bool ok);
void pt_throttle_leave(pt_throttle_t *t, uint32_t client_ip);
