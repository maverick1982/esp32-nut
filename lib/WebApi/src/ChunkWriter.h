#ifndef CHUNK_WRITER_H
#define CHUNK_WRITER_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

// Destinazione di serializeJson()/serializeJsonPretty() di ArduinoJson che passa l'uscita
// a un sink a blocchi di dimensione fissa, senza costruire tutto il JSON in una String.
// Usata per l'export a pezzi di /api/usb/dump (US-095): abbassa il picco di heap.
// Il buffer lo fornisce il chiamante (cap >= 1) e il writer non alloca memoria.
// Il sink non riceve mai un blocco di 0 byte: in HTTP/1.1 chunked un blocco vuoto
// chiuderebbe la risposta.
class ChunkWriter {
public:
    using Sink = std::function<void(const char*, size_t)>;

    ChunkWriter(char* buf, size_t cap, Sink sink)
        : _buf(buf), _cap(cap), _sink(std::move(sink)) {
        assert(cap > 0);  // con cap 0 write() scriverebbe fuori dal buffer
    }

    size_t write(uint8_t c) {
        _buf[_len++] = static_cast<char>(c);
        _total++;
        if (_len == _cap) flush();
        return 1;
    }

    size_t write(const uint8_t* s, size_t n) {
        size_t left = n;
        while (left > 0) {
            size_t room = _cap - _len;
            size_t take = left < room ? left : room;
            memcpy(_buf + _len, s, take);
            _len += take;
            s += take;
            left -= take;
            if (_len == _cap) flush();
        }
        _total += n;
        return n;
    }

    // Manda il contenuto residuo; senza dati in attesa non chiama il sink
    void flush() {
        if (_len == 0) return;
        _sink(_buf, _len);
        _len = 0;
    }

    // Byte scritti in tutto, mandati o ancora nel buffer
    size_t total() const { return _total; }

private:
    char* _buf;
    size_t _cap;
    Sink _sink;
    size_t _len = 0;
    size_t _total = 0;
};

#endif // CHUNK_WRITER_H
