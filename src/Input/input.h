#pragma once

// Keyboard / mouse devices (from cpp_TKG input.h).
//
// The original read DirectInput8 devices. DxStudio reads them through the
// IInputSource interface instead (Win32 by default), so no DirectInput is
// needed and tests can feed fake input. Key numbers are still DirectInput
// scan codes (DIK_*), so Assets/Input/keyboardKeys.csv applies unchanged.

#include "Input/inputControl.h"

#include <windows.h>
#include <Xinput.h>

namespace dxs {

constexpr int NUM_KEY_MAX = 256;              // keyboard keys
constexpr int INPUT_DEFAULT_WAIT_PRESS = 60;  // frames before repeat starts
constexpr int INPUT_DEFAULT_INTERVAL = 30;    // frames between repeats

// ---- Raw device source (interface added for DxStudio) ----------------------
class IInputSource {
public:
    virtual ~IInputSource() = default;
    // False while the game does not have keyboard focus: everything reads as released.
    virtual bool HasFocus() = 0;
    // aState[DIK code] = 0x80 when held.
    virtual void ReadKeyboard(BYTE aState[NUM_KEY_MAX]) = 0;
    // Buttons: [0] left, [1] right, [2] middle (0x80 when held); cursor in screen pixels.
    virtual void ReadMouse(BYTE aButtons[3], POINT& cursor) = 0;
    virtual bool ReadJoypad(int nPadID, XINPUT_STATE& state) = 0;
    virtual void SetJoypadVibration(int nPadID, const XINPUT_VIBRATION& vibration) = 0;
};

// GetAsyncKeyState / GetCursorPos / XInput.
class CWin32InputSource final : public IInputSource {
public:
    bool HasFocus() override;
    void ReadKeyboard(BYTE aState[NUM_KEY_MAX]) override;
    void ReadMouse(BYTE aButtons[3], POINT& cursor) override;
    bool ReadJoypad(int nPadID, XINPUT_STATE& state) override;
    void SetJoypadVibration(int nPadID, const XINPUT_VIBRATION& vibration) override;
};

// DIK scan code -> virtual key (0 when unknown).
int ScanCodeToVirtualKey(int nScanCode);

// ---- Base device ----------------------------------------------------------
class CInput {
public:
    CInput();
    virtual ~CInput();

    virtual HRESULT Init(const HINSTANCE hInstance, const HWND hWnd);
    virtual void Uninit(void);
    virtual void Update(void) = 0;

    // Source shared by every device (nullptr = the Win32 source).
    static void SetSource(IInputSource* pSource);
    static IInputSource& Source();

protected:
    HWND m_hWnd = nullptr; // window for client coordinates

private:
    inline static IInputSource* m_pSource = nullptr;
};

// ---- Keyboard -------------------------------------------------------------
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
    BYTE m_aKeyState[NUM_KEY_MAX] = {};        // held
    BYTE m_aKeyStateTrigger[NUM_KEY_MAX] = {}; // pressed this frame
    BYTE m_aKeyStateRelease[NUM_KEY_MAX] = {}; // released this frame
    int m_nKeyStateRepeat[NUM_KEY_MAX] = {};   // repeat counters
};

// ---- Mouse ----------------------------------------------------------------
class CInputMouse : public CInput {
public:
    typedef enum {
        KEY_LEFT = 0, // left button
        KEY_RIGHT,    // right button
        KEY_WHEEL,    // middle (wheel) button
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
    Vector3 GetMove(void); // added: cursor movement since the last frame (pixels)

private:
    POINT m_cursor{};                    // cursor (screen)
    POINT m_cursorPrev{};                // cursor last frame
    bool m_bHasPrev = false;
    BYTE m_aMouseState[KEY_MAX] = {};        // held
    BYTE m_aMouseStateTrigger[KEY_MAX] = {}; // pressed this frame
    BYTE m_aMouseStateRelease[KEY_MAX] = {}; // released this frame
};

} // namespace dxs
