#include "network/Js5Socket.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#ifndef _WIN32
// Map the handful of Winsock spellings used below onto their POSIX equivalents
// so the socket code compiles unchanged. The handle type widens to int, the
// invalid sentinel becomes -1, and error reporting reads errno.
using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;

inline int closesocket(SOCKET s)
{
    return ::close(s);
}

inline int WSAGetLastError()
{
    return errno;
}

#define SD_BOTH SHUT_RDWR
#endif

namespace js5 {

namespace {

constexpr size_t kMaxBlockSize = 102400;

// On Linux a send() to a peer that has closed raises SIGPIPE (default action:
// terminate). MSG_NOSIGNAL turns that into an EPIPE return instead. Windows has
// no such signal, so the flag is zero there.
#ifdef _WIN32
constexpr int kSendFlags = 0;
#else
constexpr int kSendFlags = MSG_NOSIGNAL;
#endif

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
#ifdef _WIN32
    static std::once_flag once;
    std::call_once(once, []()
    {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            throw std::runtime_error("WSAStartup failed");
        }
    });
#endif
}

#ifdef _WIN32
using SockLen = int;
#else
using SockLen = socklen_t;
#endif

bool isTimeoutError(int error)
{
#ifdef _WIN32
    return error == WSAETIMEDOUT;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

void setBlocking(SOCKET s, bool isBlocking)
{
#ifdef _WIN32
    u_long mode = isBlocking ? 0 : 1;
    const bool isOk = ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(s, F_GETFL, 0);
    const bool isOk = flags >= 0 &&
                      fcntl(s, F_SETFL, isBlocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK)) == 0;
#endif
    if (!isOk)
    {
        throw std::runtime_error("JS5 socket mode change failed: WSA " + std::to_string(WSAGetLastError()));
    }
}

enum class ConnectResult
{
    Connected,
    Failed,
    TimedOut,
};

// Wait until a non-blocking connect completes, fails, or timeoutMs elapses.
// Windows reports a failed connect in the except set; POSIX via POLLOUT plus
// SO_ERROR. poll() on POSIX avoids select()'s FD_SETSIZE limit in fd-heavy hosts
// (a JVM); select() on Windows because WSAPoll misreports failed connects on
// older builds.
ConnectResult waitForConnect(SOCKET s, int timeoutMs)
{
#ifdef _WIN32
    fd_set writable;
    fd_set failed;
    FD_ZERO(&writable);
    FD_ZERO(&failed);
    FD_SET(s, &writable);
    FD_SET(s, &failed);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    const int ready = select(0, nullptr, &writable, &failed, &tv);
#else
    pollfd pfd{s, POLLOUT, 0};
    const int ready = poll(&pfd, 1, timeoutMs);
#endif
    if (ready == 0)
    {
        return ConnectResult::TimedOut;
    }
    if (ready < 0)
    {
        return ConnectResult::Failed;
    }
    int soError = 0;
    SockLen len = sizeof(soError);
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&soError), &len) != 0 || soError != 0)
    {
        return ConnectResult::Failed;
    }
    return ConnectResult::Connected;
}

// connect() bounded by timeoutMs (<= 0 means the OS default, unbounded here).
// Leaves the socket in blocking mode on success.
ConnectResult connectWithTimeout(SOCKET s, const sockaddr *addr, int addrLen, int timeoutMs)
{
    if (timeoutMs <= 0)
    {
        return connect(s, addr, addrLen) == 0 ? ConnectResult::Connected : ConnectResult::Failed;
    }
    setBlocking(s, false);
    if (connect(s, addr, addrLen) != 0)
    {
        const int error = WSAGetLastError();
#ifdef _WIN32
        const bool isPending = error == WSAEWOULDBLOCK;
#else
        const bool isPending = error == EINPROGRESS;
#endif
        if (!isPending)
        {
            return ConnectResult::Failed;
        }
        const ConnectResult waited = waitForConnect(s, timeoutMs);
        if (waited != ConnectResult::Connected)
        {
            return waited;
        }
    }
    setBlocking(s, true);
    return ConnectResult::Connected;
}

