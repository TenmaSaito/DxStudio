// Hierarchy and Inspector: selection, tree rows, inspected values and Apply.

#include "EditorWindow.h"

#include <cwctype>

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
    EnableWindow(meshPick, single);
    for (auto control : {materialEdit, textureEdit, meshEdit}) {
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
    SetWindowTextW(meshEdit, Wide(entity.sModelAsset).c_str());
    if (entity.renderer != Entity::RendererKind::None) {
        const wchar_t* rendererName = entity.renderer == Entity::RendererKind::Mesh      ? L"MeshRenderer [IRender]"
                                      : entity.renderer == Entity::RendererKind::Polygon ? L"PolygonRenderer [IRender]"
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
        ReportApplyError("Enter a non-empty, single-line name.");
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
            ReportApplyError("Enter a finite number for each transform and color field.");
            if (!bQuietApply) SetFocus(edits[i + 2]);
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
            ReportApplyError(std::string(label) + " must be a project-relative path.");
            if (!bQuietApply) SetFocus(edit);
            return false;
        }
        std::error_code ec;
        const auto full = std::filesystem::weakly_canonical(projectRoot / relative, ec);
        const auto root = std::filesystem::weakly_canonical(projectRoot, ec);
        const auto inside = full.lexically_relative(root);
        if (ec || inside.empty() || *inside.begin() == L".." || !std::filesystem::is_regular_file(full, ec) || ec) {
            ReportApplyError(std::string(label) + " must refer to a file inside the active project.");
            if (!bQuietApply) SetFocus(edit);
            return false;
        }
        const auto normalized = inside.generic_u8string();
        destination.assign(reinterpret_cast<const char*>(normalized.data()), normalized.size());
        return true;
    };
    if (!assetPath(materialEdit, "Material", entity.sMaterialAsset) || !assetPath(textureEdit, "Texture", entity.sTextureAsset))
        return false;
    if (entity.renderer == Entity::RendererKind::Mesh || entity.renderer == Entity::RendererKind::Model) {
        std::string sMesh;
        if (!assetPath(meshEdit, "Mesh", sMesh)) return false;
        if (sMesh.empty()) {
            ReportApplyError("Mesh must name an X or FBX file (pick one with the button).");
            if (!bQuietApply) SetFocus(meshEdit);
            return false;
        }
        entity.sModelAsset = sMesh;
    }
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
        auto& selected = scene.vEntities[nSelected];
        for (size_t nIndex = 0; nIndex < selected.vComponents.size(); ++nIndex) {
            signature += "|" + selected.vComponents[nIndex];
            // Rows depend on array sizes too.
            if (IsComponentRegistered(selected.vComponents[nIndex]))
                for (const auto& info : ComponentFieldInfos(selected, nIndex)) signature += "," + info.sPath;
        }
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
            for (HWND control : row.vParts)
                if (control != row.hValue) DestroyWindow(control);
            for (HWND control : row.vPartLabels) DestroyWindow(control);
            for (HWND control : {row.hLabel, row.hValue, row.hSlider})
                if (control) DestroyWindow(control);
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
            for (const auto& info : ComponentFieldInfos(entity, nIndex)) {
                const int nPartCount = info.type == EditorFieldType::Vector3 ? 3 : info.type == EditorFieldType::Vector2 ? 2 : 1;
                if (nFieldId + nPartCount - 1 > ID_COMPONENT_FIELD_LAST) {
                    view.bTruncated = true; // too many rows for the control id range
                    break;
                }
                InspectorFieldRow row;
                row.sField = info.sPath;
                row.type = info.type;
                row.nIndent = info.nIndent;
                row.sArrayPath = info.sArrayPath;
                row.bArraySize = info.bArraySize;
                row.vEnumNames = info.vEnumNames;
                row.fMin = info.fMin;
                row.fMax = info.fMax;
                const bool readOnly = info.bReadOnly;
                if (info.bArrayHeader) {
                    // Unity array foldout: "▼ Points".
                    row.bHeader = true;
                    row.hLabel = make(L"BUTTON", Wide(info.sLabel), BS_OWNERDRAW, nFieldId++);
                    theme::SetButtonSpec(row.hLabel, {theme::ButtonRole::FOLDOUT, theme::Icon::NONE});
                    theme::SetButtonExpanded(row.hLabel, !collapsedArrays[view.sName + "/" + info.sPath]);
                    view.vRows.push_back(row);
                    continue;
                }
                row.bBoolean = info.type == EditorFieldType::Boolean;
                row.bInteger = info.type == EditorFieldType::Integer;
                row.bNumeric = row.bInteger || info.type == EditorFieldType::Float;
                row.bEnum = info.type == EditorFieldType::Enum;
                row.hLabel = make(L"STATIC", Wide(info.sLabel), SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS | SS_NOPREFIX, 0);
                auto numericEdit = [&](const std::wstring& text, int nId) {
                    HWND edit = make(L"EDIT", text, ES_AUTOHSCROLL | WS_TABSTOP | (readOnly ? ES_READONLY : 0), nId);
                    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
                    if (!readOnly)
                        SetWindowSubclass(edit, NumericFieldSubclassProc, FIELD_SCRUB_COMPONENT + static_cast<UINT_PTR>(nId),
                                          reinterpret_cast<DWORD_PTR>(this));
                    return edit;
                };
                if (row.bBoolean) {
                    row.bChecked = info.sValue == "true";
                    row.hValue = make(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, nFieldId++);
                    theme::SetButtonSpec(row.hValue, {theme::ButtonRole::CHECKBOX, theme::Icon::NONE});
                } else if (row.bEnum) {
                    // Drop-down of the enum value names.
                    // Owner-drawn so it matches the dark input fields (DrawEnumItem).
                    row.hValue = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, nFieldId++);
                    theme::ApplyDarkInput(row.hValue);
                    const int nDpi = static_cast<int>(GetDpiForWindow(panel));
                    SendMessageW(row.hValue, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), MulDiv(15, nDpi, 96));
                    SendMessageW(row.hValue, CB_SETITEMHEIGHT, 0, MulDiv(20, nDpi, 96));
                    SetWindowSubclass(row.hValue, WheelToPanelSubclassProc, 1, 1);
                    for (const auto& sName : info.vEnumNames)
                        SendMessageW(row.hValue, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Wide(sName).c_str()));
                    const auto found = std::find(info.vEnumNames.begin(), info.vEnumNames.end(), info.sValue);
                    SendMessageW(row.hValue, CB_SETCURSEL, found == info.vEnumNames.end() ? -1 : found - info.vEnumNames.begin(), 0);
                } else if (nPartCount > 1) {
                    // Vector2 / Vector3: X Y Z on one row (like Transform).
                    const auto vValues = SplitVectorText(info.sValue, nPartCount);
                    const wchar_t* aAxis[] = {L"X", L"Y", L"Z"};
                    for (int nPart = 0; nPart < nPartCount; ++nPart) {
                        row.vPartLabels.push_back(make(L"STATIC", aAxis[nPart], SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX, 0));
                        row.vParts.push_back(numericEdit(Wide(vValues[static_cast<size_t>(nPart)]), nFieldId++));
                    }
                    row.hValue = row.vParts[0];
                    row.bNumeric = true;
                } else if (row.bNumeric) {
                    row.hValue = numericEdit(Wide(DisplayValue(row, info.sValue)), nFieldId++);
                    if (info.bRange) {
                        // Unity [Range]: slider + value box.
                        row.hSlider = make(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | TBS_BOTH | WS_TABSTOP, 0);
                        theme::ApplyDarkControl(row.hSlider);
                        SetWindowSubclass(row.hSlider, WheelToPanelSubclassProc, 1, 0);
                        SendMessageW(row.hSlider, TBM_SETRANGE, FALSE, MAKELPARAM(0, SliderSteps(row)));
                        SetSliderFromValue(row, info.sValue);
                        EnableWindow(row.hSlider, !readOnly);
                    }
                } else {
                    row.hValue = make(L"EDIT", Wide(info.sValue), ES_AUTOHSCROLL | WS_TABSTOP | (readOnly ? ES_READONLY : 0), nFieldId++);
                    SendMessageW(row.hValue, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
                }
                for (HWND control : row.vParts) EnableWindow(control, !readOnly);
                EnableWindow(row.hValue, !readOnly);
                view.vRows.push_back(std::move(row));
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
        const auto vInfos = ComponentFieldInfos(entity, view.nComponentIndex);
        // A script resized an array (Play Mode): rebuild the rows once idle.
        size_t nRows = 0;
        for (const auto& row : view.vRows) nRows += row.sField.empty() ? 0 : 1;
        bool bSameRows = nRows == vInfos.size() || (view.bTruncated && nRows < vInfos.size());
        for (size_t nIndex = 0; bSameRows && nIndex < vInfos.size(); ++nIndex)
            bSameRows = nIndex < view.vRows.size() && view.vRows[nIndex].sField == vInfos[nIndex].sPath;
        if (!bSameRows) {
            RequestInspectorRebuild();
            return;
        }
        for (size_t nIndex = 0; nIndex < vInfos.size() && nIndex < view.vRows.size(); ++nIndex) {
            auto& row = view.vRows[nIndex];
            const auto& sValue = vInfos[nIndex].sValue;
            if (row.bHeader || !row.hValue) continue;
            if (row.bBoolean) {
                const bool checked = sValue == "true";
                if (checked != row.bChecked) {
                    row.bChecked = checked;
                    InvalidateRect(row.hValue, nullptr, FALSE);
                }
            } else if (row.bEnum) {
                if (SendMessageW(row.hValue, CB_GETDROPPEDSTATE, 0, 0)) continue;
                const auto found = std::find(row.vEnumNames.begin(), row.vEnumNames.end(), sValue);
                const int nSelection = found == row.vEnumNames.end() ? -1 : static_cast<int>(found - row.vEnumNames.begin());
                if (SendMessageW(row.hValue, CB_GETCURSEL, 0, 0) != nSelection) SendMessageW(row.hValue, CB_SETCURSEL, nSelection, 0);
            } else if (!row.vParts.empty()) {
                const auto vValues = SplitVectorText(sValue, static_cast<int>(row.vParts.size()));
                for (size_t nPart = 0; nPart < row.vParts.size(); ++nPart) {
                    if (GetFocus() == row.vParts[nPart]) continue;
                    const std::wstring text = Wide(vValues[nPart]);
                    if (Text(row.vParts[nPart]) != text) SetWindowTextW(row.vParts[nPart], text.c_str());
                }
            } else if (GetFocus() != row.hValue) {
                const std::wstring text = Wide(DisplayValue(row, sValue));
                if (Text(row.hValue) != text) SetWindowTextW(row.hValue, text.c_str());
            }
            if (row.hSlider && GetCapture() != row.hSlider) SetSliderFromValue(row, sValue);
        }
    }
}

