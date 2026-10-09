// Tests for the caller-supplied JS5 server config entry points
// (nxt_fetch_master_crcs_with_config, nxt_cache_open_live_with_config,
// nxt_cache_enable_live_fallback_with_config). No network: every connection
// goes to a loopback fake JS5 server.
//
//   nxtcache-js5-config-test
//
// 1. Conversion: nxt::capi::toServerConfig applies the documented defaults and
//    leaves the output untouched on failure.
// 2. Validation: every invalid config is NXT_ERR_INVALID through all three
//    entry points, with outputs cleared, and nothing is contacted.
// 3. Rejected handshake: a valid config reaches the server with the caller's
//    key and build in handshake 1, the failure surfaces as NXT_ERR_IO / NULL,
//    and the key never appears in nxt_last_error().
// 4. Served: against a fake that accepts and serves a 4-entry master index,
//    all three succeed, the CRCs come back as served, and a second enable on
//    an enabled cache is a no-op (no new connection).
// 5. No jav_config: js5::httpGetCallCount() is unchanged across all of the
//    above; a control call proves the counter does count a real attempt.
//
// Exit codes: 0 all checks passed, 1 a check failed.

#include "c_api/Js5ServerConfig.h"
#include "c_api/nxtcache_c.h"
#include "network/HttpClient.h"
#include "network/Js5Config.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;
inline int closesocket(SOCKET s)
{
    return ::close(s);
}
#endif

namespace
{

#ifdef _WIN32
constexpr int kShutBoth = SD_BOTH;
#else
constexpr int kShutBoth = SHUT_RDWR;
#endif

constexpr const char *kKey = "0123456789abcdefGHIJKLMNOPQRSTUV";   // 32 chars
constexpr int32_t kBuildMajor = 951;
constexpr int32_t kBuildMinor = 1;
constexpr size_t kHandshakeSize = 44;

using Handshake = std::array<uint8_t, kHandshakeSize>;

int failures = 0;

void check(bool isOk, const std::string &what)
{
    std::printf("  %s  %s\n", isOk ? "ok  " : "FAIL", what.c_str());
    if (!isOk)
    {
        failures++;
    }
}

bool recvExactly(SOCKET s, uint8_t *dst, size_t n)
{
    size_t got = 0;
    while (got < n)
    {
        const int r = recv(s, reinterpret_cast<char *>(dst + got), static_cast<int>(n - got), 0);
        if (r <= 0)
        {
            return false;
        }
        got += static_cast<size_t>(r);
    }
    return true;
}

// The served master index: 4 entries, CRCs chosen to be distinguishable.
constexpr std::array<uint32_t, 4> kServedCrcs = {0x11223344u, 0xDEADBEEFu, 0x00000000u, 0xCAFEBABEu};
constexpr size_t kMasterEntrySize = 4 + 4 + 4 + 4 + 64;   // crc, version, files, size, whirlpool

void putU32BE(std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

// The whole JS5 response for (255,255): the 5-byte file id, then an
// uncompressed (type 0) container holding the NXT master-index format.
std::vector<uint8_t> masterIndexResponse()
{
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>(kServedCrcs.size()));
    for (size_t i = 0; i < kServedCrcs.size(); i++)
    {
        putU32BE(payload, kServedCrcs[i]);                 // crc
        putU32BE(payload, static_cast<uint32_t>(100 + i)); // version
        putU32BE(payload, 1);                              // files
        putU32BE(payload, 0);                              // size
        payload.insert(payload.end(), kMasterEntrySize - 16, uint8_t{0});   // whirlpool
    }
    std::vector<uint8_t> response = {255};
    putU32BE(response, 255);
    response.push_back(0);   // compression type: none
    putU32BE(response, static_cast<uint32_t>(payload.size()));
    response.insert(response.end(), payload.begin(), payload.end());
    return response;
}

enum class FakeMode
{
    Reject,   // answers handshake 1 with a non-zero code
    Serve,    // accepts handshake 1 and serves the master index for (255,255)
};

// Loopback fake JS5 server on an ephemeral port, one handler thread per
// connection. Records every handshake 1; every socket is closed in the
// destructor, which unblocks the handlers.
class FakeJs5Server
{
public:
    explicit FakeJs5Server(FakeMode mode) : mode(mode)
    {
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (listener == INVALID_SOCKET ||
            bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
            listen(listener, 8) != 0)
        {
            throw std::runtime_error("fake server: bind/listen failed");
        }
#ifdef _WIN32
        int len = sizeof(addr);
#else
        socklen_t len = sizeof(addr);
#endif
        getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &len);
        port = ntohs(addr.sin_port);
        acceptor = std::jthread([this]() { acceptLoop(); });
    }

