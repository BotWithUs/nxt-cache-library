#pragma once

#include <string>

namespace js5 {

// Minimal HTTPS GET client backed by libcurl. Returns the response body as a string.
// Throws std::runtime_error on any failure (connection, non-2xx status, read).
std::string httpGet(const std::string &host, const std::string &path, bool useTls = false);

// Number of httpGet calls this process has made, counted on entry (a failed
// request still counts). Read-only diagnostic with no effect on behaviour;
// tests use it to prove a code path made no HTTP request (no jav_config fetch).
int httpGetCallCount();

}  // namespace js5
