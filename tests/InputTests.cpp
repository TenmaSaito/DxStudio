// CInputSystem port (cpp_TKG InputSystem) and the PlayerInput component,
// driven by a fake IInputSource.

#include "Engine.h"
#include "Input/input.h"
#include "Input/inputKeytable.h"
#include "Input/joypad.h"

#include <cmath>
#include <iostream>

namespace {
int g_nFailures = 0;

void Check(bool bCondition, const char* pWhat) {
    if (bCondition) return;
    std::cerr << "FAILED: " << pWhat << '\n';
    ++g_nFailures;
}

class FakeSource final : public dxs::IInputSource {
public:
    bool bFocus = true;
    BYTE aKeys[dxs::NUM_KEY_MAX] = {};
    BYTE aButtons[3] = {};
    POINT cursor{100, 50};
    XINPUT_STATE pad{};
    bool bPadConnected = true;
    int nVibrationCalls = 0;

    bool HasFocus() override { return bFocus; }

    void ReadKeyboard(BYTE aState[dxs::NUM_KEY_MAX]) override { std::copy(std::begin(aKeys), std::end(aKeys), aState); }

    void ReadMouse(BYTE aState[3], POINT& point) override {
        std::copy(std::begin(aButtons), std::end(aButtons), aState);
        point = cursor;
    }

    bool ReadJoypad(int nPadID, XINPUT_STATE& state) override {
        state = nPadID == 0 ? pad : XINPUT_STATE{};
        return nPadID == 0 && bPadConnected;
    }

    void SetJoypadVibration(int, const XINPUT_VIBRATION&) override { ++nVibrationCalls; }

    void Key(int nCode, bool bDown) { aKeys[nCode] = bDown ? 0x80 : 0; }
};

constexpr int DIK_W = 0x11, DIK_D = 0x20, DIK_SPACE = 0x39, DIK_Q = 0x10, DIK_UP = 0xC8;

class Receiver final : public dxs::Component, public dxs::IInputReceiver {
public:
    int m_nJumpPerformed = 0, m_nJumpCanceled = 0, m_nMoveEvents = 0;

    void Update(dxs::Entity&, float) override {}

    void OnInputAction(dxs::Entity&, const dxs::CInputAction& action) override {
        if (action.IsEqualName("Jump") && action.WasPerformedThisFrame()) {
            ++m_nJumpPerformed;
        }

        if (action.IsEqualName("Jump") && action.GetPhase() == dxs::CInputAction::PHASE::CANCELED) {
            ++m_nJumpCanceled;
        }

        if (action.IsEqualName("Move")) {
            ++m_nMoveEvents;
        }
    }
};
DXS_REGISTER_COMPONENT(Receiver)
} // namespace

