#include "Editor.h"
#include "Engine.h"
#include "Renderer.h"
#include "ProjectPanel.h"
#include "EditorInteraction.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cwctype>
#include <commctrl.h>
#include <commdlg.h>
#include <fstream>
#include <iomanip>
#include <shobjidl.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace dxs {
namespace {
constexpr int kHierarchy = 100, kAddCube = 101, kAddSprite = 102, kDelete = 103;
constexpr int kApply = 104, kSave = 105, kLoad = 106, kExport = 107, kReloadShader = 108;
constexpr int kMoveMode = 109, kRotateMode = 110, kScaleMode = 111;
constexpr int kComponentList = 112, kComponentChoice = 113, kAddComponent = 114, kRemoveComponent = 115;
constexpr int kPerspective = 120, kOrthographic = 121, kFrameSelection = 122, kChooseScriptEditor = 123, kPlatformInfo = 124, kFrontView = 125;
constexpr int kAutomaticScriptEditor = 126;
constexpr int kOpenProject = 127, kSaveProjectAs = 128, kBuildDebug = 129, kBuildAndroid = 130;
constexpr int kNewProject = 131, kAddEmpty = 132, kAddModel = 133;
constexpr int kComponentFields = 134, kComponentValue = 135, kApplyComponentValue = 136;
constexpr int kMoveComponentUp = 137, kMoveComponentDown = 138, kSplitter = 460;
constexpr int kDuplicate = 139;
constexpr int kPanelShow = 400, kPanelFloat = 410, kPanelMinimize = 420, kPanelHide = 430;
constexpr int kSectionToggle = 440, kResetLayout = 450, kExit = 451;
constexpr int kSaveLayout = 452, kLoadLayout = 453;
constexpr int kFirstEdit = 200;
constexpr int kFieldCount = 15;
const wchar_t* kFieldLabels[kFieldCount] = {
    L"Name", L"", L"Position X", L"Position Y", L"Position Z", L"Rotation X", L"Rotation Y", L"Rotation Z",
    L"Scale X", L"Scale Y", L"Scale Z", L"Color R", L"Color G", L"Color B", L"Color A"
};

std::wstring Wide(const std::string& value) {
    if (value.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!n) return L"?";
    std::wstring result(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), n);
    return result;
}
std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!n) return {};
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), n, nullptr, nullptr);
    return result;
}
std::wstring Text(HWND control) {
    int n = GetWindowTextLengthW(control);
    std::wstring result(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(control, result.data(), n + 1);
    result.resize(n);
    return result;
}
void Error(HWND window, const std::string& message) {
    MessageBoxW(window, Wide(message).c_str(), L"DxStudio", MB_ICONERROR | MB_OK);
}

// The shell folder picker creates a real filesystem directory selection and
// avoids treating a cancelled dialog as a failed export.
bool ChooseExportFolder(HWND owner, const std::filesystem::path& initial,
                        std::filesystem::path& result, std::string& error) {
    error.clear();
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninitialize = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) { error = "Could not initialize the folder picker."; return false; }
    {
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
        if (SUCCEEDED(hr)) {
            DWORD options{};
            hr = dialog->GetOptions(&options);
            if (SUCCEEDED(hr)) hr = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            if (SUCCEEDED(hr)) dialog->SetTitle(L"Choose a folder for the exported game");
            Microsoft::WRL::ComPtr<IShellItem> start;
            if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&start))))
                dialog->SetDefaultFolder(start.Get());
            if (SUCCEEDED(hr)) hr = dialog->Show(owner);
            if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) hr = S_FALSE;
            else if (SUCCEEDED(hr)) {
                Microsoft::WRL::ComPtr<IShellItem> item;
                hr = dialog->GetResult(&item);
                PWSTR path = nullptr;
                if (SUCCEEDED(hr)) hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                if (SUCCEEDED(hr)) result = path;
                CoTaskMemFree(path);
            }
        }
        if (FAILED(hr)) error = "Could not select the export folder (Windows error " + std::to_string(static_cast<unsigned long>(hr)) + ").";
    }
    if (uninitialize) CoUninitialize();
    return !result.empty();
}

bool ChooseModelFile(HWND owner, std::filesystem::path& result) {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner=owner; dialog.lpstrFile=path;
    dialog.nMaxFile=static_cast<DWORD>(std::size(path));
    dialog.lpstrFilter=L"3D models (*.x;*.fbx)\0*.x;*.fbx\0DirectX model (*.x)\0*.x\0FBX model (*.fbx)\0*.fbx\0";
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameW(&dialog)) return false; result=path; return true;
}

bool ChooseProjectDescriptor(HWND owner, bool save, const std::filesystem::path& initial,
                             std::filesystem::path& result, std::string& error) {
    error.clear(); result.clear();
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninitialize = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) { error = "Could not initialize the project file dialog."; return false; }
    Microsoft::WRL::ComPtr<IFileDialog> dialog;
    HRESULT hr = save
        ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))
        : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(hr)) {
        COMDLG_FILTERSPEC filter[] = {{L"DxStudio project", L"*.dxproject"}};
        dialog->SetFileTypes(1, filter); dialog->SetDefaultExtension(L"dxproject");
        dialog->SetTitle(save ? L"Save DxStudio project file" : L"Open DxStudio project");
        Microsoft::WRL::ComPtr<IShellItem> start;
        const auto folder = std::filesystem::is_directory(initial) ? initial : initial.parent_path();
        if (!folder.empty() && SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&start))))
            dialog->SetDefaultFolder(start.Get());
        hr = dialog->Show(owner);
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) hr = S_FALSE;
        else if (SUCCEEDED(hr)) {
            Microsoft::WRL::ComPtr<IShellItem> item; PWSTR path = nullptr;
            hr = dialog->GetResult(&item);
            if (SUCCEEDED(hr)) hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
            if (SUCCEEDED(hr)) result = path;
            CoTaskMemFree(path);
        }
    }
    if (FAILED(hr)) error = "Could not select the project file (Windows error " + std::to_string(static_cast<unsigned long>(hr)) + ").";
    if (uninitialize) CoUninitialize();
    return !result.empty();
}

bool SaveProjectDescriptor(const std::filesystem::path& path, const std::filesystem::path& root, std::string& error) {
    error.clear(); std::error_code ec;
    auto relative = std::filesystem::relative(root, path.parent_path(), ec);
    const auto stored = ec ? root : relative;
    auto temp = path; temp += L".tmp";
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    const auto pathU8 = path.u8string(), storedU8 = stored.u8string();
    const std::string pathText(reinterpret_cast<const char*>(pathU8.data()), pathU8.size());
    const std::string storedText(reinterpret_cast<const char*>(storedU8.data()), storedU8.size());
    if (!file) { error = "Cannot create project file: " + pathText; return false; }
    file << "DXPROJECT 1\n" << std::quoted(storedText) << '\n';
    file.close();
    if (!file || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Cannot save project file (Windows error " + std::to_string(GetLastError()) + ").";
        std::filesystem::remove(temp, ec); return false;
    }
    return true;
}

bool LoadProjectDescriptor(const std::filesystem::path& path, std::filesystem::path& root, std::string& error) {
    error.clear(); std::ifstream file(path, std::ios::binary);
    std::string magic, stored; unsigned version = 0;
    if (!file || !(file >> magic >> version >> std::quoted(stored)) || magic != "DXPROJECT" || version != 1 || stored.empty()) {
        error = "Invalid DxStudio project file."; return false;
    }
    file >> std::ws;
    if (file.peek() != std::char_traits<char>::eof()) { error = "Unexpected data in project file."; return false; }
    auto candidate = std::filesystem::u8path(stored);
    if (candidate.is_relative()) candidate = path.parent_path() / candidate;
    std::error_code ec; candidate = std::filesystem::weakly_canonical(candidate, ec);
    if (ec || !std::filesystem::is_directory(candidate, ec) || ec) { error = "The project folder does not exist."; return false; }
    root = candidate; return true;
}

LRESULT CALLBACK ViewportProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK PanelProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK HierarchySubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT CALLBACK SplitterSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
struct Editor;
struct EditorPanel {
    Editor* editor{};
    int index{};
    HWND window{}, buttons[3]{};
    bool floating = false, hidden = false, collapsed = false;
};
const wchar_t* kPanelNames[] = {L"Hierarchy", L"Scene", L"Inspector", L"Project"};

struct Editor {
    HINSTANCE instance{};
    HWND window{}, viewport{}, hierarchy{}, status{}, projectPanel{};
    HWND splitters[3]{};
    HWND controls[11]{}, gizmoButtons[3]{}, groupLabels[3]{};
    HWND labels[kFieldCount]{}, edits[kFieldCount]{};
    HWND componentList{}, componentChoice{}, componentAdd{}, componentRemove{};
    HWND componentFields{}, componentValue{}, componentValueApply{};
    std::vector<std::pair<std::string, std::string>> inspectedComponentFields;
    EditorPanel panels[4]{};
    HWND projectionButtons[2]{}, rowLabels[3]{}, inspectorHint{};
    HWND materialLabel{}, materialEdit{}, textureLabel{}, textureEdit{};
    bool sectionOpen[3]{true, true, true};
    bool shellReady = false, arranging = false;
    HFONT font{}, titleFont{};
    HBRUSH background{}, inputBackground{};
    std::filesystem::path base, projectRoot, projectFile;
    Scene scene;
    Scene editSceneSnapshot;
    Renderer renderer;
    Camera camera;
    Camera editCameraSnapshot;
    bool rendererReady = false;
    bool sceneProtected = false;
    bool playing = false;
    ULONGLONG playTick = 0;
    int selected = -1;
    std::vector<int> selectedIndices;
    GizmoMode gizmoMode = GizmoMode::Translate;
    enum class Drag { None, Orbit, Pan, Gizmo, Select } drag = Drag::None;
    POINT lastMouse{};
    POINT gizmoStartMouse{};
    int gizmoAxis = -1;
    DrawItem gizmoStartItem{};
    std::vector<std::pair<int, DrawItem>> gizmoStartItems;
    POINT selectionStart{};
    bool selectingRect = false, selectionAdd = false, selectionToggle = false;
    std::vector<int> selectionBeforeDrag;
    ULONGLONG cameraTick = 0;
    POINT rightStart{};
    bool rightUsed = false, orbitPivot = false;
    int hierarchyDrag = -1;
    POINT hierarchyDragStart{};
    bool hierarchyDragging = false;
    int dockLeft = 210, dockRight = 340, dockProject = 210;
    int inspectorScroll = 0;
    enum class LayoutDrag { None, Left, Right, Project } layoutDrag = LayoutDrag::None;

