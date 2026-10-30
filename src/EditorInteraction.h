#pragma once
#include "Renderer.h"
#include <algorithm>
#include <cmath>

namespace dxs {
inline DirectX::XMFLOAT3 CameraOffset(const Camera& camera) {
    const float cp = std::cos(camera.pitch);
    return {std::sin(camera.yaw) * cp * camera.distance,
            std::sin(camera.pitch) * camera.distance,
            -std::cos(camera.yaw) * cp * camera.distance};
}

// Look around the eye for flythrough, or around the target for orbiting.
inline void LookCamera(Camera& camera, float dx, float dy, bool orbit) {
    const auto before = CameraOffset(camera);
    camera.yaw -= dx * 0.008f;
    camera.pitch = std::clamp(camera.pitch + dy * 0.008f, -1.5f, 1.5f);
    if (!orbit) {
        const auto after = CameraOffset(camera);
        camera.target.x += before.x - after.x;
        camera.target.y += before.y - after.y;
        camera.target.z += before.z - after.z;
    }
}

inline bool FlyCamera(Camera& camera, float forward, float right, float up, float seconds, bool fast) {
    using namespace DirectX;
    const float sy = std::sin(camera.yaw), cy = std::cos(camera.yaw);
    const float sp = std::sin(camera.pitch), cp = std::cos(camera.pitch);
    auto direction = XMVectorSet(-sy * cp * forward + cy * right,
        -sp * forward + up, cy * cp * forward + sy * right, 0);
    const float length = XMVectorGetX(XMVector3Length(direction));
    if (length < 0.0001f) return false;
    // Normalize combined input so diagonal travel has the same speed.
    direction = XMVectorScale(direction, 4.0f * (fast ? 3.0f : 1.0f) * std::clamp(seconds, 0.0f, 0.05f) / length);
    XMStoreFloat3(&camera.target, XMVectorAdd(XMLoadFloat3(&camera.target), direction));
    return true;
}

inline void SnapGizmo(DrawItem& value, const DrawItem& start, GizmoMode mode, int handle) {
    auto quantize = [](float number, float step) { return std::round(number / step) * step; };
    if (mode == GizmoMode::Translate) {
        // Only the manipulated axes move onto the one-unit world grid.
        if (handle == 0 || handle == 4 || handle == 5) value.position.x = quantize(value.position.x, 1.0f);
        if (handle == 1 || handle == 3 || handle == 5) value.position.y = quantize(value.position.y, 1.0f);
        if (handle == 2 || handle == 3 || handle == 4) value.position.z = quantize(value.position.z, 1.0f);
    } else if (mode == GizmoMode::Rotate) {
        const float step = DirectX::XMConvertToRadians(15.0f);
        if (handle == 0) value.rotation.x = start.rotation.x + quantize(value.rotation.x - start.rotation.x, step);
        if (handle == 1) value.rotation.y = start.rotation.y + quantize(value.rotation.y - start.rotation.y, step);
        if (handle == 2) value.rotation.z = start.rotation.z + quantize(value.rotation.z - start.rotation.z, step);
    } else if (handle == 6) {
        // Quantize the common scale factor, preserving nonuniform proportions.
        float factor = std::abs(start.scale.x) > 1e-6f ? value.scale.x / start.scale.x :
            std::abs(start.scale.y) > 1e-6f ? value.scale.y / start.scale.y :
            std::abs(start.scale.z) > 1e-6f ? value.scale.z / start.scale.z : 1.0f;
        factor = std::max(0.1f, quantize(factor, 0.1f));
        value.scale = {start.scale.x * factor, start.scale.y * factor, start.scale.z * factor};
    } else {
        auto snapScale = [&](float v) {
            const float snapped = quantize(v, 0.1f);
            return std::abs(snapped) < 0.05f ? (v < 0 ? -0.1f : 0.1f) : snapped;
        };
        if (handle == 0) value.scale.x = snapScale(value.scale.x);
        if (handle == 1) value.scale.y = snapScale(value.scale.y);
        if (handle == 2) value.scale.z = snapScale(value.scale.z);
    }
}
}
