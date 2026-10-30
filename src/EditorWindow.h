#pragma once

// Internal editor window state shared by the Editor*.cpp files. Public entry
// point: Editor.h (RunEditor).

#include "Engine.h"
#include "Renderer.h"
#include "ProjectPanel.h"
#include "EditorInteraction.h"
#include "EditorCommands.h"
#include "EditorTheme.h"
#include "EditorUtility.h"
#include "ConsolePanel.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cwctype>
#include <commctrl.h>
#include <commdlg.h>
#include <fstream>
#include <iomanip>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>


namespace dxs::editor {

LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
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
inline const wchar_t* PANEL_NAMES[] = {L"Hierarchy", L"Scene", L"Inspector", L"Project"};
constexpr theme::Icon PANEL_ICONS[] = {theme::Icon::HIERARCHY, theme::Icon::SCENE, theme::Icon::INSPECTOR, theme::Icon::PROJECT};
// Bottom dock tabs share panel 3 (Unity: Project | Console).
inline const wchar_t* BOTTOM_TAB_NAMES[] = {L"Project", L"Console"};
constexpr theme::Icon BOTTOM_TAB_ICONS[] = {theme::Icon::PROJECT, theme::Icon::CONSOLE};
constexpr int BOTTOM_TAB_COUNT = 2;
constexpr int MENU_COUNT = 8;
inline const wchar_t* MENU_NAMES[MENU_COUNT] = {L"File", L"Edit", L"Assets", L"GameObject", L"Component", L"Build", L"Window", L"Help"};
// Top chrome: custom menu bar, then the main toolbar.
constexpr int TOP_CHROME_HEIGHT = theme::MENU_BAR_HEIGHT + theme::TOOLBAR_HEIGHT;
constexpr int HIERARCHY_SCENE_ROW = 22; // "scene" header row above the hierarchy list

struct Editor : public IPlayModeCallbacks, public IGraphicsCallbacks {
    // ---- Unity/UE shell (menu bar, toolbar, tabs, console, status) ----------
    HWND menuButtons[MENU_COUNT]{};
    HMENU menuPopups[MENU_COUNT]{};
    HWND pauseButton{}, stepButton{}, graphicsButton{}, layoutButton{};
    HWND consolePanel{}, bottomTabs[BOTTOM_TAB_COUNT]{}, statusRight{};
    int bottomTab = 0;
    // Centre dock: Scene view (editor camera, gizmos) or Game view (active Camera).
    HWND viewTabs[2]{};
    bool gameView = false;
    int hierarchyHot = -1; // row under the mouse (hover / drop target)
    bool paused = false, stepRequested = false;
    bool inspectorEnabledChecked = false;
    ProjectSettings projectSettings;
    // Hierarchy tree state (parallel to hierarchyRows).
    std::vector<int> hierarchyDepths;
    std::vector<char> hierarchyHasChildren;
    std::vector<char> collapsedEntities; // indexed by entity
    ULONGLONG fpsTick = 0;
    int fpsFrames = 0;
    CallbackRegistration callbackRegistration;

    void OnEnterPlayMode(Scene& playScene) override {
        Debug::Log("Entered Play Mode (" + std::to_string(playScene.entities.size()) + " objects).");
    }
    void OnExitPlayMode(Scene&) override { Debug::Log("Exited Play Mode. Runtime changes were discarded."); }
    void OnPauseChanged(bool bPaused) override { Debug::Log(bPaused ? "Play Mode paused." : "Play Mode resumed."); }
    void OnGraphicsApiChanged(GraphicsApi api) override {
        Debug::Log(std::string("Scene view renderer: ") + GraphicsApiDisplayName(api));
    }

    HINSTANCE instance{};
    HWND window{}, viewport{}, hierarchy{}, hierarchySearch{}, status{}, projectPanel{};
    HWND splitters[3]{};
    HWND controls[11]{}, gizmoButtons[3]{}, groupLabels[3]{};
    HWND labels[FIELD_COUNT]{}, edits[FIELD_COUNT]{};
    HWND componentList{}, componentChoice{}, componentAdd{}, componentRemove{};
    HWND componentFields{}, componentValue{}, componentValueApply{};
    std::vector<std::pair<std::string, std::string>> inspectedComponentFields;
    EditorPanel panels[4]{};
    HWND projectionButtons[2]{}, rowLabels[3]{}, inspectorHint{};
    HWND materialLabel{}, materialEdit{}, textureLabel{}, textureEdit{};
    HWND inspectorEnabled{}, inspectorIcon{}, inspectorTagLabel{}, inspectorTagValue{};
    HWND inspectorLayerLabel{}, inspectorLayerValue{};
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
    // Listbox rows may be filtered; retain an explicit mapping instead of
    // accidentally treating a visual row index as an entity index.
    std::vector<int> hierarchyRows;
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
    int dockLeft = 210, dockRight = 430, dockProject = 210;
    int inspectorScroll = 0;
    enum class LayoutDrag { None, Left, Right, Project } layoutDrag = LayoutDrag::None;