InspectorFieldRow* Editor::FindFieldRow(int controlId, InspectorComponentView** ppView) {
    auto matches = [controlId](HWND control) { return control && GetDlgCtrlID(control) == controlId; };
    for (auto& view : vComponentViews)
        for (auto& row : view.vRows) {
            bool bFound = matches(row.hValue) || (row.bHeader && matches(row.hLabel));
            for (HWND part : row.vParts) bFound = bFound || matches(part);
            if (bFound) {
                if (ppView) *ppView = &view;
                return &row;
            }
        }
    return nullptr;
}

bool Editor::IsComponentFieldEdit(HWND control) const {
    if (!control) return false;
    for (const auto& view : vComponentViews) {
        if (control == view.hScriptValue) return true;
        for (const auto& row : view.vRows) {
            if (row.hValue == control && !row.bBoolean && !row.bEnum) return true;
            if (std::find(row.vParts.begin(), row.vParts.end(), control) != row.vParts.end()) return true;
        }
    }
    return false;
}

// Applies an edited property (Enter / focus loss / drop-down / slider) or a
// toggled check box; an array header toggles its foldout.
void Editor::CommitComponentField(int controlId, bool bToggle) {
    InspectorComponentView* pView = nullptr;
    InspectorFieldRow* pRow = FindFieldRow(controlId, &pView);
    if (!pRow || !pView || nSelected < 0 || static_cast<size_t>(nSelected) >= scene.vEntities.size()) return;
    if (pRow->bHeader) {
        if (!bToggle) return;
        bool& bCollapsed = collapsedArrays[pView->sName + "/" + pRow->sField];
        bCollapsed = !bCollapsed;
        theme::SetButtonExpanded(pRow->hLabel, !bCollapsed);
        LayoutPanel(2);
        return;
    }
    auto& entity = scene.vEntities[nSelected];
    std::string sValue;
    if (pRow->bBoolean) {
        if (!bToggle) return;
        pRow->bChecked = !pRow->bChecked;
        sValue = pRow->bChecked ? "true" : "false";
        InvalidateRect(pRow->hValue, nullptr, FALSE);
    } else if (pRow->bEnum) {
        const int nSelection = static_cast<int>(SendMessageW(pRow->hValue, CB_GETCURSEL, 0, 0));
        if (nSelection < 0 || static_cast<size_t>(nSelection) >= pRow->vEnumNames.size()) return;
        sValue = pRow->vEnumNames[static_cast<size_t>(nSelection)];
    } else if (!pRow->vParts.empty()) {
        for (size_t nPart = 0; nPart < pRow->vParts.size(); ++nPart)
            sValue += (nPart ? "," : "") + Utf8(Text(pRow->vParts[nPart]));
    } else {
        sValue = Utf8(Text(pRow->hValue));
    }
    std::string error;
    const std::string sField = pRow->sField;
    const bool bStructural = pRow->bArraySize;
    if (!SetComponentEditorValue(entity, pView->nComponentIndex, sField, sValue, error)) {
        Debug::LogWarning(pView->sName + "." + sField + ": " + error);
        SetStatus(Wide(error).c_str());
    } else {
        SetStatus(L"Component value applied. Values stay live during Play Mode.");
    }
    // A new array size adds / removes rows: rebuild after this notification returns.
    if (bStructural) {
        RequestInspectorRebuild();
        return;
    }
    UpdateComponentViewValues();
    RefreshComponentFields();
}

