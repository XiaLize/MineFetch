#include "installer.h"

#include "common.h"
#include "http.h"
#include "json.h"
#include "minecraft.h"
#include "remote.h"
#include "sha1.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <vector>

namespace mf {
namespace {

struct Task {
    std::string label;
    std::filesystem::path path;
    std::string url;
    std::string sha1;
    uint64_t size = 0;
    bool hasSize = false;
};

std::string relativeLabel(const std::filesystem::path& base, const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(path, base, ec);
    if (!ec && !relative.empty()) {
        return pathToUtf8(relative);
    }
    return pathToUtf8(path);
}

void appendTask(std::vector<Task>& tasks, std::set<std::string>& seen, Task task) {
    if (task.url.empty()) {
        throw std::runtime_error("文件 " + task.label + " 没有可用的下载地址");
    }
    const std::string key = toLowerAscii(pathToUtf8(task.path));
    if (!seen.insert(key).second) {
        return;
    }
    tasks.push_back(std::move(task));
}

std::filesystem::path safeJoin(const std::filesystem::path& base, const std::string& relative) {
    const std::string normalized = normalizeRelativePath(relative);
    if (!isSafeRelativePath(normalized)) {
        throw std::runtime_error("元数据包含不安全的文件路径: " + relative);
    }
    return base / pathFromUtf8(normalized);
}

HttpOptions makeHttpOptions(const InstallOptions& options) {
    HttpOptions http;
    http.timeoutSeconds = options.timeoutSeconds;
    http.maxAttempts = std::clamp(options.retries + 1, 1, 12);
    return http;
}

void addBytes(std::atomic<uint64_t>& total, int64_t delta) {
    if (delta >= 0) {
        total.fetch_add(static_cast<uint64_t>(delta), std::memory_order_relaxed);
        return;
    }
    const uint64_t amount = static_cast<uint64_t>(-delta);
    uint64_t current = total.load(std::memory_order_relaxed);
    while (current > 0) {
        const uint64_t next = amount >= current ? 0 : current - amount;
        if (total.compare_exchange_weak(current, next, std::memory_order_relaxed)) {
            break;
        }
    }
}

std::string progressLine(uint64_t bytes, uint64_t totalBytes, uint32_t completed,
                         uint32_t totalFiles, uint32_t failed, double elapsed) {
    double ratio = 0.0;
    if (totalBytes > 0) {
        ratio = std::min(1.0, static_cast<double>(bytes) / static_cast<double>(totalBytes));
    } else if (totalFiles > 0) {
        ratio = static_cast<double>(completed + failed) / static_cast<double>(totalFiles);
    }
    const int width = 24;
    const int filled = static_cast<int>(ratio * width);
    std::string bar(static_cast<size_t>(width), '-');
    for (int i = 0; i < std::min(width, std::max(0, filled)); ++i) {
        bar[static_cast<size_t>(i)] = '#';
    }
    const double speed = elapsed > 0.001 ? static_cast<double>(bytes) / elapsed : 0.0;
    std::string line = "[" + bar + "] " + formatPercent(ratio) + "  ";
    if (totalBytes > 0) {
        line += formatBytes(bytes) + "/" + formatBytes(totalBytes);
    } else {
        line += formatBytes(bytes);
    }
    line += "  " + formatRate(speed) + "  ";
    line += std::to_string(completed) + "/" + std::to_string(totalFiles) + " 个文件";
    if (failed > 0) {
        line += "  失败 " + std::to_string(failed);
    }
    return line;
}

void renderProgress(const std::string& line, bool final, size_t& lastLength) {
    if (!Console::isTerminal()) {
        return;
    }
    if (Console::ansiEnabled()) {
        Console::print("\r\x1b[K" + line);
    } else {
        std::string padded = line;
        if (padded.size() < lastLength) {
            padded.append(lastLength - padded.size(), ' ');
        }
        Console::print("\r" + padded);
    }
    lastLength = line.size();
    if (final) {
        Console::print("\n");
    }
}

bool fileMatches(const Task& task, bool verify, const std::atomic_bool* cancel) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(task.path, ec) || ec) {
        return false;
    }
    const uint64_t size = std::filesystem::file_size(task.path, ec);
    if (ec) {
        return false;
    }
    if (task.hasSize && size != task.size) {
        return false;
    }
    if (!verify || task.sha1.empty()) {
        return true;
    }
    return iequals(sha1File(task.path, cancel), task.sha1);
}

