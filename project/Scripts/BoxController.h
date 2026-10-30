#pragma once
#include "Engine.h"

class BoxController final : public dxs::Component {
public:
    float rotationSpeed = 1.0f;
    void Start(dxs::Entity& entity) override;
    void Update(dxs::Entity& entity, float deltaSeconds) override;
    std::vector<dxs::EditorField> EditorFields() override {
        // Public values are exposed directly. Private values can opt in from
        // inside the class, providing an editor-public style annotation.
        return {dxs::EditField("Rotation Speed", rotationSpeed), dxs::EditField("Enabled", enabled_)};
    }
private:
    bool enabled_ = true;
};
