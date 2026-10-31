#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mf {

namespace fs = std::filesystem;

class Cancelled : public std::runtime_error {
public:
    Cancelled() : std::runtime_error("操作已取消") {}
};

std::wstring utf8ToWide(std::string_view text);
std::string wideToUtf8(std::wstring_view text);
fs::path pathFromUtf8(std::string_view text);
std::string pathToUtf8(const fs::path& path);

std::string trim(std::string_view text);
std::string toLowerAscii(std::string_view text);
bool iequals(std::string_view a, std::string_view b);
bool startsWith(std::string_view text, std::string_view prefix);
bool endsWith(std::string_view text, std::string_view suffix);
std::string replaceAll(std::string text, std::string_view from, std::string_view to);

std::string formatBytes(uint64_t bytes);
std::string formatDuration(double seconds);
std::string formatRate(double bytesPerSecond);
std::string formatCount(uint64_t value);
std::string formatPercent(double ratio);

std::string win32ErrorText(DWORD code);
std::string truncateOneLine(std::string text, size_t maxLength);

bool readFileUtf8(const fs::path& path, std::string& out);
bool writeFileAtomic(const fs::path& path, std::string_view data, std::string& error);

bool isSafeRelativePath(std::string_view path);
bool isSafePathComponent(std::string_view name);
std::string normalizeRelativePath(std::string path);

fs::path defaultGameDirectory();
std::string executableDirectoryUtf8();

class Console {
public:
    static void initialize();
    static bool isTerminal();
    static void enableAnsi();
    static bool ansiEnabled();
    static void clearLine();
    static void resetColor();
    static void setColor(const char* ansi);
    static void print(const std::string& text);
    static void println(const std::string& text = {});
    static void error(const std::string& text);
    static void warning(const std::string& text);
    static void success(const std::string& text);
};

}  // namespace mf
