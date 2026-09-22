#include "network/HttpClient.h"
#include "network/Js5Config.h"

#include <curl/curl.h>

#include <mutex>
#include <stdexcept>
#include <string>

namespace js5 {

namespace {

// libcurl's global init is not thread-safe; run it exactly once. The rest of
// the easy interface is safe to use per-handle on a single thread.
void ensureCurlGlobalInit()
{
    static std::once_flag once;
    std::call_once(once, []()
    {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        {
            throw std::runtime_error("curl_global_init failed");
        }
    });
}

size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t total = size * nmemb;
    auto *body = static_cast<std::string *>(userdata);
    body->append(ptr, total);
    return total;
}

struct CurlHandle
{
    CURL *handle{nullptr};

    CurlHandle()
        : handle(curl_easy_init())
    {
    }

    ~CurlHandle()
    {
        if (handle)
        {
            curl_easy_cleanup(handle);
        }
    }

    CurlHandle(const CurlHandle &) = delete;
    CurlHandle &operator=(const CurlHandle &) = delete;
};

}  // namespace

std::string httpGet(const std::string &host, const std::string &path, bool useTls)
{
    ensureCurlGlobalInit();

    CurlHandle curl;
    if (!curl.handle)
    {
        throw std::runtime_error("curl_easy_init failed");
    }

    std::string url = (useTls ? "https://" : "http://") + host + path;
    std::string body;

    curl_easy_setopt(curl.handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl.handle, CURLOPT_USERAGENT, "NXTCacheLibrary/0.1");
    curl_easy_setopt(curl.handle, CURLOPT_FOLLOWLOCATION, 1L);
    // Bound the jav_config fetch like the JS5 socket: connect, and a stall
    // (below 1 byte/s for the io window). NOSIGNAL keeps curl's timeout
    // handling off SIGALRM, which is unsafe in multithreaded hosts.
    curl_easy_setopt(curl.handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.handle, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(kDefaultConnectTimeoutMs));
    curl_easy_setopt(curl.handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl.handle, CURLOPT_LOW_SPEED_TIME, static_cast<long>(kDefaultIoTimeoutMs / 1000));

    CURLcode rc = curl_easy_perform(curl.handle);
    if (rc != CURLE_OK)
    {
        throw std::runtime_error(std::string("curl request failed: ") + curl_easy_strerror(rc));
    }

    long statusCode = 0;
    curl_easy_getinfo(curl.handle, CURLINFO_RESPONSE_CODE, &statusCode);
    if (statusCode < 200 || statusCode >= 300)
    {
        throw std::runtime_error("HTTP status " + std::to_string(statusCode));
    }

    return body;
}

}  // namespace js5
