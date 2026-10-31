#include "minecraft.h"

#include "common.h"
#include "json.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>

namespace mf {
namespace {

bool isHex(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    for (unsigned char c : text) {
        if (!std::isxdigit(c)) {
            return false;
        }
    }
    return true;
}

std::string lowerHash(std::string value) {
    return toLowerAscii(value);
}

DownloadEntry parseDownloadEntry(const json::Value& value) {
    DownloadEntry entry;
    if (!value.isObject()) {
        return entry;
    }
    entry.url = value.getString("url");
    entry.sha1 = lowerHash(value.getString("sha1"));
    entry.path = normalizeRelativePath(value.getString("path"));
    if (const json::Value* size = value.find("size"); size != nullptr && size->isNumber()) {
        entry.size = size->asUInt64();
        entry.hasSize = true;
    }
    return entry;
}

std::vector<Rule> parseRules(const json::Value& value) {
    std::vector<Rule> rules;
    if (!value.isArray()) {
        return rules;
    }
    for (const json::Value& item : value.asArray()) {
        if (!item.isObject()) {
            continue;
        }
        Rule rule;
        rule.action = toLowerAscii(item.getString("action", "allow"));
        if (const json::Value* os = item.find("os"); os != nullptr && os->isObject()) {
            rule.hasOs = true;
            rule.osName = toLowerAscii(os->getString("name"));
            rule.osArch = toLowerAscii(os->getString("arch"));
        }
        if (const json::Value* features = item.find("features");
            features != nullptr && features->isObject()) {
            for (const auto& feature : features->members()) {
                rule.features.emplace_back(feature.first, feature.second.asBool(false));
            }
        }
        rules.push_back(std::move(rule));
    }
    return rules;
}

std::string expandArch(std::string value) {
    const char* bits = sizeof(void*) == 8 ? "64" : "32";
    value = replaceAll(std::move(value), "${arch}", bits);
    return value;
}

}  // namespace

const VersionSummary* Manifest::findVersion(const std::string& id) const {
    for (const VersionSummary& version : versions) {
        if (version.id == id) {
            return &version;
        }
    }
    for (const VersionSummary& version : versions) {
        if (iequals(version.id, id)) {
            return &version;
        }
    }
    return nullptr;
}

Manifest parseManifest(const std::string& text) {
    const json::Value root = json::parse(text);
    if (!root.isObject()) {
        throw std::runtime_error("版本清单不是 JSON 对象");
    }
    Manifest manifest;
    if (const json::Value* latest = root.find("latest"); latest != nullptr && latest->isObject()) {
        manifest.latestRelease = latest->getString("release");
        manifest.latestSnapshot = latest->getString("snapshot");
    }
    const json::Value* versions = root.find("versions");
    if (versions == nullptr || !versions->isArray()) {
        throw std::runtime_error("版本清单缺少 versions 数组");
    }
    for (const json::Value& item : versions->asArray()) {
        if (!item.isObject()) {
            continue;
        }
        VersionSummary version;
        version.id = item.getString("id");
        version.type = item.getString("type");
        version.url = item.getString("url");
        version.sha1 = lowerHash(item.getString("sha1"));
        version.releaseTime = item.getString("releaseTime");
        version.time = item.getString("time");
        if (version.id.empty() || version.url.empty()) {
            continue;
        }
        manifest.versions.push_back(std::move(version));
    }
    if (manifest.versions.empty()) {
        throw std::runtime_error("版本清单中没有可用版本");
    }
    return manifest;
}

VersionMeta parseVersionMeta(const std::string& text) {
    const json::Value root = json::parse(text);
    if (!root.isObject()) {
        throw std::runtime_error("版本 JSON 不是对象");
    }
    VersionMeta version;
    version.id = root.getString("id");
    version.type = root.getString("type");
    version.mainClass = root.getString("mainClass");
    version.assets = root.getString("assets");
    version.releaseTime = root.getString("releaseTime");
    version.time = root.getString("time");
    version.inheritsFrom = root.getString("inheritsFrom");
    if (version.id.empty()) {
        throw std::runtime_error("版本 JSON 缺少 id");
    }
    if (const json::Value* javaVersion = root.find("javaVersion");
        javaVersion != nullptr && javaVersion->isObject()) {
        version.javaComponent = javaVersion->getString("component");
        version.javaMajor = static_cast<int>(javaVersion->getInt64("majorVersion"));
    }

    if (const json::Value* downloads = root.find("downloads");
        downloads != nullptr && downloads->isObject()) {
        if (const json::Value* client = downloads->find("client");
            client != nullptr && client->isObject()) {
            version.client = parseDownloadEntry(*client);
            version.hasClient = !version.client.url.empty() || !version.client.path.empty();
        }
    }

    if (const json::Value* assetIndex = root.find("assetIndex");
        assetIndex != nullptr && assetIndex->isObject()) {
        version.hasAssetIndex = true;
        version.assetIndexId = assetIndex->getString("id");
        version.assetIndex = parseDownloadEntry(*assetIndex);
        version.assetIndexTotalSize = assetIndex->getUInt64("totalSize");
        if (version.assetIndexId.empty()) {
            version.assetIndexId = version.assets;
        }
    }

    if (const json::Value* logging = root.find("logging");
        logging != nullptr && logging->isObject()) {
        if (const json::Value* client = logging->find("client");
            client != nullptr && client->isObject()) {
            if (const json::Value* file = client->find("file");
                file != nullptr && file->isObject()) {
                version.logging = parseDownloadEntry(*file);
                version.loggingId = file->getString("id");
                version.hasLogging = !version.logging.url.empty();
            }
        }
    }

    if (const json::Value* libraries = root.find("libraries");
        libraries != nullptr && libraries->isArray()) {
        for (const json::Value& item : libraries->asArray()) {
            if (!item.isObject()) {
                continue;
            }
            Library library;
            library.name = item.getString("name");
            if (const json::Value* rules = item.find("rules"); rules != nullptr) {
                library.rules = parseRules(*rules);
            }
            const json::Value* downloads = item.find("downloads");
            const bool hasNatives = item.find("natives") != nullptr;
            if (downloads != nullptr && downloads->isObject()) {
                if (const json::Value* artifact = downloads->find("artifact");
                    artifact != nullptr && artifact->isObject()) {
                    DownloadEntry entry = parseDownloadEntry(*artifact);
                    if (entry.path.empty() && !library.name.empty()) {
                        entry.path = mavenPathFromName(library.name);
                    }
                    if (!entry.url.empty() || !entry.path.empty()) {
                        library.downloads.push_back(std::move(entry));
                    }
                }
                if (const json::Value* classifiers = downloads->find("classifiers");
                    classifiers != nullptr && classifiers->isObject()) {
                    for (const auto& classifier : classifiers->members()) {
                        DownloadEntry entry = parseDownloadEntry(classifier.second);
                        if (entry.path.empty() && !library.name.empty()) {
                            entry.path = mavenPathFromName(library.name, classifier.first);
                        }
                        if (!entry.url.empty() || !entry.path.empty()) {
                            library.classifiers.emplace_back(classifier.first, std::move(entry));
                        }
                    }
                }
            }
            if (library.downloads.empty() && downloads == nullptr && !hasNatives &&
                !library.name.empty()) {
                DownloadEntry entry;
                entry.path = mavenPathFromName(library.name);
                if (!entry.path.empty()) {
                    library.downloads.push_back(std::move(entry));
                }
            }
            if (const json::Value* natives = item.find("natives");
                natives != nullptr && natives->isObject()) {
                for (const auto& native : natives->members()) {
                    if (native.second.isString()) {
                        library.natives.emplace_back(toLowerAscii(native.first),
                                                     expandArch(native.second.asString()));
                    }
                }
            }
            version.libraries.push_back(std::move(library));
        }
    }
    return version;
}

AssetIndex parseAssetIndex(const std::string& text) {
    const json::Value root = json::parse(text);
    if (!root.isObject()) {
        throw std::runtime_error("资源索引不是 JSON 对象");
    }
    AssetIndex index;
    index.virtualFiles = root.getBool("virtual");
    index.mapToResources = root.getBool("map_to_resources");
    index.totalSize = root.getUInt64("totalSize");
    const json::Value* objects = root.find("objects");
    if (objects == nullptr || !objects->isObject()) {
        throw std::runtime_error("资源索引缺少 objects 对象");
    }
    for (const auto& item : objects->members()) {
        const json::Value& value = item.second;
        if (!value.isObject()) {
            continue;
        }
        AssetObject object;
        object.name = normalizeRelativePath(item.first);
        object.hash = lowerHash(value.getString("hash"));
        if (value.find("size") != nullptr) {
            object.size = value.getUInt64("size");
            object.hasSize = true;
        }
        if (object.hash.size() != 40 || !isHex(object.hash)) {
            throw std::runtime_error("资源索引包含无效 SHA-1: " + object.name);
        }
        if (!isSafeRelativePath(object.name)) {
            throw std::runtime_error("资源索引包含不安全路径: " + object.name);
        }
        index.objects.push_back(std::move(object));
    }
    return index;
}

bool ruleMatches(const Rule& rule) {
    if (rule.hasOs) {
        if (!rule.osName.empty() && rule.osName != "windows") {
            return false;
        }
#ifdef _WIN64
        constexpr const char* kArch = "x86_64";
#else
        constexpr const char* kArch = "x86";
#endif
        if (!rule.osArch.empty() && rule.osArch != kArch) {
            return false;
        }
    }
    for (const auto& feature : rule.features) {
        if (feature.second) {
            return false;
        }
    }
    return true;
}

bool libraryAllowed(const Library& library) {
    if (library.rules.empty()) {
        return true;
    }
    bool allowed = false;
    for (const Rule& rule : library.rules) {
        if (!ruleMatches(rule)) {
            continue;
        }
        allowed = rule.action != "disallow";
    }
    return allowed;
}

std::vector<DownloadEntry> effectiveLibraryDownloads(const VersionMeta& version) {
    std::vector<DownloadEntry> result;
    std::set<std::string> seen;
    auto append = [&](DownloadEntry entry) {
        if (entry.path.empty()) {
            return;
        }
        if (!seen.insert(entry.path).second) {
            return;
        }
        result.push_back(std::move(entry));
    };

    for (const Library& library : version.libraries) {
        if (!libraryAllowed(library)) {
            continue;
        }
        for (const DownloadEntry& entry : library.downloads) {
            append(entry);
        }
        for (const auto& native : library.natives) {
            if (native.first != "windows") {
                continue;
            }
            const std::string& classifier = native.second;
            for (const auto& candidate : library.classifiers) {
                if (candidate.first == classifier) {
                    DownloadEntry entry = candidate.second;
                    if (entry.path.empty()) {
                        entry.path = mavenPathFromName(library.name, classifier);
                    }
                    append(std::move(entry));
                    break;
                }
            }
        }
    }
    return result;
}

bool isSafeMinecraftVersionId(const std::string& id) {
    return isSafePathComponent(id) && id.size() <= 128;
}

std::string mavenPathFromName(const std::string& name, const std::string& classifier) {
    const std::string expandedName = expandArch(name);
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= expandedName.size()) {
        size_t end = expandedName.find(':', start);
        if (end == std::string::npos) {
            end = expandedName.size();
        }
        parts.push_back(expandedName.substr(start, end - start));
        if (end == expandedName.size()) {
            break;
        }
        start = end + 1;
    }
    if (parts.size() < 3) {
        return {};
    }
    std::string group = parts[0];
    std::string artifact = parts[1];
    std::string version = parts[2];
    std::string effectiveClassifier = classifier;
    if (effectiveClassifier.empty() && parts.size() >= 4) {
        effectiveClassifier = parts[3];
    }
    std::string extension = "jar";
    const size_t at = version.find('@');
    if (at != std::string::npos) {
        extension = version.substr(at + 1);
        version.resize(at);
    }
    if (group.empty() || artifact.empty() || version.empty()) {
        return {};
    }
    std::replace(group.begin(), group.end(), '.', '/');
    std::string fileName = artifact + "-" + version;
    if (!effectiveClassifier.empty()) {
        fileName += "-" + effectiveClassifier;
    }
    fileName += "." + extension;
    return group + "/" + artifact + "/" + version + "/" + fileName;
}

}  // namespace mf
