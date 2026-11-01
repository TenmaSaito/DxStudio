//==================================================================================
//
// インプットアクションクラスのヘッダーファイル [inputAction.h]
//
// cpp_TKG の inputAction.h を移植。「ジャンプ」「移動」などの操作の意味を表し、
// 複数の物理入力（バインディング）を束ねる。
//
// DxStudio で追加: フェーズ（PHASE）を毎フレーム更新し、コールバックを呼ぶ
// （元のコードでは今後の予定として残されていた部分）。
//   BUTTON      : 押したら STARTED + PERFORMED、離したら CANCELED
//   VALUE_1D/2D : 0 から変わったら STARTED + PERFORMED、値が変わるたびに
//                 PERFORMED、0 に戻ったら CANCELED
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Core/Delegate.h"
#include "Input/inputControl.h"

#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dxs {

//**********************************************************************************
// *** 前方宣言 ***
//**********************************************************************************
class CInputBinding;
class CInputSystem;

//**********************************************************************************
// *** インプットアクションクラス ***
//**********************************************************************************

class CInputAction {
public:
    // アクションが受け付ける入力の種類
    enum class ACTION_TYPE {
        NONE = 0, // 無効
        BUTTON,   // 押下
        VALUE_1D, // 負方向 + 正方向
        VALUE_2D, // 2D 値（WASD 入力 / スティック）
        MAX
    };

    // アクションの進行状態
    enum class PHASE {
        NONE = 0,  // 無効
        WAITING,   // 待機中
        STARTED,   // 入力し始め
        PERFORMED, // アクションが成立
        CANCELED,  // 入力が終了
        MAX
    };

    CInputAction();
    ~CInputAction();

    HRESULT Init(std::string_view sName, const ACTION_TYPE type, const float fPressPoint = DEFAULT_PRESS_POINT); // 初期化
    void Uninit(void);                            // 終了
    void Update(const CInputSystem& inputSystem); // 更新（毎フレーム）

    void Reset(void); // フラグのリセット
    bool IsPressed(void) const { return m_bPressedCurrent; }
    bool WasPressedThisFrame(void) const { return m_bPressedCurrent && !m_bPressedPrev; }
    bool WasReleasedThisFrame(void) const { return !m_bPressedCurrent && m_bPressedPrev; }
    float ReadValue1D(void) const { return m_value.x; }
    const Vector3& ReadValue2D(void) const { return m_value; }
    bool IsEqualName(std::string_view sName) const { return sName == m_sName; }

    void AddBinding(CInputBinding* pBinding); // 入力の紐づけを登録（所有権を受け取る）
    void SetOnPerformed(std::function<void(const CInputAction&)> callback) { m_callback = std::move(callback); }
    static constexpr float GetDefaultPressPoint(void) { return DEFAULT_PRESS_POINT; }

    // ---- DxStudio で追加 ----------------------------------------------------
    void SetOnStarted(std::function<void(const CInputAction&)> callback) { m_callbackStarted = std::move(callback); }
    void SetOnCanceled(std::function<void(const CInputAction&)> callback) { m_callbackCanceled = std::move(callback); }
    // 何個でも登録できる通知（Core/Delegate.h を参照）。Set... のコールバックの後に呼ばれる。
    MulticastDelegate<const CInputAction&> onStarted, onPerformed, onCanceled;
    [[nodiscard]] PHASE GetPhase(void) const { return m_phase; }
    [[nodiscard]] ACTION_TYPE GetType(void) const { return m_type; }
    [[nodiscard]] const std::string& GetName(void) const { return m_sName; }
    [[nodiscard]] float GetPressPoint(void) const { return m_fPressPoint; }
    [[nodiscard]] size_t GetBindingCount(void) const { return m_vpBinding.size(); }
    bool WasStartedThisFrame(void) const { return m_bStartedThisFrame; }
    bool WasPerformedThisFrame(void) const { return m_bPerformedThisFrame; }
    bool WasCanceledThisFrame(void) const { return m_bCanceledThisFrame; }

private:
    Vector3 EvaluateBindings(const CInputSystem& inputSystem) const; // 全バインディングから最も大きな値を取得
    void UpdatePhase(const Vector3& previous);                       // フェーズの更新とコールバックの呼び出し

    static constexpr float DEFAULT_PRESS_POINT = 0.5f;            // 既定の「押された」とみなすしきい値
    std::string m_sName;                                          // アクションの名前
    ACTION_TYPE m_type = ACTION_TYPE::NONE;                       // 受け付ける入力の種類
    PHASE m_phase = PHASE::NONE;                                  // 進行状態
    Vector3 m_value = VECTOR3_NULL;                               // 現在の値（BUTTON / 1D は x のみ、2D は x と y を使用）
    bool m_bPressedPrev = false;                                  // 前フレームで「押されている」状態だったか
    bool m_bPressedCurrent = false;                               // 今フレームで「押されている」状態か
    float m_fPressPoint = 0.0f;                                   // 「押された」とみなすしきい値（既定 0.5）
    std::vector<std::unique_ptr<CInputBinding>> m_vpBinding;      // 紐づけた物理入力（複数ある場合は大きい値を採用）
    std::function<void(const CInputAction&)> m_callback;          // 成立時のコールバック
    std::function<void(const CInputAction&)> m_callbackStarted;   // 追加: 開始時のコールバック
    std::function<void(const CInputAction&)> m_callbackCanceled;  // 追加: 終了時のコールバック
    bool m_bStartedThisFrame = false, m_bPerformedThisFrame = false, m_bCanceledThisFrame = false; // このフレームで起きたこと
};

} // namespace dxs
