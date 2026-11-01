//==================================================================================
//
// インプットシステムクラスのソースファイル [inputSystem.cpp]
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Input/inputSystem.h"

#include "Core/Log.h"
#include "Input/input.h"
#include "Input/inputActionMap.h"
#include "Input/inputBinding.h"
#include "Input/inputKeytable.h"
#include "Input/inputProcessor.h"
#include "Input/joypad.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace dxs {
namespace {
//**********************************************************************************
// *** 定数宣言 ***
//**********************************************************************************
constexpr const char* KEY_TABLE_FILE = "Assets/Input/keyboardKeys.csv"; // キー名表のファイル

// keyboardKeys.csv が無い場合に使う、よく使う DIK のキー名
constexpr struct {
    const char* pName;
    int nCode;
} BUILTIN_KEYS[] = {
    {"ESCAPE", 0x01}, {"1", 0x02},      {"2", 0x03},      {"3", 0x04},      {"4", 0x05},        {"5", 0x06},     {"6", 0x07},
    {"7", 0x08},      {"8", 0x09},      {"9", 0x0A},      {"0", 0x0B},      {"BACK", 0x0E},     {"TAB", 0x0F},   {"Q", 0x10},
    {"W", 0x11},      {"E", 0x12},      {"R", 0x13},      {"T", 0x14},      {"Y", 0x15},        {"U", 0x16},     {"I", 0x17},
    {"O", 0x18},      {"P", 0x19},      {"ENTER", 0x1C},  {"RETURN", 0x1C}, {"LCONTROL", 0x1D}, {"LCTRL", 0x1D}, {"A", 0x1E},
    {"S", 0x1F},      {"D", 0x20},      {"F", 0x21},      {"G", 0x22},      {"H", 0x23},        {"J", 0x24},     {"K", 0x25},
    {"L", 0x26},      {"LSHIFT", 0x2A}, {"Z", 0x2C},      {"X", 0x2D},      {"C", 0x2E},        {"V", 0x2F},     {"B", 0x30},
    {"N", 0x31},      {"M", 0x32},      {"RSHIFT", 0x36}, {"LALT", 0x38},   {"LMENU", 0x38},    {"SPACE", 0x39}, {"F1", 0x3B},
    {"F2", 0x3C},     {"F3", 0x3D},     {"F4", 0x3E},     {"F5", 0x3F},     {"F6", 0x40},       {"F7", 0x41},    {"F8", 0x42},
    {"F9", 0x43},     {"F10", 0x44},    {"F11", 0x57},    {"F12", 0x58},    {"RCONTROL", 0x9D}, {"RCTRL", 0x9D}, {"RALT", 0xB8},
    {"HOME", 0xC7},   {"UP", 0xC8},     {"PRIOR", 0xC9},  {"LEFT", 0xCB},   {"RIGHT", 0xCD},    {"END", 0xCF},   {"DOWN", 0xD0},
    {"NEXT", 0xD1},   {"INSERT", 0xD2}, {"DELETE", 0xD3},
};

// 既定のアクション定義（DefaultInputActions.dxinput と同じ内容）
const char* DEFAULT_ACTIONS = R"(# DxStudio input actions (CInputSystem::LoadActionsText)
#   map <name>                         starts an action map
#   action <name> <BUTTON|VALUE_1D|VALUE_2D> [pressPoint]
#   single <control>                   one control
#   axis1d <negative> <positive>       two controls -> -1..1
#   vector2d <up> <down> <left> <right>
#   stick1d <control>                  Pad.LeftX / Pad.LeftTrigger / Pad.LeftStickUp ...
#   stick2d <Pad.LeftStick|Pad.RightStick>
#   processor invert | scale <f> | deadzone <min> [max] | clamp <min> <max>   (last binding)
# Keyboard names come from Assets/Input/keyboardKeys.csv.
map Player
action Move VALUE_2D
vector2d W S A D
vector2d UP DOWN LEFT RIGHT
stick2d Pad.LeftStick
action Look VALUE_2D
stick2d Pad.RightStick
action Jump BUTTON
single SPACE
single Pad.A
action Fire BUTTON
single Mouse.Left
single Pad.RB
action Sprint BUTTON
single LSHIFT
single Pad.LB
action Zoom VALUE_1D
axis1d Q E
stick1d Pad.RightTrigger
)";

