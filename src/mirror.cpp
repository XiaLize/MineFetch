#include "mirror.h"

#include "common.h"

#include <algorithm>
#include <array>

namespace mf {
namespace {

std::string stripTrailingSlashes(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

bool replacePrefix(std::string& url, std::string_view prefix, const std::string& replacement) {
    if (!startsWith(url, prefix)) {
        return false;
    }
    url = replacement + url.substr(prefix.size());
    return true;
}

}  // namespace

std::string Mirror::manifestUrl() const {
    if (official) {
        return "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
    }
    return root + "/mc/game/version_manifest_v2.json";
}

std::string Mirror::mapUrl(const std::string& originalUrl) const {
    if (official) {
        std::string result = originalUrl;
        static constexpr std::array<std::pair<const char*, const char*>, 5> kUpgrades = {{
            {"http://resources.download.minecraft.net", "https://resources.download.minecraft.net"},
            {"http://launchermeta.mojang.com", "https://launchermeta.mojang.com"},
            {"http://launcher.mojang.com", "https://launcher.mojang.com"},
            {"http://piston-meta.mojang.com", "https://piston-meta.mojang.com"},
            {"http://piston-data.mojang.com", "https://piston-data.mojang.com"},
        }};
        for (const auto& upgrade : kUpgrades) {
            if (replacePrefix(result, upgrade.first, upgrade.second)) {
                break;
            }
        }
        return result;
    }
    if (startsWith(originalUrl, root + "/")) {
        return originalUrl;
    }
    std::string result = originalUrl;
    static constexpr std::array<const char*, 8> kRootPrefixes = {
        "https://piston-meta.mojang.com",
        "http://piston-meta.mojang.com",
        "https://launchermeta.mojang.com",
        "http://launchermeta.mojang.com",
        "https://launcher.mojang.com",
        "http://launcher.mojang.com",
        "https://piston-data.mojang.com",
        "http://piston-data.mojang.com",
    };
    for (const char* prefix : kRootPrefixes) {
        if (replacePrefix(result, prefix, root)) {
            return result;
        }
    }
    static constexpr std::array<const char*, 2> kAssetPrefixes = {
        "https://resources.download.minecraft.net",
        "http://resources.download.minecraft.net",
    };
    for (const char* prefix : kAssetPrefixes) {
        if (replacePrefix(result, prefix, root + "/assets")) {
            return result;
        }
    }
    static constexpr std::array<const char*, 2> kLibraryPrefixes = {
        "https://libraries.minecraft.net",
        "http://libraries.minecraft.net",
    };
    for (const char* prefix : kLibraryPrefixes) {
        if (replacePrefix(result, prefix, root + "/maven")) {
            return result;
        }
    }
    static constexpr std::array<const char*, 4> kMavenPrefixes = {
        "https://files.minecraftforge.net/maven",
        "http://files.minecraftforge.net/maven",
        "https://maven.minecraftforge.net",
        "http://maven.minecraftforge.net",
    };
    for (const char* prefix : kMavenPrefixes) {
        if (replacePrefix(result, prefix, root + "/maven")) {
            return result;
        }
    }
    if (replacePrefix(result, "https://meta.fabricmc.net", root + "/fabric-meta")) {
        return result;
    }
    if (replacePrefix(result, "http://meta.fabricmc.net", root + "/fabric-meta")) {
        return result;
    }
    return result;
}

std::string Mirror::assetUrl(const std::string& hash) const {
    if (hash.size() < 2) {
        return {};
    }
    if (official) {
        return "https://resources.download.minecraft.net/" + hash.substr(0, 2) + "/" + hash;
    }
    return root + "/assets/" + hash.substr(0, 2) + "/" + hash;
}

std::string Mirror::libraryUrl(const std::string& relativePath) const {
    if (official) {
        return "https://libraries.minecraft.net/" + relativePath;
    }
    return root + "/maven/" + relativePath;
}

Mirror makeMirror(const std::string& name, const std::string& customRoot) {
    if (!customRoot.empty()) {
        std::string root = stripTrailingSlashes(trim(customRoot));
        if (!startsWith(root, "https://") && !startsWith(root, "http://")) {
            throw std::runtime_error("镜像地址必须以 http:// 或 https:// 开头");
        }
        if (root.find_first_of("?#") != std::string::npos) {
            throw std::runtime_error("镜像地址不能包含查询参数或片段");
        }
        return Mirror{"自定义镜像", root, false};
    }
    if (iequals(name, "bmclapi") || name.empty()) {
        return Mirror{"BMCLAPI", "https://bmclapi2.bangbang93.com", false};
    }
    if (iequals(name, "mojang") || iequals(name, "official") || iequals(name, "official-mojang")) {
        return Mirror{"Mojang 官方源", "https://piston-meta.mojang.com", true};
    }
    throw std::runtime_error("未知镜像 '" + name + "'，可选 bmclapi 或 mojang");
}

}  // namespace mf
