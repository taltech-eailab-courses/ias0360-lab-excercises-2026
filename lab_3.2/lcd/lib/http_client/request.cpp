#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <sstream>
#include <cstdint>
#include <cstdlib>
#include <cstring> 

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "request.hpp"
#include "sample_data.h"

#define POST_CHUNK_SIZE 512  // can adjust to 256–1024 depending on testing

static void post_err(void *arg, err_t err) {
    PostState *st = (PostState*)arg;
    if (st) { st->result = err ? err : -1; st->complete = true; }
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
                        "POST %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %u\r\n"
                        "Connection: close\r\n\r\n",
                        st->url, 
                        st->host, 
                        st->ctype ? st->ctype : "application/octet-stream", 
                        (unsigned)st->body_len);

    if (n <= 0) { st->result = -1; st->complete = true; return ERR_VAL; }

    err_t e = altcp_write(pcb, hdr, (u16_t)n, TCP_WRITE_FLAG_COPY);
    if (e != ERR_OK) { st->result = e; st->complete = true; return e; }
    if (st->body && st->body_len) {
        printf("Header: %.*s", n, hdr);
        printf("POST body length: %zu\n", st->body_len);

        printf("body: %.*s\n", (int)st->body_len, st->body);
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
    printf("ACKed %u bytes\n", acked);
    // After ACK freed space, try to queue more
    return post_send_next_chunk(pcb, st);
}

// Optional safety net: if no ACKs arrive but window opens, poll will try again.
static err_t post_poll_cb(void *arg, struct altcp_pcb *pcb) {
    PostState *st = (PostState*)arg;
    printf("[DEBUG] post_poll_cb: st=%p\n", st);
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
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
                     "POST %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %u\r\n"
                     "Connection: close\r\n\r\n",
                     st->url,
                     st->host,
                     st->ctype ? st->ctype : "application/octet-stream",
                     (unsigned)st->body_len);
    printf("header content: %.*s", n, hdr);
    printf("Header sent (%d bytes)\n", n);
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
HttpRequest::HttpRequest(const std::string& url)
    : url_(url),
      host_(HOST),                 // make sure HOST and PORT are defined macros or constants
      ctype_("application/json")
{
    // Wire PostState to our backing strings/buffers
    st_.url   = url_.c_str();
    st_.host  = host_.c_str();
    st_.port  = PORT;
    st_.ctype = ctype_.c_str();

    st_.resp     = &ctx_;
    st_.resp->len = 0;
    st_.resp->body[0] = '\0';
    
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

    ctx_.len = 0;
    ctx_.body[0] = '\0';

    st_.body = nullptr;
    st_.body_len = 0;
    st_.result = 0;
    st_.complete = false;
    st_.connected = false;
    st_.resolved = false;
    ip_addr_set_zero(&st_.addr);

    st_.resp->len = 0;
    st_.resp->body[0] = '\0';
}

void HttpRequest::reset_pcb(void) {
    if (!st_.pcb) {
        printf("[DEBUG] reset_pcb: pcb already null, skipping.\n");
        return;
    }

    // Skip if connection already completed or aborted
    if (st_.complete || st_.result == ERR_CLSD || st_.result == ERR_ABRT) {
        printf("[DEBUG] reset_pcb: skipping close (already completed/aborted)\n");
        st_.pcb = nullptr;
        return;
    }

    err_t e = altcp_close(st_.pcb);
    if (e != ERR_OK) {
        printf("[DEBUG] altcp_close failed: %d (%s)\n", e, lwip_strerr(e));
        altcp_abort(st_.pcb);
    }

    st_.pcb = nullptr;
    printf("[DEBUG] reset_pcb: safely closed\n");
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
    free(out->y);        // free old memory if reusing struct
    out->y = NULL;

    printf("Response JSON (%zu bytes): %.*s\n", len, (int)len, json);
    // Ensure NUL terminated for strstr/strtof. Our buffer is NUL-terminated by construction.
    const char *key = strstr(json, "\"data\"");
    if (!key) return;

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
    body_.append(MNIST_TEST_DATA);
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

    if (ip_addr_isany_val(st_.addr)) return data;

    st_.pcb = altcp_new_ip_type(NULL, IPADDR_TYPE_ANY);
    if (!st_.pcb) return data;
    
    altcp_arg(st_.pcb, &st_);
    altcp_err(st_.pcb, post_err);
    altcp_recv(st_.pcb, post_recv);

    err_t ce = altcp_connect(st_.pcb, &st_.addr, st_.port, get_connected);
    if (ce != ERR_OK) { altcp_close(st_.pcb); return data; }

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
    altcp_close(st_.pcb);

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
    // data.y = 0.0f;

    if (er == ERR_OK) {
        st_.addr = tmp;
        st_.resolved = true;
    }

    while (!st_.resolved) {
        async_context_poll(this->ctx);
        async_context_wait_for_work_ms(this->ctx, 50);
    }

    if (ip_addr_isany_val(st_.addr)) return data;

    st_.pcb = altcp_new_ip_type(NULL, IPADDR_TYPE_ANY);
    if (!st_.pcb) return data;

    // Pass pointer to PostState (not address-of the pointer)
    altcp_arg(st_.pcb, &st_);
    altcp_err(st_.pcb, post_err);
    altcp_recv(st_.pcb, post_recv);

    err_t ce = altcp_connect(st_.pcb, &st_.addr, st_.port, post_connected_by_chunk);
    if (ce != ERR_OK) { altcp_close(st_.pcb); return data; }

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
    altcp_close(st_.pcb);

    if (st_.result != 0) {
        printf("Failed: result=%d\n", st_.result);
        if (st_.resp && st_.resp->len) printf("Partial data:\n%.*s\n", (int)st_.resp->len, st_.resp->body);
        return data;
    }
    
    parse_response_data(st_.resp->body, st_.resp->len, &data);
    return data;
}

