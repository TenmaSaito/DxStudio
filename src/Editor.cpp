#include "Editor.h"
#include "EditorWindow.h"

namespace dxs {
namespace editor {

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
        theme::InitializeFonts(GetDpiForWindow(hwnd));
        theme::ApplyDarkTitleBar(hwnd);
        e->font = theme::GetFonts().hRegular;
        e->titleFont = theme::GetFonts().hBold;

        // Brushes are owned by the theme cache (Unity Dark palette).
        e->background = theme::Brush(theme::COLOR_PANEL);
        e->inputBackground = theme::Brush(theme::COLOR_INPUT);
        e->tooltip =
            CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
                            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr, e->instance, nullptr);
        e->CreatePanels();
        for (int i = 0; i < 3; ++i) {
            e->splitters[i] = e->Make(L"STATIC", L"", SS_NOTIFY, ID_SPLITTER + i);
            SetWindowSubclass(e->splitters[i], SplitterSubclassProc, static_cast<UINT_PTR>(i), reinterpret_cast<DWORD_PTR>(e));
        }

        const wchar_t* names[] = {L"Create", L"2D Image", L"Delete", L"Apply changes", L"Save", L"Load",
                                  L"Export", L"Shader",   L""};
        const int ids[] = {ID_CREATE_MENU, ID_ADD_SPRITE, ID_DELETE, ID_APPLY, ID_SAVE, ID_LOAD, ID_EXPORT, ID_RELOAD_SHADER, 0};
        for (int i = 0; i < 8; ++i) {
            e->controls[i] = e->Make(L"BUTTON", names[i], BS_OWNERDRAW | WS_TABSTOP, ids[i]);
        }

        e->controls[8] = e->Make(L"STATIC", L"DX12", SS_CENTER);
        e->controls[9] = e->Make(L"BUTTON", L"Play", BS_OWNERDRAW | WS_TABSTOP, ID_BUILD_DEBUG);
        e->controls[10] = e->Make(L"BUTTON", L"Android", BS_OWNERDRAW | WS_TABSTOP, ID_BUILD_ANDROID);
        e->pauseButton = e->Make(L"BUTTON", L"Pause", BS_OWNERDRAW | WS_TABSTOP, ID_PAUSE);
        e->stepButton = e->Make(L"BUTTON", L"Step", BS_OWNERDRAW | WS_TABSTOP, ID_STEP);
        e->graphicsButton = e->Make(L"BUTTON", L"DX12", BS_OWNERDRAW | WS_TABSTOP, ID_GRAPHICS_MENU);
        e->layoutButton = e->Make(L"BUTTON", L"Layout", BS_OWNERDRAW | WS_TABSTOP, ID_LAYOUT_MENU);
        for (int i = 0; i < 3; ++i) {
            SetParent(e->controls[i], e->panels[0].window);
        }

        SetParent(e->controls[3], e->panels[2].window);
        const wchar_t* modeNames[] = {L"W Move", L"E Rotate", L"R Scale"};
        for (int i = 0; i < 3; ++i) {
            e->gizmoButtons[i] = e->Make(L"BUTTON", modeNames[i], BS_OWNERDRAW | WS_TABSTOP, ID_MOVE_MODE + i);
        }

        for (auto button : e->gizmoButtons) {
            SetParent(button, e->panels[1].window);
        }

        const wchar_t* projectionNames[] = {L"Persp", L"Ortho"};
        for (int i = 0; i < 2; ++i) {
            e->projectionButtons[i] = e->Make(L"BUTTON", projectionNames[i], BS_OWNERDRAW | WS_TABSTOP, ID_PERSPECTIVE + i);
            SetParent(e->projectionButtons[i], e->panels[1].window);
        }