    ~FakeJs5Server()
    {
        shutdown(listener, kShutBoth);
        closesocket(listener);   // unblocks accept()
        std::lock_guard lock(guard);
        for (SOCKET s : accepted)
        {
            shutdown(s, kShutBoth);
            closesocket(s);        // unblocks the handlers' recv()
        }
    }

    FakeJs5Server(const FakeJs5Server &) = delete;
    FakeJs5Server &operator=(const FakeJs5Server &) = delete;

    [[nodiscard]] uint16_t getPort() const { return port; }
    [[nodiscard]] int connectionCount() const { return connections.load(); }
    [[nodiscard]] int masterRequestCount() const { return masterRequests.load(); }

    [[nodiscard]] std::vector<Handshake> handshakes()
    {
        std::lock_guard lock(guard);
        return received;
    }

private:
    void acceptLoop()
    {
        while (true)
        {
            SOCKET s = accept(listener, nullptr, nullptr);
            if (s == INVALID_SOCKET)
            {
                return;
            }
            connections++;
            std::lock_guard lock(guard);
            accepted.push_back(s);
            handlers.emplace_back([this, s]() { handle(s); });
        }
    }

    void handle(SOCKET s)
    {
        Handshake hs{};
        if (!recvExactly(s, hs.data(), hs.size()))
        {
            return;
        }
        {
            std::lock_guard lock(guard);
            received.push_back(hs);
        }
        const char reply = mode == FakeMode::Serve ? 0 : 1;
        send(s, &reply, 1, 0);
        // Handshake 2 (two op 6 / op 3 messages) and file requests are all
        // 10 bytes. Answer (255,255) requests; ignore the rest.
        std::array<uint8_t, 10> msg{};
        while (mode == FakeMode::Serve && recvExactly(s, msg.data(), msg.size()))
        {
            const bool isRequest = msg[0] == 0x21 || msg[0] == 0x01;
            const uint32_t minor = (uint32_t(msg[2]) << 24) | (uint32_t(msg[3]) << 16) |
                                   (uint32_t(msg[4]) << 8) | uint32_t(msg[5]);
            if (isRequest && msg[1] == 255 && minor == 255)
            {
                masterRequests++;
                const std::vector<uint8_t> response = masterIndexResponse();
                send(s, reinterpret_cast<const char *>(response.data()), static_cast<int>(response.size()), 0);
            }
        }
    }

    FakeMode mode;
    SOCKET listener{INVALID_SOCKET};
    uint16_t port{0};
    std::atomic<int> connections{0};
    std::atomic<int> masterRequests{0};
    std::mutex guard;
    std::vector<SOCKET> accepted;
    std::vector<Handshake> received;
    std::vector<std::jthread> handlers;   // joined before `acceptor` is destroyed
    std::jthread acceptor;                // declared last: joined first
};

nxt_js5_server_config validConfig(uint16_t port)
{
    nxt_js5_server_config config{};
    config.struct_size = sizeof(nxt_js5_server_config);
    config.key = kKey;
    config.build_major = kBuildMajor;
    config.build_minor = kBuildMinor;
    config.host = "127.0.0.1";
    config.port = port;
    return config;
}

bool errorLeaksKey()
{
    return std::string(nxt_last_error()).find(kKey) != std::string::npos;
}

