#ifndef ROM_INFLATER_H
#define ROM_INFLATER_H

#include "ZipEntryExtractor.h"

struct tinfl_decompressor_tag;

// Raw deflate with the miniz tinfl in the chip ROM; 32 KB window on the heap
class RomInflater : public IInflater {
public:
    ~RomInflater();
    bool begin(IByteSink* out) override;
    bool feed(const uint8_t* data, size_t len) override;
    bool finished() const override { return _done; }

private:
    void release();

    IByteSink* _out = nullptr;
    tinfl_decompressor_tag* _state = nullptr;
    uint8_t* _window = nullptr;
    size_t _pos = 0;
    bool _done = false;
};

#endif // ROM_INFLATER_H
