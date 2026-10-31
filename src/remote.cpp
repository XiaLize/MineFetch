#include "remote.h"

#include "common.h"
#include "sha1.h"

#include <system_error>

namespace mf {

Manifest fetchManifest(HttpClient& http, const Mirror& mirror,
                       const std::atomic_bool* cancel) {
    std::string body;
    http.get(mirror.manifestUrl(), body, cancel);
    return parseManifest(body);
}

std::string resolveVersionId(const Manifest& manifest, std::string request) {
    request = trim(request);
    if (request.empty() || iequals(request, "latest") || iequals(request, "release") ||
        iequals(request, "latest-release")) {
        if (manifest.latestRelease.empty()) {
            throw std::runtime_error("版本清单中没有最新正式版信息");
        }
        return manifest.latestRelease;
    }
    if (iequals(request, "snapshot") || iequals(request, "latest-snapshot")) {
        if (manifest.latestSnapshot.empty()) {
            throw std::runtime_error("版本清单中没有最新快照信息");
        }
        return manifest.latestSnapshot;
    }
    const VersionSummary* version = manifest.findVersion(request);
    if (version == nullptr) {
        throw std::runtime_error("找不到版本 '" + request + "'，可先运行 list 查看可用版本");
    }
    return version->id;
}

VersionSummary resolveVersionSummary(const Manifest& manifest, std::string request) {
    const std::string id = resolveVersionId(manifest, std::move(request));
    const VersionSummary* version = manifest.findVersion(id);
    if (version == nullptr) {
        throw std::runtime_error("版本清单状态不一致，找不到 '" + id + "'");
    }
    return *version;
}

VersionMeta fetchVersionMeta(HttpClient& http, const Mirror& mirror,
                             const VersionSummary& summary,
                             const std::filesystem::path& localPath, bool verify,
                             const std::atomic_bool* cancel, std::string* rawOut) {
    std::string raw;
    std::string remoteError;
    if (!summary.url.empty()) {
        try {
            http.get(mirror.mapUrl(summary.url), raw, cancel);
            if (raw.empty()) {
                throw std::runtime_error("服务器返回空响应");
            }
            if (verify && !summary.sha1.empty()) {
                const std::string actual = sha1Bytes(raw);
                if (!iequals(actual, summary.sha1)) {
                    throw std::runtime_error("版本 JSON SHA-1 不匹配（期望 " + summary.sha1 +
                                             "，实际 " + actual + "）");
                }
            }
        } catch (const Cancelled&) {
            throw;
        } catch (const std::exception& error) {
            remoteError = error.what();
            raw.clear();
        }
    }

    if (raw.empty()) {
        std::string local;
        if (!readFileUtf8(localPath, local)) {
            if (!remoteError.empty()) {
                throw std::runtime_error("下载版本 JSON 失败: " + remoteError +
                                         "；本地也没有缓存文件");
            }
            throw std::runtime_error("没有版本 JSON 下载地址，本地也没有缓存文件: " +
                                     pathToUtf8(localPath));
        }
        if (verify && !summary.sha1.empty()) {
            const std::string actual = sha1Bytes(local);
            if (!iequals(actual, summary.sha1)) {
                throw std::runtime_error("本地版本 JSON SHA-1 不匹配，无法安全使用");
            }
        }
        if (!remoteError.empty()) {
            Console::warning("版本 JSON 下载失败，改用本地缓存：" + remoteError);
        } else {
            Console::warning("没有版本清单下载地址，改用本地版本缓存: " +
                             pathToUtf8(localPath));
        }
        raw = std::move(local);
    }

    VersionMeta version = parseVersionMeta(raw);
    if (!summary.id.empty() && !iequals(version.id, summary.id)) {
        throw std::runtime_error("版本 JSON 的 id 与请求不一致（期望 " + summary.id + "，实际 " +
                                 version.id + "）");
    }
    if (rawOut != nullptr) {
        *rawOut = raw;
    }
    return version;
}

}  // namespace mf
