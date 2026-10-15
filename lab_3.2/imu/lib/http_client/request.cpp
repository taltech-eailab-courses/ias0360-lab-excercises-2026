#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <sstream>
#include <cstdint>
#include <cstdlib>
#include <cstring> 
#include <strings.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "request.hpp"
#include "sample_data.h"

#define POST_CHUNK_SIZE 512  // can adjust to 256–1024 depending on testing

static void post_err(void *arg, err_t err) {
    PostState *st = (PostState*)arg;
    // lwIP has already freed the pcb when the error callback runs: never touch it again.
    if (st) { st->result = err ? err : -1; st->pcb = nullptr; st->complete = true; }
}

static err_t post_recv(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    PostState *st = (PostState*)arg;
    if (!st) { if (p) pbuf_free(p); return ERR_OK; }
    if (err != ERR_OK) { if (p) pbuf_free(p); st->result = err; st->complete = true; return err; }
    if (!p) { // FIN
        st->complete = true;
        return ERR_OK;
    }
    // accumulate
    u16_t to_copy = p->tot_len;
    size_t space = sizeof(st->resp->body) - st->resp->len - 1;
    if (space > 0) {
        if (to_copy > space) to_copy = (u16_t)space;
        pbuf_copy_partial(p, st->resp->body + st->resp->len, to_copy, 0);
        st->resp->len += to_copy;
        st->resp->body[st->resp->len] = '\0';
    }
    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *arg) {
    (void)name;
    PostState *st = (PostState *)arg;
    if (!st) return;
    if (ipaddr) {
        st->addr = *ipaddr;
        st->resolved = true;
    } else {
        st->resolved = true; // but addr remains zero
        st->result = -1;
    }
}

static err_t post_connected(void *arg, struct altcp_pcb *pcb, err_t err) {
    PostState *st = (PostState*)arg;
    if (err != ERR_OK) { st->result = err; st->complete = true; return err; }
    st->connected = true;
    // Build request
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
                     "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
                     st->url, st->host, st->ctype ? st->ctype : "application/octet-stream", (unsigned)st->body_len);
    if (n <= 0) { st->result = -1; st->complete = true; return ERR_VAL; }
    err_t e = altcp_write(pcb, hdr, (u16_t)n, TCP_WRITE_FLAG_COPY);
    if (e != ERR_OK) { st->result = e; st->complete = true; return e; }
    if (st->body && st->body_len) {
        printf("Header: %.*s", n, hdr);
        printf("POST body length: %zu\n", st->body_len);
        // printf("body: %.*s\n", (int)st->body_len, st->body);
        e = altcp_write(pcb, st->body, (u16_t)st->body_len, TCP_WRITE_FLAG_COPY);
        if (e != ERR_OK) {
            printf("altcp_write(body) failed: %d (%s)\n", e, lwip_strerr(e));
            st->result = e;
            st->complete = true;
            return e;
        }
    }
    altcp_output(pcb);
    return ERR_OK;
}

static err_t post_send_next_chunk(struct altcp_pcb *pcb, PostState *st) {
    if (!st || !pcb) return ERR_VAL;

    // All data queued?
    if (st->sent_offset >= st->body_len) {
        // Everything queued; make sure it’s pushed out
        altcp_output(pcb);
        return ERR_OK;
    }

    while (st->sent_offset < st->body_len) {
        size_t remaining = st->body_len - st->sent_offset;
        size_t chunk_len = remaining > POST_CHUNK_SIZE ? POST_CHUNK_SIZE : remaining;

        err_t e = altcp_write(pcb,
                              st->body + st->sent_offset,
                              (u16_t)chunk_len,
                              TCP_WRITE_FLAG_COPY);
        if (e == ERR_OK) {
            st->sent_offset += chunk_len;
            // Try to flush now; if link is busy it will queue.
            altcp_output(pcb);
        } else if (e == ERR_MEM) {
            // No room right now—wait for ACK/poll to try again
            // (Do not treat as error)
            // printf("pause @ %u due to ERR_MEM\n", (unsigned)st->sent_offset);
            return ERR_OK;
        } else {
            // Fatal send error
            st->result = e;
            st->complete = true;
            return e;
        }
    }

    return ERR_OK;
}

