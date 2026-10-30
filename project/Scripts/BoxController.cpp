#include "BoxController.h"

void BoxController::Start(dxs::Entity& entity) {
    // Called once for each attachment.
    (void)entity;
}

void BoxController::Update(dxs::Entity& entity, float deltaSeconds) {
    if (enabled_) entity.draw.rotation.y += rotationSpeed * deltaSeconds;
}

DXS_REGISTER_COMPONENT(BoxController)
