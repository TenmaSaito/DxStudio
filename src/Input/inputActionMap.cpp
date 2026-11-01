#include "Input/inputActionMap.h"

namespace dxs {

CInputActionMap::CInputActionMap() {}
CInputActionMap::~CInputActionMap() {}

HRESULT CInputActionMap::Init(std::string_view sName) {
    m_sName = sName;
    m_bEnable = true;
    return S_OK;
}

void CInputActionMap::Uninit(void) {
    for (auto& pAction : m_vpAction)
        if (pAction) pAction->Uninit();
    m_vpAction.clear();
}

void CInputActionMap::Update(const CInputSystem& inputSystem) {
    if (!m_bEnable) return;
    for (auto& pAction : m_vpAction)
        if (pAction) pAction->Update(inputSystem);
}

CInputAction* CInputActionMap::Add(std::string_view sName, const CInputAction::ACTION_TYPE type, const float fPressPoint) {
    if (sName.empty() || !(type > CInputAction::ACTION_TYPE::NONE && type < CInputAction::ACTION_TYPE::MAX)) return nullptr;
    // Added: a second Add with the same name returns the existing action
    // (as CInputSystem::CreateActionMap does for maps).
    if (auto* pExisting = Find(sName)) return pExisting;
    auto& pAction = m_vpAction.emplace_back(std::make_unique<CInputAction>());
    pAction->Init(sName, type, fPressPoint);
    return pAction.get();
}

CInputAction* CInputActionMap::Find(std::string_view sName) const {
    if (sName.empty()) return nullptr;
    for (const auto& pAction : m_vpAction)
        if (pAction && pAction->IsEqualName(sName)) return pAction.get();
    return nullptr;
}

void CInputActionMap::SetEnable(const bool bEnable) {
    const bool bEnablePrev = m_bEnable;
    m_bEnable = bEnable;
    if (bEnablePrev == bEnable || bEnable) return;
    // No longer updated: clear the pressed state.
    for (auto& pAction : m_vpAction)
        if (pAction) pAction->Reset();
}

} // namespace dxs
