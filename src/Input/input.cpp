//==================================================================================
//
// 入力クラス（キーボード / マウス）のソースファイル [input.cpp]
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Input/input.h"

#pragma comment(lib, "xinput9_1_0.lib") // XInput（Windows 標準の版）をリンク

namespace dxs {

//**********************************************************************************
// *** Win32 の読み取り元 ***
//**********************************************************************************
// --- フォーカスの判定処理（最前面のウィンドウがこのプロセスの物か） ---
bool CWin32InputSource::HasFocus() {
    HWND hForeground = GetForegroundWindow();
    DWORD dwProcess = 0;
    if (hForeground) {
        GetWindowThreadProcessId(hForeground, &dwProcess);
    }

    return hForeground && dwProcess == GetCurrentProcessId();
}

//==================================================================================
// --- DIK スキャンコード → 仮想キーコードの変換処理 ---
//==================================================================================
int ScanCodeToVirtualKey(int nScanCode) {
    if (nScanCode <= 0 || nScanCode >= NUM_KEY_MAX) return 0;

    // 0x80 以上の DIK コードは E0 付きの拡張キー。MapVirtualKey で正しく
    // 変換できない物があるため、主なキーはここで直接対応させる。
    switch (nScanCode) {
    case 0xC8: return VK_UP;
    case 0xD0: return VK_DOWN;
    case 0xCB: return VK_LEFT;
    case 0xCD: return VK_RIGHT;
    case 0xC7: return VK_HOME;
    case 0xCF: return VK_END;
    case 0xC9: return VK_PRIOR;
    case 0xD1: return VK_NEXT;
    case 0xD2: return VK_INSERT;
    case 0xD3: return VK_DELETE;
    case 0x9D: return VK_RCONTROL;
    case 0xB8: return VK_RMENU;
    case 0x1D: return VK_LCONTROL;
    case 0x38: return VK_LMENU;
    case 0x2A: return VK_LSHIFT;
    case 0x36: return VK_RSHIFT;
    case 0xDB: return VK_LWIN;
    case 0xDC: return VK_RWIN;
    case 0xDD: return VK_APPS;
    case 0x9C: return VK_RETURN; // テンキーの Enter
    case 0xB5: return VK_DIVIDE;
    case 0xC5: return VK_PAUSE;
    case 0xB7: return VK_SNAPSHOT;
    default: break;
    }

    const UINT uScan = nScanCode >= 0x80 ? (0xE000u | static_cast<UINT>(nScanCode & 0x7F)) : static_cast<UINT>(nScanCode);
    return static_cast<int>(MapVirtualKeyW(uScan, MAPVK_VSC_TO_VK_EX));
}

// --- キーボードの読み取り処理（変換表は初回だけ作る） ---
void CWin32InputSource::ReadKeyboard(BYTE aState[NUM_KEY_MAX]) {
    static int s_aVirtualKey[NUM_KEY_MAX] = {};
    static bool s_bMapped = false;
    if (!s_bMapped) {
        for (int nCode = 0; nCode < NUM_KEY_MAX; ++nCode) {
            s_aVirtualKey[nCode] = ScanCodeToVirtualKey(nCode);
        }

        s_bMapped = true;
    }

    for (int nCode = 0; nCode < NUM_KEY_MAX; ++nCode) {
        aState[nCode] = s_aVirtualKey[nCode] && (GetAsyncKeyState(s_aVirtualKey[nCode]) & 0x8000) ? 0x80 : 0x00;
    }
}

// --- マウスの読み取り処理 ---
void CWin32InputSource::ReadMouse(BYTE aButtons[3], POINT& cursor) {
    // 左右入れ替えの設定は GetAsyncKeyState 側で処理される（物理ボタンで読む）
    const int aVirtualKey[3] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON};
    for (int nButton = 0; nButton < 3; ++nButton) {
        aButtons[nButton] = (GetAsyncKeyState(aVirtualKey[nButton]) & 0x8000) ? 0x80 : 0x00;
    }

