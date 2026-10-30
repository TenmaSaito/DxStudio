#include "BoxController.h"

void BoxController::Start(dxs::Entity& entity) {
    // Called once for each attachment.
    (void)entity;
}

void BoxController::Update(dxs::Entity& entity, float fDeltaSeconds) {
    if (m_bEnabled) entity.draw.rotation.y += m_fRotationSpeed * fDeltaSeconds;
}

DXS_REGISTER_COMPONENT(BoxController)
