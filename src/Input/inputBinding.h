//==================================================================================
//
// 物理入力の紐づけクラスのヘッダーファイル [inputBinding.h]
//
// cpp_TKG の inputBinding.h を移植。アクションに紐づける物理的な入力
// （キー 1 つ、WASD の 4 キー、スティックなど）を表す。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Input/inputControl.h"

#include <windows.h>

#include <array>
#include <memory>
#include <vector>

namespace dxs {

//**********************************************************************************
// *** 前方宣言 ***
//**********************************************************************************
class IInputProcessor;
class CInputSystem;

//**********************************************************************************
// *** 物理入力の紐づけクラス ***
//**********************************************************************************

class CInputBinding {
public:
    // 読み取り（合成）方法の種類
    enum class TYPE {
        NONE = 0,  // 無効
        SINGLE,    // 1 つのコントロール（Space など）   使用配列 [0]
        AXIS_1D,   // 負方向 + 正方向                 [0]=負 / [1]=正
        VECTOR_2D, // 上下左右の 4 つ（WASD）          [0]=上, [1]=下, [2]=左, [3]=右
        STICK_1D,  // スティック 1 本の軸を指定して読む [0]
        STICK_2D,  // スティック 1 本をそのまま 2D 値で [0]
        MAX
    };

    CInputBinding();
    ~CInputBinding();

    // 生成 + 初期化 + 入力登録をまとめて行う関数（呼び出し側が delete する。通常は AddBinding に渡す）
    static CInputBinding* CreateSingle(const INPUT_CONTROL_PATH& path);
    static CInputBinding* CreateAxis1D(const INPUT_CONTROL_PATH& pathNegative, const INPUT_CONTROL_PATH& pathPositive);
    static CInputBinding* CreateVector2D(const INPUT_CONTROL_PATH& pathUp, const INPUT_CONTROL_PATH& pathDown,
                                         const INPUT_CONTROL_PATH& pathLeft, const INPUT_CONTROL_PATH& pathRight);
    static CInputBinding* CreateStick1D(const INPUT_CONTROL_PATH& pathStick);
    static CInputBinding* CreateStick2D(const INPUT_CONTROL_PATH& pathStick);

    HRESULT Init(void); // 初期化
    // 入力の登録（種類ごと）
    void Uninit(void); // 終了
    void SetSingle(const INPUT_CONTROL_PATH& path);
    void SetAxis1D(const INPUT_CONTROL_PATH& pathNegative, const INPUT_CONTROL_PATH& pathPositive);
    void SetVector2D(const INPUT_CONTROL_PATH& pathUp, const INPUT_CONTROL_PATH& pathDown, const INPUT_CONTROL_PATH& pathLeft,
                     const INPUT_CONTROL_PATH& pathRight);
    void SetStick1D(const INPUT_CONTROL_PATH& pathStick);
    void SetStick2D(const INPUT_CONTROL_PATH& pathStick);

    // 追加: 読み取った値に順に適用する加工処理（バインディングが所有・解放する）
    CInputBinding* AddProcessor(IInputProcessor* pProcessor);

    [[nodiscard]] TYPE GetType() const noexcept { return m_type; }

    [[nodiscard]] bool Is2D() const noexcept { return m_type == TYPE::VECTOR_2D || m_type == TYPE::STICK_2D; }

    float ReadValue1D(const CInputSystem& inputSystem) const;   // 1D の値の読み取り
    Vector3 ReadValue2D(const CInputSystem& inputSystem) const; // 2D の値の読み取り

private:
    static constexpr int PATH_MAX_COUNT = 4;                     // 紐づける入力の最大数
    TYPE m_type = TYPE::NONE;                                    // 読み取り（合成）方法の種類
    std::array<INPUT_CONTROL_PATH, PATH_MAX_COUNT> m_aPath = {}; // SINGLE:[0] / 1D:負,正 / 2D:上,下,左,右
    std::vector<std::unique_ptr<IInputProcessor>> m_vpProcessor; // 読み取った値に順に適用する加工処理
};

} // namespace dxs
