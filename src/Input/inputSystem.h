//==================================================================================
//
// インプットシステムクラスのヘッダーファイル [inputSystem.h]
//
// cpp_TKG の inputSystem.h を移植。入力全体の窓口（シングルトン）。
//
//   auto* pInput = CInputSystem::GetInstance();
//   CInputActionMap* pMap = pInput->CreateActionMap("Player");
//   CInputAction* pJump = pMap->Add("Jump", CInputAction::ACTION_TYPE::BUTTON);
//   pJump->AddBinding(CInputBinding::CreateSingle(MakePathKeyboard(0x39))); // SPACE
//   ... 毎フレーム: pInput->Update();  if (pJump->WasPressedThisFrame()) ...
//
// DxStudio では、コンポーネントの Update の前に毎フレーム 1 回 Update() を呼ぶ。
// PlayerInput コンポーネントはアクションファイル（.dxinput。LoadActionsText を参照）を読み込む。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Core/Singleton.h"
#include "Input/inputControl.h"

#include <windows.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dxs {

//**********************************************************************************
// *** 前方宣言 ***
//**********************************************************************************
class CInputAction;
class CInputActionMap;
class CInputKeyboard;
class CInputMouse;
class CJoypad;
class IInputSource;

//**********************************************************************************
// *** インプットシステムクラス ***
//**********************************************************************************
class CInputSystem : public ISingleton<CInputSystem> {
public:
    HRESULT Init(const HINSTANCE hInstance, const HWND hWnd); // 初期化（デバイスの生成）
    void Uninit(void);                                        // 終了
    void Update(void);                                        // 更新（デバイス → アクションマップの順）

    CInputActionMap* CreateActionMap(std::string_view sName); // アクションマップの作成（同名があればそれを返す）
    CInputAction* FindAction(std::string_view sMapName, std::string_view sActionName) const; // アクションの検索
    float ReadControl(const INPUT_CONTROL_PATH& path) const;                                 // ボタン / 軸の値の取得
    Vector3 ReadStick(const INPUT_CONTROL_PATH& path) const;                                 // スティックの値の取得

    CInputKeyboard* GetKeyboard(void) const { return m_pKeyboard.get(); }

    CInputMouse* GetMouse(void) const { return m_pMouse.get(); }

    CJoypad* GetJoypad(void) const { return m_pJoypad.get(); }

    // ---- DxStudio で追加 ----------------------------------------------------
    [[nodiscard]] bool IsInitialized(void) const { return static_cast<bool>(m_pKeyboard); } // 初期化済みか

    CInputActionMap* FindActionMap(std::string_view sName) const; // アクションマップの検索

    [[nodiscard]] const std::vector<std::unique_ptr<CInputActionMap>>& GetActionMaps(void) const { return m_vpActionMap; }

    // デバイスの読み取り元の差し替え（テスト用）。nullptr で Win32 に戻す。
    void SetSource(IInputSource* pSource);

    // アクションファイル / キー名表の相対パスの基準となるプロジェクトのフォルダー
    void SetAssetRoot(const std::filesystem::path& root);

    [[nodiscard]] const std::filesystem::path& GetAssetRoot(void) const { return m_assetRoot; }

    // Assets/Input/keyboardKeys.csv を 1 回だけ読み込む（無ければ組み込みのキー名を使う）
    void EnsureKeyTable(void);

    // アクションファイル / テキストを読み込む。ファイルは 1 回だけ読む。既にある
    // マップにはアクションが追加される（同じ名前のアクションは既存の物を使う）。
    bool LoadActionsFile(const std::filesystem::path& file, std::string& error);
    bool LoadActionsText(std::string_view sText, std::string& error, std::string_view sSourceName = "actions");

    // 操作の名前 → 入力受付の情報: キーボードのキー名（keyboardKeys.csv）または 0x のスキャンコード、
    // Mouse.Left/Right/Middle、Pad[2].A/B/X/Y/LB/RB/Start/Back/Up/...、
    // Pad.LeftStick/RightStick、Pad.LeftStickUp...、Pad.LeftX/LeftY/RightX/RightY/LeftTrigger/RightTrigger
    bool ParseControl(std::string_view sName, INPUT_CONTROL_PATH& path) const;

    // 既定のアクションファイルの内容（Player: Move, Look, Jump, Fire, Sprint, Zoom）
    static const char* DefaultActionsText(void);

private:
    CInputSystem();
    ~CInputSystem();

    std::unique_ptr<CInputKeyboard> m_pKeyboard;                 // キーボード
    std::unique_ptr<CInputMouse> m_pMouse;                       // マウス
    std::unique_ptr<CJoypad> m_pJoypad;                          // ジョイパッド
    std::vector<std::unique_ptr<CInputActionMap>> m_vpActionMap; // インプットアクションのマップの配列
    std::filesystem::path m_assetRoot;                           // プロジェクトのフォルダー
    std::vector<std::filesystem::path> m_vLoadedFiles;           // 読み込み済みのアクションファイル
    bool m_bKeyTableTried = false;                               // キー名表の読み込みを試したか

    friend ISingleton<CInputSystem>; // シングルトン経由の生成のみ許可
};

} // namespace dxs
