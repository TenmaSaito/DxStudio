#include "Input/joypad.h"

#include <algorithm>
#include <cmath>

namespace dxs {
namespace {
bool ValidPad(int nPadID) { return nPadID >= 0 && nPadID < MAX_JOYPAD; }
bool ValidKey(int nKey) { return nKey >= 0 && nKey < CJoypad::KEY_MAX; }

// Thumb value in -1..1 with the dead zone removed.
float Thumb(SHORT sValue) {
    const float fValue = std::max(static_cast<float>(sValue) / 32767.0f, -1.0f);
    return std::abs(fValue) < STICK_DEADZONE ? 0.0f : fValue;
}
} // namespace

CJoypad::CJoypad() {}
CJoypad::~CJoypad() {}

void CJoypad::Init(void) {
    for (auto& pad : m_aJoypad) pad = {};
}

void CJoypad::Uninit(void) {
    for (int nPadID = 0; nPadID < MAX_JOYPAD; ++nPadID)
        if (m_aJoypad[nPadID].nCounterVibration > 0) CInput::Source().SetJoypadVibration(nPadID, XINPUT_VIBRATION{});
    Init();
}

void CJoypad::Update(void) {
    auto& source = CInput::Source();
    const bool bFocus = source.HasFocus();
    for (int nPadID = 0; nPadID < MAX_JOYPAD; ++nPadID) {
        auto& pad = m_aJoypad[nPadID];
        XINPUT_STATE state{};
        pad.bConnected = source.ReadJoypad(nPadID, state);
        if (!bFocus) state.Gamepad = {}; // like the keyboard: no input without focus
        const WORD wOld = pad.joykeyState.Gamepad.wButtons, wNew = state.Gamepad.wButtons;
        pad.joykeyStateTrigger.Gamepad.wButtons = static_cast<WORD>((wOld ^ wNew) & wNew);
        pad.joykeyStateRelease.Gamepad.wButtons = static_cast<WORD>(wOld & (wOld ^ wNew));
        for (int nKey = 0; nKey < KEY_MAX; ++nKey)
            if (((wOld ^ wNew) >> nKey) & 1) pad.nJoykeyStateRepeat[nKey] = 0;
        pad.joykeyState = state;
        if (pad.nCounterVibration > 0 && --pad.nCounterVibration == 0) {
            pad.vibration = {};
            source.SetJoypadVibration(nPadID, pad.vibration);
        }
    }
}

bool CJoypad::GetPress(const KEY key, const int nPadID) {
    return ValidPad(nPadID) && ValidKey(key) && (m_aJoypad[nPadID].joykeyState.Gamepad.wButtons & (1u << key));
}

bool CJoypad::GetTrigger(const KEY key, const int nPadID) {
    return ValidPad(nPadID) && ValidKey(key) && (m_aJoypad[nPadID].joykeyStateTrigger.Gamepad.wButtons & (1u << key));
}

bool CJoypad::GetRelease(const KEY key, const int nPadID) {
    return ValidPad(nPadID) && ValidKey(key) && (m_aJoypad[nPadID].joykeyStateRelease.Gamepad.wButtons & (1u << key));
}

bool CJoypad::GetRepeat(const KEY key, const int nWaitPress, const int nInterval, const int nPadID) {
    if (!ValidPad(nPadID) || !ValidKey(key)) return false;
    int& nRepeat = m_aJoypad[nPadID].nJoykeyStateRepeat[key];
    nRepeat++;
    if (nRepeat < nWaitPress) return GetTrigger(key, nPadID);
    return nInterval > 0 && nRepeat % nInterval == 0 && GetPress(key, nPadID);
}

float CJoypad::GetAxis(const AXIS axis, const int nPadID) {
    if (!ValidPad(nPadID)) return 0.0f;
    const auto& gamepad = m_aJoypad[nPadID].joykeyState.Gamepad;
    switch (axis) {
    case AXIS_LEFT_X: return Thumb(gamepad.sThumbLX);
    case AXIS_LEFT_Y: return Thumb(gamepad.sThumbLY);
    case AXIS_RIGHT_X: return Thumb(gamepad.sThumbRX);
    case AXIS_RIGHT_Y: return Thumb(gamepad.sThumbRY);
    case AXIS_LEFT_TRIGGER: return static_cast<float>(gamepad.bLeftTrigger) / 255.0f;
    case AXIS_RIGHT_TRIGGER: return static_cast<float>(gamepad.bRightTrigger) / 255.0f;
    default: return 0.0f;
    }
}

float CJoypad::StickDirection(const STICK stick, const int nPadID) {
    const bool bLeft = stick <= STICK_LEFT_RIGHT;
    const float fX = GetAxis(bLeft ? AXIS_LEFT_X : AXIS_RIGHT_X, nPadID);
    const float fY = GetAxis(bLeft ? AXIS_LEFT_Y : AXIS_RIGHT_Y, nPadID);
    switch (stick) {
    case STICK_LEFT_UP: case STICK_RIGHT_UP: return std::max(fY, 0.0f);
    case STICK_LEFT_DOWN: case STICK_RIGHT_DOWN: return std::max(-fY, 0.0f);
    case STICK_LEFT_LEFT: case STICK_RIGHT_LEFT: return std::max(-fX, 0.0f);
    case STICK_LEFT_RIGHT: case STICK_RIGHT_RIGHT: return std::max(fX, 0.0f);
    default: return std::sqrt(fX * fX + fY * fY);
    }
}

bool CJoypad::GetStick(const STICK stick, const float fRange, const int nPadID) {
    return ValidPad(nPadID) && stick >= 0 && stick < STICK_MAX && StickDirection(stick, nPadID) > fRange;
}

bool CJoypad::GetStick(const STICK stick, Vector3* pOut, const int nPadID) {
    if (!pOut) return false;
    *pOut = VECTOR3_NULL;
    if (!ValidPad(nPadID) || stick < 0 || stick >= STICK_MAX) return false;
    const bool bLeft = stick <= STICK_LEFT_RIGHT;
    if (stick == STICK_LEFT || stick == STICK_RIGHT) {
        pOut->x = GetAxis(bLeft ? AXIS_LEFT_X : AXIS_RIGHT_X, nPadID);
        pOut->y = GetAxis(bLeft ? AXIS_LEFT_Y : AXIS_RIGHT_Y, nPadID);
        const float fLength = std::sqrt(pOut->x * pOut->x + pOut->y * pOut->y);
        if (fLength > 1.0f) *pOut = {pOut->x / fLength, pOut->y / fLength, 0.0f};
    } else {
        pOut->x = StickDirection(stick, nPadID);
    }
    return pOut->x != 0.0f || pOut->y != 0.0f;
}

bool CJoypad::GetStickRepeat(const STICK stick, const float fRange, const int nWaitPress, const int nInterval, const int nPadID) {
    if (!ValidPad(nPadID) || stick < 0 || stick >= STICK_MAX) return false;
    int& nRepeat = m_aJoypad[nPadID].nJoythumbRepeat[stick];
    if (!GetStick(stick, fRange, nPadID)) {
        nRepeat = 0;
        return false;
    }
    const bool bFirst = nRepeat == 0;
    nRepeat++;
    if (bFirst) return true;
    return nRepeat >= nWaitPress && nInterval > 0 && nRepeat % nInterval == 0;
}

void CJoypad::SetVibration(const int nLPower, const int nRPower, const int nTime, const int nPadID) {
    if (!ValidPad(nPadID)) return;
    auto& pad = m_aJoypad[nPadID];
    pad.vibration.wLeftMotorSpeed = static_cast<WORD>(std::clamp(nLPower, 0, 65535));
    pad.vibration.wRightMotorSpeed = static_cast<WORD>(std::clamp(nRPower, 0, 65535));
    pad.nCounterVibration = std::max(nTime, 0);
    CInput::Source().SetJoypadVibration(nPadID, pad.vibration);
}

bool CJoypad::IsConnected(const int nPadID) const { return ValidPad(nPadID) && m_aJoypad[nPadID].bConnected; }

} // namespace dxs
