#pragma once

#include <string>

namespace mf {

struct Mirror {
    std::string name;
    std::string root;
    bool official = false;

    std::string manifestUrl() const;
    std::string mapUrl(const std::string& originalUrl) const;
    std::string assetUrl(const std::string& hash) const;
    std::string libraryUrl(const std::string& relativePath) const;
};

Mirror makeMirror(const std::string& name, const std::string& customRoot = {});

}  // namespace mf
