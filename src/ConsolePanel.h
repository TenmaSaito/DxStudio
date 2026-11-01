//==================================================================================
//
// Console パネルのヘッダーファイル [ConsolePanel.h]
//
// Unity 風の Console ウィンドウ: dxs::Debug のログを種類のアイコン付きで表示し、
// 種類ごとの絞り込み（件数付き）、Collapse（同じログをまとめる）、Clear ができる。
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

namespace dxs {

// hParent の子として Console を作る。大きさは MoveWindow で変える。
HWND CreateConsolePanel(HWND hParent, HINSTANCE hInstance);
void ClearConsolePanel(HWND hPanel); // 表示中のログをすべて消す

} // namespace dxs
