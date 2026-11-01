#include "Input/playerInput.h"

namespace dxs {

void PlayerInput::Start(Entity& entity) {
    auto* pSystem = CInputSystem::GetInstance();
    std::string error;
    if (!m_sActionsFile.empty() && !pSystem->LoadActionsFile(std::filesystem::u8path(m_sActionsFile), error)) {
        Debug::LogWarning(entity.sName + ": " + error + " (using the built-in default actions)");
        if (!pSystem->FindActionMap(m_sActionMap) && !pSystem->LoadActionsText(CInputSystem::DefaultActionsText(), error, "default actions"))
            Debug::LogError(error);
    }
    if (auto* pMap = GetActionMap()) pMap->SetEnable(true);
    else Debug::LogWarning(entity.sName + ": input action map '" + m_sActionMap + "' was not found.");
}

void PlayerInput::Update(Entity& entity, float) {
    const auto* pMap = GetActionMap();
    if (!pMap || !pMap->IsEnable()) return;
    for (const auto& pAction : pMap->GetActions()) {
        if (!pAction || !(pAction->WasStartedThisFrame() || pAction->WasPerformedThisFrame() || pAction->WasCanceledThisFrame())) continue;
        // Index loop: a receiver may attach components.
        for (size_t nIndex = 0; nIndex < entity.vRuntimeComponents.size(); ++nIndex)
            if (auto* pReceiver = dynamic_cast<IInputReceiver*>(entity.vRuntimeComponents[nIndex].get()))
                pReceiver->OnInputAction(entity, *pAction);
    }
}

CInputActionMap* PlayerInput::GetActionMap() const { return CInputSystem::GetInstance()->FindActionMap(m_sActionMap); }

CInputAction* PlayerInput::FindAction(std::string_view sName) const {
    const auto* pMap = GetActionMap();
    return pMap ? pMap->Find(sName) : nullptr;
}

Vector3 PlayerInput::ReadValue2D(std::string_view sName) const {
    const auto* pAction = FindAction(sName);
    return pAction ? pAction->ReadValue2D() : VECTOR3_NULL;
}

float PlayerInput::ReadValue1D(std::string_view sName) const {
    const auto* pAction = FindAction(sName);
    return pAction ? pAction->ReadValue1D() : 0.0f;
}

bool PlayerInput::IsPressed(std::string_view sName) const {
    const auto* pAction = FindAction(sName);
    return pAction && pAction->IsPressed();
}

bool PlayerInput::WasPressedThisFrame(std::string_view sName) const {
    const auto* pAction = FindAction(sName);
    return pAction && pAction->WasPressedThisFrame();
}

bool PlayerInput::WasReleasedThisFrame(std::string_view sName) const {
    const auto* pAction = FindAction(sName);
    return pAction && pAction->WasReleasedThisFrame();
}

} // namespace dxs
