#pragma once

#include "Engine.h"

class Spin final : public dxs::Component {
public:
    void Update(dxs::Entity& entity, float fDeltaSeconds) override;
};
