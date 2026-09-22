// JS5 socket timeout tests against local fake servers (no network, no cache).
//
//   nxtcache-js5-timeout-test
//
// 1. A server that accepts and never answers the handshake: the Js5Socket
//    constructor must throw "timed out" within the io timeout, not hang.
// 2. A server that completes the handshake, then never answers a file request:
//    getFile must throw "timed out"; the next getFile must reconnect (a second
//    TCP connection reaches the server) and time out again, never reuse the
//    desynchronised stream.
// 3. Connect timeout against a non-routable address. Whether the SYN is
//    silently dropped (the timeout path) or rejected at once depends on the
//    local network, so a fast rejection is reported as SKIPPED CHECK, not a pass.
//
// Exit codes: 0 all checks passed, 1 a check failed.

#include "network/Js5Config.h"
#include "network/Js5Socket.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
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

using Clock = std::chrono::steady_clock;

#ifdef _WIN32
constexpr int kShutBoth = SD_BOTH;
#else
constexpr int kShutBoth = SHUT_RDWR;
#endif

constexpr int kIoTimeoutMs = 500;
constexpr int kConnectTimeoutMs = 1000;
constexpr auto kHangBound = std::chrono::seconds(5);   // "did not hang"

int failures = 0;
int skippedChecks = 0;

void check(bool isOk, const std::string &what)
{
    std::printf("  %s  %s\n", isOk ? "ok  " : "FAIL", what.c_str());
    if (!isOk)
    {
        failures++;
    }
}

void skipCheck(const std::string &what)
{
    std::printf("  SKIPPED CHECK  %s\n", what.c_str());
    skippedChecks++;
}

bool recvExactly(SOCKET s, size_t n)
{
    std::vector<char> buffer(n);
    size_t got = 0;
    while (got < n)
    {
        const int r = recv(s, buffer.data() + got, static_cast<int>(n - got), 0);
        if (r <= 0)
        {
            return false;
        }
        got += static_cast<size_t>(r);
    }
    return true;
}

// Reads until the peer closes or errors: the "never respond" part.
void drainUntilClosed(SOCKET s)
{
    char sink[256];
    while (recv(s, sink, sizeof(sink), 0) > 0)
    {
    }
}

// Behaviour of the fake server per accepted connection.
enum class FakeMode
{
    SilentAfterAccept,       // never answers the handshake
    SilentAfterHandshake,    // answers handshake 1 with 0, then never answers requests
};

// A loopback listener on an ephemeral port. One handler thread per connection;
// every socket is closed in the destructor, which unblocks the handlers.
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
        // shutdown() before close: on Linux, close() alone does not wake a thread
        // blocked in accept()/recv() on that socket.
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

    [[nodiscard]] int getPort() const { return port; }
    [[nodiscard]] int connectionCount() const { return connections.load(); }

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

    void handle(SOCKET s) const
    {
        if (mode == FakeMode::SilentAfterHandshake && recvExactly(s, 44))
        {
            const char ok = 0;
            send(s, &ok, 1, 0);
        }
        drainUntilClosed(s);
    }

    FakeMode mode;
    SOCKET listener{INVALID_SOCKET};
    int port{0};
    std::atomic<int> connections{0};
    std::mutex guard;
    std::vector<SOCKET> accepted;
    std::vector<std::jthread> handlers;   // joined before `acceptor` is destroyed
    std::jthread acceptor;                // declared last: joined first
};

js5::ServerConfig localConfig(int port)
{
    js5::ServerConfig config;
    config.key = std::string(32, 'k');
    config.serverVersionMajor = 947;
    config.endpoint = "127.0.0.1";
    config.port = port;
    config.connectTimeoutMs = kConnectTimeoutMs;
    config.ioTimeoutMs = kIoTimeoutMs;
    return config;
}

struct Outcome
{
    bool hasThrown{false};
    std::string message;
    Clock::duration elapsed{};
};

template <typename Fn>
Outcome timed(Fn &&fn)
{
    Outcome outcome;
    const auto start = Clock::now();
    try
    {
        fn();
    }
    catch (const std::exception &e)
    {
        outcome.hasThrown = true;
        outcome.message = e.what();
    }
    outcome.elapsed = Clock::now() - start;
    return outcome;
}

long long millis(Clock::duration d)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

void expectTimedOut(const Outcome &o, const std::string &what)
{
    std::printf("    -> %s after %lld ms\n", o.hasThrown ? o.message.c_str() : "(no exception)",
                millis(o.elapsed));
    check(o.hasThrown && o.message.find("timed out") != std::string::npos, what + " throws \"timed out\"");
    check(o.elapsed < kHangBound, what + " returns within 5 s (no hang)");
    check(o.elapsed >= std::chrono::milliseconds(kIoTimeoutMs - 100),
          what + " waited for the io timeout (not an unrelated early failure)");
}

void testStalledHandshake()
{
    std::printf("Server accepts, never answers the handshake\n");
    FakeJs5Server server(FakeMode::SilentAfterAccept);
    const Outcome o = timed([&]() { js5::Js5Socket socket(localConfig(server.getPort())); });
    expectTimedOut(o, "constructor");
}

void testStalledRequestReconnects()
{
    std::printf("Server completes the handshake, never answers a request\n");
    FakeJs5Server server(FakeMode::SilentAfterHandshake);
    std::unique_ptr<js5::Js5Socket> socket;
    const Outcome made = timed([&]() { socket = std::make_unique<js5::Js5Socket>(localConfig(server.getPort())); });
    check(!made.hasThrown && socket != nullptr, "handshake succeeds (" + made.message + ")");
    if (socket == nullptr)
    {
        return;
    }
    expectTimedOut(timed([&]() { socket->getFile(2, 60); }), "first getFile");
    expectTimedOut(timed([&]() { socket->getFile(2, 60); }), "second getFile");
    check(server.connectionCount() == 2,
          "second getFile reconnected (connections=" + std::to_string(server.connectionCount()) + ")");
}

void testConnectTimeout()
{
    std::printf("Connect to a non-routable address (10.255.255.1)\n");
    js5::ServerConfig config = localConfig(43594);
    config.endpoint = "10.255.255.1";
    const Outcome o = timed([&]() { js5::Js5Socket socket(config); });
    std::printf("    -> %s after %lld ms\n", o.hasThrown ? o.message.c_str() : "(no exception)",
                millis(o.elapsed));
    check(o.hasThrown, "connect to a non-routable address fails");
    check(o.elapsed < kHangBound, "connect returns within 5 s (no hang)");
    if (o.message.find("timed out") != std::string::npos)
    {
        check(o.elapsed >= std::chrono::milliseconds(kConnectTimeoutMs - 100),
              "connect waited for the connect timeout");
    }
    else
    {
        skipCheck("connect-timeout path not exercised: this network rejected the SYN at once");
    }
}

}  // namespace

int main()
{
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
    testStalledHandshake();
    testStalledRequestReconnects();
    testConnectTimeout();
    std::printf("\n%s: %d failure(s), %d skipped check(s)\n",
                failures == 0 ? "PASS" : "FAIL", failures, skippedChecks);
    return failures == 0 ? 0 : 1;
}
