//==================================================================================
//
// アセットのサムネイルのヘッダーファイル [AssetThumbnail.h]
//
// Project ウィンドウに出すサムネイルを CPU で描く（GPU やグラフィックス API に依存しない）。
//   ・モデル（.x / .fbx）: 斜め上から見た形を、面の向きで陰影を付けて描く
//   ・マテリアル（.dxmat）: Base Color の球を描く
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include <cstdint>
#include <filesystem>
#include <vector>

namespace dxs {

// サムネイルを作れる種類のファイルか（.x / .fbx / .dxmat）
bool HasAssetThumbnail(const std::filesystem::path& file);

// file のサムネイルを nSize x nSize で描く。
// vPixels には 32bit の BGRA（0xAARRGGBB、上の行から）が入り、背景は透明（A = 0）。
// 読めないファイルや対象外の種類は false。
bool RenderAssetThumbnail(const std::filesystem::path& file, int nSize, std::vector<std::uint32_t>& vPixels);

} // namespace dxs
