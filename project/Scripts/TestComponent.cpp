#include "TestComponent.h"

void TestComponent::Update(dxs::Entity& entity, float deltaSeconds) {
    // Add your behavior here. deltaSeconds is elapsed time in seconds.
    (void)entity;
    (void)deltaSeconds;
}

DXS_REGISTER_COMPONENT(TestComponent)
