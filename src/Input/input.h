//==================================================================================
//
// 入力クラス（キーボード / マウス）のヘッダーファイル [input.h]
//
// cpp_TKG の input.h を移植。元は DirectInput8 でデバイスを読んでいたが、
// DxStudio では IInputSource インターフェース経由で読む（既定は Win32）。
// そのため DirectInput は不要で、テストでは偽の入力を差し込める。
// キー番号は DirectInput のスキャンコード（DIK_*）のままなので、
// Assets/Input/keyboardKeys.csv をそのまま使える。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Input/inputControl.h"

#include <windows.h>
#include <Xinput.h>

namespace dxs {

//**********************************************************************************
// *** 定数宣言 ***
//**********************************************************************************
constexpr int NUM_KEY_MAX = 256;             // キーボードのキー数
constexpr int INPUT_DEFAULT_WAIT_PRESS = 60; // リピートの連続入力までの待機フレーム
constexpr int INPUT_DEFAULT_INTERVAL = 30;   // リピートの連続入力の間隔フレーム

//**********************************************************************************
// *** デバイスの読み取り元インターフェース（DxStudio で追加） ***
//**********************************************************************************
class IInputSource {
public:
    virtual ~IInputSource() = default;

    // ゲームがフォーカスを持っていない間は false（すべて離された扱いになる）
    virtual bool HasFocus() = 0;

    // aState[DIK コード] = 押されていれば 0x80
    virtual void ReadKeyboard(BYTE aState[NUM_KEY_MAX]) = 0;

    // ボタン: [0] 左 / [1] 右 / [2] 中（押されていれば 0x80）、カーソルはスクリーン座標
    virtual void ReadMouse(BYTE aButtons[3], POINT& cursor) = 0;
    virtual bool ReadJoypad(int nPadID, XINPUT_STATE& state) = 0;
    virtual void SetJoypadVibration(int nPadID, const XINPUT_VIBRATION& vibration) = 0;
};

// Win32 の読み取り元（GetAsyncKeyState / GetCursorPos / XInput）
class CWin32InputSource final : public IInputSource {
public:
    bool HasFocus() override;
    void ReadKeyboard(BYTE aState[NUM_KEY_MAX]) override;
    void ReadMouse(BYTE aButtons[3], POINT& cursor) override;
    bool ReadJoypad(int nPadID, XINPUT_STATE& state) override;
    void SetJoypadVibration(int nPadID, const XINPUT_VIBRATION& vibration) override;
};

// DIK スキャンコード → 仮想キーコード（不明なら 0）
int ScanCodeToVirtualKey(int nScanCode);

//**********************************************************************************
// *** 入力クラス（デバイスの基底） ***
//**********************************************************************************
class CInput {
public:
    CInput();
    virtual ~CInput();

    virtual HRESULT Init(const HINSTANCE hInstance, const HWND hWnd);
    virtual void Uninit(void);
    virtual void Update(void) = 0;

    // 全デバイス共通の読み取り元（nullptr = Win32）
    static void SetSource(IInputSource* pSource);
    static IInputSource& Source();

protected:
    HWND m_hWnd = nullptr; // クライアント座標の基準ウィンドウ

private:
    inline static IInputSource* m_pSource = nullptr;
};

//**********************************************************************************
// *** キーボードクラス ***
//**********************************************************************************
class CInputKeyboard : public CInput {
public:
    CInputKeyboard();
    ~CInputKeyboard();

    HRESULT Init(const HINSTANCE hInstance, const HWND hWnd) override;
    void Uninit(void) override;
    void Update(void) override;
    bool GetPress(const int nKey);
    bool GetTrigger(const int nKey);
    bool GetRelease(const int nKey);
    bool GetRepeat(const int nKey, const int nWaitPress = INPUT_DEFAULT_WAIT_PRESS, const int nInterval = INPUT_DEFAULT_INTERVAL);

private:
    BYTE m_aKeyState[NUM_KEY_MAX] = {};        // プレス情報（押されている）
    BYTE m_aKeyStateTrigger[NUM_KEY_MAX] = {}; // トリガー情報（このフレームで押された）
    BYTE m_aKeyStateRelease[NUM_KEY_MAX] = {}; // リリース情報（このフレームで離された）
    int m_nKeyStateRepeat[NUM_KEY_MAX] = {};   // リピートのカウンター
};

//**********************************************************************************
// *** マウスクラス ***
//**********************************************************************************
class CInputMouse : public CInput {
public:
    // マウスのボタンの種類
    typedef enum {
        KEY_LEFT = 0, // 左クリック
        KEY_RIGHT,    // 右クリック
        KEY_WHEEL,    // 中クリック（ホイール）
        KEY_MAX
    } KEY;

    CInputMouse();
    ~CInputMouse();

    HRESULT Init(const HINSTANCE hInstance, const HWND hWnd) override;
    void Uninit(void) override;
    void Update(void) override;
    bool GetPress(const int nKey);
    bool GetTrigger(const int nKey);
    bool GetRelease(const int nKey);
    Vector3 GetPositionInScreen(void);
    Vector3 GetPositionInClient(void);
    Vector3 GetMove(void); // 追加: 前のフレームからのカーソルの移動量（ピクセル）

private:
    POINT m_cursor{};                        // カーソル位置（スクリーン座標）
    POINT m_cursorPrev{};                    // 前のフレームのカーソル位置
    bool m_bHasPrev = false;                 // 前のフレームの位置があるか
    BYTE m_aMouseState[KEY_MAX] = {};        // プレス情報
    BYTE m_aMouseStateTrigger[KEY_MAX] = {}; // トリガー情報
    BYTE m_aMouseStateRelease[KEY_MAX] = {}; // リリース情報
};

} // namespace dxs
