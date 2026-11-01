//==================================================================================
//
// エディターの表示言語（英語 / 日本語）のヘッダーファイル [EditorLocale.h]
//
// 使い方
//   ・画面に出す英語の文字列を Tr() に通すと、日本語選択時は日本語訳が返る
//     （訳が無い文字列は英語のまま。キーボードショートカット "\tCtrl+S" や
//     「メッセージ: 詳細」の詳細部分はそのまま残る）。
//   ・メニュー / メッセージボックスは AppendMenuL / MessageBoxL を使う。
//   ・言語は Edit > Preferences > Language で切り替え、
//     %LOCALAPPDATA%\DxStudio\EditorPreferences.txt に保存される。
//     初回は Windows の表示言語に合わせる。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include <windows.h>

#include <string>

namespace dxs::editor {

//**********************************************************************************
// *** 表示言語 ***
//**********************************************************************************
enum class EditorLanguage : unsigned char {
    ENGLISH = 0, // 英語（既定・翻訳キー）
    JAPANESE,    // 日本語
};

//**********************************************************************************
// *** 言語の取得・設定 ***
//**********************************************************************************
EditorLanguage GetLanguage();
// 言語を切り替えて設定ファイルへ保存する（保存失敗時は false、切り替え自体は行う）
bool SetLanguage(EditorLanguage language);
// 起動時に 1 回呼ぶ: 設定ファイル → 無ければ Windows の表示言語
void LoadLanguagePreference();

//**********************************************************************************
// *** 翻訳 ***
//**********************************************************************************
// 英語の文字列 → 現在の言語の文字列（戻り値のポインタはプログラム終了まで有効）
const wchar_t* Tr(const wchar_t* pEnglish);
std::wstring Tr(const std::wstring& sEnglish);
// UTF-8 版（エラーメッセージ用）
std::string TrUtf8(const std::string& sEnglish);

// 文字列付きのメニュー項目を翻訳して追加する（区切り線などはそのまま）
BOOL AppendMenuL(HMENU hMenu, UINT uFlags, UINT_PTR uId, LPCWSTR pText);
// 本文とタイトルを翻訳してメッセージボックスを表示する
int MessageBoxL(HWND hOwner, LPCWSTR pText, LPCWSTR pCaption, UINT uType);
// コントロール作成時の表示文字列（BUTTON / STATIC のみ翻訳。入力欄の値は訳さない）
const wchar_t* LocalizedControlText(const wchar_t* pClass, const wchar_t* pText);

} // namespace dxs::editor
