//==================================================================================
//
// キー名の対応表クラスのヘッダーファイル [inputKeytable.h]
//
// cpp_TKG の inputKeytable.h を移植。キー名 ⇔ DIK スキャンコード の対応表。
// ファイルの書式（Assets/Input/keyboardKeys.csv）: 名前,コード,locked,説明
// 同じコードに複数の名前がある場合、最初の名前が正式名、以降は別名。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Core/Singleton.h"

#include <windows.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dxs {

//**********************************************************************************
// *** キー名の対応表クラス ***
//**********************************************************************************
class CInputKeyTable : public ISingleton<CInputKeyTable> {
    friend ISingleton<CInputKeyTable>; // インスタンスの生成をシングルトンに限定

public:
    HRESULT LoadFromFile(std::string_view sFilePath,
                         const HWND hWnd);                      // CSV の読み込み（hWnd があれば失敗をメッセージボックスで通知）
    bool FindCode(std::string_view sName, int& nCodeOut) const; // 名前 → コード（大文字小文字は区別しない）
    const std::string* FindName(const int nCode) const;         // コード → 正式名

    bool IsLoaded() const noexcept { return !m_mapNameToCode.empty(); } // 追加: 読み込み済みか

    void Clear(); // 追加: 表を空にする

private:
    CInputKeyTable() = default;
    ~CInputKeyTable() = default;

    void Parse(std::string sString, std::vector<std::string>& rOut); // カンマ区切りの分割

    std::unordered_map<std::string, int> m_mapNameToCode; // 大文字の名前 → コード
    std::unordered_map<int, std::string> m_mapCodeToName; // コード → 最初に出た名前
};

} // namespace dxs