static err_t post_sent_cb(void *arg, struct altcp_pcb *pcb, u16_t acked) {
    PostState *st = (PostState*)arg;
    // printf("ACKed %u bytes\n", acked);
    if (st) st->acked += acked;
    // After ACK freed space, try to queue more
    return post_send_next_chunk(pcb, st);
}

// Optional safety net: if no ACKs arrive but window opens, poll will try again.
static err_t post_poll_cb(void *arg, struct altcp_pcb *pcb) {
    PostState *st = (PostState*)arg;
    return post_send_next_chunk(pcb, st);
}

static err_t post_connected_by_chunk(void *arg, struct altcp_pcb *pcb, err_t err) {
    PostState *st = (PostState*)arg;
    if (!st) return ERR_ARG;
    if (err != ERR_OK) {
        st->result = err;
        st->complete = true;
        return err;
    }
    st->connected = true;
    st->sent_offset = 0;
    st->header_queued = false;

    // Make sure callbacks know our state
    altcp_arg(pcb, st);
    altcp_sent(pcb, post_sent_cb);
    altcp_poll(pcb, post_poll_cb, 8);

    // Build and send headers
    char hdr[384];
    int n = snprintf(hdr, sizeof(hdr),
                     "POST %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %u\r\n"
                     "%s"
                     "Connection: close\r\n\r\n",
                     st->url,
                     st->host,
                     st->ctype ? st->ctype : "application/octet-stream",
                     (unsigned)st->body_len,
                     st->extra_headers ? st->extra_headers : "");
    if (n >= (int)sizeof(hdr)) n = -1;  // header would be truncated
    if (n <= 0) {
        st->result = ERR_VAL;
        st->complete = true;
        return ERR_VAL;
    }

    err_t e = altcp_write(pcb, hdr, (u16_t)n, TCP_WRITE_FLAG_COPY);
    if (e != ERR_OK) {
        // If headers can’t be queued, abort.
        // (ERR_MEM here is unusual at connect time; treat as fatal to simplify.)
        st->result = e;
        st->complete = true;
        return e;
    }
    st->header_queued = true;

    if (st->body && st->body_len) {
        printf("Header queued. Body length: %zu\n", st->body_len);
        e = post_send_next_chunk(pcb, st);
        if (e != ERR_OK) {
            // Only fatal errors bubble up here.
            return e;
        }
    }

    // Ensure whatever is queued gets pushed
    altcp_output(pcb);
    return ERR_OK;
}


static err_t get_connected(void *arg, struct altcp_pcb *pcb, err_t err) {
    PostState *st = (PostState*)arg;
    if (err != ERR_OK) { st->result = err; st->complete = true; return err; }
    st->connected = true;
    // Build request
    char hdr[128];
    int n = snprintf(hdr, sizeof(hdr),
                     "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", st->url, st->host);
    if (n <= 0) { st->result = -1; st->complete = true; return ERR_VAL; }
    err_t e = altcp_write(pcb, hdr, (u16_t)n, TCP_WRITE_FLAG_COPY);
    if (e != ERR_OK) { st->result = e; st->complete = true; return e; }
    if (st->body && st->body_len) {
        e = altcp_write(pcb, st->body, (u16_t)st->body_len, TCP_WRITE_FLAG_COPY);
        if (e != ERR_OK) { st->result = e; st->complete = true; return e; }
    }
    altcp_output(pcb);
    return ERR_OK;
}

// request.cpp
HttpRequest::HttpRequest(const std::string& url) : HttpRequest(url, std::string(HOST), (uint16_t)PORT) {}

HttpRequest::HttpRequest(const std::string& url, const std::string& host, uint16_t port)
    : url_(url),
      host_(host),
      ctype_("application/json")
{
    // Wire PostState to our backing strings/buffers
    st_.url   = url_.c_str();
    st_.host  = host_.c_str();
    st_.port  = port;
    st_.ctype = ctype_.c_str();

    st_.resp     = &ctx_;
    st_.body     = nullptr;
    st_.body_len = 0;

    this->ctx = cyw43_arch_async_context();   // DO NOT deinit in dtor

    printf("HTTP request created for URL: %s:%d%s\n", st_.host, st_.port, st_.url);
}

