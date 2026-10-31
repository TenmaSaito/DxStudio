// Editor docking layout: main window chrome, panels, splitters and floating panels.

#include "EditorWindow.h"

namespace dxs::editor {

void Editor::ResizeDock(LayoutDrag drag, POINT point) {
    RECT client{};
    GetClientRect(window, &client);
    const int width = static_cast<int>(client.right), height = static_cast<int>(client.bottom);
    const int x = static_cast<int>(point.x), y = static_cast<int>(point.y);
    if (drag == LayoutDrag::Left)
        nDockLeft = std::clamp(x, 160, std::max(160, width - 500));
    else if (drag == LayoutDrag::Right)
        nDockRight = std::clamp(width - x, 240, std::max(240, width - 400));
    else if (drag == LayoutDrag::Project)
        nDockProject = std::clamp(height - theme::STATUS_BAR_HEIGHT - y, 120, std::max(120, height - 220));
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
    // Areas a floating panel passed over must repaint, including owner-drawn children.
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

void Editor::SelectViewTab(bool game) {
    bGameView = game;
    for (HWND tab : viewTabs) InvalidateRect(tab, nullptr, FALSE);
    LayoutPanel(1);
    SetStatus(bGameView ? L"Game view: rendered through the first active Camera component."
                       : L"Scene view: RMB + WASD/QE fly | Alt + RMB orbit | MMB pan | W/E/R tools");
}

int Editor::PanelSlot(int panelIndex) const {
    for (int slot = 0; slot < SLOT_COUNT; ++slot)
        if (aSlotPanel[slot] == panelIndex) return slot;
    return panelIndex;
}

RECT Editor::DockSlotRect(int slot) const {
    RECT client{};
    GetClientRect(window, &client);
    const int top = TOP_CHROME_HEIGHT + 3, bottom = static_cast<int>(client.bottom) - theme::STATUS_BAR_HEIGHT;
    const int w = static_cast<int>(client.right);
    const int left = std::clamp(nDockLeft, 160, std::max(160, w / 2));
    const int right = w - std::clamp(nDockRight, 240, std::max(240, w / 2));
    const int lower = bottom - std::clamp(nDockProject, 120, std::max(120, bottom - top - 140));
    RECT rect{};
    switch (slot) {
    case SLOT_LEFT: rect = {0, top, left, lower}; break;
    case SLOT_RIGHT: rect = {right, top, w, bottom}; break;
    case SLOT_BOTTOM: rect = {0, lower, right, bottom}; break;
    default: rect = {left, top, right, lower}; break;
    }
    MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}

int Editor::DockSlotAt(POINT screen) const {
    for (const int slot : {SLOT_LEFT, SLOT_RIGHT, SLOT_BOTTOM, SLOT_CENTER}) {
        const RECT rect = DockSlotRect(slot);
        if (PtInRect(&rect, screen)) return slot;
    }
    return -1;
}

// Unity / UE style drop preview: a translucent accent rectangle over the slot.
void Editor::ShowDockPreview(int slot) {
    if (slot < 0) {
        if (dockPreview) ShowWindow(dockPreview, SW_HIDE);
        return;
    }
    if (!dockPreview) {
        WNDCLASSEXW previewClass{sizeof(previewClass)};
        previewClass.lpfnWndProc = DefWindowProcW;
        previewClass.hInstance = instance;
        previewClass.hbrBackground = theme::Brush(theme::COLOR_ACCENT);
        previewClass.lpszClassName = L"DxStudioDockPreview";
        RegisterClassExW(&previewClass);
        dockPreview = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                                      previewClass.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, window, nullptr, instance, nullptr);
        SetLayeredWindowAttributes(dockPreview, 0, 90, LWA_ALPHA);
    }
    const RECT rect = DockSlotRect(slot);
    SetWindowPos(dockPreview, HWND_TOPMOST, rect.left + 4, rect.top + 4, rect.right - rect.left - 8, rect.bottom - rect.top - 8,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// Docks a floating panel into slot; the panel that occupied the slot moves
// to the slot the dropped panel came from.
void Editor::DockPanelInSlot(int panelIndex, int slot) {
    ShowDockPreview(-1);
    const int fromSlot = PanelSlot(panelIndex);
    if (slot >= 0 && slot != fromSlot) {
        std::swap(aSlotPanel[slot], aSlotPanel[fromSlot]);
        // Side columns keep their content width when panels trade sides.
        if ((slot == SLOT_LEFT && fromSlot == SLOT_RIGHT) || (slot == SLOT_RIGHT && fromSlot == SLOT_LEFT)) std::swap(nDockLeft, nDockRight);
    }
    if (panels[panelIndex].floating) FloatPanel(panelIndex); // toggles back to docked
    else Layout();
    const std::wstring message = std::wstring(PANEL_NAMES[panelIndex]) + L" docked to the " + DOCK_SLOT_NAMES[PanelSlot(panelIndex)] + L" slot.";
    SetStatus(message.c_str());
}

void Editor::SetDrawMode(int index) {
    drawMode = static_cast<DrawMode>(std::clamp(index, 0, 2));
    SetWindowTextW(drawModeButton, DRAW_MODE_NAMES[static_cast<int>(drawMode)]);
    renderer.SetDrawMode(drawMode);
    InvalidateRect(drawModeButton, nullptr, FALSE);
    const std::wstring message = std::wstring(L"Scene draw mode: ") + DRAW_MODE_NAMES[static_cast<int>(drawMode)];
    SetStatus(message.c_str());
}

void Editor::SetGameAspect(int index) {
    nGameAspect = std::clamp(index, 0, ASPECT_COUNT - 1);
    SetWindowTextW(aspectButton, ASPECT_NAMES[nGameAspect]);
    InvalidateRect(aspectButton, nullptr, FALSE);
    LayoutPanel(1);
}

void Editor::SelectBottomTab(int tab) {
    nBottomTab = std::clamp(tab, 0, BOTTOM_TAB_COUNT - 1);
    for (HWND t : bottomTabs)
        InvalidateRect(t, nullptr, FALSE);
    LayoutPanel(3);
}

void Editor::Layout() {
    if (!bShellReady || bArranging || IsIconic(window)) return;
    bArranging = true;
    RECT r{};
    GetClientRect(window, &r);
    const int w = r.right, h = r.bottom, top = TOP_CHROME_HEIGHT + 3, bottom = theme::STATUS_BAR_HEIGHT, gap = 3;
    const int header = theme::PANEL_HEADER_HEIGHT;
    // Geometry is computed per dock slot; aSlotPanel decides which panel fills it.
    auto docked = [&](int slot) {
        const auto& panel = SlotPanel(slot);
        return !panel.floating && !panel.hidden;
    };
    const int left = docked(SLOT_LEFT) ? std::clamp(nDockLeft, 160, std::max(160, w / 2)) : 0;
    const int right = docked(SLOT_RIGHT) ? std::clamp(nDockRight, 240, std::max(240, w / 2)) : 0;
    const int projectHeight =
        docked(SLOT_BOTTOM) ? (SlotPanel(SLOT_BOTTOM).collapsed ? header : std::clamp(nDockProject, 120, std::max(120, h - top - bottom - 140))) : 0;
    const int lower = h - bottom - projectHeight;
    if (docked(SLOT_LEFT))
        MoveWindow(SlotPanel(SLOT_LEFT).window, gap, top, left - gap,
                   SlotPanel(SLOT_LEFT).collapsed ? header : std::max(header, lower - top - gap), TRUE);
    if (docked(SLOT_RIGHT))
        MoveWindow(SlotPanel(SLOT_RIGHT).window, w - right, top, right - gap,
                   SlotPanel(SLOT_RIGHT).collapsed ? header : std::max(header, h - top - bottom), TRUE);
    if (docked(SLOT_CENTER))
        MoveWindow(SlotPanel(SLOT_CENTER).window, left + gap, top, std::max(1, w - left - right - gap * 2),
                   SlotPanel(SLOT_CENTER).collapsed ? header : std::max(header, lower - top - gap), TRUE);
    if (docked(SLOT_BOTTOM))
        MoveWindow(SlotPanel(SLOT_BOTTOM).window, gap, lower, std::max(1, w - right - gap * 2), projectHeight, TRUE);
    const bool showLeft = docked(SLOT_LEFT) && docked(SLOT_CENTER),
               showRight = docked(SLOT_RIGHT) && (docked(SLOT_CENTER) || docked(SLOT_BOTTOM)),
               showProject = docked(SLOT_BOTTOM) && (docked(SLOT_LEFT) || docked(SLOT_CENTER));
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
    bArranging = false;
}

void Editor::LayoutPanel(int index) {
    if (!bShellReady) return;
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
    const int scrollOffset = index == 2 ? nInspectorScroll : 0;
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
            place(gizmoButtons[i], 6 + i * 31, header + 3, 30, 24, !bGameView);
        for (int i = 0; i < 2; ++i)
            place(projectionButtons[i], std::max(110, w - 6 - (2 - i) * 84), header + 3, 82, 24, !bGameView);
        const int viewTop = header + 30;
        place(drawModeButton, 104, header + 3, 190, 24, !bGameView);
        place(aspectButton, 6, header + 3, 130, 24, bGameView);
        // Game view: letterbox the viewport to the chosen aspect ratio.
        RECT view{0, viewTop, w, std::max(viewTop + 1, h)};
        const float ratio = bGameView ? ASPECT_RATIOS[nGameAspect] : 0.0f;
        if (ratio > 0.0f) {
            const int areaW = w, areaH = std::max(1, h - viewTop);
            int viewW = areaW, viewH = static_cast<int>(areaW / ratio);
            if (viewH > areaH) {
                viewH = areaH;
                viewW = static_cast<int>(areaH * ratio);
            }
            view = {(areaW - viewW) / 2, viewTop + (areaH - viewH) / 2, (areaW - viewW) / 2 + viewW, viewTop + (areaH - viewH) / 2 + viewH};
        }
        place(viewport, view.left, view.top, std::max<LONG>(1, view.right - view.left), std::max<LONG>(1, view.bottom - view.top));
        if (bRendererReady && expanded && !p.hidden && w > 2 && h > viewTop + 1) {
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
        place(projectPanel, 0, header, w, std::max(1, h - header), nBottomTab == 0);
        place(consolePanel, 0, header, w, std::max(1, h - header), nBottomTab == 1);
    } else {
        // Unity Inspector metrics (UnityCsReference EditorGUI.cs), scaled by DPI:
        // line 20, vertical spacing 2, indent 15 (+3 padding), label width
        // max(width * 0.45 - 40, 120), sub-label spacing 4, header icon 24.
        const int dpi = static_cast<int>(GetDpiForWindow(p.window));
        auto S = [dpi](int value) { return MulDiv(value, dpi, 96); };
        const int line = S(20), gap = S(2), box = S(19), indent = S(18), padRight = S(6);
        const int labelWidth = std::max(static_cast<int>(w * 0.45f) - S(40), S(120));
        const int fieldX = indent + labelWidth, fieldW = std::max(S(40), w - fieldX - padRight);
        const int titlebar = S(22);
        // GameObject header: [icon] [x] [name.....]  /  Tag [Untagged v]  Layer [Default v]
        const int y0 = header + S(6);
        place(inspectorIcon, S(6), y0, S(24), S(24));
        place(inspectorEnabled, S(36), y0 + S(3), S(18), S(18));
        place(edits[0], S(58), y0 + S(2), w - S(58) - padRight, box);
        const int tagY = y0 + S(28);
        const int tagLabelW = S(30), layerLabelW = S(38);
        const int popupW = std::max(S(40), (w - S(8) - tagLabelW - layerLabelW - S(6) - padRight) / 2);
        place(inspectorTagLabel, S(8), tagY, tagLabelW, line);
        place(inspectorTagValue, S(8) + tagLabelW, tagY, popupW, box);
        place(inspectorLayerLabel, S(8) + tagLabelW + popupW + S(6), tagY, layerLabelW, line);
        place(inspectorLayerValue, S(8) + tagLabelW + popupW + S(6) + layerLabelW, tagY, popupW, box);
        place(inspectorHint, 0, 0, 1, 1, false); // Unity has no hint line; values apply on Enter
        nInspectorHeaderBottom = tagY + line + S(6);
        int y = nInspectorHeaderBottom + S(2);
        auto rowLabel = [&](HWND label, int rowY, bool visible) { place(label, indent, rowY, labelWidth - S(4), line, visible); };
        // A Vector3 / Color row: sub-labels and fields share the field column.
        auto multiField = [&](int firstField, int count, int rowY, bool visible) {
            const int subGap = S(4), miniLabel = S(13);
            const int cell = std::max(S(30), (fieldW - subGap * (count - 1)) / count);
            for (int a = 0; a < count; ++a) {
                const int x = fieldX + a * (cell + subGap);
                place(labels[firstField + a], x, rowY, miniLabel, line, visible);
                place(edits[firstField + a], x + miniLabel, rowY, cell - miniLabel, box, visible);
            }
        };
        // Transform
        SetWindowTextW(groupLabels[0], L"Transform");
        theme::SetButtonExpanded(groupLabels[0], aSectionOpen[0]);
        place(groupLabels[0], 0, y, w, titlebar);
        y += titlebar + gap;
        for (int row = 0; row < 3; ++row) {
            rowLabel(rowLabels[row], y, aSectionOpen[0]);
            multiField(2 + row * 3, 3, y, aSectionOpen[0]);
            if (aSectionOpen[0]) y += line + gap;
        }
        y += S(4);
        // Renderer component (IRender): colour, material and texture.
        const bool validEntity = nSelected >= 0 && static_cast<size_t>(nSelected) < scene.vEntities.size();
        const auto rendererKind = validEntity && vSelectedIndices.size() == 1 ? scene.vEntities[nSelected].renderer : Entity::RendererKind::None;
        const bool hasRenderer = rendererKind != Entity::RendererKind::None;
        SetWindowTextW(groupLabels[1], rendererKind == Entity::RendererKind::Mesh    ? L"Mesh Renderer"
                                       : rendererKind == Entity::RendererKind::Model ? L"Model Renderer"
                                       : rendererKind == Entity::RendererKind::Image ? L"Image"
                                                                                     : L"Polygon Renderer");
        theme::SetButtonExpanded(groupLabels[1], aSectionOpen[1]);
        place(groupLabels[1], 0, y, w, titlebar, hasRenderer);
        const bool showRenderer = hasRenderer && aSectionOpen[1];
        if (hasRenderer) y += titlebar + gap;
        // Mesh (Unity: Mesh Filter > Mesh) with an object-picker button.
        const bool showMesh = showRenderer && (rendererKind == Entity::RendererKind::Mesh || rendererKind == Entity::RendererKind::Model);
        rowLabel(meshLabel, y, showMesh);
        place(meshEdit, fieldX, y, std::max(S(20), fieldW - box - S(2)), box, showMesh);
        place(meshPick, fieldX + fieldW - box, y, box, box, showMesh);
        if (showMesh) y += line + gap;
        place(colorLabel, indent, y, labelWidth - S(4), line, showRenderer);
        multiField(11, 4, y, showRenderer);
        if (showRenderer) y += line + gap;
        rowLabel(materialLabel, y, showRenderer);
        place(materialEdit, fieldX, y, fieldW, box, showRenderer);
        if (showRenderer) y += line + gap;
        rowLabel(textureLabel, y, showRenderer);
        place(textureEdit, fieldX, y, fieldW, box, showRenderer);
        if (showRenderer) y += line + gap;
        if (hasRenderer) y += S(4);
        // Script / Camera components: one titlebar and property rows each.
        place(groupLabels[2], 0, 0, 1, 1, false);
        for (auto& view : vComponentViews) {
            const bool open = !collapsedComponents[view.sName];
            theme::SetButtonExpanded(view.hHeader, open);
            place(view.hHeader, 0, y, w, titlebar);
            y += titlebar + gap;
            if (view.hScriptLabel) {
                rowLabel(view.hScriptLabel, y, open);
                place(view.hScriptValue, fieldX, y, fieldW, box, open);
                if (open) y += line + gap;
            }
            for (auto& row : view.vRows) {
                if (!row.hValue) {
                    place(row.hLabel, indent, y, w - indent - padRight, line * 2, open); // warning text
                    if (open) y += line * 2 + gap;
                    continue;
                }
                rowLabel(row.hLabel, y, open);
                if (row.bBoolean) place(row.hValue, fieldX, y + S(1), S(18), S(18), open);
                else place(row.hValue, fieldX, y, fieldW, box, open);
                if (open) y += line + gap;
            }
            y += S(4);
        }
        // The legacy list / value editors stay as the hidden model behind the views.
        for (HWND hidden : {componentList, componentFields, componentValue, componentValueApply, componentChoice, componentRemove})
            place(hidden, 0, 0, 1, 1, false);
        // Separator + centred "Add Component" button (Unity: 230 x 25).
        nAddComponentSeparator = y + S(6);
        const int addWidth = std::min(S(230), w - S(20));
        place(componentAdd, (w - addWidth) / 2, y + S(16), addWidth, S(25), validEntity && vSelectedIndices.size() == 1);
        y += S(16) + S(25) + S(12);
        // Keep Apply in a fixed footer instead of letting it disappear
        // below the scrollable component content.
        // Unity has no Apply button: edits apply on Enter or focus loss
        // (Edit > Apply Inspector Changes keeps the explicit command).
        ShowWindow(controls[3], SW_HIDE);
        const int contentBottom = y + 8, visibleHeight = std::max(1, h - header);
        const int maxScroll = std::max(0, contentBottom - visibleHeight);
        const int clamped = std::clamp(nInspectorScroll, 0, maxScroll);
        if (clamped != nInspectorScroll) {
            nInspectorScroll = clamped;
            LayoutPanel(index);
            return;
        }
        SCROLLINFO info{sizeof(info),
                        SIF_RANGE | SIF_PAGE | SIF_POS,
                        0,
                        std::max(0, contentBottom - 1),
                        static_cast<UINT>(visibleHeight),
                        nInspectorScroll,
                        0};
        SetScrollInfo(p.window, SB_VERT, &info, TRUE);
    }
    InvalidateRect(p.window, nullptr, TRUE);
}

} // namespace dxs::editor
