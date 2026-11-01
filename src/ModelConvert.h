//==================================================================================
//
// モデルの変換（X ⇔ マテリアル / FBX → X）のヘッダーファイル [ModelConvert.h]
//
// ・X ファイルの Material（Direct3D 9 の形式）を読み取り、Direct3D 11 / 12 でも
//   使えるマテリアルファイル（.dxmat）を作る。
// ・FBX ファイルを、DirectX のテキスト形式の X ファイルに変換する
//   （頂点・法線・UV・マテリアルを書き出す）。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include <DirectXMath.h>

#include <filesystem>
#include <string>
#include <vector>

namespace dxs {

//**********************************************************************************
// *** X ファイルのマテリアル 1 つ分 ***
//**********************************************************************************
struct XMaterialInfo {
    std::string sName;                                 // マテリアルの名前（無ければ空）
    DirectX::XMFLOAT4 diffuse{1.0f, 1.0f, 1.0f, 1.0f}; // 拡散色（RGBA）
    float fPower = 0.0f;                               // 鏡面反射の強さ
    DirectX::XMFLOAT3 specular{0.0f, 0.0f, 0.0f};      // 鏡面反射色
    DirectX::XMFLOAT3 emissive{0.0f, 0.0f, 0.0f};      // 発光色
    std::string sTexture;                              // TextureFilename（X ファイルからの相対パス）
};

//**********************************************************************************
// *** 関数 ***
//**********************************************************************************
// X ファイル（テキスト形式）の Material をすべて読み取る。Material が無い場合は
// 空の配列で true を返す。バイナリ形式の X ファイルは失敗する。
bool ReadXMaterials(const std::filesystem::path& xFile, std::vector<XMaterialInfo>& vOut, std::string& error);

// sText の位置 nWord にある "Material [名前] { ... }" を 1 つ読む（テンプレートの宣言や
// "MeshMaterialList" の一部なら false）。nNext には読み終えた位置が入る。
bool ParseXMaterial(const std::string& sText, std::size_t nWord, XMaterialInfo& material, std::size_t& nNext);

// X ファイルのすべての Material を、1 つの .dxmat（<名前>.dxmat、X ファイルと同じフォルダー）に
// 面ごとの色（スロット）としてまとめて作る。スロットの順はエンジンが面に割り当てる順と同じ。
//   ・同じ名前のファイルがあれば _1、_2 ... を付け、既存のファイルは上書きしない
//   ・vCreatedAssets には作ったファイル（1 つ）の projectRoot からの相対パス（'/' 区切り）が入る
bool CreateMaterialsFromX(const std::filesystem::path& projectRoot, const std::filesystem::path& xFile,
                          std::vector<std::string>& vCreatedAssets, std::string& error);

// FBX ファイルを X ファイル（テキスト形式）に変換する。xFile が既にあれば置き換える。
// 座標はエンジンが FBX を読むときと同じ値のまま書くので、変換前と同じ見た目になる。
bool ConvertFbxToX(const std::filesystem::path& fbxFile, const std::filesystem::path& xFile, std::string& error);

// 既に存在しないファイル名を返す（<stem><extension>、<stem>_1<extension>、...）
std::filesystem::path UniqueFilePath(const std::filesystem::path& folder, const std::wstring& sStem,
                                     const std::wstring& sExtension);

} // namespace dxs
