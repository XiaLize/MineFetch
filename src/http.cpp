#include "http.h"

#include "common.h"
#include "sha1.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>
#include <vector>

namespace mf {
namespace {

constexpr size_t kMaxTextSize = 64u * 1024u * 1024u;
constexpr DWORD kReadChunk = 64u * 1024u;

class WinHandle {
public:
    WinHandle() = default;
    explicit WinHandle(HINTERNET handle) : handle_(handle) {}
    ~WinHandle() { reset(); }
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;
    WinHandle(WinHandle&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    WinHandle& operator=(WinHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    HINTERNET get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }
    void reset(HINTERNET handle = nullptr) {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HINTERNET handle_ = nullptr;
};

class FileHandle {
public:
    explicit FileHandle(HANDLE handle) : handle_(handle) {}
    ~FileHandle() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    HANDLE get() const { return handle_; }
    void close() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

class PartFileGuard {
public:
    explicit PartFileGuard(std::filesystem::path path) : path_(std::move(path)) {}
    ~PartFileGuard() {
        if (!dismissed_) {
            DeleteFileW(path_.c_str());
        }
    }
    void dismiss() { dismissed_ = true; }

private:
    std::filesystem::path path_;
    bool dismissed_ = false;
};

struct ParsedUrl {
    bool secure = true;
    std::wstring host;
    INTERNET_PORT port = 443;
    std::wstring path;
};

ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl result;
    const size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        throw HttpError("URL 缺少协议: " + truncateOneLine(url, 120));
    }
    const std::string scheme = toLowerAscii(std::string_view(url).substr(0, schemeEnd));
    if (scheme == "https") {
        result.secure = true;
        result.port = 443;
    } else if (scheme == "http") {
        result.secure = false;
        result.port = 80;
    } else {
        throw HttpError("不支持的 URL 协议: " + scheme);
    }
    const size_t authorityStart = schemeEnd + 3;
    size_t authorityEnd = url.find_first_of("/?#", authorityStart);
    if (authorityEnd == std::string::npos) {
        authorityEnd = url.size();
    }
    std::string authority = url.substr(authorityStart, authorityEnd - authorityStart);
    if (authority.empty()) {
        throw HttpError("URL 缺少主机名: " + truncateOneLine(url, 120));
    }
    if (authority.find('@') != std::string::npos) {
        throw HttpError("不支持带用户信息的 URL");
    }
    std::string host;
    std::string port;
    if (authority.front() == '[') {
        const size_t closing = authority.find(']');
        if (closing == std::string::npos) {
            throw HttpError("IPv6 地址格式无效");
        }
        host = authority.substr(1, closing - 1);
        if (closing + 1 < authority.size()) {
            if (authority[closing + 1] != ':') {
                throw HttpError("URL 主机部分格式无效");
            }
            port = authority.substr(closing + 2);
        }
    } else {
        const size_t colon = authority.rfind(':');
        if (colon != std::string::npos) {
            if (authority.find(':') != colon) {
                throw HttpError("IPv6 地址需要用方括号包围");
            }
            host = authority.substr(0, colon);
            port = authority.substr(colon + 1);
        } else {
            host = authority;
        }
    }
    if (host.empty()) {
        throw HttpError("URL 缺少主机名");
    }
    for (unsigned char c : host) {
        if (c <= 0x20 || c >= 0x7F) {
            throw HttpError("URL 主机名包含非法字符");
        }
    }
    if (!port.empty()) {
        int value = 0;
        for (char c : port) {
            if (c < '0' || c > '9') {
                throw HttpError("URL 端口号无效");
            }
            value = value * 10 + (c - '0');
            if (value > 65535) {
                throw HttpError("URL 端口号超出范围");
            }
        }
        if (value <= 0) {
            throw HttpError("URL 端口号无效");
        }
        result.port = static_cast<INTERNET_PORT>(value);
    }

    std::string path = authorityEnd < url.size() ? url.substr(authorityEnd) : "/";
    const size_t fragment = path.find('#');
    if (fragment != std::string::npos) {
        path.resize(fragment);
    }
    if (path.empty()) {
        path = "/";
    } else if (path.front() == '?') {
        path.insert(path.begin(), '/');
    } else if (path.front() != '/') {
        path.insert(path.begin(), '/');
    }
    result.host = utf8ToWide(host);
    result.path = utf8ToWide(path);
    return result;
}

bool isRetryableHttpStatus(int status) {
    return status == 408 || status == 425 || status == 429 || status >= 500;
}

std::string networkErrorText(DWORD code) {
    const char* message = nullptr;
    switch (code) {
        case ERROR_WINHTTP_TIMEOUT:
            message = "连接超时";
            break;
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
            message = "无法解析主机名";
            break;
        case ERROR_WINHTTP_CANNOT_CONNECT:
            message = "无法连接服务器";
            break;
        case ERROR_WINHTTP_CONNECTION_ERROR:
            message = "连接被重置或中断";
            break;
        case ERROR_WINHTTP_SECURE_FAILURE:
            message = "TLS 握手或证书校验失败";
            break;
        case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
            message = "证书吊销状态检查失败";
            break;
        case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:
            message = "服务器返回了无效响应";
            break;
        case ERROR_WINHTTP_CLIENT_CERT_NO_PRIVATE_KEY:
            message = "系统客户端证书不可用";
            break;
        default:
            return win32ErrorText(code);
    }
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), " (0x%08lX)", static_cast<unsigned long>(code));
    return std::string(message) + buffer;
}

bool isRetryableWinHttpError(DWORD code) {
    switch (code) {
        case ERROR_WINHTTP_TIMEOUT:
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        case ERROR_WINHTTP_CANNOT_CONNECT:
        case ERROR_WINHTTP_CONNECTION_ERROR:
        case ERROR_WINHTTP_RESEND_REQUEST:
        case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
            return true;
        default:
            return false;
    }
}

std::string statusText(HINTERNET request) {
    wchar_t buffer[256]{};
    DWORD size = sizeof(buffer);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_TEXT, WINHTTP_HEADER_NAME_BY_INDEX,
                            buffer, &size, WINHTTP_NO_HEADER_INDEX)) {
        size_t characters = size / sizeof(wchar_t);
        while (characters > 0 && buffer[characters - 1] == L'\0') {
            --characters;
        }
        return trim(wideToUtf8(std::wstring_view(buffer, characters)));
    }
    return {};
}

