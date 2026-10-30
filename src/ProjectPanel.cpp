#include "ProjectPanel.h"
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <memory>
#include <locale>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace dxs {
namespace {
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
constexpr int TreeId = 7101, ListId = 7102, SearchId = 7103, RefreshId = 7104, ImportId = 7105;
constexpr int NewComponentId = 7106, ChooseEditorId = 7107, AutomaticEditorId = 7108, OpenId = 7109;
constexpr wchar_t WindowClass[] = L"DxStudioProjectPanel";

std::wstring Environment(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (!length) return {};
    std::wstring value(length, L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), length);
    if (!copied || copied >= length) return {};
    value.resize(copied);
    return value;
}

fs::path AutomaticEditor() {
    std::error_code ec;
    for (const auto* version : {L"2022", L"2019"}) {
        for (const auto* variable : {L"ProgramFiles", L"ProgramFiles(x86)"}) {
            const auto base = Environment(variable);
            if (base.empty()) continue;
            for (const auto* edition : {L"Community", L"Professional", L"Enterprise", L"Preview"}) {
                auto path = fs::path(base) / L"Microsoft Visual Studio" / version / edition / L"Common7" / L"IDE" / L"devenv.exe";
                if (fs::is_regular_file(path, ec) && !ec) return path;
                ec.clear();
            }
        }
    }
    return {};
}

fs::path EditorSettingsFolder() {
    const auto local = Environment(L"LOCALAPPDATA");
    if (local.empty()) return {};
    return fs::path(local) / L"DxStudio";
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}
std::wstring Text(HWND h) {
    const int size = GetWindowTextLengthW(h);
    std::wstring result(static_cast<size_t>(size) + 1, L'\0');
    GetWindowTextW(h, result.data(), size + 1);
    result.resize(size);
    return result;
}
struct ModalRoot {
    HWND owner{}, main{};
    bool reenable = false;
    explicit ModalRoot(HWND panel) : owner(GetAncestor(panel, GA_ROOT)), main(GetAncestor(panel, GA_ROOTOWNER)) {
        // A detached Project window is owned by the editor. Disable that editor
        // too, so it cannot destroy the panel during a nested modal loop.
        reenable = main && main != owner && IsWindowEnabled(main);
        if (reenable) EnableWindow(main, FALSE);
    }
    ~ModalRoot() { if (reenable && IsWindow(main)) EnableWindow(main, TRUE); }
};
void Error(HWND h, const std::wstring& msg) {
    ModalRoot modal(h);
    MessageBoxW(modal.owner ? modal.owner : h, msg.c_str(), L"Project", MB_OK | MB_ICONERROR);
}

bool Reparse(const fs::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}
bool Within(const fs::path& parent, const fs::path& child) {
    auto p = parent.begin(), c = child.begin();
    for (; p != parent.end(); ++p, ++c)
        if (c == child.end() || _wcsicmp(p->c_str(), c->c_str()) != 0) return false;
    return true;
}
bool AllowedPath(const fs::path& root, const fs::path& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec).lexically_normal();
    if (ec || !Within(root, absolute)) return false;
    fs::path relative = absolute.lexically_relative(root), check = root;
    if (relative.empty() && absolute != root) return false;
    for (const auto& part : relative) {
        if (part == L"." || part.empty()) continue;
        if (part == L"..") return false;
        check /= part;
        if (Reparse(check)) return false;
    }
    fs::path resolved = fs::weakly_canonical(absolute, ec);
    return !ec && Within(root, resolved);
}
struct Entry { fs::path path; bool folder; };
struct Node { fs::path path; };

