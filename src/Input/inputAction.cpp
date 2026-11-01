#include "Input/inputAction.h"

#include "Input/inputBinding.h"
#include "Input/inputSystem.h"

#include <cmath>

namespace dxs {
namespace {
float Length(const Vector3& value) { return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z); }
} // namespace

CInputAction::CInputAction() {}
CInputAction::~CInputAction() {}

HRESULT CInputAction::Init(std::string_view sName, const ACTION_TYPE type, const float fPressPoint) {
    m_sName = sName;
    m_type = type;
    m_phase = PHASE::WAITING;
    m_value = VECTOR3_NULL;
    m_bPressedPrev = false;
    m_bPressedCurrent = false;
    m_fPressPoint = fPressPoint;
    m_callback = {};
    m_vpBinding.clear();
    return S_OK;
}

void CInputAction::Uninit(void) { m_vpBinding.clear(); }

void CInputAction::Update(const CInputSystem& inputSystem) {
    const Vector3 previous = m_value;
    m_value = EvaluateBindings(inputSystem); // largest value of all bindings
    const float fLength = Length(m_value);
    // Threshold decides "pressed"; keep last frame's result.
    m_bPressedPrev = m_bPressedCurrent;
    m_bPressedCurrent = (fLength >= m_fPressPoint);
    UpdatePhase(previous);
}

void CInputAction::UpdatePhase(const Vector3& previous) {
    m_bStartedThisFrame = m_bPerformedThisFrame = m_bCanceledThisFrame = false;
    if (m_type == ACTION_TYPE::BUTTON) {
        if (WasPressedThisFrame()) {
            m_bStartedThisFrame = m_bPerformedThisFrame = true;
            m_phase = PHASE::PERFORMED;
        } else if (WasReleasedThisFrame()) {
            m_bCanceledThisFrame = true;
            m_phase = PHASE::CANCELED;
        } else {
            m_phase = m_bPressedCurrent ? PHASE::PERFORMED : PHASE::WAITING;
        }
    } else {
        const bool bActive = Length(m_value) > 0.0f, bWasActive = Length(previous) > 0.0f;
        const bool bChanged = m_value.x != previous.x || m_value.y != previous.y || m_value.z != previous.z;
        if (bActive && !bWasActive) {
            m_bStartedThisFrame = m_bPerformedThisFrame = true;
            m_phase = PHASE::PERFORMED;
        } else if (bActive) {
            m_bPerformedThisFrame = bChanged;
            m_phase = PHASE::PERFORMED;
        } else if (bWasActive) {
            m_bCanceledThisFrame = true;
            m_phase = PHASE::CANCELED;
        } else {
            m_phase = PHASE::WAITING;
        }
    }
    if (m_bStartedThisFrame) {
        if (m_callbackStarted) m_callbackStarted(*this);
        onStarted.Broadcast(*this);
    }
    if (m_bPerformedThisFrame) {
        if (m_callback) m_callback(*this);
        onPerformed.Broadcast(*this);
    }
    if (m_bCanceledThisFrame) {
        if (m_callbackCanceled) m_callbackCanceled(*this);
        onCanceled.Broadcast(*this);
    }
}

void CInputAction::Reset(void) {
    m_phase = PHASE::WAITING;
    m_value = VECTOR3_NULL;
    m_bPressedPrev = false;
    m_bPressedCurrent = false;
    m_bStartedThisFrame = m_bPerformedThisFrame = m_bCanceledThisFrame = false;
}

void CInputAction::AddBinding(CInputBinding* pBinding) {
    if (pBinding) m_vpBinding.push_back(std::unique_ptr<CInputBinding>(pBinding));
}

Vector3 CInputAction::EvaluateBindings(const CInputSystem& inputSystem) const {
    Vector3 valueMax = VECTOR3_NULL;
    switch (m_type) {
    case ACTION_TYPE::BUTTON:
    case ACTION_TYPE::VALUE_1D:
        for (const auto& pBinding : m_vpBinding) {
            if (!pBinding) continue;
            const float fValue = pBinding->ReadValue1D(inputSystem);
            if (std::fabs(fValue) > std::fabs(valueMax.x)) valueMax.x = fValue;
        }
        break;
    case ACTION_TYPE::VALUE_2D:
        for (const auto& pBinding : m_vpBinding) {
            if (!pBinding) continue;
            const Vector3 value = pBinding->ReadValue2D(inputSystem);
            if (Length(value) > Length(valueMax)) valueMax = value;
        }
        break;
    default: break;
    }
    return valueMax;
}

} // namespace dxs
