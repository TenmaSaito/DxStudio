#pragma once

// Input system (from cpp_TKG inputSystem.h).
//
//   auto* pInput = CInputSystem::GetInstance();
//   CInputActionMap* pMap = pInput->CreateActionMap("Player");
//   CInputAction* pJump = pMap->Add("Jump", CInputAction::ACTION_TYPE::BUTTON);
//   pJump->AddBinding(CInputBinding::CreateSingle(MakePathKeyboard(0x39))); // SPACE
//   ... every frame: pInput->Update();  if (pJump->WasPressedThisFrame()) ...
//
// DxStudio calls Update() once per frame before the components' Update, and
// the PlayerInput component loads action files (.dxinput, see LoadActionsText).

#include "Core/Singleton.h"
#include "Input/inputControl.h"

#include <windows.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dxs {

class CInputAction;
class CInputActionMap;
class CInputKeyboard;
class CInputMouse;
class CJoypad;
class IInputSource;

class CInputSystem : public ISingleton<CInputSystem> {
public:
    HRESULT Init(const HINSTANCE hInstance, const HWND hWnd);
    void Uninit(void);
    void Update(void);

    CInputActionMap* CreateActionMap(std::string_view sName);
    CInputAction* FindAction(std::string_view sMapName, std::string_view sActionName) const;
    float ReadControl(const INPUT_CONTROL_PATH& path) const;
    Vector3 ReadStick(const INPUT_CONTROL_PATH& path) const;
    CInputKeyboard* GetKeyboard(void) const { return m_pKeyboard.get(); }
    CInputMouse* GetMouse(void) const { return m_pMouse.get(); }
    CJoypad* GetJoypad(void) const { return m_pJoypad.get(); }

    // ---- Added for DxStudio ------------------------------------------------
    [[nodiscard]] bool IsInitialized(void) const { return static_cast<bool>(m_pKeyboard); }
    CInputActionMap* FindActionMap(std::string_view sName) const;
    [[nodiscard]] const std::vector<std::unique_ptr<CInputActionMap>>& GetActionMaps(void) const { return m_vpActionMap; }
    // Replaces where devices read from (tests); nullptr restores Win32.
    void SetSource(IInputSource* pSource);
    // Project folder for relative action / key-table paths.
    void SetAssetRoot(const std::filesystem::path& root);
    [[nodiscard]] const std::filesystem::path& GetAssetRoot(void) const { return m_assetRoot; }
    // Loads Assets/Input/keyboardKeys.csv once (built-in names are the fallback).
    void EnsureKeyTable(void);
    // Parses an action file / text. Each file is loaded once; a map that already
    // exists gets the actions added (existing actions are reused by name).
    bool LoadActionsFile(const std::filesystem::path& file, std::string& error);
    bool LoadActionsText(std::string_view sText, std::string& error, std::string_view sSourceName = "actions");
    // Control name -> path: keyboard key names (keyboardKeys.csv) or 0x scan
    // codes, Mouse.Left/Right/Middle, Pad[2].A/B/X/Y/LB/RB/Start/Back/Up/...,
    // Pad.LeftStick/RightStick, Pad.LeftStickUp..., Pad.LeftX/LeftY/RightX/RightY/LeftTrigger/RightTrigger.
    bool ParseControl(std::string_view sName, INPUT_CONTROL_PATH& path) const;

    // The default action file content (Player: Move, Look, Jump, Fire, Sprint, Zoom).
    static const char* DefaultActionsText(void);

private:
    CInputSystem();
    ~CInputSystem();

    std::unique_ptr<CInputKeyboard> m_pKeyboard;
    std::unique_ptr<CInputMouse> m_pMouse;
    std::unique_ptr<CJoypad> m_pJoypad;
    std::vector<std::unique_ptr<CInputActionMap>> m_vpActionMap;
    std::filesystem::path m_assetRoot;
    std::vector<std::filesystem::path> m_vLoadedFiles;
    bool m_bKeyTableTried = false;

    friend ISingleton<CInputSystem>;
};

} // namespace dxs