struct NamePrompt {
    HWND edit{};
    std::wstring value;
    bool accepted = false;
};
LRESULT CALLBACK NamePromptProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* prompt = reinterpret_cast<NamePrompt*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        prompt = static_cast<NamePrompt*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(prompt));
    }
    if (!prompt) return DefWindowProcW(window, message, wp, lp);
    if (message == WM_CREATE) {
        auto instance = reinterpret_cast<CREATESTRUCTW*>(lp)->hInstance;
        auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
            auto control = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return control;
        };
        make(L"STATIC", L"Component name (letters, digits, underscores):", 0, 16, 16, 360, 20, 0);
        prompt->edit = make(L"EDIT", L"NewComponent", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 16, 42, 360, 25, 10);
        SendMessageW(prompt->edit, EM_SETLIMITTEXT, 64, 0);
        make(L"STATIC", L"Creates Scripts/Name.h and Name.cpp. Rebuild to attach.", 0, 16, 78, 360, 32, 0);
        make(L"BUTTON", L"Create", BS_DEFPUSHBUTTON | WS_TABSTOP, 192, 118, 88, 27, IDOK);
        make(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 288, 118, 88, 27, IDCANCEL);
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wp) == IDOK) {
        prompt->value = Text(prompt->edit);
        prompt->accepted = true;
        DestroyWindow(window); return 0;
    }
    if ((message == WM_COMMAND && LOWORD(wp) == IDCANCEL) || message == WM_CLOSE) {
        DestroyWindow(window); return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

bool PromptComponentName(HWND panel, HINSTANCE instance, std::wstring& value) {
    WNDCLASSEXW wc{sizeof(wc)}; wc.hInstance = instance; wc.lpfnWndProc = NamePromptProc;
    wc.lpszClassName = L"DxStudioComponentName"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    HWND owner = GetAncestor(panel, GA_ROOT), main = GetAncestor(panel, GA_ROOTOWNER);
    RECT area{}; GetWindowRect(owner, &area);
    RECT size{0,0,394,162}; AdjustWindowRectEx(&size, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    NamePrompt prompt;
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"New C++ Component", WS_CAPTION | WS_SYSMENU,
        area.left + std::max(0L, (area.right - area.left - (size.right - size.left))/2),
        area.top + std::max(0L, (area.bottom - area.top - (size.bottom - size.top))/2),
        size.right-size.left, size.bottom-size.top, owner, nullptr, instance, &prompt);
    if (!dialog) return false;
    const bool enableOwner = owner && IsWindowEnabled(owner), enableMain = main && main != owner && IsWindowEnabled(main);
    if (enableOwner) EnableWindow(owner, FALSE);
    if (enableMain) EnableWindow(main, FALSE);
    ShowWindow(dialog, SW_SHOW); SetFocus(prompt.edit); SendMessageW(prompt.edit, EM_SETSEL, 0, -1);
    MSG msg{};
    while (IsWindow(dialog)) {
        const auto result = GetMessageW(&msg, nullptr, 0, 0);
        if (result <= 0) { if (result == 0) PostQuitMessage(static_cast<int>(msg.wParam)); break; }
        if (!IsDialogMessageW(dialog, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (IsWindow(dialog)) DestroyWindow(dialog);
    if (enableMain && IsWindow(main)) EnableWindow(main, TRUE);
    if (enableOwner && IsWindow(owner)) { EnableWindow(owner, TRUE); SetActiveWindow(owner); }
    if (prompt.accepted) value = prompt.value;
    return prompt.accepted;
}

struct Panel {
    HWND window{}, title{}, search{}, refresh{}, import{}, breadcrumb{}, tree{}, list{};
    HINSTANCE instance{};
    HFONT font{}, bold{};
    HBRUSH background{}, input{};
    fs::path root, current, scriptEditor, engineRoot;
    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<Entry> entries;
    HTREEITEM currentNode{};

    HWND Make(const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0, DWORD ex = 0) {
        HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
                                 window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return h;
    }
    // Reject reparse points in every component, including a path that was
    // replaced after it was shown in the tree, then check the resolved path.
    bool Allowed(const fs::path& path) const {
        return AllowedPath(root, path);
    }
    std::vector<Entry> Enumerate(const fs::path& folder) const {
        std::vector<Entry> out;
        if (!Allowed(folder)) return out;
        std::error_code ec;
        fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
        while (!ec && it != end && out.size() < 5000) {
            const fs::path path = it->path();
            if (!Reparse(path)) {
                const auto status = it->symlink_status(ec);
                if (!ec && (fs::is_directory(status) || fs::is_regular_file(status)))
                    out.push_back({path, fs::is_directory(status)});
            }
            ec.clear(); it.increment(ec);
        }
        std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
            if (a.folder != b.folder) return a.folder > b.folder;
            return Lower(a.path.filename().wstring()) < Lower(b.path.filename().wstring());
        });
        return out;
    }
    HTREEITEM AddNode(HTREEITEM parent, const fs::path& path, const std::wstring& label) {
        auto node = std::make_unique<Node>(); node->path = path;
        TVINSERTSTRUCTW insert{}; insert.hParent = parent; insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM;
        insert.item.pszText = const_cast<wchar_t*>(label.c_str());
        insert.item.lParam = reinterpret_cast<LPARAM>(node.get());
        HTREEITEM item = TreeView_InsertItem(tree, &insert);
        if (item) nodes.push_back(std::move(node));
        return item;
    }
    void AddSubfolders(HTREEITEM parent, const fs::path& path, unsigned depth) {
        if (depth >= 16 || nodes.size() >= 2000) return;
        for (const auto& e : Enumerate(path)) {
            if (!e.folder || nodes.size() >= 2000) continue;
            HTREEITEM node = AddNode(parent, e.path, e.path.filename().wstring());
            if (!node) continue;
            if (_wcsicmp(e.path.c_str(), current.c_str()) == 0) currentNode = node;
            AddSubfolders(node, e.path, depth + 1);
        }
    }
    void PopulateList() {
        ListView_DeleteAllItems(list); entries.clear();
        if (!Allowed(current)) { SetWindowTextW(breadcrumb, L"Project folder unavailable"); return; }
        std::wstring pathText = L"Project";
        fs::path rel = current.lexically_relative(root);
        if (!rel.empty() && rel != L".") pathText += L" / " + rel.wstring();
        SetWindowTextW(breadcrumb, pathText.c_str());
        const auto filter = Lower(Text(search));
        for (const auto& entry : Enumerate(current)) {
            const std::wstring name = entry.path.filename().wstring();
            if (!filter.empty() && Lower(name).find(filter) == std::wstring::npos) continue;
            const int index = static_cast<int>(entries.size());
            entries.push_back(entry);
            SHFILEINFOW info{};
            const DWORD attr = entry.folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            SHGetFileInfoW(entry.path.c_str(), attr, &info, sizeof(info), SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
            LVITEMW item{}; item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
            item.iItem = index; item.pszText = const_cast<wchar_t*>(name.c_str()); item.iImage = info.iIcon; item.lParam = index;
            ListView_InsertItem(list, &item);
            std::wstring kind = entry.folder ? L"Folder" : entry.path.extension().wstring();
            if (kind.empty()) kind = L"File";
            ListView_SetItemText(list, index, 1, kind.data());
        }
    }
    void Refresh() {
        TreeView_DeleteAllItems(tree); nodes.clear(); currentNode = nullptr;
        if (!Allowed(root)) { PopulateList(); return; }
        HTREEITEM rootItem = AddNode(TVI_ROOT, root, L"Project");
        if (!rootItem) return;
        if (_wcsicmp(root.c_str(), current.c_str()) == 0) currentNode = rootItem;
        AddSubfolders(rootItem, root, 0);
        TreeView_Expand(tree, rootItem, TVE_EXPAND);
        if (!currentNode) { current = root; currentNode = rootItem; }
        TreeView_SelectItem(tree, currentNode);
        TreeView_EnsureVisible(tree, currentNode);
        PopulateList();
    }
    void LoadEditor() {
        const auto folder = EditorSettingsFolder();
        const auto settings = folder / L"script-editor.txt";
        // External-editor preferences belong to this Windows user, not to an
        // imported project that could otherwise nominate an executable.
        if (folder.empty() || Reparse(folder) || !AllowedPath(folder, settings)) return;
        std::error_code ec;
        if (fs::file_size(settings, ec) > 32768 || ec) return;
        std::ifstream in(settings, std::ios::binary);
        std::string path;
        if (!std::getline(in, path) || path.empty()) return;
        try {
            auto candidate = fs::u8path(path);
            if (candidate.is_absolute() && Lower(candidate.extension().wstring()) == L".exe") scriptEditor = candidate;
        } catch (...) { /* An invalid optional setting falls back to automatic. */ }
    }
    bool SaveEditor(const fs::path& path) {
        const auto folder = EditorSettingsFolder();
        std::error_code ec;
        if (!folder.empty()) fs::create_directories(folder, ec);
        if (folder.empty() || ec || Reparse(folder)) { Error(window, L"The editor preferences folder is unavailable."); return false; }
        const auto settings = folder / L"script-editor.txt";
        // OPEN_ALWAYS plus OPEN_REPARSE_POINT lets us inspect the actual entry
        // before truncation, without following an existing symbolic link.
        HANDLE file = CreateFileW(settings.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE) { Error(window, L"Could not save the script editor setting."); return false; }
        BY_HANDLE_FILE_INFORMATION info{};
        bool okay = GetFileInformationByHandle(file, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
        DWORD written{};
        const auto utf8 = path.u8string();
        std::string content(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        content += '\n';
        if (okay) okay = SetEndOfFile(file) && WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr)
            && written == content.size();
        CloseHandle(file);
        if (!okay) { Error(window, L"Could not save the script editor setting. Reparse points are not supported."); return false; }
        scriptEditor = path;
        return true;
    }
    void ChooseEditor() {
        HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const bool uninit = SUCCEEDED(init);
        if (FAILED(init) && init != RPC_E_CHANGED_MODE) { Error(window, L"Could not initialize the file dialog."); return; }
        fs::path chosen;
        {
            ComPtr<IFileOpenDialog> dialog;
            HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
            if (SUCCEEDED(hr)) {
                const COMDLG_FILTERSPEC filter{L"Editor executable (*.exe)", L"*.exe"};
                dialog->SetFileTypes(1, &filter);
                dialog->SetTitle(L"Choose an editor (Visual Studio, VS Code, or another editor)");
                DWORD options{};
                hr = dialog->GetOptions(&options);
                if (SUCCEEDED(hr)) hr = dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
                ModalRoot modal(window);
                if (SUCCEEDED(hr)) hr = dialog->Show(modal.owner ? modal.owner : window);
                if (SUCCEEDED(hr)) {
                    ComPtr<IShellItem> item; PWSTR path = nullptr;
                    hr = dialog->GetResult(&item);
                    if (SUCCEEDED(hr)) hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                    if (SUCCEEDED(hr)) chosen = path;
                    CoTaskMemFree(path);
                }
            }
            if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_CANCELLED)) Error(window, L"Could not select an editor.");
        }
        if (uninit) CoUninitialize();
        if (chosen.empty()) return;
        if (Lower(chosen.extension().wstring()) != L".exe") { Error(window, L"Choose an editor executable (.exe)."); return; }
        SaveEditor(chosen);
    }
    void NewComponent() {
        std::wstring name, error;
        if (!PromptComponentName(window, instance, name)) return;
        if (!CreateProjectComponent(root, name, error)) { Error(window, error); return; }
        if (!GenerateVisualStudioProject(root, engineRoot, error)) { Error(window, error); return; }
        current = root / L"Scripts"; SetWindowTextW(search, L""); Refresh();
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            if (entries[i].path.filename() == name + L".cpp") {
                ListView_SetItemState(list, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(list, i, FALSE); break;
            }
        }
        ModalRoot modal(window);
        MessageBoxW(modal.owner,
            L"Created the header and registered C++ component in project/Scripts.\n\nDouble-click the source to edit it. Run build.cmd and restart the editor to attach the new component.",
            L"Component created", MB_OK | MB_ICONINFORMATION);
    }
    void ContextMenu(HWND origin, POINT screen) {
        if (screen.x == -1 && screen.y == -1) {
            RECT bounds{}; GetWindowRect(origin ? origin : window, &bounds);
            screen = {bounds.left + 24, bounds.top + 24};
        } else if (origin == tree) {
            TVHITTESTINFO hit{}; hit.pt = screen; ScreenToClient(tree, &hit.pt);
            if (auto item = TreeView_HitTest(tree, &hit)) TreeView_SelectItem(tree, item);
        } else if (origin == list) {
            LVHITTESTINFO hit{}; hit.pt = screen; ScreenToClient(list, &hit.pt);
            const int item = ListView_HitTest(list, &hit);
            ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            if (item >= 0) ListView_SetItemState(list, item, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
        HMENU menu = CreatePopupMenu();
        const int selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
        AppendMenuW(menu, MF_STRING | (selected < 0 ? MF_GRAYED : 0), OpenId, L"Open");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, NewComponentId, L"Create > C++ Component...");
        AppendMenuW(menu, MF_STRING, ImportId, L"Import file...");
        AppendMenuW(menu, MF_STRING, RefreshId, L"Refresh");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, ChooseEditorId, L"Choose script editor...");
        AppendMenuW(menu, MF_STRING | (scriptEditor.empty() ? MF_CHECKED : 0), AutomaticEditorId, L"Automatic editor (Visual Studio / default app)");
        const auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, window, nullptr);
        DestroyMenu(menu);
        if (command == OpenId) OpenEntry(selected);
        else if (command == NewComponentId) NewComponent();
        else if (command == ImportId) ImportFile();
        else if (command == RefreshId) Refresh();
        else if (command == ChooseEditorId) ChooseEditor();
        else if (command == AutomaticEditorId) SaveEditor({});
    }
    void OpenEntry(int index) {
        if (index < 0 || static_cast<size_t>(index) >= entries.size()) return;
        const auto entry = entries[static_cast<size_t>(index)];
        if (!Allowed(entry.path)) { Error(window, L"This item is unavailable or outside the project folder."); Refresh(); return; }
        if (entry.folder) { current = entry.path; SetWindowTextW(search, L""); Refresh(); return; }
        const auto ext = Lower(entry.path.extension().wstring());
        const wchar_t* permitted[] = {L".cpp", L".c", L".h", L".hpp", L".hlsl", L".hlsli", L".dxs", L".dxmat", L".txt", L".md", L".json"};
        if (std::find_if(std::begin(permitted), std::end(permitted), [&](const wchar_t* v) { return ext == v; }) == std::end(permitted)) {
            Error(window, L"This file type is not opened by the project browser."); return;
        }
        const bool source = ext == L".cpp" || ext == L".c" || ext == L".h" || ext == L".hpp" || ext == L".hlsl" || ext == L".hlsli";
        auto editor = source ? (scriptEditor.empty() ? AutomaticEditor() : scriptEditor) : fs::path{};
        if (!editor.empty()) {
            std::wstring args;
            if (Lower(editor.filename().wstring()) == L"devenv.exe") {
                std::wstring generationError;
                if (!GenerateVisualStudioProject(root, engineRoot, generationError)) {
                    Error(window, generationError); return;
                }
                args = L"\"" + (root / L"DxStudioProject.sln").wstring() + L"\" /Edit \"" + entry.path.wstring() + L"\"";
            } else args = L"\"" + entry.path.wstring() + L"\"";
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", editor.c_str(), args.c_str(), root.c_str(), SW_SHOWNORMAL)) <= 32)
                Error(window, L"The script editor could not be started. Use Choose script editor or Automatic editor in the Project menu.");
            return;
        }
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", entry.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            Error(window, L"No application could open this file.");
    }
    void ImportFile() {
        if (!Allowed(current)) { Error(window, L"The destination folder is unavailable or outside the project."); return; }
        HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const bool uninit = SUCCEEDED(init);
        if (FAILED(init) && init != RPC_E_CHANGED_MODE) { Error(window, L"Could not initialize the file dialog."); return; }
        fs::path source;
        {
            ComPtr<IFileOpenDialog> dialog;
            HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
            if (SUCCEEDED(hr)) {
                DWORD options{};
                hr = dialog->GetOptions(&options);
                if (SUCCEEDED(hr)) hr = dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
                if (SUCCEEDED(hr)) hr = dialog->SetTitle(L"Import a file into the selected project folder");
                // The panel is a child window. Owning the dialog by that child
                // leaves the editor's top-level window interactive, so closing
                // it could destroy this Panel while Show's modal loop is active.
                // Disable the whole editor until the file picker returns.
                ModalRoot modal(window);
                if (SUCCEEDED(hr)) hr = dialog->Show(modal.owner ? modal.owner : window);
                if (SUCCEEDED(hr)) {
                    ComPtr<IShellItem> item;
                    PWSTR path = nullptr;
                    hr = dialog->GetResult(&item);
                    if (SUCCEEDED(hr)) hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                    if (SUCCEEDED(hr)) source = path;
                    CoTaskMemFree(path);
                }
            }
            if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_CANCELLED)) Error(window, L"Could not select a file to import.");
        }
        if (uninit) CoUninitialize();
        if (source.empty()) return;
        std::wstring error;
        if (!ImportProjectFile(root, current, source, error)) { Error(window, error); return; }
        Refresh();
    }
    void Layout() {
        RECT r{}; GetClientRect(window, &r);
        const int w = r.right, h = r.bottom;
        MoveWindow(title, 8, 5, 80, 22, TRUE);
        MoveWindow(import, 92, 3, 76, 24, TRUE);
        MoveWindow(refresh, 172, 3, 76, 24, TRUE);
        MoveWindow(search, 256, 3, std::max(100, w - 264), 24, TRUE);
        MoveWindow(breadcrumb, 8, 31, std::max(1, w - 16), 20, TRUE);
        const int left = std::min(200, std::max(110, w / 4));
        MoveWindow(tree, 8, 53, left, std::max(1, h - 60), TRUE);
        MoveWindow(list, 12 + left, 53, std::max(1, w - left - 20), std::max(1, h - 60), TRUE);
        ListView_SetColumnWidth(list, 0, std::max(120, w - left - 135));
        ListView_SetColumnWidth(list, 1, 100);
    }
};

