#pragma once

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>

namespace mf {

class HttpError : public std::runtime_error {
public:
    HttpError(const std::string& message, int statusCode = 0, bool retryable = false,
              int retryAfterSeconds = 0)
        : std::runtime_error(message),
          statusCode_(statusCode),
          retryable_(retryable),
          retryAfterSeconds_(retryAfterSeconds) {}

    int statusCode() const { return statusCode_; }
    bool retryable() const { return retryable_; }
    int retryAfterSeconds() const { return retryAfterSeconds_; }

private:
    int statusCode_ = 0;
    bool retryable_ = false;
    int retryAfterSeconds_ = 0;
};

struct HttpOptions {
    std::string userAgent = "MineFetch/1.0 (Minecraft Java downloader)";
    int timeoutSeconds = 30;
    int maxAttempts = 3;
};

struct HttpResult {
    int statusCode = 0;
    uint64_t contentLength = 0;
};

class HttpClient {
public:
    explicit HttpClient(HttpOptions options);
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    HttpResult get(const std::string& url, std::string& body,
                   const std::atomic_bool* cancel = nullptr);
    HttpResult download(const std::string& url, const std::filesystem::path& target,
                        const std::string& expectedSha1,
                        const std::atomic_bool* cancel,
                        const std::function<void(int64_t)>& progress,
                        uint64_t expectedSize = 0);

private:
    HttpResult getAttempt(const std::string& url, std::string& body,
                          const std::atomic_bool* cancel);
    HttpResult downloadAttempt(const std::string& url, const std::filesystem::path& target,
                               const std::string& expectedSha1,
                               const std::atomic_bool* cancel,
                               const std::function<void(int64_t)>& progress,
                               uint64_t* attemptBytes, uint64_t expectedSize);

    HttpOptions options_;
    HINTERNET session_ = nullptr;
};

}  // namespace mf
