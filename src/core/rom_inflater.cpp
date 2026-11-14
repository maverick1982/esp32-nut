#include "core/rom_inflater.h"
#include <esp_heap_caps.h>
#include <miniz.h>

static const size_t WINDOW = TINFL_LZ_DICT_SIZE;

RomInflater::~RomInflater() {
    release();
}

void RomInflater::release() {
    free(_state);
    heap_caps_free(_window);
    _state = nullptr;
    _window = nullptr;
}

bool RomInflater::begin(IByteSink* out) {
    release();
    _out = out;
    _pos = 0;
    _done = false;
    _state = (tinfl_decompressor*)malloc(sizeof(tinfl_decompressor));
    _window = (uint8_t*)heap_caps_malloc(WINDOW, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_window) _window = (uint8_t*)heap_caps_malloc(WINDOW, MALLOC_CAP_8BIT);
    if (!_state || !_window) {
        release();
        return false;
    }
    tinfl_init((tinfl_decompressor*)_state);
    return true;
}

bool RomInflater::feed(const uint8_t* data, size_t len) {
    if (!_state || _done) return len == 0;
    for (;;) {
        size_t in_bytes = len;
        size_t out_bytes = WINDOW - _pos;
        tinfl_status st = tinfl_decompress((tinfl_decompressor*)_state, data, &in_bytes, _window, _window + _pos,
                                           &out_bytes, TINFL_FLAG_HAS_MORE_INPUT);
        data += in_bytes;
        len -= in_bytes;
        if (out_bytes > 0) {
            if (!_out->write(_window + _pos, out_bytes)) return false;
            _pos = (_pos + out_bytes) & (WINDOW - 1);
        }
        if (st == TINFL_STATUS_DONE) {
            _done = true;
            release();
            return len == 0;
        }
        if (st < 0) return false;
        if (st == TINFL_STATUS_NEEDS_MORE_INPUT && len == 0) return true;
    }
}
