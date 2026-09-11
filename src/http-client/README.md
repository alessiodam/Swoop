# CEagle HTTP

HTTP/1.1 client for the TI-84 Plus CE, built on the WiTi socket layer. It ships with
CEagle, its only user; put it anywhere on the include path alongside a `witi/` and
`#include "http-client/http_client.h"`.

No dependencies beyond `witi.h` from the WiTi LibLoad library. No dynamic allocation: the client owns every
buffer it needs (~2.5 KB per `http_client_t`).

## Files

| file | role |
| --- | --- |
| `http_client.h` | the only header an application needs |
| `http_client.c` | connection, request writer, response parser, redirects, keep-alive |
| `http_headers.c` | fixed-arena header store used for both request and response headers |
| `http_url.c` | URL parse, relative resolution, percent encoding, form builder |
| `http_util.c` | case-insensitive helpers, number parsing, token lists |
| `http_internal.h` | shared internals, not part of the public surface |

## What it covers

- `GET`, `HEAD`, `POST`, `PUT`, `PATCH`, `DELETE`, `OPTIONS`, plus any custom method
- request headers, request bodies from a buffer or from a streaming callback
- `Content-Length` bodies, `chunked` transfer encoding (request and response), and
  read-until-close responses
- `1xx` informational responses, `204`/`304`/`HEAD` empty bodies
- redirect following (`301`, `302`, `303`, `307`, `308`) with relative `Location`
  resolution and the usual method rewrite to `GET`
- keep-alive connection reuse across requests to the same origin, with one automatic
  retry when a reused connection turns out to be dead
- `https` through the bridge's TLS offload — same API, `witi` opens a `WITI_TLS` socket
- connect and idle timeouts, cancellation, streaming or buffered response bodies

Not covered: content decompression, cookies, authentication, proxies, HTTP/2.
Ask the server for `Accept-Encoding: identity` if it likes to gzip.

## Blocking use

```c
#include "http-client/http_client.h"

static char body[4096];
static http_client_t client;

http_init(&client);

if (http_get(&client, "http://example.com/", body, sizeof body) == HTTP_OK)
{
    printf("%u %s\n", http_status(&client), http_reason(&client));
    printf("%s\n", body);
}
```

`http_get`, `http_post`, `http_put`, `http_delete` and `http_head` pump `witi_poll()`
internally and return when the exchange is over. The body is copied into the buffer
you passed and always NUL-terminated; `http_truncated()` says whether it did not fit.

## Non-blocking use

The blocking helpers are a thin wrapper over the state machine. Drive it yourself when
the calculator has a UI to keep alive:

```c
static bool on_body(http_client_t *client, const void *data, size_t length, void *user)
{
    html_feed(&doc, data, length);
    return true;
}

static void on_done(http_client_t *client, http_error_t error, void *user)
{
    finished = true;
}

static const http_callbacks_t callbacks = {
    .on_body = on_body,
    .on_done = on_done,
};

http_request_t request;

http_request_init(&request, HTTP_GET);
http_begin(&client, "http://example.com/", &request, &callbacks);

while (!finished)
{
    witi_poll();
    http_update(&client);
    draw_progress();
}
```

`http_update()` must be called from the main loop, right after `witi_poll()`. Every
callback fires from inside one of those two, never from an interrupt.

Callback order for a response is `on_status`, then `on_header` once per stored header,
then `on_response`, then `on_body` repeatedly, then `on_done`. Redirect hops that are
followed do not report their headers; `on_redirect` fires instead, and returning `false`
from it stops the follow and delivers the `3xx` response as the final one.

Returning `false` from `on_body` aborts the transfer with `HTTP_ERR_ABORTED` — useful
when the destination buffer is full and the rest of the page is not worth the wire time.

## Requests

