#pragma once

// Key name <-> DIK scan code table (from cpp_TKG inputKeytable.h).
// File format (Assets/Input/keyboardKeys.csv): name,code,locked,note
// The first name listed for a code is its official name; later ones are aliases.

#include "Core/Singleton.h"

#include <windows.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dxs {

class CInputKeyTable : public ISingleton<CInputKeyTable> {
    friend ISingleton<CInputKeyTable>;

public:
    HRESULT LoadFromFile(std::string_view sFilePath, const HWND hWnd);
    bool FindCode(std::string_view sName, int& nCodeOut) const;   // name -> code (case-insensitive)
    const std::string* FindName(const int nCode) const;           // code -> official name
    bool IsLoaded() const noexcept { return !m_mapNameToCode.empty(); } // added
    void Clear();                                                  // added

private:
    CInputKeyTable() = default;
    ~CInputKeyTable() = default;

    void Parse(std::string sString, std::vector<std::string>& rOut);

    std::unordered_map<std::string, int> m_mapNameToCode;  // upper-case name -> code
    std::unordered_map<int, std::string> m_mapCodeToName;  // code -> first name
};

} // namespace dxs
