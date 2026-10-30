#pragma once

// String conversion, shell dialogs and .dxproject descriptor helpers used by
// the editor window.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <filesystem>
#include <string>

namespace dxs::editor {

std::wstring Wide(const std::string& value);
std::string Utf8(const std::wstring& value);
// Text of a window (EDIT, STATIC, BUTTON...).
std::wstring Text(HWND hControl);
// Shows an error box and writes the message to the Console.
void Error(HWND hWindow, const std::string& message);

bool ChooseExportFolder(HWND hOwner, const std::filesystem::path& initial, std::filesystem::path& result, std::string& error);
bool ChooseModelFile(HWND hOwner, std::filesystem::path& result);
bool ChooseProjectDescriptor(HWND hOwner, bool bSave, const std::filesystem::path& initial, std::filesystem::path& result,
                             std::string& error);
bool SaveProjectDescriptor(const std::filesystem::path& path, const std::filesystem::path& root, std::string& error);
bool LoadProjectDescriptor(const std::filesystem::path& path, std::filesystem::path& root, std::string& error);

} // namespace dxs::editor