    void TickCamera() {
        const ULONGLONG now = GetTickCount64();
        const float seconds = cameraTick ? static_cast<float>(now - cameraTick) / 1000.0f : 0.0f;
        cameraTick = now;
        if (drag != Drag::Orbit || orbitPivot || GetCapture() != viewport ||
            GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) != window) return;
        auto down = [](int key) { return (GetAsyncKeyState(key) & 0x8000) ? 1.0f : 0.0f; };
        if (FlyCamera(camera, down('W') - down('S'), down('D') - down('A'),
                      down('E') - down('Q'), seconds, down(VK_SHIFT) != 0)) {
            rightUsed = true;
            renderer.SetCamera(camera);
        }
    }

    void SyncHierarchy() {
        SendMessageW(hierarchy, LB_SETSEL, FALSE, -1);
        for (int index : selectedIndices) SendMessageW(hierarchy, LB_SETSEL, TRUE, index);
        if (selected >= 0) SendMessageW(hierarchy, LB_SETCARETINDEX, selected, FALSE);
    }
    void SetSelection(std::vector<int> indices, int active = -1) {
        indices.erase(std::remove_if(indices.begin(), indices.end(), [&](int i) { return i < 0 || static_cast<size_t>(i) >= scene.entities.size(); }), indices.end());
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        selectedIndices = std::move(indices);
        selected = std::find(selectedIndices.begin(), selectedIndices.end(), active) != selectedIndices.end() ? active : (selectedIndices.empty() ? -1 : selectedIndices.back());
        SyncHierarchy();
        Inspect();
    }
    std::vector<int> MergeSelection(const std::vector<int>& hits) const {
        auto result = (selectionAdd || selectionToggle) ? selectionBeforeDrag : std::vector<int>{};
        for (int hit : hits) {
            auto it = std::find(result.begin(), result.end(), hit);
            if (selectionToggle && it != result.end()) result.erase(it);
            else if (it == result.end()) result.push_back(hit);
        }
        return result;
    }
    void RestoreDrag() {
        for (const auto& entry : gizmoStartItems) if (entry.first >= 0 && static_cast<size_t>(entry.first) < scene.entities.size())
            scene.entities[entry.first].draw = LocalDrawFromWorld(scene, entry.first, entry.second);
        Inspect();
    }
    void UpdateGroup(const DrawItem& updated) {
        using namespace DirectX;
        const auto pivot = XMLoadFloat3(&gizmoStartItem.position);
        const XMFLOAT3 dp{updated.position.x-gizmoStartItem.position.x, updated.position.y-gizmoStartItem.position.y, updated.position.z-gizmoStartItem.position.z};
        const XMFLOAT3 dr{updated.rotation.x-gizmoStartItem.rotation.x, updated.rotation.y-gizmoStartItem.rotation.y, updated.rotation.z-gizmoStartItem.rotation.z};
        const XMFLOAT3 factor{std::abs(gizmoStartItem.scale.x)>1e-6f?updated.scale.x/gizmoStartItem.scale.x:1.f,
            std::abs(gizmoStartItem.scale.y)>1e-6f?updated.scale.y/gizmoStartItem.scale.y:1.f,
            std::abs(gizmoStartItem.scale.z)>1e-6f?updated.scale.z/gizmoStartItem.scale.z:1.f};
        const auto orientation = XMMatrixRotationRollPitchYaw(gizmoStartItem.rotation.x,gizmoStartItem.rotation.y,gizmoStartItem.rotation.z);
        const auto scale = XMMatrixTranspose(orientation) * XMMatrixScaling(factor.x,factor.y,factor.z) * orientation;
        const auto rotation = XMMatrixRotationRollPitchYaw(dr.x,dr.y,dr.z);
        for (const auto& entry : gizmoStartItems) {
            if (entry.first == selected) { scene.entities[entry.first].draw = LocalDrawFromWorld(scene, entry.first, updated); continue; }
            auto d = entry.second;
            if (gizmoMode == GizmoMode::Translate) { d.position.x += dp.x; d.position.y += dp.y; d.position.z += dp.z; }
            else {
                const auto relative = XMVectorSubtract(XMLoadFloat3(&d.position), pivot);
                XMStoreFloat3(&d.position, XMVectorAdd(pivot, XMVector3TransformNormal(relative, gizmoMode == GizmoMode::Scale ? scale : rotation)));
                if (gizmoMode == GizmoMode::Rotate) { d.rotation.x += dr.x; d.rotation.y += dr.y; d.rotation.z += dr.z; }
                else { d.scale.x *= factor.x; d.scale.y *= factor.y; d.scale.z *= factor.z; }
            }
            scene.entities[entry.first].draw = LocalDrawFromWorld(scene, entry.first, d);
        }
    }
    void FrameSelection() {
        if (selectedIndices.empty()) return;
        DirectX::XMFLOAT3 min{FLT_MAX,FLT_MAX,FLT_MAX}, max{-FLT_MAX,-FLT_MAX,-FLT_MAX};
        const auto world = ResolveWorldDraws(scene);
        for (int i : selectedIndices) {
            const auto& d = world[i];
            const float extent = std::max({std::abs(d.scale.x),std::abs(d.scale.y),std::abs(d.scale.z)}) * 0.75f;
            min.x=std::min(min.x,d.position.x-extent); min.y=std::min(min.y,d.position.y-extent); min.z=std::min(min.z,d.position.z-extent);
            max.x=std::max(max.x,d.position.x+extent); max.y=std::max(max.y,d.position.y+extent); max.z=std::max(max.z,d.position.z+extent);
        }
        camera.target = {(min.x+max.x)*0.5f,(min.y+max.y)*0.5f,(min.z+max.z)*0.5f};
        camera.distance = std::clamp(std::max({max.x-min.x,max.y-min.y,max.z-min.z})*2.5f,1.0f,80.0f);
        renderer.SetCamera(camera);
        SetStatus(L"Focused selection. W / E / R: Move / Rotate / Scale.");
    }

    HWND Make(const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0, DWORD ex = 0) {
        HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
                                 window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return h;
    }
    void CreatePanels() {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.hInstance = instance; wc.lpfnWndProc = PanelProc;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"DxStudioPanel";
        RegisterClassExW(&wc);
        for (int i = 0; i < 4; ++i) {
            auto& p = panels[i]; p.editor = this; p.index = i;
            p.window = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, kPanelNames[i],
                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | (i==2?WS_VSCROLL:0), 0, 0, 100, 100, window, nullptr, instance, &p);
            const wchar_t* labels[] = {L"Float", L"_", L"x"};
            const int ids[] = {kPanelFloat + i, kPanelMinimize + i, kPanelHide + i};
            for (int b = 0; b < 3; ++b) {
                p.buttons[b] = Make(L"BUTTON", labels[b], BS_OWNERDRAW | WS_TABSTOP, ids[b]);
                SetParent(p.buttons[b], p.window);
            }
        }
        HMENU bar = CreateMenu(), file = CreatePopupMenu(), objects = CreatePopupMenu();
        HMENU build = CreatePopupMenu(), settings = CreatePopupMenu(), windows = CreatePopupMenu();
        AppendMenuW(file, MF_STRING, kSave, L"Save scene\tCtrl+S");
        AppendMenuW(file, MF_STRING, kLoad, L"Reload scene");
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, kNewProject, L"New project...");
        AppendMenuW(file, MF_STRING, kOpenProject, L"Open project...");
        AppendMenuW(file, MF_STRING, kSaveProjectAs, L"Save project file as...");
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, kExit, L"Exit");
        AppendMenuW(objects, MF_STRING, kAddCube, L"Create cube");
        AppendMenuW(objects, MF_STRING, kAddSprite, L"Create world quad");
        AppendMenuW(objects, MF_STRING, kAddEmpty, L"Create empty object");
        AppendMenuW(objects, MF_STRING, kAddModel, L"Place X / FBX model...");
        AppendMenuW(objects, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(objects, MF_STRING, kFrameSelection, L"Frame selection\tF");
        AppendMenuW(objects, MF_STRING, kDuplicate, L"Duplicate\tCtrl+D");
        AppendMenuW(objects, MF_STRING, kDelete, L"Delete selection");
        AppendMenuW(build, MF_STRING, kExport, L"Export Windows game...");
        AppendMenuW(build, MF_STRING, kBuildDebug, L"Play in Editor\tCtrl+P");
        AppendMenuW(build, MF_STRING, kBuildAndroid, L"Build Android Debug APK");
        AppendMenuW(build, MF_STRING, kPlatformInfo, L"Build platforms...");
        AppendMenuW(settings, MF_STRING, kChooseScriptEditor, L"Script editor...");
        AppendMenuW(settings, MF_STRING, kAutomaticScriptEditor, L"Use automatic Visual Studio detection");
        AppendMenuW(settings, MF_STRING, kReloadShader, L"Reload shader");
        AppendMenuW(settings, MF_STRING, kFrontView, L"Front orthographic view");
        for (int i = 0; i < 4; ++i) {
            HMENU submenu = CreatePopupMenu();
            AppendMenuW(submenu, MF_STRING, kPanelShow + i, L"Show / restore");
            AppendMenuW(submenu, MF_STRING, kPanelFloat + i, L"Float / dock");
            AppendMenuW(submenu, MF_STRING, kPanelMinimize + i, L"Minimize / expand");
            AppendMenuW(submenu, MF_STRING, kPanelHide + i, L"Hide");
            AppendMenuW(windows, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), kPanelNames[i]);
        }
        AppendMenuW(windows, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(windows, MF_STRING, kSaveLayout, L"Save current layout");
        AppendMenuW(windows, MF_STRING, kLoadLayout, L"Load saved layout");
        AppendMenuW(windows, MF_STRING, kResetLayout, L"Reset layout");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"File");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(objects), L"GameObject");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(build), L"Build");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(settings), L"Settings");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(windows), L"Window");
        SetMenu(window, bar);
    }
    void ResizeDock(LayoutDrag drag, POINT point) {
        RECT client{}; GetClientRect(window,&client);
        const int width=static_cast<int>(client.right),height=static_cast<int>(client.bottom);
        const int x=static_cast<int>(point.x),y=static_cast<int>(point.y);
        if(drag==LayoutDrag::Left)dockLeft=std::clamp(x,160,std::max(160,width-500));
        else if(drag==LayoutDrag::Right)dockRight=std::clamp(width-x,240,std::max(240,width-400));
        else if(drag==LayoutDrag::Project)dockProject=std::clamp(height-28-y,120,std::max(120,height-220));
        Layout();
    }
    void FloatPanel(int index) {
        auto& p = panels[index];
        RECT old{}; GetWindowRect(p.window, &old);
        p.floating = !p.floating; p.hidden = false; p.collapsed = false;
        if (p.floating) {
            SetParent(p.window, nullptr);
            SetWindowLongPtrW(p.window, GWL_STYLE, WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN);
            SetWindowLongPtrW(p.window, GWL_EXSTYLE, WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT);
            SetWindowLongPtrW(p.window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(window));
            const int width = index == 1 ? 800 : index == 3 ? 700 : index == 2 ? 360 : 300;
            SetWindowPos(p.window, HWND_TOP, old.left, old.top, width, index == 3 ? 350 : 620,
                SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        } else {
            ShowWindow(p.window, SW_RESTORE);
            SetWindowLongPtrW(p.window, GWL_STYLE, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);
            SetWindowLongPtrW(p.window, GWL_EXSTYLE, WS_EX_CONTROLPARENT);
            SetParent(p.window, window);
            SetWindowPos(p.window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
        SetWindowTextW(p.buttons[0], p.floating ? L"Dock" : L"Float");
        Layout();
    }
    bool ShellCommand(int id) {
        if (id == kExit) { SendMessageW(window, WM_CLOSE, 0, 0); return true; }
        if (id >= kSectionToggle && id < kSectionToggle + 3) {
            sectionOpen[id - kSectionToggle] = !sectionOpen[id - kSectionToggle];
            LayoutPanel(2); return true;
        }
        if (id == kResetLayout) {
            for (int i = 0; i < 4; ++i) {
                if (panels[i].floating) FloatPanel(i);
                panels[i].hidden = panels[i].collapsed = false;
            }
            Layout(); return true;
        }
        if(id==kSaveLayout){
            const auto path=projectRoot/L"EditorLayout.dxl";auto temp=path;temp+=L".tmp";
            std::ofstream file(temp,std::ios::binary|std::ios::trunc);
            if(!file){Error(window,"Could not save the workspace layout.");return true;}
            file<<"DXLAYOUT 1 "<<dockLeft<<' '<<dockRight<<' '<<dockProject<<'\n';
            for(const auto& panel:panels)file<<(panel.floating?1:0)<<' '<<(panel.hidden?1:0)<<' '<<(panel.collapsed?1:0)<<'\n';
            file.close();
            if(!file||!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){Error(window,"Could not finalize the workspace layout.");return true;}
            SetStatus(L"Workspace layout saved in the active project.");return true;
        }
        if(id==kLoadLayout){
            const auto path=projectRoot/L"EditorLayout.dxl";std::ifstream file(path,std::ios::binary);std::string magic;unsigned version{};
            int left{},right{},project{},floating[4]{},hidden[4]{},collapsed[4]{};
            if(!file||!(file>>magic>>version>>left>>right>>project)||magic!="DXLAYOUT"||version!=1){Error(window,"No valid saved workspace layout was found.");return true;}
            for(int i=0;i<4;++i)if(!(file>>floating[i]>>hidden[i]>>collapsed[i])||(floating[i]&~1)||(hidden[i]&~1)||(collapsed[i]&~1)){Error(window,"The saved workspace layout is invalid.");return true;}
            dockLeft=left;dockRight=right;dockProject=project;inspectorScroll=0;
            for(int i=0;i<4;++i){if(panels[i].floating!=(floating[i]!=0))FloatPanel(i);panels[i].hidden=hidden[i]!=0;panels[i].collapsed=collapsed[i]!=0;}
            Layout();SetStatus(L"Saved workspace layout restored.");return true;
        }
        int index = -1;
        if (id >= kPanelShow && id < kPanelShow + 4) {
            index = id - kPanelShow;
            auto& p = panels[index]; p.hidden = p.collapsed = false;
            ShowWindow(p.window, p.floating ? SW_RESTORE : SW_SHOW);
        } else if (id >= kPanelFloat && id < kPanelFloat + 4) {
            FloatPanel(id - kPanelFloat); return true;
        } else if (id >= kPanelMinimize && id < kPanelMinimize + 4) {
            index = id - kPanelMinimize; auto& p = panels[index];
            if (p.floating) ShowWindow(p.window, IsIconic(p.window) ? SW_RESTORE : SW_MINIMIZE);
            else p.collapsed = !p.collapsed;
        } else if (id >= kPanelHide && id < kPanelHide + 4) {
            index = id - kPanelHide; panels[index].hidden = true;
        }
        if (index < 0) return false;
        Layout(); return true;
    }
    void ShowObjectMenu(POINT point) {
        if (point.x == -1 && point.y == -1) GetCursorPos(&point);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kAddCube, L"Create 3D / Cube");
        AppendMenuW(menu, MF_STRING, kAddSprite, L"Create 2D / World quad");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        const UINT availability = selectedIndices.empty() ? MF_GRAYED : 0;
        AppendMenuW(menu, MF_STRING | availability, kFrameSelection, L"Frame selection\tF");
        AppendMenuW(menu, MF_STRING | availability, kDuplicate, L"Duplicate\tCtrl+D");
        AppendMenuW(menu, MF_STRING | availability, kDelete, L"Delete selection");
        const int action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
        DestroyMenu(menu);
        if (action) Command(action, BN_CLICKED);
    }
    void ShowComponentMenu(POINT point) {
        if(point.x==-1&&point.y==-1)GetCursorPos(&point);
        if(selected<0||static_cast<size_t>(selected)>=scene.entities.size())return;
        auto& entity=scene.entities[selected];
        const int selectedRow=static_cast<int>(SendMessageW(componentList,LB_GETCURSEL,0,0));
        const int offset=entity.renderer==Entity::RendererKind::None?0:1;
        const int componentIndex=selectedRow-offset;
        const bool custom=componentIndex>=0&&static_cast<size_t>(componentIndex)<entity.components.size();
        HMENU menu=CreatePopupMenu();
        AppendMenuW(menu,MF_STRING|(selectedRow==LB_ERR?MF_GRAYED:0),kRemoveComponent,L"Remove Component");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING|(!custom||componentIndex==0?MF_GRAYED:0),kMoveComponentUp,L"Move Up");
        AppendMenuW(menu,MF_STRING|(!custom||static_cast<size_t>(componentIndex+1)>=entity.components.size()?MF_GRAYED:0),kMoveComponentDown,L"Move Down");
        const int action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);
        DestroyMenu(menu); if(action)Command(action,BN_CLICKED);
    }
    void SetStatus(const wchar_t* text) { SetWindowTextW(status, text); }
    void TogglePlay() {
        if (!playing) {
            if (!Apply()) return;
            editSceneSnapshot = scene;
            editCameraSnapshot = camera;
            playing = true;
            playTick = GetTickCount64();
            SetWindowTextW(controls[9], L"Stop");
            renderer.SetCamera(ActiveGameCamera(scene));
            SetStatus(L"PLAY MODE: project components are running. Press Stop or Ctrl+P to restore the edited scene.");
        } else {
            playing = false;
            scene = std::move(editSceneSnapshot);
            camera = editCameraSnapshot;
            playTick = 0;
            SetWindowTextW(controls[9], L"Play");
            renderer.SetCamera(camera);
            selectedIndices.erase(std::remove_if(selectedIndices.begin(), selectedIndices.end(),
                [&](int index) { return index < 0 || static_cast<size_t>(index) >= scene.entities.size(); }), selectedIndices.end());
            selected = selectedIndices.empty() ? -1 : selectedIndices.back();
            Populate();
            SetStatus(L"Play Mode stopped. Runtime changes were discarded.");
        }
        InvalidateRect(controls[9], nullptr, TRUE);
    }
    bool SwitchProject(const std::filesystem::path& descriptor) {
        if (playing) TogglePlay();
        std::filesystem::path root; std::string error;
        if (!LoadProjectDescriptor(descriptor, root, error)) { Error(window, error); return false; }
        Scene loaded;
        if (!LoadScene(root / L"scene.dxs", loaded, error)) { Error(window, error); return false; }
        std::error_code ec;
        if (!std::filesystem::is_regular_file(root / L"Default.hlsl", ec) || ec) {
            Error(window, "The project does not contain Default.hlsl."); return false;
        }
        renderer.Shutdown(); rendererReady = false;
        if (projectPanel) DestroyWindow(projectPanel);
        projectRoot = root; projectFile = descriptor; scene = std::move(loaded);
        projectPanel = CreateProjectPanel(panels[3].window, instance, projectRoot, base);
        selected = scene.entities.empty() ? -1 : 0;
        selectedIndices = selected < 0 ? std::vector<int>{} : std::vector<int>{selected};
        sceneProtected = false; Populate(); Layout();
        const auto title = L"DxStudio  —  " + projectFile.stem().wstring();
        SetWindowTextW(window, title.c_str());
        if (!InitRenderer()) return false;
        SetStatus(L"Project opened. Project C++ classes require a Debug or Release rebuild.");
        return true;
    }
    bool NewProject() {
        std::filesystem::path requested; std::string error;
        if(!ChooseProjectDescriptor(window,true,base,requested,error)){if(!error.empty())Error(window,error);return false;}
        const auto projectName=requested.stem().wstring();
        if(projectName.empty()||projectName==L"."||projectName==L"..") { Error(window,"Enter a valid project name."); return false; }
        const auto root=requested.parent_path()/projectName;
        const auto descriptor=root/(projectName+L".dxproject");
        std::error_code ec;
        if(std::filesystem::exists(root,ec)) { Error(window,"A folder with that project name already exists. Choose another name."); return false; }
        for(const auto& folder:{root/L"Scripts",root/L"Assets"/L"Models",root/L"Assets"/L"Materials",root/L"Assets"/L"Textures",root/L"Assets"/L"Animations"}) {
            std::filesystem::create_directories(folder,ec);
            if(ec){Error(window,"Could not create the project folders: "+ec.message());return false;}
        }
        std::filesystem::copy_file(base/L"project"/L"Default.hlsl",root/L"Default.hlsl",ec);
        if(ec){Error(window,"Could not copy the default shader: "+ec.message());return false;}
        Material defaultMaterial; defaultMaterial.name="Default";
        if(!SaveMaterial(root/L"Assets"/L"Materials"/L"Default.dxmat",defaultMaterial,error)){Error(window,error);return false;}
        Scene fresh; fresh.projectRoot=root;
        Entity mainCamera; mainCamera.name="Main Camera"; mainCamera.renderer=Entity::RendererKind::None;
        mainCamera.draw.position={0.0f,2.0f,-6.0f}; mainCamera.components={"Camera"};
        mainCamera.componentProperties["Camera"]={{"Active","true"},{"Orthographic","false"},{"Field of View","60"},{"Near Clip","0.1"},{"Far Clip","1000"}};
        fresh.entities.push_back(std::move(mainCamera));
        if(!SaveScene(root/L"scene.dxs",fresh,error)||!SaveProjectDescriptor(descriptor,root,error)){
            Error(window,error);return false;
        }
        std::wstring generationError;
        if(!GenerateVisualStudioProject(root,base,generationError)){Error(window,Utf8(generationError));return false;}
        return SwitchProject(descriptor);
    }
    bool RunBuildScript(const wchar_t* scriptName, const wchar_t* label) {
        const auto script = base / scriptName;
        if (!std::filesystem::is_regular_file(script)) { Error(window, "The build script is missing."); return false; }
        std::wstring command = L"cmd.exe /d /s /c \"\"" + script.wstring() + L"\" \"" + projectRoot.wstring() + L"\"\"";
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
        const auto running = std::wstring(L"Building ") + label + L"...";
        SetStatus(running.c_str());
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                            base.c_str(), &startup, &process)) {
            Error(window, "Could not start the build process."); return false;
        }
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exitCode = 1; GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        if (exitCode != 0) {
            Error(window, "The build failed. Open the build log in the corresponding build folder.");
            SetStatus(L"Build failed."); return false;
        }
        const auto completed = std::wstring(label) + L" build completed.";
        SetStatus(completed.c_str());
        MessageBoxW(window, completed.c_str(), L"Build", MB_OK | MB_ICONINFORMATION);
        return true;
    }
    void Populate() {
        SendMessageW(hierarchy, LB_RESETCONTENT, 0, 0);
        for (size_t index = 0; index < scene.entities.size(); ++index) {
            const auto& e = scene.entities[index];
            int depth = 0, parent = e.parent;
            while (parent >= 0 && static_cast<size_t>(parent) < scene.entities.size() && depth < 16) {
                ++depth; parent = scene.entities[parent].parent;
            }
            std::wstring name(static_cast<size_t>(depth) * 3, L' ');
            if (depth) name += L"↳ ";
            const wchar_t* icon=e.renderer==Entity::RendererKind::None?L"○  ":e.renderer==Entity::RendererKind::Model?L"◇  ":e.renderer==Entity::RendererKind::Image?L"▣  ":e.draw.is2D?L"▣  ":L"◆  ";
            name += std::wstring(icon) + Wide(e.name);
            SendMessageW(hierarchy, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
        selectedIndices.erase(std::remove_if(selectedIndices.begin(), selectedIndices.end(), [&](int i) { return i < 0 || static_cast<size_t>(i) >= scene.entities.size(); }), selectedIndices.end());
        if (std::find(selectedIndices.begin(), selectedIndices.end(), selected) == selectedIndices.end()) selected = selectedIndices.empty() ? -1 : selectedIndices.back();
        SyncHierarchy();
        Inspect();
    }
    void Inspect() {
        bool valid = selected >= 0 && selected < static_cast<int>(scene.entities.size());
        const bool single = valid && selectedIndices.size() == 1;
        for (auto e : edits) if (e) { EnableWindow(e, single); SetWindowTextW(e, L""); }
        for(auto control:{materialEdit,textureEdit}){EnableWindow(control,single);SetWindowTextW(control,L"");}
        EnableWindow(controls[2], valid);
        EnableWindow(controls[3], single);
        EnableWindow(componentList, single);
        EnableWindow(componentChoice, single);
        EnableWindow(componentAdd, single);
        EnableWindow(componentRemove, single);
        EnableWindow(componentFields, single);
        EnableWindow(componentValue, single);
        EnableWindow(componentValueApply, single);
        int componentSelection = static_cast<int>(SendMessageW(componentList, LB_GETCURSEL, 0, 0));
        SendMessageW(componentList, LB_RESETCONTENT, 0, 0);
        SendMessageW(componentFields, LB_RESETCONTENT, 0, 0); inspectedComponentFields.clear();
        SetWindowTextW(componentValue,L"");
        if (!valid) return;
        if (!single) {
            auto label = std::to_wstring(selectedIndices.size()) + L" objects selected";
            SetWindowTextW(edits[0], label.c_str());
            return;
        }
        const auto& entity = scene.entities[selected];
        SetWindowTextW(edits[0], Wide(entity.name).c_str());
        SetWindowTextW(materialEdit,Wide(entity.materialAsset).c_str());
        SetWindowTextW(textureEdit,Wide(entity.textureAsset).c_str());
        if(entity.renderer!=Entity::RendererKind::None){
            const wchar_t* rendererName=entity.renderer==Entity::RendererKind::Polygon?L"PolygonRenderer [IRender]":entity.renderer==Entity::RendererKind::Model?L"ModelRenderer [IRender]":L"Image [IRender]";
            SendMessageW(componentList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(rendererName));
        }
        for (const auto& component : entity.components) {
            const std::wstring name = Wide(component);
            SendMessageW(componentList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
        if (componentSelection >= 0 && componentSelection < static_cast<int>(entity.components.size())+(entity.renderer!=Entity::RendererKind::None?1:0))
            SendMessageW(componentList, LB_SETCURSEL, componentSelection, 0);
        RefreshComponentFields();
        const auto& d = entity.draw;
        const float values[] = { d.position.x, d.position.y, d.position.z,
                                 DirectX::XMConvertToDegrees(d.rotation.x), DirectX::XMConvertToDegrees(d.rotation.y), DirectX::XMConvertToDegrees(d.rotation.z),
                                 d.scale.x, d.scale.y, d.scale.z, d.color.x, d.color.y, d.color.z, d.color.w };
        for (int i = 0; i < 13; ++i) {
            wchar_t buffer[64];
            // Nine significant digits preserve a float when it is reapplied.
            swprintf_s(buffer, L"%.9g", static_cast<double>(values[i]));
            SetWindowTextW(edits[i + 2], buffer);
        }
    }
    void RefreshComponentFields() {
        const int oldField=static_cast<int>(SendMessageW(componentFields,LB_GETCURSEL,0,0));
        const int listIndex=static_cast<int>(SendMessageW(componentList,LB_GETCURSEL,0,0));
        inspectedComponentFields.clear(); SendMessageW(componentFields,LB_RESETCONTENT,0,0);
        if(selected<0||static_cast<size_t>(selected)>=scene.entities.size()||listIndex==LB_ERR)return;
        auto& entity=scene.entities[selected];
        const int offset=entity.renderer==Entity::RendererKind::None?0:1;
        const int componentIndex=listIndex-offset;
        if(componentIndex<0||static_cast<size_t>(componentIndex)>=entity.components.size())return;
        inspectedComponentFields=ComponentEditorValues(entity,static_cast<size_t>(componentIndex));
        for(const auto& [name,value]:inspectedComponentFields) {
            const auto row=Wide(name+" = "+value);
            SendMessageW(componentFields,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str()));
        }
        if(!inspectedComponentFields.empty()) {
            const int chosen=std::clamp(oldField,0,static_cast<int>(inspectedComponentFields.size())-1);
            SendMessageW(componentFields,LB_SETCURSEL,chosen,0);
            if(GetFocus()!=componentValue)SetWindowTextW(componentValue,Wide(inspectedComponentFields[chosen].second).c_str());
        }
    }
    bool Apply() {
        if (selectedIndices.size() != 1 || selected < 0 || selected >= static_cast<int>(scene.entities.size())) return true;
        std::string name = Utf8(Text(edits[0]));
        if (name.empty() || name.find_first_of("\r\n") != std::string::npos) {
            Error(window, "Enter a non-empty, single-line name."); return false;
        }
        float values[13]{};
        for (int i = 0; i < 13; ++i) {
            auto s = Text(edits[i + 2]);
            try {
                size_t consumed = 0;
                values[i] = std::stof(s, &consumed);
                while (consumed < s.size() && iswspace(s[consumed])) ++consumed;
                if (consumed != s.size() || !std::isfinite(values[i])) throw std::invalid_argument("number");
            } catch (...) {
                Error(window, "Enter a finite number for each transform and color field.");
                SetFocus(edits[i + 2]); return false;
            }
        }
        auto& entity = scene.entities[selected];
        entity.name = name;
        auto assetPath=[&](HWND edit,const char* label,std::string& destination){
            const auto value=Utf8(Text(edit)); if(value.empty()){destination.clear();return true;}
            auto relative=std::filesystem::u8path(value).lexically_normal();
            if(relative.is_absolute()||relative.empty()||*relative.begin()==L"..") { Error(window,std::string(label)+" must be a project-relative path.");SetFocus(edit);return false; }
            std::error_code ec; const auto full=std::filesystem::weakly_canonical(projectRoot/relative,ec);
            const auto root=std::filesystem::weakly_canonical(projectRoot,ec); const auto inside=full.lexically_relative(root);
            if(ec||inside.empty()||*inside.begin()==L".."||!std::filesystem::is_regular_file(full,ec)||ec) {
                Error(window,std::string(label)+" must refer to a file inside the active project.");SetFocus(edit);return false;
            }
            const auto normalized=inside.generic_u8string(); destination.assign(reinterpret_cast<const char*>(normalized.data()),normalized.size()); return true;
        };
        if(!assetPath(materialEdit,"Material",entity.materialAsset)||!assetPath(textureEdit,"Texture",entity.textureAsset))return false;
        auto& d = entity.draw;
        d.position = {values[0], values[1], values[2]};
        // Retain the original radians when an unchanged degree field is reapplied.
        if (values[3] != DirectX::XMConvertToDegrees(d.rotation.x)) d.rotation.x = DirectX::XMConvertToRadians(values[3]);
        if (values[4] != DirectX::XMConvertToDegrees(d.rotation.y)) d.rotation.y = DirectX::XMConvertToRadians(values[4]);
        if (values[5] != DirectX::XMConvertToDegrees(d.rotation.z)) d.rotation.z = DirectX::XMConvertToRadians(values[5]);
        d.scale = {values[6], values[7], values[8]};
        d.color = {values[9], values[10], values[11], values[12]};
        Populate();
        SetStatus(L"Changes applied. Save the scene to keep them.");
        return true;
    }
    void Layout() {
        if (!shellReady || arranging || IsIconic(window)) return;
        arranging = true;
        RECT r{}; GetClientRect(window, &r);
        const int w = r.right, h = r.bottom, top = 48, bottom = 28, gap = 5;
        auto docked = [&](int i) { return !panels[i].floating && !panels[i].hidden; };
        const int left = docked(0) ? std::clamp(dockLeft,160,std::max(160,w/2)) : 0;
        const int right = docked(2) ? std::clamp(dockRight,240,std::max(240,w/2)) : 0;
        const int projectHeight = docked(3) ? (panels[3].collapsed ? 28 : std::clamp(dockProject,120,std::max(120,h-top-bottom-140))) : 0;
        const int lower = h-bottom-projectHeight;
        if (docked(0)) MoveWindow(panels[0].window, gap, top, left-gap, panels[0].collapsed ? 28 : std::max(28,lower-top-gap), TRUE);
        if (docked(2)) MoveWindow(panels[2].window, w-right, top, right-gap, panels[2].collapsed ? 28 : std::max(28,h-top-bottom), TRUE);
        if (docked(1)) MoveWindow(panels[1].window, left+gap, top, std::max(1,w-left-right-gap*2), panels[1].collapsed ? 28 : std::max(28,lower-top-gap), TRUE);
        if (docked(3)) MoveWindow(panels[3].window, gap, lower, std::max(1,w-right-gap*2), projectHeight, TRUE);
        const bool showLeft=docked(0)&&docked(1),showRight=docked(2)&&(docked(1)||docked(3)),showProject=docked(3)&&(docked(0)||docked(1));
        for (int i = 0; i < 4; ++i) {
            ShowWindow(panels[i].window, panels[i].hidden ? SW_HIDE : SW_SHOWNA);
            LayoutPanel(i);
        }
        SetWindowPos(splitters[0],HWND_TOP,left-2,top,9,std::max(1,lower-top),SWP_NOACTIVATE);
        SetWindowPos(splitters[1],HWND_TOP,w-right-4,top,9,std::max(1,h-top-bottom),SWP_NOACTIVATE);
        SetWindowPos(splitters[2],HWND_TOP,gap,lower-4,std::max(1,w-right-gap*2),9,SWP_NOACTIVATE);
        ShowWindow(splitters[0],showLeft?SW_SHOWNA:SW_HIDE); ShowWindow(splitters[1],showRight?SW_SHOWNA:SW_HIDE); ShowWindow(splitters[2],showProject?SW_SHOWNA:SW_HIDE);
        int x = 236;
        const int toolbar[] = {4,5,9,10,6,7};
        const int widths[] = {64,64,82,88,82,82};
        for (int i=0;i<6;++i) { MoveWindow(controls[toolbar[i]],x,10,widths[i],28,TRUE); x+=widths[i]+7; }
        ShowWindow(controls[8], SW_HIDE);
        MoveWindow(status, 10, h-23, std::max(1,w-20),20,TRUE);
        InvalidateRect(window, nullptr, TRUE);
        arranging = false;
    }
    void LayoutPanel(int index) {
        if (!shellReady) return;
        auto& p = panels[index];
        if (IsIconic(p.window)) return;
        RECT r{}; GetClientRect(p.window,&r);
        const int w = r.right, h = r.bottom;
        const int closeX=std::max(4,w-27),minimizeX=std::max(4,closeX-25),floatWidth=w>=220?52:24;
        const int floatX=std::max(4,minimizeX-floatWidth-3);
        SetWindowTextW(p.buttons[0],w>=220?(p.floating?L"Dock":L"Float"):L"↗");
        MoveWindow(p.buttons[0],floatX,3,floatWidth,22,TRUE);
        MoveWindow(p.buttons[1],minimizeX,3,22,22,TRUE);
        MoveWindow(p.buttons[2],closeX,3,22,22,TRUE);
        const bool expanded = !p.collapsed;
        const int scrollOffset=index==2?inspectorScroll:0;
        auto place = [&](HWND control,int x,int y,int width,int height,bool visible=true) {
            if (!control) return;
            MoveWindow(control,x,y-scrollOffset,std::max(1,width),height,TRUE);
            ShowWindow(control,expanded && visible ? SW_SHOWNA : SW_HIDE);
        };
        if (index == 0) {
            place(controls[0],7,34,(w-20)/2,25);
            place(controls[1],10+(w-20)/2,34,(w-20)/2,25);
            place(hierarchy,7,64,w-14,std::max(1,h-103));
            place(controls[2],7,h-32,w-14,25);
        } else if (index == 1) {
            for (int i=0;i<3;++i) place(gizmoButtons[i],7+i*72,33,68,25);
            for (int i=0;i<2;++i) place(projectionButtons[i],231+i*72,33,68,25);
            place(viewport,1,64,w-2,std::max(1,h-65));
            if (rendererReady && expanded && !p.hidden && w>2 && h>65) {
                RECT actual{}; GetClientRect(viewport,&actual);
                renderer.Resize(actual.right,actual.bottom);
            }
        } else if (index == 3) {
            place(projectPanel,1,29,w-2,std::max(1,h-30));
        } else {
            place(labels[0],10,39,46,20);
            place(edits[0],59,34,w-69,27);
            place(inspectorHint,10,65,w-20,20);
            int y=91;
            const wchar_t* names[] = {L"Transform",L"Rendering / Color",L"Components"};
            for (int group=0;group<3;++group) {
                std::wstring title=(sectionOpen[group]?L"▼  ":L"▶  ")+std::wstring(names[group]);
                SetWindowTextW(groupLabels[group],title.c_str());
                place(groupLabels[group],6,y,w-12,26); y+=32;
                if (group==0) {
                    for (int row=0;row<3;++row) {
                        place(rowLabels[row],10,y+5,78,20,sectionOpen[group]);
                        const int cell=std::max(45,(w-102)/3);
                        for (int a=0;a<3;++a) {
                            const int field=2+row*3+a, x=91+a*cell;
                            place(labels[field],x,y+5,12,20,sectionOpen[group]);
                            place(edits[field],x+14,y,cell-17,26,sectionOpen[group]);
                        }
                        if (sectionOpen[group]) y+=31;
                    }
                } else if (group==1) {
                    const int cell=(w-20)/4;
                    for (int a=0;a<4;++a) {
                        const int x=10+a*cell;
                        place(labels[11+a],x,y+5,12,20,sectionOpen[group]);
                        place(edits[11+a],x+14,y,cell-18,26,sectionOpen[group]);
                    }
                    if (sectionOpen[group]) y+=32;
                    place(materialLabel,10,y+5,65,20,sectionOpen[group]);
                    place(materialEdit,78,y,w-88,26,sectionOpen[group]);
                    if(sectionOpen[group])y+=31;
                    place(textureLabel,10,y+5,65,20,sectionOpen[group]);
                    place(textureEdit,78,y,w-88,26,sectionOpen[group]);
                    if(sectionOpen[group])y+=31;
                } else {
                    place(componentList,10,y,w-20,76,sectionOpen[group]);
                    place(componentChoice,10,y+82,w-20,180,sectionOpen[group]);
                    place(componentAdd,10,y+114,(w-25)/2,27,sectionOpen[group]);
                    place(componentRemove,15+(w-25)/2,y+114,(w-25)/2,27,sectionOpen[group]);
                    place(componentFields,10,y+147,w-20,72,sectionOpen[group]);
                    place(componentValue,10,y+225,w-105,27,sectionOpen[group]);
                    place(componentValueApply,w-89,y+225,79,27,sectionOpen[group]);
                    if (sectionOpen[group]) y+=260;
                }
                y+=5;
            }
            place(controls[3],10,y,w-20,28);
            const int contentBottom=y+40,visibleHeight=std::max(1,h-29);
            const int maxScroll=std::max(0,contentBottom-visibleHeight);
            const int clamped=std::clamp(inspectorScroll,0,maxScroll);
            if(clamped!=inspectorScroll){inspectorScroll=clamped;LayoutPanel(index);return;}
            SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,contentBottom-1),static_cast<UINT>(visibleHeight),inspectorScroll,0};
            SetScrollInfo(p.window,SB_VERT,&info,TRUE);
        }
        InvalidateRect(p.window,nullptr,TRUE);
    }
    bool InitRenderer() {
        renderer.Shutdown(); rendererReady = false;
        RECT r{}; GetClientRect(viewport, &r);
        std::string error;
        if (!renderer.Initialize(viewport, std::max<LONG>(1, r.right), std::max<LONG>(1, r.bottom),
                                 (projectRoot / L"Default.hlsl").wstring(), error)) {
            Error(window, "Renderer initialization failed: " + error);
            SetStatus(L"Renderer unavailable. Fix the shader and select Reload Shader.");
            return false;
        }
        renderer.SetCamera(camera);
        rendererReady = true; SetStatus(L"RMB + WASD/QE: fly | Alt + RMB: orbit | MMB: pan | Shift + gizmo: snap");
        return true;
    }
    void Draw() {
        TickCamera();
        if (!rendererReady || IsIconic(window) || !IsWindowVisible(viewport) || IsIconic(panels[1].window)) return;
        if (playing) {
            const ULONGLONG now = GetTickCount64();
            const float dt = std::clamp(static_cast<float>(now - playTick) / 1000.0f, 0.0f, 0.1f);
            playTick = now;
            UpdateUserComponents(scene, dt);
            renderer.SetCamera(ActiveGameCamera(scene));
            RefreshComponentFields();
        } else {
            renderer.SetCamera(camera);
        }
        auto items = ResolveWorldDraws(scene);
        std::string error;
        renderer.SetSelection(selectedIndices);
        if (!renderer.Render(items, error, selected, true, gizmoMode, true)) {
            rendererReady = false;
            Error(window, "Rendering failed: " + error);
            SetStatus(L"Rendering stopped. Select Reload Shader to retry.");
        }
    }
    void Command(int id, int notification) {
        if (id == kBuildDebug) { TogglePlay(); return; }
        if (playing && (id == kSave || id == kLoad || id == kOpenProject || id == kSaveProjectAs ||
                        id == kExport || id == kBuildAndroid || id == kReloadShader)) {
            Error(window, "Stop Play Mode before saving, loading, switching, exporting, building, or reloading the shader.");
            return;
        }
        if (id == kPerspective || id == kOrthographic || id == kFrontView) {
            camera.orthographic = id != kPerspective;
            if (id == kFrontView) { camera.yaw = 0; camera.pitch = 0; }
            renderer.SetCamera(camera);
            InvalidateRect(window, nullptr, TRUE);
            for (auto button : projectionButtons) InvalidateRect(button, nullptr, TRUE);
            SetStatus(camera.orthographic ? L"Orthographic camera. Save or export to keep this camera view." :
                L"Perspective camera. Quads and cubes share the same 3D world.");
            return;
        }
        if (id == kFrameSelection) { FrameSelection(); return; }
        if (id == kNewProject) { NewProject(); return; }
        if (id == kChooseScriptEditor) { ChooseProjectScriptEditor(projectPanel); return; }
        if (id == kAutomaticScriptEditor) { UseAutomaticProjectScriptEditor(projectPanel); return; }
        if (id == kOpenProject) {
            std::filesystem::path descriptor; std::string error;
            if (ChooseProjectDescriptor(window, false, projectFile.empty() ? projectRoot : projectFile, descriptor, error))
                SwitchProject(descriptor);
            else if (!error.empty()) Error(window, error);
            return;
        }
        if (id == kSaveProjectAs) {
            std::filesystem::path descriptor; std::string error;
            if (ChooseProjectDescriptor(window, true, projectFile.empty() ? projectRoot : projectFile, descriptor, error)) {
                if (!SaveProjectDescriptor(descriptor, projectRoot, error)) Error(window, error);
                else { projectFile = descriptor; SetStatus(L"Project file saved."); }
            } else if (!error.empty()) Error(window, error);
            return;
        }
        if (id == kBuildAndroid) { RunBuildScript(L"build-android.cmd", L"Android Debug APK"); return; }
        if (id == kPlatformInfo) {
            MessageBoxW(window,
                L"Windows x64: DirectX 12 export and in-editor Play Mode are available.\n\n"
                L"Android: OpenGL ES Debug APK build is available when Android SDK/NDK are installed.\n\n"
                L"Use the Build menu or the toolbar buttons.", L"Build platforms", MB_OK | MB_ICONINFORMATION);
            return;
        }
        if (id == kHierarchy && notification == LBN_SELCHANGE) {
            const int count = static_cast<int>(SendMessageW(hierarchy, LB_GETSELCOUNT, 0, 0));
            std::vector<int> next(count > 0 ? count : 0);
            if (count > 0) SendMessageW(hierarchy, LB_GETSELITEMS, count, reinterpret_cast<LPARAM>(next.data()));
            const int caret = static_cast<int>(SendMessageW(hierarchy, LB_GETCARETINDEX, 0, 0));
            if (!Apply()) { SyncHierarchy(); return; }
            SetSelection(std::move(next), caret);
            return;
        }
        if (id == kComponentList && notification == LBN_SELCHANGE) { RefreshComponentFields(); return; }
        if (id == kComponentFields && notification == LBN_SELCHANGE) {
            const int index=static_cast<int>(SendMessageW(componentFields,LB_GETCURSEL,0,0));
            if(index>=0&&static_cast<size_t>(index)<inspectedComponentFields.size())SetWindowTextW(componentValue,Wide(inspectedComponentFields[index].second).c_str());
            return;
        }
        if (notification != BN_CLICKED) return;
        if(id==kApplyComponentValue) {
            if(selected<0||static_cast<size_t>(selected)>=scene.entities.size())return;
            auto& entity=scene.entities[selected];
            const int listIndex=static_cast<int>(SendMessageW(componentList,LB_GETCURSEL,0,0));
            const int fieldIndex=static_cast<int>(SendMessageW(componentFields,LB_GETCURSEL,0,0));
            const int componentIndex=listIndex-(entity.renderer==Entity::RendererKind::None?0:1);
            if(componentIndex<0||fieldIndex<0||static_cast<size_t>(fieldIndex)>=inspectedComponentFields.size()) { Error(window,"Select a component field first."); return; }
            std::string error;
            if(!SetComponentEditorValue(entity,static_cast<size_t>(componentIndex),inspectedComponentFields[fieldIndex].first,Utf8(Text(componentValue)),error))Error(window,error);
            else { RefreshComponentFields(); SetStatus(L"Component field applied. Values remain live during Play Mode."); }
            return;
        }
        if (id == kAddComponent && selected >= 0 && static_cast<size_t>(selected) < scene.entities.size()) {
            if (!Apply()) return;
            int choice = static_cast<int>(SendMessageW(componentChoice, CB_GETCURSEL, 0, 0));
            if (choice == CB_ERR) { Error(window, "Choose a registered component first."); return; }
            int length = static_cast<int>(SendMessageW(componentChoice, CB_GETLBTEXTLEN, choice, 0));
            std::wstring value(static_cast<size_t>(length) + 1, L'\0');
            SendMessageW(componentChoice, CB_GETLBTEXT, choice, reinterpret_cast<LPARAM>(value.data()));
            value.resize(length);
            auto name = Utf8(value);
            if(name=="PolygonRenderer"||name=="ModelRenderer"||name=="Image"){
                auto& entity=scene.entities[selected];
                entity.renderer=name=="PolygonRenderer"?Entity::RendererKind::Polygon:name=="ModelRenderer"?Entity::RendererKind::Model:Entity::RendererKind::Image;
                if(entity.renderer==Entity::RendererKind::Model&&entity.modelAsset.empty()){Error(window,"Place an X/FBX model first, or assign a model asset through Place X / FBX model.");entity.renderer=Entity::RendererKind::None;return;}
                Inspect();SetStatus(L"IRender component attached at compile-time-known renderer type.");return;
            }
            auto& components = scene.entities[selected].components;
            if (std::find(components.begin(), components.end(), name) != components.end()) { Error(window, "This component is already attached."); return; }
            if (components.size() >= 64) { Error(window, "An object can have up to 64 components."); return; }
            components.push_back(name);
            Inspect();
            SendMessageW(componentList, LB_SETCURSEL, static_cast<WPARAM>(components.size() - 1 + (scene.entities[selected].renderer==Entity::RendererKind::None?0:1)), 0);
            RefreshComponentFields();
            SetStatus(L"Component attached. Save or export to keep it.");
        } else if (id == kRemoveComponent && selected >= 0 && static_cast<size_t>(selected) < scene.entities.size()) {
            int index = static_cast<int>(SendMessageW(componentList, LB_GETCURSEL, 0, 0));
            if (index == LB_ERR) { Error(window, "Select an attached component to remove."); return; }
            if (!Apply()) return;
            auto& components = scene.entities[selected].components;
            auto& entity=scene.entities[selected]; const int rendererOffset=entity.renderer==Entity::RendererKind::None?0:1;
            if(index==0&&rendererOffset){entity.renderer=Entity::RendererKind::None;}
            else {index-=rendererOffset;if(index>=0&&static_cast<size_t>(index)<components.size()){
                DetachComponent(entity,static_cast<size_t>(index));
            }}
            Inspect(); SetStatus(L"Component detached. Its serialized Inspector values were removed.");
        } else if ((id==kMoveComponentUp||id==kMoveComponentDown) && selected>=0 && static_cast<size_t>(selected)<scene.entities.size()) {
            auto& entity=scene.entities[selected]; const int offset=entity.renderer==Entity::RendererKind::None?0:1;
            const int row=static_cast<int>(SendMessageW(componentList,LB_GETCURSEL,0,0)); int index=row-offset;
            const int next=index+(id==kMoveComponentUp?-1:1);
            if(index>=0&&next>=0&&static_cast<size_t>(index)<entity.components.size()&&static_cast<size_t>(next)<entity.components.size()) {
                std::swap(entity.components[index],entity.components[next]); entity.runtimeComponentNames.clear();
                entity.runtimeComponents.clear(); entity.runtimeComponentStarted.clear(); Inspect();
                SendMessageW(componentList,LB_SETCURSEL,next+offset,0); RefreshComponentFields();
                SetStatus(L"Component order changed. Components run in Inspector order.");
            }
        } else if (id >= kMoveMode && id <= kScaleMode) {
            gizmoMode = id == kMoveMode ? GizmoMode::Translate : id == kRotateMode ? GizmoMode::Rotate : GizmoMode::Scale;
            for (auto button : gizmoButtons) InvalidateRect(button, nullptr, TRUE);
            SetStatus(L"Drag a colored axis to transform. Esc cancels a drag.");
        } else if (id == kAddCube || id == kAddSprite || id == kAddEmpty) {
            if (!Apply()) return;
            Entity entity;
            entity.name = id == kAddCube ? "Cube" : id == kAddSprite ? "Sprite" : "Empty";
            entity.draw.is2D = id == kAddSprite;
            entity.renderer = id == kAddEmpty ? Entity::RendererKind::None : Entity::RendererKind::Polygon;
            entity.draw.color = id == kAddCube ? DirectX::XMFLOAT4{0.35f, 0.68f, 1.f, 1.f} : DirectX::XMFLOAT4{1.f, 0.62f, 0.28f, 1.f};
            scene.entities.push_back(entity); selected = static_cast<int>(scene.entities.size()) - 1; selectedIndices = {selected};
            Populate(); SetStatus(L"Object added. Save the scene to keep it.");
        } else if (id == kAddModel) {
            std::filesystem::path source; if(!ChooseModelFile(window,source))return;
            const auto folder=projectRoot/L"Assets"/L"Models"; std::error_code ec; std::filesystem::create_directories(folder,ec);
            std::wstring importError; auto destination=folder/source.filename();
            if(std::filesystem::weakly_canonical(source,ec)!=std::filesystem::weakly_canonical(destination,ec) && !ImportProjectFile(projectRoot,folder,source,importError)) { Error(window,Utf8(importError));return; }
            Entity entity; entity.name=Utf8(source.stem().wstring()); entity.renderer=Entity::RendererKind::Model;
            const auto relative=std::filesystem::relative(destination,projectRoot,ec).generic_u8string();
            entity.modelAsset.assign(reinterpret_cast<const char*>(relative.data()),relative.size());
            scene.entities.push_back(std::move(entity)); selected=static_cast<int>(scene.entities.size())-1;selectedIndices={selected};Populate();
            SetStatus(L"Model placed from project/Assets/Models. ASCII FBX and text X are supported.");
        } else if(id==kDuplicate&&!selectedIndices.empty()) {
            if(!Apply())return;
            std::vector<int> duplicates; duplicates.reserve(selectedIndices.size());
            for(int source:selectedIndices)if(source>=0&&static_cast<size_t>(source)<scene.entities.size()) {
                Entity copy=scene.entities[static_cast<size_t>(source)]; copy.name+=" (Clone)";
                scene.entities.push_back(std::move(copy)); duplicates.push_back(static_cast<int>(scene.entities.size())-1);
            }
            if(!duplicates.empty()){selectedIndices=duplicates;selected=duplicates.back();Populate();SetStatus(L"Selection duplicated. Ctrl+D repeats this action.");}
        } else if (id == kDelete && selected >= 0 && selected < static_cast<int>(scene.entities.size())) {
            if (selectedIndices.size() > 1 && MessageBoxW(window, L"Remove all selected objects from the scene?", L"Delete objects", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
            for (auto it = selectedIndices.rbegin(); it != selectedIndices.rend(); ++it) {
                const int removed = *it;
                scene.entities.erase(scene.entities.begin() + removed);
                for (auto& entity : scene.entities) {
                    if (entity.parent == removed) entity.parent = -1;
                    else if (entity.parent > removed) --entity.parent;
                }
            }
            selectedIndices.clear(); selected = -1; Populate(); SetStatus(L"Selected objects removed. Save to keep changes.");
        } else if (id == kApply) {
            Apply();
        } else if (id == kSave) {
            if (sceneProtected) { Error(window, "The existing scene could not be loaded. Fix it and use Load before saving."); return; }
            if (!Apply()) return;
            std::string error;
            if (!SaveScene(projectRoot / L"scene.dxs", scene, error)) Error(window, error);
            else SetStatus(L"Scene saved to the active project.");
        } else if (id == kLoad) {
            Scene loaded; std::string error;
            if (!LoadScene(projectRoot / L"scene.dxs", loaded, error)) Error(window, error);
            else {
                scene = std::move(loaded); renderer.SetCamera(camera);
                for (auto button : projectionButtons) InvalidateRect(button, nullptr, TRUE);
                sceneProtected = false; selected = scene.entities.empty() ? -1 : 0;
                selectedIndices = selected < 0 ? std::vector<int>{} : std::vector<int>{selected};
                Populate(); SetStatus(L"Scene loaded.");
            }
        } else if (id == kExport) {
            if (sceneProtected) { Error(window, "The existing scene could not be loaded. Fix it and use Load before exporting."); return; }
            if (!Apply()) return;
            std::string error;
            std::filesystem::path destination;
            if (!ChooseExportFolder(window, base, destination, error)) { if (!error.empty()) Error(window, error); return; }
            std::error_code ec;
            bool existing = false;
            for (const auto& file : {L"Game.exe", L"scene.dxs", L"Default.vs.cso", L"Default.ps.cso"}) {
                if (std::filesystem::exists(destination / file, ec)) existing = true;
                if (ec) { Error(window, "Could not check the export folder: " + ec.message()); return; }
            }
            if (existing && MessageBoxW(window, L"The selected folder already contains one or more exported game files. Replace them?", L"Export", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
            if (!ExportGame(base, projectRoot, destination, scene, error)) Error(window, error);
            else { SetStatus(L"Export complete. Run Game.exe in the selected folder."); MessageBoxW(window, (L"Exported to:\n" + destination.wstring()).c_str(), L"Export complete", MB_OK); }
        } else if (id == kReloadShader) {
            InitRenderer();
        }
    }
};

LRESULT CALLBACK HierarchySubclassProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
                                       UINT_PTR, DWORD_PTR data) {
    auto* e = reinterpret_cast<Editor*>(data);
    if (!e) return DefSubclassProc(hwnd, message, wp, lp);
    switch (message) {
    case WM_LBUTTONDOWN: {
        const DWORD hit = static_cast<DWORD>(SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lp));
        e->hierarchyDrag = HIWORD(hit) ? -1 : LOWORD(hit);
        e->hierarchyDragStart = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        e->hierarchyDragging = false;
        break;
    }
    case WM_MOUSEMOVE:
        if ((wp & MK_LBUTTON) && e->hierarchyDrag >= 0 &&
            (std::abs(GET_X_LPARAM(lp)-e->hierarchyDragStart.x) > 5 || std::abs(GET_Y_LPARAM(lp)-e->hierarchyDragStart.y) > 5)) {
            if (!e->hierarchyDragging) { e->hierarchyDragging = true; SetCapture(hwnd); }
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (e->hierarchyDragging && e->hierarchyDrag >= 0) {
            const DWORD hit = static_cast<DWORD>(SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lp));
            const int parent = HIWORD(hit) ? -1 : static_cast<int>(LOWORD(hit));
            std::string error;
            if (!SetEntityParent(e->scene, e->hierarchyDrag, parent, error)) Error(e->window, error);
            else { e->Populate(); e->SetStatus(parent < 0 ? L"Object moved to the hierarchy root." : L"Parent relationship changed. Save the scene to keep it."); }
            e->hierarchyDragging = false; e->hierarchyDrag = -1; ReleaseCapture(); return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        e->hierarchyDragging = false; e->hierarchyDrag = -1; break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, HierarchySubclassProc, 1); break;
    }
    return DefSubclassProc(hwnd, message, wp, lp);
}

LRESULT CALLBACK SplitterSubclassProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
                                     UINT_PTR splitter, DWORD_PTR data) {
    auto* e=reinterpret_cast<Editor*>(data); if(!e)return DefSubclassProc(hwnd,message,wp,lp);
    const auto drag=splitter==0?Editor::LayoutDrag::Left:splitter==1?Editor::LayoutDrag::Right:Editor::LayoutDrag::Project;
    switch(message) {
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr,splitter==2?IDC_SIZENS:IDC_SIZEWE)); return TRUE;
    case WM_LBUTTONDOWN:
        e->layoutDrag=drag; SetCapture(hwnd); return 0;
    case WM_MOUSEMOVE:
        if(GetCapture()==hwnd&&e->layoutDrag==drag) {
            POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; ClientToScreen(hwnd,&point); ScreenToClient(e->window,&point);
            e->ResizeDock(drag,point); return 0;
        }
        break;
    case WM_LBUTTONUP:
        if(GetCapture()==hwnd){ReleaseCapture();e->layoutDrag=Editor::LayoutDrag::None;return 0;}break;
    case WM_CAPTURECHANGED:
        if(e->layoutDrag==drag)e->layoutDrag=Editor::LayoutDrag::None; break;
    case WM_PAINT: {
        PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT r{};GetClientRect(hwnd,&r);
        HBRUSH brush=CreateSolidBrush(RGB(52,55,60));FillRect(dc,&r,brush);DeleteObject(brush);EndPaint(hwnd,&ps);return 0;
    }
    case WM_NCDESTROY: RemoveWindowSubclass(hwnd,SplitterSubclassProc,splitter); break;
    }
    return DefSubclassProc(hwnd,message,wp,lp);
}

