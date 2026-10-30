#pragma once
#include "Engine.h"

class BoxController final : public dxs::Component {
public:
    float m_fRotationSpeed = 1.0f;
    void Start(dxs::Entity& entity) override;
    void Update(dxs::Entity& entity, float fDeltaSeconds) override;
    std::vector<dxs::EditorField> EditorFields() override {
        // Public values are exposed directly. Private values can opt in from
        // inside the class, providing an editor-public style annotation.
        return {dxs::EditField("Rotation Speed", m_fRotationSpeed), dxs::EditField("Enabled", m_bEnabled)};
    }
private:
    bool m_bEnabled = true;
};
