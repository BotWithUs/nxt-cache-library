#pragma once

#include <string>

namespace js5 {

// Minimal HTTPS GET client backed by libcurl. Returns the response body as a string.
// Throws std::runtime_error on any failure (connection, non-2xx status, read).
std::string httpGet(const std::string &host, const std::string &path, bool useTls = false);

}  // namespace js5