uint64_t queryContentLength(HINTERNET request) {
    ULONGLONG length = 0;
    DWORD size = sizeof(length);
    if (WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER64,
                            WINHTTP_HEADER_NAME_BY_INDEX, &length, &size,
                            WINHTTP_NO_HEADER_INDEX)) {
        return static_cast<uint64_t>(length);
    }
    return 0;
}

void checkCancel(const std::atomic_bool* cancel) {
    if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
        throw Cancelled();
    }
}

void sleepWithCancel(int milliseconds, const std::atomic_bool* cancel) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        checkCancel(cancel);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void writeAll(HANDLE file, const char* data, DWORD size) {
    DWORD offset = 0;
    while (offset < size) {
        DWORD written = 0;
        if (!WriteFile(file, data + offset, size - offset, &written, nullptr) || written == 0) {
            throw HttpError("写入临时文件失败: " + win32ErrorText(GetLastError()));
        }
        offset += written;
    }
}

void addRequestHeaders(HINTERNET request) {
    static const wchar_t kHeaders[] = L"Accept: */*\r\n";
    if (!WinHttpAddRequestHeaders(request, kHeaders, static_cast<DWORD>(-1),
                                  WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        throw HttpError("设置 HTTP 请求头失败: " + win32ErrorText(GetLastError()));
    }
}

void sendRequest(HINTERNET request, const std::wstring& host) {
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0,
                            0, 0)) {
        const DWORD error = GetLastError();
        throw HttpError("请求 " + wideToUtf8(host) + " 失败: " + networkErrorText(error), 0,
                        isRetryableWinHttpError(error));
    }
    if (!WinHttpReceiveResponse(request, nullptr)) {
        const DWORD error = GetLastError();
        throw HttpError("读取 " + wideToUtf8(host) + " 的响应失败: " + networkErrorText(error), 0,
                        isRetryableWinHttpError(error));
    }
}

int queryStatus(HINTERNET request) {
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        throw HttpError("读取 HTTP 状态码失败: " + networkErrorText(GetLastError()), 0, true);
    }
    return static_cast<int>(status);
}

int querySecondsHeader(HINTERNET request, const wchar_t* name) {
    wchar_t buffer[64]{};
    DWORD size = sizeof(buffer);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, buffer, &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        return 0;
    }
    const size_t characters = std::min<size_t>(size / sizeof(wchar_t), std::size(buffer));
    std::string text = trim(wideToUtf8(std::wstring_view(buffer, characters)));
    int value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            break;
        }
        if (value > 86400) {
            break;
        }
        value = value * 10 + (c - '0');
    }
    return std::min(value, 300);
}

int queryRetryAfter(HINTERNET request) {
    int value = querySecondsHeader(request, L"Retry-After-short");
    if (value == 0) {
        value = querySecondsHeader(request, L"Retry-After");
    }
    return value;
}

