#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

#include "mirror.h"

namespace mf {

struct InstallOptions {
    std::filesystem::path gameDirectory;
    std::string customName;
    Mirror mirror;
    int threads = 8;
    int retries = 3;
    int timeoutSeconds = 30;
    bool force = false;
    bool noAssets = false;
    bool noLogging = false;
    bool verify = true;
    bool verbose = false;
    bool fallbackOfficial = true;
};

struct InstallSummary {
    bool okay = false;
    bool cancelled = false;
    std::string versionId;
    uint64_t downloadedBytes = 0;
    uint32_t downloadedFiles = 0;
    uint32_t skippedFiles = 0;
    uint32_t failedFiles = 0;
    double elapsedSeconds = 0.0;
};

class Installer {
public:
    Installer(InstallOptions options, std::atomic_bool* cancel);
    InstallSummary run(const std::string& requestedVersion);

private:
    InstallSummary runAttempt(const std::string& requestedVersion);

    InstallOptions options_;
    std::atomic_bool* cancel_ = nullptr;
};

}  // namespace mf
