#ifndef HTTPS_STREAM_H
#define HTTPS_STREAM_H

#include <Arduino.h>
#include <esp_http_client.h>

// HTTPS GET with certificate verification (ESP-IDF CA bundle), redirects followed.
// read() and readBytes() are the ArduinoJson reader interface.
class HttpsStream {
public:
    ~HttpsStream() { close(); }

    // HTTP status of the final response, or -1 with error() set
    int open(const char* url, const char* accept, const char* user_agent);
    void close();

    int64_t contentLength() const { return _length; }
    const String& error() const { return _error; }

    // Bytes read, 0 at the end, -1 on error
    int readChunk(uint8_t* buf, size_t len);

    int read();
    size_t readBytes(char* buf, size_t len);

private:
    esp_http_client_handle_t _client = nullptr;
    int64_t _length = -1;
    String _error;
    uint8_t _buf[256];
    size_t _buf_len = 0;
    size_t _buf_pos = 0;
};

#endif // HTTPS_STREAM_H