// --- 小文字への変換処理 ---
std::string Lower(std::string_view sText) {
    std::string sResult(sText);
    std::transform(sResult.begin(), sResult.end(), sResult.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return sResult;
}

// --- 1 行を空白区切りで分割する処理（# 以降はコメント） ---
std::vector<std::string> Tokens(const std::string& sLine) {
    std::vector<std::string> vTokens;
    std::istringstream in(sLine);
    for (std::string sToken; in >> sToken;) {
        if (sToken[0] == '#') break; // 行末のコメント
        vTokens.push_back(sToken);
    }

    return vTokens;
}
} // namespace

//==================================================================================
// --- コンストラクタ / デストラクタ ---
//==================================================================================
CInputSystem::CInputSystem() {}

CInputSystem::~CInputSystem() {}

//==================================================================================
// --- 既定のアクション定義の取得処理 ---
//==================================================================================
const char* CInputSystem::DefaultActionsText(void) {
    return DEFAULT_ACTIONS;
}

//==================================================================================
// --- 初期化処理 ---
//==================================================================================
HRESULT CInputSystem::Init(const HINSTANCE hInstance, const HWND hWnd) {
    HRESULT hr = S_OK;

    // キーボード → マウス → ジョイパッドの順に生成・初期化（生成済みなら初期化のみ）
    if (!m_pKeyboard) {
        m_pKeyboard = std::make_unique<CInputKeyboard>();
    }

    hr = m_pKeyboard->Init(hInstance, hWnd);
    if (FAILED(hr)) return hr;
    if (!m_pMouse) {
        m_pMouse = std::make_unique<CInputMouse>();
    }

    hr = m_pMouse->Init(hInstance, hWnd);
    if (FAILED(hr)) return hr;
    if (!m_pJoypad) {
        m_pJoypad = std::make_unique<CJoypad>();
        m_pJoypad->Init();
    }

    return hr;
}

//==================================================================================
// --- 終了処理 ---
//==================================================================================
void CInputSystem::Uninit(void) {
    for (auto& pActionMap : m_vpActionMap) {
        if (pActionMap) {
            pActionMap->Uninit();
        }
    }

    m_vpActionMap.clear();
    m_vLoadedFiles.clear();
    if (m_pJoypad) {
        m_pJoypad->Uninit();
    }

    m_pJoypad.reset();
    if (m_pMouse) {
        m_pMouse->Uninit();
    }

    m_pMouse.reset();
    if (m_pKeyboard) {
        m_pKeyboard->Uninit();
    }

    m_pKeyboard.reset();
}

//==================================================================================
// --- 更新処理 ---
//==================================================================================
void CInputSystem::Update(void) {
    if (!IsInitialized()) {
        Init(GetModuleHandleW(nullptr), nullptr); // 追加: 未初期化なら初回の更新で初期化する
    }

    // 各デバイスの更新 → アクションマップの更新
    if (m_pKeyboard) {
        m_pKeyboard->Update();
    }

    if (m_pMouse) {
        m_pMouse->Update();
    }

    if (m_pJoypad) {
        m_pJoypad->Update();
    }

    for (auto& pActionMap : m_vpActionMap) {
        if (pActionMap) {
            pActionMap->Update(*this);
        }
    }
}

//==================================================================================
// --- アクションマップの作成処理 ---
//==================================================================================
CInputActionMap* CInputSystem::CreateActionMap(std::string_view sName) {
    if (sName.empty()) return nullptr;
    if (auto* pExisting = FindActionMap(sName)) return pExisting; // 多重生成防止: 同じ名前のマップがあればそれを返す
    auto& pActionMap = m_vpActionMap.emplace_back(std::make_unique<CInputActionMap>());
    pActionMap->Init(sName);
    return pActionMap.get();
}

//==================================================================================
// --- アクションマップの検索処理 ---
//==================================================================================
CInputActionMap* CInputSystem::FindActionMap(std::string_view sName) const {
    for (const auto& pActionMap : m_vpActionMap) {
        if (pActionMap && pActionMap->IsEqualName(sName)) return pActionMap.get();
    }

    return nullptr;
}

