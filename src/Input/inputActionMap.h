#pragma once

// Group of input actions (from cpp_TKG inputActionMap.h).

#include "Input/inputAction.h"

namespace dxs {

class CInputSystem;

class CInputActionMap {
public:
    CInputActionMap();
    ~CInputActionMap();

    HRESULT Init(std::string_view sName);
    void Uninit(void);
    void Update(const CInputSystem& inputSystem);

    CInputAction* Add(std::string_view sName, const CInputAction::ACTION_TYPE type,
                      const float fPressPoint = CInputAction::GetDefaultPressPoint());
    CInputAction* Find(std::string_view sName) const;
    void SetEnable(const bool bEnable);
    bool IsEqualName(std::string_view sName) const { return m_sName == sName; }

    // ---- Added for DxStudio ------------------------------------------------
    [[nodiscard]] bool IsEnable(void) const { return m_bEnable; }
    [[nodiscard]] const std::string& GetName(void) const { return m_sName; }
    [[nodiscard]] const std::vector<std::unique_ptr<CInputAction>>& GetActions(void) const { return m_vpAction; }

private:
    std::string m_sName;                                    // map name
    bool m_bEnable = true;                                  // updated when enabled
    std::vector<std::unique_ptr<CInputAction>> m_vpAction;  // actions
};

} // namespace dxs
