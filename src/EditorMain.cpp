#include "Editor.h"
#include <windows.h>
#include <filesystem>
#include <vector>

// Common Controls 6: themed (dark-capable) scrollbars, combo boxes and tooltips.
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    // Prevent Windows from bitmap-scaling the editor. Native ClearType text and
    // the DirectX viewport remain sharp on high-DPI monitors.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::vector<wchar_t> path(32768);
    DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n == path.size()) {
        MessageBoxW(nullptr, L"Cannot determine executable directory.", L"DxStudio", MB_ICONERROR);
        return 1;
    }
    std::filesystem::path projectFile;
    if (commandLine && *commandLine) {
        std::wstring argument(commandLine);
        if (argument.size() >= 2 && argument.front() == L'"' && argument.back() == L'"')
            argument = argument.substr(1, argument.size() - 2);
        projectFile = argument;
    }
    return dxs::RunEditor(instance, std::filesystem::path(std::wstring(path.data(), n)).parent_path(), projectFile);
}