void Editor::RequestInspectorRebuild() {
    if (bInspectorRebuildPosted) return;
    bInspectorRebuildPosted = true;
    PostMessageW(window, WM_DXS_REBUILD_INSPECTOR, 0, 0);
}

void Editor::RebuildInspectorNow() {
    bInspectorRebuildPosted = false;
    sComponentViewSignature.clear();
    RebuildComponentViews();
    RefreshComponentFields();
}

int Editor::SliderSteps(const InspectorFieldRow& row) const {
    if (row.bInteger) return std::clamp(static_cast<int>(std::lround(row.fMax - row.fMin)), 1, 10000);
    return 1000;
}

void Editor::SetSliderFromValue(const InspectorFieldRow& row, const std::string& sValue) {
    if (!row.hSlider) return;
    float fValue = 0.0f;
    try { fValue = std::stof(sValue); } catch (...) { return; }
    const float fSpan = row.fMax - row.fMin;
    const float fRatio = fSpan > 0.0f ? std::clamp((fValue - row.fMin) / fSpan, 0.0f, 1.0f) : 0.0f;
    const int nPosition = static_cast<int>(std::lround(fRatio * static_cast<float>(SliderSteps(row))));
    if (SendMessageW(row.hSlider, TBM_GETPOS, 0, 0) != nPosition) SendMessageW(row.hSlider, TBM_SETPOS, TRUE, nPosition);
}

