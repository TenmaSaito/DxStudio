//==================================================================================
//
// 組み込みの基本図形のヘッダーファイル [Primitives.h]
//
// Unity の GameObject > 3D Object / 2D Object に相当する基本図形。
// すべて単位ボックス [-0.5, 0.5] に収まり、<プロジェクト>/Assets/Primitives/ に
// DirectX のテキスト形式 .x ファイルとして書き出される。そのため普通の
// メッシュアセットとして扱える（MeshRenderer で描画、メッシュ欄で X / FBX と
// 差し替え可能、書き出したゲームにも他のアセットと一緒に入る）。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include <DirectXMath.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace dxs {

//**********************************************************************************
// *** 基本図形の種類 ***
//**********************************************************************************
enum class PrimitiveShape : unsigned char {
    CUBE, SPHERE, CAPSULE, CYLINDER, CONE, PYRAMID, PLANE, // 3D
    SQUARE, CIRCLE, TRIANGLE,                             // 2D（XY 平面上、-Z 方向を向く）
};

//**********************************************************************************
// *** 基本図形の情報 ***
//**********************************************************************************
struct PrimitiveInfo {
    PrimitiveShape shape; // 種類
    const char* pName;    // ファイル名（拡張子なし）とメニューの表示名（例: "Sphere"）
    bool bFlat;           // 2D 図形か
};

// 基本図形の一覧（メニューの並び順）
inline constexpr std::array<PrimitiveInfo, 10> PRIMITIVES = {{
    {PrimitiveShape::CUBE, "Cube", false},       {PrimitiveShape::SPHERE, "Sphere", false},
    {PrimitiveShape::CAPSULE, "Capsule", false}, {PrimitiveShape::CYLINDER, "Cylinder", false},
    {PrimitiveShape::CONE, "Cone", false},       {PrimitiveShape::PYRAMID, "Pyramid", false},
    {PrimitiveShape::PLANE, "Plane", false},     {PrimitiveShape::SQUARE, "Square", true},
    {PrimitiveShape::CIRCLE, "Circle", true},    {PrimitiveShape::TRIANGLE, "Triangle", true},
}};

// 基本図形のファイルを置くフォルダー（プロジェクトからの相対パス）
inline constexpr const char* PRIMITIVE_FOLDER = "Assets/Primitives";

// 単位ボックス内の三角形リスト（3 頂点で 1 つの三角形）を作る
std::vector<DirectX::XMFLOAT3> BuildPrimitive(PrimitiveShape shape);
// アセットのパスを返す（例: "Assets/Primitives/Sphere.x"）
std::string PrimitiveAssetPath(PrimitiveShape shape);
// file に DirectX のテキスト形式 .x で書き出す
bool WritePrimitiveFile(const std::filesystem::path& file, PrimitiveShape shape, std::string& error);
// <projectRoot>/Assets/Primitives に無いファイルだけを作る。
// 既にあるファイルは変更しない（ユーザーが編集した形を残すため）。
bool EnsurePrimitiveAssets(const std::filesystem::path& projectRoot, std::string& error);

} // namespace dxs
