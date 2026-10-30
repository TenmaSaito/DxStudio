#include "TestComponent.h"

void TestComponent::Update(dxs::Entity& entity, float fDeltaSeconds) {
    // Add your behavior here. fDeltaSeconds is elapsed time in seconds.
    (void)entity;
    (void)fDeltaSeconds;
}

DXS_REGISTER_COMPONENT(TestComponent)
