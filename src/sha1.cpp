#include "sha1.h"

#include <array>
#include <stdexcept>

namespace mf {
namespace {

std::string toHex(const unsigned char* data, size_t size) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string result;
    result.resize(size * 2);
    for (size_t i = 0; i < size; ++i) {
        result[i * 2] = kDigits[data[i] >> 4];
        result[i * 2 + 1] = kDigits[data[i] & 0x0F];
    }
    return result;
}

void checkNtStatus(NTSTATUS status, const char* operation) {
    if (status < 0) {
        throw std::runtime_error(std::string(operation) + " 失败（NTSTATUS 0x" +
                                 std::to_string(static_cast<unsigned long>(status)) + "）");
    }
}

}  // namespace

Sha1::Sha1() {
    checkNtStatus(BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA1_ALGORITHM, nullptr, 0),
                  "初始化 SHA-1");
    DWORD objectSize = 0;
    DWORD written = 0;
    checkNtStatus(BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH,
                                    reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize),
                                    &written, 0),
                  "读取 SHA-1 参数");
    object_.resize(objectSize);
    checkNtStatus(BCryptCreateHash(algorithm_, &hash_, object_.data(), objectSize, nullptr, 0, 0),
                  "创建 SHA-1 上下文");
}

Sha1::~Sha1() {
    if (hash_ != nullptr) {
        BCryptDestroyHash(hash_);
    }
    if (algorithm_ != nullptr) {
        BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
}

void Sha1::update(const void* data, size_t size) {
    if (finished_) {
        throw std::runtime_error("SHA-1 上下文已经结束");
    }
    if (size == 0) {
        return;
    }
    checkNtStatus(BCryptHashData(hash_, static_cast<PUCHAR>(const_cast<void*>(data)),
                                 static_cast<ULONG>(size), 0),
                  "更新 SHA-1");
}

std::string Sha1::finishHex() {
    if (finished_) {
        throw std::runtime_error("SHA-1 上下文已经结束");
    }
    std::array<unsigned char, 20> digest{};
    checkNtStatus(BCryptFinishHash(hash_, digest.data(), static_cast<ULONG>(digest.size()), 0),
                  "计算 SHA-1");
    BCryptDestroyHash(hash_);
    hash_ = nullptr;
    finished_ = true;
    return toHex(digest.data(), digest.size());
}

std::string sha1Bytes(std::string_view data) {
    Sha1 hash;
    hash.update(data.data(), data.size());
    return hash.finishHex();
}

std::string sha1File(const fs::path& path, const std::atomic_bool* cancel) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("无法读取 " + pathToUtf8(path) + ": " +
                                 win32ErrorText(GetLastError()));
    }
    Sha1 hash;
    std::vector<unsigned char> buffer(1 << 20);
    for (;;) {
        if (cancel != nullptr && cancel->load()) {
            CloseHandle(file);
            throw Cancelled();
        }
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            const DWORD error = GetLastError();
            CloseHandle(file);
            throw std::runtime_error("读取 " + pathToUtf8(path) + " 失败: " +
                                     win32ErrorText(error));
        }
        if (read == 0) {
            break;
        }
        hash.update(buffer.data(), read);
    }
    CloseHandle(file);
    return hash.finishHex();
}

}  // namespace mf
