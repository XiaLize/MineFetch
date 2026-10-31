#include "common.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sstream>

namespace mf {
namespace {

std::mutex g_consoleMutex;
bool g_consoleInitialized = false;
bool g_consoleTerminal = false;
bool g_consoleAnsi = false;
std::atomic_bool g_consoleSilent{false};

constexpr const char* kReset = "\x1b[0m";
constexpr const char* kRed = "\x1b[31m";
constexpr const char* kGreen = "\x1b[32m";
constexpr const char* kYellow = "\x1b[33m";

std::string formatDouble(double value, int precision) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
    std::string result(buffer);
    if (result.find('.') != std::string::npos) {
        while (!result.empty() && result.back() == '0') {
            result.pop_back();
        }
        if (!result.empty() && result.back() == '.') {
            result.pop_back();
        }
    }
    return result;
}

bool writeAll(HANDLE file, const char* data, size_t size) {
    size_t written = 0;
    while (written < size) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(size - written, 1u << 30));
        DWORD actual = 0;
        if (!WriteFile(file, data + written, chunk, &actual, nullptr)) {
            return false;
        }
        if (actual == 0) {
            return false;
        }
        written += actual;
    }
    return true;
}

}  // namespace

std::wstring utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                     static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) {
        needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                     nullptr, 0);
    }
    if (needed <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), needed);
    return result;
}

std::string wideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                     nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), needed, nullptr, nullptr);
    return result;
}

fs::path pathFromUtf8(std::string_view text) {
    return fs::u8path(text.begin(), text.end());
}

std::string pathToUtf8(const fs::path& path) {
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

std::string trim(std::string_view text) {
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string toLowerAscii(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool endsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.substr(text.size() - suffix.size()) == suffix;
}

std::string replaceAll(std::string text, std::string_view from, std::string_view to) {
    if (from.empty()) {
        return text;
    }
    size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
    return text;
}

std::string formatBytes(uint64_t bytes) {
    constexpr double kKiB = 1024.0;
    constexpr double kMiB = kKiB * 1024.0;
    constexpr double kGiB = kMiB * 1024.0;
    const double value = static_cast<double>(bytes);
    if (value >= kGiB) {
        return formatDouble(value / kGiB, 2) + " GB";
    }
    if (value >= kMiB) {
        return formatDouble(value / kMiB, 1) + " MB";
    }
    if (value >= kKiB) {
        return formatDouble(value / kKiB, 1) + " KB";
    }
    return std::to_string(bytes) + " B";
}

std::string formatDuration(double seconds) {
    if (seconds < 0.0 || seconds != seconds) {
        return "--";
    }
    if (seconds < 60.0) {
        return formatDouble(seconds, 1) + " 秒";
    }
    const uint64_t total = static_cast<uint64_t>(seconds + 0.5);
    const uint64_t hours = total / 3600;
    const uint64_t minutes = (total % 3600) / 60;
    const uint64_t secs = total % 60;
    std::ostringstream out;
    if (hours > 0) {
        out << hours << " 小时 " << minutes << " 分";
    } else {
        out << minutes << " 分 " << secs << " 秒";
    }
    return out.str();
}

std::string formatRate(double bytesPerSecond) {
    if (bytesPerSecond < 0.0 || bytesPerSecond != bytesPerSecond) {
        return "--";
    }
    return formatBytes(static_cast<uint64_t>(bytesPerSecond)) + "/s";
}

std::string formatCount(uint64_t value) {
    std::string text = std::to_string(value);
    for (int i = static_cast<int>(text.size()) - 3; i > 0; i -= 3) {
        text.insert(static_cast<size_t>(i), ",");
    }
    return text;
}

std::string formatPercent(double ratio) {
    if (ratio < 0.0 || ratio != ratio) {
        ratio = 0.0;
    }
    if (ratio > 1.0) {
        ratio = 1.0;
    }
    return formatDouble(ratio * 100.0, 1) + "%";
}

std::string win32ErrorText(DWORD code) {
    wchar_t* buffer = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD length = FormatMessageW(flags, nullptr, code, 0,
                                  reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::string message;
    if (length > 0 && buffer != nullptr) {
        message = wideToUtf8(std::wstring_view(buffer, length));
        LocalFree(buffer);
        message = trim(message);
        while (!message.empty() && (message.back() == '.' || message.back() == '\r' ||
                                    message.back() == '\n' || message.back() == ' ')) {
            message.pop_back();
        }
    }
    if (message.empty()) {
        message = "Windows 错误";
    }
    char codeText[32];
    std::snprintf(codeText, sizeof(codeText), " (0x%08lX)", static_cast<unsigned long>(code));
    return message + codeText;
}

std::string truncateOneLine(std::string text, size_t maxLength) {
    for (char& c : text) {
        if (c == '\r' || c == '\n' || c == '\t') {
            c = ' ';
        }
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    if (text.size() <= maxLength) {
        return text;
    }
    size_t cut = maxLength;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    text.resize(cut);
    return text + "...";
}

bool readFileUtf8(const fs::path& path, std::string& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    constexpr uint64_t kMaxTextFileSize = 64ull * 1024ull * 1024ull;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        static_cast<uint64_t>(size.QuadPart) > kMaxTextFileSize) {
        CloseHandle(file);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    size_t offset = 0;
    while (offset < out.size()) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(out.size() - offset, 1u << 20));
        DWORD actual = 0;
        if (!ReadFile(file, out.data() + offset, chunk, &actual, nullptr) || actual == 0) {
            CloseHandle(file);
            return false;
        }
        offset += actual;
    }
    CloseHandle(file);
    return true;
}

bool writeFileAtomic(const fs::path& path, std::string_view data, std::string& error) {
    std::error_code ec;
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            error = "无法创建目录 " + pathToUtf8(path.parent_path()) + ": " + ec.message();
            return false;
        }
    }
    static std::atomic<uint32_t> counter{0};
    const fs::path temporary =
        path.wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(counter.fetch_add(1));
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "无法写入 " + pathToUtf8(temporary) + ": " + win32ErrorText(GetLastError());
        return false;
    }
    bool okay = writeAll(file, data.data(), data.size());
    if (okay) {
        okay = FlushFileBuffers(file) != FALSE;
    }
    const DWORD writeError = okay ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!okay) {
        DeleteFileW(temporary.c_str());
        error = "写入 " + pathToUtf8(temporary) + " 失败: " + win32ErrorText(writeError);
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD moveError = GetLastError();
        DeleteFileW(temporary.c_str());
        error = "替换文件 " + pathToUtf8(path) + " 失败: " + win32ErrorText(moveError);
        return false;
    }
    return true;
}

