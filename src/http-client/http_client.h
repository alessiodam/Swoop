#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <witi.h>

#define HTTP_VERSION "HTTP/1.1"
#define HTTP_USER_AGENT "CEagle/1.0 (TI-84 Plus CE)"

#ifndef HTTP_HOST_MAX
#define HTTP_HOST_MAX 64
#endif

#ifndef HTTP_PATH_MAX
#define HTTP_PATH_MAX 192
#endif

#ifndef HTTP_URL_MAX
#define HTTP_URL_MAX 272
#endif

#ifndef HTTP_LINE_MAX
#define HTTP_LINE_MAX 256
#endif

#ifndef HTTP_HEAD_MAX
#define HTTP_HEAD_MAX 512
#endif

#ifndef HTTP_HEADER_FIELDS
#define HTTP_HEADER_FIELDS 12
#endif

#ifndef HTTP_HEADER_ARENA
#define HTTP_HEADER_ARENA 448
#endif

#ifndef HTTP_REASON_MAX
#define HTTP_REASON_MAX 32
#endif

#ifndef HTTP_SEND_CHUNK
#define HTTP_SEND_CHUNK 192
#endif

#define HTTP_REDIRECTS_DEFAULT 5
#define HTTP_CONNECT_TIMEOUT_DEFAULT 12000
#define HTTP_IDLE_TIMEOUT_DEFAULT 15000
#define HTTP_LENGTH_UNKNOWN 0xFFFFFFFFuL

typedef enum {
    HTTP_GET = 0,
    HTTP_HEAD,
    HTTP_POST,
    HTTP_PUT,
    HTTP_PATCH,
    HTTP_DELETE,
    HTTP_OPTIONS,
    HTTP_METHOD_COUNT
} http_method_t;

typedef enum {
    HTTP_OK = 0,
    HTTP_ERR_URL,
    HTTP_ERR_BUSY,
    HTTP_ERR_LINK,
    HTTP_ERR_OFFLINE,
    HTTP_ERR_SOCKET,
    HTTP_ERR_CONNECT,
    HTTP_ERR_SEND,
    HTTP_ERR_TIMEOUT,
    HTTP_ERR_CLOSED,
    HTTP_ERR_PROTOCOL,
    HTTP_ERR_HEAD_TOO_LARGE,
    HTTP_ERR_REDIRECT,
    HTTP_ERR_BODY,
    HTTP_ERR_ABORTED,
    HTTP_ERROR_COUNT
} http_error_t;

typedef enum {
    HTTP_STATE_IDLE = 0,
    HTTP_STATE_CONNECTING,
    HTTP_STATE_SENDING_HEAD,
    HTTP_STATE_SENDING_BODY,
    HTTP_STATE_STATUS,
    HTTP_STATE_HEADERS,
    HTTP_STATE_BODY,
    HTTP_STATE_DONE,
    HTTP_STATE_FAILED
} http_state_t;

typedef enum {
    HTTP_BODY_NONE = 0,
    HTTP_BODY_LENGTH,
    HTTP_BODY_CHUNKED,
    HTTP_BODY_UNTIL_CLOSE
} http_body_mode_t;

typedef struct {
    uint8_t count;
    uint16_t used;
    bool truncated;
    uint16_t name[HTTP_HEADER_FIELDS];
    uint16_t value[HTTP_HEADER_FIELDS];
    char arena[HTTP_HEADER_ARENA];
} http_headers_t;

typedef struct {
    bool tls;
    uint16_t port;
    char host[HTTP_HOST_MAX];
    char path[HTTP_PATH_MAX];
} http_url_t;

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    bool overflow;
} http_form_t;

typedef size_t (*http_body_source_t)(void *destination, size_t max, void *user);

typedef struct {
    http_method_t method;
    const char *method_name;
    const void *body;
    size_t body_length;
    http_body_source_t body_source;
    void *body_user;
    uint32_t body_total;
    const char *content_type;
    const char *user_agent;
    const char *accept;
    const http_headers_t *headers;
    bool follow_redirects;
    uint8_t max_redirects;
    bool keep_alive;
    uint32_t connect_timeout_ms;
    uint32_t idle_timeout_ms;
} http_request_t;

typedef struct http_client http_client_t;

typedef struct {
    void (*on_status)(http_client_t *client, uint16_t status, const char *reason, void *user);
    void (*on_header)(http_client_t *client, const char *name, const char *value, void *user);
    void (*on_response)(http_client_t *client, void *user);
    bool (*on_body)(http_client_t *client, const void *data, size_t length, void *user);
    bool (*on_redirect)(http_client_t *client, const char *location, void *user);
    bool (*on_idle)(http_client_t *client, void *user);
    void (*on_done)(http_client_t *client, http_error_t error, void *user);
    void *user;
} http_callbacks_t;

struct http_client {
    http_state_t state;
    http_error_t error;
    http_request_t request;
    http_callbacks_t callbacks;
    http_url_t url;

    witi_socket_t *socket;
    bool socket_connected;
    bool socket_eof;
    bool socket_failed;
    uint8_t socket_error;