// Per-call recv/send timeout (<= 0 leaves the OS default: block forever).
void setIoTimeout(SOCKET s, int timeoutMs)
{
    if (timeoutMs <= 0)
    {
        return;
    }
#ifdef _WIN32
    const DWORD value = static_cast<DWORD>(timeoutMs);
#else
    const timeval value{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
#endif
    const auto *raw = reinterpret_cast<const char *>(&value);
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, raw, sizeof(value)) != 0 ||
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, raw, sizeof(value)) != 0)
    {
        throw std::runtime_error("JS5 setsockopt timeout failed: WSA " + std::to_string(WSAGetLastError()));
    }
}

// Resolve and connect to the configured endpoint, trying each address. Returns
// a connected, blocking socket with I/O timeouts set, or throws. Never leaks a
// socket on the failure paths.
SOCKET openConnection(const ServerConfig &config)
{
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo *result = nullptr;
    const std::string portStr = std::to_string(config.port);
    if (getaddrinfo(config.endpoint.c_str(), portStr.c_str(), &hints, &result) != 0 || !result)
    {
        throw std::runtime_error("getaddrinfo failed for " + config.endpoint);
    }

    SOCKET s = INVALID_SOCKET;
    bool hasTimedOut = false;
    for (addrinfo *p = result; p; p = p->ai_next)
    {
        s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET)
        {
            continue;
        }
        ConnectResult outcome = ConnectResult::Failed;
        try
        {
            outcome = connectWithTimeout(s, p->ai_addr, static_cast<int>(p->ai_addrlen),
                                         config.connectTimeoutMs);
        }
        catch (...)
        {
            closesocket(s);
            freeaddrinfo(result);
            throw;
        }
        if (outcome == ConnectResult::Connected)
        {
            break;
        }
        hasTimedOut = hasTimedOut || outcome == ConnectResult::TimedOut;
        closesocket(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(result);
    if (s == INVALID_SOCKET)
    {
        const std::string target = config.endpoint + ":" + portStr;
        if (hasTimedOut)
        {
            throw std::runtime_error("JS5 connect to " + target + " timed out after " +
                                     std::to_string(config.connectTimeoutMs) + " ms");
        }
        throw std::runtime_error("connect failed to " + target);
    }
    try
    {
        setIoTimeout(s, config.ioTimeoutMs);
    }
    catch (...)
    {
        closesocket(s);
        throw;
    }
    return s;
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
    disconnect();
}

void Js5Socket::disconnect()
{
    SOCKET s = static_cast<SOCKET>(socket_);
    if (s != INVALID_SOCKET)
    {
        shutdown(s, SD_BOTH);
        closesocket(s);
    }
    socket_ = static_cast<uintptr_t>(INVALID_SOCKET);
    connected_ = false;
}

void Js5Socket::connectAndHandshake()
{
    if (config_.key.size() != 32)
    {
        throw std::runtime_error("server key must be exactly 32 chars, got " +
                                 std::to_string(config_.key.size()));
    }
    disconnect();
    socket_ = static_cast<uintptr_t>(openConnection(config_));
    try
    {
        handshake();
    }
    catch (...)
    {
        disconnect();
        throw;
    }
    connected_ = true;
}

void Js5Socket::handshake()
{
    // Handshake 1: type=15, length=42, version1, version2, key (32 bytes + null), lang=0
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
        if (r < 0)
        {
            const int error = WSAGetLastError();
            if (isTimeoutError(error))
            {
                throw std::runtime_error("JS5 recv timed out after " +
                                         std::to_string(config_.ioTimeoutMs) + " ms with no data");
            }
            throw std::runtime_error("JS5 recv failed: WSA " + std::to_string(error));
        }
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
                     static_cast<int>(n - sent), kSendFlags);
        if (r <= 0)
        {
            const int error = WSAGetLastError();
            if (r < 0 && isTimeoutError(error))
            {
                throw std::runtime_error("JS5 send timed out after " +
                                         std::to_string(config_.ioTimeoutMs) + " ms");
            }
            throw std::runtime_error("JS5 send failed: WSA " + std::to_string(error));
        }
        sent += static_cast<size_t>(r);
    }
}

std::vector<uint8_t> Js5Socket::getFile(int major, int minor)
{
    if (!connected_)
    {
        // An earlier failure closed the connection; start a fresh one.
        connectAndHandshake();
    }
    try
    {
        return fetchFile(major, minor);
    }
    catch (...)
    {
        // The stream position is unknown after any failure mid-request, so the
        // connection cannot be reused. The next call reconnects.
        disconnect();
        throw;
    }
}

std::vector<uint8_t> Js5Socket::fetchFile(int major, int minor)
{

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