void testConversionDefaults()
{
    std::printf("Conversion applies the documented defaults\n");
    nxt_js5_server_config config = validConfig(0);
    config.host = nullptr;
    js5::ServerConfig out;
    std::string why;
    const bool isOk = nxt::capi::toServerConfig(&config, out, why);
    check(isOk, "minimal config converts (" + why + ")");
    check(out.key == kKey, "key copied");
    check(out.serverVersionMajor == kBuildMajor && out.serverVersionMinor == kBuildMinor,
          "build major/minor copied");
    check(out.endpoint == "content.runescape.com", "NULL host -> content.runescape.com");
    check(out.port == 43594, "port 0 -> 43594");
    check(out.connectTimeoutMs == js5::kDefaultConnectTimeoutMs && out.ioTimeoutMs == js5::kDefaultIoTimeoutMs,
          "timeouts stay at the ServerConfig defaults");

    config.host = "";
    js5::ServerConfig emptyHost;
    check(nxt::capi::toServerConfig(&config, emptyHost, why) && emptyHost.endpoint == "content.runescape.com",
          "\"\" host -> content.runescape.com");

    config.host = "content.beta.runescape.com";
    config.port = 443;
    config.build_minor = 0;
    js5::ServerConfig explicitHost;
    check(nxt::capi::toServerConfig(&config, explicitHost, why) &&
              explicitHost.endpoint == "content.beta.runescape.com" && explicitHost.port == 443 &&
              explicitHost.serverVersionMinor == 0,
          "explicit host / port / build_minor 0 honoured");
}

// One invalid variant of a valid config, with a description.
struct BadConfig
{
    std::string what;
    nxt_js5_server_config config;
};

std::vector<BadConfig> badConfigs(uint16_t port)
{
    static const std::string shortKey(31, 'k');
    static const std::string longKey(33, 'k');
    std::vector<BadConfig> out;
    auto add = [&](const std::string &what, auto mutate)
    {
        nxt_js5_server_config c = validConfig(port);
        mutate(c);
        out.push_back({what, c});
    };
    add("struct_size 0", [](nxt_js5_server_config &c) { c.struct_size = 0; });
    add("struct_size - 1", [](nxt_js5_server_config &c) { c.struct_size -= 1; });
    add("struct_size + 8", [](nxt_js5_server_config &c) { c.struct_size += 8; });
    add("null key", [](nxt_js5_server_config &c) { c.key = nullptr; });
    add("empty key", [](nxt_js5_server_config &c) { c.key = ""; });
    add("31-char key", [](nxt_js5_server_config &c) { c.key = shortKey.c_str(); });
    add("33-char key", [](nxt_js5_server_config &c) { c.key = longKey.c_str(); });
    add("build_major 0", [](nxt_js5_server_config &c) { c.build_major = 0; });
    add("build_major -5", [](nxt_js5_server_config &c) { c.build_major = -5; });
    add("build_minor -1", [](nxt_js5_server_config &c) { c.build_minor = -1; });
    return out;
}

void testConversionRejects(uint16_t port)
{
    std::printf("Conversion rejects invalid configs and leaves the output untouched\n");
    std::string why;
    js5::ServerConfig out;
    out.endpoint = "sentinel";
    check(!nxt::capi::toServerConfig(nullptr, out, why) && out.endpoint == "sentinel", "null config");
    for (const BadConfig &bad : badConfigs(port))
    {
        why.clear();
        const bool isOk = nxt::capi::toServerConfig(&bad.config, out, why);
        check(!isOk && out.endpoint == "sentinel" && !why.empty() && why.find(kKey) == std::string::npos,
              bad.what + " -> rejected (" + why + ")");
    }
}

void expectInvalidEverywhere(const std::string &what, const nxt_js5_server_config *config, nxt_cache *local)
{
    auto *crcs = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(0x1));   // poisoned
    size_t count = 12345;
    const nxt_result crcRc = nxt_fetch_master_crcs_with_config(config, &crcs, &count);
    check(crcRc == NXT_ERR_INVALID && crcs == nullptr && count == 0 && !errorLeaksKey(),
          "fetch_master_crcs_with_config: " + what + " -> NXT_ERR_INVALID, outs cleared");

    nxt_cache *live = nxt_cache_open_live_with_config(config);
    check(live == nullptr && !errorLeaksKey(), "open_live_with_config: " + what + " -> NULL");
    nxt_cache_close(live);

    check(nxt_cache_enable_live_fallback_with_config(local, config) == NXT_ERR_INVALID && !errorLeaksKey(),
          "enable_live_fallback_with_config: " + what + " -> NXT_ERR_INVALID");
}

