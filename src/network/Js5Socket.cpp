#include "network/Js5Socket.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace js5 {

namespace {

constexpr size_t kMaxBlockSize = 102400;

void writeU16BE(uint8_t *p, uint16_t v) { p[0] = static_cast<uint8_t>(v >> 8); p[1] = static_cast<uint8_t>(v); }
void writeU32BE(uint8_t *p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

uint32_t readU32BE(const uint8_t *p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
            static_cast<uint32_t>(p[3]);
}

void ensureWsaStartup()
{
    static std::once_flag once;
    std::call_once(once, []() {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            throw std::runtime_error("WSAStartup failed");
        }
    });
}

}  // namespace

Js5Socket::Js5Socket(const ServerConfig &config)
    : config_(config), socket_(static_cast<uintptr_t>(INVALID_SOCKET)), connected_(false)
{
    ensureWsaStartup();
    connectAndHandshake();
}

Js5Socket::~Js5Socket()
{
    SOCKET s = static_cast<SOCKET>(socket_);
    if (s != INVALID_SOCKET)
    {
        shutdown(s, SD_BOTH);
        closesocket(s);
    }
}

void Js5Socket::connectAndHandshake()
{
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo *result = nullptr;
    std::string portStr = std::to_string(config_.port);
    if (getaddrinfo(config_.endpoint.c_str(), portStr.c_str(), &hints, &result) != 0 || !result)
    {
        throw std::runtime_error("getaddrinfo failed for " + config_.endpoint);
    }

    SOCKET s = INVALID_SOCKET;
    for (addrinfo *p = result; p; p = p->ai_next)
    {
        s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (connect(s, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) break;
        closesocket(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(result);
    if (s == INVALID_SOCKET)
    {
        throw std::runtime_error("connect failed to " + config_.endpoint + ":" + portStr);
    }
    socket_ = static_cast<uintptr_t>(s);

    // Handshake 1: type=15, length=42, version1, version2, key (32 bytes + null), lang=0
    if (config_.key.size() != 32)
    {
        throw std::runtime_error("server key must be exactly 32 chars, got " +
                                 std::to_string(config_.key.size()));
    }
    uint8_t hs1[44];
    hs1[0] = 15;
    hs1[1] = 42;
    writeU32BE(hs1 + 2, static_cast<uint32_t>(config_.serverVersionMajor));
    writeU32BE(hs1 + 6, static_cast<uint32_t>(config_.serverVersionMinor));
    std::memcpy(hs1 + 10, config_.key.data(), 32);
    hs1[42] = 0;   // null terminator for key
    hs1[43] = 0;   // lang
    writeBytes(hs1, sizeof(hs1));

    uint8_t resp = 0;
    readBytes(&resp, 1);
    if (resp != 0)
    {
        throw std::runtime_error("JS5 handshake1 rejected with code " + std::to_string(resp));
    }

    // Handshake 2: write op=6 then op=3 (no response expected)
    auto writeHs2 = [&](uint8_t op) {
        uint8_t buf[10];
        buf[0] = op;
        // tribyte = 5
        buf[1] = 0;
        buf[2] = 0;
        buf[3] = 5;
        writeU16BE(buf + 4, 0);  // short1
        writeU16BE(buf + 6, static_cast<uint16_t>(config_.serverVersionMajor & 0xFFFF));
        writeU16BE(buf + 8, 0);  // short2
        writeBytes(buf, sizeof(buf));
    };
    writeHs2(6);
    writeHs2(3);

    connected_ = true;
}

void Js5Socket::readBytes(uint8_t *dst, size_t n)
{
    SOCKET s = static_cast<SOCKET>(socket_);
    size_t got = 0;
    while (got < n)
    {
        int r = recv(s, reinterpret_cast<char *>(dst + got),
                     static_cast<int>(n - got), 0);
        if (r == 0) throw std::runtime_error("JS5 socket closed by peer");
        if (r < 0)  throw std::runtime_error("JS5 recv failed: WSA " + std::to_string(WSAGetLastError()));
        got += static_cast<size_t>(r);
    }
}

void Js5Socket::writeBytes(const uint8_t *src, size_t n)
{
    SOCKET s = static_cast<SOCKET>(socket_);
    size_t sent = 0;
    while (sent < n)
    {
        int r = send(s, reinterpret_cast<const char *>(src + sent),
                     static_cast<int>(n - sent), 0);
        if (r <= 0) throw std::runtime_error("JS5 send failed: WSA " + std::to_string(WSAGetLastError()));
        sent += static_cast<size_t>(r);
    }
}

std::vector<uint8_t> Js5Socket::getFile(int major, int minor)
{
    if (!connected_) throw std::runtime_error("JS5 socket not connected");

    // File request: mode, major, minor (u32be), version (u16be), short2 (u16be)
    uint8_t req[10];
    req[0] = (major == 255 && minor == 255) ? 0x21 : 0x01;
    req[1] = static_cast<uint8_t>(major);
    writeU32BE(req + 2, static_cast<uint32_t>(minor));
    writeU16BE(req + 6, static_cast<uint16_t>(config_.serverVersionMajor & 0xFFFF));
    writeU16BE(req + 8, 0);
    writeBytes(req, sizeof(req));

    // Read framed response. Server delivers up to maxBlockSize bytes per block,
    // each block prefixed with a 5-byte (major, minor & 0x7fffffff) re-identifier.
    std::vector<uint8_t> out;
    out.reserve(64 * 1024);
    int totalBytes = -1;
    int currentBytes = 0;

    while (totalBytes < 0 || currentBytes < totalBytes)
    {
        size_t bytesread = 0;
        uint8_t fileId[5];
        readBytes(fileId, 5);
        bytesread += 5;

        uint8_t respMajor = fileId[0];
        uint32_t respMinor = readU32BE(fileId + 1) & 0x7FFFFFFFu;
        if (respMajor != static_cast<uint8_t>(major) ||
            respMinor != static_cast<uint32_t>(minor))
        {
            throw std::runtime_error("JS5 response file-id mismatch");
        }

        if (totalBytes < 0)
        {
            uint8_t comp[5];
            readBytes(comp, 5);
            bytesread += 5;
            uint8_t compType = comp[0];
            uint32_t compressedSize = readU32BE(comp + 1);
            totalBytes = 5 + (compType == 0 ? 0 : 4) + static_cast<int>(compressedSize);
            out.insert(out.end(), comp, comp + 5);
            currentBytes += 5;
        }

        size_t bytesleft = static_cast<size_t>(totalBytes - currentBytes);
        size_t payloadsize = std::min(kMaxBlockSize - bytesread, bytesleft);
        if (payloadsize > 0)
        {
            size_t before = out.size();
            out.resize(before + payloadsize);
            readBytes(out.data() + before, payloadsize);
            currentBytes += static_cast<int>(payloadsize);
        }
    }
    return out;
}

}  // namespace js5
