//==================================================================================
//
// 入力操作の型をまとめたヘッダーファイル [inputControl.h]
//
// cpp_TKG の inputControl.h を移植。どのデバイスのどのボタン / 軸を読むかを表す。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include <DirectXMath.h>

namespace dxs {

// cpp_TKG の Vector3 に相当する型と、すべて 0 の値
using Vector3 = DirectX::XMFLOAT3;
inline const Vector3 VECTOR3_NULL{0.0f, 0.0f, 0.0f};

//**********************************************************************************
// *** 入力受付の対象デバイス ***
//**********************************************************************************
enum class INPUT_DEVICE {
    NONE = 0,      // 無効
    KEYBOARD,      // キーボード（nControl = DIK スキャンコード。Assets/Input/keyboardKeys.csv を参照）
    MOUSE,         // マウスのボタン（CInputMouse::KEY）
    JOYPAD_BUTTON, // ジョイパッドのボタン（CJoypad::KEY）
    JOYPAD_STICK,  // ジョイパッドのスティック（CJoypad::STICK）
    JOYPAD_AXIS,   // ジョイパッドの軸 1 本（CJoypad::AXIS）
    MAX
};

//**********************************************************************************
// *** 入力受付の情報（物理的な操作 1 つ） ***
//**********************************************************************************
struct INPUT_CONTROL_PATH {
    INPUT_DEVICE device = INPUT_DEVICE::NONE; // デバイスの種類
    int nControl = 0;                         // 受け付けるキー（DIK / マウス / ジョイパッドのボタン / スティック / 軸）
    int nIdxDevice = 0;                       // デバイス番号（ジョイパッドのみ使用）
};

//**********************************************************************************
// *** 入力受付の情報の作成関数 ***
//**********************************************************************************
inline INPUT_CONTROL_PATH MakePathKeyboard(int nKey) {
    return INPUT_CONTROL_PATH{INPUT_DEVICE::KEYBOARD, nKey, 0};
}

inline INPUT_CONTROL_PATH MakePathJoypadAxis(int nAxis, int nIdxDevice = 0) {
    return INPUT_CONTROL_PATH{INPUT_DEVICE::JOYPAD_AXIS, nAxis, nIdxDevice};
}

// DxStudio で追加
inline INPUT_CONTROL_PATH MakePathMouse(int nButton) {
    return INPUT_CONTROL_PATH{INPUT_DEVICE::MOUSE, nButton, 0};
}

inline INPUT_CONTROL_PATH MakePathJoypadButton(int nKey, int nIdxDevice = 0) {
    return INPUT_CONTROL_PATH{INPUT_DEVICE::JOYPAD_BUTTON, nKey, nIdxDevice};
}

inline INPUT_CONTROL_PATH MakePathJoypadStick(int nStick, int nIdxDevice = 0) {
    return INPUT_CONTROL_PATH{INPUT_DEVICE::JOYPAD_STICK, nStick, nIdxDevice};
}

} // namespace dxs
