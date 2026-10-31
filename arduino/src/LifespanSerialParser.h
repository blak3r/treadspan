#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Passive, bounded Modbus RTU framing. No Arduino dependency and no serial writes.
namespace treadspan {
inline uint16_t modbusCrc(const uint8_t* bytes, size_t length) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
    }
    return crc;
}
inline uint16_t word(const uint8_t* bytes) {
    return (uint16_t(bytes[0]) << 8) | bytes[1];
}

class RtuDecoder {
public:
    typedef void (*FrameHandler)(void*, const uint8_t*, size_t, bool);
    explicit RtuDecoder(bool requests) : requests_(requests) {}
    void reset() { length_ = 0; dirty_ = false; last_ = 0; }
    void feed(uint8_t byte, uint32_t now, FrameHandler handler, void* context) {
        // Host-observed silence, not a claim of precise wire-edge timing.
        if (length_ && uint32_t(now - last_) > 100) { length_ = 0; dirty_ = true; }
        last_ = now;
        if (length_ == sizeof(buffer_)) discard(1);
        buffer_[length_++] = byte;
        while (length_ >= 5) {
            const size_t expected = frameLength(buffer_, length_);
            if (!expected) { discard(1); continue; }
            if (length_ < expected) {
                // A corrupt length field must not hide a complete following frame.
                bool found = false;
                for (size_t offset = 1; offset + 5 <= length_; ++offset) {
                    const size_t n = frameLength(buffer_ + offset, length_ - offset);
                    if (n && offset + n <= length_ && valid(buffer_ + offset, n)) {
                        discard(offset); found = true; break;
                    }
                }
                if (found) continue;
                return;
            }
            if (!valid(buffer_, expected)) { discard(1); continue; }
            handler(context, buffer_, expected, dirty_);
            dirty_ = false;
            consume(expected);
        }
    }
private:
    size_t frameLength(const uint8_t* p, size_t n) const {
        if (n < 3 || p[0] != 1) return 0;
        if (p[1] == 6) return 8;
        if (p[1] == 3) {
            if (requests_) return 8;
            return p[2] >= 2 && p[2] <= 250 && !(p[2] & 1) ? size_t(p[2]) + 5 : 0;
        }
        return !requests_ && (p[1] == 0x83 || p[1] == 0x86) ? 5 : 0;
    }
    static bool valid(const uint8_t* p, size_t n) {
        return modbusCrc(p, n - 2) == (uint16_t(p[n - 2]) | (uint16_t(p[n - 1]) << 8));
    }
    void consume(size_t n) { memmove(buffer_, buffer_ + n, length_ - n); length_ -= n; }
    void discard(size_t n) { dirty_ = true; consume(n); }
    bool requests_;
    uint8_t buffer_[256] = {};
    size_t length_ = 0;
    uint32_t last_ = 0;
    bool dirty_ = false;
};

class LifespanSerialParser {
public:
    typedef void (*ValueHandler)(void*, uint16_t);
    LifespanSerialParser(ValueHandler steps, ValueHandler speed, void* context)
        : steps_(steps), speed_(speed), context_(context) {}
    void reset() { requests_.reset(); responses_.reset(); count_ = 0; }
    void requestByte(uint8_t byte, uint32_t now) {
        now_ = now; requests_.feed(byte, now, onRequest, this);
    }
    void responseByte(uint8_t byte, uint32_t now) {
        now_ = now; responses_.feed(byte, now, onResponse, this);
    }
private:
    struct Pending { uint8_t frame[8]; uint32_t at; };
    static void onRequest(void* context, const uint8_t* p, size_t, bool dirty) {
        static_cast<LifespanSerialParser*>(context)->request(p, dirty);
    }
    static void onResponse(void* context, const uint8_t* p, size_t n, bool dirty) {
        static_cast<LifespanSerialParser*>(context)->response(p, n, dirty);
    }
    void expire() {
        while (count_ && uint32_t(now_ - pending_[0].at) > 500) pop();
    }
    void pop() {
        if (count_) { --count_; memmove(pending_, pending_ + 1, count_ * sizeof(Pending)); }
    }
    void request(const uint8_t* p, bool dirty) {
        if (dirty) count_ = 0;
        expire();
        // Keep writes in the queue too: their echoes are not step responses.
        if (count_ == 16) { count_ = 0; return; }
        memcpy(pending_[count_].frame, p, 8);
        pending_[count_++].at = now_;
        if (p[1] == 6 && word(p + 2) == 0x000a && speed_)
            speed_(context_, word(p + 4));
    }
    void response(const uint8_t* p, size_t n, bool dirty) {
        if (dirty) count_ = 0;
        expire();
        if (!count_) return;
        uint8_t request[8]; memcpy(request, pending_[0].frame, 8);
        pop();
        if (p[1] == (request[1] | 0x80)) return; // exception, not a value
        if (request[1] == 6) {
            if (n != 8 || memcmp(p, request, 8)) count_ = 0;
            return;
        }
        if (p[1] != 3 || n != size_t(word(request + 4)) * 2 + 5 || p[2] != word(request + 4) * 2) {
            count_ = 0; return;
        }
        if (word(request + 2) == 0x000f && word(request + 4) == 1 && steps_)
            steps_(context_, word(p + 3));
    }
    RtuDecoder requests_{true}, responses_{false};
    Pending pending_[16] = {};
    size_t count_ = 0;
    uint32_t now_ = 0;
    ValueHandler steps_, speed_;
    void* context_;
};
} // namespace treadspan