    void TickCamera() {
        const ULONGLONG now = GetTickCount64();
        const float seconds = cameraTick ? static_cast<float>(now - cameraTick) / 1000.0f : 0.0f;
        cameraTick = now;
        if (drag != Drag::Orbit || orbitPivot || GetCapture() != viewport ||
            GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) != window)
            return;
        auto down = [](int key) {
            return (GetAsyncKeyState(key) & 0x8000) ? 1.0f : 0.0f;
        };
        if (FlyCamera(camera, down('W') - down('S'), down('D') - down('A'), down('E') - down('Q'), seconds,
                      down(VK_SHIFT) != 0)) {
            rightUsed = true;
            renderer.SetCamera(camera);
        }
    }

    void SyncHierarchy();
    void SetSelection(std::vector<int> indices, int active = -1) {
        indices.erase(std::remove_if(indices.begin(), indices.end(),
                                     [&](int i) { return i < 0 || static_cast<size_t>(i) >= scene.entities.size(); }),
                      indices.end());
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        selectedIndices = std::move(indices);
        selected = std::find(selectedIndices.begin(), selectedIndices.end(), active) != selectedIndices.end()
                       ? active
                       : (selectedIndices.empty() ? -1 : selectedIndices.back());
        SyncHierarchy();
        Inspect();
    }
    std::vector<int> MergeSelection(const std::vector<int>& hits) const;
    void RestoreDrag();
    void UpdateGroup(const DrawItem& updated);
    void FrameSelection();

    HWND Make(const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0, DWORD ex = 0) {
        HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, window,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return h;
    }
    // ---- Theme ------------------------------------------------------------------
    HWND tooltip{};
    void AddTooltip(HWND control, const wchar_t* text);
    // Registers the Unity/UE appearance of every owner-drawn control.
    void ApplyTheme();
    // Inputs are borderless; the parent panel paints a Unity-style box around
    // them. Returns the gap between the box and the control.
    RECT BoxInset(HWND control) const;
    void PaintInputBoxes(HDC dc, HWND panel);
    theme::Icon EntityIcon(const Entity& entity) const;
    // Hierarchy row: indent, foldout, type icon, name (dimmed when inactive).
    void DrawHierarchyRow(const DRAWITEMSTRUCT& item);
    void DrawComponentRow(const DRAWITEMSTRUCT& item);
    // Field row: "Name" on the left, value in an input-looking box (Unity property row).
    void DrawFieldRow(const DRAWITEMSTRUCT& item);
    // Inspector header statics: object icon and Tag / Layer dropdown fields.
    void DrawInspectorStatic(const DRAWITEMSTRUCT& item);
    void CreatePanels();
    // Popup menus shown by the custom-drawn menu bar (dark, Unity order).
    void CreateMainMenu();
    // Opens menu `index` under its menu-bar button (also used for Alt+letter).
    void OpenMainMenu(int index);
    void UpdateMenuChecks();
    void ResizeDock(LayoutDrag drag, POINT point);
    void FloatPanel(int index);
    bool ShellCommand(int id);
    void ShowObjectMenu(POINT point);
    void ShowComponentMenu(POINT point);
    void SetStatus(const wchar_t* text) {
        SetWindowTextW(status, text);
        RECT r{};
        GetClientRect(window, &r);
        RECT bar{0, r.bottom - theme::STATUS_BAR_HEIGHT, r.right, r.bottom};
        InvalidateRect(window, &bar, TRUE);
    }
    // ---- Play Mode (Unity: Play / Pause / Step) --------------------------------
    void RefreshTransport();
    void TogglePlay();
    void TogglePause();
    // Unity behaviour: Step pauses (entering Play Mode if needed) and runs one frame.
    void StepFrame();
    // ---- Graphics API (DirectX 12 / 11 / 9) ---------------------------------------
    void SwitchGraphicsApi(GraphicsApi api);
    void UpdateGraphicsBadge();
    void ShowToolbarDropdown(int id);
    // ---- Bottom dock tabs (Project | Console) -----------------------------------
    void SelectBottomTab(int tab);
    void SelectViewTab(bool game);
    void AttachToSelection(const std::string& name);
    // Reads <project>/ProjectSettings.dxsettings (missing file = defaults).
    void LoadSettings();
    bool SwitchProject(const std::filesystem::path& descriptor);
    bool NewProject();
    bool RunBuildScript(const wchar_t* scriptName, const wchar_t* label);
    void Populate();
    void Inspect();
    void RefreshComponentFields();
    bool Apply();
    // Main window: menu bar, toolbar, docked panels, status bar.
    //   +---------------------------------------------------------------+
    //   | File Edit Assets GameObject Component Build Window Help        |  menu bar
    //   | [Save][Load]        [>][||][>|]        [Shader][Export]..[DX12]|  toolbar
    //   | Hierarchy |          Scene              |  Inspector          |
    //   |-----------+-----------------------------|                     |
    //   | Project | Console                       |                     |
    //   | status                                             DX12 60fps|  status bar
    //   +---------------------------------------------------------------+
    void Layout();
    void LayoutPanel(int index);
    // (Re)creates the Scene view renderer with the project's graphics API. If
    // that API is unavailable the next one is used (12 -> 11 -> 9).
    bool InitRenderer();
    void Draw();
    // Right side of the status bar: backend, frame rate and Play state.
    void UpdateFrameStats();
    void Command(int id, int notification);
};

} // namespace dxs::editor
