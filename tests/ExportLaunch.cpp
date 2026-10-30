#include "Engine.h"
#include <iostream>

struct WindowProbe { DWORD pid; HWND game{}; bool dialog{}; };
BOOL CALLBACK ProbeWindow(HWND hwnd, LPARAM lparam) {
    auto& probe = *reinterpret_cast<WindowProbe*>(lparam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != probe.pid) return TRUE;
    wchar_t classname[128]{};
    GetClassNameW(hwnd, classname, 128);
    if (wcscmp(classname, L"DxStudioGameWindow") == 0) probe.game = hwnd;
    if (wcscmp(classname, L"#32770") == 0) probe.dialog = true;
    return TRUE;
}

int main() {
    const auto dir = std::filesystem::current_path() / L"build-direct" / (L"launch-test-" + std::to_wstring(GetCurrentProcessId()));
    if (!std::filesystem::create_directory(dir)) { std::cerr << "Could not create isolated export directory\n"; return 1; }
    std::string error;
    dxs::Scene scene;
    int result = 0;
    PROCESS_INFORMATION process{};
    if (!dxs::LoadScene(L"project/scene.dxs", scene, error) || !dxs::ExportGame(std::filesystem::current_path(), dir, scene, error)) {
        std::cerr << error << '\n'; result = 2;
    } else {
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        const auto exe = dir / L"Game.exe";
        if (!CreateProcessW(exe.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &startup, &process)) {
            std::cerr << "Unable to launch export, Windows error " << GetLastError() << '\n'; result = 3;
        } else {
            Sleep(2000);
            WindowProbe probe{process.dwProcessId};
            EnumWindows(ProbeWindow, reinterpret_cast<LPARAM>(&probe));
            DWORD code = 0;
            GetExitCodeProcess(process.hProcess, &code);
            if (code != STILL_ACTIVE || !probe.game || probe.dialog) {
                std::cerr << "Exported game failed to reach its window without an error dialog\n"; result = 4;
            }
            if (probe.game) PostMessageW(probe.game, WM_CLOSE, 0, 0);
            if (WaitForSingleObject(process.hProcess, 5000) == WAIT_TIMEOUT) {
                TerminateProcess(process.hProcess, 1);
                WaitForSingleObject(process.hProcess, 5000);
            }
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
    }
    for (const auto* file : {L"Game.exe", L"scene.dxs", L"Default.vs.cso", L"Default.ps.cso", L"Default9.vs.cso", L"Default9.ps.cso", L"ProjectSettings.dxsettings"}) {
        std::error_code ignored;
        std::filesystem::remove(dir / file, ignored);
    }
    std::error_code ignored;
    std::filesystem::remove(dir, ignored);
    if (!result) std::cout << "Exported source-free game started without an error dialog.\n";
    return result;
}
