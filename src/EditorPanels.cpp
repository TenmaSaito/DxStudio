// Child window procedures: hierarchy list, splitters, dock panels and the
// DirectX viewport (mouse picking, gizmo drags, camera navigation).

#include "EditorWindow.h"

namespace dxs::editor {

LRESULT CALLBACK HierarchySubclassProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto* e = reinterpret_cast<Editor*>(data);
    if (!e) return DefSubclassProc(hwnd, message, wp, lp);
    switch (message) {
    case WM_LBUTTONDOWN: {
        const DWORD hit = static_cast<DWORD>(SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lp));
        const int row = HIWORD(hit) ? -1 : static_cast<int>(LOWORD(hit));
        // Clicking the foldout arrow expands / collapses children (Unity).
        if (row >= 0 && static_cast<size_t>(row) < e->vHierarchyRows.size() && e->vHierarchyHasChildren[row]) {
            const int arrowX = 6 + e->vHierarchyDepths[row] * 14;
            const int x = GET_X_LPARAM(lp);
            if (x >= arrowX - 2 && x <= arrowX + 14) {
                const int entity = e->vHierarchyRows[row];
                e->vCollapsedEntities.resize(e->scene.vEntities.size(), 0);
                e->vCollapsedEntities[entity] = !e->vCollapsedEntities[entity];
                e->Populate();
                return 0;
            }
        }
        e->nHierarchyDrag = row >= 0 && static_cast<size_t>(row) < e->vHierarchyRows.size() ? e->vHierarchyRows[row] : -1;
        e->hierarchyDragStart = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        e->bHierarchyDragging = false;
        break;
    }
    case WM_MOUSELEAVE:
        if (e->nHierarchyHot != -1) {
            e->nHierarchyHot = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    case WM_MOUSEMOVE: {
        const DWORD hover = static_cast<DWORD>(SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lp));
        const int hoverRow = HIWORD(hover) ? -1 : static_cast<int>(LOWORD(hover));
        if (hoverRow != e->nHierarchyHot) {
            e->nHierarchyHot = hoverRow;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
        if ((wp & MK_LBUTTON) && e->nHierarchyDrag >= 0 &&
            (std::abs(GET_X_LPARAM(lp) - e->hierarchyDragStart.x) > 5 ||
             std::abs(GET_Y_LPARAM(lp) - e->hierarchyDragStart.y) > 5)) {
            if (!e->bHierarchyDragging) {
                e->bHierarchyDragging = true;
                SetCapture(hwnd);
            }
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        // Some input sources coalesce the mouse-move message into the final
        // button-up.  Treat a meaningful start/end displacement as a drag as
        // well, so hierarchy parenting is not dependent on an intermediate
        // WM_MOUSEMOVE being delivered.
        if ((e->bHierarchyDragging || (e->nHierarchyDrag >= 0 && (std::abs(GET_X_LPARAM(lp) - e->hierarchyDragStart.x) > 5 ||
                                                                std::abs(GET_Y_LPARAM(lp) - e->hierarchyDragStart.y) > 5))) &&
            e->nHierarchyDrag >= 0) {
            const int child = e->nHierarchyDrag;
            const DWORD hit = static_cast<DWORD>(SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lp));
            const int row = HIWORD(hit) ? -1 : static_cast<int>(LOWORD(hit));
            const int parent = row >= 0 && static_cast<size_t>(row) < e->vHierarchyRows.size() ? e->vHierarchyRows[row] : -1;
            std::string error;
            if (!SetEntityParent(e->scene, child, parent, error))
                Error(e->window, error);
            else {
                // Preserve the dragged object as the active selection. The
                // list is rebuilt depth-first so a successful drop is visible
                // immediately as a parent/child indentation change.
                e->nSelected = child;
                e->vSelectedIndices = {child};
                e->Populate();
                e->SetStatus(parent < 0 ? L"Object moved to the hierarchy root."
                                        : L"Parent set by drag and drop. Save the scene to keep it.");
            }
            e->bHierarchyDragging = false;
            e->nHierarchyDrag = -1;
            ReleaseCapture();
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        // Only a capture loss during our own drag cancels it; the list box
        // also takes and releases capture while it tracks a selection click.
        if (e->bHierarchyDragging && reinterpret_cast<HWND>(lp) != hwnd) {
            e->bHierarchyDragging = false;
            e->nHierarchyDrag = -1;
            e->nHierarchyHot = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    case WM_NCDESTROY: RemoveWindowSubclass(hwnd, HierarchySubclassProc, 1); break;
    }
    return DefSubclassProc(hwnd, message, wp, lp);
}

// Unity-style scrubbing: drag a field label horizontally to change the value
// of the matching Inspector field. Shift = 10x, Alt = 0.1x.
LRESULT CALLBACK LabelDragSubclassProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR field, DWORD_PTR data) {
    auto* e = reinterpret_cast<Editor*>(data);
    const int index = static_cast<int>(field);
    switch (message) {
    case WM_SETCURSOR:
        if (IsWindowEnabled(e->edits[index])) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        if (!IsWindowEnabled(e->edits[index])) break;
        const std::wstring text = Text(e->edits[index]);
        try {
            e->fScrubStartValue = std::stof(text);
        } catch (...) {
            return 0;
        }
        e->nScrubField = index;
        POINT cursor{};
        GetCursorPos(&cursor);
        e->nScrubStartX = cursor.x;
        SetCapture(hwnd);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (GetCapture() == hwnd && e->nScrubField == index) {
            POINT cursor{};
            GetCursorPos(&cursor);
            // Per-pixel step: position 0.01, rotation 0.5 degrees, scale 0.01, colour 0.005.
            float step = index >= 11 ? 0.005f : (index >= 5 && index <= 7) ? 0.5f : 0.01f;
            if (GetKeyState(VK_SHIFT) & 0x8000) step *= 10.0f;
            if (GetKeyState(VK_MENU) & 0x8000) step *= 0.1f;
            float value = e->fScrubStartValue + static_cast<float>(cursor.x - e->nScrubStartX) * step;
            if (index >= 11) value = std::clamp(value, 0.0f, 1.0f);
            wchar_t buffer[64];
            swprintf_s(buffer, L"%.6g", static_cast<double>(value));
            SetWindowTextW(e->edits[index], buffer);
            SendMessageW(e->edits[index], EM_SETMODIFY, FALSE, 0); // already applied below
            e->SetFieldValue(index, value);
            e->RequestRender();
            return 0;
        }
        break;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (e->nScrubField == index) {
            e->nScrubField = -1;
            if (message == WM_LBUTTONUP) ReleaseCapture();
            return 0;
        }
        break;
    case WM_NCDESTROY: RemoveWindowSubclass(hwnd, LabelDragSubclassProc, field); break;
    }
    return DefSubclassProc(hwnd, message, wp, lp);
}

LRESULT CALLBACK SplitterSubclassProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR splitter, DWORD_PTR data) {
    auto* e = reinterpret_cast<Editor*>(data);
    if (!e) return DefSubclassProc(hwnd, message, wp, lp);
    const auto drag = splitter == 0   ? Editor::LayoutDrag::Left
                      : splitter == 1 ? Editor::LayoutDrag::Right
                                      : Editor::LayoutDrag::Project;
    switch (message) {
    case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr, splitter == 2 ? IDC_SIZENS : IDC_SIZEWE)); return TRUE;
    case WM_LBUTTONDOWN:
        e->layoutDrag = drag;
        SetCapture(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == hwnd && e->layoutDrag == drag) {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(hwnd, &point);
            ScreenToClient(e->window, &point);
            e->ResizeDock(drag, point);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) {
            ReleaseCapture();
            e->layoutDrag = Editor::LayoutDrag::None;
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (e->layoutDrag == drag) e->layoutDrag = Editor::LayoutDrag::None;
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{};
        GetClientRect(hwnd, &r);
        theme::Fill(dc, r, GetCapture() == hwnd ? theme::COLOR_TAB_ACCENT : theme::COLOR_SEPARATOR);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCDESTROY: RemoveWindowSubclass(hwnd, SplitterSubclassProc, splitter); break;
    }
    return DefSubclassProc(hwnd, message, wp, lp);
}

