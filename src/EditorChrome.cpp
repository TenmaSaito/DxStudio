// Editor chrome: theme registration, menu bar, tooltips and owner-drawn rows.

#include "EditorWindow.h"

namespace dxs::editor {

void Editor::AddTooltip(HWND control, const wchar_t* text) {
    if (!tooltip || !control) return;
    TTTOOLINFOW info{sizeof(info)};
    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd = GetParent(control);
    info.uId = reinterpret_cast<UINT_PTR>(control);
    info.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
}

void Editor::ApplyTheme() {
    using theme::ButtonRole;
    using theme::Icon;
    auto spec = [](ButtonRole role, Icon icon, COLORREF iconColor = CLR_INVALID, COLORREF background = CLR_INVALID) {
        theme::ButtonSpec value{role, icon, iconColor};
        value.background = background;
        return value;
    };
    const COLORREF sceneBar = theme::COLOR_TAB_ACTIVE;
    theme::SetButtonSpec(controls[0], spec(ButtonRole::DROPDOWN, Icon::PLUS, CLR_INVALID, sceneBar));
    SetWindowTextW(controls[0], L"");
    theme::SetButtonSpec(controls[1], spec(ButtonRole::ICON, Icon::QUAD, theme::COLOR_ICON_CUBE, sceneBar));
    theme::SetButtonSpec(controls[2], spec(ButtonRole::PUSH, Icon::TRASH));
    theme::SetButtonSpec(controls[3], spec(ButtonRole::PRIMARY, Icon::CHECK, CLR_INVALID, theme::COLOR_TAB_STRIP));
    theme::SetButtonSpec(controls[4], spec(ButtonRole::TOOL, Icon::SAVE, CLR_INVALID, theme::COLOR_TOOLBAR));
    theme::SetButtonSpec(controls[5], spec(ButtonRole::TOOL, Icon::LOAD, theme::COLOR_ICON_FOLDER, theme::COLOR_TOOLBAR));
    theme::SetButtonSpec(controls[6], spec(ButtonRole::TOOL, Icon::EXPORT, CLR_INVALID, theme::COLOR_TOOLBAR));
    theme::SetButtonSpec(controls[7], spec(ButtonRole::TOOL, Icon::SHADER, RGB(190, 150, 230), theme::COLOR_TOOLBAR));
    theme::SetButtonSpec(controls[9], spec(ButtonRole::TRANSPORT, Icon::PLAY, CLR_INVALID, theme::COLOR_TOOLBAR));
    theme::SetButtonSpec(controls[10], spec(ButtonRole::TOOL, Icon::ANDROID, RGB(164, 198, 57), theme::COLOR_TOOLBAR));
    theme::SetButtonSpec(pauseButton, spec(ButtonRole::TRANSPORT, Icon::PAUSE, CLR_INVALID, theme::COLOR_TOOLBAR));
    theme::SetButtonSpec(stepButton, spec(ButtonRole::TRANSPORT, Icon::STEP, CLR_INVALID, theme::COLOR_TOOLBAR));
    theme::SetButtonSpec(graphicsButton,
                         spec(ButtonRole::DROPDOWN, Icon::GRAPHICS, theme::COLOR_ICON_CUBE, theme::COLOR_TOOLBAR));
    theme::SetButtonSpec(layoutButton, spec(ButtonRole::DROPDOWN, Icon::LAYOUT, CLR_INVALID, theme::COLOR_TOOLBAR));
    const Icon tools[] = {Icon::MOVE, Icon::ROTATE, Icon::SCALE};
    for (int i = 0; i < 3; ++i)
        theme::SetButtonSpec(gizmoButtons[i], spec(ButtonRole::ICON, tools[i], CLR_INVALID, sceneBar));
    theme::SetButtonSpec(projectionButtons[0], spec(ButtonRole::TOOL, Icon::PERSPECTIVE, CLR_INVALID, sceneBar));
    theme::SetButtonSpec(projectionButtons[1], spec(ButtonRole::TOOL, Icon::ORTHOGRAPHIC, CLR_INVALID, sceneBar));
    theme::SetButtonSpec(inspectorEnabled, spec(ButtonRole::CHECKBOX, Icon::NONE));
    theme::SetButtonSpec(componentAdd, spec(ButtonRole::PRIMARY, Icon::PLUS));
    theme::SetButtonSpec(componentRemove, spec(ButtonRole::PUSH, Icon::TRASH));
    theme::SetButtonSpec(componentValueApply, spec(ButtonRole::PUSH, Icon::NONE));
    for (HWND control : {hierarchy, componentList, componentFields})
        theme::ApplyDarkControl(control);
    theme::ApplyDarkInput(componentChoice);
    for (HWND edit : edits)
        if (edit) SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
    // Unity: drag a field's label left/right to scrub its value.
    for (int field = 2; field < FIELD_COUNT; ++field) {
        if (!labels[field]) continue;
        SetWindowLongPtrW(labels[field], GWL_STYLE, GetWindowLongPtrW(labels[field], GWL_STYLE) | SS_NOTIFY);
        SetWindowSubclass(labels[field], LabelDragSubclassProc, static_cast<UINT_PTR>(field), reinterpret_cast<DWORD_PTR>(this));
    }
    AddTooltip(drawModeButton, L"Scene draw mode");
    AddTooltip(aspectButton, L"Game view aspect ratio");
    AddTooltip(controls[9], L"Play (Ctrl+P)");
    AddTooltip(pauseButton, L"Pause (Ctrl+Shift+P)");
    AddTooltip(stepButton, L"Step one frame (Ctrl+Alt+P)");
    AddTooltip(controls[4], L"Save scene (Ctrl+S)");
    AddTooltip(controls[5], L"Reload scene from disk");
    AddTooltip(controls[6], L"Export the Windows game");
    AddTooltip(controls[7], L"Reload Default.hlsl");
    AddTooltip(controls[10], L"Build Android Debug APK");
    AddTooltip(graphicsButton, L"Graphics API: DirectX 12 / 11 / 9");
    AddTooltip(layoutButton, L"Editor layout");
    AddTooltip(controls[0], L"Create GameObject");
    AddTooltip(controls[1], L"Create 2D world quad");
    AddTooltip(gizmoButtons[0], L"Move tool (W)");
    AddTooltip(gizmoButtons[1], L"Rotate tool (E)");
    AddTooltip(gizmoButtons[2], L"Scale tool (R)");
    AddTooltip(inspectorEnabled, L"Active (visible in the scene)");
}

RECT Editor::BoxInset(HWND control) const {
    if (!control) return {};
    if (control == hierarchySearch) return {20, 2, 4, 2};
    for (HWND edit : edits)
        if (edit == control) return {4, 1, 4, 1};
    if (IsComponentFieldEdit(control)) return {4, 1, 4, 1};
    if (control == materialEdit || control == textureEdit || control == componentValue) return {4, 1, 4, 1};
    if (control == componentList || control == componentFields) return {1, 1, 1, 1};
    return {};
}

void Editor::PaintInputBoxes(HDC dc, HWND panel) {
    const HWND focus = GetFocus();
    auto paintBox = [&](HWND control) {
        if (!control || GetParent(control) != panel || !IsWindowVisible(control)) return;
        const RECT inset = BoxInset(control);
        RECT box{};
        GetWindowRect(control, &box);
        MapWindowPoints(nullptr, panel, reinterpret_cast<POINT*>(&box), 2);
        box.left -= inset.left;
        box.top -= inset.top;
        box.right += inset.right;
        box.bottom += inset.bottom;
        const bool list = control == componentList || control == componentFields;
        theme::RoundFill(dc, box, 3, list ? theme::COLOR_PANEL : theme::COLOR_INPUT,
                         focus == control ? theme::COLOR_FOCUS : theme::COLOR_INPUT_BORDER);
        if (control == hierarchySearch) {
            RECT icon{box.left + 5, box.top + 4, box.left + 17, box.bottom - 4};
            theme::DrawIcon(dc, theme::Icon::SEARCH, icon, theme::COLOR_TEXT_DIM);
        }
    };
    paintBox(hierarchySearch);
    for (HWND edit : edits)
        paintBox(edit);
    for (HWND control : {materialEdit, textureEdit, componentValue, componentList, componentFields})
        paintBox(control);
    for (const auto& view : vComponentViews) {
        paintBox(view.hScriptValue);
        for (const auto& row : view.vRows)
            if (!row.bBoolean) paintBox(row.hValue);
    }
}

theme::Icon Editor::EntityIcon(const Entity& entity) const {
    if (std::find(entity.vComponents.begin(), entity.vComponents.end(), "Camera") != entity.vComponents.end())
        return theme::Icon::CAMERA;
    switch (entity.renderer) {
    case Entity::RendererKind::None: return theme::Icon::GAME_OBJECT;
    case Entity::RendererKind::Model: return theme::Icon::MODEL;
    case Entity::RendererKind::Image: return theme::Icon::IMAGE;
    default: return entity.draw.bIs2D ? theme::Icon::QUAD : theme::Icon::CUBE;
    }
}

void Editor::DrawHierarchyRow(const DRAWITEMSTRUCT& item) {
    HDC dc = item.hDC;
    const RECT rect = item.rcItem;
    const int row = static_cast<int>(item.itemID);
    if (row < 0 || static_cast<size_t>(row) >= vHierarchyRows.size()) {
        theme::Fill(dc, rect, theme::COLOR_PANEL);
        return;
    }
    const int entityIndex = vHierarchyRows[row];
    const auto& entity = scene.vEntities[entityIndex];
    const bool selectedRow = (item.itemState & ODS_SELECTED) != 0;
    const bool focused = GetFocus() == hierarchy;
    const bool hot = row == nHierarchyHot;
    theme::Fill(dc, rect, selectedRow ? (focused ? theme::COLOR_SELECTION : theme::COLOR_SELECTION_INACTIVE)
                                      : hot ? theme::COLOR_ROW_HOVER : theme::COLOR_PANEL);
    if (hot && bHierarchyDragging && entityIndex != nHierarchyDrag) theme::Frame(dc, rect, theme::COLOR_TAB_ACCENT); // drop target
    const int mid = (rect.top + rect.bottom) / 2;
    int x = rect.left + 6 + vHierarchyDepths[row] * 14;
    if (vHierarchyHasChildren[row]) {
        const bool collapsed = entityIndex < static_cast<int>(vCollapsedEntities.size()) && vCollapsedEntities[entityIndex];
        RECT chevron{x, mid - 6, x + 12, mid + 6};
        theme::DrawIcon(dc, collapsed ? theme::Icon::CHEVRON_RIGHT : theme::Icon::CHEVRON_DOWN, chevron,
                        theme::COLOR_TEXT_DIM);
    }
    x += 14;
    const bool active = entity.draw.bVisible;
    const auto icon = EntityIcon(entity);
    RECT iconBox{x, mid - 8, x + 16, mid + 8};
    const COLORREF iconColor =
        icon == theme::Icon::CUBE || icon == theme::Icon::MODEL ? theme::COLOR_ICON_CUBE : theme::COLOR_TEXT;
    theme::DrawIcon(dc, icon, iconBox, active ? iconColor : theme::COLOR_TEXT_DISABLED);
    RECT text{x + 22, rect.top, rect.right - 4, rect.bottom};
    const COLORREF textColor = !active       ? theme::COLOR_TEXT_DISABLED
                               : selectedRow ? theme::COLOR_TEXT_BRIGHT
                                             : theme::COLOR_TEXT;
    theme::DrawLabel(dc, Wide(entity.sName), text, textColor, theme::GetFonts().hRegular,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (entity.vComponents.size() >
        (std::find(entity.vComponents.begin(), entity.vComponents.end(), "Camera") != entity.vComponents.end() ? 1u : 0u)) {
        RECT badge{rect.right - 20, mid - 6, rect.right - 8, mid + 6};
        theme::DrawIcon(dc, theme::Icon::SCRIPT, badge, theme::COLOR_ICON_SCRIPT);
    }
}

void Editor::DrawComponentRow(const DRAWITEMSTRUCT& item) {
    HDC dc = item.hDC;
    const RECT rect = item.rcItem;
    theme::Fill(dc, rect, (item.itemState & ODS_SELECTED) ? theme::COLOR_SELECTION : theme::COLOR_PANEL);
    if (item.itemID == static_cast<UINT>(-1)) return;
    wchar_t text[256]{};
    SendMessageW(item.hwndItem, LB_GETTEXT, item.itemID, reinterpret_cast<LPARAM>(text));
    const std::wstring label = text;
    const bool rendererRow = label.find(L"[IRender]") != std::wstring::npos;
    const auto icon = rendererRow ? theme::Icon::RENDERER : label == L"Camera" ? theme::Icon::CAMERA : theme::Icon::SCRIPT;
    const int mid = (rect.top + rect.bottom) / 2;
    RECT iconBox{rect.left + 6, mid - 7, rect.left + 20, mid + 7};
    theme::DrawIcon(dc, icon, iconBox, rendererRow ? theme::COLOR_ICON_CUBE : theme::COLOR_ICON_SCRIPT);
    RECT textRect{rect.left + 26, rect.top, rect.right - 4, rect.bottom};
    theme::DrawLabel(dc, label, textRect, theme::COLOR_TEXT, theme::GetFonts().hRegular,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void Editor::DrawFieldRow(const DRAWITEMSTRUCT& item) {
    HDC dc = item.hDC;
    const RECT rect = item.rcItem;
    const bool selectedRow = (item.itemState & ODS_SELECTED) != 0;
    theme::Fill(dc, rect, selectedRow ? theme::Blend(theme::COLOR_PANEL, theme::COLOR_SELECTION, 0.55f) : theme::COLOR_PANEL);
    if (item.itemID == static_cast<UINT>(-1) || item.itemID >= vInspectedComponentFields.size()) return;
    const auto& [name, value] = vInspectedComponentFields[item.itemID];
    const int split = rect.left + std::max(70, static_cast<int>(rect.right - rect.left) * 2 / 5);
    RECT nameRect{rect.left + 8, rect.top, split - 4, rect.bottom};
    theme::DrawLabel(dc, Wide(name), nameRect, theme::COLOR_TEXT, theme::GetFonts().hRegular,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    RECT box{split, rect.top + 2, rect.right - 4, rect.bottom - 2};
    theme::RoundFill(dc, box, 2, theme::COLOR_INPUT, theme::COLOR_INPUT_BORDER);
    if (value == "true" || value == "false") {
        RECT check{box.left + 4, box.top + 1, box.left + 4 + (box.bottom - box.top) - 2, box.bottom - 1};
        if (value == "true") theme::DrawIcon(dc, theme::Icon::CHECK, check, theme::COLOR_TEXT_BRIGHT);
        RECT label{check.right + 4, box.top, box.right - 4, box.bottom};
        theme::DrawLabel(dc, Wide(value), label, theme::COLOR_TEXT_DIM, theme::GetFonts().hSmall,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        RECT valueRect{box.left + 5, box.top, box.right - 4, box.bottom};
        theme::DrawLabel(dc, Wide(value), valueRect, theme::COLOR_TEXT_BRIGHT, theme::GetFonts().hRegular,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

void Editor::DrawInspectorStatic(const DRAWITEMSTRUCT& item) {
    HDC dc = item.hDC;
    RECT rect = item.rcItem;
    theme::Fill(dc, rect, theme::COLOR_PANEL);
    if (item.hwndItem == inspectorIcon) {
        const bool valid = nSelected >= 0 && static_cast<size_t>(nSelected) < scene.vEntities.size();
        const auto icon =
            valid && vSelectedIndices.size() == 1 ? EntityIcon(scene.vEntities[nSelected]) : theme::Icon::GAME_OBJECT;
        theme::DrawIcon(dc, icon, rect, valid ? theme::COLOR_ICON_CUBE : theme::COLOR_TEXT_DISABLED);
        return;
    }
    theme::RoundFill(dc, rect, 3, theme::COLOR_BUTTON_PRESSED, theme::COLOR_INPUT_BORDER);
    wchar_t text[64]{};
    GetWindowTextW(item.hwndItem, text, 64);
    RECT label{rect.left + 6, rect.top, rect.right - 18, rect.bottom};
    theme::DrawLabel(dc, text, label, theme::COLOR_TEXT, theme::GetFonts().hRegular,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT arrow{rect.right - 16, rect.top + 6, rect.right - 6, rect.bottom - 6};
    theme::DrawIcon(dc, theme::Icon::DROPDOWN, arrow, theme::COLOR_TEXT_DIM);
}

void Editor::CreatePanels() {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance = instance;
    wc.lpfnWndProc = PanelProc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DxStudioPanel";
    RegisterClassExW(&wc);
    for (int i = 0; i < 4; ++i) {
        auto& p = panels[i];
        p.editor = this;
        p.index = i;
        p.window = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, PANEL_NAMES[i],
                                   WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | (i == 2 ? WS_VSCROLL : 0), 0, 0, 100, 100,
                                   window, nullptr, instance, &p);
        if (i == 2) theme::ApplyDarkControl(p.window); // dark Inspector scrollbar
        // Unity-style tab strip chrome: float / minimize / close icons.
        const theme::Icon icons[] = {theme::Icon::FLOAT, theme::Icon::MINIMIZE, theme::Icon::CLOSE};
        const wchar_t* labels[] = {L"Float", L"Minimize", L"Close"};
        const int ids[] = {ID_PANEL_FLOAT + i, ID_PANEL_MINIMIZE + i, ID_PANEL_HIDE + i};
        for (int b = 0; b < 3; ++b) {
            p.buttons[b] = Make(L"BUTTON", labels[b], BS_OWNERDRAW, ids[b]);
            SetParent(p.buttons[b], p.window);
            theme::ButtonSpec spec{theme::ButtonRole::ICON, icons[b], theme::COLOR_TEXT_DIM};
            spec.background = theme::COLOR_TAB_STRIP;
            theme::SetButtonSpec(p.buttons[b], spec);
        }
    }
    const wchar_t* viewNames[] = {L"Scene", L"Game"};
    const theme::Icon viewIcons[] = {theme::Icon::SCENE, theme::Icon::CAMERA};
    for (int t = 0; t < 2; ++t) {
        viewTabs[t] = Make(L"BUTTON", viewNames[t], BS_OWNERDRAW, ID_VIEW_TAB_FIRST + t);
        SetParent(viewTabs[t], panels[1].window);
        theme::SetButtonSpec(viewTabs[t], {theme::ButtonRole::TAB, viewIcons[t]});
    }
    {
        drawModeButton = Make(L"BUTTON", DRAW_MODE_NAMES[0], BS_OWNERDRAW, ID_DRAW_MODE_MENU);
        aspectButton = Make(L"BUTTON", ASPECT_NAMES[0], BS_OWNERDRAW, ID_ASPECT_MENU);
        theme::ButtonSpec dropdown{theme::ButtonRole::DROPDOWN, theme::Icon::RENDERER};
        dropdown.background = theme::COLOR_TAB_ACTIVE;
        SetParent(drawModeButton, panels[1].window);
        theme::SetButtonSpec(drawModeButton, dropdown);
        dropdown.icon = theme::Icon::CAMERA;
        SetParent(aspectButton, panels[1].window);
        theme::SetButtonSpec(aspectButton, dropdown);
    }
    for (int t = 0; t < BOTTOM_TAB_COUNT; ++t) {
        bottomTabs[t] = Make(L"BUTTON", BOTTOM_TAB_NAMES[t], BS_OWNERDRAW, ID_PANEL_TAB_FIRST + t);
        SetParent(bottomTabs[t], panels[3].window);
        theme::SetButtonSpec(bottomTabs[t], {theme::ButtonRole::TAB, BOTTOM_TAB_ICONS[t]});
    }
    CreateMainMenu();
}

void Editor::CreateMainMenu() {
    HMENU file = CreatePopupMenu(), edit = CreatePopupMenu(), assets = CreatePopupMenu(), objects = CreatePopupMenu();
    HMENU components = CreatePopupMenu(), build = CreatePopupMenu(), windows = CreatePopupMenu(), help = CreatePopupMenu();
    HMENU settings = CreatePopupMenu(), graphics = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_SAVE, L"Save scene\tCtrl+S");
    AppendMenuW(file, MF_STRING, ID_LOAD, L"Reload scene");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_NEW_PROJECT, L"New project...");
    AppendMenuW(file, MF_STRING, ID_OPEN_PROJECT, L"Open project...");
    AppendMenuW(file, MF_STRING, ID_SAVE_PROJECT_AS, L"Save project file as...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_EXPORT, L"Build and export Windows game...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_EXIT, L"Exit");
    AppendMenuW(edit, MF_STRING, ID_DUPLICATE, L"Duplicate\tCtrl+D");
    AppendMenuW(edit, MF_STRING, ID_DELETE, L"Delete\tDel");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, ID_FRAME_SELECTION, L"Frame Selected\tF");
    AppendMenuW(edit, MF_STRING, ID_APPLY, L"Apply Inspector Changes\tEnter");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, ID_BUILD_DEBUG, L"Play\tCtrl+P");
    AppendMenuW(edit, MF_STRING, ID_PAUSE, L"Pause\tCtrl+Shift+P");
    AppendMenuW(edit, MF_STRING, ID_STEP, L"Step\tCtrl+Alt+P");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(graphics, MF_STRING, ID_GRAPHICS_D3D12, L"DirectX 12");
    AppendMenuW(graphics, MF_STRING, ID_GRAPHICS_D3D11, L"DirectX 11");
    AppendMenuW(graphics, MF_STRING, ID_GRAPHICS_D3D9, L"DirectX 9");
    AppendMenuW(settings, MF_POPUP, reinterpret_cast<UINT_PTR>(graphics), L"Graphics API");
    AppendMenuW(settings, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(settings, MF_STRING, ID_CHOOSE_SCRIPT_EDITOR, L"Script editor...");
    AppendMenuW(settings, MF_STRING, ID_AUTOMATIC_SCRIPT_EDITOR, L"Use automatic Visual Studio detection");
    AppendMenuW(settings, MF_STRING, ID_RELOAD_SHADER, L"Reload shader");
    AppendMenuW(settings, MF_STRING, ID_FRONT_VIEW, L"Front orthographic view");
    AppendMenuW(edit, MF_POPUP, reinterpret_cast<UINT_PTR>(settings), L"Project Settings");
    AppendMenuW(assets, MF_STRING, ID_RELOAD_SHADER, L"Reimport / Reload shader");
    AppendMenuW(assets, MF_STRING, ID_CHOOSE_SCRIPT_EDITOR, L"Open selected script editor...");
    AppendMenuW(objects, MF_STRING, ID_ADD_CUBE, L"3D Object / Cube");
    AppendMenuW(objects, MF_STRING, ID_ADD_SPRITE, L"2D Object / World quad");
    AppendMenuW(objects, MF_STRING, ID_ADD_EMPTY, L"Create Empty");
    AppendMenuW(objects, MF_STRING, ID_ADD_MODEL, L"Place X / FBX model...");
    AppendMenuW(objects, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(objects, MF_STRING, ID_FRAME_SELECTION, L"Frame selection\tF");
    AppendMenuW(objects, MF_STRING, ID_DUPLICATE, L"Duplicate\tCtrl+D");
    AppendMenuW(objects, MF_STRING, ID_DELETE, L"Delete selection");
    // Component menu: every registered class, like Unity's Component menu.
    const auto names = RegisteredComponentNames();
    for (size_t i = 0; i < names.size() && ID_ADD_COMPONENT_MENU_FIRST + static_cast<int>(i) <= ID_ADD_COMPONENT_MENU_LAST;
         ++i)
        AppendMenuW(components, MF_STRING, ID_ADD_COMPONENT_MENU_FIRST + static_cast<int>(i),
                    Wide("Add " + names[i]).c_str());
    AppendMenuW(build, MF_STRING, ID_EXPORT, L"Export Windows game...");
    AppendMenuW(build, MF_STRING, ID_BUILD_DEBUG, L"Play in Editor\tCtrl+P");
    AppendMenuW(build, MF_STRING, ID_BUILD_ANDROID, L"Build Android Debug APK");
    AppendMenuW(build, MF_STRING, ID_PLATFORM_INFO, L"Build platforms...");
    for (int i = 0; i < 4; ++i) {
        HMENU submenu = CreatePopupMenu();
        AppendMenuW(submenu, MF_STRING, ID_PANEL_SHOW + i, L"Show / restore");
        AppendMenuW(submenu, MF_STRING, ID_PANEL_FLOAT + i, L"Float / dock");
        AppendMenuW(submenu, MF_STRING, ID_PANEL_MINIMIZE + i, L"Minimize / expand");
        AppendMenuW(submenu, MF_STRING, ID_PANEL_HIDE + i, L"Hide");
        AppendMenuW(windows, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), PANEL_NAMES[i]);
    }
    AppendMenuW(windows, MF_STRING, ID_SHOW_CONSOLE, L"Console\tCtrl+Shift+C");
    AppendMenuW(windows, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(windows, MF_STRING, ID_SAVE_LAYOUT, L"Save current layout");
    AppendMenuW(windows, MF_STRING, ID_LOAD_LAYOUT, L"Load saved layout");
    AppendMenuW(windows, MF_STRING, ID_RESET_LAYOUT, L"Reset layout");
    AppendMenuW(help, MF_STRING, ID_OPEN_README, L"DxStudio manual (README)");
    AppendMenuW(help, MF_STRING, ID_PLATFORM_INFO, L"Build platforms...");
    AppendMenuW(help, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(help, MF_STRING, ID_ABOUT, L"About DxStudio");
    const HMENU popups[MENU_COUNT] = {file, edit, assets, objects, components, build, windows, help};
    for (int m = 0; m < MENU_COUNT; ++m) {
        menuPopups[m] = popups[m];
        menuButtons[m] = Make(L"BUTTON", MENU_NAMES[m], BS_OWNERDRAW, ID_MENU_BAR_FIRST + m);
        theme::SetButtonSpec(menuButtons[m], {theme::ButtonRole::MENU_BAR});
    }
}

void Editor::OpenMainMenu(int index) {
    if (index < 0 || index >= MENU_COUNT) return;
    UpdateMenuChecks();
    RECT r{};
    GetWindowRect(menuButtons[index], &r);
    SendMessageW(menuButtons[index], BM_SETSTATE, TRUE, 0);
    const int command =
        TrackPopupMenu(menuPopups[index], TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, r.left, r.bottom, 0, window, nullptr);
    SendMessageW(menuButtons[index], BM_SETSTATE, FALSE, 0);
    if (command) SendMessageW(window, WM_COMMAND, MAKEWPARAM(command, BN_CLICKED), 0);
}

void Editor::UpdateMenuChecks() {
    const auto api = projectSettings.graphicsApi;
    const int apiCommand = api == GraphicsApi::DIRECT3D12   ? ID_GRAPHICS_D3D12
                           : api == GraphicsApi::DIRECT3D11 ? ID_GRAPHICS_D3D11
                                                            : ID_GRAPHICS_D3D9;
    for (HMENU menu : menuPopups) {
        CheckMenuRadioItem(menu, ID_GRAPHICS_D3D12, ID_GRAPHICS_D3D9, apiCommand, MF_BYCOMMAND);
        EnableMenuItem(menu, ID_PAUSE, MF_BYCOMMAND | (bPlaying ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(menu, ID_PAUSE, MF_BYCOMMAND | (bPaused ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menu, ID_BUILD_DEBUG, MF_BYCOMMAND | (bPlaying ? MF_CHECKED : MF_UNCHECKED));
    }
}

// Scene "Shaded" and Game "Free Aspect" dropdowns of the view toolbar.
void Editor::ShowViewDropdown(int id) {
    const bool drawModes = id == ID_DRAW_MODE_MENU;
    HWND button = drawModes ? drawModeButton : aspectButton;
    RECT r{};
    GetWindowRect(button, &r);
    HMENU menu = CreatePopupMenu();
    const int count = drawModes ? 3 : ASPECT_COUNT;
    for (int i = 0; i < count; ++i)
        AppendMenuW(menu, MF_STRING, (drawModes ? ID_DRAW_MODE_FIRST : ID_ASPECT_FIRST) + i, drawModes ? DRAW_MODE_NAMES[i] : ASPECT_NAMES[i]);
    const int first = drawModes ? ID_DRAW_MODE_FIRST : ID_ASPECT_FIRST;
    CheckMenuRadioItem(menu, first, first + count - 1, first + (drawModes ? static_cast<int>(drawMode) : nGameAspect), MF_BYCOMMAND);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, r.left, r.bottom, 0, window, nullptr);
    DestroyMenu(menu);
    if (command) SendMessageW(window, WM_COMMAND, MAKEWPARAM(command, BN_CLICKED), 0);
}

void Editor::ShowToolbarDropdown(int id) {
    HWND button = id == ID_GRAPHICS_MENU ? graphicsButton : layoutButton;
    RECT r{};
    GetWindowRect(button, &r);
    HMENU menu = CreatePopupMenu();
    if (id == ID_GRAPHICS_MENU) {
        AppendMenuW(menu, MF_STRING, ID_GRAPHICS_D3D12, L"DirectX 12  (Direct3D 12)");
        AppendMenuW(menu, MF_STRING, ID_GRAPHICS_D3D11, L"DirectX 11  (Direct3D 11)");
        AppendMenuW(menu, MF_STRING, ID_GRAPHICS_D3D9, L"DirectX 9  (Direct3D 9, Shader Model 3)");
        const auto api = projectSettings.graphicsApi;
        CheckMenuRadioItem(menu, ID_GRAPHICS_D3D12, ID_GRAPHICS_D3D9,
                           api == GraphicsApi::DIRECT3D12   ? ID_GRAPHICS_D3D12
                           : api == GraphicsApi::DIRECT3D11 ? ID_GRAPHICS_D3D11
                                                            : ID_GRAPHICS_D3D9,
                           MF_BYCOMMAND);
    } else {
        AppendMenuW(menu, MF_STRING, ID_SAVE_LAYOUT, L"Save Layout");
        AppendMenuW(menu, MF_STRING, ID_LOAD_LAYOUT, L"Load Layout");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, ID_RESET_LAYOUT, L"Revert Factory Settings");
    }
    const int command =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, r.right, r.bottom, 0, window, nullptr);
    DestroyMenu(menu);
    if (command) SendMessageW(window, WM_COMMAND, MAKEWPARAM(command, BN_CLICKED), 0);
}

} // namespace dxs::editor
