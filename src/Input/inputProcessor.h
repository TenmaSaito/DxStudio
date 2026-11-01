#pragma once

// Value processors applied by a binding (from cpp_TKG inputProcessor.h).
// Processors run in the order they were added, on the 1D value, or on X and
// Y separately for 2D bindings.

#include <algorithm>
#include <cmath>

namespace dxs {

class IInputProcessor {
public:
    virtual ~IInputProcessor() {}
    virtual float Process(const float fValue) = 0;
};

// ---- Built-in processors (added for DxStudio) ------------------------------
class CInputProcessorInvert final : public IInputProcessor {
public:
    float Process(const float fValue) override { return -fValue; }
};

class CInputProcessorScale final : public IInputProcessor {
public:
    explicit CInputProcessorScale(float fFactor) : m_fFactor(fFactor) {}
    float Process(const float fValue) override { return fValue * m_fFactor; }
private:
    float m_fFactor = 1.0f;
};

// |value| below fMin -> 0, above fMax -> 1, rescaled in between (sign kept).
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

class CInputProcessorClamp final : public IInputProcessor {
public:
    CInputProcessorClamp(float fMin, float fMax) : m_fMin(fMin), m_fMax(fMax) {}
    float Process(const float fValue) override { return std::clamp(fValue, m_fMin, m_fMax); }
private:
    float m_fMin = -1.0f, m_fMax = 1.0f;
};

} // namespace dxs
