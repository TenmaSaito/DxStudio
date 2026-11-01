//==================================================================================
//
// シーンビューの操作（カメラ・ギズモ）のヘッダーファイル [EditorInteraction.h]
//
// ウィンドウに依存しない計算だけをまとめているので、テストから直接呼べる。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Renderer.h"
#include <algorithm>
#include <cmath>

namespace dxs {
//==================================================================================
// --- 注視点から視点（カメラの位置）へのずれの計算処理 ---
//==================================================================================
inline DirectX::XMFLOAT3 CameraOffset(const Camera& camera) {
    const float cp = std::cos(camera.fPitch);
    return {std::sin(camera.fYaw) * cp * camera.fDistance, std::sin(camera.fPitch) * camera.fDistance,
            -std::cos(camera.fYaw) * cp * camera.fDistance};
}

//==================================================================================
// --- カメラの向きの変更処理 ---
//==================================================================================
// フライスルーでは視点を中心に、オービット（orbit = true）では注視点を中心に回す。
inline void LookCamera(Camera& camera, float dx, float dy, bool orbit) {
    const auto before = CameraOffset(camera);
    camera.fYaw -= dx * 0.008f;
    camera.fPitch = std::clamp(camera.fPitch + dy * 0.008f, -1.5f, 1.5f); // 真上 / 真下を越えないようにする
    if (!orbit) {
        // 視点が動かないよう、ずれの変化分だけ注視点を動かす
        const auto after = CameraOffset(camera);
        camera.target.x += before.x - after.x;
        camera.target.y += before.y - after.y;
        camera.target.z += before.z - after.z;
    }
}

//==================================================================================
// --- カメラの移動処理（WASD / QE のフライスルー） ---
//==================================================================================
// 動いたら true（入力が無ければ false）
inline bool FlyCamera(Camera& camera, float forward, float right, float up, float seconds, bool fast) {
    using namespace DirectX;
    const float sy = std::sin(camera.fYaw), cy = std::cos(camera.fYaw);
    const float sp = std::sin(camera.fPitch), cp = std::cos(camera.fPitch);

    // 前後・左右・上下の入力を、カメラの向きに合わせたワールドの方向にする
    auto direction = XMVectorSet(-sy * cp * forward + cy * right, -sp * forward + up, cy * cp * forward + sy * right, 0);
    const float length = XMVectorGetX(XMVector3Length(direction));
    if (length < 0.0001f) return false;

    // 合成した入力を正規化し、斜めに動いても同じ速さにする（Shift で 3 倍、1 回の移動は 0.05 秒分まで）
    direction = XMVectorScale(direction, 4.0f * (fast ? 3.0f : 1.0f) * std::clamp(seconds, 0.0f, 0.05f) / length);
    XMStoreFloat3(&camera.target, XMVectorAdd(XMLoadFloat3(&camera.target), direction));
    return true;
}

//==================================================================================
// --- ギズモの吸着処理（Ctrl を押しながらのドラッグ） ---
//==================================================================================
// 移動は 1 単位、回転は 15 度、拡大率は 0.1 刻みに揃える。
inline void SnapGizmo(DrawItem& value, const DrawItem& start, GizmoMode mode, int handle) {
    auto quantize = [](float number, float step) {
        return std::round(number / step) * step;
    };
    if (mode == GizmoMode::Translate) {
        // 操作している軸だけを 1 単位のワールドのグリッドに合わせる
        if (handle == 0 || handle == 4 || handle == 5) {
            value.position.x = quantize(value.position.x, 1.0f);
        }

        if (handle == 1 || handle == 3 || handle == 5) {
            value.position.y = quantize(value.position.y, 1.0f);
        }

        if (handle == 2 || handle == 3 || handle == 4) {
            value.position.z = quantize(value.position.z, 1.0f);
        }
    } else if (mode == GizmoMode::Rotate) {
        // ドラッグ開始時からの回転量を 15 度刻みにする
        const float step = DirectX::XMConvertToRadians(15.0f);
        if (handle == 0) {
            value.rotation.x = start.rotation.x + quantize(value.rotation.x - start.rotation.x, step);
        }

        if (handle == 1) {
            value.rotation.y = start.rotation.y + quantize(value.rotation.y - start.rotation.y, step);
        }

        if (handle == 2) {
            value.rotation.z = start.rotation.z + quantize(value.rotation.z - start.rotation.z, step);
        }
    } else if (handle == 6) {
        // 均等スケール: 共通の倍率を揃え、軸ごとの比率は保つ
        float factor = std::abs(start.scale.x) > 1e-6f   ? value.scale.x / start.scale.x
                       : std::abs(start.scale.y) > 1e-6f ? value.scale.y / start.scale.y
                       : std::abs(start.scale.z) > 1e-6f ? value.scale.z / start.scale.z
                                                         : 1.0f;
        factor = std::max(0.1f, quantize(factor, 0.1f));
        value.scale = {start.scale.x * factor, start.scale.y * factor, start.scale.z * factor};
    } else {
        // 軸ごとのスケール: 0 にはならないようにする（最小 ±0.1）
        auto snapScale = [&](float v) {
            const float snapped = quantize(v, 0.1f);
            return std::abs(snapped) < 0.05f ? (v < 0 ? -0.1f : 0.1f) : snapped;
        };
        if (handle == 0) {
            value.scale.x = snapScale(value.scale.x);
        }

        if (handle == 1) {
            value.scale.y = snapScale(value.scale.y);
        }

        if (handle == 2) {
            value.scale.z = snapScale(value.scale.z);
        }
    }
}
} // namespace dxs