LRESULT CALLBACK PanelProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto* p=reinterpret_cast<EditorPanel*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (message==WM_NCCREATE) {
        p=static_cast<EditorPanel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));
        p->window=hwnd;
    }
    if (!p || !p->editor) return DefWindowProcW(hwnd,message,wp,lp);
    auto* e=p->editor;
    switch(message) {
    case WM_SIZE:
        if (wp!=SIZE_MINIMIZED) e->LayoutPanel(p->index);
        return 0;
    case WM_MOUSEWHEEL:
        if(p->index==2){e->inspectorScroll=std::max(0,e->inspectorScroll-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*48);e->LayoutPanel(2);return 0;}
        break;
    case WM_VSCROLL:
        if(p->index==2){
            SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(hwnd,SB_VERT,&info);int position=e->inspectorScroll;
            switch(LOWORD(wp)){
            case SB_LINEUP:position-=32;break;case SB_LINEDOWN:position+=32;break;
            case SB_PAGEUP:position-=static_cast<int>(info.nPage);break;case SB_PAGEDOWN:position+=static_cast<int>(info.nPage);break;
            case SB_THUMBPOSITION:case SB_THUMBTRACK:position=info.nTrackPos;break;case SB_TOP:position=0;break;case SB_BOTTOM:position=info.nMax;break;
            default:return 0;}
            e->inspectorScroll=std::max(0,position);e->LayoutPanel(2);return 0;
        }
        break;
    case WM_COMMAND:
    case WM_DRAWITEM:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return SendMessageW(e->window,message,wp,lp);
    case WM_CONTEXTMENU:
        if(p->index==0){e->ShowObjectMenu({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;}
        if(p->index==2){e->ShowComponentMenu({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;}
        break;
    case WM_CLOSE:
        p->hidden=true; ShowWindow(hwnd,SW_HIDE); e->Layout(); return 0;
    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize=p->index==2?POINT{360,580}:p->index==1?POINT{420,280}:p->index==3?POINT{450,240}:POINT{260,260};
        return 0;
    }
    case WM_ERASEBKGND: {
        RECT r{}; GetClientRect(hwnd,&r);
        FillRect(reinterpret_cast<HDC>(wp),&r,e->background); return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps);
        RECT r{}; GetClientRect(hwnd,&r);
        HBRUSH border=CreateSolidBrush(RGB(64,67,73));
        FrameRect(dc,&r,border);
        r.bottom=28; FillRect(dc,&r,border); DeleteObject(border);
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(228,231,236));
        auto old=SelectObject(dc,e->titleFont);
        r.left=9; r.right=std::max<LONG>(r.left,r.right-122);
        DrawTextW(dc,kPanelNames[p->index],-1,&r,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        SelectObject(dc,old); EndPaint(hwnd,&ps); return 0;
    }
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}

LRESULT CALLBACK ViewportProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    Editor* e = reinterpret_cast<Editor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        e = static_cast<Editor*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(e));
    }
    if (!e) return DefWindowProcW(hwnd, message, wp, lp);
    switch (message) {
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        if (!e->rendererReady) return 0;
        if (!e->Apply()) return 0;
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (!(wp & MK_CONTROL) && e->selected >= 0 && static_cast<size_t>(e->selected) < e->scene.entities.size()) {
            const auto item = WorldDraw(e->scene, e->selected);
            const int axis = e->renderer.HitGizmo(x, y, item, e->gizmoMode);
            if (axis >= 0) {
                e->gizmoAxis = axis;
                e->gizmoStartItem = item;
                e->gizmoStartMouse = {x, y};
                e->gizmoStartItems.clear();
                for (int i : e->selectedIndices) e->gizmoStartItems.emplace_back(i, WorldDraw(e->scene, i));
                e->renderer.SetGizmoHot(axis);
                e->drag = Editor::Drag::Gizmo;
                SetCapture(hwnd);
                return 0;
            }
        }
        e->selectionStart = {x, y};
        e->selectionBeforeDrag = e->selectedIndices;
        e->selectionAdd = (wp & MK_SHIFT) != 0;
        e->selectionToggle = (wp & MK_CONTROL) != 0;
        e->selectingRect = false;
        e->drag = Editor::Drag::Select;
        SetCapture(hwnd);
        return 0;
    }
    case WM_LBUTTONUP:
        if (e->drag == Editor::Drag::Gizmo) {
            e->drag = Editor::Drag::None;
            ReleaseCapture();
            e->renderer.SetGizmoHot(-1);
            e->SetStatus(L"Transform changed. Save the scene to keep it.");
        } else if (e->drag == Editor::Drag::Select) {
            auto items = ResolveWorldDraws(e->scene);
            std::vector<int> hits;
            const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (e->selectingRect) hits = e->renderer.PickRect(e->selectionStart.x, e->selectionStart.y, x, y, items);
            else { const int hit = e->renderer.Pick(x, y, items); if (hit >= 0) hits.push_back(hit); }
            e->drag = Editor::Drag::None;
            e->renderer.SetMarquee(false,0,0,0,0);
            ReleaseCapture();
            e->SetSelection(e->MergeSelection(hits), hits.empty() ? -1 : hits.back());
            e->SetStatus(L"Selection updated. Shift adds, Ctrl toggles; drag empty space to select an area.");
        }
        return 0;
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        if (e->drag == Editor::Drag::Gizmo || e->drag == Editor::Drag::Select) return 0;
        SetFocus(hwnd);
        e->drag = message == WM_RBUTTONDOWN ? Editor::Drag::Orbit : Editor::Drag::Pan;
        e->lastMouse = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (message == WM_RBUTTONDOWN) {
            e->rightStart = e->lastMouse;
            e->rightUsed = false;
            e->orbitPivot = (GetKeyState(VK_MENU) & 0x8000) != 0;
            e->cameraTick = GetTickCount64();
        }
        SetCapture(hwnd);
        return 0;
    case WM_RBUTTONUP:
        if (e->drag == Editor::Drag::Orbit) {
            const bool menu = !e->rightUsed && !e->orbitPivot;
            e->drag = Editor::Drag::None; ReleaseCapture();
            if (menu) { POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ClientToScreen(hwnd, &pt); e->ShowObjectMenu(pt); }
        }
        return 0;
    case WM_MBUTTONUP:
        if (e->drag == Editor::Drag::Pan) { e->drag = Editor::Drag::None; ReleaseCapture(); }
        return 0;
    case WM_CAPTURECHANGED:
        if (e->drag == Editor::Drag::Gizmo && e->selected >= 0 && static_cast<size_t>(e->selected) < e->scene.entities.size()) {
            e->RestoreDrag();
            e->SetStatus(L"Transform cancelled.");
        }
        if (e->drag == Editor::Drag::Select) e->renderer.SetMarquee(false,0,0,0,0);
        e->renderer.SetGizmoHot(-1);
        e->drag = Editor::Drag::None;
        return 0;
    case WM_KEYDOWN:
        if (e->drag == Editor::Drag::Orbit) return 0;
        if (wp == VK_ESCAPE && (e->drag == Editor::Drag::Gizmo || e->drag == Editor::Drag::Select)) {
            if (e->drag == Editor::Drag::Gizmo) e->RestoreDrag();
            e->renderer.SetMarquee(false,0,0,0,0);
            e->renderer.SetGizmoHot(-1);
            e->drag = Editor::Drag::None;
            ReleaseCapture();
            e->SetStatus(L"Transform cancelled.");
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (e->drag != Editor::Drag::None) {
            const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (e->drag == Editor::Drag::Select) {
                if (std::abs(x - e->selectionStart.x) >= 4 || std::abs(y - e->selectionStart.y) >= 4) e->selectingRect = true;
                if (e->selectingRect) e->renderer.SetMarquee(true, e->selectionStart.x, e->selectionStart.y, x, y);
                return 0;
            }
            if (e->drag == Editor::Drag::Gizmo) {
                if (e->selected >= 0 && static_cast<size_t>(e->selected) < e->scene.entities.size()) {
                    DrawItem updated;
                    if (e->renderer.DragGizmo(e->gizmoStartItem, e->gizmoMode, e->gizmoAxis,
                                             e->gizmoStartMouse.x, e->gizmoStartMouse.y, x, y, updated)) {
                        if (wp & MK_SHIFT) SnapGizmo(updated, e->gizmoStartItem, e->gizmoMode, e->gizmoAxis);
                        e->UpdateGroup(updated);
                        e->Inspect();
                    }
                }
                return 0;
            }
            const float dx = static_cast<float>(x - e->lastMouse.x), dy = static_cast<float>(y - e->lastMouse.y);
            e->lastMouse = {x, y};
            if (e->drag == Editor::Drag::Orbit) {
                if (std::abs(x - e->rightStart.x) >= 3 || std::abs(y - e->rightStart.y) >= 3) e->rightUsed = true;
                if (e->rightUsed) LookCamera(e->camera, dx, dy, e->orbitPivot);
            } else {
                RECT rect{}; GetClientRect(hwnd, &rect);
                const float height = static_cast<float>(std::max<LONG>(1, rect.bottom));
                {
                    const float worldHeight = e->camera.distance * (e->camera.orthographic ? 10.0f / 6.32455532f : 2.0f * std::tan(DirectX::XM_PIDIV4 * 0.5f));
                    const float speed = worldHeight / height;
                    const float sy = std::sin(e->camera.yaw), cy = std::cos(e->camera.yaw);
                    const float sp = std::sin(e->camera.pitch), cp = std::cos(e->camera.pitch);
                    e->camera.target.x += (-dx * cy - dy * sy * sp) * speed;
                    e->camera.target.y += dy * cp * speed;
                    e->camera.target.z += (-dx * sy + dy * cy * sp) * speed;
                }
            }
            e->renderer.SetCamera(e->camera);
        } else if (e->rendererReady && e->selected >= 0 && static_cast<size_t>(e->selected) < e->scene.entities.size()) {
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};
            TrackMouseEvent(&track);
            e->renderer.SetGizmoHot(e->renderer.HitGizmo(GET_X_LPARAM(lp),GET_Y_LPARAM(lp),WorldDraw(e->scene,e->selected),e->gizmoMode));
        }
        return 0;
    case WM_MOUSELEAVE:
        if (e->drag == Editor::Drag::None) e->renderer.SetGizmoHot(-1);
        return 0;
    case WM_MOUSEWHEEL: {
        const float steps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / static_cast<float>(WHEEL_DELTA);
        e->camera.distance = std::clamp(e->camera.distance * std::pow(0.85f, steps), 0.5f, 80.0f);
        e->renderer.SetCamera(e->camera);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps{}; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); return 0; }
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    Editor* e = reinterpret_cast<Editor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        e = static_cast<Editor*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(e));
        e->window = hwnd;
    }
    if (!e) return DefWindowProcW(hwnd, message, wp, lp);
    switch (message) {
    case WM_CREATE: {
        const int fontHeight = -MulDiv(13, static_cast<int>(GetDpiForWindow(hwnd)), 96);
        const int titleFontHeight = -MulDiv(14, static_cast<int>(GetDpiForWindow(hwnd)), 96);
        e->font = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        e->titleFont = CreateFontW(titleFontHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        e->background = CreateSolidBrush(RGB(43, 45, 50)); e->inputBackground = CreateSolidBrush(RGB(54, 57, 63));
        e->CreatePanels();
        for(int i=0;i<3;++i){
            e->splitters[i]=e->Make(L"STATIC",L"",SS_NOTIFY,kSplitter+i);
            SetWindowSubclass(e->splitters[i],SplitterSubclassProc,static_cast<UINT_PTR>(i),reinterpret_cast<DWORD_PTR>(e));
        }
        const wchar_t* names[] = {L"+ Cube", L"+ Quad", L"Delete selection", L"Apply changes", L"Save", L"Load", L"Export game...", L"Reload Shader", L""};
        const int ids[] = {kAddCube, kAddSprite, kDelete, kApply, kSave, kLoad, kExport, kReloadShader, 0};
        for (int i = 0; i < 8; ++i) e->controls[i] = e->Make(L"BUTTON", names[i], BS_OWNERDRAW | WS_TABSTOP, ids[i]);
        e->controls[8] = e->Make(L"STATIC", L"DX12", SS_CENTER);
        e->controls[9] = e->Make(L"BUTTON", L"Play", BS_OWNERDRAW | WS_TABSTOP, kBuildDebug);
        e->controls[10] = e->Make(L"BUTTON", L"Android", BS_OWNERDRAW | WS_TABSTOP, kBuildAndroid);
        SetWindowTextW(e->controls[6], L"Export");
        SetWindowTextW(e->controls[7], L"Shader");
        for (int i=0;i<3;++i) SetParent(e->controls[i],e->panels[0].window);
        SetParent(e->controls[3],e->panels[2].window);
        const wchar_t* modeNames[] = {L"Move", L"Rotate", L"Scale"};
        for (int i = 0; i < 3; ++i)
            e->gizmoButtons[i] = e->Make(L"BUTTON", modeNames[i], BS_OWNERDRAW | WS_TABSTOP, kMoveMode + i);
        for (auto button:e->gizmoButtons) SetParent(button,e->panels[1].window);
        const wchar_t* projectionNames[]={L"Persp",L"Ortho"};
        for (int i=0;i<2;++i) {
            e->projectionButtons[i]=e->Make(L"BUTTON",projectionNames[i],BS_OWNERDRAW|WS_TABSTOP,kPerspective+i);
            SetParent(e->projectionButtons[i],e->panels[1].window);
        }
        e->hierarchy = e->Make(L"LISTBOX", L"", LBS_NOTIFY | LBS_EXTENDEDSEL | WS_VSCROLL | WS_BORDER | WS_TABSTOP, kHierarchy);
        SetParent(e->hierarchy,e->panels[0].window);
        SetWindowSubclass(e->hierarchy, HierarchySubclassProc, 1, reinterpret_cast<DWORD_PTR>(e));
        e->viewport = CreateWindowExW(0, L"DxStudioViewport", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                      0, 0, 1, 1, e->panels[1].window, nullptr, e->instance, e);
        for (int i = 0; i < kFieldCount; ++i) {
            if (i == 1) continue;
            e->labels[i] = e->Make(L"STATIC", kFieldLabels[i], SS_LEFT);
            e->edits[i] = e->Make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, kFirstEdit + i);
            SetParent(e->labels[i],e->panels[2].window);
            SetParent(e->edits[i],e->panels[2].window);
            if (i>=2 && i<=10) SetWindowTextW(e->labels[i],((i-2)%3)==0?L"X":((i-2)%3)==1?L"Y":L"Z");
            if (i>=11) SetWindowTextW(e->labels[i],i==11?L"R":i==12?L"G":i==13?L"B":L"A");
        }
        e->inspectorHint=e->Make(L"STATIC",L"Edit values, then Apply changes",SS_LEFT);
        SetParent(e->inspectorHint,e->panels[2].window);
        e->materialLabel=e->Make(L"STATIC",L"Material",SS_LEFT);
        e->materialEdit=e->Make(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP);
        e->textureLabel=e->Make(L"STATIC",L"Texture",SS_LEFT);
        e->textureEdit=e->Make(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP);
        for(auto control:{e->materialLabel,e->materialEdit,e->textureLabel,e->textureEdit})SetParent(control,e->panels[2].window);
        const wchar_t* rowNames[]={L"Position",L"Rotation °",L"Scale"};
        const wchar_t* sections[] = {L"Transform", L"Rendering / Color", L"Components"};
        for (int i = 0; i < 3; ++i) {
            e->rowLabels[i]=e->Make(L"STATIC",rowNames[i],SS_LEFT);
            SetParent(e->rowLabels[i],e->panels[2].window);
            e->groupLabels[i] = e->Make(L"BUTTON", sections[i], BS_OWNERDRAW|WS_TABSTOP, kSectionToggle+i);
            SetParent(e->groupLabels[i],e->panels[2].window);
            SendMessageW(e->groupLabels[i], WM_SETFONT, reinterpret_cast<WPARAM>(e->titleFont), TRUE);
        }
        e->componentList = e->Make(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP, kComponentList);
        e->componentChoice = e->Make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kComponentChoice);
        e->componentAdd = e->Make(L"BUTTON", L"Attach", BS_OWNERDRAW | WS_TABSTOP, kAddComponent);
        e->componentRemove = e->Make(L"BUTTON", L"Remove", BS_OWNERDRAW | WS_TABSTOP, kRemoveComponent);
        e->componentFields = e->Make(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP, kComponentFields);
        e->componentValue = e->Make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, kComponentValue);
        e->componentValueApply = e->Make(L"BUTTON", L"Set", BS_OWNERDRAW | WS_TABSTOP, kApplyComponentValue);
        for (auto control:{e->componentList,e->componentChoice,e->componentAdd,e->componentRemove,e->componentFields,e->componentValue,e->componentValueApply}) SetParent(control,e->panels[2].window);
        for(const wchar_t* builtin:{L"PolygonRenderer",L"ModelRenderer",L"Image"}) SendMessageW(e->componentChoice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(builtin));
        for (const auto& name : RegisteredComponentNames()) {
            auto wide = Wide(name);
            SendMessageW(e->componentChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
        }
        SendMessageW(e->componentChoice, CB_SETCURSEL, 0, 0);
        e->status = e->Make(L"STATIC", L"", SS_LEFT);
        e->projectPanel = CreateProjectPanel(e->panels[3].window, e->instance, e->projectRoot, e->base);
        std::string error;
        if (!LoadScene(e->projectRoot / L"scene.dxs", e->scene, error)) {
            std::error_code ec;
            bool exists = std::filesystem::exists(e->projectRoot / L"scene.dxs", ec);
            if (exists || ec) {
                e->sceneProtected = true;
                Error(hwnd, "The existing scene could not be loaded. Save and export are disabled to protect it. Fix the file and select Load.\n\n" + error);
            }
            Entity cube; cube.name = "Cube"; cube.draw.color = {0.35f, 0.68f, 1.f, 1.f};
            e->scene.entities.push_back(cube);
        }
        e->camera=Camera{};
        e->shellReady=true;
        e->selected = e->scene.entities.empty() ? -1 : 0; if (e->selected >= 0) e->selectedIndices = {e->selected}; e->Populate(); e->Layout();
        e->InitRenderer(); SetTimer(hwnd, 1, 16, nullptr); return 0;
    }
    case WM_SIZE:
        if (e->viewport && wp != SIZE_MINIMIZED) e->Layout();
        return 0;
    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
            suggested->right-suggested->left, suggested->bottom-suggested->top,
            SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO*>(lp); info->ptMinTrackSize = {1000, 700}; return 0;
    }
    case WM_COMMAND:
        if (!e->ShellCommand(LOWORD(wp))) e->Command(LOWORD(wp), HIWORD(wp));
        return 0;
    case WM_LBUTTONDOWN: {
        RECT client{}; GetClientRect(hwnd,&client); const int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        const int lower=client.bottom-28-(e->panels[3].hidden||e->panels[3].floating?0:(e->panels[3].collapsed?28:e->dockProject));
        if(!e->panels[0].hidden&&!e->panels[0].floating&&std::abs(x-e->dockLeft)<=6)e->layoutDrag=Editor::LayoutDrag::Left;
        else if(!e->panels[2].hidden&&!e->panels[2].floating&&std::abs(x-(client.right-e->dockRight))<=6)e->layoutDrag=Editor::LayoutDrag::Right;
        else if(!e->panels[3].hidden&&!e->panels[3].floating&&std::abs(y-lower)<=6)e->layoutDrag=Editor::LayoutDrag::Project;
        if(e->layoutDrag!=Editor::LayoutDrag::None){SetCapture(hwnd);return 0;} break;
    }
    case WM_MOUSEMOVE:
        if(e->layoutDrag!=Editor::LayoutDrag::None){RECT client{};GetClientRect(hwnd,&client);const int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
            const int width=static_cast<int>(client.right),height=static_cast<int>(client.bottom);
            if(e->layoutDrag==Editor::LayoutDrag::Left)e->dockLeft=std::clamp(x,160,std::max(160,width-500));
            else if(e->layoutDrag==Editor::LayoutDrag::Right)e->dockRight=std::clamp(width-x,240,std::max(240,width-400));
            else e->dockProject=std::clamp(height-28-y,120,std::max(120,height-220));
            e->Layout();return 0;} break;
    case WM_LBUTTONUP:
        if(e->layoutDrag!=Editor::LayoutDrag::None){e->layoutDrag=Editor::LayoutDrag::None;ReleaseCapture();return 0;}break;
    case WM_CAPTURECHANGED: e->layoutDrag=Editor::LayoutDrag::None; break;
    case WM_MOUSEWHEEL: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(e->viewport, &pt);
        RECT r{}; GetClientRect(e->viewport, &r);
        if (PtInRect(&r, pt)) return SendMessageW(e->viewport, message, wp, lp);
        break;
    }
    case WM_TIMER: e->Draw(); return 0;
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (item->CtlType != ODT_BUTTON) break;
        const int id = static_cast<int>(item->CtlID);
        const bool active = (id == kBuildDebug && e->playing) ||
            (id == kMoveMode && e->gizmoMode == GizmoMode::Translate) ||
            (id == kRotateMode && e->gizmoMode == GizmoMode::Rotate) || (id == kScaleMode && e->gizmoMode == GizmoMode::Scale) ||
            (id == kPerspective && !e->camera.orthographic) || (id == kOrthographic && e->camera.orthographic);
        const bool section=id>=kSectionToggle && id<kSectionToggle+3;
        const COLORREF bg = id == kBuildDebug && e->playing ? RGB(45,112,70) : active ? RGB(68,96,124) :
            (item->itemState & ODS_SELECTED) ? RGB(83,87,94) : RGB(57,60,66);
        HBRUSH brush = CreateSolidBrush(bg);
        FillRect(item->hDC, &item->rcItem, brush);
        DeleteObject(brush);
        HBRUSH border = CreateSolidBrush(active ? RGB(97,151,199) : RGB(77,80,86));
        FrameRect(item->hDC, &item->rcItem, border);
        DeleteObject(border);
        wchar_t text[128]{}; GetWindowTextW(item->hwndItem, text, 128);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, (item->itemState & ODS_DISABLED) ? RGB(125,127,131) : RGB(225,228,232));
        auto old = SelectObject(item->hDC, section ? e->titleFont : e->font);
        RECT r = item->rcItem;
        InflateRect(&r, -4, -2);
        if (section) r.left+=6;
        DrawTextW(item->hDC, text, -1, &r, (section?DT_LEFT:DT_CENTER) | DT_VCENTER | DT_SINGLELINE |
            DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(item->hDC, old);
        if (item->itemState & ODS_FOCUS) { InflateRect(&r,-3,-3); DrawFocusRect(item->hDC,&r); }
        return TRUE;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wp); SetTextColor(dc, RGB(225, 228, 234));
        if (message==WM_CTLCOLORSTATIC) {
            HWND control=reinterpret_cast<HWND>(lp);
            for (int i=2;i<=10;++i) if (e->labels[i]==control) {
                const COLORREF colors[]={RGB(237,115,115),RGB(139,201,119),RGB(119,168,237)};
                SetTextColor(dc,colors[(i-2)%3]);
            }
        }
        const bool input = message != WM_CTLCOLORSTATIC;
        SetBkColor(dc, input ? RGB(54, 57, 63) : RGB(43, 45, 50));
        return reinterpret_cast<LRESULT>(input ? e->inputBackground : e->background);
    }
    case WM_ERASEBKGND: {
        RECT r{}; GetClientRect(hwnd, &r); FillRect(reinterpret_cast<HDC>(wp), &r, e->background); return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); RECT r{}; GetClientRect(hwnd, &r);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(235, 238, 245));
        auto old = SelectObject(dc, e->titleFont);
        RECT title{12,10,224,38};
        DrawTextW(dc,L"DxStudio",-1,&title,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(dc, old); EndPaint(hwnd, &ps); return 0;
    }
    case WM_DESTROY:
        e->shellReady=false;
        for (auto& panel:e->panels) if (panel.floating && IsWindow(panel.window)) DestroyWindow(panel.window);
        KillTimer(hwnd, 1); e->renderer.Shutdown(); DeleteObject(e->font); DeleteObject(e->titleFont);
        DeleteObject(e->background); DeleteObject(e->inputBackground); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
} // namespace

