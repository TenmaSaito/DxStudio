#pragma once

// PlayerInput component (Unity: PlayerInput) built on CInputSystem.
//
// Add Component > Input > PlayerInput loads an action file (.dxinput) into
// CInputSystem and enables one action map. Scripts then either
//   * poll:     auto* pInput = dxs::GetComponent<dxs::PlayerInput>(entity);
//               const auto move = pInput->ReadValue2D("Move");
//               if (pInput->WasPressedThisFrame("Jump")) ...
//   * or receive events by also deriving from IInputReceiver:
//               void OnInputAction(Entity&, const CInputAction& action) override
//               called on the same object when an action starts, performs
//               (value changed) or is canceled (GetPhase() tells which).

#include "Engine.h"
#include "Input/inputAction.h"
#include "Input/inputActionMap.h"
#include "Input/inputBinding.h"
#include "Input/inputSystem.h"

namespace dxs {

class IInputReceiver {
public:
    virtual ~IInputReceiver() = default;
    virtual void OnInputAction(Entity& entity, const CInputAction& action) = 0;
};

class PlayerInput final : public Component {
public:
    static constexpr const char* DEFAULT_ACTIONS_FILE = "Assets/Input/DefaultInputActions.dxinput";

    void Start(Entity& entity) override;
    void Update(Entity& entity, float fDeltaSeconds) override;
    std::vector<EditorField> EditorFields() override {
        return {EditField("Actions File", m_sActionsFile), EditField("Action Map", m_sActionMap)};
    }

    [[nodiscard]] CInputActionMap* GetActionMap() const;
    [[nodiscard]] CInputAction* FindAction(std::string_view sName) const;
    // Shortcuts (0 / false when the action does not exist).
    [[nodiscard]] Vector3 ReadValue2D(std::string_view sName) const;
    [[nodiscard]] float ReadValue1D(std::string_view sName) const;
    [[nodiscard]] bool IsPressed(std::string_view sName) const;
    [[nodiscard]] bool WasPressedThisFrame(std::string_view sName) const;
    [[nodiscard]] bool WasReleasedThisFrame(std::string_view sName) const;

    std::string m_sActionsFile = DEFAULT_ACTIONS_FILE;
    std::string m_sActionMap = "Player";
};

} // namespace dxs
