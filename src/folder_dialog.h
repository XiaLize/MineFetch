#pragma once

#include <filesystem>
#include <string>

namespace mf {

// Shows the Windows folder picker. Returns true when the user selected a folder.
// On cancel, returns false with cancelled=true and an empty error.
bool chooseFolder(const std::filesystem::path& initialFolder, const std::wstring& title,
                  std::filesystem::path& selectedFolder, bool& cancelled, std::string& error);

}  // namespace mf
