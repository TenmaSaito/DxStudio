//==================================================================================
//
// エディターの補助関数のヘッダーファイル [EditorUtility.h]
//
// エディターのウィンドウで使う、文字列の変換、シェルのダイアログ、
// プロジェクトファイル（.dxproject）の読み書きの補助関数。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <filesystem>
#include <string>

namespace dxs::editor {

//**********************************************************************************
// *** 文字列 ***
//**********************************************************************************
std::wstring Wide(const std::string& value);  // UTF-8 → UTF-16
std::string Utf8(const std::wstring& value);  // UTF-16 → UTF-8
// ウィンドウ（EDIT / STATIC / BUTTON など）の文字列
std::wstring Text(HWND hControl);
// エラーのメッセージボックスを表示し、Console にも書き出す
void Error(HWND hWindow, const std::string& message);

//**********************************************************************************
// *** ダイアログとプロジェクトファイル ***
//**********************************************************************************
bool ChooseExportFolder(HWND hOwner, const std::filesystem::path& initial, std::filesystem::path& result, std::string& error); // 書き出し先のフォルダーの選択
bool ChooseModelFile(HWND hOwner, std::filesystem::path& result);                                                              // モデルファイルの選択
// .dxproject の選択（bSave = true なら「名前を付けて保存」）
bool ChooseProjectDescriptor(HWND hOwner, bool bSave, const std::filesystem::path& initial, std::filesystem::path& result,
                             std::string& error);
bool SaveProjectDescriptor(const std::filesystem::path& path, const std::filesystem::path& root, std::string& error); // .dxproject の保存
bool LoadProjectDescriptor(const std::filesystem::path& path, std::filesystem::path& root, std::string& error);       // .dxproject の読み込み

} // namespace dxs::editor