        e->hierarchy = e->Make(L"LISTBOX", L"",
                               LBS_NOTIFY | LBS_EXTENDEDSEL | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT |
                                   WS_VSCROLL | WS_TABSTOP,
                               ID_HIERARCHY);
        SendMessageW(e->hierarchy, LB_SETITEMHEIGHT, 0, theme::ROW_HEIGHT);
        SetParent(e->hierarchy, e->panels[0].window);
        SetWindowSubclass(e->hierarchy, HierarchySubclassProc, 1, reinterpret_cast<DWORD_PTR>(e));
        e->hierarchySearch = e->Make(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, ID_HIERARCHY_SEARCH);
        SendMessageW(e->hierarchySearch, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(Tr(L"Search Hierarchy")));
        SetParent(e->hierarchySearch, e->panels[0].window);
        e->viewport = CreateWindowExW(0, L"DxStudioViewport", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 1, 1,
                                      e->panels[1].window, nullptr, e->instance, e);
        for (int i = 0; i < FIELD_COUNT; ++i) {
            if (i == 1) continue;
            e->labels[i] = e->Make(L"STATIC", FIELD_LABELS[i], SS_LEFT);
            e->edits[i] = e->Make(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, ID_FIRST_EDIT + i);
            SetParent(e->labels[i], e->panels[2].window);
            SetParent(e->edits[i], e->panels[2].window);
            if (i >= 2 && i <= 10) {
                SetWindowTextW(e->labels[i], ((i - 2) % 3) == 0 ? L"X" : ((i - 2) % 3) == 1 ? L"Y" : L"Z");
            }

            if (i >= 11) {
                SetWindowTextW(e->labels[i], i == 11 ? L"R" : i == 12 ? L"G" : i == 13 ? L"B" : L"A");
            }
        }

        e->inspectorIcon = e->Make(L"STATIC", L"", SS_OWNERDRAW);
        e->inspectorEnabled = e->Make(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, ID_INSPECTOR_ENABLED);
        e->inspectorTagLabel = e->Make(L"STATIC", L"Tag", SS_LEFT);
        e->inspectorTagValue = e->Make(L"STATIC", L"Untagged", SS_OWNERDRAW);
        e->inspectorLayerLabel = e->Make(L"STATIC", L"Layer", SS_LEFT);
        e->inspectorLayerValue = e->Make(L"STATIC", L"Default", SS_OWNERDRAW);
        e->inspectorHint = e->Make(L"STATIC", L"Edit values, then Apply changes", SS_LEFT);
        e->colorLabel = e->Make(L"STATIC", L"Color", SS_LEFT | SS_CENTERIMAGE);
        SetParent(e->colorLabel, e->panels[2].window);
        for (auto control : {e->inspectorIcon, e->inspectorEnabled, e->inspectorTagLabel, e->inspectorTagValue,
                             e->inspectorLayerLabel, e->inspectorLayerValue}) {
            SetParent(control, e->panels[2].window);
        }

        SetParent(e->inspectorHint, e->panels[2].window);
        e->materialLabel = e->Make(L"STATIC", L"Material", SS_LEFT);
        e->materialEdit = e->Make(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP);
        e->textureLabel = e->Make(L"STATIC", L"Texture", SS_LEFT);
        e->textureEdit = e->Make(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP);
        e->meshLabel = e->Make(L"STATIC", L"Mesh", SS_LEFT);
        e->meshEdit = e->Make(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP);
        e->meshPick = e->Make(L"BUTTON", L"\x25CE", BS_OWNERDRAW | WS_TABSTOP, ID_MESH_PICK);
        e->meshConvert = e->Make(L"BUTTON", L"Create Material (Direct3D 11 / 12)", BS_OWNERDRAW | WS_TABSTOP, ID_MESH_CONVERT);
        for (auto control : {e->materialLabel, e->materialEdit, e->textureLabel, e->textureEdit, e->meshLabel, e->meshEdit,
                             e->meshPick, e->meshConvert}) {
            SetParent(control, e->panels[2].window);
        }

