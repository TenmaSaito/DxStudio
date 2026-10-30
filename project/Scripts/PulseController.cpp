#include "PulseController.h"

#include <algorithm>

void PulseController::Start(dxs::Entity& entity) {
    // Runs until the first co_yield right now, then once per frame after Update.
    StartCoroutine(PulseLoop(entity));
}

void PulseController::Update(dxs::Entity&, float) {}

dxs::Coroutine PulseController::PulseLoop(dxs::Entity& entity) {
    while (true) {
        co_yield ScaleTo(entity, 1.5f);                 // wait for the nested coroutine
        co_yield dxs::WaitForSeconds(m_fWaitSeconds);
        co_yield ScaleTo(entity, 1.0f);
        co_yield Blink(entity);
        co_yield dxs::WaitUntil([&entity] { return entity.draw.bVisible; });
    }
}

dxs::Coroutine PulseController::ScaleTo(dxs::Entity& entity, float fTarget) {
    const float fStart = entity.draw.scale.x;
    for (float fElapsed = 0.0f; fElapsed < m_fPulseSeconds; fElapsed += dxs::Time::DeltaTime()) {
        const float fScale = fStart + (fTarget - fStart) * std::clamp(fElapsed / m_fPulseSeconds, 0.0f, 1.0f);
        entity.draw.scale = {fScale, fScale, fScale};
        co_yield nullptr;                               // next frame
    }
    entity.draw.scale = {fTarget, fTarget, fTarget};
}

dxs::Coroutine PulseController::Blink(dxs::Entity& entity) {
    for (int nIndex = 0; nIndex < m_nBlinkCount * 2; ++nIndex) {
        entity.draw.bVisible = !entity.draw.bVisible;
        co_yield dxs::WaitForSeconds(0.1f);
    }
    entity.draw.bVisible = true;
}

DXS_REGISTER_COMPONENT(PulseController)
