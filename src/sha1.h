#pragma once

#include <windows.h>
#include <bcrypt.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "common.h"

namespace mf {

class Sha1 {
public:
    Sha1();
    ~Sha1();
    Sha1(const Sha1&) = delete;
    Sha1& operator=(const Sha1&) = delete;

    void update(const void* data, size_t size);
    std::string finishHex();

private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<unsigned char> object_;
    bool finished_ = false;
};

std::string sha1Bytes(std::string_view data);
std::string sha1File(const fs::path& path, const std::atomic_bool* cancel = nullptr);

}  // namespace mf
