#pragma once

// Physical input bound to an action (from cpp_TKG inputBinding.h).

#include "Input/inputControl.h"

#include <windows.h>

#include <array>
#include <memory>
#include <vector>

namespace dxs {

class IInputProcessor;
class CInputSystem;

class CInputBinding {
public:
    enum class TYPE {
        NONE = 0,  // none
        SINGLE,    // one control (Space...)            path [0]
        AXIS_1D,   // negative + positive               [0]=negative / [1]=positive
        VECTOR_2D, // four controls (WASD)              [0]=up, [1]=down, [2]=left, [3]=right
        STICK_1D,  // one stick axis                    [0]
        STICK_2D,  // a whole stick as a 2D value       [0]
        MAX
    };

    CInputBinding();
    ~CInputBinding();

    static CInputBinding* CreateSingle(const INPUT_CONTROL_PATH& path);
    static CInputBinding* CreateAxis1D(const INPUT_CONTROL_PATH& pathNegative, const INPUT_CONTROL_PATH& pathPositive);
    static CInputBinding* CreateVector2D(const INPUT_CONTROL_PATH& pathUp, const INPUT_CONTROL_PATH& pathDown,
                                         const INPUT_CONTROL_PATH& pathLeft, const INPUT_CONTROL_PATH& pathRight);
    static CInputBinding* CreateStick1D(const INPUT_CONTROL_PATH& pathStick);
    static CInputBinding* CreateStick2D(const INPUT_CONTROL_PATH& pathStick);

    HRESULT Init(void);
    void Uninit(void);
    void SetSingle(const INPUT_CONTROL_PATH& path);
    void SetAxis1D(const INPUT_CONTROL_PATH& pathNegative, const INPUT_CONTROL_PATH& pathPositive);
    void SetVector2D(const INPUT_CONTROL_PATH& pathUp, const INPUT_CONTROL_PATH& pathDown, const INPUT_CONTROL_PATH& pathLeft,
                     const INPUT_CONTROL_PATH& pathRight);
    void SetStick1D(const INPUT_CONTROL_PATH& pathStick);
    void SetStick2D(const INPUT_CONTROL_PATH& pathStick);

    // Added: processors run in order on the value read (owned by the binding).
    CInputBinding* AddProcessor(IInputProcessor* pProcessor);
    [[nodiscard]] TYPE GetType() const noexcept { return m_type; }
    [[nodiscard]] bool Is2D() const noexcept { return m_type == TYPE::VECTOR_2D || m_type == TYPE::STICK_2D; }

    float ReadValue1D(const CInputSystem& inputSystem) const;
    Vector3 ReadValue2D(const CInputSystem& inputSystem) const;

private:
    static constexpr int PATH_MAX_COUNT = 4;                          // controls per binding
    TYPE m_type = TYPE::NONE;                                         // how the controls combine
    std::array<INPUT_CONTROL_PATH, PATH_MAX_COUNT> m_aPath = {};      // SINGLE:[0] / 1D:-,+ / 2D:up,down,left,right
    std::vector<std::unique_ptr<IInputProcessor>> m_vpProcessor;      // applied to the value in order
};

} // namespace dxs
