#include "Input/inputKeytable.h"

#include "Core/Log.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace dxs {
namespace {
std::string Upper(std::string_view sText) {
    std::string sResult(sText);
    std::transform(sResult.begin(), sResult.end(), sResult.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return sResult;
}

std::string Trim(const std::string& sText) {
    const auto nBegin = sText.find_first_not_of(" \t\r\n");
    if (nBegin == std::string::npos) return {};
    const auto nEnd = sText.find_last_not_of(" \t\r\n");
    return sText.substr(nBegin, nEnd - nBegin + 1);
}
} // namespace

HRESULT CInputKeyTable::LoadFromFile(std::string_view sFilePath, const HWND hWnd) {
    std::ifstream file(std::filesystem::u8path(std::string(sFilePath)), std::ios::binary);
    if (!file.is_open()) {
        const std::string sError = "Cannot open the key table: " + std::string(sFilePath);
        if (hWnd) MessageBoxA(hWnd, sError.c_str(), "Fileload Error", MB_ICONERROR);
        else Debug::LogError(sError);
        return E_FAIL;
    }
    std::string sRead;
    bool bFirstLine = true;
    while (std::getline(file, sRead)) {
        if (bFirstLine && sRead.size() >= 3 && sRead.compare(0, 3, "\xEF\xBB\xBF") == 0) sRead.erase(0, 3); // UTF-8 BOM
        bFirstLine = false;
        sRead = Trim(sRead);
        if (sRead.empty() || sRead[0] == '#') continue; // comment
        std::vector<std::string> vsParse;
        Parse(sRead, vsParse);
        if (vsParse.size() < 2) continue;
        const std::string sName = Upper(Trim(vsParse[0]));
        int nCode = 0;
        try {
            nCode = std::stoi(Trim(vsParse[1]), nullptr, 16);
        } catch (...) {
            continue; // malformed row
        }
        if (sName.empty()) continue;
        m_mapNameToCode[sName] = nCode;
        m_mapCodeToName.emplace(nCode, Trim(vsParse[0])); // keep the first (official) name
    }
    return S_OK;
}

bool CInputKeyTable::FindCode(std::string_view sName, int& nCodeOut) const {
    const auto found = m_mapNameToCode.find(Upper(sName));
    if (found == m_mapNameToCode.end()) return false;
    nCodeOut = found->second;
    return true;
}

const std::string* CInputKeyTable::FindName(const int nCode) const {
    const auto found = m_mapCodeToName.find(nCode);
    return found == m_mapCodeToName.end() ? nullptr : &found->second;
}

void CInputKeyTable::Clear() {
    m_mapNameToCode.clear();
    m_mapCodeToName.clear();
}

void CInputKeyTable::Parse(std::string sString, std::vector<std::string>& rOut) {
    auto offset = std::string::size_type(0);
    constexpr auto SEPARATOR_LENGTH = std::string_view(",").size();
    while (true) {
        const auto pos = sString.find(",", offset);
        if (pos == std::string::npos) {
            rOut.push_back(sString.substr(offset));
            break;
        }
        rOut.push_back(sString.substr(offset, pos - offset));
        offset = pos + SEPARATOR_LENGTH;
    }
}

} // namespace dxs
