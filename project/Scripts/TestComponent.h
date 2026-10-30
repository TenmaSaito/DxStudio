#pragma once
#include "Engine.h"

class TestComponent final : public dxs::Component {
public:
    void Update(dxs::Entity& entity, float deltaSeconds) override;
};
