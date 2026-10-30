#pragma once
#include "Engine.h"

// Coroutine sample (Unity: IEnumerator + StartCoroutine).
// Attach "PulseController" to an object and press Play: it grows, waits,
// shrinks, blinks three times and repeats. Inspector values are editable.
class PulseController final : public dxs::Component {
public:
    void Start(dxs::Entity& entity) override;
    void Update(dxs::Entity& entity, float deltaSeconds) override;
    std::vector<dxs::EditorField> EditorFields() override {
        return {dxs::EditField("Pulse Seconds", m_fPulseSeconds), dxs::EditField("Wait Seconds", m_fWaitSeconds),
                dxs::EditField("Blink Count", m_nBlinkCount)};
    }

private:
    dxs::Coroutine PulseLoop(dxs::Entity& entity);
    dxs::Coroutine ScaleTo(dxs::Entity& entity, float fTarget);
    dxs::Coroutine Blink(dxs::Entity& entity);

    float m_fPulseSeconds = 0.5f;
    float m_fWaitSeconds = 1.0f;
    int m_nBlinkCount = 3;
};