void copyLegacyAssets(const std::filesystem::path& gameDirectory, const AssetIndex& index,
                      const std::atomic_bool* cancel) {
    if (!index.virtualFiles && !index.mapToResources) {
        return;
    }
    const std::filesystem::path objects = gameDirectory / L"assets" / L"objects";
    std::vector<std::pair<std::filesystem::path, const AssetObject*>> destinations;
    if (index.virtualFiles) {
        destinations.emplace_back(gameDirectory / L"assets" / L"virtual" / L"legacy", nullptr);
    }
    if (index.mapToResources) {
        destinations.emplace_back(gameDirectory / L"resources", nullptr);
    }
    uint32_t copied = 0;
    uint32_t skipped = 0;
    uint32_t missing = 0;
    for (const auto& destination : destinations) {
        for (const AssetObject& object : index.objects) {
            if (cancel != nullptr && cancel->load()) {
                throw Cancelled();
            }
            const std::filesystem::path source =
                objects / utf8ToWide(object.hash.substr(0, 2)) / utf8ToWide(object.hash);
            const std::filesystem::path target = destination.first / pathFromUtf8(object.name);
            std::error_code ec;
            if (!std::filesystem::is_regular_file(source, ec) || ec) {
                ++missing;
                continue;
            }
            const uint64_t sourceSize = std::filesystem::file_size(source, ec);
            if (ec) {
                ++missing;
                continue;
            }
            if (std::filesystem::is_regular_file(target, ec) && !ec) {
                const uint64_t targetSize = std::filesystem::file_size(target, ec);
                if (!ec && targetSize == sourceSize) {
                    ++skipped;
                    continue;
                }
            }
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) {
                Console::warning("无法创建目录 " + pathToUtf8(target.parent_path()) + ": " +
                                 ec.message());
                continue;
            }
            std::filesystem::copy_file(source, target,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                Console::warning("复制旧版资源失败 " + pathToUtf8(target) + ": " + ec.message());
            } else {
                ++copied;
            }
        }
    }
    Console::println("旧版资源目录：复制 " + std::to_string(copied) + " 个，已存在 " +
                     std::to_string(skipped) + " 个" +
                     (missing > 0 ? "，缺失 " + std::to_string(missing) + " 个" : ""));
}

}  // namespace

Installer::Installer(InstallOptions options, std::atomic_bool* cancel)
    : options_(std::move(options)), cancel_(cancel) {}

InstallSummary Installer::run(const std::string& requestedVersion) {
    InstallSummary first;
    std::string firstError;
    bool threw = false;
    try {
        first = runAttempt(requestedVersion);
    } catch (const Cancelled&) {
        throw;
    } catch (const std::exception& error) {
        threw = true;
        firstError = error.what();
    }

    const bool bmclapi =
        options_.mirror.root.find("bangbang93") != std::string::npos && !options_.mirror.official;
    const bool needsFallback = threw || !first.okay;
    if (needsFallback && !first.cancelled && options_.fallbackOfficial && bmclapi) {
        Console::warning("BMCLAPI 下载未完成，自动切换到 Mojang 官方源重试（--no-fallback 可关闭）");
        options_.mirror = makeMirror("mojang");
        if (threw) {
            Console::warning("上次错误: " + firstError);
        }
        return runAttempt(requestedVersion);
    }
    if (threw) {
        throw std::runtime_error(firstError);
    }
    return first;
}