// Slider moved (WM_HSCROLL): write the value box and apply it live.
void Editor::OnInspectorSlider(HWND hSlider) {
    for (auto& view : vComponentViews)
        for (auto& row : view.vRows) {
            if (row.hSlider != hSlider || !row.hValue) continue;
            const float fRatio = static_cast<float>(SendMessageW(hSlider, TBM_GETPOS, 0, 0)) / static_cast<float>(SliderSteps(row));
            const float fValue = row.fMin + fRatio * (row.fMax - row.fMin);
            wchar_t aBuffer[64];
            if (row.bInteger) swprintf_s(aBuffer, L"%d", static_cast<int>(std::lround(fValue)));
            else swprintf_s(aBuffer, L"%.4g", static_cast<double>(fValue));
            SetWindowTextW(row.hValue, aBuffer);
            CommitComponentField(GetDlgCtrlID(row.hValue), false);
            RequestRender();
            return;
        }
}

// Sliders and drop-downs would change their value under the mouse wheel while
// the Inspector is being scrolled; unless focused they pass it to the panel.
// dwRefData: 1 for a combo box (its open list keeps the wheel).
LRESULT CALLBACK WheelToPanelSubclassProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR uId, DWORD_PTR bComboBox) {
    if (message == WM_MOUSEWHEEL && GetFocus() != hwnd && !(bComboBox && SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0)))
        return SendMessageW(GetParent(hwnd), WM_MOUSEWHEEL, wp, lp);
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, WheelToPanelSubclassProc, uId);
    return DefSubclassProc(hwnd, message, wp, lp);
}

// Floats are stored with full precision ("0.800000012"); show them short ("0.8").
std::string DisplayValue(const InspectorFieldRow& row, const std::string& sValue) {
    if (row.type != EditorFieldType::Float) return sValue;
    try {
        const float fValue = std::stof(sValue);
        char aBuffer[32];
        std::snprintf(aBuffer, sizeof(aBuffer), "%.6g", static_cast<double>(fValue));
        return aBuffer;
    } catch (...) {
        return sValue;
    }
}