//==================================================================================
// --- アクションの検索処理 ---
//==================================================================================
CInputAction* CInputSystem::FindAction(std::string_view sMapName, std::string_view sActionName) const {
    if (sMapName.empty() || sActionName.empty()) return nullptr;
    for (const auto& pActionMap : m_vpActionMap) {
        if (!pActionMap || !pActionMap->IsEqualName(sMapName)) continue;
        if (auto* pAction = pActionMap->Find(sActionName)) return pAction;
    }

    return nullptr;
}

//==================================================================================
// --- ボタン入力の取得処理 ---
//==================================================================================
float CInputSystem::ReadControl(const INPUT_CONTROL_PATH& path) const {
    if (!(path.device > INPUT_DEVICE::NONE && path.device < INPUT_DEVICE::MAX) || !IsInitialized()) return 0.0f;
    switch (path.device) {
    case INPUT_DEVICE::KEYBOARD: return m_pKeyboard->GetPress(path.nControl) ? 1.0f : 0.0f;
    case INPUT_DEVICE::MOUSE: return m_pMouse->GetPress(path.nControl) ? 1.0f : 0.0f;
    case INPUT_DEVICE::JOYPAD_BUTTON:
        if (!(path.nIdxDevice >= 0 && path.nIdxDevice < MAX_JOYPAD)) return 0.0f;
        return m_pJoypad->GetPress(static_cast<CJoypad::KEY>(path.nControl), path.nIdxDevice) ? 1.0f : 0.0f;
    case INPUT_DEVICE::JOYPAD_AXIS:
        if (!(path.nIdxDevice >= 0 && path.nIdxDevice < MAX_JOYPAD)) return 0.0f;
        return m_pJoypad->GetAxis(static_cast<CJoypad::AXIS>(path.nControl), path.nIdxDevice);
    case INPUT_DEVICE::JOYPAD_STICK: {
        // 追加: 方向スティックは 0..1、スティック全体は倒し具合を返す
        const Vector3 value = ReadStick(path);
        return std::sqrt(value.x * value.x + value.y * value.y);
    }
    default: return 0.0f;
    }
}

//==================================================================================
// --- スティック入力の取得処理 ---
//==================================================================================
Vector3 CInputSystem::ReadStick(const INPUT_CONTROL_PATH& path) const {
    if (path.device != INPUT_DEVICE::JOYPAD_STICK || !(path.nIdxDevice >= 0 && path.nIdxDevice < MAX_JOYPAD) || !m_pJoypad) {
        return VECTOR3_NULL;
    }

    Vector3 value = VECTOR3_NULL;
    m_pJoypad->GetStick(static_cast<CJoypad::STICK>(path.nControl), &value, path.nIdxDevice);
    return value;
}

//==================================================================================
// --- 読み取り元の差し替え処理 ---
//==================================================================================
void CInputSystem::SetSource(IInputSource* pSource) {
    CInput::SetSource(pSource);
}

//==================================================================================
// --- プロジェクトのフォルダーの設定処理 ---
//==================================================================================
void CInputSystem::SetAssetRoot(const std::filesystem::path& root) {
    if (root == m_assetRoot) return;
    m_assetRoot = root;
    m_bKeyTableTried = false; // フォルダーが変わったらキー名表を読み直せるようにする
}

//==================================================================================
// --- キー名表の読み込み処理（1 回だけ） ---
//==================================================================================
void CInputSystem::EnsureKeyTable(void) {
    auto* pTable = CInputKeyTable::GetInstance();
    if (pTable->IsLoaded() || m_bKeyTableTried) return;
    m_bKeyTableTried = true;
    std::error_code ec;
    const auto file = m_assetRoot / std::filesystem::u8path(KEY_TABLE_FILE);
    if (std::filesystem::is_regular_file(file, ec)) {
        const auto u8 = file.u8string();
        pTable->LoadFromFile(std::string(reinterpret_cast<const char*>(u8.data()), u8.size()), nullptr);
    }
}

