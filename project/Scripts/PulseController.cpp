//==================================================================================
//
// 伸び縮みするコンポーネントのソースファイル [PulseController.cpp]
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "PulseController.h"

#include <algorithm>

//==================================================================================
// --- 開始処理 ---
//==================================================================================
void PulseController::Start(dxs::Entity& entity) {
    // 最初の co_yield まで今すぐ実行し、以降は毎フレーム Update の後に進める
    StartCoroutine(PulseLoop(entity));
}

void PulseController::Update(dxs::Entity&, float) {}

//==================================================================================
// --- 繰り返しのコルーチン ---
//==================================================================================
dxs::Coroutine PulseController::PulseLoop(dxs::Entity& entity) {
    while (true) {
        co_yield ScaleTo(entity, 1.5f); // 子コルーチンの終わりを待つ
        co_yield dxs::WaitForSeconds(m_fWaitSeconds);
        co_yield ScaleTo(entity, 1.0f);
        co_yield Blink(entity);
        co_yield dxs::WaitUntil([&entity] { return entity.draw.bVisible; });
    }
}

//==================================================================================
// --- 拡大率を変えるコルーチン ---
//==================================================================================
dxs::Coroutine PulseController::ScaleTo(dxs::Entity& entity, float fTarget) {
    const float fStart = entity.draw.scale.x;
    for (float fElapsed = 0.0f; fElapsed < m_fPulseSeconds; fElapsed += dxs::Time::DeltaTime()) {
        const float fScale = fStart + (fTarget - fStart) * std::clamp(fElapsed / m_fPulseSeconds, 0.0f, 1.0f);
        entity.draw.scale = {fScale, fScale, fScale};
        co_yield nullptr; // 次のフレームへ
    }

    entity.draw.scale = {fTarget, fTarget, fTarget};
}

//==================================================================================
// --- 点滅するコルーチン ---
//==================================================================================
dxs::Coroutine PulseController::Blink(dxs::Entity& entity) {
    for (int nIndex = 0; nIndex < m_nBlinkCount * 2; ++nIndex) {
        entity.draw.bVisible = !entity.draw.bVisible;
        co_yield dxs::WaitForSeconds(0.1f);
    }

    entity.draw.bVisible = true;
}

DXS_REGISTER_COMPONENT(PulseController)