// Enum drop-down (CBS_OWNERDRAWFIXED): dark field with a chevron, dark list.
void Editor::DrawEnumItem(const DRAWITEMSTRUCT& item) {
    const bool bField = (item.itemState & ODS_COMBOBOXEDIT) != 0;
    const bool bHighlighted = (item.itemState & ODS_SELECTED) != 0 && !bField;
    RECT rect = item.rcItem;
    FillRect(item.hDC, &rect, theme::Brush(bHighlighted ? theme::COLOR_SELECTION : theme::COLOR_INPUT));
    std::wstring sText;
    if (item.itemID != static_cast<UINT>(-1)) {
        const int nLength = static_cast<int>(SendMessageW(item.hwndItem, CB_GETLBTEXTLEN, item.itemID, 0));
        if (nLength > 0) {
            sText.resize(static_cast<size_t>(nLength) + 1);
            SendMessageW(item.hwndItem, CB_GETLBTEXT, item.itemID, reinterpret_cast<LPARAM>(sText.data()));
            sText.resize(static_cast<size_t>(nLength));
        }
    }
    RECT textRect{rect.left + 4, rect.top, rect.right - 4, rect.bottom}; // the control draws its own arrow button
    const bool bDisabled = (item.itemState & ODS_DISABLED) != 0;
    theme::DrawLabel(item.hDC, sText, textRect, bDisabled ? theme::COLOR_TEXT_DISABLED : bHighlighted ? theme::COLOR_TEXT_BRIGHT : theme::COLOR_TEXT,
                     theme::GetFonts().hRegular, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

// "1,2,3" -> {"1","2","3"} (missing parts are empty).
std::vector<std::string> SplitVectorText(const std::string& sValue, int nCount) {
    std::vector<std::string> vParts;
    std::string sPart;
    for (const char c : sValue) {
        if (c == ',') {
            vParts.push_back(sPart);
            sPart.clear();
        } else {
            sPart += c;
        }
    }
    vParts.push_back(sPart);
    vParts.resize(static_cast<size_t>(std::max(nCount, 1)));
    for (auto& part : vParts) {
        // Shorter display: 0.100000001 -> 0.1
        try {
            size_t nUsed = 0;
            const float fValue = std::stof(part, &nUsed);
            char aBuffer[32];
            std::snprintf(aBuffer, sizeof(aBuffer), "%.6g", static_cast<double>(fValue));
            part = aBuffer;
        } catch (...) {
        }
    }
    return vParts;
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
    HMENU menu = CreatePopupMenu(), rendering = CreatePopupMenu(), physicsMenu = CreatePopupMenu(), inputMenu = CreatePopupMenu(), scripts = CreatePopupMenu();
    const int count = static_cast<int>(SendMessageW(componentChoice, CB_GETCOUNT, 0, 0));
    std::vector<std::string> attached;
    if (nSelected >= 0 && static_cast<size_t>(nSelected) < scene.vEntities.size()) attached = scene.vEntities[nSelected].vComponents;
    for (int nIndex = 0; nIndex < count; ++nIndex) {
        const int length = static_cast<int>(SendMessageW(componentChoice, CB_GETLBTEXTLEN, nIndex, 0));
        std::wstring text(static_cast<size_t>(length) + 1, L'\0');
        SendMessageW(componentChoice, CB_GETLBTEXT, nIndex, reinterpret_cast<LPARAM>(text.data()));
        text.resize(length);
        const bool renderer = text == L"MeshRenderer" || text == L"PolygonRenderer" || text == L"ModelRenderer" || text == L"Image";
        const bool isAttached = std::find(attached.begin(), attached.end(), Utf8(text)) != attached.end();
        const bool physicsComponent = text == L"Rigidbody" || text == L"BoxCollider" || text == L"SphereCollider" || text == L"MeshCollider";
        const bool inputComponent = text == L"PlayerInput";
        AppendMenuW(renderer ? rendering : physicsComponent ? physicsMenu : inputComponent ? inputMenu : scripts, MF_STRING | (isAttached ? MF_GRAYED : 0), 1 + nIndex,
                    text.c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(rendering), L"Rendering");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(physicsMenu), L"Physics");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(inputMenu), L"Input");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(scripts), L"Scripts");
    const int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_CENTERALIGN | TPM_TOPALIGN, (rect.left + rect.right) / 2, rect.bottom, 0, window, nullptr);
    DestroyMenu(menu);
    if (choice <= 0) return;
    SendMessageW(componentChoice, CB_SETCURSEL, choice - 1, 0);
    Command(ID_ADD_COMPONENT, BN_CLICKED); // existing attach logic
}

void Editor::EnsureProjectPrimitives() {
    std::string error;
    if (!EnsurePrimitiveAssets(projectRoot, error)) Debug::LogWarning(error);
}