HttpRequest::~HttpRequest() {}

void HttpRequest::reinitialize() {
    body_.clear();
    body_bin_.clear();
    raw_body_.clear();
    raw_body_.shrink_to_fit();  // free memory

    // Clear response context
    ctx_.len = 0;
    ctx_.body[0] = '\0';
    ctx_.ok = false;

    // Reset state
    st_.body = nullptr;
    st_.body_len = 0;
    st_.result = 0;
    st_.complete = false;
    st_.connected = false;
    st_.resolved = false;
    st_.sent_offset = 0;
    st_.header_queued = false;
    ip_addr_set_zero(&st_.addr);
    
    // PCB should already be closed by post()/get(), but ensure it's nulled
    st_.pcb = nullptr;
}

void HttpRequest::set_body(const uint8_t* data, size_t len) {
    body_.clear();
    body_.reserve(len * 3 + len + 32);

    body_.append("{\"x\": \"");
    for (size_t i = 0; i < len; ++i) {
        char numbuf[4];
        int n = snprintf(numbuf, sizeof(numbuf), "%u", data[i]);
        body_.append(numbuf, n);

        if (i < len - 1)
            body_ += ',';
    }
    body_.append("\"}");

    st_.body = body_.c_str();
    st_.body_len = body_.size();
    st_.ctype = "application/json";
}

void HttpRequest::set_body_raw(const char* data, size_t len) {
    body_.clear();
    body_.reserve(len);
    body_.append(data, len);
    st_.body = body_.c_str();
    st_.body_len = body_.size();
    st_.ctype = "application/json";

    printf("Body content: %.*s\n", (int)st_.body_len, st_.body);
}

void HttpRequest::set_body_binary(const uint8_t* data, size_t len) {
    // Directly set the body pointer and length, no copying.
    st_.body = reinterpret_cast<const char*>(data);
    st_.body_len = len;
    st_.ctype = "application/octet-stream";

    printf("Set binary body of length %zu\n", st_.body_len);
}

std::string HttpRequest::get_body() const {
    return body_;
}

void HttpRequest::parse_response_data(const char *json, size_t len, ResponseDataStr *out) {

    if (!json || !out) return;
    out->status_ok = false;
    
    // DO NOT free out->y here - it's the caller's responsibility!
    // The ResponseDataStr is created fresh on the stack each time.
    out->y = NULL;
    out->error[0] = '\0';

    printf("Response JSON (%zu bytes): %.*s\n", len, (int)len, json);
    // Ensure NUL terminated for strstr/strtof. Our buffer is NUL-terminated by construction.
    const char *key = strstr(json, "\"data\"");
    if (!key) {
        const char *ek = strstr(json, "\"error\"");
        const char *ec = ek ? strchr(ek, ':') : nullptr;
        const char *eq = ec ? strchr(ec, '"') : nullptr;
        if (eq) {
            eq++;
            const char *ee = strchr(eq, '"');
            size_t n = ee ? (size_t)(ee - eq) : 0;
            if (n >= sizeof(out->error)) n = sizeof(out->error) - 1;
            memcpy(out->error, eq, n);
            out->error[n] = '\0';
        }
        return;
    }

    const char *colon = strchr(key, ':');
    if (!colon) return;

    const char *start = strchr(colon, '"');
    if (!start) return;
    start++; // move past the quote

    const char *end = strchr(start, '"');
    if (!end) return;

    size_t value_len = (size_t)(end - start);
    printf("Parsed string value length: %zu\n", value_len);
    if (value_len == 0) return;

    // allocate memory for string + null terminator
    char *buf = (char *)malloc(value_len + 1);
    if (!buf) return;

    memcpy(buf, start, value_len);
    buf[value_len] = '\0';

    out->y = buf;
    out->status_ok = true;
    return;
}


void HttpRequest::set_dummy_mnist_data() {
    body_.clear();
    body_.assign("{\"x\": \"");
    body_.append(TEST_DATA);
    body_.append("\"}");
    st_.body = body_.c_str();
    st_.body_len = body_.size();
}

void HttpRequest::set_dummy_image_data() { set_dummy_mnist_data(); }

void HttpRequest::set_dummy_imu_data() { set_dummy_mnist_data(); }

