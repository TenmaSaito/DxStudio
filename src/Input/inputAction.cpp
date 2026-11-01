//==================================================================================
//
// インプットアクションクラスのソースファイル [inputAction.cpp]
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Input/inputAction.h"

#include "Input/inputBinding.h"
#include "Input/inputSystem.h"

#include <cmath>

namespace dxs {
namespace {
// --- 3D の長さ ---
float Length(const Vector3& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}
} // namespace

CInputAction::CInputAction() {}

CInputAction::~CInputAction() {}

//==================================================================================
// --- 初期化処理 ---
//==================================================================================
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

//==================================================================================
// --- 終了処理 ---
//==================================================================================
void CInputAction::Uninit(void) {
    m_vpBinding.clear();
}

//==================================================================================
// --- 更新処理 ---
//==================================================================================
void CInputAction::Update(const CInputSystem& inputSystem) {
    const Vector3 previous = m_value;
    m_value = EvaluateBindings(inputSystem); // 全バインディングの中で最も大きい値
    const float fLength = Length(m_value);

    // しきい値で押下状態を判定し、前フレームの結果を保存
    m_bPressedPrev = m_bPressedCurrent;
    m_bPressedCurrent = (fLength >= m_fPressPoint);
    UpdatePhase(previous);
}

//==================================================================================
// --- フェーズの更新処理（DxStudio で追加） ---
//==================================================================================
void CInputAction::UpdatePhase(const Vector3& previous) {
    m_bStartedThisFrame = m_bPerformedThisFrame = m_bCanceledThisFrame = false;
    if (m_type == ACTION_TYPE::BUTTON) { // ボタン: 押した / 離した瞬間
        if (WasPressedThisFrame()) {
            m_bStartedThisFrame = m_bPerformedThisFrame = true;
            m_phase = PHASE::PERFORMED;
        } else if (WasReleasedThisFrame()) {
            m_bCanceledThisFrame = true;
            m_phase = PHASE::CANCELED;
        } else {
            m_phase = m_bPressedCurrent ? PHASE::PERFORMED : PHASE::WAITING;
        }
    } else { // 値: 0 から変わった / 値が変わった / 0 に戻った
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

    // コールバック → デリゲートの順に通知
    if (m_bStartedThisFrame) {
        if (m_callbackStarted) {
            m_callbackStarted(*this);
        }

        onStarted.Broadcast(*this);
    }

    if (m_bPerformedThisFrame) {
        if (m_callback) {
            m_callback(*this);
        }

        onPerformed.Broadcast(*this);
    }

    if (m_bCanceledThisFrame) {
        if (m_callbackCanceled) {
            m_callbackCanceled(*this);
        }

        onCanceled.Broadcast(*this);
    }
}

//==================================================================================
// --- フラグのリセット処理 ---
//==================================================================================
void CInputAction::Reset(void) {
    m_phase = PHASE::WAITING;
    m_value = VECTOR3_NULL;
    m_bPressedPrev = false;
    m_bPressedCurrent = false;
    m_bStartedThisFrame = m_bPerformedThisFrame = m_bCanceledThisFrame = false;
}

//==================================================================================
// --- 入力の紐づけの登録処理 ---
//==================================================================================
void CInputAction::AddBinding(CInputBinding* pBinding) {
    if (pBinding) {
        m_vpBinding.push_back(std::unique_ptr<CInputBinding>(pBinding));
    }
}

//==================================================================================
// --- 全登録済み入力から最も大きな値を取得する処理 ---
//==================================================================================
Vector3 CInputAction::EvaluateBindings(const CInputSystem& inputSystem) const {
    Vector3 valueMax = VECTOR3_NULL;
    switch (m_type) {
    case ACTION_TYPE::BUTTON:
    case ACTION_TYPE::VALUE_1D:
        for (const auto& pBinding : m_vpBinding) {
            if (!pBinding) continue;
            const float fValue = pBinding->ReadValue1D(inputSystem);
            if (std::fabs(fValue) > std::fabs(valueMax.x)) {
                valueMax.x = fValue;
            }
        }

        break;
    case ACTION_TYPE::VALUE_2D:
        for (const auto& pBinding : m_vpBinding) {
            if (!pBinding) continue;
            const Vector3 value = pBinding->ReadValue2D(inputSystem);
            if (Length(value) > Length(valueMax)) {
                valueMax = value;
            }
        }

        break;
    default: break;
    }

    return valueMax;
}

} // namespace dxs
