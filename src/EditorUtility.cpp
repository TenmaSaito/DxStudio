//==================================================================================
//
// エディターの補助関数のソースファイル [EditorUtility.cpp]
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "EditorUtility.h"
#include "EditorLocale.h"
#include "Core/Log.h"

#include <commdlg.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <fstream>
#include <iomanip>

namespace dxs::editor {

//==================================================================================
// --- UTF-8 → UTF-16 の変換処理 ---
//==================================================================================
std::wstring Wide(const std::string& value) {
    if (value.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!n) return L"?";
    std::wstring result(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), n);
    return result;
}

//==================================================================================
// --- UTF-16 → UTF-8 の変換処理 ---
//==================================================================================
std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!n) return {};
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), n, nullptr, nullptr);
    return result;
}

//==================================================================================
// --- ウィンドウの文字列の取得処理 ---
//==================================================================================
std::wstring Text(HWND control) {
    int n = GetWindowTextLengthW(control);
    std::wstring result(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(control, result.data(), n + 1);
    result.resize(n);
    return result;
}

//==================================================================================
// --- エラーの表示処理（メッセージボックス + Console） ---
//==================================================================================
void Error(HWND window, const std::string& rawMessage) {
    const std::string message = TrUtf8(rawMessage); // 表示言語に翻訳（訳が無ければ英語のまま）
    Debug::LogError(message);
    MessageBoxL(window, Wide(message).c_str(), L"DxStudio", MB_ICONERROR | MB_OK);
}

//==================================================================================
// --- 書き出し先のフォルダーの選択処理 ---
//==================================================================================
// シェルのフォルダー選択ダイアログを使うと、実際のフォルダーを確実に選べ、
// キャンセルしたことを書き出しの失敗と区別できる。
bool ChooseExportFolder(HWND owner, const std::filesystem::path& initial, std::filesystem::path& result, std::string& error) {
    error.clear();
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninitialize = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
        error = "Could not initialize the folder picker.";
        return false;
    }

    {
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
        if (SUCCEEDED(hr)) {
            DWORD options{};
            hr = dialog->GetOptions(&options);
            if (SUCCEEDED(hr)) {
                hr = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            }

            if (SUCCEEDED(hr)) {
                dialog->SetTitle(L"Choose a folder for the exported game");
            }

            Microsoft::WRL::ComPtr<IShellItem> start;
            if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&start)))) {
                dialog->SetDefaultFolder(start.Get());
            }

            if (SUCCEEDED(hr)) {
                hr = dialog->Show(owner);
            }

            if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
                hr = S_FALSE;
            } else if (SUCCEEDED(hr)) {
                Microsoft::WRL::ComPtr<IShellItem> item;
                hr = dialog->GetResult(&item);
                PWSTR path = nullptr;
                if (SUCCEEDED(hr)) {
                    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                }

                if (SUCCEEDED(hr)) {
                    result = path;
                }

                CoTaskMemFree(path);
            }
        }

        if (FAILED(hr)) {
            error = "Could not select the export folder (Windows error " + std::to_string(static_cast<unsigned long>(hr)) + ").";
        }
    }

    if (uninitialize) {
        CoUninitialize();
    }

    return !result.empty();
}

//==================================================================================
// --- モデルファイル（X / FBX）の選択処理 ---
//==================================================================================
bool ChooseModelFile(HWND owner, std::filesystem::path& result) {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = owner;
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.lpstrFilter = L"3D models (*.x;*.fbx)\0*.x;*.fbx\0DirectX model (*.x)\0*.x\0FBX model (*.fbx)\0*.fbx\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return false;
    result = path;
    return true;
}