void testValidation(FakeJs5Server &server, nxt_cache *local)
{
    std::printf("C API rejects invalid arguments without contacting the server\n");
    expectInvalidEverywhere("null config", nullptr, local);
    for (const BadConfig &bad : badConfigs(server.getPort()))
    {
        expectInvalidEverywhere(bad.what, &bad.config, local);
    }

    const nxt_js5_server_config good = validConfig(server.getPort());
    uint32_t *crcs = nullptr;
    size_t count = 0;
    check(nxt_fetch_master_crcs_with_config(&good, nullptr, &count) == NXT_ERR_INVALID,
          "fetch_master_crcs_with_config: null out_crcs -> NXT_ERR_INVALID");
    check(nxt_fetch_master_crcs_with_config(&good, &crcs, nullptr) == NXT_ERR_INVALID,
          "fetch_master_crcs_with_config: null out_count -> NXT_ERR_INVALID");
    check(nxt_cache_enable_live_fallback_with_config(nullptr, &good) == NXT_ERR_INVALID,
          "enable_live_fallback_with_config: null handle -> NXT_ERR_INVALID");
    check(server.connectionCount() == 0,
          "no connection was made (connections=" + std::to_string(server.connectionCount()) + ")");
}

uint32_t readU32BE(const uint8_t *p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

void expectCallerHandshake(const Handshake &hs, const std::string &what)
{
    const bool isFramed = hs[0] == 15 && hs[1] == 42 && hs[42] == 0 && hs[43] == 0;
    const bool isBuild = readU32BE(hs.data() + 2) == uint32_t(kBuildMajor) &&
                         readU32BE(hs.data() + 6) == uint32_t(kBuildMinor);
    const bool isKey = std::memcmp(hs.data() + 10, kKey, 32) == 0;
    check(isFramed && isBuild && isKey, what + ": handshake 1 carries the caller's key and build");
}

void testHandshake(FakeJs5Server &server, nxt_cache *local)
{
    std::printf("A valid config goes straight to the JS5 handshake with the caller's values\n");
    const nxt_js5_server_config good = validConfig(server.getPort());

    uint32_t *crcs = nullptr;
    size_t count = 0;
    const nxt_result crcRc = nxt_fetch_master_crcs_with_config(&good, &crcs, &count);
    check(crcRc == NXT_ERR_IO && crcs == nullptr && count == 0, "fetch_master_crcs_with_config: rejected handshake -> NXT_ERR_IO");
    check(!errorLeaksKey(), "fetch_master_crcs_with_config: key absent from nxt_last_error");

    nxt_cache *live = nxt_cache_open_live_with_config(&good);
    check(live == nullptr && !errorLeaksKey(), "open_live_with_config: rejected handshake -> NULL, key absent");
    nxt_cache_close(live);

    check(nxt_cache_enable_live_fallback_with_config(local, &good) == NXT_ERR_IO && !errorLeaksKey(),
          "enable_live_fallback_with_config: rejected handshake -> NXT_ERR_IO, key absent");
    // A failed enable leaves the cache unchanged, so a retry connects again.
    check(nxt_cache_enable_live_fallback_with_config(local, &good) == NXT_ERR_IO,
          "enable_live_fallback_with_config: retry after failure tries again");

    const std::vector<Handshake> seen = server.handshakes();
    check(seen.size() == 4, "server saw 4 handshakes (saw " + std::to_string(seen.size()) + ")");
    const char *names[] = {"fetch_master_crcs", "open_live", "enable_live_fallback", "enable_live_fallback retry"};
    for (size_t i = 0; i < seen.size() && i < 4; i++)
    {
        expectCallerHandshake(seen[i], names[i]);
    }
}

void testServed(nxt_cache *local)
{
    std::printf("A server that accepts: all three succeed against the caller's config\n");
    FakeJs5Server server(FakeMode::Serve);
    const nxt_js5_server_config good = validConfig(server.getPort());

    uint32_t *crcs = nullptr;
    size_t count = 0;
    const nxt_result crcRc = nxt_fetch_master_crcs_with_config(&good, &crcs, &count);
    check(crcRc == NXT_OK, "fetch_master_crcs_with_config -> NXT_OK");
    const bool isServed = crcs != nullptr && count == kServedCrcs.size() &&
                          std::equal(kServedCrcs.begin(), kServedCrcs.end(), crcs);
    check(isServed, "fetch_master_crcs_with_config returns the 4 served CRCs in order (count=" +
                        std::to_string(count) + ")");
    nxt_free(crcs);
    check(server.masterRequestCount() == 1, "exactly one (255,255) request was made");

    nxt_cache *live = nxt_cache_open_live_with_config(&good);
    check(live != nullptr, "open_live_with_config -> handle");
    nxt_cache_close(live);

    const int beforeEnable = server.connectionCount();
    check(nxt_cache_enable_live_fallback_with_config(local, &good) == NXT_OK,
          "enable_live_fallback_with_config -> NXT_OK");
    check(server.connectionCount() == beforeEnable + 1, "enable connected once");
    check(nxt_cache_enable_live_fallback_with_config(local, &good) == NXT_OK,
          "enable_live_fallback_with_config again -> NXT_OK");
    check(server.connectionCount() == beforeEnable + 1, "second enable is a no-op: no new connection");
    nxt_js5_server_config bad = good;
    bad.struct_size = 0;
    check(nxt_cache_enable_live_fallback_with_config(local, &bad) == NXT_ERR_INVALID,
          "an invalid config is still rejected on an enabled cache");

    const std::vector<Handshake> seen = server.handshakes();
    check(seen.size() == 3, "server saw 3 handshakes (saw " + std::to_string(seen.size()) + ")");
    const char *names[] = {"fetch_master_crcs (served)", "open_live (served)", "enable_live_fallback (served)"};
    for (size_t i = 0; i < seen.size() && i < 3; i++)
    {
        expectCallerHandshake(seen[i], names[i]);
    }
}

// A loopback port with nothing listening: connections are refused at once.
uint16_t closedLoopbackPort()
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    getsockname(s, reinterpret_cast<sockaddr *>(&addr), &len);
    closesocket(s);
    return ntohs(addr.sin_port);
}

