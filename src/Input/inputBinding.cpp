//==================================================================================
//
// 物理入力の紐づけクラスのソースファイル [inputBinding.cpp]
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Input/inputBinding.h"

#include "Input/inputProcessor.h"
#include "Input/inputSystem.h"

#include <cmath>

namespace dxs {
namespace {
// --- 2D の長さ ---
float Length2D(const Vector3& value) {
    return std::sqrt(value.x * value.x + value.y * value.y);
}
} // namespace

CInputBinding::CInputBinding() {}

CInputBinding::~CInputBinding() {}

//==================================================================================
// --- 単発入力の生成処理 ---
//==================================================================================
CInputBinding* CInputBinding::CreateSingle(const INPUT_CONTROL_PATH& path) {
    auto* pBinding = new CInputBinding();
    pBinding->Init();
    pBinding->SetSingle(path);
    return pBinding;
}

//==================================================================================
// --- 正負の軸値入力の生成処理 ---
//==================================================================================
CInputBinding* CInputBinding::CreateAxis1D(const INPUT_CONTROL_PATH& pathNegative, const INPUT_CONTROL_PATH& pathPositive) {
    auto* pBinding = new CInputBinding();
    pBinding->Init();
    pBinding->SetAxis1D(pathNegative, pathPositive);
    return pBinding;
}

//==================================================================================
// --- 上下左右のコントロール入力の生成処理 ---
//==================================================================================
CInputBinding* CInputBinding::CreateVector2D(const INPUT_CONTROL_PATH& pathUp, const INPUT_CONTROL_PATH& pathDown,
                                             const INPUT_CONTROL_PATH& pathLeft, const INPUT_CONTROL_PATH& pathRight) {
    auto* pBinding = new CInputBinding();
    pBinding->Init();
    pBinding->SetVector2D(pathUp, pathDown, pathLeft, pathRight);
    return pBinding;
}

//==================================================================================
// --- 軸指定スティック入力の生成処理 ---
//==================================================================================
CInputBinding* CInputBinding::CreateStick1D(const INPUT_CONTROL_PATH& pathStick) {
    auto* pBinding = new CInputBinding();
    pBinding->Init();
    pBinding->SetStick1D(pathStick);
    return pBinding;
}

//==================================================================================
// --- スティック入力の生成処理 ---
//==================================================================================
CInputBinding* CInputBinding::CreateStick2D(const INPUT_CONTROL_PATH& pathStick) {
    auto* pBinding = new CInputBinding();
    pBinding->Init();
    pBinding->SetStick2D(pathStick);
    return pBinding;
}

//==================================================================================
// --- 初期化 / 終了処理 ---
//==================================================================================
HRESULT CInputBinding::Init(void) {
    m_type = TYPE::NONE;
    m_aPath = {};
    m_vpProcessor.clear();
    return S_OK;
}

void CInputBinding::Uninit(void) {
    m_vpProcessor.clear();
}

//==================================================================================
// --- 入力の登録処理（種類ごと） ---
//==================================================================================
void CInputBinding::SetSingle(const INPUT_CONTROL_PATH& path) {
    m_type = TYPE::SINGLE;
    m_aPath[0] = path;
}

void CInputBinding::SetAxis1D(const INPUT_CONTROL_PATH& pathNegative, const INPUT_CONTROL_PATH& pathPositive) {
    m_type = TYPE::AXIS_1D;
    m_aPath[0] = pathNegative;
    m_aPath[1] = pathPositive;
}

void CInputBinding::SetVector2D(const INPUT_CONTROL_PATH& pathUp, const INPUT_CONTROL_PATH& pathDown,
                                const INPUT_CONTROL_PATH& pathLeft, const INPUT_CONTROL_PATH& pathRight) {
    m_type = TYPE::VECTOR_2D;
    m_aPath[0] = pathUp;
    m_aPath[1] = pathDown;
    m_aPath[2] = pathLeft;
    m_aPath[3] = pathRight;
}

void CInputBinding::SetStick1D(const INPUT_CONTROL_PATH& path) {
    m_type = TYPE::STICK_1D;
    m_aPath[0] = path;
}

void CInputBinding::SetStick2D(const INPUT_CONTROL_PATH& path) {
    m_type = TYPE::STICK_2D;
    m_aPath[0] = path;
}

//==================================================================================
// --- 加工処理の追加処理 ---
//==================================================================================
CInputBinding* CInputBinding::AddProcessor(IInputProcessor* pProcessor) {
    if (pProcessor) {
        m_vpProcessor.emplace_back(pProcessor);
    }

    return this;
}

//==================================================================================
// --- 単発・軸値の取得処理 ---
//==================================================================================
float CInputBinding::ReadValue1D(const CInputSystem& inputSystem) const {
    float fValue = 0.0f;
    switch (m_type) {
    case TYPE::SINGLE:
    case TYPE::STICK_1D: fValue = inputSystem.ReadControl(m_aPath[0]); break;
    case TYPE::AXIS_1D: fValue = inputSystem.ReadControl(m_aPath[1]) - inputSystem.ReadControl(m_aPath[0]); break; // 正 - 負
    case TYPE::VECTOR_2D:
    case TYPE::STICK_2D:
        fValue = Length2D(ReadValue2D(inputSystem));
        return fValue; // 2D の入力を 1D のアクションに使う場合は長さ
    default: break;
    }

    // 加工処理を順に適用
    for (const auto& pProcessor : m_vpProcessor) {
        if (pProcessor) {
            fValue = pProcessor->Process(fValue);
        }
    }

    return fValue;
}

//==================================================================================
// --- 上下左右・スティックの取得処理 ---
//==================================================================================
Vector3 CInputBinding::ReadValue2D(const CInputSystem& inputSystem) const {
    Vector3 value = VECTOR3_NULL;
    switch (m_type) {
    case TYPE::VECTOR_2D: {
        const float fUpper = inputSystem.ReadControl(m_aPath[0]);
        const float fLower = inputSystem.ReadControl(m_aPath[1]);
        const float fLeft = inputSystem.ReadControl(m_aPath[2]);
        const float fRight = inputSystem.ReadControl(m_aPath[3]);
        value = Vector3(fRight - fLeft, fUpper - fLower, 0.0f);
        const float fLength = Length2D(value);
        if (fLength > 1.0f) {
            value = {value.x / fLength, value.y / fLength, 0.0f}; // 斜め入力で速くならないよう、長さを 1 に収める
        }

        break;
    }
    case TYPE::STICK_2D: value = inputSystem.ReadStick(m_aPath[0]); break;
    case TYPE::SINGLE:
    case TYPE::AXIS_1D:
    case TYPE::STICK_1D: {
        // 1D の入力を 2D のアクションに使う場合は X に入れる（加工処理は ReadValue1D 内で適用済み）
        value.x = ReadValue1D(inputSystem);
        return value;
    }
    default: break;
    }

    // 加工処理を X と Y それぞれに適用
    for (const auto& pProcessor : m_vpProcessor) {
        if (!pProcessor) continue;
        value.x = pProcessor->Process(value.x);
        value.y = pProcessor->Process(value.y);
    }

    return value;
}

} // namespace dxs
