#ifndef SWOOP_NET_H
#define SWOOP_NET_H

#include "http-client/http_client.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NET_MAX 6144
#define NET_URL_MAX 272

typedef void (*net_done_t)(uint16_t status, const char *body, size_t length);
typedef bool (*net_chunk_t)(const void *data, size_t length);
typedef void (*net_head_t)(const char *name, const char *value);

void net_init(void);
void net_update(void);
void net_release(void);
void net_cancel(void);
bool net_busy(void);

bool net_get(const char *url, net_done_t done);
bool net_stream(const char *url, net_chunk_t chunk, net_head_t head, net_done_t done);

const char *net_error(void);
uint16_t net_status(void);
uint32_t net_received(void);
uint32_t net_expected(void);
const char *net_stage(void);

char *net_url(const char *base, const char *path);
void net_escape(char *out, size_t max, const char *text);

#endif