    GetCursorPos(&cursor);
}

// --- ジョイパッドの読み取り / 振動の設定処理 ---
bool CWin32InputSource::ReadJoypad(int nPadID, XINPUT_STATE& state) {
    ZeroMemory(&state, sizeof(state));
    return XInputGetState(static_cast<DWORD>(nPadID), &state) == ERROR_SUCCESS;
}

void CWin32InputSource::SetJoypadVibration(int nPadID, const XINPUT_VIBRATION& vibration) {
    XINPUT_VIBRATION copy = vibration;
    XInputSetState(static_cast<DWORD>(nPadID), &copy);
}

//**********************************************************************************
// *** 入力クラス（基底） ***
//**********************************************************************************
CInput::CInput() {}

CInput::~CInput() {}

// --- 初期化 / 終了処理 ---
HRESULT CInput::Init(const HINSTANCE, const HWND hWnd) {
    m_hWnd = hWnd;
    return S_OK;
}

void CInput::Uninit(void) {
    m_hWnd = nullptr;
}

// --- 読み取り元の設定 / 取得処理（未設定なら Win32） ---
void CInput::SetSource(IInputSource* pSource) {
    m_pSource = pSource;
}

IInputSource& CInput::Source() {
    static CWin32InputSource win32;
    return m_pSource ? *m_pSource : win32;
}

//**********************************************************************************
// *** キーボードクラス ***
//**********************************************************************************
CInputKeyboard::CInputKeyboard() {}

CInputKeyboard::~CInputKeyboard() {}

//==================================================================================
// --- 初期化処理（状態をすべて 0 に） ---
//==================================================================================
HRESULT CInputKeyboard::Init(const HINSTANCE hInstance, const HWND hWnd) {
    HRESULT hr = CInput::Init(hInstance, hWnd);
    ZeroMemory(m_aKeyState, sizeof(m_aKeyState));
    ZeroMemory(m_aKeyStateTrigger, sizeof(m_aKeyStateTrigger));
    ZeroMemory(m_aKeyStateRelease, sizeof(m_aKeyStateRelease));
    ZeroMemory(m_nKeyStateRepeat, sizeof(m_nKeyStateRepeat));
    return hr;
}

void CInputKeyboard::Uninit(void) {
    CInput::Uninit();
}

//==================================================================================
// --- 更新処理 ---
//==================================================================================
void CInputKeyboard::Update(void) {
    BYTE aKeyState[NUM_KEY_MAX] = {}; // 今フレームのキーボードの入力情報
    auto& source = Source();
    if (source.HasFocus()) {
        source.ReadKeyboard(aKeyState); // フォーカスが無ければすべて離された扱い
    }

    for (int nCntKey = 0; nCntKey < NUM_KEY_MAX; nCntKey++) {
        // リリース = 前は押されていて今は違う / トリガー = 前は違って今は押されている
        m_aKeyStateRelease[nCntKey] = (m_aKeyState[nCntKey] & (m_aKeyState[nCntKey] ^ aKeyState[nCntKey]));
        m_aKeyStateTrigger[nCntKey] = ((aKeyState[nCntKey] ^ m_aKeyState[nCntKey]) & aKeyState[nCntKey]);
        if (m_aKeyState[nCntKey] != aKeyState[nCntKey]) {
            m_nKeyStateRepeat[nCntKey] = 0; // 状態が変わったらリピートのカウントをやり直す
        }

        m_aKeyState[nCntKey] = aKeyState[nCntKey];
    }
}

//==================================================================================
// --- プレス / トリガー / リリースの判定処理 ---
//==================================================================================
bool CInputKeyboard::GetPress(const int nKey) {
    return nKey >= 0 && nKey < NUM_KEY_MAX && (m_aKeyState[nKey] & 0x80);
}

