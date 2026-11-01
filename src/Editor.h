//==================================================================================
//
// エディターの入口のヘッダーファイル [Editor.h]
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

namespace dxs {
// エディターを起動し、ウィンドウが閉じられるまでメッセージループを回す
//   baseDir            : 実行ファイルのフォルダー（Game.exe やシェーダーの場所）
//   initialProjectFile : 最初に開く .dxproject（空なら前回 / 既定のプロジェクト）
int RunEditor(HINSTANCE instance, const std::filesystem::path& baseDir,
              const std::filesystem::path& initialProjectFile = {});
}
