//==================================================================================
//
// インプットアクションのマップクラスのソースファイル [inputActionMap.cpp]
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Input/inputActionMap.h"

namespace dxs {

CInputActionMap::CInputActionMap() {}

CInputActionMap::~CInputActionMap() {}

//==================================================================================
// --- 初期化処理 ---
//==================================================================================
HRESULT CInputActionMap::Init(std::string_view sName) {
    m_sName = sName;
    m_bEnable = true;
    return S_OK;
}

//==================================================================================
// --- 終了処理 ---
//==================================================================================
void CInputActionMap::Uninit(void) {
    for (auto& pAction : m_vpAction) {
        if (pAction) {
            pAction->Uninit();
        }
    }

    m_vpAction.clear();
}

//==================================================================================
// --- 更新処理 ---
//==================================================================================
void CInputActionMap::Update(const CInputSystem& inputSystem) {
    if (!m_bEnable) return; // 無効ならスキップ
    for (auto& pAction : m_vpAction) {
        if (pAction) {
            pAction->Update(inputSystem);
        }
    }
}

//==================================================================================
// --- インプットアクションの追加処理 ---
//==================================================================================
CInputAction* CInputActionMap::Add(std::string_view sName, const CInputAction::ACTION_TYPE type, const float fPressPoint) {
    if (sName.empty() || !(type > CInputAction::ACTION_TYPE::NONE && type < CInputAction::ACTION_TYPE::MAX)) return nullptr;

    // 追加: 同じ名前で 2 回追加した場合は既存のアクションを返す
    // （CInputSystem::CreateActionMap のマップの扱いと同じ）
    if (auto* pExisting = Find(sName)) return pExisting;
    auto& pAction = m_vpAction.emplace_back(std::make_unique<CInputAction>());
    pAction->Init(sName, type, fPressPoint);
    return pAction.get();
}

//==================================================================================
// --- インプットアクションの検索処理 ---
//==================================================================================
CInputAction* CInputActionMap::Find(std::string_view sName) const {
    if (sName.empty()) return nullptr;
    for (const auto& pAction : m_vpAction) {
        if (pAction && pAction->IsEqualName(sName)) return pAction.get();
    }

    return nullptr;
}

//==================================================================================
// --- 有効化設定処理 ---
//==================================================================================
void CInputActionMap::SetEnable(const bool bEnable) {
    const bool bEnablePrev = m_bEnable;
    m_bEnable = bEnable;
    if (bEnablePrev == bEnable || bEnable) return; // 既に設定済み、または有効にした場合はスキップ
    // 更新されなくなるため、押下状態をリセット
    for (auto& pAction : m_vpAction) {
        if (pAction) {
            pAction->Reset();
        }
    }
}

} // namespace dxs
