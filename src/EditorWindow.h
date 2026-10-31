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
// Dock slots of the main window. aSlotPanel maps each slot to the panel shown
// there; dropping a floating panel on a slot swaps it with that slot's panel.
constexpr int SLOT_LEFT = 0, SLOT_CENTER = 1, SLOT_RIGHT = 2, SLOT_BOTTOM = 3, SLOT_COUNT = 4;
inline const wchar_t* DOCK_SLOT_NAMES[SLOT_COUNT] = {L"left", L"centre", L"right", L"bottom"};
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

// Inspector property row (Unity: label column + field column).
struct InspectorFieldRow {
    std::string sField;
    HWND hLabel = nullptr;
    HWND hValue = nullptr; // EDIT, or owner-drawn check box for bool; null for a warning row
    bool bBoolean = false;
    bool bChecked = false;
    bool bNumeric = false; // float / int: mouse drag and wheel adjust the value
    bool bInteger = false;
};
// Unreal-style numeric field: drag inside the box (or wheel) to change the
// value; a click without dragging enters text editing with the value selected.
// uIdSubclass: Transform/Color field index (2..14), or FIELD_SCRUB_COMPONENT + control id.
constexpr UINT_PTR FIELD_SCRUB_COMPONENT = 0x10000;
LRESULT CALLBACK NumericFieldSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
// One attached component shown as a titlebar plus property rows.
struct InspectorComponentView {
    size_t nComponentIndex = 0;
    std::string sName;
    HWND hHeader = nullptr;
    HWND hScriptLabel = nullptr;
    HWND hScriptValue = nullptr;
    std::vector<InspectorFieldRow> vRows;
};

struct Editor : public IPlayModeCallbacks, public IGraphicsCallbacks {
    // ---- Unity-style Inspector component sections --------------------------
    std::vector<InspectorComponentView> vComponentViews;
    std::string sComponentViewSignature;
    std::unordered_map<std::string, bool> collapsedComponents;
    HWND colorLabel{};
    int nInspectorHeaderBottom = 0;
    int nAddComponentSeparator = 0;
    void RebuildComponentViews();
    void UpdateComponentViewValues();
    InspectorFieldRow* FindFieldRow(int controlId, InspectorComponentView** ppView);
    bool IsComponentFieldEdit(HWND control) const;
    void CommitComponentField(int controlId, bool bToggle);
    bool SelectComponentForWindow(HWND control);
    bool IsOverTitlebarMenu(HWND header) const;
    void ShowTitlebarMenu(HWND header);
    void ShowAddComponentMenu();
    // Unity applies Inspector edits when a field loses focus (no Apply button).
    bool bQuietApply = false;
    void ReportApplyError(const std::string& message);
    bool IsInspectorEdit(HWND control) const;
    // True when a Transform / Rendering field holds typed but unapplied text.
    bool InspectorHasPendingEdits() const;
    // ---- Real-time feedback while editing ----------------------------------
    // WM_TIMER is only delivered when the queue is empty, so a stream of mouse
    // moves would starve it. Drags render directly, at most every ~15 ms.
    ULONGLONG nLastRenderTick = 0;
    void RequestRender();
    // Writes one Transform / Color value (field 2..14) straight to the entity.
    bool SetFieldValue(int field, float fValue);
    // Live apply of a field while it is being typed (valid numbers only).
    void ApplyFieldLive(int field);
    // Updates only the Transform / Color texts (cheap; used during drags).
    void RefreshTransformFields();
    // Mouse adjustment of numeric fields (drag inside the box / wheel).
    int nFieldScrubStartX = 0;
    float fFieldScrubStartValue = 0.0f;
    bool bFieldScrubbing = false;
    bool bFieldPressed = false;
    // Step per pixel (drag) or per wheel notch, before Shift / Alt modifiers.
    float FieldScrubStep(UINT_PTR fieldId, bool bWheel) const;
    // Writes a new numeric value to a Transform field or a component property.
    void SetScrubbedValue(UINT_PTR fieldId, HWND hEdit, float fValue);
    void ApplyOnFocusLoss(HWND control);
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
    // Docking: slot -> panel index, and the translucent drop preview window.
    int aSlotPanel[SLOT_COUNT] = {0, 1, 2, 3};
    HWND dockPreview{};
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
        Debug::Log("Entered Play Mode (" + std::to_string(playScene.vEntities.size()) + " objects).");
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
    int nDockLeft = 210, nDockRight = 480, nDockProject = 210;
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
                                     [&](int i) { return i < 0 || static_cast<size_t>(i) >= scene.vEntities.size(); }),
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
    EditorPanel& SlotPanel(int slot) { return panels[aSlotPanel[slot]]; }
    int PanelSlot(int panelIndex) const;
    // Screen rectangle of a dock slot, used for hit testing and the preview.
    RECT DockSlotRect(int slot) const;
    // Slot under a screen point, or -1 outside the dock area.
    int DockSlotAt(POINT screen) const;
    void ShowDockPreview(int slot);
    void DockPanelInSlot(int panelIndex, int slot);
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
