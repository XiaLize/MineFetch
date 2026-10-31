#pragma once

#include <atomic>
#include <string>

#include "http.h"
#include "minecraft.h"
#include "mirror.h"

namespace mf {

Manifest fetchManifest(HttpClient& http, const Mirror& mirror,
                       const std::atomic_bool* cancel = nullptr);

std::string resolveVersionId(const Manifest& manifest, std::string request);
VersionSummary resolveVersionSummary(const Manifest& manifest, std::string request);

VersionMeta fetchVersionMeta(HttpClient& http, const Mirror& mirror,
                             const VersionSummary& summary,
                             const std::filesystem::path& localPath, bool verify,
                             const std::atomic_bool* cancel, std::string* rawOut = nullptr);

}  // namespace mf
