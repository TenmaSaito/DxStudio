// Hierarchy and Inspector: selection, tree rows, inspected values and Apply.

#include "EditorWindow.h"

namespace dxs::editor {

void Editor::SyncHierarchy() {
    SendMessageW(hierarchy, LB_SETSEL, FALSE, -1);
    for (size_t row = 0; row < vHierarchyRows.size(); ++row) {
        if (std::find(vSelectedIndices.begin(), vSelectedIndices.end(), vHierarchyRows[row]) != vSelectedIndices.end())
            SendMessageW(hierarchy, LB_SETSEL, TRUE, static_cast<WPARAM>(row));
        if (vHierarchyRows[row] == nSelected) SendMessageW(hierarchy, LB_SETCARETINDEX, row, FALSE);
    }
}

std::vector<int> Editor::MergeSelection(const std::vector<int>& hits) const {
    auto result = (bSelectionAdd || bSelectionToggle) ? vSelectionBeforeDrag : std::vector<int>{};
    for (int hit : hits) {
        auto it = std::find(result.begin(), result.end(), hit);
        if (bSelectionToggle && it != result.end())
            result.erase(it);
        else if (it == result.end())
            result.push_back(hit);
    }
    return result;
}

void Editor::RestoreDrag() {
    for (const auto& entry : vGizmoStartItems)
        if (entry.first >= 0 && static_cast<size_t>(entry.first) < scene.vEntities.size())
            scene.vEntities[entry.first].draw = LocalDrawFromWorld(scene, entry.first, entry.second);
    Inspect();
}

void Editor::UpdateGroup(const DrawItem& updated) {
    using namespace DirectX;
    const auto pivot = XMLoadFloat3(&gizmoStartItem.position);
    const XMFLOAT3 dp{updated.position.x - gizmoStartItem.position.x, updated.position.y - gizmoStartItem.position.y,
                      updated.position.z - gizmoStartItem.position.z};
    const XMFLOAT3 dr{updated.rotation.x - gizmoStartItem.rotation.x, updated.rotation.y - gizmoStartItem.rotation.y,
                      updated.rotation.z - gizmoStartItem.rotation.z};
    const XMFLOAT3 factor{std::abs(gizmoStartItem.scale.x) > 1e-6f ? updated.scale.x / gizmoStartItem.scale.x : 1.f,
                          std::abs(gizmoStartItem.scale.y) > 1e-6f ? updated.scale.y / gizmoStartItem.scale.y : 1.f,
                          std::abs(gizmoStartItem.scale.z) > 1e-6f ? updated.scale.z / gizmoStartItem.scale.z : 1.f};
    const auto orientation =
        XMMatrixRotationRollPitchYaw(gizmoStartItem.rotation.x, gizmoStartItem.rotation.y, gizmoStartItem.rotation.z);
    const auto scale = XMMatrixTranspose(orientation) * XMMatrixScaling(factor.x, factor.y, factor.z) * orientation;
    const auto rotation = XMMatrixRotationRollPitchYaw(dr.x, dr.y, dr.z);
    for (const auto& entry : vGizmoStartItems) {
        if (entry.first == nSelected) {
            scene.vEntities[entry.first].draw = LocalDrawFromWorld(scene, entry.first, updated);
            continue;
        }
        auto d = entry.second;
        if (gizmoMode == GizmoMode::Translate) {
            d.position.x += dp.x;
            d.position.y += dp.y;
            d.position.z += dp.z;
        } else {
            const auto relative = XMVectorSubtract(XMLoadFloat3(&d.position), pivot);
            XMStoreFloat3(&d.position, XMVectorAdd(pivot, XMVector3TransformNormal(
                                                              relative, gizmoMode == GizmoMode::Scale ? scale : rotation)));
            if (gizmoMode == GizmoMode::Rotate) {
                d.rotation.x += dr.x;
                d.rotation.y += dr.y;
                d.rotation.z += dr.z;
            } else {
                d.scale.x *= factor.x;
                d.scale.y *= factor.y;
                d.scale.z *= factor.z;
            }
        }
        scene.vEntities[entry.first].draw = LocalDrawFromWorld(scene, entry.first, d);
    }
}

void Editor::FrameSelection() {
    if (vSelectedIndices.empty()) return;
    DirectX::XMFLOAT3 min{FLT_MAX, FLT_MAX, FLT_MAX}, max{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    const auto world = ResolveWorldDraws(scene);
    for (int i : vSelectedIndices) {
        const auto& d = world[i];
        const float extent = std::max({std::abs(d.scale.x), std::abs(d.scale.y), std::abs(d.scale.z)}) * 0.75f;
        min.x = std::min(min.x, d.position.x - extent);
        min.y = std::min(min.y, d.position.y - extent);
        min.z = std::min(min.z, d.position.z - extent);
        max.x = std::max(max.x, d.position.x + extent);
        max.y = std::max(max.y, d.position.y + extent);
        max.z = std::max(max.z, d.position.z + extent);
    }
    camera.target = {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f};
    camera.fDistance = std::clamp(std::max({max.x - min.x, max.y - min.y, max.z - min.z}) * 2.5f, 1.0f, 80.0f);
    renderer.SetCamera(camera);
    SetStatus(L"Focused selection. W / E / R: Move / Rotate / Scale.");
}

void Editor::Populate() {
    SendMessageW(hierarchy, LB_RESETCONTENT, 0, 0);
    vHierarchyRows.clear();
    auto lower = [](std::wstring value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return value;
    };
    const auto filter = lower(Text(hierarchySearch));
    std::vector<unsigned char> visible(scene.vEntities.size(), filter.empty() ? 1 : 0);
    if (!filter.empty()) {
        for (size_t i = 0; i < scene.vEntities.size(); ++i)
            if (lower(Wide(scene.vEntities[i].sName)).find(filter) != std::wstring::npos) {
                for (int node = static_cast<int>(i), steps = 0;
                     node >= 0 && static_cast<size_t>(node) < scene.vEntities.size() && steps++ < 64;
                     node = scene.vEntities[node].nParent)
                    visible[static_cast<size_t>(node)] = 1;
            }
    }
    // Depth-first tree rows. Collapsed parents hide their children unless
    // a search is active (search always reveals matching descendants).
    vCollapsedEntities.resize(scene.vEntities.size(), 0);
    vHierarchyDepths.clear();
    vHierarchyHasChildren.clear();
    std::vector<char> hasChildren(scene.vEntities.size(), 0);
    for (const auto& e : scene.vEntities)
        if (e.nParent >= 0 && static_cast<size_t>(e.nParent) < scene.vEntities.size())
            hasChildren[static_cast<size_t>(e.nParent)] = 1;
    auto add = [&](auto&& self, int parent, int depth) -> void {
        for (size_t index = 0; index < scene.vEntities.size(); ++index) {
            const auto& e = scene.vEntities[index];
            if (e.nParent != parent || !visible[index]) continue;
            // The string keeps the old indented text so screen readers and
            // LB_GETTEXT still describe the row; owner draw renders the tree.
            std::wstring name(static_cast<size_t>(depth) * 3, L' ');
            name += Wide(e.sName);
            SendMessageW(hierarchy, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
            vHierarchyRows.push_back(static_cast<int>(index));
            vHierarchyDepths.push_back(depth);
            vHierarchyHasChildren.push_back(hasChildren[index]);
            if (filter.empty() && vCollapsedEntities[index]) continue;
            self(self, static_cast<int>(index), depth + 1);
        }
    };
    add(add, -1, 0);
    vSelectedIndices.erase(std::remove_if(vSelectedIndices.begin(), vSelectedIndices.end(),
                                         [&](int i) { return i < 0 || static_cast<size_t>(i) >= scene.vEntities.size(); }),
                          vSelectedIndices.end());
    if (std::find(vSelectedIndices.begin(), vSelectedIndices.end(), nSelected) == vSelectedIndices.end())
        nSelected = vSelectedIndices.empty() ? -1 : vSelectedIndices.back();
    SyncHierarchy();
    Inspect();
}

void Editor::Inspect() {
    bool valid = nSelected >= 0 && nSelected < static_cast<int>(scene.vEntities.size());
    const bool single = valid && vSelectedIndices.size() == 1;
    for (auto e : edits)
        if (e) {
            EnableWindow(e, single);
            SetWindowTextW(e, L"");
        }
    for (auto control : {materialEdit, textureEdit}) {
        EnableWindow(control, single);
        SetWindowTextW(control, L"");
    }
    EnableWindow(controls[2], valid);
    EnableWindow(controls[3], single);
    EnableWindow(componentList, single);
    EnableWindow(componentChoice, single);
    EnableWindow(componentAdd, single);
    EnableWindow(componentRemove, single);
    EnableWindow(componentFields, single);
    EnableWindow(componentValue, single);
    EnableWindow(componentValueApply, single);
    EnableWindow(inspectorEnabled, single);
    bInspectorEnabledChecked = false;
    InvalidateRect(inspectorEnabled, nullptr, FALSE);
    InvalidateRect(inspectorIcon, nullptr, FALSE);
    int componentSelection = static_cast<int>(SendMessageW(componentList, LB_GETCURSEL, 0, 0));
    SendMessageW(componentList, LB_RESETCONTENT, 0, 0);
    SendMessageW(componentFields, LB_RESETCONTENT, 0, 0);
    vInspectedComponentFields.clear();
    SetWindowTextW(componentValue, L"");
    RebuildComponentViews();
    if (!valid) return;
    if (!single) {
        auto label = std::to_wstring(vSelectedIndices.size()) + L" objects selected";
        SetWindowTextW(edits[0], label.c_str());
        return;
    }
    const auto& entity = scene.vEntities[nSelected];
    bInspectorEnabledChecked = entity.draw.bVisible;
    SetWindowTextW(edits[0], Wide(entity.sName).c_str());
    SetWindowTextW(materialEdit, Wide(entity.sMaterialAsset).c_str());
    SetWindowTextW(textureEdit, Wide(entity.sTextureAsset).c_str());
    if (entity.renderer != Entity::RendererKind::None) {
        const wchar_t* rendererName = entity.renderer == Entity::RendererKind::Polygon ? L"PolygonRenderer [IRender]"
                                      : entity.renderer == Entity::RendererKind::Model ? L"ModelRenderer [IRender]"
                                                                                       : L"Image [IRender]";
        SendMessageW(componentList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(rendererName));
    }
    for (const auto& component : entity.vComponents) {
        const std::wstring name = Wide(component);
        SendMessageW(componentList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }
    if (componentSelection >= 0 && componentSelection < static_cast<int>(entity.vComponents.size()) +
                                                            (entity.renderer != Entity::RendererKind::None ? 1 : 0))
        SendMessageW(componentList, LB_SETCURSEL, componentSelection, 0);
    RefreshComponentFields();
    const auto& d = entity.draw;
    const float values[] = {d.position.x,
                            d.position.y,
                            d.position.z,
                            DirectX::XMConvertToDegrees(d.rotation.x),
                            DirectX::XMConvertToDegrees(d.rotation.y),
                            DirectX::XMConvertToDegrees(d.rotation.z),
                            d.scale.x,
                            d.scale.y,
                            d.scale.z,
                            d.color.x,
                            d.color.y,
                            d.color.z,
                            d.color.w};
    for (int i = 0; i < 13; ++i) {
        wchar_t buffer[64];
        // Nine significant digits preserve a float when it is reapplied.
        swprintf_s(buffer, L"%.9g", static_cast<double>(values[i]));
        SetWindowTextW(edits[i + 2], buffer);
    }
}

void Editor::RefreshComponentFields() {
    UpdateComponentViewValues();
    const int oldField = static_cast<int>(SendMessageW(componentFields, LB_GETCURSEL, 0, 0));
    const int listIndex = static_cast<int>(SendMessageW(componentList, LB_GETCURSEL, 0, 0));
    vInspectedComponentFields.clear();
    SendMessageW(componentFields, LB_RESETCONTENT, 0, 0);
    if (nSelected < 0 || static_cast<size_t>(nSelected) >= scene.vEntities.size() || listIndex == LB_ERR) return;
    auto& entity = scene.vEntities[nSelected];
    const int offset = entity.renderer == Entity::RendererKind::None ? 0 : 1;
    const int componentIndex = listIndex - offset;
    if (componentIndex < 0 || static_cast<size_t>(componentIndex) >= entity.vComponents.size()) return;
    vInspectedComponentFields = ComponentEditorValues(entity, static_cast<size_t>(componentIndex));
    for (const auto& [name, value] : vInspectedComponentFields) {
        const auto row = Wide(name + " = " + value);
        SendMessageW(componentFields, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
    }
    if (!vInspectedComponentFields.empty()) {
        const int chosen = std::clamp(oldField, 0, static_cast<int>(vInspectedComponentFields.size()) - 1);
        SendMessageW(componentFields, LB_SETCURSEL, chosen, 0);
        if (GetFocus() != componentValue)
            SetWindowTextW(componentValue, Wide(vInspectedComponentFields[chosen].second).c_str());
    }
}

bool Editor::Apply() {
    if (vSelectedIndices.size() != 1 || nSelected < 0 || nSelected >= static_cast<int>(scene.vEntities.size())) return true;
    std::string name = Utf8(Text(edits[0]));
    if (name.empty() || name.find_first_of("\r\n") != std::string::npos) {
        Error(window, "Enter a non-empty, single-line name.");
        return false;
    }
    float values[13]{};
    for (int i = 0; i < 13; ++i) {
        auto s = Text(edits[i + 2]);
        try {
            size_t consumed = 0;
            values[i] = std::stof(s, &consumed);
            while (consumed < s.size() && iswspace(s[consumed]))
                ++consumed;
            if (consumed != s.size() || !std::isfinite(values[i])) throw std::invalid_argument("number");
        } catch (...) {
            Error(window, "Enter a finite number for each transform and color field.");
            SetFocus(edits[i + 2]);
            return false;
        }
    }
    auto& entity = scene.vEntities[nSelected];
    entity.sName = name;
    auto assetPath = [&](HWND edit, const char* label, std::string& destination) {
        const auto value = Utf8(Text(edit));
        if (value.empty()) {
            destination.clear();
            return true;
        }
        auto relative = std::filesystem::u8path(value).lexically_normal();
        if (relative.is_absolute() || relative.empty() || *relative.begin() == L"..") {
            Error(window, std::string(label) + " must be a project-relative path.");
            SetFocus(edit);
            return false;
        }
        std::error_code ec;
        const auto full = std::filesystem::weakly_canonical(projectRoot / relative, ec);
        const auto root = std::filesystem::weakly_canonical(projectRoot, ec);
        const auto inside = full.lexically_relative(root);
        if (ec || inside.empty() || *inside.begin() == L".." || !std::filesystem::is_regular_file(full, ec) || ec) {
            Error(window, std::string(label) + " must refer to a file inside the active project.");
            SetFocus(edit);
            return false;
        }
        const auto normalized = inside.generic_u8string();
        destination.assign(reinterpret_cast<const char*>(normalized.data()), normalized.size());
        return true;
    };
    if (!assetPath(materialEdit, "Material", entity.sMaterialAsset) || !assetPath(textureEdit, "Texture", entity.sTextureAsset))
        return false;
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


// ---- Unity-style component sections ------------------------------------------
// Each attached component gets a titlebar (foldout, icon, title, ⋮) followed by
// one property row per EditorField. The views are rebuilt only when the
// selection or the attachment list changes; otherwise only values refresh.
void Editor::RebuildComponentViews() {
    const bool single = nSelected >= 0 && static_cast<size_t>(nSelected) < scene.vEntities.size() && vSelectedIndices.size() == 1;
    std::string signature;
    if (single) {
        signature = std::to_string(nSelected) + "|" + (bPlaying ? "P" : "E");
        for (const auto& name : scene.vEntities[nSelected].vComponents) signature += "|" + name;
    }
    if (signature == sComponentViewSignature) {
        UpdateComponentViewValues();
        return;
    }
    sComponentViewSignature = signature;
    for (auto& view : vComponentViews) {
        for (HWND control : {view.hHeader, view.hScriptLabel, view.hScriptValue})
            if (control) DestroyWindow(control);
        for (auto& row : view.vRows) {
            if (row.hLabel) DestroyWindow(row.hLabel);
            if (row.hValue) DestroyWindow(row.hValue);
        }
    }
    vComponentViews.clear();
    if (!single) {
        LayoutPanel(2);
        return;
    }
    auto& entity = scene.vEntities[nSelected];
    HWND panel = panels[2].window;
    auto make = [&](const wchar_t* className, const std::wstring& text, DWORD style, int id) {
        HWND control = Make(className, text.c_str(), style, id);
        SetParent(control, panel);
        return control;
    };
    int nFieldId = ID_COMPONENT_FIELD_FIRST;
    for (size_t nIndex = 0; nIndex < entity.vComponents.size() && nIndex < COMPONENT_VIEW_LIMIT; ++nIndex) {
        InspectorComponentView view;
        view.nComponentIndex = nIndex;
        view.sName = entity.vComponents[nIndex];
        const bool camera = view.sName == "Camera";
        view.hHeader = make(L"BUTTON", Wide(camera ? view.sName : view.sName + " (Script)"), BS_OWNERDRAW,
                            ID_COMPONENT_HEADER_FIRST + static_cast<int>(nIndex));
        theme::SetButtonSpec(view.hHeader, {theme::ButtonRole::HEADER, camera ? theme::Icon::CAMERA : theme::Icon::SCRIPT,
                                            camera ? theme::COLOR_TEXT : theme::COLOR_ICON_SCRIPT});
        if (!camera) {
            // Unity shows the MonoScript as a read-only object field.
            view.hScriptLabel = make(L"STATIC", L"Script", SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, 0);
            view.hScriptValue = make(L"EDIT", Wide(view.sName), ES_AUTOHSCROLL | ES_READONLY, 0);
        }
        if (!IsComponentRegistered(view.sName)) {
            InspectorFieldRow warning;
            warning.hLabel = make(L"STATIC", L"The associated script can not be loaded. Register the class and rebuild.", SS_LEFT | SS_NOPREFIX, 0);
            view.vRows.push_back(warning);
        } else {
            const auto values = ComponentEditorValues(entity, nIndex);
            std::vector<EditorField> fields;
            if (nIndex < entity.vRuntimeComponents.size() && entity.vRuntimeComponents[nIndex]) fields = entity.vRuntimeComponents[nIndex]->EditorFields();
            for (const auto& [field, value] : values) {
                if (nFieldId > ID_COMPONENT_FIELD_LAST) break;
                InspectorFieldRow row;
                row.sField = field;
                const auto info = std::find_if(fields.begin(), fields.end(), [&](const EditorField& candidate) { return candidate.sName == field; });
                row.bBoolean = info != fields.end() && info->type == EditorFieldType::Boolean;
                const bool readOnly = info != fields.end() && info->bReadOnly;
                row.hLabel = make(L"STATIC", Wide(field), SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS | SS_NOPREFIX, 0);
                if (row.bBoolean) {
                    row.bChecked = value == "true";
                    row.hValue = make(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, nFieldId);
                    theme::SetButtonSpec(row.hValue, {theme::ButtonRole::CHECKBOX, theme::Icon::NONE});
                } else {
                    row.hValue = make(L"EDIT", Wide(value), ES_AUTOHSCROLL | WS_TABSTOP | (readOnly ? ES_READONLY : 0), nFieldId);
                    SendMessageW(row.hValue, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
                }
                EnableWindow(row.hValue, !readOnly);
                ++nFieldId;
                view.vRows.push_back(row);
            }
        }
        vComponentViews.push_back(std::move(view));
    }
    LayoutPanel(2);
}

void Editor::UpdateComponentViewValues() {
    if (nSelected < 0 || static_cast<size_t>(nSelected) >= scene.vEntities.size()) return;
    auto& entity = scene.vEntities[nSelected];
    for (auto& view : vComponentViews) {
        if (view.nComponentIndex >= entity.vComponents.size() || !IsComponentRegistered(view.sName)) continue;
        const auto values = ComponentEditorValues(entity, view.nComponentIndex);
        for (auto& row : view.vRows) {
            if (!row.hValue) continue;
            const auto found = std::find_if(values.begin(), values.end(), [&](const auto& pair) { return pair.first == row.sField; });
            if (found == values.end()) continue;
            if (row.bBoolean) {
                const bool checked = found->second == "true";
                if (checked != row.bChecked) {
                    row.bChecked = checked;
                    InvalidateRect(row.hValue, nullptr, FALSE);
                }
            } else if (GetFocus() != row.hValue) {
                const std::wstring text = Wide(found->second);
                if (Text(row.hValue) != text) SetWindowTextW(row.hValue, text.c_str());
            }
        }
    }
}

InspectorFieldRow* Editor::FindFieldRow(int controlId, InspectorComponentView** ppView) {
    for (auto& view : vComponentViews)
        for (auto& row : view.vRows)
            if (row.hValue && GetDlgCtrlID(row.hValue) == controlId) {
                if (ppView) *ppView = &view;
                return &row;
            }
    return nullptr;
}

bool Editor::IsComponentFieldEdit(HWND control) const {
    for (const auto& view : vComponentViews) {
        if (control == view.hScriptValue) return true;
        for (const auto& row : view.vRows)
            if (row.hValue == control && !row.bBoolean) return true;
    }
    return false;
}

// Applies an edited property (Enter / focus loss) or a toggled check box.
void Editor::CommitComponentField(int controlId, bool bToggle) {
    InspectorComponentView* pView = nullptr;
    InspectorFieldRow* pRow = FindFieldRow(controlId, &pView);
    if (!pRow || !pView || nSelected < 0 || static_cast<size_t>(nSelected) >= scene.vEntities.size()) return;
    auto& entity = scene.vEntities[nSelected];
    std::string sValue;
    if (pRow->bBoolean) {
        if (!bToggle) return;
        pRow->bChecked = !pRow->bChecked;
        sValue = pRow->bChecked ? "true" : "false";
        InvalidateRect(pRow->hValue, nullptr, FALSE);
    } else {
        sValue = Utf8(Text(pRow->hValue));
    }
    std::string error;
    if (!SetComponentEditorValue(entity, pView->nComponentIndex, pRow->sField, sValue, error)) {
        Debug::LogWarning(pView->sName + "." + pRow->sField + ": " + error);
        SetStatus(Wide(error).c_str());
    } else {
        SetStatus(L"Component value applied. Values stay live during Play Mode.");
    }
    UpdateComponentViewValues();
    RefreshComponentFields();
}

// Routes the titlebar's ⋮ / right-click to the legacy component menu by
// selecting the matching row of the (hidden) component list first.
bool Editor::SelectComponentForWindow(HWND control) {
    if (nSelected < 0 || static_cast<size_t>(nSelected) >= scene.vEntities.size()) return false;
    const int offset = scene.vEntities[nSelected].renderer == Entity::RendererKind::None ? 0 : 1;
    if (control == groupLabels[1] && offset) {
        SendMessageW(componentList, LB_SETCURSEL, 0, 0);
        return true;
    }
    for (const auto& view : vComponentViews)
        if (control == view.hHeader) {
            SendMessageW(componentList, LB_SETCURSEL, offset + static_cast<int>(view.nComponentIndex), 0);
            RefreshComponentFields();
            return true;
        }
    return false;
}

// True when the cursor is over the ⋮ area at the right end of a titlebar.
bool Editor::IsOverTitlebarMenu(HWND header) const {
    POINT cursor{};
    GetCursorPos(&cursor);
    RECT rect{};
    GetWindowRect(header, &rect);
    return PtInRect(&rect, cursor) && cursor.x >= rect.right - MulDiv(26, static_cast<int>(GetDpiForWindow(header)), 96);
}

void Editor::ShowTitlebarMenu(HWND header) {
    RECT rect{};
    GetWindowRect(header, &rect);
    if (header == groupLabels[0]) {
        // Transform: Unity's "Reset".
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (vSelectedIndices.size() == 1 ? 0 : MF_GRAYED), 1, L"Reset");
        const int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, rect.right, rect.bottom, 0, window, nullptr);
        DestroyMenu(menu);
        if (choice == 1 && nSelected >= 0 && static_cast<size_t>(nSelected) < scene.vEntities.size()) {
            auto& draw = scene.vEntities[nSelected].draw;
            draw.position = {0, 0, 0};
            draw.rotation = {0, 0, 0};
            draw.scale = {1, 1, 1};
            Inspect();
            SetStatus(L"Transform reset.");
        }
        return;
    }
    if (SelectComponentForWindow(header)) ShowComponentMenu({rect.right - MulDiv(150, static_cast<int>(GetDpiForWindow(header)), 96), rect.bottom});
}

// Unity's "Add Component" button opens a categorised list of components.
void Editor::ShowAddComponentMenu() {
    RECT rect{};
    GetWindowRect(componentAdd, &rect);
    HMENU menu = CreatePopupMenu(), rendering = CreatePopupMenu(), scripts = CreatePopupMenu();
    const int count = static_cast<int>(SendMessageW(componentChoice, CB_GETCOUNT, 0, 0));
    std::vector<std::string> attached;
    if (nSelected >= 0 && static_cast<size_t>(nSelected) < scene.vEntities.size()) attached = scene.vEntities[nSelected].vComponents;
    for (int nIndex = 0; nIndex < count; ++nIndex) {
        const int length = static_cast<int>(SendMessageW(componentChoice, CB_GETLBTEXTLEN, nIndex, 0));
        std::wstring text(static_cast<size_t>(length) + 1, L'\0');
        SendMessageW(componentChoice, CB_GETLBTEXT, nIndex, reinterpret_cast<LPARAM>(text.data()));
        text.resize(length);
        const bool renderer = text == L"PolygonRenderer" || text == L"ModelRenderer" || text == L"Image";
        const bool isAttached = std::find(attached.begin(), attached.end(), Utf8(text)) != attached.end();
        AppendMenuW(renderer ? rendering : scripts, MF_STRING | (isAttached ? MF_GRAYED : 0), 1 + nIndex, text.c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(rendering), L"Rendering");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(scripts), L"Scripts");
    const int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_CENTERALIGN | TPM_TOPALIGN, (rect.left + rect.right) / 2, rect.bottom, 0, window, nullptr);
    DestroyMenu(menu);
    if (choice <= 0) return;
    SendMessageW(componentChoice, CB_SETCURSEL, choice - 1, 0);
    Command(ID_ADD_COMPONENT, BN_CLICKED); // existing attach logic
}

} // namespace dxs::editor
