#ifndef HTTP_CLIENT_REQUEST_HPP
#define HTTP_CLIENT_REQUEST_HPP

#include <string>
#include <map>
#include <memory>
#include <vector>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/async_context.h"

#include "lwip/apps/httpd.h"
#include "lwip/altcp_tls.h"
#include "lwip/apps/sntp.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/altcp.h"
#include "lwip/pbuf.h"

const char HOST[] = "taltech-mles-app.ngrok.io";
const char URL_REQUEST[] = "/api/v1/air_writing";
const int PORT = 80;


const char WIFI_SSID[] = "<YOUR_WIFI_SSID>";
const char WIFI_PASSWORD[] = "<YOUR_WIFI_PASSWORD>";
const char TEST_DATA[] = 
"0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.16862746,0.18431373,0.18431373,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.03529412,0.42352942,0.9764706,0.99215686,0.99215686,0.8156863,0.8117647,0.8117647,0.8117647,0.58431375,0.25490198,0.050980393,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.03529412,0.72156864,0.99607843,0.99215686,0.99215686,0.99215686,0.99607843,0.99215686,0.99215686,0.99215686,0.99607843,0.99215686,0.8352941,0.09803922,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.21568628,0.79607844,0.99607843,0.99607843,0.78039217,0.49803922,0.49803922,0.23529412,0.3647059,0.32941177,0.26666668,0.5921569,0.87058824,0.99607843,0.6313726,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.5411765,0.99215686,0.99215686,0.78039217,0.07450981,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.60784316,0.99215686,0.827451,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.5411765,0.99215686,0.99215686,0.06666667,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.2901961,0.94509804,0.99215686,0.827451,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.4117647,0.99215686,0.99215686,0.4,0.0,0.0,0.0,0.0,0.0,0.0,0.13333334,0.8980392,0.99215686,0.99215686,0.627451,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.58431375,0.99607843,0.8980392,0.15686275,0.0,0.0,0.0,0.14901961,0.6,0.99607843,0.99607843,0.99607843,0.7058824,0.09803922,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.07450981,0.7764706,0.99607843,0.8117647,0.03529412,0.13333334,0.28235295,0.92156863,0.99215686,0.99215686,0.8784314,0.54509807,0.050980393,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.06666667,0.827451,0.99215686,0.84313726,0.9411765,0.99607843,0.99215686,0.91764706,0.5019608,0.06666667,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.4,0.8980392,0.99215686,0.99215686,0.99215686,0.89411765,0.3019608,0.050980393,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.27450982,0.6666667,0.99607843,0.99607843,0.99607843,0.99607843,0.99607843,0.99607843,0.46666667,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.101960786,0.50980395,0.9019608,0.99607843,0.99215686,0.99215686,0.7254902,0.4509804,0.2509804,0.827451,0.99215686,0.972549,0.08235294,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.6509804,0.9098039,0.99215686,0.99215686,0.96862745,0.63529414,0.18039216,0.050980393,0.02745098,0.35686275,0.9607843,0.99215686,0.99607843,0.21960784,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.5019608,0.99215686,0.99215686,0.99215686,0.8235294,0.3647059,0.49803922,0.62352943,0.8,0.99215686,0.99215686,0.99215686,0.89411765,0.05882353,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.5254902,0.94509804,0.99607843,1.0,0.99607843,0.99607843,0.99607843,0.99607843,0.99607843,0.99607843,0.89411765,0.13333334,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.105882354,0.4509804,0.54901963,0.80784315,0.80784315,0.80784315,0.8117647,0.80784315,0.48235294,0.05882353,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0";



typedef struct ResponseCtx {
    char body[1024];
    size_t len;
    bool ok;
} ResponseCtx;

typedef struct ResponseDataStr {
    char *y;
    bool status_ok;
    int http_status;     // 0 if no status line was received
    char error[32];      // "error" code of a JSON error reply, e.g. "character_too_large"
} ResponseDataStr;

// Outcome of HttpRequest::post_ex(): never blocks longer than the configured timeouts.
struct HttpResult {
    enum Reason {
        kOk = 0,
        kDnsFailed,
        kDnsTimeout,
        kConnectFailed,
        kConnectTimeout,
        kResponseTimeout,
        kConnectionReset,   // connection dropped / aborted by the peer
        kBadResponse,       // no HTTP status line, or the body is shorter than Content-Length
    };
    Reason reason = kOk;
    int http_status = 0;    // valid when a status line was received
    int lwip_err = 0;
    const char *body = nullptr;  // points into the HttpRequest object; valid while it lives
    size_t body_len = 0;
    bool truncated = false;      // response did not fit the receive buffer
};

typedef struct {
    const char *url;
    const char *host;
    const char *ctype;
    const char *body;
    size_t body_len;
    size_t sent_offset;   // how many body bytes have been queued
    bool   header_queued; // header was successfully queued

    // tcp
    struct altcp_pcb *pcb;

    // result/flow
    volatile bool complete;
    volatile bool resolved;
    volatile bool connected;
    err_t result;

    ip_addr_t addr;
    uint16_t port;

    ResponseCtx *resp;

    // post_ex(): extra request header lines ("Name: value\r\n"...) and bytes acknowledged by the peer
    const char *extra_headers;
    size_t acked;
} PostState;


class HttpRequest {
public:
    HttpRequest(const std::string& url);
    HttpRequest(const std::string& url, const std::string& host, uint16_t port);
    ~HttpRequest();

    // Bounded variant of post(): DNS + TCP connect must finish within connect_ms and the complete
    // exchange within total_ms (both measured from the call). Takes the lwIP lock around every raw
    // API call, so it is safe to run on a core other than the one that owns the cyw43 async context.
    // Reports the HTTP status and the response body; it does not interpret the body.
    // The request body set with set_body*() must stay valid until the call returns. The blocking
    // wait cannot be interrupted: a cancel request can only take effect after it returns.
    void set_timeouts(uint32_t connect_ms, uint32_t total_ms) { connect_timeout_ms_ = connect_ms; total_timeout_ms_ = total_ms; }
    void set_extra_headers(const char* header_lines) { extra_headers_ = header_lines ? header_lines : ""; }
    HttpResult post_ex();

    void set_body(const uint8_t* data, size_t len);
    // void set_body_binary(const std::vector<uint8_t>& data);
    void set_body_binary(const uint8_t* data, size_t len);
    void set_body_raw(const char* data, size_t len);
    void set_dummy_mnist_data();
    void set_dummy_image_data();
    void set_dummy_imu_data();
    void reinitialize();

    std::string get_body() const;

    ResponseDataStr post();
    ResponseDataStr get();

private:
    // Backing storage that will stay alive
    std::string url_;
    std::string host_;
    std::string ctype_;
    std::string body_;     // holds JSON payload
    std::vector<uint8_t> body_bin_; // holds binary payload
    std::vector<uint8_t> raw_body_;

    ResponseCtx ctx_{};    // persistent response buffer
    PostState   st_{};     // persistent state

    async_context_t* ctx = nullptr;
    uint32_t connect_timeout_ms_ = 10000;
    uint32_t total_timeout_ms_ = 20000;
    std::string extra_headers_;

    void parse_response_data(const char *json, size_t len, ResponseDataStr *out);
};

#endif // HTTP_CLIENT_REQUEST_HPP