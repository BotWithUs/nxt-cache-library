#pragma once

#include "network/Js5Config.h"

#include <cstdint>
#include <vector>

namespace js5 {

// Live JS5 binary protocol client. Connects to content.runescape.com:43594, runs the
// (op 15) + (op 6) + (op 3) handshake, then exposes synchronous getFile(major, minor)
// requests. Single-flight: each call writes a request, reads framed responses across
// 102400-byte block boundaries until the file is complete, returns the raw bytes
// (1-byte compression type + 4-byte size + optional 4-byte uncompressed size + payload).
//
// Thread-unsafe. Construct, use, destroy on one thread.
class Js5Socket
{
public:
    explicit Js5Socket(const ServerConfig &config);
    ~Js5Socket();

    Js5Socket(const Js5Socket &) = delete;
    Js5Socket &operator=(const Js5Socket &) = delete;

    // Returns the raw response payload (still in JS5 compression-format wrapper).
    // Throws std::runtime_error on protocol errors.
    std::vector<uint8_t> getFile(int major, int minor);

private:
    void connectAndHandshake();
    void readBytes(uint8_t *dst, size_t n);
    void writeBytes(const uint8_t *src, size_t n);

    ServerConfig config_;
    uintptr_t socket_;  // SOCKET handle stored as uintptr_t to avoid leaking winsock includes
    bool connected_;
};

}  // namespace js5