        const wchar_t* rowNames[] = {L"Position", L"Rotation °", L"Scale"};
        const wchar_t* sections[] = {L"Transform", L"Rendering / Color", L"Components"};
        const theme::Icon sectionIcons[] = {theme::Icon::TRANSFORM, theme::Icon::RENDERER, theme::Icon::SCRIPT};
        const COLORREF sectionColors[] = {theme::COLOR_TEXT, theme::COLOR_ICON_CUBE, theme::COLOR_ICON_SCRIPT};
        for (int i = 0; i < 3; ++i) {
            e->rowLabels[i] = e->Make(L"STATIC", rowNames[i], SS_LEFT);
            SetParent(e->rowLabels[i], e->panels[2].window);
            e->groupLabels[i] = e->Make(L"BUTTON", sections[i], BS_OWNERDRAW | WS_TABSTOP, ID_SECTION_TOGGLE + i);
            SetParent(e->groupLabels[i], e->panels[2].window);
            theme::SetButtonSpec(e->groupLabels[i], {theme::ButtonRole::HEADER, sectionIcons[i], sectionColors[i]});
        }

        e->componentList = e->Make(
            L"LISTBOX", L"", LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP,
            ID_COMPONENT_LIST);
        e->componentChoice = e->Make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_COMPONENT_CHOICE);
        e->componentAdd = e->Make(L"BUTTON", L"Add Component", BS_OWNERDRAW | WS_TABSTOP, ID_ADD_COMPONENT);
        e->componentRemove = e->Make(L"BUTTON", L"Remove", BS_OWNERDRAW | WS_TABSTOP, ID_REMOVE_COMPONENT);
        e->componentFields = e->Make(
            L"LISTBOX", L"", LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP,
            ID_COMPONENT_FIELDS);
        e->componentValue = e->Make(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, ID_COMPONENT_VALUE);
        e->componentValueApply = e->Make(L"BUTTON", L"Set", BS_OWNERDRAW | WS_TABSTOP, ID_APPLY_COMPONENT_VALUE);
        for (HWND list : {e->componentList, e->componentFields}) {
            SendMessageW(list, LB_SETITEMHEIGHT, 0, theme::ROW_HEIGHT);
        }

        for (auto control : {e->componentList, e->componentChoice, e->componentAdd, e->componentRemove, e->componentFields,
                             e->componentValue, e->componentValueApply}) {
            SetParent(control, e->panels[2].window);
        }

        for (const wchar_t* builtin : {L"MeshRenderer", L"PolygonRenderer", L"ModelRenderer", L"Image"}) {
            SendMessageW(e->componentChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(builtin));
        }

        for (const auto& name : RegisteredComponentNames()) {
            auto wide = Wide(name);
            SendMessageW(e->componentChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
        }

        SendMessageW(e->componentChoice, CB_SETCURSEL, 0, 0);
        e->status = e->Make(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS | SS_NOPREFIX);
        e->statusRight = e->Make(L"STATIC", L"", SS_RIGHT | SS_NOPREFIX);
        e->progressGauge = e->Make(L"STATIC", L"", SS_OWNERDRAW);
        ShowWindow(e->progressGauge, SW_HIDE);
        e->ApplyTheme();
        e->LoadSettings();
        e->EnsureProjectPrimitives();
        e->projectPanel = CreateProjectPanel(e->panels[3].window, e->instance, e->projectRoot, e->base);
        e->consolePanel = CreateConsolePanel(e->panels[3].window, e->instance);
        e->callbackRegistration = RegisterCallbacks(*e);
        Debug::Log("DxStudio editor started. Project: " + Utf8(e->projectRoot.wstring()));
        std::string error;
        if (!LoadScene(e->projectRoot / L"scene.dxs", e->scene, error)) {
            std::error_code ec;
            bool exists = std::filesystem::exists(e->projectRoot / L"scene.dxs", ec);
            if (exists || ec) {
                e->bSceneProtected = true;
                Error(hwnd, "The existing scene could not be loaded. Save and export are disabled to protect it. Fix the file "
                            "and select Load.\n\n" +
                                error);
            }

            Entity cube;
            cube.sName = "Cube";
            cube.draw.color = {0.35f, 0.68f, 1.f, 1.f};
            e->scene.vEntities.push_back(cube);
        }

        e->camera = Camera{};

        // 作成後に名前を変えたコントロール（"Position X" → "X" など）もあるので、
        // 起動完了時点の文字列を言語切り替えの基準として記録し直す。
        for (auto& [hControl, sEnglish] : e->vLocalizedControls) {
            const std::wstring sCurrent = Text(hControl);
            if (sCurrent != Tr(sEnglish.c_str())) {
                sEnglish = sCurrent;
            }
        }

        std::erase_if(e->vLocalizedControls, [](const auto& record) { return record.second.empty(); });
        e->bShellReady = true;
        e->nSelected = e->scene.vEntities.empty() ? -1 : 0;
        if (e->nSelected >= 0) {
            e->vSelectedIndices = {e->nSelected};
        }

        e->Populate();
        e->Layout();
        e->InitRenderer();
        SetTimer(hwnd, 1, 16, nullptr);
        return 0;
    }
    case WM_SIZE:
        if (e->viewport && wp != SIZE_MINIMIZED) {
            e->Layout();
        }

        return 0;
    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        theme::InitializeFonts(HIWORD(wp));
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize = {1000, 700};
        return 0;
    }
    case WM_HSCROLL:
        if (lp) {
            e->OnInspectorSlider(reinterpret_cast<HWND>(lp)); // Inspector range sliders
        }

        return 0;
    case WM_DXS_REBUILD_INSPECTOR: e->RebuildInspectorNow(); return 0;

    case WM_DXS_PROJECT_ASSET_COMMAND: {
        // Project パネルの右クリックメニューからの変換（物体には設定しない）
        const auto* pFile = reinterpret_cast<const std::filesystem::path*>(lp);
        if (!pFile) return 0;

        if (wp == PROJECT_ASSET_CREATE_MATERIAL) {
            e->CreateMaterialFromX(*pFile, false);
        } else if (wp == PROJECT_ASSET_CONVERT_TO_X) {
            e->ConvertFbxAsset(*pFile, false);
        }

        return 0;
    }

    case WM_COMMAND:
        // Inputs draw their own focus border; repaint it on focus changes.
        if ((HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS) && lp) {
            InvalidateRect(GetParent(reinterpret_cast<HWND>(lp)), nullptr, FALSE);
        }

        if (HIWORD(wp) == EN_KILLFOCUS && e->IsInspectorEdit(reinterpret_cast<HWND>(lp))) {
            e->ApplyOnFocusLoss(reinterpret_cast<HWND>(lp));
        }

        // Transform / Color fields follow the keyboard in real time.
        if (HIWORD(wp) == EN_CHANGE && LOWORD(wp) >= ID_FIRST_EDIT + 2 && LOWORD(wp) < ID_FIRST_EDIT + FIELD_COUNT) {
            e->ApplyFieldLive(LOWORD(wp) - ID_FIRST_EDIT);
        }

        // Inspector property fields commit on check-box clicks and focus loss.
        if (LOWORD(wp) >= ID_COMPONENT_FIELD_FIRST && LOWORD(wp) <= ID_COMPONENT_FIELD_LAST) {
            const bool bComboBox = lp && e->FindFieldRow(LOWORD(wp), nullptr) && e->FindFieldRow(LOWORD(wp), nullptr)->bEnum;
            if (bComboBox) {
                if (HIWORD(wp) == CBN_SELCHANGE) {
                    e->CommitComponentField(LOWORD(wp), false);
                }
            } else if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == EN_KILLFOCUS) {
                e->CommitComponentField(LOWORD(wp), HIWORD(wp) == BN_CLICKED);
            }

            return 0;
        }

        if (!e->ShellCommand(LOWORD(wp))) {
            e->Command(LOWORD(wp), HIWORD(wp));
        }

        return 0;
    case WM_LBUTTONDOWN: {
        RECT client{};
        GetClientRect(hwnd, &client);
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        const int lower = client.bottom - theme::STATUS_BAR_HEIGHT -
                          (e->SlotPanel(SLOT_BOTTOM).hidden || e->SlotPanel(SLOT_BOTTOM).floating
                               ? 0
                               : (e->SlotPanel(SLOT_BOTTOM).collapsed ? theme::PANEL_HEADER_HEIGHT : e->nDockProject));
        if (!e->SlotPanel(SLOT_LEFT).hidden && !e->SlotPanel(SLOT_LEFT).floating && std::abs(x - e->nDockLeft) <= 6) {
            e->layoutDrag = Editor::LayoutDrag::Left;
        } else if (!e->SlotPanel(SLOT_RIGHT).hidden && !e->SlotPanel(SLOT_RIGHT).floating &&
                   std::abs(x - (client.right - e->nDockRight)) <= 6) {
            e->layoutDrag = Editor::LayoutDrag::Right;
        } else if (!e->SlotPanel(SLOT_BOTTOM).hidden && !e->SlotPanel(SLOT_BOTTOM).floating && std::abs(y - lower) <= 6) {
            e->layoutDrag = Editor::LayoutDrag::Project;
        }

        if (e->layoutDrag != Editor::LayoutDrag::None) {
            SetCapture(hwnd);
            return 0;
        }

        break;
    }
    case WM_MOUSEMOVE:
        if (e->layoutDrag != Editor::LayoutDrag::None) {
            RECT client{};
            GetClientRect(hwnd, &client);
            const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            const int width = static_cast<int>(client.right), height = static_cast<int>(client.bottom);
            if (e->layoutDrag == Editor::LayoutDrag::Left) {
                e->nDockLeft = std::clamp(x, 160, std::max(160, width - 500));
            } else if (e->layoutDrag == Editor::LayoutDrag::Right) {
                e->nDockRight = std::clamp(width - x, 240, std::max(240, width - 400));
            } else {
                e->nDockProject = std::clamp(height - theme::STATUS_BAR_HEIGHT - y, 120, std::max(120, height - 220));
            }

            e->Layout();
            return 0;
        }

        break;
    case WM_LBUTTONUP:
        if (e->layoutDrag != Editor::LayoutDrag::None) {
            e->layoutDrag = Editor::LayoutDrag::None;
            ReleaseCapture();
            return 0;
        }

        break;
    case WM_CAPTURECHANGED: e->layoutDrag = Editor::LayoutDrag::None; break;
    case WM_MOUSEWHEEL: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(e->viewport, &pt);
        RECT r{};
        GetClientRect(e->viewport, &r);
        if (PtInRect(&r, pt)) return SendMessageW(e->viewport, message, wp, lp);
        break;
    }
    case WM_TIMER:
        if (wp == TASK_STATUS_TIMER) {
            Debug::DispatchPending();
            e->UpdateTaskStatus();
        } else {
            e->Draw();
        }

        return 0;
    case WM_DXS_TASK_DONE:
        Debug::DispatchPending(); // messages the worker logged
        e->FinishBackgroundTask();
        return 0;
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        const int id = static_cast<int>(item->CtlID);
        if (item->CtlType == ODT_LISTBOX) {
            if (item->hwndItem == e->hierarchy) {
                e->DrawHierarchyRow(*item);
            } else if (item->hwndItem == e->componentList) {
                e->DrawComponentRow(*item);
            } else if (item->hwndItem == e->componentFields) {
                e->DrawFieldRow(*item);
            }

            return TRUE;
        }

        if (item->CtlType == ODT_COMBOBOX) {
            e->DrawEnumItem(*item);
            return TRUE;
        }

        if (item->CtlType == ODT_STATIC && item->hwndItem == e->progressGauge) {
            e->DrawProgressGauge(*item);
            return TRUE;
        }

        if (item->CtlType == ODT_STATIC) {
            e->DrawInspectorStatic(*item);
            return TRUE;
        }

        if (item->CtlType != ODT_BUTTON) break;
        const bool active =
            (id == ID_BUILD_DEBUG && e->bPlaying) || (id == ID_PAUSE && e->bPaused) ||
            (id == ID_MOVE_MODE && e->gizmoMode == GizmoMode::Translate) ||
            (id == ID_ROTATE_MODE && e->gizmoMode == GizmoMode::Rotate) ||
            (id == ID_SCALE_MODE && e->gizmoMode == GizmoMode::Scale) || (id == ID_PERSPECTIVE && !e->camera.bOrthographic) ||
            (id == ID_ORTHOGRAPHIC && e->camera.bOrthographic) || (id == ID_INSPECTOR_ENABLED && e->bInspectorEnabledChecked) ||
            (id >= ID_COMPONENT_FIELD_FIRST && id <= ID_COMPONENT_FIELD_LAST && e->FindFieldRow(id, nullptr) &&
             e->FindFieldRow(id, nullptr)->bChecked) ||
            (id >= ID_PANEL_TAB_FIRST && id < ID_PANEL_TAB_FIRST + BOTTOM_TAB_COUNT &&
             e->nBottomTab == id - ID_PANEL_TAB_FIRST) ||
            (id == ID_VIEW_TAB_FIRST && !e->bGameView) || (id == ID_VIEW_TAB_FIRST + 1 && e->bGameView);
        theme::DrawButton(*item, active);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wp);
        HWND control = reinterpret_cast<HWND>(lp);
        wchar_t className[16]{};
        GetClassNameW(control, className, 16);
        const bool isEdit = _wcsicmp(className, L"Edit") == 0;
        const bool isComboList = _wcsicmp(className, L"ComboLBox") == 0;
        COLORREF text = IsWindowEnabled(control) ? theme::COLOR_TEXT : theme::COLOR_TEXT_DISABLED;
        COLORREF back = isEdit || isComboList ? theme::COLOR_INPUT : theme::COLOR_PANEL;
        if (control == e->status || control == e->statusRight) {
            back = theme::COLOR_TOOLBAR;
            text = theme::COLOR_TEXT_DIM;
        } else if (control == e->inspectorHint) {
            text = theme::COLOR_TEXT_DIM;
        }

        // Unity draws X / Y / Z and R / G / B / A in the normal label colour;
        // "script can not be loaded" rows use the warning colour.
        for (const auto& view : e->vComponentViews) {
            for (const auto& row : view.vRows) {
                if (row.hLabel == control && !row.hValue) {
                    text = theme::COLOR_LOG_WARNING;
                }
            }
        }

        SetTextColor(dc, text);
        SetBkColor(dc, back);
        return reinterpret_cast<LRESULT>(theme::Brush(back));
    }
    case WM_ERASEBKGND: {
        RECT r{};
        GetClientRect(hwnd, &r);
        theme::Fill(reinterpret_cast<HDC>(wp), r, theme::COLOR_SEPARATOR);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{};
        GetClientRect(hwnd, &r);

        // Menu bar + main toolbar + status bar (Unity: #191919 chrome).
        RECT chrome{0, 0, r.right, TOP_CHROME_HEIGHT};
        theme::Fill(dc, chrome, theme::COLOR_TOOLBAR);
        theme::Line(dc, 0, theme::MENU_BAR_HEIGHT, r.right, theme::MENU_BAR_HEIGHT,
                    theme::Blend(theme::COLOR_TOOLBAR, RGB(255, 255, 255), 0.06f));
        if (e->bPlaying) {
            theme::Line(dc, 0, TOP_CHROME_HEIGHT - 2, r.right, TOP_CHROME_HEIGHT - 2, theme::COLOR_ACCENT, 2);
        }

        RECT logo{8, 4, 24, 20};
        theme::DrawIcon(dc, theme::Icon::CUBE, logo, theme::COLOR_ICON_CUBE);
        RECT statusBar{0, r.bottom - theme::STATUS_BAR_HEIGHT, r.right, r.bottom};
        theme::Fill(dc, statusBar, theme::COLOR_TOOLBAR);
        RECT statusIcon{9, statusBar.top + 5, 23, statusBar.bottom - 5};
        theme::DrawIcon(dc, theme::Icon::INFO, statusIcon, theme::COLOR_TEXT_DIM);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        e->bShellReady = false;
        e->callbackRegistration.Clear();
        for (auto& panel : e->panels) {
            if (panel.floating && IsWindow(panel.window)) {
                DestroyWindow(panel.window);
            }
        }

        for (HMENU menu : e->menuPopups) {
            if (menu) {
                DestroyMenu(menu);
            }
        }

        KillTimer(hwnd, 1);
        e->CancelBackgroundTask(true); // closing stops a running build
        e->renderer.Shutdown();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, message, wp, lp);
}
} // namespace editor