ResponseDataStr HttpRequest::get() {
    // Implement GET request logic
    ip_addr_t tmp;
    err_t er = dns_gethostbyname(st_.host, &tmp, dns_found_cb, &st_);

    ResponseDataStr data{};
    data.status_ok = false;
    data.y = NULL;

    if (er == ERR_OK) {
        st_.addr = tmp;
        st_.resolved = true;
    }

    while (!st_.resolved) {
        async_context_poll(this->ctx);
        async_context_wait_for_work_ms(this->ctx, 50);
    }

    if (ip_addr_isany_val(st_.addr)) {
        printf("DNS resolution failed (GET)\n");
        return data;
    }

    st_.pcb = altcp_new_ip_type(NULL, IPADDR_TYPE_ANY);
    if (!st_.pcb) {
        printf("Failed to create PCB (GET)\n");
        return data;
    }
    
    altcp_arg(st_.pcb, &st_);
    altcp_err(st_.pcb, post_err);
    altcp_recv(st_.pcb, post_recv);

    err_t ce = altcp_connect(st_.pcb, &st_.addr, st_.port, get_connected);
    if (ce != ERR_OK) { 
        printf("altcp_connect failed: %d\n", ce);
        altcp_abort(st_.pcb);
        st_.pcb = nullptr;
        return data;
    }

    while (!st_.complete) {
        async_context_poll(this->ctx);
        async_context_wait_for_work_ms(this->ctx, 100);
    }

    // Strip headers
    if (st_.resp && st_.resp->len > 0) {
        char* sep = strstr(st_.resp->body, "\r\n\r\n");
        if (sep) {
            size_t header_len = (size_t)(sep + 4 - st_.resp->body);
            size_t body_only = st_.resp->len > header_len ? st_.resp->len - header_len : 0;
            if (body_only > 0) {
                memmove(st_.resp->body, sep + 4, body_only);
            }
            st_.resp->len = body_only;
            st_.resp->body[st_.resp->len] = '\0';
        }
    }
    if (st_.pcb) {
        altcp_close(st_.pcb);
        st_.pcb = nullptr;
    }

    if (st_.result != 0) {
        printf("Failed: result=%d\n", st_.result);
        if (st_.resp && st_.resp->len) printf("Partial data:\n%.*s\n", (int)st_.resp->len, st_.resp->body);
        return data;
    }

    parse_response_data(st_.resp->body, st_.resp->len, &data);
    return data;
}

ResponseDataStr HttpRequest::post() {

    ip_addr_t tmp;
    // Pass `&st_` (PostState*), NOT `&state` (PostState**)
    err_t er = dns_gethostbyname(st_.host, &tmp, dns_found_cb, &st_);

    ResponseDataStr data{};
    data.status_ok = false;
    data.y = NULL;

    if (er == ERR_OK) {
        st_.addr = tmp;
        st_.resolved = true;
    }

    while (!st_.resolved) {
        async_context_poll(this->ctx);
        async_context_wait_for_work_ms(this->ctx, 50);
    }

    if (ip_addr_isany_val(st_.addr)) {
        printf("DNS resolution failed (POST)\n");
        return data;
    }

    st_.pcb = altcp_new_ip_type(NULL, IPADDR_TYPE_ANY);
    if (!st_.pcb) {
        printf("Failed to create PCB (POST)\n");
        return data;
    }

    // Pass pointer to PostState (not address-of the pointer)
    altcp_arg(st_.pcb, &st_);
    altcp_err(st_.pcb, post_err);
    altcp_recv(st_.pcb, post_recv);

    err_t ce = altcp_connect(st_.pcb, &st_.addr, st_.port, post_connected_by_chunk);
    if (ce != ERR_OK) { 
        printf("altcp_connect failed: %d\n", ce);
        altcp_abort(st_.pcb);
        st_.pcb = nullptr;
        return data;
    }

    while (!st_.complete) {
        async_context_poll(this->ctx);
        async_context_wait_for_work_ms(this->ctx, 100);
    }

    // Strip headers
    if (st_.resp && st_.resp->len > 0) {
        char* eol = strstr(st_.resp->body, "\r\n");
        if (eol) printf("HTTP response: %.*s\n", (int)(eol - st_.resp->body), st_.resp->body);   // e.g. 307 = redirect to https
        sscanf(st_.resp->body, "HTTP/%*d.%*d %d", &data.http_status);
        char* sep = strstr(st_.resp->body, "\r\n\r\n");
        if (sep) {
            size_t header_len = (size_t)(sep + 4 - st_.resp->body);
            size_t body_only = st_.resp->len > header_len ? st_.resp->len - header_len : 0;
            if (body_only > 0) {
                memmove(st_.resp->body, sep + 4, body_only);
            }
            st_.resp->len = body_only;
            st_.resp->body[st_.resp->len] = '\0';
        }
    }
    if (st_.pcb) {
        altcp_close(st_.pcb);
        st_.pcb = nullptr;
    }

    if (st_.result != 0) {
        printf("Failed: result=%d\n", st_.result);
        if (st_.resp && st_.resp->len) printf("Partial data:\n%.*s\n", (int)st_.resp->len, st_.resp->body);
        return data;
    }
    
    parse_response_data(st_.resp->body, st_.resp->len, &data);
    return data;
}

