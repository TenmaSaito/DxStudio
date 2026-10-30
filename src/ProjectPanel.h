#pragma once
#include <windows.h>
#include <filesystem>
#include <string>

namespace dxs {
// A self-contained child window. Resize with MoveWindow; destroying its parent
// destroys the panel. The browser never leaves projectRoot.
HWND CreateProjectPanel(HWND parent, HINSTANCE instance, const std::filesystem::path& projectRoot,
                        const std::filesystem::path& engineRoot = {});

bool GenerateVisualStudioProject(const std::filesystem::path& projectRoot,
                                 const std::filesystem::path& engineRoot,
                                 std::wstring& error);

// Commands shared by the Project context menu and the editor's Settings menu.
void ChooseProjectScriptEditor(HWND panel);
void UseAutomaticProjectScriptEditor(HWND panel);
void RefreshProjectPanel(HWND panel);
void NewProjectComponent(HWND panel);

// Creates a registered C++ callback and header in project/Scripts. The name
// must be an ASCII identifier; existing files are never replaced.
bool CreateProjectComponent(const std::filesystem::path& projectRoot,
                            const std::wstring& name, std::wstring& error);

// Copy a user-selected file into the project without replacing an existing
// file. Returns false if the destination escapes the project, uses a reparse
// point, or the copy fails. error contains a user-facing explanation.
bool ImportProjectFile(const std::filesystem::path& projectRoot,
                       const std::filesystem::path& destinationFolder,
                       const std::filesystem::path& source,
                       std::wstring& error);
}