LRESULT CALLBACK PanelProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    Panel* p = reinterpret_cast<Panel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        p = static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
        p->window = hwnd;
    }
    if (!p) return DefWindowProcW(hwnd, message, wp, lp);
    switch (message) {
    case WM_CREATE: {
        p->font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        p->bold = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        p->background = CreateSolidBrush(RGB(43,45,50)); p->input = CreateSolidBrush(RGB(54,57,63));
        p->title = p->Make(L"STATIC", L"PROJECT", SS_LEFT);
        SendMessageW(p->title, WM_SETFONT, reinterpret_cast<WPARAM>(p->bold), TRUE);
        p->import = p->Make(L"BUTTON", L"Import...", BS_OWNERDRAW | WS_TABSTOP, ImportId);
        p->refresh = p->Make(L"BUTTON", L"Refresh", BS_OWNERDRAW | WS_TABSTOP, RefreshId);
        p->search = p->Make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, SearchId);
        SendMessageW(p->search, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"Search current folder"));
        p->breadcrumb = p->Make(L"STATIC", L"", SS_LEFT);
        p->tree = p->Make(WC_TREEVIEWW, L"", WS_BORDER | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS, TreeId);
        p->list = p->Make(WC_LISTVIEWW, L"", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, ListId);
        TreeView_SetBkColor(p->tree, RGB(54,57,63)); TreeView_SetTextColor(p->tree, RGB(225,228,234));
        ListView_SetBkColor(p->list, RGB(54,57,63)); ListView_SetTextBkColor(p->list, RGB(54,57,63));
        ListView_SetTextColor(p->list, RGB(225,228,234));
        ListView_SetExtendedListViewStyle(p->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        SHFILEINFOW info{};
        auto images = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(L"C:\\", FILE_ATTRIBUTE_DIRECTORY, &info, sizeof(info),
            SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES));
        if (images) ListView_SetImageList(p->list, images, LVSIL_SMALL);
        LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.pszText = const_cast<wchar_t*>(L"Name"); column.cx = 200;
        ListView_InsertColumn(p->list, 0, &column); column.pszText = const_cast<wchar_t*>(L"Type"); column.cx = 100;
        ListView_InsertColumn(p->list, 1, &column);
        p->LoadEditor(); p->Layout(); p->Refresh(); return 0;
    }
    case WM_SIZE: p->Layout(); return 0;
    case WM_CONTEXTMENU:
        p->ContextMenu(reinterpret_cast<HWND>(wp), {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}); return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == SearchId && HIWORD(wp) == EN_CHANGE) p->PopulateList();
        else if (LOWORD(wp) == RefreshId && HIWORD(wp) == BN_CLICKED) p->Refresh();
        else if (LOWORD(wp) == ImportId && HIWORD(wp) == BN_CLICKED) p->ImportFile();
        return 0;
    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lp);
        if (hdr->idFrom == TreeId && hdr->code == TVN_SELCHANGEDW) {
            auto* change = reinterpret_cast<NMTREEVIEWW*>(lp);
            auto* node = reinterpret_cast<Node*>(change->itemNew.lParam);
            if (node && p->Allowed(node->path)) { p->current = node->path; p->currentNode = change->itemNew.hItem; p->PopulateList(); }
        } else if (hdr->idFrom == ListId && hdr->code == NM_DBLCLK) {
            auto* action = reinterpret_cast<NMITEMACTIVATE*>(lp); p->OpenEntry(action->iItem);
        } else if (hdr->idFrom == ListId && hdr->code == LVN_KEYDOWN) {
            auto* key = reinterpret_cast<NMLVKEYDOWN*>(lp);
            if (key->wVKey == VK_RETURN) p->OpenEntry(ListView_GetNextItem(p->list, -1, LVNI_SELECTED));
        }
        return 0;
    }
    case WM_DRAWITEM: {
        const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (draw->CtlID != ImportId && draw->CtlID != RefreshId) break;
        const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
        const bool disabled = (draw->itemState & ODS_DISABLED) != 0;
        HBRUSH fill = CreateSolidBrush(pressed ? RGB(67, 82, 103) : RGB(55, 58, 64));
        HBRUSH border = CreateSolidBrush(RGB(79, 83, 91));
        FillRect(draw->hDC, &draw->rcItem, fill);
        FrameRect(draw->hDC, &draw->rcItem, border);
        DeleteObject(fill); DeleteObject(border);
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, disabled ? RGB(130, 133, 139) : RGB(225, 228, 234));
        const auto oldFont = SelectObject(draw->hDC, p->font);
        RECT textRect = draw->rcItem;
        const wchar_t* label = draw->CtlID == ImportId ? L"Import..." : L"Refresh";
        DrawTextW(draw->hDC, label, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(draw->hDC, oldFont);
        if (draw->itemState & ODS_FOCUS) {
            RECT focus = draw->rcItem; InflateRect(&focus, -3, -3); DrawFocusRect(draw->hDC, &focus);
        }
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wp); SetTextColor(dc, RGB(225,228,234));
        const bool edit = message == WM_CTLCOLOREDIT;
        SetBkColor(dc, edit ? RGB(54,57,63) : RGB(43,45,50));
        return reinterpret_cast<LRESULT>(edit ? p->input : p->background);
    }
    case WM_ERASEBKGND: { RECT r{}; GetClientRect(hwnd, &r); FillRect(reinterpret_cast<HDC>(wp), &r, p->background); return 1; }
    case WM_NCDESTROY:
        DeleteObject(p->font); DeleteObject(p->bold); DeleteObject(p->background); DeleteObject(p->input);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); delete p; return DefWindowProcW(hwnd, message, wp, lp);
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
}

