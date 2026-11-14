#include "network/https_stream.h"
#include <esp_crt_bundle.h>

static const int MAX_REDIRECTS = 5;

int HttpsStream::open(const char* url, const char* accept, const char* user_agent) {
    close();
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 15000;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 2048;
    cfg.user_agent = user_agent;
    cfg.disable_auto_redirect = true;
    _client = esp_http_client_init(&cfg);
    if (!_client) {
        _error = "HTTP client init failed";
        return -1;
    }
    if (accept) esp_http_client_set_header(_client, "Accept", accept);

    for (int hop = 0; hop <= MAX_REDIRECTS; hop++) {
        esp_err_t err = esp_http_client_open(_client, 0);
        if (err != ESP_OK) {
            _error = String("Connection failed: ") + esp_err_to_name(err);
            close();
            return -1;
        }
        _length = esp_http_client_fetch_headers(_client);
        int status = esp_http_client_get_status_code(_client);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            esp_http_client_flush_response(_client, nullptr);
            if (esp_http_client_set_redirection(_client) != ESP_OK) {
                _error = "Bad redirect";
                close();
                return -1;
            }
            esp_http_client_close(_client);
            continue;
        }
        return status;
    }
    _error = "Too many redirects";
    close();
    return -1;
}

void HttpsStream::close() {
    if (_client) {
        esp_http_client_close(_client);
        esp_http_client_cleanup(_client);
        _client = nullptr;
    }
    _buf_len = _buf_pos = 0;
}

int HttpsStream::readChunk(uint8_t* buf, size_t len) {
    if (!_client) return -1;
    int n = esp_http_client_read(_client, (char*)buf, (int)len);
    if (n < 0) {
        _error = "Read failed";
        return -1;
    }
    return n;
}

int HttpsStream::read() {
    if (_buf_pos >= _buf_len) {
        int n = readChunk(_buf, sizeof(_buf));
        if (n <= 0) return -1;
        _buf_len = (size_t)n;
        _buf_pos = 0;
    }
    return _buf[_buf_pos++];
}

size_t HttpsStream::readBytes(char* buf, size_t len) {
    size_t n = 0;
    while (n < len) {
        int c = read();
        if (c < 0) break;
        buf[n++] = (char)c;
    }
    return n;
}