//==================================================================================
// --- プロジェクトファイル（.dxproject）の選択処理 ---
//==================================================================================
bool ChooseProjectDescriptor(HWND owner, bool save, const std::filesystem::path& initial, std::filesystem::path& result,
                             std::string& error) {
    error.clear();
    result.clear();
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninitialize = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
        error = "Could not initialize the project file dialog.";
        return false;
    }

    Microsoft::WRL::ComPtr<IFileDialog> dialog;
    HRESULT hr = save ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))
                      : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(hr)) {
        COMDLG_FILTERSPEC filter[] = {{L"DxStudio project", L"*.dxproject"}};
        dialog->SetFileTypes(1, filter);
        dialog->SetDefaultExtension(L"dxproject");
        dialog->SetTitle(save ? L"Save DxStudio project file" : L"Open DxStudio project");
        Microsoft::WRL::ComPtr<IShellItem> start;
        const auto folder = std::filesystem::is_directory(initial) ? initial : initial.parent_path();
        if (!folder.empty() && SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&start)))) {
            dialog->SetDefaultFolder(start.Get());
        }

        hr = dialog->Show(owner);
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            hr = S_FALSE;
        } else if (SUCCEEDED(hr)) {
            Microsoft::WRL::ComPtr<IShellItem> item;
            PWSTR path = nullptr;
            hr = dialog->GetResult(&item);
            if (SUCCEEDED(hr)) {
                hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
            }

            if (SUCCEEDED(hr)) {
                result = path;
            }

            CoTaskMemFree(path);
        }
    }

    if (FAILED(hr)) {
        error = "Could not select the project file (Windows error " + std::to_string(static_cast<unsigned long>(hr)) + ").";
    }

    if (uninitialize) {
        CoUninitialize();
    }

    return !result.empty();
}

//==================================================================================
// --- プロジェクトファイルの保存処理 ---
//==================================================================================
bool SaveProjectDescriptor(const std::filesystem::path& path, const std::filesystem::path& root, std::string& error) {
    error.clear();
    std::error_code ec;

    // プロジェクトのフォルダーは .dxproject からの相対パスで保存する（フォルダーごと移動しても開ける）
    auto relative = std::filesystem::relative(root, path.parent_path(), ec);
    const auto stored = ec ? root : relative;
    auto temp = path;
    temp += L".tmp";
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    const auto pathU8 = path.u8string(), storedU8 = stored.u8string();
    const std::string pathText(reinterpret_cast<const char*>(pathU8.data()), pathU8.size());
    const std::string storedText(reinterpret_cast<const char*>(storedU8.data()), storedU8.size());
    if (!file) {
        error = "Cannot create project file: " + pathText;
        return false;
    }

    // 書式: DXPROJECT 1 "プロジェクトのフォルダー"（一時ファイルに書いてから置き換える）
    file << "DXPROJECT 1\n" << std::quoted(storedText) << '\n';
    file.close();
    if (!file || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Cannot save project file (Windows error " + std::to_string(GetLastError()) + ").";
        std::filesystem::remove(temp, ec);
        return false;
    }

    return true;
}

//==================================================================================
// --- プロジェクトファイルの読み込み処理 ---
//==================================================================================
bool LoadProjectDescriptor(const std::filesystem::path& path, std::filesystem::path& root, std::string& error) {
    error.clear();
    std::ifstream file(path, std::ios::binary);
    std::string magic, stored;
    unsigned version = 0;
    if (!file || !(file >> magic >> version >> std::quoted(stored)) || magic != "DXPROJECT" || version != 1 || stored.empty()) {
        error = "Invalid DxStudio project file.";
        return false;
    }

    file >> std::ws;
    if (file.peek() != std::char_traits<char>::eof()) {
        error = "Unexpected data in project file.";
        return false;
    }

    auto candidate = std::filesystem::u8path(stored);
    if (candidate.is_relative()) {
        candidate = path.parent_path() / candidate;
    }

    std::error_code ec;
    candidate = std::filesystem::weakly_canonical(candidate, ec);
    if (ec || !std::filesystem::is_directory(candidate, ec) || ec) {
        error = "The project folder does not exist.";
        return false;
    }

    root = candidate;
    return true;
}

} // namespace dxs::editor
