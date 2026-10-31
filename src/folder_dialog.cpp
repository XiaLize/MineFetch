#include "folder_dialog.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cstdio>
#include <system_error>

namespace mf {
namespace {

class ComInitializer {
public:
    ComInitializer() {
        result_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        owns_ = SUCCEEDED(result_);
    }
    ~ComInitializer() {
        if (owns_) {
            CoUninitialize();
        }
    }
    ComInitializer(const ComInitializer&) = delete;
    ComInitializer& operator=(const ComInitializer&) = delete;

    HRESULT result() const { return result_; }
    bool usable() const { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }

private:
    HRESULT result_ = E_FAIL;
    bool owns_ = false;
};

std::string hresultText(HRESULT result) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(result));
    return std::string(buffer);
}

class DialogHandle {
public:
    DialogHandle() = default;
    ~DialogHandle() {
        if (dialog_ != nullptr) {
            dialog_->Release();
        }
    }
    DialogHandle(const DialogHandle&) = delete;
    DialogHandle& operator=(const DialogHandle&) = delete;

    HRESULT create() {
        return CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IFileOpenDialog,
                                reinterpret_cast<void**>(&dialog_));
    }
    IFileOpenDialog* get() const { return dialog_; }

private:
    IFileOpenDialog* dialog_ = nullptr;
};

}  // namespace

bool chooseFolder(const std::filesystem::path& initialFolder, const std::wstring& title,
                  std::filesystem::path& selectedFolder, bool& cancelled, std::string& error) {
    cancelled = false;
    error.clear();
    selectedFolder.clear();

    ComInitializer com;
    if (!com.usable()) {
        error = "无法初始化 Windows 文件对话框: " + hresultText(com.result());
        return false;
    }

    DialogHandle dialog;
    HRESULT result = dialog.create();
    if (FAILED(result)) {
        error = "无法创建文件夹选择窗口: " + hresultText(result);
        return false;
    }

    FILEOPENDIALOGOPTIONS options = 0;
    result = dialog.get()->GetOptions(&options);
    if (SUCCEEDED(result)) {
        result = dialog.get()->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                                          FOS_PATHMUSTEXIST);
    }
    if (SUCCEEDED(result)) {
        result = dialog.get()->SetTitle(title.c_str());
    }
    if (FAILED(result)) {
        error = "无法初始化文件夹选择窗口: " + hresultText(result);
        return false;
    }

    if (!initialFolder.empty()) {
        std::error_code ec;
        if (std::filesystem::is_directory(initialFolder, ec) && !ec) {
            IShellItem* initialItem = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(initialFolder.c_str(), nullptr,
                                                      IID_IShellItem,
                                                      reinterpret_cast<void**>(&initialItem)))) {
                dialog.get()->SetFolder(initialItem);
                initialItem->Release();
            }
        }
    }

    result = dialog.get()->Show(GetConsoleWindow());
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        cancelled = true;
        return false;
    }
    if (FAILED(result)) {
        error = "打开文件夹选择窗口失败: " + hresultText(result);
        return false;
    }

    IShellItem* item = nullptr;
    result = dialog.get()->GetResult(&item);
    if (FAILED(result) || item == nullptr) {
        error = "无法读取选择的文件夹: " + hresultText(result);
        return false;
    }

    PWSTR rawPath = nullptr;
    result = item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
    if (SUCCEEDED(result) && rawPath != nullptr) {
        selectedFolder = std::filesystem::path(rawPath);
        CoTaskMemFree(rawPath);
    }
    item->Release();

    if (selectedFolder.empty()) {
        error = "选择的文件夹没有有效路径";
        return false;
    }
    return true;
}

}  // namespace mf
