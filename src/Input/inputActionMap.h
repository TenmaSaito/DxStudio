//==================================================================================
//
// インプットアクションのマップクラスのヘッダーファイル [inputActionMap.h]
//
// cpp_TKG の inputActionMap.h を移植。アクションをまとめたグループ
// （例: "Player"、"UI"）。マップ単位で有効 / 無効を切り替えられる。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Input/inputAction.h"

namespace dxs {

class CInputSystem;

//**********************************************************************************
// *** インプットアクションのマップクラス ***
//**********************************************************************************
class CInputActionMap {
public:
    CInputActionMap();
    ~CInputActionMap();

    HRESULT Init(std::string_view sName);         // 初期化
    void Uninit(void);                            // 終了
    void Update(const CInputSystem& inputSystem); // 更新（有効な時だけアクションを更新）

    // アクションの追加（同じ名前が既にあればそれを返す）
    CInputAction* Add(std::string_view sName, const CInputAction::ACTION_TYPE type,
                      const float fPressPoint = CInputAction::GetDefaultPressPoint());
    CInputAction* Find(std::string_view sName) const; // 名前で検索
    void SetEnable(const bool bEnable);               // 有効 / 無効の設定（無効にすると押下状態をリセット）
    bool IsEqualName(std::string_view sName) const { return m_sName == sName; }

    // ---- DxStudio で追加 ----------------------------------------------------
    [[nodiscard]] bool IsEnable(void) const { return m_bEnable; }
    [[nodiscard]] const std::string& GetName(void) const { return m_sName; }
    [[nodiscard]] const std::vector<std::unique_ptr<CInputAction>>& GetActions(void) const { return m_vpAction; }

private:
    std::string m_sName;                                    // アクションマップの名前
    bool m_bEnable = true;                                  // 有効フラグ
    std::vector<std::unique_ptr<CInputAction>> m_vpAction;  // インプットアクションの配列
};

} // namespace dxs
