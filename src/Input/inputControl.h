#pragma once

// Input control types (from cpp_TKG inputControl.h).

#include <DirectXMath.h>

namespace dxs {

using Vector3 = DirectX::XMFLOAT3;
inline const Vector3 VECTOR3_NULL{0.0f, 0.0f, 0.0f};

// Device a control is read from.
enum class INPUT_DEVICE {
    NONE = 0,       // none
    KEYBOARD,       // keyboard (nControl = DIK scan code, see Assets/Input/keyboardKeys.csv)
    MOUSE,          // mouse button (CInputMouse::KEY)
    JOYPAD_BUTTON,  // joypad button (CJoypad::KEY)
    JOYPAD_STICK,   // joypad stick (CJoypad::STICK)
    JOYPAD_AXIS,    // one joypad axis (CJoypad::AXIS)
    MAX
};

// One physical control.
struct INPUT_CONTROL_PATH {
    INPUT_DEVICE device = INPUT_DEVICE::NONE; // device type
    int nControl = 0;                         // DIK / MOUSEKEY / JOYKEY / JOYSTICK / AXIS
    int nIdxDevice = 0;                       // device number (joypad only)
};

inline INPUT_CONTROL_PATH MakePathKeyboard(int nKey) { return INPUT_CONTROL_PATH{INPUT_DEVICE::KEYBOARD, nKey, 0}; }
inline INPUT_CONTROL_PATH MakePathJoypadAxis(int nAxis, int nIdxDevice = 0) { return INPUT_CONTROL_PATH{INPUT_DEVICE::JOYPAD_AXIS, nAxis, nIdxDevice}; }
// Added for DxStudio.
inline INPUT_CONTROL_PATH MakePathMouse(int nButton) { return INPUT_CONTROL_PATH{INPUT_DEVICE::MOUSE, nButton, 0}; }
inline INPUT_CONTROL_PATH MakePathJoypadButton(int nKey, int nIdxDevice = 0) { return INPUT_CONTROL_PATH{INPUT_DEVICE::JOYPAD_BUTTON, nKey, nIdxDevice}; }
inline INPUT_CONTROL_PATH MakePathJoypadStick(int nStick, int nIdxDevice = 0) { return INPUT_CONTROL_PATH{INPUT_DEVICE::JOYPAD_STICK, nStick, nIdxDevice}; }

} // namespace dxs
