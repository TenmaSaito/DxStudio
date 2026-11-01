//==================================================================================
//
// Project パネル（アセットのブラウザー）のヘッダーファイル [ProjectPanel.h]
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include <windows.h>
#include <filesystem>
#include <string>

namespace dxs {
// 単独で動く子ウィンドウを作る。大きさは MoveWindow で変え、親を破棄すると
// パネルも破棄される。ブラウザーは projectRoot の外には出ない。
HWND CreateProjectPanel(HWND parent, HINSTANCE instance, const std::filesystem::path& projectRoot,
                        const std::filesystem::path& engineRoot = {});

// スクリプトを編集するための Visual Studio のプロジェクトを生成する
bool GenerateVisualStudioProject(const std::filesystem::path& projectRoot,
                                 const std::filesystem::path& engineRoot,
                                 std::wstring& error);

// Project の右クリックメニューとエディターの Settings メニューで共通のコマンド
void ChooseProjectScriptEditor(HWND panel);       // スクリプトエディターの選択
void UseAutomaticProjectScriptEditor(HWND panel); // スクリプトエディターを自動で選ぶ
void RefreshProjectPanel(HWND panel);             // 表示の更新
void NewProjectComponent(HWND panel);             // 新しいコンポーネントの作成

// project/Scripts に、登録済みの C++ コンポーネントとヘッダーを作る。
// 名前は ASCII の識別子であること。既にあるファイルは置き換えない。
bool CreateProjectComponent(const std::filesystem::path& projectRoot,
                            const std::wstring& name, std::wstring& error);

// ユーザーが選んだファイルを、既存のファイルを置き換えずにプロジェクトへコピーする。
// コピー先がプロジェクトの外に出る場合、リパースポイントを使う場合、コピーに
// 失敗した場合は false。error にはユーザー向けの説明が入る。
bool ImportProjectFile(const std::filesystem::path& projectRoot,
                       const std::filesystem::path& destinationFolder,
                       const std::filesystem::path& source,
                       std::wstring& error);
}
