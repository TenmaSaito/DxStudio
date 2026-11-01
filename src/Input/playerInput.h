//==================================================================================
//
// PlayerInput コンポーネントのヘッダーファイル [playerInput.h]
//
// CInputSystem を使った PlayerInput コンポーネント（Unity の PlayerInput 相当）。
// Add Component > Input > PlayerInput で付けると、アクションファイル（.dxinput）を
// CInputSystem に読み込み、アクションマップを 1 つ有効にする。スクリプトからは:
//   ・毎フレーム読む:  auto* pInput = dxs::GetComponent<dxs::PlayerInput>(entity);
//                      const auto move = pInput->ReadValue2D("Move");
//                      if (pInput->WasPressedThisFrame("Jump")) ...
//   ・イベントで受け取る: IInputReceiver も継承して
//                      void OnInputAction(Entity&, const CInputAction& action) override
//                      を実装する。アクションの開始・成立（値の変化）・終了のときに、
//                      同じ物体のコンポーネントへ呼ばれる（どれかは GetPhase() で分かる）。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Engine.h"
#include "Input/inputAction.h"
#include "Input/inputActionMap.h"
#include "Input/inputBinding.h"
#include "Input/inputSystem.h"

namespace dxs {

//**********************************************************************************
// *** 入力イベントを受け取るインターフェース ***
//**********************************************************************************
class IInputReceiver {
public:
    virtual ~IInputReceiver() = default;
    virtual void OnInputAction(Entity& entity, const CInputAction& action) = 0;
};

//**********************************************************************************
// *** PlayerInput コンポーネント ***
//**********************************************************************************
class PlayerInput final : public Component {
public:
    static constexpr const char* DEFAULT_ACTIONS_FILE = "Assets/Input/DefaultInputActions.dxinput"; // 既定のアクションファイル

    void Start(Entity& entity) override;                       // アクションファイルを読み込みマップを有効にする
    void Update(Entity& entity, float fDeltaSeconds) override; // このフレームのイベントを IInputReceiver へ届ける

    std::vector<EditorField> EditorFields() override {
        return {EditField("Actions File", m_sActionsFile), EditField("Action Map", m_sActionMap)};
    }

    [[nodiscard]] CInputActionMap* GetActionMap() const;                  // 使っているアクションマップ
    [[nodiscard]] CInputAction* FindAction(std::string_view sName) const; // マップ内のアクションの検索
    // よく使う読み取りの近道（アクションが無ければ 0 / false）
    [[nodiscard]] Vector3 ReadValue2D(std::string_view sName) const;
    [[nodiscard]] float ReadValue1D(std::string_view sName) const;
    [[nodiscard]] bool IsPressed(std::string_view sName) const;
    [[nodiscard]] bool WasPressedThisFrame(std::string_view sName) const;
    [[nodiscard]] bool WasReleasedThisFrame(std::string_view sName) const;

    std::string m_sActionsFile = DEFAULT_ACTIONS_FILE; // アクションファイル（プロジェクトからの相対パス）
    std::string m_sActionMap = "Player";               // 使うアクションマップの名前
};

} // namespace dxs