int retryDelayMilliseconds(int attempt, const HttpError& error) {
    int waitMilliseconds = 1000 << std::min(attempt, 3);
    if (error.retryAfterSeconds() > 0) {
        waitMilliseconds = std::max(waitMilliseconds, error.retryAfterSeconds() * 1000);
    }
    return waitMilliseconds;
}

}  // namespace

HttpClient::HttpClient(HttpOptions options) : options_(std::move(options)) {
    options_.maxAttempts = std::max(1, options_.maxAttempts);
    options_.timeoutSeconds = std::max(1, options_.timeoutSeconds);
    const std::wstring agent = utf8ToWide(options_.userAgent);
    session_ = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session_ == nullptr) {
        throw HttpError("初始化 WinHTTP 失败: " + win32ErrorText(GetLastError()));
    }
    const int timeoutMs = options_.timeoutSeconds * 1000;
    WinHttpSetTimeouts(session_, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
}

HttpClient::~HttpClient() {
    if (session_ != nullptr) {
        WinHttpCloseHandle(session_);
    }
}

HttpResult HttpClient::get(const std::string& url, std::string& body,
                           const std::atomic_bool* cancel) {
    for (int attempt = 0; attempt < options_.maxAttempts; ++attempt) {
        try {
            return getAttempt(url, body, cancel);
        } catch (const Cancelled&) {
            throw;
        } catch (const HttpError& error) {
            if (!error.retryable() || attempt + 1 >= options_.maxAttempts) {
                throw;
            }
            sleepWithCancel(retryDelayMilliseconds(attempt, error), cancel);
        }
    }
    throw HttpError("请求失败: " + url);
}

HttpResult HttpClient::getAttempt(const std::string& url, std::string& body,
                                  const std::atomic_bool* cancel) {
    checkCancel(cancel);
    body.clear();
    ParsedUrl parsed = parseUrl(url);
    WinHandle connection(WinHttpConnect(session_, parsed.host.c_str(), parsed.port, 0));
    if (!connection) {
        const DWORD error = GetLastError();
        throw HttpError("连接主机失败: " + networkErrorText(error), 0,
                        isRetryableWinHttpError(error));
    }
    WinHandle request(WinHttpOpenRequest(connection.get(), L"GET", parsed.path.c_str(), nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        parsed.secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request) {
        const DWORD error = GetLastError();
        throw HttpError("创建 HTTP 请求失败: " + networkErrorText(error), 0,
                        isRetryableWinHttpError(error));
    }
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy,
                          sizeof(redirectPolicy))) {
        throw HttpError("设置 HTTP 重定向策略失败: " + win32ErrorText(GetLastError()));
    }
    addRequestHeaders(request.get());
    sendRequest(request.get(), parsed.host);

    const int status = queryStatus(request.get());
    const uint64_t contentLength = queryContentLength(request.get());
    if (status < 200 || status >= 300) {
        const std::string text = statusText(request.get());
        throw HttpError("HTTP " + std::to_string(status) +
                            (text.empty() ? std::string() : " " + text),
                        status, isRetryableHttpStatus(status), queryRetryAfter(request.get()));
    }

    for (;;) {
        checkCancel(cancel);
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            const DWORD error = GetLastError();
            throw HttpError("读取 HTTP 响应失败: " + networkErrorText(error), 0,
                            isRetryableWinHttpError(error));
        }
        if (available == 0) {
            break;
        }
        std::vector<char> buffer(std::min<DWORD>(available, kReadChunk));
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), buffer.data(),
                             static_cast<DWORD>(buffer.size()), &read)) {
            const DWORD error = GetLastError();
            throw HttpError("下载响应内容失败: " + networkErrorText(error), 0,
                            isRetryableWinHttpError(error));
        }
        if (read == 0) {
            break;
        }
        if (body.size() + read > kMaxTextSize) {
            throw HttpError("服务器返回的文本内容过大");
        }
        body.append(buffer.data(), read);
    }
    return HttpResult{status, contentLength};
}

HttpResult HttpClient::download(const std::string& url, const std::filesystem::path& target,
                                const std::string& expectedSha1, const std::atomic_bool* cancel,
                                const std::function<void(int64_t)>& progress,
                                uint64_t expectedSize) {
    for (int attempt = 0; attempt < options_.maxAttempts; ++attempt) {
        uint64_t attemptBytes = 0;
        try {
            return downloadAttempt(url, target, expectedSha1, cancel, progress, &attemptBytes,
                                   expectedSize);
        } catch (const Cancelled&) {
            if (attemptBytes > 0 && progress) {
                progress(-static_cast<int64_t>(attemptBytes));
            }
            throw;
        } catch (const HttpError& error) {
            if (attemptBytes > 0 && progress) {
                progress(-static_cast<int64_t>(attemptBytes));
            }
            if (!error.retryable() || attempt + 1 >= options_.maxAttempts) {
                throw;
            }
            sleepWithCancel(retryDelayMilliseconds(attempt, error), cancel);
        } catch (...) {
            if (attemptBytes > 0 && progress) {
                progress(-static_cast<int64_t>(attemptBytes));
            }
            throw;
        }
    }
    throw HttpError("下载失败: " + url);
}