// ---------------------------------------------------------------------------------------------
// post_ex(): bounded, lwIP-lock-safe POST
// ---------------------------------------------------------------------------------------------

// A DNS lookup cannot be cancelled in lwIP, so its callback may fire after the request object is
// gone. The callback therefore never dereferences its argument: it checks a generation number and
// a registry pointer that post_ex() clears (under the lwIP lock) before it returns.
static PostState *volatile g_dns_target = nullptr;
static volatile uintptr_t g_dns_generation = 0;

static void dns_found_cb_safe(const char *name, const ip_addr_t *ipaddr, void *arg) {
    (void)name;
    PostState *st = g_dns_target;
    if (!st || (uintptr_t)arg != g_dns_generation) return;  // stale lookup
    if (ipaddr) {
        st->addr = *ipaddr;
        st->resolved = true;
    } else {
        st->resolved = true;   // addr stays zero: reported as DNS failure
    }
}

static void abort_pcb(PostState *st) {
    cyw43_arch_lwip_begin();
    if (st->pcb) {
        altcp_arg(st->pcb, nullptr);  // callbacks must not touch the state any more
        altcp_abort(st->pcb);
        st->pcb = nullptr;
    }
    cyw43_arch_lwip_end();
}

static void clear_dns_target() {
    cyw43_arch_lwip_begin();
    g_dns_target = nullptr;
    cyw43_arch_lwip_end();
}

static bool header_value(const char *headers, size_t len, const char *name, long *out) {
    // case-insensitive search for "\r\n<name>:" in the header block
    const size_t nl = strlen(name);
    for (size_t i = 0; i + nl + 2 < len; ++i) {
        if (headers[i] != '\r' || headers[i + 1] != '\n') continue;
        if (strncasecmp(headers + i + 2, name, nl) == 0 && headers[i + 2 + nl] == ':') {
            *out = strtol(headers + i + 3 + nl, nullptr, 10);
            return true;
        }
    }
    return false;
}

// True once the headers announced a Content-Length and that many body bytes arrived. Lets the
// caller stop waiting for a FIN from servers/proxies that keep the connection open. Call with the
// lwIP lock held (the receive callback appends to the buffer).
static bool http_response_complete(const ResponseCtx *r) {
    const char *sep = strstr(r->body, "\r\n\r\n");
    long cl = 0;
    if (!sep || !header_value(r->body, (size_t)(sep - r->body) + 2, "content-length", &cl)) return false;
    return (size_t)(r->body + r->len - (sep + 4)) >= (size_t)cl;
}