using namespace editor;

int RunEditor(HINSTANCE instance, const std::filesystem::path& baseDir, const std::filesystem::path& initialProjectFile) {
    RegisterProjectComponents();
    Debug::SetMainThread();   // Console updates stay on the UI thread
    LoadLanguagePreference(); // 表示言語（設定ファイル → Windows の表示言語）
    INITCOMMONCONTROLSEX commonControls{sizeof(commonControls), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&commonControls);
    theme::EnableDarkMode();
    Editor editor;
    editor.instance = instance;
    editor.base = baseDir;
    editor.projectRoot = baseDir / L"project";
    editor.projectFile = baseDir / L"project" / L"DxStudio.dxproject";
    if (!initialProjectFile.empty()) {
        std::string error;
        if (!LoadProjectDescriptor(initialProjectFile, editor.projectRoot, error)) {
            Error(nullptr, error);
            return 1;
        }

        editor.projectFile = initialProjectFile;
    }

    WNDCLASSEXW viewportClass{sizeof(viewportClass)};
    viewportClass.lpfnWndProc = ViewportProc;
    viewportClass.hInstance = instance;
    viewportClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    viewportClass.lpszClassName = L"DxStudioViewport";
    if (!RegisterClassExW(&viewportClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DxStudioEditorWindow";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"DxStudio", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                  CW_USEDEFAULT, 1360, 820, nullptr, nullptr, instance, &editor);
    if (!window) return 1;
    const auto title = L"DxStudio  —  " + editor.projectFile.stem().wstring();
    SetWindowTextW(window, title.c_str());
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            editor.Command(ID_SAVE, BN_CLICKED);
            continue;
        }

        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        // Unity shortcuts: Ctrl+Shift+P pause, Ctrl+Alt+P step, Ctrl+Shift+C console.
        if (msg.message == WM_KEYDOWN && msg.wParam == 'P' && control && shift) {
            editor.ShellCommand(ID_PAUSE);
            continue;
        }

        if (msg.message == WM_SYSKEYDOWN && msg.wParam == 'P' && control) {
            editor.ShellCommand(ID_STEP);
            continue;
        }

        if (msg.message == WM_KEYDOWN && msg.wParam == 'C' && control && shift) {
            editor.ShellCommand(ID_SHOW_CONSOLE);
            continue;
        }

        // Alt + first letter opens a menu of the custom menu bar.
        if (msg.message == WM_SYSKEYDOWN && !control && !(msg.lParam & (1 << 30))) {
            const wchar_t keys[MENU_COUNT] = {L'F', L'E', L'A', L'G', L'C', L'B', L'W', L'H'};
            const auto found = std::find(std::begin(keys), std::end(keys), static_cast<wchar_t>(msg.wParam));
            if (found != std::end(keys)) {
                editor.OpenMainMenu(static_cast<int>(found - std::begin(keys)));
                continue;
            }
        }

        if (msg.message == WM_KEYDOWN && msg.wParam == 'P' && control) {
            editor.Command(ID_BUILD_DEBUG, BN_CLICKED);
            continue;
        }

        if (msg.message == WM_KEYDOWN && msg.wParam == 'D' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            editor.Command(ID_DUPLICATE, BN_CLICKED);
            continue;
        }

        if (msg.message == WM_KEYDOWN && msg.wParam == VK_F2 && editor.vSelectedIndices.size() == 1) {
            SetFocus(editor.edits[0]);
            SendMessageW(editor.edits[0], EM_SETSEL, 0, -1);
            continue;
        }

        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && editor.IsComponentFieldEdit(msg.hwnd)) {
            editor.CommitComponentField(GetDlgCtrlID(msg.hwnd), false);
            continue;
        }

        if (msg.message == WM_KEYDOWN && editor.vSelectedIndices.size() == 1) {
            const int field = GetDlgCtrlID(msg.hwnd) - ID_FIRST_EDIT;
            if (field >= 0 && field < FIELD_COUNT && msg.hwnd == editor.edits[field]) {
                if (msg.wParam == VK_RETURN) {
                    editor.Apply();
                    continue;
                }

                if (field >= 2 && (msg.wParam == VK_UP || msg.wParam == VK_DOWN)) {
                    try {
                        const auto current = Text(msg.hwnd);
                        size_t used = 0;
                        float value = std::stof(current, &used);
                        while (used < current.size() && iswspace(current[used])) {
                            ++used;
                        }

                        if (used == current.size() && std::isfinite(value)) {
                            float step = field >= 11 ? 0.01f : field >= 5 && field <= 7 ? 1.0f : 0.1f;
                            if (GetKeyState(VK_SHIFT) & 0x8000) {
                                step *= 10;
                            }

                            value += msg.wParam == VK_UP ? step : -step;
                            wchar_t text[64];
                            swprintf_s(text, L"%.9g", static_cast<double>(value));
                            SetWindowTextW(msg.hwnd, text);
                            editor.Apply();
                        }
                    } catch (...) { /* Keep invalid input for the user to correct. */
                    }

                    continue;
                }
            }
        }

        // Ensure dialog-style keyboard navigation cannot swallow Esc during a drag.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE &&
            (editor.drag == Editor::Drag::Gizmo || editor.drag == Editor::Drag::Select)) {
            SendMessageW(editor.viewport, WM_KEYDOWN, msg.wParam, msg.lParam);
            continue;
        }

        if (msg.message == WM_KEYDOWN &&
            (msg.hwnd == editor.viewport || msg.hwnd == editor.hierarchy || msg.hwnd == editor.window) &&
            editor.drag != Editor::Drag::Orbit && editor.drag != Editor::Drag::Gizmo && !(GetKeyState(VK_CONTROL) & 0x8000) &&
            !(GetKeyState(VK_MENU) & 0x8000)) {
            if (msg.wParam == 'W' || msg.wParam == 'E' || msg.wParam == 'R') {
                editor.Command(msg.wParam == 'W' ? ID_MOVE_MODE : msg.wParam == 'E' ? ID_ROTATE_MODE : ID_SCALE_MODE, BN_CLICKED);
                continue;
            }

            if (msg.wParam == 'F') {
                editor.FrameSelection();
                continue;
            }

            if (msg.wParam == VK_DELETE) {
                editor.Command(ID_DELETE, BN_CLICKED);
                continue;
            }
        }

        HWND dialogRoot = GetAncestor(msg.hwnd, GA_ROOT);
        if (!dialogRoot || !IsDialogMessageW(dialogRoot, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return static_cast<int>(msg.wParam);
}
} // namespace dxs
