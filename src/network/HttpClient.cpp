#include "network/HttpClient.h"

#include <stdexcept>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

namespace js5 {

namespace {

std::wstring widen(const std::string &s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

struct HInternetHandle
{
    HINTERNET h{nullptr};
    explicit HInternetHandle(HINTERNET handle) : h(handle) {}
    ~HInternetHandle() { if (h) WinHttpCloseHandle(h); }
    HInternetHandle(const HInternetHandle &) = delete;
    HInternetHandle &operator=(const HInternetHandle &) = delete;
};

}  // namespace

std::string httpGet(const std::string &host, const std::string &path, bool useTls)
{
    HInternetHandle session(WinHttpOpen(L"NXTCacheLibrary/0.1",
                                        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME,
                                        WINHTTP_NO_PROXY_BYPASS,
                                        0));
    if (!session.h) throw std::runtime_error("WinHttpOpen failed");

    INTERNET_PORT port = useTls ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HInternetHandle conn(WinHttpConnect(session.h, widen(host).c_str(), port, 0));
    if (!conn.h) throw std::runtime_error("WinHttpConnect failed");

    DWORD flags = useTls ? WINHTTP_FLAG_SECURE : 0;
    HInternetHandle req(WinHttpOpenRequest(conn.h, L"GET", widen(path).c_str(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!req.h) throw std::runtime_error("WinHttpOpenRequest failed");

    if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    {
        throw std::runtime_error("WinHttpSendRequest failed");
    }
    if (!WinHttpReceiveResponse(req.h, nullptr))
    {
        throw std::runtime_error("WinHttpReceiveResponse failed");
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                             WINHTTP_NO_HEADER_INDEX))
    {
        throw std::runtime_error("WinHttpQueryHeaders status failed");
    }
    if (statusCode < 200 || statusCode >= 300)
    {
        throw std::runtime_error("HTTP status " + std::to_string(statusCode));
    }

    std::string body;
    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(req.h, &available))
        {
            throw std::runtime_error("WinHttpQueryDataAvailable failed");
        }
        if (available == 0) break;

        std::vector<char> chunk(available);
        DWORD read = 0;
        if (!WinHttpReadData(req.h, chunk.data(), available, &read))
        {
            throw std::runtime_error("WinHttpReadData failed");
        }
        if (read == 0) break;
        body.append(chunk.data(), read);
    }
    return body;
}

}  // namespace js5