//==================================================================================
// --- 操作の名前の読み取り処理 ---
//==================================================================================
bool CInputSystem::ParseControl(std::string_view sName, INPUT_CONTROL_PATH& path) const {
    const std::string sLower = Lower(sName);
    if (sLower.rfind("mouse.", 0) == 0) { // マウス
        const std::string sButton = sLower.substr(6);
        const int nButton = sButton == "left"                           ? CInputMouse::KEY_LEFT
                            : sButton == "right"                        ? CInputMouse::KEY_RIGHT
                            : sButton == "middle" || sButton == "wheel" ? CInputMouse::KEY_WHEEL
                                                                        : -1;
        if (nButton < 0) return false;
        path = MakePathMouse(nButton);
        return true;
    }

    if (sLower.rfind("pad", 0) == 0) {
        // ジョイパッド: Pad.A（1 台目）または Pad2.A（2 台目）
        const auto nDot = sLower.find('.');
        if (nDot == std::string::npos) return false;
        int nPad = 0;
        if (nDot > 3) {
            try {
                nPad = std::stoi(sLower.substr(3, nDot - 3)) - 1;
            } catch (...) {
                return false;
            }
        }

        if (nPad < 0 || nPad >= MAX_JOYPAD) return false;
        const std::string sControl = sLower.substr(nDot + 1);

        static const struct {
            const char* pName;
            int nKey;
        } BUTTONS[] = {{"up", CJoypad::KEY_UP},
                       {"down", CJoypad::KEY_DOWN},
                       {"left", CJoypad::KEY_LEFT},
                       {"right", CJoypad::KEY_RIGHT},
                       {"start", CJoypad::KEY_START},
                       {"back", CJoypad::KEY_BACK},
                       {"leftstickpress", CJoypad::KEY_LEFT_PUSH},
                       {"rightstickpress", CJoypad::KEY_RIGHT_PUSH},
                       {"lb", CJoypad::KEY_LB},
                       {"rb", CJoypad::KEY_RB},
                       {"a", CJoypad::KEY_A},
                       {"b", CJoypad::KEY_B},
                       {"x", CJoypad::KEY_X},
                       {"y", CJoypad::KEY_Y}};

        for (const auto& button : BUTTONS) {
            if (sControl == button.pName) {
                path = MakePathJoypadButton(button.nKey, nPad);
                return true;
            }
        }

        static const struct {
            const char* pName;
            int nStick;
        } STICKS[] = {{"leftstick", CJoypad::STICK_LEFT},
                      {"leftstickup", CJoypad::STICK_LEFT_UP},
                      {"leftstickdown", CJoypad::STICK_LEFT_DOWN},
                      {"leftstickleft", CJoypad::STICK_LEFT_LEFT},
                      {"leftstickright", CJoypad::STICK_LEFT_RIGHT},
                      {"rightstick", CJoypad::STICK_RIGHT},
                      {"rightstickup", CJoypad::STICK_RIGHT_UP},
                      {"rightstickdown", CJoypad::STICK_RIGHT_DOWN},
                      {"rightstickleft", CJoypad::STICK_RIGHT_LEFT},
                      {"rightstickright", CJoypad::STICK_RIGHT_RIGHT}};

        for (const auto& stick : STICKS) {
            if (sControl == stick.pName) {
                path = MakePathJoypadStick(stick.nStick, nPad);
                return true;
            }
        }

        static const struct {
            const char* pName;
            int nAxis;
        } AXES[] = {{"leftx", CJoypad::AXIS_LEFT_X},
                    {"lefty", CJoypad::AXIS_LEFT_Y},
                    {"rightx", CJoypad::AXIS_RIGHT_X},
                    {"righty", CJoypad::AXIS_RIGHT_Y},
                    {"lefttrigger", CJoypad::AXIS_LEFT_TRIGGER},
                    {"righttrigger", CJoypad::AXIS_RIGHT_TRIGGER}};

        for (const auto& axis : AXES) {
            if (sControl == axis.pName) {
                path = MakePathJoypadAxis(axis.nAxis, nPad);
                return true;
            }
        }

        return false;
    }

    // キーボード: 0x のスキャンコード、キー名表の名前、組み込みの名前の順に試す
    if (sLower.size() > 2 && sLower.rfind("0x", 0) == 0) {
        try {
            const int nCode = std::stoi(sLower.substr(2), nullptr, 16);
            if (nCode <= 0 || nCode >= NUM_KEY_MAX) return false;
            path = MakePathKeyboard(nCode);
            return true;
        } catch (...) {
            return false;
        }
    }

    int nCode = 0;
    if (CInputKeyTable::GetInstance()->FindCode(sName, nCode)) {
        path = MakePathKeyboard(nCode);
        return true;
    }

    for (const auto& key : BUILTIN_KEYS) {
        if (Lower(key.pName) == sLower) {
            path = MakePathKeyboard(key.nCode);
            return true;
        }
    }

    return false;
}

