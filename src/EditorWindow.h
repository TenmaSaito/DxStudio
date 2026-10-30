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
    // Dragging the tab out of the dock floats the panel; moving a floating
    // panel back over the editor docks it again (Unity / UE tab docking).
    POINT tabDragStart{};
    bool bTabPressed = false;
    bool bMoving = false;
};
inline const wchar_t* const DRAW_MODE_NAMES[] = {L"Shaded", L"Wireframe", L"Shaded Wireframe"};
inline const wchar_t* const ASPECT_NAMES[] = {L"Free Aspect", L"16:9", L"16:10", L"4:3", L"1:1"};
constexpr float ASPECT_RATIOS[] = {0.0f, 16.0f / 9.0f, 16.0f / 10.0f, 4.0f / 3.0f, 1.0f};
constexpr int ASPECT_COUNT = 5;
LRESULT CALLBACK LabelDragSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
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
    int nBottomTab = 0;
    // Centre dock: Scene view (editor camera, gizmos) or Game view (active Camera).
    HWND viewTabs[2]{};
    bool bGameView = false;
    int nHierarchyHot = -1;
    // Scene view draw mode and Game view aspect ratio (Unity toolbar dropdowns).
    HWND drawModeButton{}, aspectButton{};
    DrawMode drawMode = DrawMode::SHADED;
    int nGameAspect = 0;
    // Label scrubbing (drag the X / Y / Z / R / G / B / A label horizontally).
    int nScrubField = -1;
    int nScrubStartX = 0;
    float fScrubStartValue = 0.0f; // row under the mouse (hover / drop target)
    bool bPaused = false, bStepRequested = false;
    bool bInspectorEnabledChecked = false;
    ProjectSettings projectSettings;
    // Hierarchy tree state (parallel to hierarchyRows).
    std::vector<int> vHierarchyDepths;
    std::vector<char> vHierarchyHasChildren;
    std::vector<char> vCollapsedEntities; // indexed by entity
    ULONGLONG nFpsTick = 0;
    int nFpsFrames = 0;
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
    std::vector<std::pair<std::string, std::string>> vInspectedComponentFields;
    EditorPanel panels[4]{};
    HWND projectionButtons[2]{}, rowLabels[3]{}, inspectorHint{};
    HWND materialLabel{}, materialEdit{}, textureLabel{}, textureEdit{};
    HWND inspectorEnabled{}, inspectorIcon{}, inspectorTagLabel{}, inspectorTagValue{};
    HWND inspectorLayerLabel{}, inspectorLayerValue{};
    bool aSectionOpen[3]{true, true, true};
    bool bShellReady = false, bArranging = false;
    HFONT font{}, titleFont{};
    HBRUSH background{}, inputBackground{};
    std::filesystem::path base, projectRoot, projectFile;
    Scene scene;
    Scene editSceneSnapshot;
    Renderer renderer;
    Camera camera;
    Camera editCameraSnapshot;
    bool bRendererReady = false;
    bool bSceneProtected = false;
    bool bPlaying = false;
    ULONGLONG nPlayTick = 0;
    int nSelected = -1;
    std::vector<int> vSelectedIndices;
    // Listbox rows may be filtered; retain an explicit mapping instead of
    // accidentally treating a visual row index as an entity index.
    std::vector<int> vHierarchyRows;
    GizmoMode gizmoMode = GizmoMode::Translate;
    enum class Drag { None, Orbit, Pan, Gizmo, Select } drag = Drag::None;
    POINT lastMouse{};
    POINT gizmoStartMouse{};
    int nGizmoAxis = -1;
    DrawItem gizmoStartItem{};
    std::vector<std::pair<int, DrawItem>> vGizmoStartItems;
    POINT selectionStart{};
    bool bSelectingRect = false, bSelectionAdd = false, bSelectionToggle = false;
    std::vector<int> vSelectionBeforeDrag;
    ULONGLONG nCameraTick = 0;
    POINT rightStart{};
    bool bRightUsed = false, bOrbitPivot = false;
    int nHierarchyDrag = -1;
    POINT hierarchyDragStart{};
    bool bHierarchyDragging = false;
    int nDockLeft = 210, nDockRight = 430, nDockProject = 210;
    int nInspectorScroll = 0;
    enum class LayoutDrag { None, Left, Right, Project } layoutDrag = LayoutDrag::None;

    void TickCamera() {
        const ULONGLONG now = GetTickCount64();
        const float seconds = nCameraTick ? static_cast<float>(now - nCameraTick) / 1000.0f : 0.0f;
        nCameraTick = now;
        if (drag != Drag::Orbit || bOrbitPivot || GetCapture() != viewport ||
            GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) != window)
            return;
        auto down = [](int key) {
            return (GetAsyncKeyState(key) & 0x8000) ? 1.0f : 0.0f;
        };
        if (FlyCamera(camera, down('W') - down('S'), down('D') - down('A'), down('E') - down('Q'), seconds,
                      down(VK_SHIFT) != 0)) {
            bRightUsed = true;
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
        vSelectedIndices = std::move(indices);
        nSelected = std::find(vSelectedIndices.begin(), vSelectedIndices.end(), active) != vSelectedIndices.end()
                       ? active
                       : (vSelectedIndices.empty() ? -1 : vSelectedIndices.back());
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
    void ShowViewDropdown(int id);
    void SetDrawMode(int index);
    void SetGameAspect(int index);
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