HttpResult HttpClient::downloadAttempt(const std::string& url,
                                       const std::filesystem::path& target,
                                       const std::string& expectedSha1,
                                       const std::atomic_bool* cancel,
                                       const std::function<void(int64_t)>& progress,
                                       uint64_t* attemptBytes, uint64_t expectedSize) {
    checkCancel(cancel);
    *attemptBytes = 0;
    std::error_code ec;
    if (!target.parent_path().empty()) {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) {
            throw HttpError("无法创建目录 " + pathToUtf8(target.parent_path()) + ": " +
                            ec.message());
        }
    }
    const std::filesystem::path partPath(target.wstring() + L".part");
    DeleteFileW(partPath.c_str());
    PartFileGuard partGuard(partPath);

    ParsedUrl parsed = parseUrl(url);
    WinHandle connection(WinHttpConnect(session_, parsed.host.c_str(), parsed.port, 0));
    if (!connection) {
        const DWORD error = GetLastError();
        throw HttpError("连接主机失败: " + networkErrorText(error), 0,
                        isRetryableWinHttpError(error));
    }
    WinHandle request(WinHttpOpenRequest(connection.get(), L"GET", parsed.path.c_str(), nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        parsed.secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request) {
        const DWORD error = GetLastError();
        throw HttpError("创建 HTTP 请求失败: " + networkErrorText(error), 0,
                        isRetryableWinHttpError(error));
    }
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy,
                          sizeof(redirectPolicy))) {
        throw HttpError("设置 HTTP 重定向策略失败: " + win32ErrorText(GetLastError()));
    }
    addRequestHeaders(request.get());
    sendRequest(request.get(), parsed.host);

    const int status = queryStatus(request.get());
    const uint64_t contentLength = queryContentLength(request.get());
    if (status < 200 || status >= 300) {
        const std::string text = statusText(request.get());
        throw HttpError("HTTP " + std::to_string(status) +
                            (text.empty() ? std::string() : " " + text),
                        status, isRetryableHttpStatus(status), queryRetryAfter(request.get()));
    }

    FileHandle file(CreateFileW(partPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) {
        throw HttpError("无法创建临时文件 " + pathToUtf8(partPath) + ": " +
                        win32ErrorText(GetLastError()));
    }

    Sha1 hasher;
    const bool verifyHash = !expectedSha1.empty();
    uint64_t total = 0;
    for (;;) {
        checkCancel(cancel);
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            const DWORD error = GetLastError();
            throw HttpError("读取 HTTP 响应失败: " + networkErrorText(error), 0,
                            isRetryableWinHttpError(error));
        }
        if (available == 0) {
            break;
        }
        std::vector<char> buffer(std::min<DWORD>(available, kReadChunk));
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), buffer.data(),
                             static_cast<DWORD>(buffer.size()), &read)) {
            const DWORD error = GetLastError();
            throw HttpError("下载响应内容失败: " + networkErrorText(error), 0,
                            isRetryableWinHttpError(error));
        }
        if (read == 0) {
            break;
        }
        writeAll(file.get(), buffer.data(), read);
        if (verifyHash) {
            hasher.update(buffer.data(), read);
        }
        total += read;
        *attemptBytes = total;
        if (progress) {
            progress(static_cast<int64_t>(read));
        }
    }

    if (!FlushFileBuffers(file.get())) {
        throw HttpError("刷新临时文件失败: " + win32ErrorText(GetLastError()));
    }
    file.close();

    if (expectedSize > 0 && total != expectedSize) {
        throw HttpError("文件大小不匹配（期望 " + std::to_string(expectedSize) + " 字节，实际 " +
                            std::to_string(total) + " 字节）",
                        0, true);
    }
    if (verifyHash) {
        const std::string actual = hasher.finishHex();
        if (!iequals(actual, expectedSha1)) {
            throw HttpError("SHA-1 校验失败（期望 " + expectedSha1 + "，实际 " + actual + "）", 0,
                            true);
        }
    }
    if (!MoveFileExW(partPath.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        throw HttpError("保存文件 " + pathToUtf8(target) + " 失败: " + win32ErrorText(error));
    }
    partGuard.dismiss();
    return HttpResult{status, contentLength};
}

}  // namespace mf