void Editor::AppendPrimitiveMenus(HMENU menu) {
    HMENU objects3D = CreatePopupMenu(), objects2D = CreatePopupMenu();
    for (size_t nIndex = 0; nIndex < PRIMITIVES.size(); ++nIndex)
        AppendMenuW(PRIMITIVES[nIndex].bFlat ? objects2D : objects3D, MF_STRING, ID_ADD_PRIMITIVE_FIRST + static_cast<int>(nIndex),
                    Wide(PRIMITIVES[nIndex].pName).c_str());
    AppendMenuW(objects2D, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(objects2D, MF_STRING, ID_ADD_SPRITE, L"World Quad (Polygon Renderer)");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(objects3D), L"3D Object");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(objects2D), L"2D Object");
}

void Editor::AddPrimitive(PrimitiveShape shape) {
    if (!Apply()) return;
    EnsureProjectPrimitives();
    Entity entity;
    for (const auto& info : PRIMITIVES)
        if (info.shape == shape) entity.sName = info.pName;
    entity.renderer = Entity::RendererKind::Mesh;
    entity.sModelAsset = PrimitiveAssetPath(shape);
    for (const auto& info : PRIMITIVES)
        if (info.shape == shape) entity.draw.bIs2D = info.bFlat; // quad stand-in for players without mesh support
    entity.draw.color = {0.82f, 0.82f, 0.82f, 1.f};
    scene.vEntities.push_back(std::move(entity));
    nSelected = static_cast<int>(scene.vEntities.size()) - 1;
    vSelectedIndices = {nSelected};
    Populate();
    SetStatus(L"Object added with a MeshRenderer. Save the scene to keep it.");
}

// Unity object picker for the Mesh field: built-in primitives or any X / FBX.
void Editor::ShowMeshPicker() {
    if (nSelected < 0 || static_cast<size_t>(nSelected) >= scene.vEntities.size() || vSelectedIndices.size() != 1) return;
    constexpr int BROWSE = 1000;
    RECT rect{};
    GetWindowRect(meshPick, &rect);
    HMENU menu = CreatePopupMenu();
    const auto& current = scene.vEntities[nSelected].sModelAsset;
    for (size_t nIndex = 0; nIndex < PRIMITIVES.size(); ++nIndex) {
        const bool bCurrent = current == PrimitiveAssetPath(PRIMITIVES[nIndex].shape);
        AppendMenuW(menu, MF_STRING | (bCurrent ? MF_CHECKED : 0), 1 + static_cast<int>(nIndex), Wide(PRIMITIVES[nIndex].pName).c_str());
        if (nIndex + 1 < PRIMITIVES.size() && PRIMITIVES[nIndex].bFlat != PRIMITIVES[nIndex + 1].bFlat)
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, BROWSE, L"Browse X / FBX...");
    const int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, rect.right, rect.bottom, 0, window, nullptr);
    DestroyMenu(menu);
    if (choice <= 0) return;
    if (InspectorHasPendingEdits() && !Apply()) return;
    std::string sAsset;
    if (choice == BROWSE) {
        std::filesystem::path source;
        if (!ChooseModelFile(window, source)) return;
        const auto folder = projectRoot / L"Assets" / L"Models";
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        const auto destination = folder / source.filename();
        const auto inside = std::filesystem::weakly_canonical(source, ec).lexically_relative(std::filesystem::weakly_canonical(projectRoot, ec));
        std::filesystem::path relative;
        if (!inside.empty() && *inside.begin() != L"..") {
            relative = inside; // already a project file: reference it in place
        } else {
            std::wstring importError;
            if (!ImportProjectFile(projectRoot, folder, source, importError)) {
                Error(window, Utf8(importError));
                return;
            }
            relative = std::filesystem::relative(destination, projectRoot, ec);
        }
        const auto u8 = relative.generic_u8string();
        sAsset.assign(reinterpret_cast<const char*>(u8.data()), u8.size());
    } else {
        sAsset = PrimitiveAssetPath(PRIMITIVES[static_cast<size_t>(choice - 1)].shape);
        scene.vEntities[nSelected].draw.bIs2D = PRIMITIVES[static_cast<size_t>(choice - 1)].bFlat;
        EnsureProjectPrimitives();
    }
    auto& entity = scene.vEntities[nSelected];
    entity.sModelAsset = sAsset;
    if (entity.renderer == Entity::RendererKind::None) entity.renderer = Entity::RendererKind::Mesh;
    Populate();
    SetStatus(L"Mesh assigned. Save the scene to keep it.");
}

} // namespace dxs::editor

