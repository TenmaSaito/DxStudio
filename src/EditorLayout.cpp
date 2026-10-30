// Editor docking layout: main window chrome, panels, splitters and floating panels.

#include "EditorWindow.h"

namespace dxs::editor {

void Editor::ResizeDock(LayoutDrag drag, POINT point) {
    RECT client{};
    GetClientRect(window, &client);
    const int width = static_cast<int>(client.right), height = static_cast<int>(client.bottom);
    const int x = static_cast<int>(point.x), y = static_cast<int>(point.y);
    if (drag == LayoutDrag::Left)
        dockLeft = std::clamp(x, 160, std::max(160, width - 500));
    else if (drag == LayoutDrag::Right)
        dockRight = std::clamp(width - x, 240, std::max(240, width - 400));
    else if (drag == LayoutDrag::Project)
        dockProject = std::clamp(height - theme::STATUS_BAR_HEIGHT - y, 120, std::max(120, height - 220));
    Layout();
}

void Editor::FloatPanel(int index) {
    auto& p = panels[index];
    RECT old{};
    GetWindowRect(p.window, &old);
    p.floating = !p.floating;
    p.hidden = false;
    p.collapsed = false;
    if (p.floating) {
        SetParent(p.window, nullptr);
        SetWindowLongPtrW(p.window, GWL_STYLE,
                          WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN);
        SetWindowLongPtrW(p.window, GWL_EXSTYLE, WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT);
        SetWindowLongPtrW(p.window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(window));
        const int width = index == 1 ? 800 : index == 3 ? 700 : index == 2 ? 360 : 300;
        SetWindowPos(p.window, HWND_TOP, old.left, old.top, width, index == 3 ? 350 : 620, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        theme::ApplyDarkTitleBar(p.window);
    } else {
        ShowWindow(p.window, SW_RESTORE);
        SetWindowLongPtrW(p.window, GWL_STYLE, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);
        SetWindowLongPtrW(p.window, GWL_EXSTYLE, WS_EX_CONTROLPARENT);
        SetParent(p.window, window);
        SetWindowPos(p.window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    SetWindowTextW(p.buttons[0], p.floating ? L"Dock" : L"Float");
    theme::SetButtonIcon(p.buttons[0], p.floating ? theme::Icon::DOCK : theme::Icon::FLOAT);
    Layout();
}

void Editor::SelectViewTab(bool game) {
    gameView = game;
    for (HWND tab : viewTabs) InvalidateRect(tab, nullptr, FALSE);
    LayoutPanel(1);
    SetStatus(gameView ? L"Game view: rendered through the first active Camera component."
                       : L"Scene view: RMB + WASD/QE fly | Alt + RMB orbit | MMB pan | W/E/R tools");
}

void Editor::SelectBottomTab(int tab) {
    bottomTab = std::clamp(tab, 0, BOTTOM_TAB_COUNT - 1);
    for (HWND t : bottomTabs)
        InvalidateRect(t, nullptr, FALSE);
    LayoutPanel(3);
}

void Editor::Layout() {
    if (!shellReady || arranging || IsIconic(window)) return;
    arranging = true;
    RECT r{};
    GetClientRect(window, &r);
    const int w = r.right, h = r.bottom, top = TOP_CHROME_HEIGHT + 3, bottom = theme::STATUS_BAR_HEIGHT, gap = 3;
    const int header = theme::PANEL_HEADER_HEIGHT;
    auto docked = [&](int i) {
        return !panels[i].floating && !panels[i].hidden;
    };
    const int left = docked(0) ? std::clamp(dockLeft, 160, std::max(160, w / 2)) : 0;
    const int right = docked(2) ? std::clamp(dockRight, 240, std::max(240, w / 2)) : 0;
    const int projectHeight =
        docked(3) ? (panels[3].collapsed ? header : std::clamp(dockProject, 120, std::max(120, h - top - bottom - 140))) : 0;
    const int lower = h - bottom - projectHeight;
    if (docked(0))
        MoveWindow(panels[0].window, gap, top, left - gap, panels[0].collapsed ? header : std::max(header, lower - top - gap),
                   TRUE);
    if (docked(2))
        MoveWindow(panels[2].window, w - right, top, right - gap,
                   panels[2].collapsed ? header : std::max(header, h - top - bottom), TRUE);
    if (docked(1))
        MoveWindow(panels[1].window, left + gap, top, std::max(1, w - left - right - gap * 2),
                   panels[1].collapsed ? header : std::max(header, lower - top - gap), TRUE);
    if (docked(3)) MoveWindow(panels[3].window, gap, lower, std::max(1, w - right - gap * 2), projectHeight, TRUE);
    const bool showLeft = docked(0) && docked(1), showRight = docked(2) && (docked(1) || docked(3)),
               showProject = docked(3) && (docked(0) || docked(1));
    for (int i = 0; i < 4; ++i) {
        ShowWindow(panels[i].window, panels[i].hidden ? SW_HIDE : SW_SHOWNA);
        LayoutPanel(i);
    }
    SetWindowPos(splitters[0], HWND_TOP, left - 3, top, 6, std::max(1, lower - top), SWP_NOACTIVATE);
    SetWindowPos(splitters[1], HWND_TOP, w - right - 4, top, 6, std::max(1, h - top - bottom), SWP_NOACTIVATE);
    SetWindowPos(splitters[2], HWND_TOP, gap, lower - 4, std::max(1, w - right - gap * 2), 6, SWP_NOACTIVATE);
    ShowWindow(splitters[0], showLeft ? SW_SHOWNA : SW_HIDE);
    ShowWindow(splitters[1], showRight ? SW_SHOWNA : SW_HIDE);
    ShowWindow(splitters[2], showProject ? SW_SHOWNA : SW_HIDE);

    // Menu bar: text-width buttons after the logo.
    HDC dc = GetDC(window);
    HGDIOBJ oldFont = SelectObject(dc, theme::GetFonts().hRegular);
    int x = 30;
    for (int m = 0; m < MENU_COUNT; ++m) {
        SIZE size{};
        GetTextExtentPoint32W(dc, MENU_NAMES[m], static_cast<int>(wcslen(MENU_NAMES[m])), &size);
        const int width = size.cx + 18;
        MoveWindow(menuButtons[m], x, 1, width, theme::MENU_BAR_HEIGHT - 2, TRUE);
        x += width;
    }
    // Toolbar buttons are as wide as icon + label (+ chevron) at the current DPI.
    auto measure = [&](HWND button, int extra) {
        wchar_t text[64]{};
        GetWindowTextW(button, text, 64);
        SIZE size{};
        GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
        return static_cast<int>(size.cx) + extra;
    };
    constexpr int ICON_TEXT_PADDING = 40, DROPDOWN_PADDING = 56;
    const int widths[] = {measure(controls[4], ICON_TEXT_PADDING),  measure(controls[5], ICON_TEXT_PADDING),
                          measure(layoutButton, DROPDOWN_PADDING),  measure(graphicsButton, DROPDOWN_PADDING),
                          measure(controls[10], ICON_TEXT_PADDING), measure(controls[6], ICON_TEXT_PADDING),
                          measure(controls[7], ICON_TEXT_PADDING)};
    SelectObject(dc, oldFont);
    ReleaseDC(window, dc);

    // Toolbar: file actions left, transport centred, build actions right.
    const int toolY = theme::MENU_BAR_HEIGHT + 5, toolH = 26;
    MoveWindow(controls[4], 8, toolY, widths[0], toolH, TRUE);
    MoveWindow(controls[5], 12 + widths[0], toolY, widths[1], toolH, TRUE);
    const int transportX = std::max(160, w / 2 - 51);
    MoveWindow(controls[9], transportX, toolY, 34, toolH, TRUE);
    MoveWindow(pauseButton, transportX + 34, toolY, 34, toolH, TRUE);
    MoveWindow(stepButton, transportX + 68, toolY, 34, toolH, TRUE);
    int rightX = w - 8;
    auto placeRight = [&](HWND control, int width) {
        rightX -= width;
        MoveWindow(control, rightX, toolY, width, toolH, TRUE);
        rightX -= 4;
    };
    placeRight(layoutButton, widths[2]);
    placeRight(graphicsButton, widths[3]);
    rightX -= 8;
    placeRight(controls[10], widths[4]);
    placeRight(controls[6], widths[5]);
    placeRight(controls[7], widths[6]);
    ShowWindow(controls[8], SW_HIDE);
    MoveWindow(status, 30, h - bottom + 3, std::max(1, w - 340), bottom - 5, TRUE);
    MoveWindow(statusRight, w - 300, h - bottom + 3, 290, bottom - 5, TRUE);
    InvalidateRect(window, nullptr, TRUE);
    arranging = false;
}

void Editor::LayoutPanel(int index) {
    if (!shellReady) return;
    auto& p = panels[index];
    if (IsIconic(p.window)) return;
    RECT r{};
    GetClientRect(p.window, &r);
    const int w = r.right, h = r.bottom;
    const int header = theme::PANEL_HEADER_HEIGHT;
    // Tab strip chrome (right aligned icons).
    const int closeX = std::max(4, w - 24), minimizeX = std::max(4, closeX - 22), floatX = std::max(4, minimizeX - 22);
    MoveWindow(p.buttons[0], floatX, 4, 20, 20, TRUE);
    MoveWindow(p.buttons[1], minimizeX, 4, 20, 20, TRUE);
    MoveWindow(p.buttons[2], closeX, 4, 20, 20, TRUE);
    const bool expanded = !p.collapsed;
    const int scrollOffset = index == 2 ? inspectorScroll : 0;
    auto place = [&](HWND control, int x, int y, int width, int height, bool visible = true) {
        if (!control) return;
        const RECT inset = BoxInset(control);
        MoveWindow(control, x + inset.left, y - scrollOffset + inset.top,
                   std::max(1, width - static_cast<int>(inset.left + inset.right)),
                   std::max(1, height - static_cast<int>(inset.top + inset.bottom)), TRUE);
        ShowWindow(control, expanded && visible ? SW_SHOWNA : SW_HIDE);
    };
    if (index == 0) {
        // Unity Hierarchy: [+ v] [2D] [search.....], scene row, tree, footer.
        place(controls[0], 6, header + 4, 44, 24);
        place(controls[1], 54, header + 4, 32, 24);
        place(hierarchySearch, 92, header + 4, w - 100, 24);
        const int listTop = header + 32 + HIERARCHY_SCENE_ROW;
        place(hierarchy, 0, listTop, w, std::max(1, h - listTop - 34));
        place(controls[2], 6, h - 30, w - 12, 24);
    } else if (index == 1) {
        // Unity: Scene | Game tabs. The tool strip only exists in the Scene view.
        for (int t = 0; t < 2; ++t) {
            MoveWindow(viewTabs[t], t * 100, 2, 100, header - 2, TRUE);
            ShowWindow(viewTabs[t], SW_SHOWNA);
        }
        for (int i = 0; i < 3; ++i)
            place(gizmoButtons[i], 6 + i * 31, header + 3, 30, 24, !gameView);
        for (int i = 0; i < 2; ++i)
            place(projectionButtons[i], std::max(110, w - 6 - (2 - i) * 84), header + 3, 82, 24, !gameView);
        const int viewTop = header + 30;
        place(viewport, 0, viewTop, w, std::max(1, h - viewTop));
        if (rendererReady && expanded && !p.hidden && w > 2 && h > viewTop + 1) {
            RECT actual{};
            GetClientRect(viewport, &actual);
            renderer.Resize(actual.right, actual.bottom);
        }
    } else if (index == 3) {
        int tabX = 0;
        for (int t = 0; t < BOTTOM_TAB_COUNT; ++t) {
            MoveWindow(bottomTabs[t], tabX, 2, 108, header - 2, TRUE);
            ShowWindow(bottomTabs[t], SW_SHOWNA);
            tabX += 108;
        }
        place(projectPanel, 0, header, w, std::max(1, h - header), bottomTab == 0);
        place(consolePanel, 0, header, w, std::max(1, h - header), bottomTab == 1);
    } else {
        // Unity-like object header: icon, active state, object name, and
        // metadata strip always precede editable component sections.
        place(inspectorIcon, 10, 35, 26, 26);
        place(inspectorEnabled, 42, 38, 20, 20);
        place(edits[0], 67, 34, w - 77, 24);
        place(inspectorTagLabel, 10, 65, 34, 22);
        place(inspectorTagValue, 46, 64, 90, 22);
        place(inspectorLayerLabel, 144, 65, 42, 22);
        place(inspectorLayerValue, 188, 64, std::max(45, w - 198), 22);
        place(inspectorHint, 10, 90, w - 20, 20);
        int y = 118;
        const wchar_t* names[] = {L"Transform", L"Rendering / Color", L"Components"};
        for (int group = 0; group < 3; ++group) {
            SetWindowTextW(groupLabels[group], names[group]);
            theme::SetButtonExpanded(groupLabels[group], sectionOpen[group]);
            place(groupLabels[group], 0, y, w, 24);
            y += 30;
            if (group == 0) {
                for (int row = 0; row < 3; ++row) {
                    place(rowLabels[row], 14, y + 2, 70, 22, sectionOpen[group]);
                    // Leave a fixed gutter for the row caption and for
                    // every axis caption. This prevents the X/Y/Z labels
                    // and numeric values from colliding in narrow docks.
                    const int fieldStart = 86;
                    const int cell = std::max(43, (w - fieldStart - 10) / 3);
                    for (int a = 0; a < 3; ++a) {
                        const int field = 2 + row * 3 + a, x = fieldStart + a * cell;
                        place(labels[field], x + 3, y + 2, 11, 22, sectionOpen[group]);
                        place(edits[field], x + 15, y, cell - 18, 24, sectionOpen[group]);
                    }
                    if (sectionOpen[group]) y += 28;
                }
            } else if (group == 1) {
                const int cell = (w - 20) / 4;
                for (int a = 0; a < 4; ++a) {
                    const int x = 12 + a * cell;
                    place(labels[11 + a], x + 3, y + 2, 11, 22, sectionOpen[group]);
                    place(edits[11 + a], x + 15, y, cell - 20, 24, sectionOpen[group]);
                }
                if (sectionOpen[group]) y += 28;
                place(materialLabel, 14, y + 2, 70, 22, sectionOpen[group]);
                place(materialEdit, 86, y, w - 96, 24, sectionOpen[group]);
                if (sectionOpen[group]) y += 28;
                place(textureLabel, 14, y + 2, 70, 22, sectionOpen[group]);
                place(textureEdit, 86, y, w - 96, 24, sectionOpen[group]);
                if (sectionOpen[group]) y += 28;
            } else {
                place(componentList, 10, y, w - 20, 60, sectionOpen[group]);
                place(componentFields, 10, y + 66, w - 20, 64, sectionOpen[group]);
                place(componentValue, 10, y + 136, w - 94, 24, sectionOpen[group]);
                place(componentValueApply, w - 80, y + 136, 70, 24, sectionOpen[group]);
                // Unity: wide centred "Add Component" button under the components.
                place(componentChoice, 10, y + 170, w - 20, 180, sectionOpen[group]);
                place(componentAdd, 10, y + 202, (w - 25) / 2, 26, sectionOpen[group]);
                place(componentRemove, 15 + (w - 25) / 2, y + 202, (w - 25) / 2, 26, sectionOpen[group]);
                if (sectionOpen[group]) y += 236;
            }
            y += 4;
        }
        // Keep Apply in a fixed footer instead of letting it disappear
        // below the scrollable component content.
        MoveWindow(controls[3], 10, std::max(29, h - 34), std::max(1, w - 20), 26, TRUE);
        ShowWindow(controls[3], expanded ? SW_SHOWNA : SW_HIDE);
        const int contentBottom = y + 8, visibleHeight = std::max(1, h - 62);
        const int maxScroll = std::max(0, contentBottom - visibleHeight);
        const int clamped = std::clamp(inspectorScroll, 0, maxScroll);
        if (clamped != inspectorScroll) {
            inspectorScroll = clamped;
            LayoutPanel(index);
            return;
        }
        SCROLLINFO info{sizeof(info),
                        SIF_RANGE | SIF_PAGE | SIF_POS,
                        0,
                        std::max(0, contentBottom - 1),
                        static_cast<UINT>(visibleHeight),
                        inspectorScroll,
                        0};
        SetScrollInfo(p.window, SB_VERT, &info, TRUE);
    }
    InvalidateRect(p.window, nullptr, TRUE);
}

} // namespace dxs::editor