int RunEditor(HINSTANCE instance, const std::filesystem::path& baseDir,
              const std::filesystem::path& initialProjectFile) {
    RegisterProjectComponents();
    Editor editor; editor.instance = instance; editor.base = baseDir;
    editor.projectRoot = baseDir / L"project";
    editor.projectFile = baseDir / L"project" / L"DxStudio.dxproject";
    if (!initialProjectFile.empty()) {
        std::string error;
        if (!LoadProjectDescriptor(initialProjectFile, editor.projectRoot, error)) {
            Error(nullptr, error); return 1;
        }
        editor.projectFile = initialProjectFile;
    }
    WNDCLASSEXW viewportClass{sizeof(viewportClass)};
    viewportClass.lpfnWndProc = ViewportProc; viewportClass.hInstance = instance;
    viewportClass.hCursor = LoadCursorW(nullptr, IDC_ARROW); viewportClass.lpszClassName = L"DxStudioViewport";
    if (!RegisterClassExW(&viewportClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = WindowProc; wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"DxStudioEditorWindow";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"DxStudio", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 1360, 820, nullptr, nullptr, instance, &editor);
    if (!window) return 1;
    const auto title = L"DxStudio  —  " + editor.projectFile.stem().wstring();
    SetWindowTextW(window, title.c_str());
    ShowWindow(window, SW_SHOW); UpdateWindow(window);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            editor.Command(kSave, BN_CLICKED);
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == 'P' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            editor.Command(kBuildDebug, BN_CLICKED);
            continue;
        }
        if(msg.message==WM_KEYDOWN&&msg.wParam=='D'&&(GetKeyState(VK_CONTROL)&0x8000)){
            editor.Command(kDuplicate,BN_CLICKED);continue;
        }
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_F2&&editor.selectedIndices.size()==1){
            SetFocus(editor.edits[0]);SendMessageW(editor.edits[0],EM_SETSEL,0,-1);continue;
        }
        if (msg.message == WM_KEYDOWN && editor.selectedIndices.size() == 1) {
            const int field = GetDlgCtrlID(msg.hwnd) - kFirstEdit;
            if (field >= 0 && field < kFieldCount && msg.hwnd == editor.edits[field]) {
                if (msg.wParam == VK_RETURN) { editor.Apply(); continue; }
                if (field >= 2 && (msg.wParam == VK_UP || msg.wParam == VK_DOWN)) {
                    try {
                        const auto current = Text(msg.hwnd);
                        size_t used = 0;
                        float value = std::stof(current, &used);
                        while (used < current.size() && iswspace(current[used])) ++used;
                        if (used == current.size() && std::isfinite(value)) {
                            float step = field >= 11 ? 0.01f : field >= 5 && field <= 7 ? 1.0f : 0.1f;
                            if (GetKeyState(VK_SHIFT) & 0x8000) step *= 10;
                            value += msg.wParam == VK_UP ? step : -step;
                            wchar_t text[64]; swprintf_s(text, L"%.9g", static_cast<double>(value));
                            SetWindowTextW(msg.hwnd, text);
                            editor.Apply();
                        }
                    } catch (...) { /* Keep invalid input for the user to correct. */ }
                    continue;
                }
            }
        }
        // Ensure dialog-style keyboard navigation cannot swallow Esc during a drag.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && (editor.drag == Editor::Drag::Gizmo || editor.drag == Editor::Drag::Select)) {
            SendMessageW(editor.viewport, WM_KEYDOWN, msg.wParam, msg.lParam);
            continue;
        }
        if (msg.message == WM_KEYDOWN && (msg.hwnd == editor.viewport || msg.hwnd == editor.hierarchy || msg.hwnd == editor.window)
            && editor.drag != Editor::Drag::Orbit && editor.drag != Editor::Drag::Gizmo
            && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
            if (msg.wParam == 'W' || msg.wParam == 'E' || msg.wParam == 'R') {
                editor.Command(msg.wParam == 'W' ? kMoveMode : msg.wParam == 'E' ? kRotateMode : kScaleMode, BN_CLICKED);
                continue;
            }
            if (msg.wParam == 'F') { editor.FrameSelection(); continue; }
            if (msg.wParam == VK_DELETE) { editor.Command(kDelete, BN_CLICKED); continue; }
        }
        HWND dialogRoot = GetAncestor(msg.hwnd, GA_ROOT);
        if (!dialogRoot || !IsDialogMessageW(dialogRoot, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return static_cast<int>(msg.wParam);
}
} // namespace dxs