HttpResult HttpRequest::post_ex() {
    HttpResult res;
    ctx_.len = 0;
    ctx_.body[0] = '\0';
    ctx_.ok = false;
    st_.complete = false;
    st_.connected = false;
    st_.resolved = false;
    st_.sent_offset = 0;
    st_.acked = 0;
    st_.header_queued = false;
    st_.result = 0;
    st_.pcb = nullptr;
    st_.extra_headers = extra_headers_.c_str();
    ip_addr_set_zero(&st_.addr);

    const absolute_time_t start = get_absolute_time();
    const absolute_time_t connect_deadline = delayed_by_ms(start, connect_timeout_ms_);
    const absolute_time_t total_deadline = delayed_by_ms(start, total_timeout_ms_);

    // --- DNS -------------------------------------------------------------------------------
    ip_addr_t tmp;
    cyw43_arch_lwip_begin();
    g_dns_generation = g_dns_generation + 1;
    g_dns_target = &st_;
    err_t er = dns_gethostbyname(st_.host, &tmp, dns_found_cb_safe, (void *)g_dns_generation);
    cyw43_arch_lwip_end();
    if (er == ERR_OK) {
        st_.addr = tmp;
        st_.resolved = true;
    } else if (er != ERR_INPROGRESS) {
        clear_dns_target();
        res.reason = HttpResult::kDnsFailed;
        res.lwip_err = er;
        return res;
    }
    while (!st_.resolved) {
        if (time_reached(connect_deadline)) {
            clear_dns_target();
            res.reason = HttpResult::kDnsTimeout;
            return res;
        }
        async_context_poll(this->ctx);
        async_context_wait_for_work_ms(this->ctx, 20);
    }
    clear_dns_target();
    if (ip_addr_isany_val(st_.addr)) {
        res.reason = HttpResult::kDnsFailed;
        return res;
    }

    // --- connect ---------------------------------------------------------------------------
    cyw43_arch_lwip_begin();
    st_.pcb = altcp_new_ip_type(NULL, IPADDR_TYPE_ANY);
    err_t ce = ERR_MEM;
    if (st_.pcb) {
        altcp_arg(st_.pcb, &st_);
        altcp_err(st_.pcb, post_err);
        altcp_recv(st_.pcb, post_recv);
        ce = altcp_connect(st_.pcb, &st_.addr, st_.port, post_connected_by_chunk);
        if (ce != ERR_OK) {
            altcp_abort(st_.pcb);
            st_.pcb = nullptr;
        }
    }
    cyw43_arch_lwip_end();
    if (ce != ERR_OK) {
        res.reason = HttpResult::kConnectFailed;
        res.lwip_err = ce;
        return res;
    }

    // --- exchange --------------------------------------------------------------------------
    while (!st_.complete) {
        if (!st_.connected && time_reached(connect_deadline)) {
            abort_pcb(&st_);
            res.reason = HttpResult::kConnectTimeout;
            return res;
        }
        if (time_reached(total_deadline)) {
            abort_pcb(&st_);
            res.reason = HttpResult::kResponseTimeout;
            return res;
        }
        cyw43_arch_lwip_begin();
        const bool done = http_response_complete(&ctx_);
        cyw43_arch_lwip_end();
        if (done) break;
        async_context_poll(this->ctx);
        async_context_wait_for_work_ms(this->ctx, 20);
    }

    cyw43_arch_lwip_begin();
    if (st_.pcb) {
        altcp_arg(st_.pcb, nullptr);
        if (altcp_close(st_.pcb) != ERR_OK) altcp_abort(st_.pcb);
        st_.pcb = nullptr;
    }
    cyw43_arch_lwip_end();

    // --- interpret the response --------------------------------------------------------------
    res.lwip_err = st_.result;
    res.truncated = ctx_.len + 1 >= sizeof(ctx_.body);
    char *sep = strstr(ctx_.body, "\r\n\r\n");
    int status = 0;
    if (!sep || sscanf(ctx_.body, "HTTP/%*d.%*d %d", &status) != 1) {
        res.reason = (st_.result != 0) ? HttpResult::kConnectionReset : HttpResult::kBadResponse;
        return res;
    }
    res.http_status = status;
    res.body = sep + 4;
    res.body_len = (size_t)(ctx_.body + ctx_.len - (sep + 4));
    long content_length = -1;
    if (header_value(ctx_.body, (size_t)(sep - ctx_.body) + 2, "content-length", &content_length) &&
        content_length >= 0 && !res.truncated && res.body_len < (size_t)content_length) {
        // connection ended before the announced body arrived
        res.reason = (st_.result != 0) ? HttpResult::kConnectionReset : HttpResult::kBadResponse;
        return res;
    }
    res.reason = HttpResult::kOk;
    return res;
}