// The counter must see a request, or "zero requests" would prove nothing.
void testHttpCounterCounts()
{
    std::printf("HTTP counter control: a real httpGet attempt is counted\n");
    const int before = js5::httpGetCallCount();
    bool hasThrown = false;
    try
    {
        js5::httpGet("127.0.0.1:" + std::to_string(closedLoopbackPort()), "/jav_config.ws");
    }
    catch (const std::exception &)
    {
        hasThrown = true;
    }
    check(hasThrown, "httpGet to a closed loopback port fails");
    check(js5::httpGetCallCount() == before + 1, "httpGetCallCount went up by 1");
}

}  // namespace

int main()
{
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
    const int httpBefore = js5::httpGetCallCount();
    {
        FakeJs5Server server(FakeMode::Reject);
        // RSCache opens no files until a read, so any path makes a valid local handle.
        nxt_cache *local = nxt_cache_open_local("nxtcache-js5-config-test-no-such-dir");
        check(local != nullptr, "local handle opened");

        testConversionDefaults();
        testConversionRejects(server.getPort());
        if (local != nullptr)
        {
            testValidation(server, local);
            testHandshake(server, local);
            testServed(local);
        }
        nxt_cache_close(local);
    }
    std::printf("No jav_config request on any *_with_config path\n");
    check(js5::httpGetCallCount() == httpBefore,
          "httpGet was never called (calls=" + std::to_string(js5::httpGetCallCount() - httpBefore) + ")");
    testHttpCounterCounts();

    std::printf("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