    bool reused;
    bool retried;
    bool held;
    bool held_tls;
    uint16_t held_port;
    char held_host[HTTP_HOST_MAX];

    char head[HTTP_HEAD_MAX];
    size_t head_length;
    size_t head_sent;

    const uint8_t *body_cursor;
    size_t body_left;
    uint8_t stage[HTTP_SEND_CHUNK];
    size_t stage_length;
    size_t stage_sent;
    bool source_done;
    bool request_chunked;

    char line[HTTP_LINE_MAX];
    uint16_t line_length;
    bool line_overflow;

    uint16_t status;
    char reason[HTTP_REASON_MAX];
    http_headers_t response;

    uint8_t body_mode;
    uint8_t chunk_state;
    uint32_t content_length;
    uint32_t chunk_left;
    uint32_t received;

    bool response_close;
    bool discarding;
    bool redirecting;
    uint8_t redirects;
    char location[HTTP_URL_MAX];

    uint8_t *sink;
    size_t sink_capacity;
    size_t sink_length;
    bool sink_overflow;

    uint32_t deadline;
    uint32_t idle_timeout;
    uint32_t started;
    uint32_t elapsed;

    bool active;
    bool finished;
    bool abort;
    char text[HTTP_URL_MAX];
};

void http_init(http_client_t *client);
void http_request_init(http_request_t *request, http_method_t method);

bool http_begin(http_client_t *client, const char *url, const http_request_t *request,
                const http_callbacks_t *callbacks);
void http_update(http_client_t *client);
bool http_busy(const http_client_t *client);
bool http_finished(const http_client_t *client);
void http_cancel(http_client_t *client);
void http_release(http_client_t *client);

void http_set_sink(http_client_t *client, void *buffer, size_t capacity);
const char *http_text(const http_client_t *client);
const void *http_data(const http_client_t *client);
size_t http_length(const http_client_t *client);
bool http_truncated(const http_client_t *client);

http_error_t http_perform(http_client_t *client, const char *url,
                          const http_request_t *request, const http_callbacks_t *callbacks);
http_error_t http_get(http_client_t *client, const char *url, void *buffer, size_t capacity);
http_error_t http_post(http_client_t *client, const char *url, const char *content_type,
                       const void *body, size_t length, void *buffer, size_t capacity);
http_error_t http_put(http_client_t *client, const char *url, const char *content_type,
                      const void *body, size_t length, void *buffer, size_t capacity);
http_error_t http_delete(http_client_t *client, const char *url, void *buffer, size_t capacity);
http_error_t http_head(http_client_t *client, const char *url);

http_state_t http_state(const http_client_t *client);
const char *http_state_name(http_state_t state);
http_error_t http_error(const http_client_t *client);
const char *http_error_text(http_error_t error);
uint16_t http_status(const http_client_t *client);
const char *http_reason(const http_client_t *client);
uint32_t http_content_length(const http_client_t *client);
uint32_t http_received(const http_client_t *client);
uint32_t http_elapsed_ms(const http_client_t *client);
uint8_t http_redirect_count(const http_client_t *client);
http_body_mode_t http_body_mode(const http_client_t *client);
const http_headers_t *http_response_headers(const http_client_t *client);
const char *http_response_header(const http_client_t *client, const char *name);
const http_url_t *http_url(const http_client_t *client);
const char *http_effective_url(http_client_t *client);
const char *http_method_name(http_method_t method);
const char *http_status_text(uint16_t status);
bool http_status_is_redirect(uint16_t status);

void http_headers_clear(http_headers_t *headers);
bool http_headers_add(http_headers_t *headers, const char *name, const char *value);
bool http_headers_set(http_headers_t *headers, const char *name, const char *value);
bool http_headers_set_number(http_headers_t *headers, const char *name, uint32_t value);
void http_headers_remove(http_headers_t *headers, const char *name);
const char *http_headers_get(const http_headers_t *headers, const char *name);
bool http_headers_has(const http_headers_t *headers, const char *name);
uint8_t http_headers_count(const http_headers_t *headers);
const char *http_headers_name_at(const http_headers_t *headers, uint8_t index);
const char *http_headers_value_at(const http_headers_t *headers, uint8_t index);
bool http_headers_truncated(const http_headers_t *headers);

bool http_url_parse(http_url_t *url, const char *text);
bool http_url_resolve(http_url_t *url, const char *reference);
size_t http_url_format(const http_url_t *url, char *out, size_t max);
bool http_url_same_origin(const http_url_t *a, const http_url_t *b);
size_t http_url_encode(char *out, size_t max, const char *text, const char *keep);
size_t http_url_decode(char *out, size_t max, const char *text, bool plus_is_space);

void http_form_init(http_form_t *form, char *buffer, size_t capacity);
bool http_form_add(http_form_t *form, const char *name, const char *value);
bool http_form_add_number(http_form_t *form, const char *name, int32_t value);
const char *http_form_data(const http_form_t *form);
size_t http_form_length(const http_form_t *form);

#endif
