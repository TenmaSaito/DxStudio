//==================================================================================
//
// 値の加工インターフェースのヘッダーファイル [inputProcessor.h]
//
// cpp_TKG の inputProcessor.h を移植。バインディングが読み取った値に、追加した順に
// 適用される（1D は値そのもの、2D は X と Y それぞれに適用）。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include <algorithm>
#include <cmath>

namespace dxs {

//**********************************************************************************
// *** 値の加工インターフェース ***
//**********************************************************************************
class IInputProcessor {
public:
    virtual ~IInputProcessor() {}
    virtual float Process(const float fValue) = 0;
};

//**********************************************************************************
// *** 組み込みの加工処理（DxStudio で追加） ***
//**********************************************************************************
// 符号を反転する
class CInputProcessorInvert final : public IInputProcessor {
public:
    float Process(const float fValue) override { return -fValue; }
};

// 倍率を掛ける
class CInputProcessorScale final : public IInputProcessor {
public:
    explicit CInputProcessorScale(float fFactor) : m_fFactor(fFactor) {}
    float Process(const float fValue) override { return fValue * m_fFactor; }
private:
    float m_fFactor = 1.0f;
};

// デッドゾーン: |値| が fMin 以下なら 0、fMax 以上なら 1、その間は 0..1 に引き伸ばす（符号は保つ）
class CInputProcessorDeadZone final : public IInputProcessor {
public:
    CInputProcessorDeadZone(float fMin, float fMax = 1.0f) : m_fMin(fMin), m_fMax(fMax) {}
    float Process(const float fValue) override {
        const float fAbs = std::abs(fValue);
        if (fAbs <= m_fMin) return 0.0f;
        const float fScaled = m_fMax > m_fMin ? std::min((fAbs - m_fMin) / (m_fMax - m_fMin), 1.0f) : 1.0f;
        return fValue < 0 ? -fScaled : fScaled;
    }
private:
    float m_fMin = 0.0f, m_fMax = 1.0f;
};

// 範囲内に収める
class CInputProcessorClamp final : public IInputProcessor {
public:
    CInputProcessorClamp(float fMin, float fMax) : m_fMin(fMin), m_fMax(fMax) {}
    float Process(const float fValue) override { return std::clamp(fValue, m_fMin, m_fMax); }
private:
    float m_fMin = -1.0f, m_fMax = 1.0f;
};

} // namespace dxs