int main() {
    using dxs::CInputAction;
    FakeSource fake;
    auto* pSystem = dxs::CInputSystem::GetInstance();
    pSystem->SetSource(&fake);
    pSystem->SetAssetRoot("project");
    pSystem->Init(GetModuleHandleW(nullptr), nullptr);

    // 1. Key table (keyboardKeys.csv): case-insensitive names, first name is official.
    pSystem->EnsureKeyTable();
    auto* pTable = dxs::CInputKeyTable::GetInstance();
    int nCode = 0;
    Check(pTable->IsLoaded(), "key table loaded from Assets/Input/keyboardKeys.csv");
    Check(pTable->FindCode("space", nCode) && nCode == 0x39, "FindCode is case-insensitive");
    Check(pTable->FindCode("ESC", nCode) && nCode == 0x01, "aliases resolve to the same code");
    Check(pTable->FindName(0x01) && *pTable->FindName(0x01) == "ESCAPE", "FindName returns the first (official) name");
    Check(dxs::ScanCodeToVirtualKey(DIK_SPACE) == VK_SPACE && dxs::ScanCodeToVirtualKey(DIK_UP) == VK_UP &&
              dxs::ScanCodeToVirtualKey(DIK_W) == 'W',
          "DIK scan codes map to virtual keys");

    // 2. Default actions file.
    std::string error;
    Check(pSystem->LoadActionsFile("Assets/Input/DefaultInputActions.dxinput", error), "default actions file loads");
    auto* pMove = pSystem->FindAction("Player", "Move");
    auto* pJump = pSystem->FindAction("Player", "Jump");
    auto* pZoom = pSystem->FindAction("Player", "Zoom");
    Check(pMove && pJump && pZoom && pMove->GetBindingCount() == 3, "Player map has Move (3 bindings), Jump, Zoom");
    Check(pSystem->LoadActionsFile("Assets/Input/DefaultInputActions.dxinput", error) && pMove->GetBindingCount() == 3,
          "loading the same file again adds nothing");
    if (!pMove || !pJump || !pZoom) return 1;

    // 3. Vector2D (WASD): value, normalised diagonal, phases and callbacks.
    int nPerformed = 0, nStarted = 0, nCanceled = 0, nDelegate = 0;
    pMove->SetOnPerformed([&](const CInputAction&) { ++nPerformed; });
    pMove->SetOnStarted([&](const CInputAction&) { ++nStarted; });
    pMove->SetOnCanceled([&](const CInputAction&) { ++nCanceled; });
    pMove->onPerformed.AddLambda([&](const CInputAction&) { ++nDelegate; });
    fake.Key(DIK_W, true);
    pSystem->Update();
    Check(pMove->ReadValue2D().y == 1.0f && pMove->ReadValue2D().x == 0.0f, "W reads (0, 1)");
    Check(pMove->WasStartedThisFrame() && pMove->WasPerformedThisFrame() && nStarted == 1 && nPerformed == 1 && nDelegate == 1,
          "first frame: STARTED + PERFORMED callbacks and delegate");
    pSystem->Update();
    Check(!pMove->WasPerformedThisFrame() && nPerformed == 1 && pMove->GetPhase() == CInputAction::PHASE::PERFORMED,
          "unchanged value: no new PERFORMED event, phase stays PERFORMED");
    fake.Key(DIK_D, true);
    pSystem->Update();
    Check(std::abs(pMove->ReadValue2D().x - 0.7071f) < 1e-3f && std::abs(pMove->ReadValue2D().y - 0.7071f) < 1e-3f,
          "diagonal is normalised");
    Check(nPerformed == 2, "value change performs again");
    fake.Key(DIK_W, false);
    fake.Key(DIK_D, false);
    pSystem->Update();
    Check(pMove->GetPhase() == CInputAction::PHASE::CANCELED && nCanceled == 1, "release cancels");
    pSystem->Update();
    Check(pMove->GetPhase() == CInputAction::PHASE::WAITING, "then waits");

    // 4. Button: pressed / released this frame; keyboard trigger and repeat.
    fake.Key(DIK_SPACE, true);
    pSystem->Update();
    Check(pJump->WasPressedThisFrame() && pJump->IsPressed(), "Space presses Jump");
    Check(pSystem->GetKeyboard()->GetTrigger(DIK_SPACE) && pSystem->GetKeyboard()->GetPress(DIK_SPACE),
          "keyboard trigger + press");
    pSystem->Update();
    Check(!pJump->WasPressedThisFrame() && pJump->IsPressed() && !pSystem->GetKeyboard()->GetTrigger(DIK_SPACE),
          "held: no new press");
    fake.Key(DIK_SPACE, false);
    pSystem->Update();
    Check(pJump->WasReleasedThisFrame() && pSystem->GetKeyboard()->GetRelease(DIK_SPACE), "released this frame");

    // 5. Joypad: stick, button, trigger axis, vibration countdown.
    fake.pad.Gamepad.sThumbLX = 32767;
    fake.pad.Gamepad.wButtons = XINPUT_GAMEPAD_A;
    fake.pad.Gamepad.bRightTrigger = 255;
    pSystem->Update();
    Check(std::abs(pMove->ReadValue2D().x - 1.0f) < 1e-3f, "left stick drives Move");
    Check(pJump->WasPressedThisFrame(), "pad A presses Jump");
    Check(std::abs(pZoom->ReadValue1D() - 1.0f) < 1e-3f, "right trigger drives Zoom (stick1d)");
    Check(pSystem->GetJoypad()->IsConnected(0) && !pSystem->GetJoypad()->IsConnected(1), "pad connection state");
    pSystem->GetJoypad()->SetVibration(30000, 30000, 2, 0);
    pSystem->Update();
    pSystem->Update();
    Check(fake.nVibrationCalls == 2, "vibration starts and stops after its frame count");
    fake.pad = {};
    pSystem->Update();

    // 6. Axis1D + processors, mouse, focus loss.
    Check(pSystem->LoadActionsText("map Test\naction Turn VALUE_1D\naxis1d Q E\nprocessor invert\nprocessor scale 2\n"
                                   "action Click BUTTON\nsingle Mouse.Left\naction Code BUTTON\nsingle 0x39\n",
                                   error, "test"),
          "action text with processors loads");
    fake.Key(DIK_Q, true);
    fake.aButtons[0] = 0x80;
    pSystem->Update();
    Check(pSystem->FindAction("Test", "Turn")->ReadValue1D() == 2.0f, "Q (-1) inverted and scaled = 2");
    Check(pSystem->FindAction("Test", "Click")->IsPressed(), "mouse left button");
    fake.cursor = {130, 40};
    pSystem->Update();
    Check(pSystem->GetMouse()->GetMove().x == 30.0f && pSystem->GetMouse()->GetMove().y == -10.0f, "mouse movement");
    fake.bFocus = false;
    pSystem->Update();
    Check(!pSystem->FindAction("Test", "Click")->IsPressed() && pSystem->FindAction("Test", "Turn")->ReadValue1D() == 0.0f,
          "no input without focus");
    fake.bFocus = true;
    fake.Key(DIK_Q, false);
    fake.aButtons[0] = 0;
    pSystem->CreateActionMap("Test")->SetEnable(false);
    Check(!pSystem->FindActionMap("Test")->IsEnable(), "action map disable");

    // 7. Errors: line number, nothing created.
    Check(!pSystem->LoadActionsText("map Broken\naction A BUTTON\nsingle NoSuchKey\n", error, "broken") &&
              error.find("broken(3)") != std::string::npos && !pSystem->FindActionMap("Broken"),
          "unknown control reports its line and creates nothing");

    // 8. PlayerInput component + IInputReceiver in Play.
    {
        dxs::Scene scene;
        scene.projectRoot = "project";
        dxs::Entity player;
        player.sName = "Player";
        player.vComponents = {"PlayerInput", "Receiver"};
        scene.vEntities.push_back(player);
        dxs::UpdateUserComponents(scene, 1.0f / 60.0f); // Start
        fake.Key(DIK_SPACE, true);
        fake.Key(DIK_W, true);
        dxs::UpdateUserComponents(scene, 1.0f / 60.0f);
        auto* pInput = dxs::GetComponent<dxs::PlayerInput>(scene.vEntities[0]);
        auto* pReceiver = dxs::GetComponent<Receiver>(scene.vEntities[0]);
        Check(pInput && pInput->WasPressedThisFrame("Jump") && pInput->ReadValue2D("Move").y == 1.0f, "PlayerInput polling");
        Check(pReceiver && pReceiver->m_nJumpPerformed == 1 && pReceiver->m_nMoveEvents >= 1, "IInputReceiver gets the events");
        fake.Key(DIK_SPACE, false);
        dxs::UpdateUserComponents(scene, 1.0f / 60.0f);
        Check(pReceiver && pReceiver->m_nJumpCanceled == 1, "release is delivered as CANCELED");
        Check(pInput && pInput->FindAction("NoSuchAction") == nullptr && pInput->ReadValue1D("NoSuchAction") == 0.0f,
              "missing action is safe");
    }

    pSystem->Uninit();
    pSystem->SetSource(nullptr);
    if (g_nFailures) return 1;
    std::cout
        << "Input system (key table, actions file, phases, callbacks, joypad, processors, focus, PlayerInput) tests passed.\n";
    return 0;
}