bool GenerateVisualStudioProject(const fs::path& projectRoot, const fs::path& engineRoot,
                                 std::wstring& error) {
    error.clear();
    std::error_code ec;
    const auto project = fs::weakly_canonical(projectRoot, ec);
    if (ec || !fs::is_directory(project, ec)) { error = L"The project folder is unavailable."; return false; }
    auto engine = engineRoot.empty() ? project.parent_path() : fs::weakly_canonical(engineRoot, ec);
    if (ec || !fs::is_regular_file(engine / L"src" / L"Engine.h", ec)) {
        error = L"Engine.h could not be located for Visual Studio navigation."; return false;
    }
    std::vector<fs::path> sources, headers;
    const auto scripts = project / L"Scripts";
    for (fs::directory_iterator it(scripts, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const auto ext = Lower(it->path().extension().wstring());
        if (ext == L".cpp" || ext == L".c") sources.push_back(it->path());
        else if (ext == L".h" || ext == L".hpp") headers.push_back(it->path());
    }
    if (ec) { error = L"The Scripts folder could not be enumerated."; return false; }
    std::sort(sources.begin(), sources.end()); std::sort(headers.begin(), headers.end());
    auto xml = [](std::wstring value) {
        struct Pair { const wchar_t* from; const wchar_t* to; };
        constexpr Pair pairs[] = {{L"&",L"&amp;"},{L"<",L"&lt;"},{L">",L"&gt;"},{L"\"",L"&quot;"}};
        for (const auto& pair : pairs) for (size_t at = 0; (at = value.find(pair.from, at)) != std::wstring::npos; at += wcslen(pair.to)) value.replace(at, wcslen(pair.from), pair.to);
        return value;
    };
    std::wostringstream projectText;
    projectText << LR"(<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations"><ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration></ItemGroup>
  <PropertyGroup Label="Globals"><ProjectGuid>{7B9A71C4-7A86-4AF4-95AC-55388502D811}</ProjectGuid><Keyword>MakeFileProj</Keyword><RootNamespace>DxStudioProject</RootNamespace></PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration"><ConfigurationType>Makefile</ConfigurationType><UseDebugLibraries>true</UseDebugLibraries><PlatformToolset>v143</PlatformToolset></PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <PropertyGroup>
)";
    projectText << L"    <NMakeBuildCommandLine>&quot;" << xml((engine / L"build-debug.cmd").wstring()) << L"&quot; &quot;" << xml(project.wstring()) << L"&quot;</NMakeBuildCommandLine>\n";
    projectText << L"    <NMakeReBuildCommandLine>$(NMakeBuildCommandLine)</NMakeReBuildCommandLine><NMakeCleanCommandLine>echo Clean build outputs from DxStudio</NMakeCleanCommandLine>\n";
    projectText << L"    <NMakeIncludeSearchPath>" << xml((engine/L"src").wstring()) << L";" << xml(scripts.wstring()) << L";$(NMakeIncludeSearchPath)</NMakeIncludeSearchPath>\n";
    projectText << L"    <NMakePreprocessorDefinitions>UNICODE;_UNICODE;NOMINMAX;WIN32_LEAN_AND_MEAN;$(NMakePreprocessorDefinitions)</NMakePreprocessorDefinitions><NMakeForcedIncludes></NMakeForcedIncludes>\n";
    projectText << LR"(  </PropertyGroup>
  <ItemDefinitionGroup><ClCompile><LanguageStandard>stdcpp20</LanguageStandard><AdditionalIncludeDirectories>)" << xml((engine/L"src").wstring()) << L";" << xml(scripts.wstring()) << LR"(;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories></ClCompile></ItemDefinitionGroup>
  <ItemGroup>
)";
    for (const auto& path : sources) projectText << L"    <ClCompile Include=\"" << xml(path.wstring()) << L"\" />\n";
    projectText << L"  </ItemGroup>\n  <ItemGroup>\n";
    for (const auto& path : headers) projectText << L"    <ClInclude Include=\"" << xml(path.wstring()) << L"\" />\n";
    projectText << L"    <ClInclude Include=\"" << xml((engine/L"src"/L"Engine.h").wstring()) << L"\"><Link>Engine API\\Engine.h</Link></ClInclude>\n";
    projectText << L"    <ClInclude Include=\"" << xml((engine/L"src"/L"Renderer.h").wstring()) << L"\"><Link>Engine API\\Renderer.h</Link></ClInclude>\n";
    projectText << LR"(  </ItemGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
)";
    constexpr wchar_t sln[] = LR"(Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 17
