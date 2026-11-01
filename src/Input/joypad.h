//==================================================================================
//
// ジョイパッドクラスのヘッダーファイル [joypad.h]
//
// cpp_TKG の joypad.h を移植。XInput 対応のパッドを読む。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Input/input.h"

namespace dxs {

//**********************************************************************************
// *** 定数宣言 ***
//**********************************************************************************
constexpr int MAX_JOYPAD = 2;                       // ジョイパッドの最大数
constexpr int JOYTHUMB_REPEAT_NUM = 2;              // リピートに対応しているスティックの数
constexpr int JOYPAD_DEFAULT_WAIT_PRESS = 2;        // リピートの連続入力までの待機フレーム
constexpr int JOYPAD_DEFAULT_INTERVAL = 2;          // リピートの連続入力の間隔フレーム
constexpr float STICK_DEADZONE = 0.05f;             // 動いたと感知するデッドゾーン
constexpr float JOYPAD_DEFAULT_STICK_RANGE = 0.05f; // スティックを倒したと判定する範囲

//**********************************************************************************
// *** ジョイパッドクラス ***
//**********************************************************************************
class CJoypad {
public:
    // ボタン入力一覧（番号 = XINPUT_GAMEPAD のビット位置）
    typedef enum {
        KEY_UP = 0,     // [00] 十字キー（上）
        KEY_DOWN,       // [01] 十字キー（下）
        KEY_LEFT,       // [02] 十字キー（左）
        KEY_RIGHT,      // [03] 十字キー（右）
        KEY_START,      // [04] START
        KEY_BACK,       // [05] BACK
        KEY_LEFT_PUSH,  // [06] 左スティック押し込み
        KEY_RIGHT_PUSH, // [07] 右スティック押し込み
        KEY_LB,         // [08] LB
        KEY_RB,         // [09] RB
        KEY_NONE1,      // [10] 予約済み
        KEY_NONE2,      // [11] 予約済み
        KEY_A,          // [12] A
        KEY_B,          // [13] B
        KEY_X,          // [14] X
        KEY_Y,          // [15] Y
        KEY_MAX
    } KEY;

    // スティック入力一覧（スティック全体 = 2D、または 1 方向）
    typedef enum {
        STICK_LEFT,        // 左スティック（2D で取得）
        STICK_LEFT_UP,     // 左スティック（上）
        STICK_LEFT_DOWN,   // 左スティック（下）
        STICK_LEFT_LEFT,   // 左スティック（左）
        STICK_LEFT_RIGHT,  // 左スティック（右）
        STICK_RIGHT,       // 右スティック（2D で取得）
        STICK_RIGHT_UP,    // 右スティック（上）
        STICK_RIGHT_DOWN,  // 右スティック（下）
        STICK_RIGHT_LEFT,  // 右スティック（左）
        STICK_RIGHT_RIGHT, // 右スティック（右）
        STICK_MAX
    } STICK;

    // 軸の一覧（-1..1。トリガーは 0..1）
    typedef enum {
        AXIS_LEFT_X = 0,    // 左スティックの X 軸
        AXIS_LEFT_Y,        // 左スティックの Y 軸
        AXIS_RIGHT_X,       // 右スティックの X 軸
        AXIS_RIGHT_Y,       // 右スティックの Y 軸
        AXIS_LEFT_TRIGGER,  // 追加: 左トリガー（LT）
        AXIS_RIGHT_TRIGGER, // 追加: 右トリガー（RT）
        AXIS_MAX
    } AXIS;

    CJoypad();
    ~CJoypad();

    void Init(void);
    void Uninit(void);
    void Update(void);

    bool GetPress(const KEY key, const int nPadID = 0);
    bool GetTrigger(const KEY key, const int nPadID = 0);
    bool GetRelease(const KEY key, const int nPadID = 0);
    bool GetRepeat(const KEY key, const int nWaitPress = JOYPAD_DEFAULT_WAIT_PRESS, const int nInterval = JOYPAD_DEFAULT_INTERVAL,
                   const int nPadID = 0);
    float GetAxis(const AXIS axis, const int nPadID = 0);

    bool GetStick(const STICK stick, const float fRange = JOYPAD_DEFAULT_STICK_RANGE, const int nPadID = 0);
    bool GetStick(const STICK stick, Vector3* pOut, const int nPadID = 0);
    bool GetStickRepeat(const STICK stick, const float fRange = JOYPAD_DEFAULT_STICK_RANGE,
                        const int nWaitPress = JOYPAD_DEFAULT_WAIT_PRESS, const int nInterval = JOYPAD_DEFAULT_INTERVAL,
                        const int nPadID = 0);

    void SetVibration(const int nLPower, const int nRPower, const int nTime, const int nPadID = 0);
    bool IsConnected(const int nPadID = 0) const; // 追加: 接続されているか

private:
    // 方向スティック（STICK_LEFT_UP など）の倒し具合 0..1
    float StickDirection(const STICK stick, const int nPadID);

    // ジョイパッド 1 台分の情報
    typedef struct {
        XINPUT_STATE joykeyState;        // プレス情報
        XINPUT_STATE joykeyStateTrigger; // トリガー情報
        XINPUT_STATE joykeyStateRelease; // リリース情報
        int nJoykeyStateRepeat[KEY_MAX]; // ボタンのリピート情報
        int nJoythumbRepeat[STICK_MAX];  // スティックのリピート情報
        XINPUT_VIBRATION vibration;      // 振動の情報
        int nCounterVibration;           // 振動の残りフレーム
        bool bConnected;                 // 追加: 接続されているか
    } XINPUT_INFO;

    XINPUT_INFO m_aJoypad[MAX_JOYPAD] = {}; // ジョイパッドの情報
};

} // namespace dxs
