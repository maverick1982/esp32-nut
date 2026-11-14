#include "ZipEntryExtractor.h"
#include <string.h>
#include "Crc32.h"

static const uint32_t SIG_LOCAL = 0x04034b50;
static const uint32_t SIG_CENTRAL = 0x02014b50;
static const uint32_t SIG_END = 0x06054b50;

static uint16_t le16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

ZipEntryExtractor::ZipEntryExtractor(const char* entry_name, IInflater* inflater, IByteSink* out)
    : _name(entry_name), _inflater(inflater), _out(out), _crc_sink(this) {}

bool ZipEntryExtractor::CrcSink::write(const uint8_t* data, size_t len) {
    ZipEntryExtractor* o = _owner;
    if ((uint64_t)o->_written + len > o->_usize) return o->fail("Entry larger than its header says");
    o->_crc = crc32Update(o->_crc, data, len);
    o->_written += (uint32_t)len;
    return o->_out->write(data, len) || o->fail("Write failed");
}

bool ZipEntryExtractor::fail(const char* msg) {
    if (_state != State::FAILED) _error = msg;
    _state = State::FAILED;
    return false;
}

bool ZipEntryExtractor::startEntry() {
    _method = le16(_header + 8);
    _crc_expected = le32(_header + 14);
    uint32_t csize = le32(_header + 18);
    _usize = le32(_header + 22);
    _remaining = csize;
    _name_len = le16(_header + 26);
    _name_skip = le16(_header + 28);
    _is_target = false;
    _state = State::NAME;
    if (_name_len >= sizeof(_entry_name)) {
        _name_skip += _name_len;
        _name_len = 0;
    }
    return true;
}

bool ZipEntryExtractor::entryData(const uint8_t* data, size_t len) {
    if (_method == 0) return _crc_sink.write(data, len);
    return _inflater->feed(data, len) || fail("Inflate failed");
}

bool ZipEntryExtractor::endEntry() {
    if (_method == 8 && !_inflater->finished()) return fail("Compressed data ended early");
    if (_written != _usize) return fail("Entry size mismatch");
    if (_crc != _crc_expected) return fail("Entry CRC-32 mismatch");
    _complete = true;
    _state = State::DONE;
    return true;
}

bool ZipEntryExtractor::feed(const uint8_t* data, size_t len) {
    size_t i = 0;
    while (i < len) {
        switch (_state) {
        case State::FAILED:
            return false;
        case State::DONE:
            return true;
        case State::HEADER: {
            size_t n = sizeof(_header) - _header_len;
            if (n > len - i) n = len - i;
            memcpy(_header + _header_len, data + i, n);
            _header_len += n;
            i += n;
            if (_header_len >= 4) {
                uint32_t sig = le32(_header);
                if (sig == SIG_CENTRAL || sig == SIG_END) {
                    if (!_found) return fail("Entry not found in the zip");
                    _state = State::DONE;
                    return true;
                }
                if (sig != SIG_LOCAL) return fail("Not a zip file");
            }
            if (_header_len == sizeof(_header)) {
                _header_len = 0;
                startEntry();
            }
            break;
        }
        case State::NAME: {
            while (i < len && (_name_len > 0 || _name_skip > 0)) {
                if (_name_len > 0) {
                    size_t pos = le16(_header + 26) - _name_len;
                    _entry_name[pos] = (char)data[i];
                    _name_len--;
                } else {
                    _name_skip--;
                }
                i++;
            }
            if (_name_len > 0 || _name_skip > 0) break;
            uint16_t real_len = le16(_header + 26);
            if (real_len < sizeof(_entry_name)) {
                _entry_name[real_len] = '\0';
            } else {
                _entry_name[0] = '\0';
            }
            if (strcmp(_entry_name, _name) == 0) {
                uint16_t flags = le16(_header + 6);
                if (flags & 0x0001) return fail("Encrypted entry");
                if (flags & 0x0008) return fail("Zip entry with data descriptor not supported");
                if (_method != 0 && _method != 8) return fail("Unsupported compression method");
                if (_method == 0 && _remaining != _usize) return fail("Entry size mismatch");
                _found = true;
                _is_target = true;
                _crc = 0;
                _written = 0;
                if (_method == 8 && !_inflater->begin(&_crc_sink)) return fail("Inflater start failed");
                _state = State::ENTRY;
                if (_remaining == 0 && !endEntry()) return false;
            } else {
                if (le16(_header + 6) & 0x0008) return fail("Zip entry with data descriptor not supported");
                _state = _remaining > 0 ? State::SKIP : State::HEADER;
            }
            break;
        }
        case State::SKIP: {
            size_t n = len - i;
            if (n > _remaining) n = _remaining;
            _remaining -= (uint32_t)n;
            i += n;
            if (_remaining == 0) _state = State::HEADER;
            break;
        }
        case State::ENTRY: {
            size_t n = len - i;
            if (n > _remaining) n = _remaining;
            if (!entryData(data + i, n)) return false;
            _remaining -= (uint32_t)n;
            i += n;
            if (_remaining == 0 && !endEntry()) return false;
            break;
        }
        }
    }
    return _state != State::FAILED;
}
