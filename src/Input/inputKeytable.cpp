//==================================================================================
//
// キー名の対応表クラスのソースファイル [inputKeytable.cpp]
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Input/inputKeytable.h"

#include "Core/Log.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace dxs {
namespace {
// --- 大文字への変換処理 ---
std::string Upper(std::string_view sText) {
    std::string sResult(sText);
    std::transform(sResult.begin(), sResult.end(), sResult.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return sResult;
}

// --- 前後の空白の除去処理 ---
std::string Trim(const std::string& sText) {
    const auto nBegin = sText.find_first_not_of(" \t\r\n");
    if (nBegin == std::string::npos) return {};
    const auto nEnd = sText.find_last_not_of(" \t\r\n");
    return sText.substr(nBegin, nEnd - nBegin + 1);
}
} // namespace

//==================================================================================
// --- ファイル読み込み処理 ---
//==================================================================================
HRESULT CInputKeyTable::LoadFromFile(std::string_view sFilePath, const HWND hWnd) {
    std::ifstream file(std::filesystem::u8path(std::string(sFilePath)), std::ios::binary);
    if (!file.is_open()) {
        const std::string sError = "Cannot open the key table: " + std::string(sFilePath);
        if (hWnd) {
            MessageBoxA(hWnd, sError.c_str(), "Fileload Error", MB_ICONERROR);
        } else {
            Debug::LogError(sError);
        }

        return E_FAIL;
    }

    std::string sRead;
    bool bFirstLine = true;
    while (std::getline(file, sRead)) {
        if (bFirstLine && sRead.size() >= 3 && sRead.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            sRead.erase(0, 3); // UTF-8 の BOM を除く
        }

        bFirstLine = false;
        sRead = Trim(sRead);
        if (sRead.empty() || sRead[0] == '#') continue; // 空行・コメント行は飛ばす
        std::vector<std::string> vsParse;
        Parse(sRead, vsParse);
        if (vsParse.size() < 2) continue;
        const std::string sName = Upper(Trim(vsParse[0]));
        int nCode = 0;
        try {
            nCode = std::stoi(Trim(vsParse[1]), nullptr, 16);
        } catch (...) {
            continue; // 書式が正しくない行は飛ばす
        }

        if (sName.empty()) continue;
        m_mapNameToCode[sName] = nCode;
        m_mapCodeToName.emplace(nCode, Trim(vsParse[0])); // 最初の名前（正式名）だけを登録する
    }

    return S_OK;
}

//==================================================================================
// --- 名前 → コードの変換処理 ---
//==================================================================================
bool CInputKeyTable::FindCode(std::string_view sName, int& nCodeOut) const {
    const auto found = m_mapNameToCode.find(Upper(sName));
    if (found == m_mapNameToCode.end()) return false;
    nCodeOut = found->second;
    return true;
}

//==================================================================================
// --- コード → 名前の変換処理 ---
//==================================================================================
const std::string* CInputKeyTable::FindName(const int nCode) const {
    const auto found = m_mapCodeToName.find(nCode);
    return found == m_mapCodeToName.end() ? nullptr : &found->second;
}

//==================================================================================
// --- 表の消去処理 ---
//==================================================================================
void CInputKeyTable::Clear() {
    m_mapNameToCode.clear();
    m_mapCodeToName.clear();
}

//==================================================================================
// --- 文字列のパース処理（カンマ区切り） ---
//==================================================================================
void CInputKeyTable::Parse(std::string sString, std::vector<std::string>& rOut) {
    auto offset = std::string::size_type(0);
    constexpr auto SEPARATOR_LENGTH = std::string_view(",").size();
    while (true) {
        // セパレーターを検索（見つからなければ残りを最後の要素にする）
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