bool CInputKeyboard::GetTrigger(const int nKey) {
    return nKey >= 0 && nKey < NUM_KEY_MAX && (m_aKeyStateTrigger[nKey] & 0x80);
}

bool CInputKeyboard::GetRelease(const int nKey) {
    return nKey >= 0 && nKey < NUM_KEY_MAX && (m_aKeyStateRelease[nKey] & 0x80);
}

//==================================================================================
// --- リピートの判定処理 ---
//==================================================================================
bool CInputKeyboard::GetRepeat(const int nKey, const int nWaitPress, const int nInterval) {
    if (nKey < 0 || nKey >= NUM_KEY_MAX) return false;
    m_nKeyStateRepeat[nKey]++;
    if (m_nKeyStateRepeat[nKey] < nWaitPress) return (m_aKeyStateTrigger[nKey] & 0x80) != 0; // 待機時間内はトリガーだけ
    if (nInterval > 0 && m_nKeyStateRepeat[nKey] % nInterval == 0) return (m_aKeyState[nKey] & 0x80) != 0; // 間隔ごとにプレスを返す
    return false;
}

//**********************************************************************************
// *** マウスクラス ***
//**********************************************************************************
CInputMouse::CInputMouse() {}

CInputMouse::~CInputMouse() {}

HRESULT CInputMouse::Init(const HINSTANCE hInstance, const HWND hWnd) {
    m_bHasPrev = false;
    return CInput::Init(hInstance, hWnd);
}

void CInputMouse::Uninit(void) {
    CInput::Uninit();
}

//==================================================================================
// --- 更新処理 ---
//==================================================================================
void CInputMouse::Update(void) {
    BYTE aButtons[KEY_MAX] = {};
    POINT cursor = m_cursor;
    auto& source = Source();
    if (source.HasFocus()) {
        source.ReadMouse(aButtons, cursor);
    }

    m_cursorPrev = m_bHasPrev ? m_cursor : cursor; // 初回は移動量 0 になるようにする
    m_cursor = cursor;
    m_bHasPrev = true;
    for (int nKey = 0; nKey < KEY_MAX; nKey++) {
        m_aMouseStateRelease[nKey] = (m_aMouseState[nKey] & (m_aMouseState[nKey] ^ aButtons[nKey]));
        m_aMouseStateTrigger[nKey] = ((aButtons[nKey] ^ m_aMouseState[nKey]) & aButtons[nKey]);
        m_aMouseState[nKey] = aButtons[nKey];
    }
}

//==================================================================================
// --- プレス / トリガー / リリースの判定処理 ---
//==================================================================================
bool CInputMouse::GetPress(const int nKey) {
    return nKey >= 0 && nKey < KEY_MAX && (m_aMouseState[nKey] & 0x80);
}

bool CInputMouse::GetTrigger(const int nKey) {
    return nKey >= 0 && nKey < KEY_MAX && (m_aMouseStateTrigger[nKey] & 0x80);
}

bool CInputMouse::GetRelease(const int nKey) {
    return nKey >= 0 && nKey < KEY_MAX && (m_aMouseStateRelease[nKey] & 0x80);
}

//==================================================================================
// --- カーソル位置 / 移動量の取得処理 ---
//==================================================================================
Vector3 CInputMouse::GetPositionInScreen(void) {
    return {static_cast<float>(m_cursor.x), static_cast<float>(m_cursor.y), 0.0f};
}

Vector3 CInputMouse::GetPositionInClient(void) {
    POINT client = m_cursor;
    if (m_hWnd) {
        ScreenToClient(m_hWnd, &client);
    }

    return {static_cast<float>(client.x), static_cast<float>(client.y), 0.0f};
}

Vector3 CInputMouse::GetMove(void) {
    return {static_cast<float>(m_cursor.x - m_cursorPrev.x), static_cast<float>(m_cursor.y - m_cursorPrev.y), 0.0f};
}

} // namespace dxs
