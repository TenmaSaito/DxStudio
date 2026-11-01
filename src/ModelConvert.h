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
    std::string sName;                           // マテリアルの名前（無ければ空）
    DirectX::XMFLOAT4 diffuse{1.0f, 1.0f, 1.0f, 1.0f}; // 拡散色（RGBA）
    float fPower = 0.0f;                         // 鏡面反射の強さ
    DirectX::XMFLOAT3 specular{0.0f, 0.0f, 0.0f}; // 鏡面反射色
    DirectX::XMFLOAT3 emissive{0.0f, 0.0f, 0.0f}; // 発光色
    std::string sTexture;                        // TextureFilename（X ファイルからの相対パス）
};

//**********************************************************************************
// *** 関数 ***
//**********************************************************************************
// X ファイル（テキスト形式）の Material をすべて読み取る。Material が無い場合は
// 空の配列で true を返す。バイナリ形式の X ファイルは失敗する。
bool ReadXMaterials(const std::filesystem::path& xFile, std::vector<XMaterialInfo>& vOut, std::string& error);

// X ファイルの Material から .dxmat を作る（X ファイルと同じフォルダー）。
//   ・Material が 1 つ（または無い）なら <名前>.dxmat、複数なら <名前>_<マテリアル名>.dxmat
//   ・同じ名前のファイルがあれば _1、_2 ... を付け、既存のファイルは上書きしない
//   ・vCreatedAssets には projectRoot からの相対パス（'/' 区切り）が入る
bool CreateMaterialsFromX(const std::filesystem::path& projectRoot, const std::filesystem::path& xFile,
                          std::vector<std::string>& vCreatedAssets, std::string& error);

// FBX ファイルを X ファイル（テキスト形式）に変換する。xFile が既にあれば置き換える。
// 座標はエンジンが FBX を読むときと同じ値のまま書くので、変換前と同じ見た目になる。
bool ConvertFbxToX(const std::filesystem::path& fbxFile, const std::filesystem::path& xFile, std::string& error);

// 既に存在しないファイル名を返す（<stem><extension>、<stem>_1<extension>、...）
std::filesystem::path UniqueFilePath(const std::filesystem::path& folder, const std::wstring& sStem,
                                     const std::wstring& sExtension);

} // namespace dxs
