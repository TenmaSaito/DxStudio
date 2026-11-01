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
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dxs {
// Project パネルのアイテムをパネルの外へドラッグして離したときに、最上位のウィンドウ
// （エディター）へ送るメッセージ。lParam は ProjectAssetDrop*（SendMessage の間だけ有効）。
constexpr UINT WM_DXS_PROJECT_ASSET_DROP = WM_APP + 44;

struct ProjectAssetDrop {
    std::filesystem::path file; // ドラッグしたファイル（絶対パス）
    POINT screen{};             // 離した位置（スクリーン座標）
};

// サムネイルを描く関数（エディターが設定する。AssetThumbnail.h の RenderAssetThumbnail と同じ形）。
// nSize x nSize の 0xAARRGGBB（上の行から、背景は透明）を返す。描けなければ false。
using ProjectThumbnailProvider = bool (*)(const std::filesystem::path& file, int nSize, std::vector<std::uint32_t>& vPixels);
void SetProjectThumbnailProvider(ProjectThumbnailProvider provider);

// Project パネルから、パネルを持つ最上位のウィンドウ（エディター）へ送るメッセージ。
//   wParam : PROJECT_ASSET_CREATE_MATERIAL（X → .dxmat）または PROJECT_ASSET_CONVERT_TO_X（FBX → X）
//   lParam : 対象のファイル（const std::filesystem::path*。SendMessage の間だけ有効）
constexpr UINT WM_DXS_PROJECT_ASSET_COMMAND = WM_APP + 43;
constexpr WPARAM PROJECT_ASSET_CREATE_MATERIAL = 1;
constexpr WPARAM PROJECT_ASSET_CONVERT_TO_X = 2;

// 単独で動く子ウィンドウを作る。大きさは MoveWindow で変え、親を破棄すると
// パネルも破棄される。ブラウザーは projectRoot の外には出ない。
HWND CreateProjectPanel(HWND parent, HINSTANCE instance, const std::filesystem::path& projectRoot,
                        const std::filesystem::path& engineRoot = {});

// スクリプトを編集するための Visual Studio のプロジェクトを生成する
bool GenerateVisualStudioProject(const std::filesystem::path& projectRoot, const std::filesystem::path& engineRoot,
                                 std::wstring& error);

// Project の右クリックメニューとエディターの Settings メニューで共通のコマンド
void ChooseProjectScriptEditor(HWND panel);       // スクリプトエディターの選択
void UseAutomaticProjectScriptEditor(HWND panel); // スクリプトエディターを自動で選ぶ
void RefreshProjectPanel(HWND panel);             // 表示の更新
void NewProjectComponent(HWND panel);             // 新しいコンポーネントの作成

// project/Scripts に、登録済みの C++ コンポーネントとヘッダーを作る。
// 名前は ASCII の識別子であること。既にあるファイルは置き換えない。
bool CreateProjectComponent(const std::filesystem::path& projectRoot, const std::wstring& name, std::wstring& error);

// ユーザーが選んだファイルを、既存のファイルを置き換えずにプロジェクトへコピーする。
// コピー先がプロジェクトの外に出る場合、リパースポイントを使う場合、コピーに
// 失敗した場合は false。error にはユーザー向けの説明が入る。
bool ImportProjectFile(const std::filesystem::path& projectRoot, const std::filesystem::path& destinationFolder,
                       const std::filesystem::path& source, std::wstring& error);
} // namespace dxs
