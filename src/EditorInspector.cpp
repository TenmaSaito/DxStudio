// Hierarchy and Inspector: selection, tree rows, inspected values and Apply.

#include "EditorWindow.h"

namespace dxs::editor {

void Editor::SyncHierarchy() {
    SendMessageW(hierarchy, LB_SETSEL, FALSE, -1);
    for (size_t row = 0; row < hierarchyRows.size(); ++row) {
        if (std::find(selectedIndices.begin(), selectedIndices.end(), hierarchyRows[row]) != selectedIndices.end())
            SendMessageW(hierarchy, LB_SETSEL, TRUE, static_cast<WPARAM>(row));
        if (hierarchyRows[row] == selected) SendMessageW(hierarchy, LB_SETCARETINDEX, row, FALSE);
    }
}

std::vector<int> Editor::MergeSelection(const std::vector<int>& hits) const {
    auto result = (selectionAdd || selectionToggle) ? selectionBeforeDrag : std::vector<int>{};
    for (int hit : hits) {
        auto it = std::find(result.begin(), result.end(), hit);
        if (selectionToggle && it != result.end())
            result.erase(it);
        else if (it == result.end())
            result.push_back(hit);
    }
    return result;
}

void Editor::RestoreDrag() {
    for (const auto& entry : gizmoStartItems)
        if (entry.first >= 0 && static_cast<size_t>(entry.first) < scene.entities.size())
            scene.entities[entry.first].draw = LocalDrawFromWorld(scene, entry.first, entry.second);
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
    for (const auto& entry : gizmoStartItems) {
        if (entry.first == selected) {
            scene.entities[entry.first].draw = LocalDrawFromWorld(scene, entry.first, updated);
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
        scene.entities[entry.first].draw = LocalDrawFromWorld(scene, entry.first, d);
    }
}

void Editor::FrameSelection() {
    if (selectedIndices.empty()) return;
    DirectX::XMFLOAT3 min{FLT_MAX, FLT_MAX, FLT_MAX}, max{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    const auto world = ResolveWorldDraws(scene);
    for (int i : selectedIndices) {
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
    camera.distance = std::clamp(std::max({max.x - min.x, max.y - min.y, max.z - min.z}) * 2.5f, 1.0f, 80.0f);
    renderer.SetCamera(camera);
    SetStatus(L"Focused selection. W / E / R: Move / Rotate / Scale.");
}

void Editor::Populate() {
    SendMessageW(hierarchy, LB_RESETCONTENT, 0, 0);
    hierarchyRows.clear();
    auto lower = [](std::wstring value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return value;
    };
    const auto filter = lower(Text(hierarchySearch));
    std::vector<unsigned char> visible(scene.entities.size(), filter.empty() ? 1 : 0);
    if (!filter.empty()) {
        for (size_t i = 0; i < scene.entities.size(); ++i)
            if (lower(Wide(scene.entities[i].name)).find(filter) != std::wstring::npos) {
                for (int node = static_cast<int>(i), steps = 0;
                     node >= 0 && static_cast<size_t>(node) < scene.entities.size() && steps++ < 64;
                     node = scene.entities[node].parent)
                    visible[static_cast<size_t>(node)] = 1;
            }
    }
    // Depth-first tree rows. Collapsed parents hide their children unless
    // a search is active (search always reveals matching descendants).
    collapsedEntities.resize(scene.entities.size(), 0);
    hierarchyDepths.clear();
    hierarchyHasChildren.clear();
    std::vector<char> hasChildren(scene.entities.size(), 0);
    for (const auto& e : scene.entities)
        if (e.parent >= 0 && static_cast<size_t>(e.parent) < scene.entities.size())
            hasChildren[static_cast<size_t>(e.parent)] = 1;
    auto add = [&](auto&& self, int parent, int depth) -> void {
        for (size_t index = 0; index < scene.entities.size(); ++index) {
            const auto& e = scene.entities[index];
            if (e.parent != parent || !visible[index]) continue;
            // The string keeps the old indented text so screen readers and
            // LB_GETTEXT still describe the row; owner draw renders the tree.
            std::wstring name(static_cast<size_t>(depth) * 3, L' ');
            name += Wide(e.name);
            SendMessageW(hierarchy, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
            hierarchyRows.push_back(static_cast<int>(index));
            hierarchyDepths.push_back(depth);
            hierarchyHasChildren.push_back(hasChildren[index]);
            if (filter.empty() && collapsedEntities[index]) continue;
            self(self, static_cast<int>(index), depth + 1);
        }
    };
    add(add, -1, 0);
    selectedIndices.erase(std::remove_if(selectedIndices.begin(), selectedIndices.end(),
                                         [&](int i) { return i < 0 || static_cast<size_t>(i) >= scene.entities.size(); }),
                          selectedIndices.end());
    if (std::find(selectedIndices.begin(), selectedIndices.end(), selected) == selectedIndices.end())
        selected = selectedIndices.empty() ? -1 : selectedIndices.back();
    SyncHierarchy();
    Inspect();
}

void Editor::Inspect() {
    bool valid = selected >= 0 && selected < static_cast<int>(scene.entities.size());
    const bool single = valid && selectedIndices.size() == 1;
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
    inspectorEnabledChecked = false;
    InvalidateRect(inspectorEnabled, nullptr, FALSE);
    InvalidateRect(inspectorIcon, nullptr, FALSE);
    int componentSelection = static_cast<int>(SendMessageW(componentList, LB_GETCURSEL, 0, 0));
    SendMessageW(componentList, LB_RESETCONTENT, 0, 0);
    SendMessageW(componentFields, LB_RESETCONTENT, 0, 0);
    inspectedComponentFields.clear();
    SetWindowTextW(componentValue, L"");
    if (!valid) return;
    if (!single) {
        auto label = std::to_wstring(selectedIndices.size()) + L" objects selected";
        SetWindowTextW(edits[0], label.c_str());
        return;
    }
    const auto& entity = scene.entities[selected];
    inspectorEnabledChecked = entity.draw.visible;
    SetWindowTextW(edits[0], Wide(entity.name).c_str());
    SetWindowTextW(materialEdit, Wide(entity.materialAsset).c_str());
    SetWindowTextW(textureEdit, Wide(entity.textureAsset).c_str());
    if (entity.renderer != Entity::RendererKind::None) {
        const wchar_t* rendererName = entity.renderer == Entity::RendererKind::Polygon ? L"PolygonRenderer [IRender]"
                                      : entity.renderer == Entity::RendererKind::Model ? L"ModelRenderer [IRender]"
                                                                                       : L"Image [IRender]";
        SendMessageW(componentList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(rendererName));
    }
    for (const auto& component : entity.components) {
        const std::wstring name = Wide(component);
        SendMessageW(componentList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }
    if (componentSelection >= 0 && componentSelection < static_cast<int>(entity.components.size()) +
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
    const int oldField = static_cast<int>(SendMessageW(componentFields, LB_GETCURSEL, 0, 0));
    const int listIndex = static_cast<int>(SendMessageW(componentList, LB_GETCURSEL, 0, 0));
    inspectedComponentFields.clear();
    SendMessageW(componentFields, LB_RESETCONTENT, 0, 0);
    if (selected < 0 || static_cast<size_t>(selected) >= scene.entities.size() || listIndex == LB_ERR) return;
    auto& entity = scene.entities[selected];
    const int offset = entity.renderer == Entity::RendererKind::None ? 0 : 1;
    const int componentIndex = listIndex - offset;
    if (componentIndex < 0 || static_cast<size_t>(componentIndex) >= entity.components.size()) return;
    inspectedComponentFields = ComponentEditorValues(entity, static_cast<size_t>(componentIndex));
    for (const auto& [name, value] : inspectedComponentFields) {
        const auto row = Wide(name + " = " + value);
        SendMessageW(componentFields, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
    }
    if (!inspectedComponentFields.empty()) {
        const int chosen = std::clamp(oldField, 0, static_cast<int>(inspectedComponentFields.size()) - 1);
        SendMessageW(componentFields, LB_SETCURSEL, chosen, 0);
        if (GetFocus() != componentValue)
            SetWindowTextW(componentValue, Wide(inspectedComponentFields[chosen].second).c_str());
    }
}

bool Editor::Apply() {
    if (selectedIndices.size() != 1 || selected < 0 || selected >= static_cast<int>(scene.entities.size())) return true;
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
    auto& entity = scene.entities[selected];
    entity.name = name;
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
    if (!assetPath(materialEdit, "Material", entity.materialAsset) || !assetPath(textureEdit, "Texture", entity.textureAsset))
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

} // namespace dxs::editor