```c
http_request_t request;
http_headers_t headers;

http_request_init(&request, HTTP_POST);
http_headers_clear(&headers);
http_headers_set(&headers, "X-Device", "TI-84 Plus CE");
http_headers_set(&headers, "Authorization", "Bearer ...");

request.headers = &headers;
request.content_type = "application/json";
request.body = "{\"count\":3}";
request.body_length = 11;
request.follow_redirects = true;
request.max_redirects = 5;
request.keep_alive = true;
request.idle_timeout_ms = 15000;
```

`Host`, `User-Agent`, `Accept`, `Connection` and `Content-Length` are added for you and
are skipped whenever the same header is present in `request.headers`, so overriding any
of them is a matter of setting it there. `request.method_name` sends a method the enum
does not list.

Everything the request points at — body, header set, content type — must stay valid
until `on_done` fires, because redirects rebuild the request from it.

### Streaming request bodies

```c
static size_t feed(void *destination, size_t max, void *user)
{
    return fread(destination, 1, max, user);
}

request.body_source = feed;
request.body_user = file;
request.body_total = size;                  /* Content-Length */
request.body_total = HTTP_LENGTH_UNKNOWN;   /* or chunked */
```

A streamed body cannot be replayed, so a `307`/`308` redirect on such a request fails
with `HTTP_ERR_REDIRECT`.

## Responses

```c
uint16_t status = http_status(&client);
const char *type = http_response_header(&client, "Content-Type");
uint32_t length = http_content_length(&client);   /* HTTP_LENGTH_UNKNOWN if chunked */
uint32_t got = http_received(&client);
uint8_t hops = http_redirect_count(&client);
const char *final = http_effective_url(&client);
```

Response headers land in a fixed arena — `HTTP_HEADER_FIELDS` (12) fields inside
`HTTP_HEADER_ARENA` (448) bytes. Anything past that is dropped and
`http_headers_truncated()` becomes true. Both limits are compile-time overridable:

```
make CFLAGS="-Wall -Wextra -Oz -DHTTP_HEADER_FIELDS=20 -DHTTP_HEADER_ARENA=1024"
```

## URLs

```c
http_url_t url;

http_url_parse(&url, "https://example.com:8443/a/b?q=1");
http_url_resolve(&url, "../c");              /* -> https://example.com:8443/a/c */
http_url_format(&url, text, sizeof text);
```

`http_url_resolve` takes absolute URLs, scheme-relative `//host/path`, absolute paths,
query-only references and relative paths, normalises `.` and `..`, and drops fragments.
That is all a browser needs to turn an `href` into something `http_begin` accepts.

Percent encoding and form bodies:

```c
char buffer[128];
http_form_t form;

http_form_init(&form, buffer, sizeof buffer);
http_form_add(&form, "name", "ada lovelace");
http_form_add_number(&form, "count", 3);

http_post(&client, url, "application/x-www-form-urlencoded",
          http_form_data(&form), http_form_length(&form), body, sizeof body);
```

## Connection reuse

With `keep_alive` set the socket stays open after a response and is reused by the next
request to the same scheme, host and port — including the next hop of a redirect chain.
A reused connection that the server had already closed is retried once on a fresh
socket, so reuse never turns into a spurious failure. `http_release()` drops the held
socket; it is also dropped by `http_cancel()`, by any error, and by `Connection: close`.

Each held connection occupies one of the four WiTi sockets. Release the client before
opening several sockets for something else.

## Errors

`http_error()` returns the code, `http_error_text()` a short description. The ones worth
handling separately are `HTTP_ERR_OFFLINE` (no Wi-Fi yet), `HTTP_ERR_LINK` (no bridge),
`HTTP_ERR_TIMEOUT`, and `HTTP_ERR_HEAD_TOO_LARGE` (the request head exceeded
`HTTP_HEAD_MAX`, 512 bytes by default).

## Example

`examples/browser` is a complete text-mode web browser using this library: address bar,
redirect following, HTML-to-text rendering, link navigation and a response header view.
