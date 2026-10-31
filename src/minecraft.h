#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mf {

struct VersionSummary {
    std::string id;
    std::string type;
    std::string url;
    std::string sha1;
    std::string releaseTime;
    std::string time;
};

struct Manifest {
    std::string latestRelease;
    std::string latestSnapshot;
    std::vector<VersionSummary> versions;

    const VersionSummary* findVersion(const std::string& id) const;
};

struct DownloadEntry {
    std::string url;
    std::string sha1;
    std::string path;
    uint64_t size = 0;
    bool hasSize = false;
};

struct Rule {
    std::string action;
    bool hasOs = false;
    std::string osName;
    std::string osArch;
    std::vector<std::pair<std::string, bool>> features;
};

struct Library {
    std::string name;
    std::vector<Rule> rules;
    std::vector<DownloadEntry> downloads;
    std::vector<std::pair<std::string, DownloadEntry>> classifiers;
    std::vector<std::pair<std::string, std::string>> natives;
};

struct VersionMeta {
    std::string id;
    std::string type;
    std::string mainClass;
    std::string assets;
    std::string releaseTime;
    std::string time;
    std::string inheritsFrom;
    std::string javaComponent;
    int javaMajor = 0;

    bool hasAssetIndex = false;
    std::string assetIndexId;
    DownloadEntry assetIndex;
    uint64_t assetIndexTotalSize = 0;

    bool hasClient = false;
    DownloadEntry client;

    bool hasLogging = false;
    std::string loggingId;
    DownloadEntry logging;

    std::vector<Library> libraries;
};

struct AssetObject {
    std::string name;
    std::string hash;
    uint64_t size = 0;
    bool hasSize = false;
};

struct AssetIndex {
    bool virtualFiles = false;
    bool mapToResources = false;
    uint64_t totalSize = 0;
    std::vector<AssetObject> objects;
};

Manifest parseManifest(const std::string& text);
VersionMeta parseVersionMeta(const std::string& text);
AssetIndex parseAssetIndex(const std::string& text);

std::vector<DownloadEntry> effectiveLibraryDownloads(const VersionMeta& version);

bool libraryAllowed(const Library& library);
bool ruleMatches(const Rule& rule);
bool isSafeMinecraftVersionId(const std::string& id);
std::string mavenPathFromName(const std::string& name, const std::string& classifier = {});

}  // namespace mf