VisualStudioVersion = 17.0.31903.59
MinimumVisualStudioVersion = 10.0.40219.1
Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "DxStudioProject", "DxStudioProject.vcxproj", "{7B9A71C4-7A86-4AF4-95AC-55388502D811}"
EndProject
Global
	GlobalSection(SolutionConfigurationPlatforms) = preSolution
		Debug|x64 = Debug|x64
	EndGlobalSection
	GlobalSection(ProjectConfigurationPlatforms) = postSolution
		{7B9A71C4-7A86-4AF4-95AC-55388502D811}.Debug|x64.ActiveCfg = Debug|x64
		{7B9A71C4-7A86-4AF4-95AC-55388502D811}.Debug|x64.Build.0 = Debug|x64
	EndGlobalSection
EndGlobal
)";
    auto write = [&](const fs::path& path, const std::wstring& text) {
        std::wofstream out(path, std::ios::binary | std::ios::trunc); out.imbue(std::locale(""));
        if (!out) return false; out << text; out.close(); return static_cast<bool>(out);
    };
    if (!write(project/L"DxStudioProject.vcxproj", projectText.str()) || !write(project/L"DxStudioProject.sln", sln)) {
        error = L"Visual Studio project files could not be generated."; return false;
    }
    return true;
}

bool CreateProjectComponent(const fs::path& projectRoot, const std::wstring& name, std::wstring& error) {
    error.clear();
    const auto letter = [](wchar_t c) { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z'); };
    if (name.empty() || name.size() > 64 || !letter(name[0]) ||
        !std::all_of(name.begin(), name.end(), [&](wchar_t c) { return letter(c) || (c >= L'0' && c <= L'9') || c == L'_'; })) {
        error = L"Use 1 to 64 letters, digits or underscores, starting with a letter. Do not include an extension."; return false;
    }
    const auto lower = Lower(name);
    if (lower == L"con" || lower == L"prn" || lower == L"aux" || lower == L"nul" ||
        (lower.size() == 4 && (lower.substr(0,3) == L"com" || lower.substr(0,3) == L"lpt") && lower[3] >= L'1' && lower[3] <= L'9')) {
        error = L"That name is reserved by Windows. Choose another component name."; return false;
    }
    std::error_code ec;
    const fs::path root = fs::canonical(projectRoot, ec);
    if (ec || Reparse(projectRoot) || !fs::is_directory(root, ec) || ec) { error = L"The project folder is unavailable."; return false; }
    const auto folder = root / L"Scripts";
    fs::create_directory(folder, ec);
    if (ec || !AllowedPath(root, folder) || !fs::is_directory(folder, ec) || ec) {
        error = L"The Scripts folder is unavailable or uses a reparse point."; return false;
    }
    std::string identifier;
    identifier.reserve(name.size());
    for (wchar_t c : name) identifier.push_back(static_cast<char>(c)); // Validated ASCII above.
    const std::string header = "#pragma once\n#include \"Engine.h\"\n\nclass " + identifier + " final : public dxs::Component {\npublic:\n    void Start(dxs::Entity& entity) override;\n    void Update(dxs::Entity& entity, float deltaSeconds) override;\n};\n";
    const std::string source = "#include \"" + identifier + ".h\"\n\nvoid " + identifier + "::Start(dxs::Entity& entity) {\n    // Called once for each attachment.\n    (void)entity;\n}\n\nvoid " + identifier + "::Update(dxs::Entity& entity, float deltaSeconds) {\n    // Put this component's behavior here.\n    (void)entity;\n    (void)deltaSeconds;\n}\n\nDXS_REGISTER_COMPONENT(" + identifier + ")\n";
    const auto headerPath = folder / (name + L".h"), sourcePath = folder / (name + L".cpp");
    HANDLE files[2]{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
    const fs::path paths[]{headerPath, sourcePath};
    const std::string* contents[]{&header, &source};
    bool success = true;
    DWORD failure = ERROR_SUCCESS;
    for (int i = 0; i < 2; ++i) {
        // No replacement, even if another program creates the name now. DELETE
        // access lets rollback remove only our own newly created file by handle.
        files[i] = CreateFileW(paths[i].c_str(), GENERIC_WRITE | DELETE, FILE_SHARE_READ,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (files[i] == INVALID_HANDLE_VALUE) { success = false; failure = GetLastError(); break; }
    }
    for (int i = 0; success && i < 2; ++i) {
        DWORD written{};
        success = WriteFile(files[i], contents[i]->data(), static_cast<DWORD>(contents[i]->size()), &written, nullptr)
            && written == contents[i]->size() && FlushFileBuffers(files[i]);
        if (!success) failure = GetLastError();
    }
    for (auto file : files) if (file != INVALID_HANDLE_VALUE) {
        if (!success) { FILE_DISPOSITION_INFO discard{TRUE}; SetFileInformationByHandle(file, FileDispositionInfo, &discard, sizeof(discard)); }
        CloseHandle(file);
    }
    if (!success) {
        if (failure == ERROR_FILE_EXISTS || failure == ERROR_ALREADY_EXISTS)
            error = L"A header or source file with that name already exists. Existing files were not changed.";
        else error = L"Could not create the component (Windows error " + std::to_wstring(failure) + L").";
    }
    return success;
}

void ChooseProjectScriptEditor(HWND panel) {
    auto* p = reinterpret_cast<Panel*>(GetWindowLongPtrW(panel, GWLP_USERDATA));
    if (p) p->ChooseEditor();
}
void UseAutomaticProjectScriptEditor(HWND panel) {
    auto* p = reinterpret_cast<Panel*>(GetWindowLongPtrW(panel, GWLP_USERDATA));
    if (p) p->SaveEditor({});
}
void RefreshProjectPanel(HWND panel) {
    auto* p = reinterpret_cast<Panel*>(GetWindowLongPtrW(panel, GWLP_USERDATA));
    if (p) p->Refresh();
}
void NewProjectComponent(HWND panel) {
    auto* p = reinterpret_cast<Panel*>(GetWindowLongPtrW(panel, GWLP_USERDATA));
    if (p) p->NewComponent();
}

bool ImportProjectFile(const fs::path& projectRoot, const fs::path& destinationFolder,
                       const fs::path& source, std::wstring& error) {
    error.clear();
    std::error_code ec;
    const fs::path root = fs::canonical(projectRoot, ec);
    if (ec || !fs::is_directory(root, ec) || ec) {
        error = L"The project folder is unavailable."; return false;
    }
    if (!AllowedPath(root, destinationFolder) || !fs::is_directory(destinationFolder, ec) || ec) {
        error = L"The destination folder is unavailable, outside the project, or uses a reparse point.";
        return false;
    }
    const fs::path filename = source.filename();
    if (filename.empty() || filename == L"." || filename == L"..") {
        error = L"Choose a file to import."; return false;
    }
    // CREATE_NEW semantics of CopyFileW(..., TRUE) prevent replacing existing
    // files, including a file created after the destination was checked.
    if (!CopyFileW(source.c_str(), (destinationFolder / filename).c_str(), TRUE)) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS)
            error = L"A file with that name already exists. Nothing was overwritten.";
        else error = L"Import failed (Windows error " + std::to_wstring(code) + L").";
        return false;
    }
    return true;
}

HWND CreateProjectPanel(HWND parent, HINSTANCE instance, const fs::path& projectRoot, const fs::path& engineRoot) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES};
    if (!InitCommonControlsEx(&controls)) return nullptr;
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = PanelProc; wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = WindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    std::error_code ec;
    fs::path root = fs::weakly_canonical(projectRoot, ec);
    if (ec) root = fs::absolute(projectRoot, ec).lexically_normal();
    if (ec) return nullptr;
    std::wstring generationError;
    GenerateVisualStudioProject(root, engineRoot.empty() ? root.parent_path() : engineRoot, generationError);
    auto panel = std::make_unique<Panel>(); panel->instance = instance; panel->root = root; panel->current = root;
    panel->engineRoot = engineRoot.empty() ? root.parent_path() : engineRoot;
    // The window takes ownership during WM_NCCREATE.
    Panel* raw = panel.release();
    return CreateWindowExW(0, WindowClass, L"Project", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                           0, 0, 1, 1, parent, nullptr, instance, raw);
}
}