LRESULT CALLBACK PanelProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto* p = reinterpret_cast<EditorPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        p = static_cast<EditorPanel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
        p->window = hwnd;
    }
    if (!p || !p->editor) return DefWindowProcW(hwnd, message, wp, lp);
    auto* e = p->editor;
    switch (message) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) e->LayoutPanel(p->index);
        return 0;
    case WM_MOUSEWHEEL:
        if (p->index == 2) {
            e->nInspectorScroll = std::max(0, e->nInspectorScroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 48);
            e->LayoutPanel(2);
            return 0;
        }
        break;
    case WM_VSCROLL:
        if (p->index == 2) {
            SCROLLINFO info{sizeof(info), SIF_ALL};
            GetScrollInfo(hwnd, SB_VERT, &info);
            int position = e->nInspectorScroll;
            switch (LOWORD(wp)) {
            case SB_LINEUP: position -= 32; break;
            case SB_LINEDOWN: position += 32; break;
            case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
            case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
            case SB_THUMBPOSITION:
            case SB_THUMBTRACK: position = info.nTrackPos; break;
            case SB_TOP: position = 0; break;
            case SB_BOTTOM: position = info.nMax; break;
            default: return 0;
            }
            e->nInspectorScroll = std::max(0, position);
            e->LayoutPanel(2);
            return 0;
        }
        break;
    case WM_COMMAND:
    case WM_HSCROLL:
    case WM_DRAWITEM:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: return SendMessageW(e->window, message, wp, lp);
    case WM_CONTEXTMENU:
        if (p->index == 0) {
            e->ShowObjectMenu({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
        }
        if (p->index == 2) {
            e->SelectComponentForWindow(reinterpret_cast<HWND>(wp));
            e->ShowComponentMenu({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
        }
        break;
    case WM_CLOSE:
        p->hidden = true;
        ShowWindow(hwnd, SW_HIDE);
        e->Layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize = p->index == 2   ? POINT{360, 580}
                               : p->index == 1 ? POINT{420, 280}
                               : p->index == 3 ? POINT{450, 240}
                                               : POINT{260, 260};
        return 0;
    }
    case WM_ERASEBKGND: {
        RECT r{};
        GetClientRect(hwnd, &r);
        theme::Fill(reinterpret_cast<HDC>(wp), r, theme::COLOR_PANEL);
        return 1;
    }
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        if (!p->floating && GET_Y_LPARAM(lp) < theme::PANEL_HEADER_HEIGHT) {
            p->bTabPressed = true;
            p->tabDragStart = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            SetCapture(hwnd);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (p->bTabPressed && GetCapture() == hwnd &&
            (std::abs(GET_X_LPARAM(lp) - p->tabDragStart.x) > 12 || std::abs(GET_Y_LPARAM(lp) - p->tabDragStart.y) > 12)) {
            // Tear the tab off: float the panel under the cursor and hand the
            // drag to the system move loop (the button is still held down).
            p->bTabPressed = false;
            ReleaseCapture();
            POINT cursor{};
            GetCursorPos(&cursor);
            e->FloatPanel(p->index);
            SetWindowPos(hwnd, HWND_TOP, cursor.x - 80, cursor.y - 12, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
            SendMessageW(hwnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (p->bTabPressed) {
            p->bTabPressed = false;
            ReleaseCapture();
        }
        return 0;
    case WM_ENTERSIZEMOVE:
        p->bMoving = false;
        break;
    case WM_MOVING:
        if (p->floating) {
            p->bMoving = true;
            POINT cursor{};
            GetCursorPos(&cursor);
            RECT dock{};
            GetClientRect(e->window, &dock);
            MapWindowPoints(e->window, nullptr, reinterpret_cast<POINT*>(&dock), 2);
            dock.top += TOP_CHROME_HEIGHT;
            const int slot = GetAsyncKeyState(VK_CONTROL) < 0 ? -1 : e->DockSlotAt(cursor);
            e->ShowDockPreview(slot);
            e->SetStatus(slot >= 0 ? L"Release to dock the panel in the highlighted slot (Ctrl: keep floating)." : L"Floating panel.");
        }
        break;
    case WM_EXITSIZEMOVE:
        if (p->floating && p->bMoving) {
            p->bMoving = false;
            POINT cursor{};
            GetCursorPos(&cursor);
            RECT dock{};
            GetClientRect(e->window, &dock);
            MapWindowPoints(e->window, nullptr, reinterpret_cast<POINT*>(&dock), 2);
            dock.top += TOP_CHROME_HEIGHT;
            const int slot = GetAsyncKeyState(VK_CONTROL) < 0 ? -1 : e->DockSlotAt(cursor); // Ctrl keeps it floating
            e->ShowDockPreview(-1);
            if (PtInRect(&dock, cursor) && slot >= 0) e->DockPanelInSlot(p->index, slot);
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{};
        GetClientRect(hwnd, &r);
        const int header = theme::PANEL_HEADER_HEIGHT;
        const auto& fonts = theme::GetFonts();
        // Dock tab strip (Unity: dark strip, the active tab merges with the body).
        RECT strip{0, 0, r.right, header};
        theme::Fill(dc, strip, theme::COLOR_TAB_STRIP);
        theme::Line(dc, 0, header - 1, r.right, header - 1, theme::COLOR_SEPARATOR);
        if (p->index != 3 && p->index != 1) { // panels 1 and 3 use tab buttons
            HGDIOBJ old = SelectObject(dc, fonts.hBold);
            SIZE size{};
            GetTextExtentPoint32W(dc, PANEL_NAMES[p->index], static_cast<int>(wcslen(PANEL_NAMES[p->index])), &size);
            SelectObject(dc, old);
            RECT tab{0, 2, std::min<LONG>(r.right - 70, 34 + size.cx + 14), header};
            theme::Fill(dc, tab, theme::COLOR_TAB_ACTIVE);
            const HWND focus = GetFocus();
            const bool focused = focus == hwnd || IsChild(hwnd, focus);
            theme::Line(dc, tab.left, 1, tab.right, 1,
                        focused ? theme::COLOR_TAB_ACCENT : theme::Blend(theme::COLOR_TAB_ACTIVE, RGB(255, 255, 255), 0.12f), 2);
            RECT icon{10, 8, 24, 22};
            theme::DrawIcon(dc, PANEL_ICONS[p->index], icon, theme::COLOR_TEXT);
            RECT text{30, 2, tab.right - 4, header};
            theme::DrawLabel(dc, PANEL_NAMES[p->index], text, theme::COLOR_TEXT_BRIGHT, fonts.hBold,
                             DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }
        if (p->index == 0 && !p->collapsed) {
            // Toolbar row and the Unity scene header row ("SampleScene").
            RECT toolbar{0, header, r.right, header + 32};
            theme::Fill(dc, toolbar, theme::COLOR_TAB_ACTIVE);
            theme::Line(dc, 0, header + 31, r.right, header + 31, theme::COLOR_SEPARATOR);
            RECT sceneRow{0, header + 32, r.right, header + 32 + HIERARCHY_SCENE_ROW};
            theme::Fill(dc, sceneRow, theme::Blend(theme::COLOR_PANEL, RGB(255, 255, 255), 0.05f));
            RECT chevron{4, sceneRow.top + 5, 16, sceneRow.bottom - 5};
            theme::DrawIcon(dc, theme::Icon::CHEVRON_DOWN, chevron, theme::COLOR_TEXT_DIM);
            RECT icon{18, sceneRow.top + 3, 34, sceneRow.bottom - 3};
            theme::DrawIcon(dc, theme::Icon::GAME_OBJECT, icon, theme::COLOR_TEXT);
            const std::wstring sceneName = e->projectFile.stem().wstring() + (e->bPlaying ? L"  (Playing)" : L"");
            RECT text{40, sceneRow.top, r.right - 30, sceneRow.bottom};
            theme::DrawLabel(dc, sceneName, text, theme::COLOR_TEXT_BRIGHT, fonts.hBold,
                             DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            RECT kebab{r.right - 22, sceneRow.top + 4, r.right - 8, sceneRow.bottom - 4};
            theme::DrawIcon(dc, theme::Icon::KEBAB, kebab, theme::COLOR_TEXT_DIM);
            theme::Line(dc, 0, r.bottom - 36, r.right, r.bottom - 36, theme::COLOR_SEPARATOR);
        } else if (p->index == 1 && !p->collapsed) {
            if (e->bGameView) {
                RECT letterbox{0, header + 30, r.right, r.bottom};
                theme::Fill(dc, letterbox, theme::COLOR_TOOLBAR);
            }
            RECT toolbar{0, header, r.right, header + 30};
            theme::Fill(dc, toolbar, theme::COLOR_TAB_ACTIVE);
            theme::Line(dc, 0, header + 29, r.right, header + 29, theme::COLOR_SEPARATOR);
        } else if (p->index == 2 && !p->collapsed) {
            // Unity: separator under the GameObject header and above "Add Component".
            theme::Line(dc, 0, e->nInspectorHeaderBottom - e->nInspectorScroll, r.right, e->nInspectorHeaderBottom - e->nInspectorScroll,
                        theme::COLOR_SEPARATOR);
            if (IsWindowVisible(e->componentAdd))
                theme::Line(dc, 0, e->nAddComponentSeparator - e->nInspectorScroll, r.right, e->nAddComponentSeparator - e->nInspectorScroll,
                            theme::COLOR_SEPARATOR);

        }
        if (!p->collapsed) e->PaintInputBoxes(dc, hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

LRESULT CALLBACK ViewportProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    Editor* e = reinterpret_cast<Editor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        e = static_cast<Editor*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(e));
    }
    if (!e) return DefWindowProcW(hwnd, message, wp, lp);
    // Keys in the view are camera / tool shortcuts. Without this the dialog
    // manager (IsDialogMessage in RunEditor) treats every fly key (WASDQE) and
    // Alt+key as an unmatched mnemonic and beeps on each auto-repeat.
    if (message == WM_GETDLGCODE) return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    if (message == WM_CHAR || message == WM_SYSCHAR) return 0;
    if (e->bGameView) {
        switch (message) {
        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: SetFocus(hwnd); return 0;
        case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: case WM_MOUSEMOVE: case WM_MOUSEWHEEL: return 0;
        default: break;
        }
    }
    switch (message) {
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        if (!e->bRendererReady) return 0;
        if (!e->Apply()) return 0;
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (!(wp & MK_CONTROL) && e->nSelected >= 0 && static_cast<size_t>(e->nSelected) < e->scene.vEntities.size()) {
            const auto item = WorldDraw(e->scene, e->nSelected);
            const int axis = e->renderer.HitGizmo(x, y, item, e->gizmoMode);
            if (axis >= 0) {
                e->nGizmoAxis = axis;
                e->gizmoStartItem = item;
                e->gizmoStartMouse = {x, y};
                e->vGizmoStartItems.clear();
                for (int i : e->vSelectedIndices)
                    e->vGizmoStartItems.emplace_back(i, WorldDraw(e->scene, i));
                e->renderer.SetGizmoHot(axis);
                e->drag = Editor::Drag::Gizmo;
                SetCapture(hwnd);
                return 0;
            }
        }
        e->selectionStart = {x, y};
        e->vSelectionBeforeDrag = e->vSelectedIndices;
        e->bSelectionAdd = (wp & MK_SHIFT) != 0;
        e->bSelectionToggle = (wp & MK_CONTROL) != 0;
        e->bSelectingRect = false;
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
            if (e->bSelectingRect)
                hits = e->renderer.PickRect(e->selectionStart.x, e->selectionStart.y, x, y, items);
            else {
                const int hit = e->renderer.Pick(x, y, items);
                if (hit >= 0) hits.push_back(hit);
            }
            e->drag = Editor::Drag::None;
            e->renderer.SetMarquee(false, 0, 0, 0, 0);
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
            e->bRightUsed = false;
            e->bOrbitPivot = (GetKeyState(VK_MENU) & 0x8000) != 0;
            e->nCameraTick = GetTickCount64();
        }
        SetCapture(hwnd);
        return 0;
    case WM_RBUTTONUP:
        if (e->drag == Editor::Drag::Orbit) {
            const bool menu = !e->bRightUsed && !e->bOrbitPivot;
            e->drag = Editor::Drag::None;
            ReleaseCapture();
            if (menu) {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ClientToScreen(hwnd, &pt);
                e->ShowObjectMenu(pt);
            }
        }
        return 0;
    case WM_MBUTTONUP:
        if (e->drag == Editor::Drag::Pan) {
            e->drag = Editor::Drag::None;
            ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (e->drag == Editor::Drag::Gizmo && e->nSelected >= 0 && static_cast<size_t>(e->nSelected) < e->scene.vEntities.size()) {
            e->RestoreDrag();
            e->SetStatus(L"Transform cancelled.");
        }
        if (e->drag == Editor::Drag::Select) e->renderer.SetMarquee(false, 0, 0, 0, 0);
        e->renderer.SetGizmoHot(-1);
        e->drag = Editor::Drag::None;
        return 0;
    case WM_KEYDOWN:
        if (e->drag == Editor::Drag::Orbit) return 0;
        if (wp == VK_ESCAPE && (e->drag == Editor::Drag::Gizmo || e->drag == Editor::Drag::Select)) {
            if (e->drag == Editor::Drag::Gizmo) e->RestoreDrag();
            e->renderer.SetMarquee(false, 0, 0, 0, 0);
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
                if (std::abs(x - e->selectionStart.x) >= 4 || std::abs(y - e->selectionStart.y) >= 4) e->bSelectingRect = true;
                if (e->bSelectingRect) e->renderer.SetMarquee(true, e->selectionStart.x, e->selectionStart.y, x, y);
                e->RequestRender();
                return 0;
            }
            if (e->drag == Editor::Drag::Gizmo) {
                if (e->nSelected >= 0 && static_cast<size_t>(e->nSelected) < e->scene.vEntities.size()) {
                    DrawItem updated;
                    if (e->renderer.DragGizmo(e->gizmoStartItem, e->gizmoMode, e->nGizmoAxis, e->gizmoStartMouse.x,
                                              e->gizmoStartMouse.y, x, y, updated)) {
                        if (wp & MK_SHIFT) SnapGizmo(updated, e->gizmoStartItem, e->gizmoMode, e->nGizmoAxis);
                        e->UpdateGroup(updated);
                        e->RefreshTransformFields(); // full Inspect() per mouse move starved rendering
                        e->RequestRender();
                    }
                }
                return 0;
            }
            const float dx = static_cast<float>(x - e->lastMouse.x), dy = static_cast<float>(y - e->lastMouse.y);
            e->lastMouse = {x, y};
            if (e->drag == Editor::Drag::Orbit) {
                if (std::abs(x - e->rightStart.x) >= 3 || std::abs(y - e->rightStart.y) >= 3) e->bRightUsed = true;
                if (e->bRightUsed) LookCamera(e->camera, dx, dy, e->bOrbitPivot);
            } else {
                RECT rect{};
                GetClientRect(hwnd, &rect);
                const float height = static_cast<float>(std::max<LONG>(1, rect.bottom));
                {
                    const float worldHeight =
                        e->camera.fDistance *
                        (e->camera.bOrthographic ? 10.0f / 6.32455532f : 2.0f * std::tan(DirectX::XM_PIDIV4 * 0.5f));
                    const float speed = worldHeight / height;
                    const float sy = std::sin(e->camera.fYaw), cy = std::cos(e->camera.fYaw);
                    const float sp = std::sin(e->camera.fPitch), cp = std::cos(e->camera.fPitch);
                    e->camera.target.x += (-dx * cy - dy * sy * sp) * speed;
                    e->camera.target.y += dy * cp * speed;
                    e->camera.target.z += (-dx * sy + dy * cy * sp) * speed;
                }
            }
            e->renderer.SetCamera(e->camera);
            e->RequestRender();
        } else if (e->bRendererReady && e->nSelected >= 0 && static_cast<size_t>(e->nSelected) < e->scene.vEntities.size()) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            e->renderer.SetGizmoHot(
                e->renderer.HitGizmo(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), WorldDraw(e->scene, e->nSelected), e->gizmoMode));
        }
        return 0;
    case WM_MOUSELEAVE:
        if (e->drag == Editor::Drag::None) e->renderer.SetGizmoHot(-1);
        return 0;
    case WM_MOUSEWHEEL: {
        const float steps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / static_cast<float>(WHEEL_DELTA);
        e->camera.fDistance = std::clamp(e->camera.fDistance * std::pow(0.85f, steps), 0.5f, 80.0f);
        e->renderer.SetCamera(e->camera);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

} // namespace dxs::editor
