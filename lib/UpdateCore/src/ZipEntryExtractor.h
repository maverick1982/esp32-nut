#ifndef ZIP_ENTRY_EXTRACTOR_H
#define ZIP_ENTRY_EXTRACTOR_H

#include <stddef.h>
#include <stdint.h>

class IByteSink {
public:
    virtual ~IByteSink() {}
    virtual bool write(const uint8_t* data, size_t len) = 0;
};

// Raw deflate decoder: decoded bytes go to the sink given to begin()
class IInflater {
public:
    virtual ~IInflater() {}
    virtual bool begin(IByteSink* out) = 0;
    virtual bool feed(const uint8_t* data, size_t len) = 0;
    virtual bool finished() const = 0;
};

// Streams a zip file and writes one stored or deflated entry, decoded, to a sink.
// complete() is true when the entry's size and CRC-32 match its local header.
class ZipEntryExtractor {
public:
    ZipEntryExtractor(const char* entry_name, IInflater* inflater, IByteSink* out);

    // false once an error is found, see error()
    bool feed(const uint8_t* data, size_t len);

    bool found() const { return _found; }
    bool complete() const { return _complete; }
    uint32_t entrySize() const { return _usize; }
    uint32_t written() const { return _written; }
    const char* error() const { return _error; }

private:
    class CrcSink : public IByteSink {
    public:
        explicit CrcSink(ZipEntryExtractor* owner) : _owner(owner) {}
        bool write(const uint8_t* data, size_t len) override;
    private:
        ZipEntryExtractor* _owner;
    };

    enum class State : uint8_t { HEADER, NAME, SKIP, ENTRY, DONE, FAILED };

    bool fail(const char* msg);
    bool startEntry();
    bool endEntry();
    bool entryData(const uint8_t* data, size_t len);

    const char* _name;
    IInflater* _inflater;
    IByteSink* _out;
    CrcSink _crc_sink;

    State _state = State::HEADER;
    uint8_t _header[30];
    size_t _header_len = 0;
    char _entry_name[64];
    size_t _name_len = 0;
    size_t _name_skip = 0;
    uint32_t _remaining = 0;
    bool _is_target = false;
    uint16_t _method = 0;
    uint32_t _crc_expected = 0;
    uint32_t _usize = 0;
    uint32_t _crc = 0;
    uint32_t _written = 0;
    bool _found = false;
    bool _complete = false;
    const char* _error = "";
};

#endif // ZIP_ENTRY_EXTRACTOR_H