InstallSummary Installer::runAttempt(const std::string& requestedVersion) {
    const auto started = std::chrono::steady_clock::now();
    InstallSummary summary;
    summary.versionId = requestedVersion;

    std::error_code ec;
    if (!std::filesystem::create_directories(options_.gameDirectory, ec) && ec) {
        throw std::runtime_error("无法创建游戏目录 " + pathToUtf8(options_.gameDirectory) + ": " +
                                 ec.message());
    }
    if (!std::filesystem::is_directory(options_.gameDirectory, ec) || ec) {
        throw std::runtime_error("游戏目录不是文件夹: " +
                                 pathToUtf8(options_.gameDirectory));
    }

    Console::println("下载源: " + options_.mirror.name + "  (" + options_.mirror.root + ")");
    Console::println("目标目录: " + pathToUtf8(options_.gameDirectory));
    if (options_.mirror.root.find("bangbang93") != std::string::npos) {
        Console::println("BMCLAPI 文件版权归原站点所有；本程序只在下载界面展示来源。");
    }

    HttpOptions metaHttpOptions = makeHttpOptions(options_);
    const bool usingBmclapi =
        options_.mirror.root.find("bangbang93") != std::string::npos &&
        !options_.mirror.official;
    if (usingBmclapi && options_.fallbackOfficial) {
        // Fail over to the official source quickly instead of sleeping through
        // several 45-second BMCLAPI rate-limit windows.
        metaHttpOptions.maxAttempts = 1;
        metaHttpOptions.timeoutSeconds = std::min(metaHttpOptions.timeoutSeconds, 10);
    }
    HttpClient metadataHttp(metaHttpOptions);

    const std::string request = trim(requestedVersion);
    const bool needsManifest = request.empty() || iequals(request, "latest") ||
                               iequals(request, "release") ||
                               iequals(request, "latest-release") ||
                               iequals(request, "snapshot") ||
                               iequals(request, "latest-snapshot");

    Manifest manifest;
    bool haveManifest = false;
    Console::println("正在获取版本清单（" + options_.mirror.name + "）...");
    try {
        manifest = fetchManifest(metadataHttp, options_.mirror, cancel_);
        haveManifest = true;
    } catch (const Cancelled&) {
        throw;
    } catch (const std::exception& error) {
        if (needsManifest || !isSafeMinecraftVersionId(request)) {
            throw std::runtime_error("获取版本清单失败: " + std::string(error.what()));
        }
        std::error_code localError;
        const std::filesystem::path cachedJson =
            options_.gameDirectory / L"versions" / pathFromUtf8(request) /
            (pathFromUtf8(request).wstring() + L".json");
        if (!std::filesystem::is_regular_file(cachedJson, localError) || localError) {
            throw std::runtime_error("获取版本清单失败: " + std::string(error.what()));
        }
        Console::warning("获取版本清单失败，改用本地版本缓存：" +
                         std::string(error.what()));
    }

    VersionSummary versionSummary;
    if (haveManifest) {
        versionSummary = resolveVersionSummary(manifest, request);
    } else {
        versionSummary.id = request;
        versionSummary.url.clear();
    }
    if (!isSafeMinecraftVersionId(versionSummary.id)) {
        throw std::runtime_error("版本号包含不安全字符: " + versionSummary.id);
    }

    const std::filesystem::path versionDirectory =
        options_.gameDirectory / L"versions" / pathFromUtf8(versionSummary.id);
    const std::filesystem::path versionJsonPath =
        versionDirectory / (pathFromUtf8(versionSummary.id).wstring() + L".json");
    std::string rawVersionJson;
    VersionMeta version =
        fetchVersionMeta(metadataHttp, options_.mirror, versionSummary, versionJsonPath,
                         options_.verify, cancel_, &rawVersionJson);
    if (!isSafeMinecraftVersionId(version.id)) {
        throw std::runtime_error("版本 JSON 的 id 包含不安全字符: " + version.id);
    }
    if (!version.inheritsFrom.empty()) {
        throw std::runtime_error("版本 '" + version.id + "' 继承自 '" + version.inheritsFrom +
                                 "'，本下载器目前只支持独立原版版本");
    }
    if (!version.hasClient) {
        throw std::runtime_error("版本 JSON 没有客户端下载信息");
    }

    const std::string sourceId = version.id;
    const std::string outputId =
        options_.customName.empty() ? sourceId : options_.customName;
    if (!isSafeMinecraftVersionId(outputId)) {
        throw std::runtime_error("自定义版本名包含不安全字符: " + outputId);
    }
    const bool customName = !iequals(outputId, sourceId);
    if (customName) {
        if (iequals(outputId, "latest") || iequals(outputId, "latest-release") ||
            iequals(outputId, "latest-snapshot") || iequals(outputId, "release") ||
            iequals(outputId, "snapshot")) {
            throw std::runtime_error("自定义版本名不能使用 " + outputId);
        }
        if (haveManifest && manifest.findVersion(outputId) != nullptr) {
            throw std::runtime_error("不能使用官方版本号作为自定义名称: " + outputId);
        }
    }

    std::string jsonToWrite = rawVersionJson;
    if (customName) {
        json::Value document = json::parse(rawVersionJson);
        if (!document.set("id", json::Value::makeString(outputId))) {
            throw std::runtime_error("无法修改版本 JSON 的 id");
        }
        jsonToWrite = json::stringify(document, true);
    }

    const std::filesystem::path outputDirectory =
        options_.gameDirectory / L"versions" / pathFromUtf8(outputId);
    const std::filesystem::path outputJsonPath =
        outputDirectory / (pathFromUtf8(outputId).wstring() + L".json");
    if (customName) {
        std::error_code existingError;
        if (std::filesystem::is_regular_file(outputJsonPath, existingError) && !existingError) {
            std::string existing;
            if (readFileUtf8(outputJsonPath, existing)) {
                const std::string existingId = json::parse(existing).getString("id");
                if (!iequals(existingId, outputId)) {
                    throw std::runtime_error("版本目录已存在且属于其他版本: " +
                                             pathToUtf8(outputDirectory));
                }
            }
        }
    }
    std::string writeError;
    if (!writeFileAtomic(outputJsonPath, jsonToWrite, writeError)) {
        throw std::runtime_error("保存版本 JSON 失败: " + writeError);
    }

    version.id = outputId;
    summary.versionId = outputId;
    if (customName) {
        Console::println("版本: " + outputId + "（自定义名称，基于 " + sourceId + "）");
    } else {
        Console::println("版本: " + outputId + " (" +
                         (version.type.empty() ? "unknown" : version.type) + ")");
    }
    if (version.javaMajor > 0) {
        Console::println("需要 Java " + std::to_string(version.javaMajor) +
                         (version.javaComponent.empty() ? "" : "（" + version.javaComponent + "）"));
    }

    std::vector<Task> tasks;
    std::set<std::string> taskKeys;

    {
        Task task;
        task.path = outputDirectory / (pathFromUtf8(outputId).wstring() + L".jar");
        task.label = relativeLabel(options_.gameDirectory, task.path);
        task.url = version.client.url.empty()
                       ? (options_.mirror.official
                              ? std::string()
                              : options_.mirror.root + "/version/" + sourceId + "/client")
                       : options_.mirror.mapUrl(version.client.url);
        task.sha1 = version.client.sha1;
        task.size = version.client.size;
        task.hasSize = version.client.hasSize;
        appendTask(tasks, taskKeys, std::move(task));
    }

    for (const DownloadEntry& entry : effectiveLibraryDownloads(version)) {
        if (entry.path.empty()) {
            continue;
        }
        const std::string relative = normalizeRelativePath(entry.path);
        if (!isSafeRelativePath(relative)) {
            throw std::runtime_error("依赖库路径不安全: " + entry.path);
        }
        Task task;
        task.path = safeJoin(options_.gameDirectory / L"libraries", relative);
        task.label = relativeLabel(options_.gameDirectory, task.path);
        task.url = entry.url.empty() ? options_.mirror.libraryUrl(relative)
                                     : options_.mirror.mapUrl(entry.url);
        task.sha1 = entry.sha1;
        task.size = entry.size;
        task.hasSize = entry.hasSize;
        appendTask(tasks, taskKeys, std::move(task));
    }

    if (version.hasLogging && !options_.noLogging) {
        std::string loggingName = version.loggingId;
        if (loggingName.empty()) {
            loggingName = "client-log.xml";
        }
        std::filesystem::path loggingPath = pathFromUtf8(loggingName);
        loggingPath = loggingPath.filename();
        if (loggingPath.empty() || !isSafePathComponent(pathToUtf8(loggingPath))) {
            loggingName = "client-log.xml";
        } else {
            loggingName = pathToUtf8(loggingPath);
        }
        Task task;
        task.path = options_.gameDirectory / L"logging" / pathFromUtf8(loggingName);
        task.label = relativeLabel(options_.gameDirectory, task.path);
        task.url = version.logging.url.empty() ? std::string()
                                               : options_.mirror.mapUrl(version.logging.url);
        task.sha1 = version.logging.sha1;
        task.size = version.logging.size;
        task.hasSize = version.logging.hasSize;
        if (!task.url.empty()) {
            appendTask(tasks, taskKeys, std::move(task));
        }
    }

    AssetIndex assetIndex;
    if (!options_.noAssets) {
        if (!version.hasAssetIndex || version.assetIndex.url.empty()) {
            Console::warning("该版本没有资源索引，跳过资源文件");
        } else {
            std::string indexId = version.assetIndexId.empty() ? version.assets : version.assetIndexId;
            if (!isSafePathComponent(indexId)) {
                throw std::runtime_error("资源索引 id 不安全: " + indexId);
            }
            const std::filesystem::path indexPath =
                options_.gameDirectory / L"assets" / L"indexes" /
                (pathFromUtf8(indexId).wstring() + L".json");
            std::string indexRaw;
            std::string indexError;
            try {
                metadataHttp.get(options_.mirror.mapUrl(version.assetIndex.url), indexRaw, cancel_);
                if (options_.verify && !version.assetIndex.sha1.empty() &&
                    !iequals(sha1Bytes(indexRaw), version.assetIndex.sha1)) {
                    throw std::runtime_error("资源索引 SHA-1 不匹配");
                }
            } catch (const Cancelled&) {
                throw;
            } catch (const std::exception& error) {
                indexError = error.what();
                std::string local;
                if (readFileUtf8(indexPath, local) &&
                    (!options_.verify || version.assetIndex.sha1.empty() ||
                     iequals(sha1Bytes(local), version.assetIndex.sha1))) {
                    indexRaw = std::move(local);
                    Console::warning("资源索引下载失败，改用本地缓存：" + indexError);
                } else {
                    throw std::runtime_error("下载资源索引失败: " + indexError);
                }
            }
            if (!writeFileAtomic(indexPath, indexRaw, writeError)) {
                throw std::runtime_error("保存资源索引失败: " + writeError);
            }
            assetIndex = parseAssetIndex(indexRaw);

            for (const AssetObject& object : assetIndex.objects) {
                Task task;
                task.path = options_.gameDirectory / L"assets" / L"objects" /
                            utf8ToWide(object.hash.substr(0, 2)) / utf8ToWide(object.hash);
                task.label = relativeLabel(options_.gameDirectory, task.path);
                task.url = options_.mirror.assetUrl(object.hash);
                task.sha1 = object.hash;
                task.size = object.size;
                task.hasSize = object.hasSize;
                appendTask(tasks, taskKeys, std::move(task));
            }
        }
    }

    std::vector<Task> pending;
    pending.reserve(tasks.size());
    uint32_t skipped = 0;
    if (options_.force) {
        pending = std::move(tasks);
    } else {
        if (Console::isTerminal() && !tasks.empty()) {
            Console::clearLine();
        }
        for (size_t i = 0; i < tasks.size(); ++i) {
            if (cancel_ != nullptr && cancel_->load()) {
                throw Cancelled();
            }
            if (!options_.verbose && Console::isTerminal() && tasks.size() >= 200 &&
                i % 100 == 0) {
                Console::print("\r检查本地文件 " + std::to_string(i) + "/" +
                               std::to_string(tasks.size()) + " ...");
            }
            bool matches = false;
            try {
                matches = fileMatches(tasks[i], options_.verify, cancel_);
            } catch (const Cancelled&) {
                throw;
            } catch (const std::exception& error) {
                if (options_.verbose) {
                    Console::clearLine();
                    Console::warning("检查 " + tasks[i].label + " 失败，将重新下载：" +
                                     std::string(error.what()));
                }
            }
            if (matches) {
                ++skipped;
            } else {
                pending.push_back(std::move(tasks[i]));
            }
        }
        if (!options_.verbose && Console::isTerminal() && tasks.size() >= 200) {
            Console::clearLine();
        }
    }

    uint64_t totalBytes = 0;
    uint32_t unknownSizes = 0;
    for (const Task& task : pending) {
        if (task.hasSize) {
            totalBytes += task.size;
        } else {
            ++unknownSizes;
        }
    }
    if (!pending.empty()) {
        Console::println("需要下载 " + std::to_string(pending.size()) + " 个文件，共 " +
                         (unknownSizes > 0 ? "至少 " : "") + formatBytes(totalBytes) +
                         (unknownSizes > 0 ? "（" + std::to_string(unknownSizes) +
                                                " 个文件大小未知）"
                                          : ""));
        summary.skippedFiles = skipped;
        if (Console::isTerminal()) {
            Console::println("开始下载，按 Ctrl+C 可取消。");
        }
    } else {
        Console::success("所有文件已完整，无需下载。");
    }

    std::vector<std::string> errors(pending.size());
    std::atomic<size_t> next{0};
    std::atomic<uint32_t> completed{0};
    std::atomic<uint32_t> failed{0};
    std::atomic<uint64_t> downloaded{0};
    std::atomic_bool fatal{false};
    const auto downloadStarted = std::chrono::steady_clock::now();
    size_t lastLineLength = 0;

    if (!pending.empty()) {
        const uint32_t workerCount =
            std::max(1u, std::min<uint32_t>(static_cast<uint32_t>(options_.threads),
                                            static_cast<uint32_t>(pending.size())));
        std::vector<std::thread> workers;
        workers.reserve(workerCount);
        for (uint32_t worker = 0; worker < workerCount; ++worker) {
            workers.emplace_back([&]() {
                std::unique_ptr<HttpClient> client;
                try {
                    client = std::make_unique<HttpClient>(makeHttpOptions(options_));
                } catch (const std::exception& error) {
                    Console::clearLine();
                    Console::error("初始化下载线程失败: " + std::string(error.what()));
                    fatal.store(true);
                    return;
                }
                for (;;) {
                    if (fatal.load() || (cancel_ != nullptr && cancel_->load())) {
                        return;
                    }
                    const size_t index = next.fetch_add(1);
                    if (index >= pending.size()) {
                        return;
                    }
                    const Task& task = pending[index];
                    if (options_.verbose) {
                        Console::clearLine();
                        Console::println("[下载] " + task.label);
                    }
                    try {
                        client->download(
                            task.url, task.path, task.sha1, cancel_,
                            [&](int64_t delta) { addBytes(downloaded, delta); }, task.size);
                        completed.fetch_add(1, std::memory_order_relaxed);
                    } catch (const Cancelled&) {
                        return;
                    } catch (const std::exception& error) {
                        failed.fetch_add(1, std::memory_order_relaxed);
                        errors[index] = truncateOneLine(error.what(), 500);
                        Console::clearLine();
                        Console::error(task.label + ": " + errors[index]);
                    }
                }
            });
        }

        while (!fatal.load()) {
            if (cancel_ != nullptr && cancel_->load()) {
                break;
            }
            const uint32_t done = completed.load() + failed.load();
            if (done >= pending.size()) {
                break;
            }
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - downloadStarted)
                    .count();
            renderProgress(progressLine(downloaded.load(), totalBytes, completed.load(),
                                        static_cast<uint32_t>(pending.size()), failed.load(),
                                        elapsed),
                           false, lastLineLength);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        for (std::thread& worker : workers) {
            worker.join();
        }
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - downloadStarted)
                .count();
        if (Console::isTerminal()) {
            renderProgress(progressLine(downloaded.load(), totalBytes, completed.load(),
                                        static_cast<uint32_t>(pending.size()), failed.load(),
                                        elapsed),
                           true, lastLineLength);
        }
    }

    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - downloadStarted).count();
    if (fatal.load() && failed.load() == 0) {
        failed.store(1);
    }
    summary.downloadedBytes = downloaded.load();
    summary.downloadedFiles = completed.load();
    summary.failedFiles = failed.load();
    summary.skippedFiles = skipped;
    summary.elapsedSeconds = elapsed;

    if (cancel_ != nullptr && cancel_->load()) {
        summary.cancelled = true;
        Console::warning("下载已取消。已完成的文件会保留，重新运行可继续。");
        return summary;
    }

    if (failed.load() > 0) {
        Console::println("有 " + std::to_string(failed.load()) +
                         " 个文件下载失败，重新运行会跳过已完成的文件。");
        uint32_t shown = 0;
        for (size_t i = 0; i < errors.size() && shown < 10; ++i) {
            if (!errors[i].empty()) {
                Console::println("  - " + pending[i].label + ": " + errors[i]);
                ++shown;
            }
        }
        if (failed.load() > shown) {
            Console::println("  ... 其余失败项已省略");
        }
    }

    copyLegacyAssets(options_.gameDirectory, assetIndex, cancel_);

    summary.okay = failed.load() == 0;
    summary.elapsedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (summary.okay) {
        Console::success("安装完成: " + summary.versionId);
    }
    Console::println("下载 " + std::to_string(summary.downloadedFiles) + " 个文件，共 " +
                     formatBytes(summary.downloadedBytes) + "；跳过 " +
                     std::to_string(summary.skippedFiles) + " 个已存在文件。");
    const double average = summary.elapsedSeconds > 0.001
                               ? static_cast<double>(summary.downloadedBytes) /
                                     summary.elapsedSeconds
                               : 0.0;
    Console::println("总用时 " + formatDuration(summary.elapsedSeconds) + "，平均 " +
                     formatRate(average));
    Console::println("游戏目录: " + pathToUtf8(options_.gameDirectory));
    return summary;
}

}  // namespace mf
