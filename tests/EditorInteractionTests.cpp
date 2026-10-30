#include "EditorInteraction.h"
#include <cassert>
#include <iostream>

bool Near(float a, float b) { return std::abs(a - b) < 0.0001f; }
int main() {
    dxs::Camera camera;
    auto offset = dxs::CameraOffset(camera);
    const auto target = camera.target;
    dxs::LookCamera(camera, 60, -20, false);
    auto after = dxs::CameraOffset(camera);
    assert(Near(target.x + offset.x, camera.target.x + after.x));
    assert(Near(target.y + offset.y, camera.target.y + after.y));
    assert(Near(target.z + offset.z, camera.target.z + after.z));
    const auto pivot = camera.target;
    dxs::LookCamera(camera, 10, 10, true);
    assert(Near(camera.target.x, pivot.x) && Near(camera.target.y, pivot.y) && Near(camera.target.z, pivot.z));
    camera = {}; camera.pitch = 0;
    assert(dxs::FlyCamera(camera, 1, 0, 0, 0.05f, false));
    assert(Near(camera.target.z, 0.2f));
    camera = {}; camera.pitch = 0;
    dxs::FlyCamera(camera, 1, 1, 0, 0.05f, false);
    assert(Near(std::sqrt(camera.target.x*camera.target.x + camera.target.z*camera.target.z), 0.2f));
    assert(!dxs::FlyCamera(camera, 0, 0, 0, 0.05f, false));
    dxs::DrawItem start, value;
    value.position = {-1.6f, 2.4f, 0.33f};
    dxs::SnapGizmo(value, start, dxs::GizmoMode::Translate, 5);
    assert(value.position.x == -2 && value.position.y == 2 && value.position.z == 0.33f);
    start.rotation.z = DirectX::XMConvertToRadians(7.0f);
    value.rotation.z = DirectX::XMConvertToRadians(27.0f);
    dxs::SnapGizmo(value, start, dxs::GizmoMode::Rotate, 2);
    assert(Near(DirectX::XMConvertToDegrees(value.rotation.z), 22.0f));
    start.scale = {2, 3, -4}; value.scale = {2.74f, 4.11f, -5.48f};
    dxs::SnapGizmo(value, start, dxs::GizmoMode::Scale, 6);
    assert(Near(value.scale.x, 2.8f) && Near(value.scale.y, 4.2f) && Near(value.scale.z, -5.6f));
    std::cout << "Fly camera, fixed-eye look, orbit and grid/angle/scale snapping tests passed.\n";
}