static err_t post_connected_stream(void *arg, struct altcp_pcb *pcb, err_t err) {
    PostState *st = (PostState*)arg;
    if (err != ERR_OK) { st->result = err; st->complete = true; return err; }

    st->header_queued = false;
    st->connected = true;

    altcp_arg(pcb, st);
    altcp_sent(pcb, post_sent_cb);
    altcp_poll(pcb, post_poll_cb, 8);

    // Build and send the HTTP header
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
                        "POST %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %u\r\n"
                        "Connection: close\r\n\r\n",
                        st->url,
                        st->host,
                        st->ctype ? st->ctype : "application/octet-stream",
                        (unsigned)st->body_len);
    if (n <= 0) {
        st->result = -1;
        st->complete = true;
        return ERR_VAL;
    }

    printf("header content: %.*s", n, hdr);
    printf("Header sent (%d bytes)\n", n);
    err_t e = altcp_write(pcb, hdr, (u16_t)n, TCP_WRITE_FLAG_COPY);
    if (e != ERR_OK) {
        printf("Header send failed: %d (%s)\n", e, lwip_strerr(e));
        st->result = e;
        st->complete = true;
        return e;
    }

    st->header_queued = true;

    altcp_output(pcb);
    return ERR_OK;
}

err_t HttpRequest::set_request_header(size_t total_size, const char* content_type) {
    ip_addr_t tmp;
    err_t er = dns_gethostbyname(st_.host, &tmp, dns_found_cb, &st_);
    if (er == ERR_OK) {
        st_.addr = tmp;
        st_.resolved = true;
    }
    while (!st_.resolved) {
        async_context_poll(this->ctx);
        async_context_wait_for_work_ms(this->ctx, 50);
    }
    if (ip_addr_isany_val(st_.addr)) return ERR_CONN;

    st_.pcb = altcp_new_ip_type(NULL, IPADDR_TYPE_ANY);
    if (!st_.pcb) return ERR_MEM;

    st_.body_len = total_size;
    st_.ctype = content_type;

    altcp_arg(st_.pcb, &st_);
    altcp_err(st_.pcb, post_err);
    altcp_sent(st_.pcb, post_sent_cb);
    altcp_poll(st_.pcb, post_poll_cb, 8);
    altcp_recv(st_.pcb, post_recv);
    

    printf("Connecting to %s:%d...\n", st_.host, st_.port);
    err_t ce = altcp_connect(st_.pcb, &st_.addr, st_.port, post_connected_stream);
    if (ce != ERR_OK) {
        printf("altcp_connect failed: %d (%s)\n", ce, lwip_strerr(ce));
        reset_pcb();
        return ce;
    }
    
    printf("Connection established and header sent\n");
    return ERR_OK;
}

err_t HttpRequest::post_chunked(size_t total_size, const char* chunk_data, size_t chunk_len) {
    printf("[DEBUG] post_chunked start: pcb=%p connected=%d complete=%d chunk_len=%zu\n",
        st_.pcb, st_.connected, st_.complete, chunk_len);

    if (!st_.pcb) {
        printf("[ERROR] PCB is null!\n");
        return ERR_CONN;
    }
    if (!st_.connected) {
        printf("[ERROR] PCB not connected yet!\n");
        return ERR_ARG;
    }
    if (!chunk_data || chunk_len == 0) {
        printf("[ERROR] Invalid chunk arguments (ptr=%p, len=%zu)\n", chunk_data, chunk_len);
        return ERR_ARG;
    }
    printf("[DEBUG] pcb=%p connected=%d complete=%d state=%d\n",
        (void*)st_.pcb, st_.connected, st_.complete,
        st_.pcb ? (int)st_.pcb->state : -1);

    body_.clear();
    body_.reserve(chunk_len);
    body_.append(chunk_data, chunk_len);
    st_.body = nullptr;  // clear previous body pointer
    st_.body = body_.c_str();
    st_.body_len = body_.size();

    // err_t e = altcp_write(st_.pcb, chunk_data, (u16_t)chunk_len, TCP_WRITE_FLAG_COPY);
    err_t e = altcp_write(st_.pcb, st_.body, (u16_t)st_.body_len, TCP_WRITE_FLAG_COPY);
    if (e == ERR_OK) {
        printf("Sent text chunk of %zu bytes: \"%.*s\"\n", chunk_len, (int)chunk_len, chunk_data);
        altcp_output(st_.pcb);
    } else {
        printf("Chunk send failed: %d (%s)\n", e, lwip_strerr(e));
        st_.result = e;
        return e;
    }

    async_context_poll(this->ctx);
    async_context_wait_for_work_ms(this->ctx, 20);
    return ERR_OK;
}


ResponseDataStr HttpRequest::retrieve_data(void) {
    ResponseDataStr data{};
    data.status_ok = false;
    data.y = NULL;

    // Wait for all chunks to be sent (st_.complete is set after last chunk in post_chunked)
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

    if (!st_.pcb) {
        printf("[DEBUG] post_recv: FIN received — remote closed connection\n");
        st_.complete = true;
        return data;
    }

    printf("Result=%d\n", st_.result);
    if (st_.result != 0) {
        printf("Failed: result=%d\n", st_.result);
        if (st_.resp && st_.resp->len) printf("Partial data:\n%.*s\n", (int)st_.resp->len, st_.resp->body);
        return data;
    }

    parse_response_data(st_.resp->body, st_.resp->len, &data);
    return data;
}

void HttpRequest::await_until_connected() {
    while (!st_.connected) {
        async_context_poll(this->ctx);
        async_context_wait_for_work_ms(this->ctx, 50);
    }
}