//==================================================================================
// --- アクションファイルの読み込み処理 ---
//==================================================================================
bool CInputSystem::LoadActionsFile(const std::filesystem::path& file, std::string& error) {
    error.clear();
    const auto full = file.is_absolute() ? file : m_assetRoot / file;
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(full, ec);
    if (std::find(m_vLoadedFiles.begin(), m_vLoadedFiles.end(), canonical) != m_vLoadedFiles.end()) return true; // 読み込み済み
    std::ifstream in(full, std::ios::binary);
    if (!in) {
        const auto u8 = full.u8string();
        error = "Cannot open input actions: " + std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
        return false;
    }

    std::ostringstream text;
    text << in.rdbuf();
    std::string sText = text.str();
    if (sText.size() >= 3 && sText.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        sText.erase(0, 3);
    }

    const auto u8 = full.filename().u8string();
    if (!LoadActionsText(sText, error, std::string(reinterpret_cast<const char*>(u8.data()), u8.size()))) return false;
    m_vLoadedFiles.push_back(canonical);
    return true;
}

//==================================================================================
// --- アクション定義の読み込み処理（テキスト） ---
//==================================================================================
bool CInputSystem::LoadActionsText(std::string_view sText, std::string& error, std::string_view sSourceName) {
    error.clear();
    EnsureKeyTable();

    // まず全体を読み取る。途中に誤りがあれば何も作らない。
    struct BindingSpec {
        std::string sKind;
        std::vector<INPUT_CONTROL_PATH> vPaths;
        std::vector<std::vector<std::string>> vProcessors;
    };

    struct ActionSpec {
        std::string sName;
        CInputAction::ACTION_TYPE type{};
        float fPressPoint = CInputAction::GetDefaultPressPoint();
        std::vector<BindingSpec> vBindings;
    };

    struct MapSpec {
        std::string sName;
        std::vector<ActionSpec> vActions;
    };

    std::vector<MapSpec> vMaps;
    std::istringstream in{std::string(sText)};
    int nLine = 0;
    auto fail = [&](const std::string& sMessage) {
        error = std::string(sSourceName) + "(" + std::to_string(nLine) + "): " + sMessage;
        return false;
    };

    // 1 行ずつ: map / action / バインディング / processor
    for (std::string sLine; std::getline(in, sLine);) {
        ++nLine;
        const auto vTokens = Tokens(sLine);
        if (vTokens.empty()) continue;
        const std::string sCommand = Lower(vTokens[0]);
        if (sCommand == "map") {
            if (vTokens.size() != 2) return fail("expected: map <name>");
            vMaps.push_back({vTokens[1], {}});
        } else if (sCommand == "action") {
            if (vMaps.empty()) return fail("'action' before any 'map'");
            if (vTokens.size() < 3 || vTokens.size() > 4) {
                return fail("expected: action <name> <BUTTON|VALUE_1D|VALUE_2D> [pressPoint]");
            }

            const std::string sType = Lower(vTokens[2]);
            ActionSpec action{vTokens[1]};
            if (sType == "button") {
                action.type = CInputAction::ACTION_TYPE::BUTTON;
            } else if (sType == "value_1d") {
                action.type = CInputAction::ACTION_TYPE::VALUE_1D;
            } else if (sType == "value_2d") {
                action.type = CInputAction::ACTION_TYPE::VALUE_2D;
            } else {
                return fail("unknown action type '" + vTokens[2] + "'");
            }

            if (vTokens.size() == 4) {
                try {
                    action.fPressPoint = std::stof(vTokens[3]);
                } catch (...) {
                    return fail("invalid press point");
                }
            }

            vMaps.back().vActions.push_back(std::move(action));
        } else if (sCommand == "single" || sCommand == "axis1d" || sCommand == "vector2d" || sCommand == "stick1d" ||
                   sCommand == "stick2d") {
            if (vMaps.empty() || vMaps.back().vActions.empty()) return fail("binding before any 'action'");
            const size_t nExpected = sCommand == "axis1d" ? 2 : sCommand == "vector2d" ? 4 : 1;
            if (vTokens.size() != nExpected + 1) return fail(sCommand + " expects " + std::to_string(nExpected) + " control(s)");
            BindingSpec binding{sCommand};
            for (size_t nIndex = 1; nIndex < vTokens.size(); ++nIndex) {
                INPUT_CONTROL_PATH path;
                if (!ParseControl(vTokens[nIndex], path)) return fail("unknown control '" + vTokens[nIndex] + "'");
                binding.vPaths.push_back(path);
            }

            if (sCommand == "stick2d" &&
                (binding.vPaths[0].device != INPUT_DEVICE::JOYPAD_STICK ||
                 (binding.vPaths[0].nControl != CJoypad::STICK_LEFT && binding.vPaths[0].nControl != CJoypad::STICK_RIGHT))) {
                return fail("stick2d needs Pad.LeftStick or Pad.RightStick");
            }

            vMaps.back().vActions.back().vBindings.push_back(std::move(binding));
        } else if (sCommand == "processor") {
            if (vMaps.empty() || vMaps.back().vActions.empty() || vMaps.back().vActions.back().vBindings.empty()) {
                return fail("processor before any binding");
            }

            const std::string sKind = vTokens.size() > 1 ? Lower(vTokens[1]) : "";
            const size_t nArguments = vTokens.size() - 2;
            const bool bValid = (sKind == "invert" && nArguments == 0) || (sKind == "scale" && nArguments == 1) ||
                                (sKind == "deadzone" && (nArguments == 1 || nArguments == 2)) ||
                                (sKind == "clamp" && nArguments == 2);
            if (!bValid) return fail("expected: processor invert | scale <f> | deadzone <min> [max] | clamp <min> <max>");
            std::vector<std::string> vProcessor(vTokens.begin() + 1, vTokens.end());
            for (size_t nIndex = 1; nIndex < vProcessor.size(); ++nIndex) {
                try {
                    (void)std::stof(vProcessor[nIndex]);
                } catch (...) {
                    return fail("invalid number '" + vProcessor[nIndex] + "'");
                }
            }

            vMaps.back().vActions.back().vBindings.back().vProcessors.push_back(std::move(vProcessor));
        } else {
            return fail("unknown command '" + vTokens[0] + "'");
        }
    }

    // 読み取った内容からマップ・アクション・バインディング・加工処理を作る
    for (const auto& mapSpec : vMaps) {
        auto* pMap = CreateActionMap(mapSpec.sName);
        for (const auto& actionSpec : mapSpec.vActions) {
            auto* pAction = pMap->Add(actionSpec.sName, actionSpec.type, actionSpec.fPressPoint);
            if (!pAction) continue;
            for (const auto& bindingSpec : actionSpec.vBindings) {
                const auto& vPaths = bindingSpec.vPaths;
                CInputBinding* pBinding = bindingSpec.sKind == "single"   ? CInputBinding::CreateSingle(vPaths[0])
                                          : bindingSpec.sKind == "axis1d" ? CInputBinding::CreateAxis1D(vPaths[0], vPaths[1])
                                          : bindingSpec.sKind == "vector2d"
                                              ? CInputBinding::CreateVector2D(vPaths[0], vPaths[1], vPaths[2], vPaths[3])
                                          : bindingSpec.sKind == "stick1d" ? CInputBinding::CreateStick1D(vPaths[0])
                                                                           : CInputBinding::CreateStick2D(vPaths[0]);
                for (const auto& vProcessor : bindingSpec.vProcessors) {
                    const std::string sKind = Lower(vProcessor[0]);
                    auto argument = [&](size_t nIndex, float fDefault) {
                        return nIndex < vProcessor.size() ? std::stof(vProcessor[nIndex]) : fDefault;
                    };
                    if (sKind == "invert") {
                        pBinding->AddProcessor(new CInputProcessorInvert());
                    } else if (sKind == "scale") {
                        pBinding->AddProcessor(new CInputProcessorScale(argument(1, 1.0f)));
                    } else if (sKind == "deadzone") {
                        pBinding->AddProcessor(new CInputProcessorDeadZone(argument(1, 0.0f), argument(2, 1.0f)));
                    } else if (sKind == "clamp") {
                        pBinding->AddProcessor(new CInputProcessorClamp(argument(1, -1.0f), argument(2, 1.0f)));
                    }
                }

                pAction->AddBinding(pBinding);
            }
        }
    }

    return true;
}

} // namespace dxs
