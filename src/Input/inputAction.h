#pragma once

// Input action (from cpp_TKG inputAction.h).
//
// Added for DxStudio: the PHASE is updated every frame and the callbacks
// fire (the original left this as a TODO):
//   BUTTON    : STARTED + PERFORMED when pressed, CANCELED when released.
//   VALUE_1D/2D: STARTED + PERFORMED when it leaves zero, PERFORMED again
//               whenever the value changes, CANCELED when it returns to zero.

#include "Core/Delegate.h"
#include "Input/inputControl.h"

#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dxs {

class CInputBinding;
class CInputSystem;

class CInputAction {
public:
    enum class ACTION_TYPE {
        NONE = 0, // none
        BUTTON,   // press
        VALUE_1D, // negative + positive
        VALUE_2D, // 2D value (WASD / stick)
        MAX
    };

    enum class PHASE {
        NONE = 0,  // none
        WAITING,   // idle
        STARTED,   // input began
        PERFORMED, // action happened
        CANCELED,  // input ended
        MAX
    };

    CInputAction();
    ~CInputAction();

    HRESULT Init(std::string_view sName, const ACTION_TYPE type, const float fPressPoint = DEFAULT_PRESS_POINT);
    void Uninit(void);
    void Update(const CInputSystem& inputSystem);

    void Reset(void);
    bool IsPressed(void) const { return m_bPressedCurrent; }
    bool WasPressedThisFrame(void) const { return m_bPressedCurrent && !m_bPressedPrev; }
    bool WasReleasedThisFrame(void) const { return !m_bPressedCurrent && m_bPressedPrev; }
    float ReadValue1D(void) const { return m_value.x; }
    const Vector3& ReadValue2D(void) const { return m_value; }
    bool IsEqualName(std::string_view sName) const { return sName == m_sName; }

    void AddBinding(CInputBinding* pBinding);
    void SetOnPerformed(std::function<void(const CInputAction&)> callback) { m_callback = std::move(callback); }
    static constexpr float GetDefaultPressPoint(void) { return DEFAULT_PRESS_POINT; }

    // ---- Added for DxStudio ------------------------------------------------
    void SetOnStarted(std::function<void(const CInputAction&)> callback) { m_callbackStarted = std::move(callback); }
    void SetOnCanceled(std::function<void(const CInputAction&)> callback) { m_callbackCanceled = std::move(callback); }
    // Any number of listeners (see Core/Delegate.h); fire after the Set... callbacks.
    MulticastDelegate<const CInputAction&> onStarted, onPerformed, onCanceled;
    [[nodiscard]] PHASE GetPhase(void) const { return m_phase; }
    [[nodiscard]] ACTION_TYPE GetType(void) const { return m_type; }
    [[nodiscard]] const std::string& GetName(void) const { return m_sName; }
    [[nodiscard]] float GetPressPoint(void) const { return m_fPressPoint; }
    [[nodiscard]] size_t GetBindingCount(void) const { return m_vpBinding.size(); }
    bool WasStartedThisFrame(void) const { return m_bStartedThisFrame; }
    bool WasPerformedThisFrame(void) const { return m_bPerformedThisFrame; }
    bool WasCanceledThisFrame(void) const { return m_bCanceledThisFrame; }

private:
    Vector3 EvaluateBindings(const CInputSystem& inputSystem) const;
    void UpdatePhase(const Vector3& previous);

    static constexpr float DEFAULT_PRESS_POINT = 0.5f;            // default actuation threshold
    std::string m_sName;                                          // action name
    ACTION_TYPE m_type = ACTION_TYPE::NONE;                       // input kind
    PHASE m_phase = PHASE::NONE;                                  // progress
    Vector3 m_value = VECTOR3_NULL;                               // BUTTON/1D use x, 2D uses x and y
    bool m_bPressedPrev = false;                                  // pressed last frame
    bool m_bPressedCurrent = false;                               // pressed this frame
    float m_fPressPoint = 0.0f;                                   // pressed threshold (0.5)
    std::vector<std::unique_ptr<CInputBinding>> m_vpBinding;      // bindings (the largest value wins)
    std::function<void(const CInputAction&)> m_callback;          // performed callback
    std::function<void(const CInputAction&)> m_callbackStarted;   // added
    std::function<void(const CInputAction&)> m_callbackCanceled;  // added
    bool m_bStartedThisFrame = false, m_bPerformedThisFrame = false, m_bCanceledThisFrame = false;
};

} // namespace dxs