bool isSafePathComponent(std::string_view name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    for (unsigned char c : name) {
        if (c < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' ||
            c == '\\' || c == '|' || c == '?' || c == '*') {
            return false;
        }
    }
    if (name.back() == ' ' || name.back() == '.') {
        return false;
    }
    return true;
}

std::string normalizeRelativePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (startsWith(path, "./")) {
        path.erase(0, 2);
    }
    return path;
}

bool isSafeRelativePath(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.find('\0') != std::string_view::npos) {
        return false;
    }
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) &&
        path[1] == ':') {
        return false;
    }
    size_t begin = 0;
    while (begin <= path.size()) {
        size_t end = path.find('/', begin);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        if (!isSafePathComponent(path.substr(begin, end - begin))) {
            return false;
        }
        if (end == path.size()) {
            break;
        }
        begin = end + 1;
    }
    return true;
}

fs::path defaultGameDirectory() {
    wchar_t appData[MAX_PATH * 2]{};
    DWORD length = GetEnvironmentVariableW(L"APPDATA", appData,
                                           static_cast<DWORD>(std::size(appData)));
    if (length > 0 && length < std::size(appData)) {
        return fs::path(appData) / L".minecraft";
    }
    std::error_code ec;
    fs::path current = fs::current_path(ec);
    if (ec) {
        return fs::path(L".minecraft");
    }
    return current / L".minecraft";
}

std::string executableDirectoryUtf8() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return pathToUtf8(fs::path(buffer).parent_path());
}

void Console::initialize() {
    if (g_consoleInitialized) {
        return;
    }
    g_consoleInitialized = true;
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    g_consoleTerminal = output != INVALID_HANDLE_VALUE && output != nullptr &&
                        GetConsoleMode(output, &mode) != FALSE;
    if (g_consoleTerminal) {
        g_consoleAnsi = SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != FALSE;
    }
}

bool Console::isTerminal() {
    initialize();
    return g_consoleTerminal;
}

void Console::setSilent(bool silent) {
    g_consoleSilent.store(silent, std::memory_order_relaxed);
}

bool Console::isSilent() {
    return g_consoleSilent.load(std::memory_order_relaxed);
}

void Console::enableAnsi() {
    initialize();
}

bool Console::ansiEnabled() {
    initialize();
    return g_consoleAnsi;
}

void Console::clearLine() {
    initialize();
    if (!g_consoleTerminal || g_consoleSilent.load(std::memory_order_relaxed)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    if (g_consoleAnsi) {
        std::cout << "\r\x1b[K" << std::flush;
    } else {
        std::cout << '\r' << std::string(120, ' ') << '\r' << std::flush;
    }
}

void Console::resetColor() {
    if (g_consoleAnsi && !g_consoleSilent.load(std::memory_order_relaxed)) {
        std::cout << kReset;
    }
}

void Console::setColor(const char* ansi) {
    if (g_consoleAnsi && ansi != nullptr &&
        !g_consoleSilent.load(std::memory_order_relaxed)) {
        std::cout << ansi << std::flush;
    }
}

void Console::print(const std::string& text) {
    if (g_consoleSilent.load(std::memory_order_relaxed)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    std::cout << text << std::flush;
}

void Console::println(const std::string& text) {
    if (g_consoleSilent.load(std::memory_order_relaxed)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    std::cout << text << '\n' << std::flush;
}

void Console::error(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    if (g_consoleAnsi) {
        std::cout << kRed << "错误: " << kReset;
    } else {
        std::cout << "错误: ";
    }
    std::cout << text << '\n' << std::flush;
}

void Console::warning(const std::string& text) {
    if (g_consoleSilent.load(std::memory_order_relaxed)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    if (g_consoleAnsi) {
        std::cout << kYellow << "提示: " << kReset;
    } else {
        std::cout << "提示: ";
    }
    std::cout << text << '\n' << std::flush;
}

void Console::success(const std::string& text) {
    if (g_consoleSilent.load(std::memory_order_relaxed)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    if (g_consoleAnsi) {
        std::cout << kGreen << text << kReset << '\n' << std::flush;
    } else {
        std::cout << text << '\n' << std::flush;
    }
}

}  // namespace mf
