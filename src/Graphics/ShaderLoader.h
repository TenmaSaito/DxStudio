//==================================================================================
//
// シェーダー読み込みのヘッダーファイル [ShaderLoader.h]
//
// 全バックエンド共通の HLSL の読み込み・コンパイル。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include <d3dcommon.h>
#include <wrl/client.h>

#include <string>

namespace dxs::graphics {

using Microsoft::WRL::ComPtr;

//**********************************************************************************
// *** 頂点シェーダーとピクセルシェーダーの組 ***
//**********************************************************************************
struct ShaderPair {
    ComPtr<ID3DBlob> pVertexShader; // コンパイル済みの頂点シェーダー
    ComPtr<ID3DBlob> pPixelShader;  // コンパイル済みのピクセルシェーダー
};

//**********************************************************************************
// *** バックエンドごとのシェーダーモデル ***
//**********************************************************************************
struct ShaderProfile {
    const char* pVertexTarget = "vs_5_0";  // 頂点シェーダーのターゲット
    const char* pPixelTarget = "ps_5_0";   // ピクセルシェーダーのターゲット
    // コンパイル済みファイル名の接尾辞: Default.cso -> Default<接尾辞>.vs.cso
    const wchar_t* pCompiledSuffix = L"";
    bool bLegacy = false; // SM3: 後方互換のコンパイルを有効にする
};

inline constexpr ShaderProfile SHADER_PROFILE_SM5{"vs_5_0", "ps_5_0", L"", false}; // D3D12 / D3D11 用
inline constexpr ShaderProfile SHADER_PROFILE_SM3{"vs_3_0", "ps_3_0", L"9", true}; // D3D9 用

// sPath を読み込む。".cso" なら <名前><接尾辞>.vs.cso / .ps.cso を読み、
// それ以外の拡張子は HLSL ソースとして VSMain / PSMain をコンパイルする。
bool LoadShaderPair(const std::wstring& sPath, const ShaderProfile& profile, ShaderPair& shaders, std::string& error);

// メモリ上の HLSL をコンパイルする（組み込みのオーバーレイ用シェーダーと D3D9 の代替）
bool CompileShaderSource(const char* pSource, const ShaderProfile& profile, ShaderPair& shaders, std::string& error);

// エディターの補助線に使う、組み込みの単色シェーダーのソース
extern const char* const BUILTIN_FLAT_SHADER_SOURCE;

} // namespace dxs::graphics