namespace dxs::editor {

void Editor::ReportApplyError(const std::string& message) {
    if (!bQuietApply) {
        Error(window, message);
        return;
    }
    // Focus-loss apply: no modal box (it would steal focus again); the field
    // is restored from the scene by the caller.
    Debug::LogWarning(message);
    SetStatus(Wide(message).c_str());
}

bool Editor::IsInspectorEdit(HWND control) const {
    if (!control) return false;
    if (control == materialEdit || control == textureEdit || control == meshEdit) return true;
    for (int field = 0; field < FIELD_COUNT; ++field)
        if (field != 1 && edits[field] == control) return true;
    return false;
}

bool Editor::InspectorHasPendingEdits() const {
    for (HWND control : {materialEdit, textureEdit, meshEdit})
        if (control && SendMessageW(control, EM_GETMODIFY, 0, 0)) return true;
    for (int field = 0; field < FIELD_COUNT; ++field)
        if (field != 1 && edits[field] && SendMessageW(edits[field], EM_GETMODIFY, 0, 0)) return true;
    return false;
}

void Editor::ApplyOnFocusLoss(HWND control) {
    if (bQuietApply || vSelectedIndices.size() != 1 || !SendMessageW(control, EM_GETMODIFY, 0, 0)) return;
    bQuietApply = true;
    const bool bApplied = Apply();
    bQuietApply = false;
    if (!bApplied) Inspect(); // invalid input: show the values the object really has
}

} // namespace dxs::editor
namespace dxs::editor {

void Editor::RequestRender() {
    const ULONGLONG nNow = GetTickCount64();
    if (nNow - nLastRenderTick >= 15) Draw();
}

bool Editor::SetFieldValue(int field, float fValue) {
    if (field < 2 || field > 14 || vSelectedIndices.size() != 1 || nSelected < 0 ||
        static_cast<size_t>(nSelected) >= scene.vEntities.size() || !std::isfinite(fValue))
        return false;
    auto& d = scene.vEntities[nSelected].draw;
    float* apTargets[] = {&d.position.x, &d.position.y, &d.position.z, &d.rotation.x, &d.rotation.y, &d.rotation.z,
                          &d.scale.x,    &d.scale.y,    &d.scale.z,    &d.color.x,    &d.color.y,    &d.color.z, &d.color.w};
    // Rotation fields show degrees; the scene stores radians.
    *apTargets[field - 2] = field >= 5 && field <= 7 ? DirectX::XMConvertToRadians(fValue) : fValue;
    return true;
}

void Editor::ApplyFieldLive(int field) {
    // Only the field the user is typing in; programmatic SetWindowText of
    // other fields also sends EN_CHANGE and must not write back.
    if (field < 2 || field > 14 || GetFocus() != edits[field]) return;
    const std::wstring sText = Text(edits[field]);
    if (sText.empty()) return;
    wchar_t* pEnd = nullptr;
    const float fValue = std::wcstof(sText.c_str(), &pEnd);
    while (pEnd && *pEnd && std::iswspace(*pEnd)) ++pEnd;
    if (!pEnd || *pEnd || pEnd == sText.c_str()) return; // incomplete input such as "-" or "1e"
    if (SetFieldValue(field, fValue)) RequestRender();
}

void Editor::RefreshTransformFields() {
    if (vSelectedIndices.size() != 1 || nSelected < 0 || static_cast<size_t>(nSelected) >= scene.vEntities.size()) return;
    const auto& d = scene.vEntities[nSelected].draw;
    const float afValues[] = {d.position.x, d.position.y, d.position.z,
                              DirectX::XMConvertToDegrees(d.rotation.x), DirectX::XMConvertToDegrees(d.rotation.y),
                              DirectX::XMConvertToDegrees(d.rotation.z), d.scale.x, d.scale.y, d.scale.z,
                              d.color.x, d.color.y, d.color.z, d.color.w};
    for (int nIndex = 0; nIndex < 13; ++nIndex) {
        HWND hEdit = edits[nIndex + 2];
        if (GetFocus() == hEdit) continue;
        wchar_t aBuffer[64];
        swprintf_s(aBuffer, L"%.9g", static_cast<double>(afValues[nIndex]));
        if (Text(hEdit) != aBuffer) SetWindowTextW(hEdit, aBuffer);
    }
}

} // namespace dxs::editor
namespace dxs::editor {

float Editor::FieldScrubStep(UINT_PTR fieldId, bool bWheel) const {
    float fStep = 0.01f;                                    // position / scale per pixel
    if (fieldId >= 5 && fieldId <= 7) fStep = 0.5f;         // rotation (degrees)
    else if (fieldId >= 11 && fieldId <= 14) fStep = 0.005f; // colour
    else if (fieldId >= FIELD_SCRUB_COMPONENT) {
        const InspectorFieldRow* pRow = const_cast<Editor*>(this)->FindFieldRow(static_cast<int>(fieldId - FIELD_SCRUB_COMPONENT), nullptr);
        fStep = pRow && pRow->bInteger ? 0.1f : 0.01f;      // ints: 1 per 10 pixels
        if (pRow && pRow->hSlider && !pRow->bInteger) fStep = std::max((pRow->fMax - pRow->fMin) / 200.0f, 1e-4f); // range: 200 px
    }
    if (bWheel) fStep *= 10.0f;                             // one notch = 10 pixels
    if (GetKeyState(VK_SHIFT) & 0x8000) fStep *= 10.0f;
    if (GetKeyState(VK_MENU) & 0x8000) fStep *= 0.1f;
    return fStep;
}

void Editor::SetScrubbedValue(UINT_PTR fieldId, HWND hEdit, float fValue) {
    wchar_t aBuffer[64];
    if (fieldId >= FIELD_SCRUB_COMPONENT) {
        const int controlId = static_cast<int>(fieldId - FIELD_SCRUB_COMPONENT);
        const InspectorFieldRow* pRow = FindFieldRow(controlId, nullptr);
        if (pRow && pRow->bInteger) swprintf_s(aBuffer, L"%d", static_cast<int>(std::lround(fValue)));
        else swprintf_s(aBuffer, L"%.6g", static_cast<double>(fValue));
        SetWindowTextW(hEdit, aBuffer);
        CommitComponentField(controlId, false);
    } else {
        const int field = static_cast<int>(fieldId);
        if (field >= 11) fValue = std::clamp(fValue, 0.0f, 1.0f);
        swprintf_s(aBuffer, L"%.6g", static_cast<double>(fValue));
        SetWindowTextW(hEdit, aBuffer);
        SendMessageW(hEdit, EM_SETMODIFY, FALSE, 0); // applied directly below
        SetFieldValue(field, fValue);
    }
    RequestRender();
}

LRESULT CALLBACK NumericFieldSubclassProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR fieldId, DWORD_PTR data) {
    auto* e = reinterpret_cast<Editor*>(data);
    const bool bEditing = GetFocus() == hwnd; // typing mode: normal edit behaviour
    auto currentValue = [&](float& fValue) {
        const std::wstring sText = Text(hwnd);
        wchar_t* pEnd = nullptr;
        fValue = std::wcstof(sText.c_str(), &pEnd);
        return pEnd != sText.c_str() && std::isfinite(fValue);
    };
    switch (message) {
    case WM_SETCURSOR:
        if (!bEditing && IsWindowEnabled(hwnd)) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        if (bEditing) break;
        if (!currentValue(e->fFieldScrubStartValue)) break;
        e->bFieldPressed = true;
        e->bFieldScrubbing = false;
        e->nFieldScrubStartX = GET_X_LPARAM(lp);
        SetCapture(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        if (e->bFieldPressed && GetCapture() == hwnd) {
            const int dx = GET_X_LPARAM(lp) - e->nFieldScrubStartX;
            if (!e->bFieldScrubbing && std::abs(dx) < 3) return 0;
            e->bFieldScrubbing = true;
            e->SetScrubbedValue(fieldId, hwnd, e->fFieldScrubStartValue + static_cast<float>(dx) * e->FieldScrubStep(fieldId, false));
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (e->bFieldPressed) {
            const bool bWasScrubbing = e->bFieldScrubbing;
            e->bFieldPressed = e->bFieldScrubbing = false;
            ReleaseCapture();
            if (!bWasScrubbing) {
                // Plain click: start typing with the whole value selected.
                SetFocus(hwnd);
                SendMessageW(hwnd, EM_SETSEL, 0, -1);
            }
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (e->bFieldPressed && reinterpret_cast<HWND>(lp) != hwnd) e->bFieldPressed = e->bFieldScrubbing = false;
        break;
    case WM_MOUSEWHEEL: {
        float fValue = 0.0f;
        // Only a field in typing mode takes the wheel, so scrolling the
        // Inspector never changes values by accident.
        if (!bEditing || !IsWindowEnabled(hwnd) || !currentValue(fValue)) break;
        const float fNotches = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
        e->SetScrubbedValue(fieldId, hwnd, fValue + fNotches * e->FieldScrubStep(fieldId, true));
        SendMessageW(hwnd, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_NCDESTROY: RemoveWindowSubclass(hwnd, NumericFieldSubclassProc, fieldId); break;
    }
    return DefSubclassProc(hwnd, message, wp, lp);
}

} // namespace dxs::editor