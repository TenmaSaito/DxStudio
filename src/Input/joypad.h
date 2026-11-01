#pragma once

// XInput joypads (from cpp_TKG joypad.h).

#include "Input/input.h"

namespace dxs {

constexpr int MAX_JOYPAD = 2;                       // supported pads
constexpr int JOYTHUMB_REPEAT_NUM = 2;              // sticks with repeat support
constexpr int JOYPAD_DEFAULT_WAIT_PRESS = 2;        // frames before repeat starts
constexpr int JOYPAD_DEFAULT_INTERVAL = 2;          // frames between repeats
constexpr float STICK_DEADZONE = 0.05f;             // stick dead zone
constexpr float JOYPAD_DEFAULT_STICK_RANGE = 0.05f; // tilt that counts as "pushed"

class CJoypad {
public:
    // Buttons (index = XINPUT_GAMEPAD bit).
    typedef enum {
        KEY_UP = 0,     // [00] D-pad up
        KEY_DOWN,       // [01] D-pad down
        KEY_LEFT,       // [02] D-pad left
        KEY_RIGHT,      // [03] D-pad right
        KEY_START,      // [04] START
        KEY_BACK,       // [05] BACK
        KEY_LEFT_PUSH,  // [06] left stick press
        KEY_RIGHT_PUSH, // [07] right stick press
        KEY_LB,         // [08] LB
        KEY_RB,         // [09] RB
        KEY_NONE1,      // [10] reserved
        KEY_NONE2,      // [11] reserved
        KEY_A,          // [12] A
        KEY_B,          // [13] B
        KEY_X,          // [14] X
        KEY_Y,          // [15] Y
        KEY_MAX
    } KEY;

    // Sticks: whole stick (2D) or one direction.
    typedef enum {
        STICK_LEFT,        // left stick (2D)
        STICK_LEFT_UP,     // left stick up
        STICK_LEFT_DOWN,   // left stick down
        STICK_LEFT_LEFT,   // left stick left
        STICK_LEFT_RIGHT,  // left stick right
        STICK_RIGHT,       // right stick (2D)
        STICK_RIGHT_UP,    // right stick up
        STICK_RIGHT_DOWN,  // right stick down
        STICK_RIGHT_LEFT,  // right stick left
        STICK_RIGHT_RIGHT, // right stick right
        STICK_MAX
    } STICK;

    // Single axes (-1..1; triggers 0..1).
    typedef enum {
        AXIS_LEFT_X = 0,    // left stick X
        AXIS_LEFT_Y,        // left stick Y
        AXIS_RIGHT_X,       // right stick X
        AXIS_RIGHT_Y,       // right stick Y
        AXIS_LEFT_TRIGGER,  // added: LT
        AXIS_RIGHT_TRIGGER, // added: RT
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
    bool IsConnected(const int nPadID = 0) const; // added

private:
    // Value of a direction stick (STICK_LEFT_UP ...) in 0..1.
    float StickDirection(const STICK stick, const int nPadID);

    typedef struct {
        XINPUT_STATE joykeyState;          // held
        XINPUT_STATE joykeyStateTrigger;   // pressed this frame
        XINPUT_STATE joykeyStateRelease;   // released this frame
        int nJoykeyStateRepeat[KEY_MAX];   // button repeat
        int nJoythumbRepeat[STICK_MAX];    // stick repeat
        XINPUT_VIBRATION vibration;        // vibration
        int nCounterVibration;             // vibration frames left
        bool bConnected;                   // added
    } XINPUT_INFO;

    XINPUT_INFO m_aJoypad[MAX_JOYPAD] = {};
};

} // namespace dxs
