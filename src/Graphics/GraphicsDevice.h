//==================================================================================
//
// 描画デバイスのインターフェースのヘッダーファイル [GraphicsDevice.h]
//
// IGraphicsDevice: Renderer が使う最小限の描画命令。
//
// Renderer（ピック・ギズモ・グリッド・アウトライン・描画順）は API に依存せず、
// このインターフェースだけを使う。各バックエンドが下の命令を
// Direct3D 12 / 11 / 9 の呼び出しに変換する。
//
//   UploadVertices（何回でも）->  BeginFrame  ->  { SetPipeline, Draw }（繰り返し）->  EndFrame
//
// シェーダーの決まり（全バックエンド共通。project/Default.hlsl を参照）:
//   VSMain / PSMain、入力は float3 POSITION、定数は row_major float4x4 mvp + float4 color。
//   D3D12 / D3D11 は定数バッファ b0（vs_5_0 / ps_5_0）として渡す。
//   D3D9 は同じ HLSL を vs_3_0 / ps_3_0 でコンパイルし、頂点シェーダーは
//   mvp を c0-c3、ピクセルシェーダーは color を c0 から読む（fxc がこの順で割り当てる）。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "GraphicsApi.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DirectXMath.h>

#include <cstddef>
#include <memory>
#include <string>

namespace dxs::graphics {

//**********************************************************************************
// *** 頂点（位置のみ） ***
//**********************************************************************************
struct Vertex {
    float x, y, z;
};

//**********************************************************************************
// *** 描画状態の組み合わせ（Renderer が使う固定のパイプライン） ***
//**********************************************************************************
enum class PipelineKind : unsigned char {
    SCENE,             // 三角形、プロジェクトのシェーダー、深度テスト + 深度書き込み
    SCENE_WIREFRAME,   // SCENE をワイヤーフレームで描く（シーンビューの描画モード）
    SCENE_TRANSPARENT, // SCENE + アルファブレンド、深度テストあり、深度書き込みなし
    GRID,              // 線、組み込みシェーダー、深度テストあり、深度書き込みなし
    OVERLAY,           // 線、組み込みシェーダー、深度テストなし（アウトライン・ギズモ・範囲選択）
};

//**********************************************************************************
// *** オブジェクトごとの定数（HLSL の定数バッファと同じ並び、80 バイト） ***
//**********************************************************************************
struct ObjectConstants {
    DirectX::XMFLOAT4X4 mvp; // ワールド × ビュー × 射影 行列（行優先）
    DirectX::XMFLOAT4 color; // 描画色（RGBA）
};

static_assert(sizeof(ObjectConstants) == 80, "ObjectConstants must match the HLSL constant buffer");

//**********************************************************************************
// *** デバイス作成時の情報 ***
//**********************************************************************************
struct DeviceCreateInfo {
    HWND hWnd = nullptr;                     // 描画先のウィンドウ
    unsigned uWidth = 0;                     // 描画領域の幅
    unsigned uHeight = 0;                    // 描画領域の高さ
    std::wstring sShaderPath;                // .hlsl のソース、またはコンパイル済みの Default.cso
    const Vertex* pStaticVertices = nullptr; // 最初に送る固定の頂点（立方体・グリッドなど）
    std::size_t nStaticVertexCount = 0;      // 固定の頂点数
    std::size_t nMaxVertexCount = 0;         // 固定 + フレームごとのモデル頂点の最大数
};

//**********************************************************************************
// *** 描画デバイスのインターフェース ***
//**********************************************************************************
class IGraphicsDevice {
public:
    virtual ~IGraphicsDevice() = default;

    [[nodiscard]] virtual GraphicsApi Api() const = 0;                              // どの API か
    virtual bool Initialize(const DeviceCreateInfo& info, std::string& error) = 0;  // 初期化
    virtual bool Resize(unsigned uWidth, unsigned uHeight, std::string& error) = 0; // 描画領域の大きさ変更
    // nFirstVertex 番目から頂点を書き込む（フレームの外でのみ有効）
    virtual bool UploadVertices(std::size_t nFirstVertex, const Vertex* pVertices, std::size_t nCount, std::string& error) = 0;
    virtual bool BeginFrame(const float (&aClearColor)[4], std::string& error) = 0; // フレーム開始（画面クリア）
    virtual void SetPipeline(PipelineKind kind) = 0;                                // 描画状態の切り替え
    virtual void Draw(const ObjectConstants& constants, unsigned uVertexCount, unsigned uFirstVertex) = 0; // 描画
    virtual bool EndFrame(std::string& error) = 0; // フレーム終了（画面に表示）
    virtual void Shutdown() = 0;                   // 解放
};

// 指定した API のデバイスを作る（そのバックエンドがビルドされていない場合のみ nullptr）
std::unique_ptr<IGraphicsDevice> CreateGraphicsDevice(GraphicsApi api);

// 各バックエンドの生成関数（それぞれ別のソースファイルにある）
std::unique_ptr<IGraphicsDevice> CreateDeviceD3D12();
std::unique_ptr<IGraphicsDevice> CreateDeviceD3D11();
std::unique_ptr<IGraphicsDevice> CreateDeviceD3D9();

// エラー文の作成（例: "Create Direct3D 11 device failed (HRESULT 0x887A0004)"）
std::string FormatHResult(const char* pAction, long nResult);

} // namespace dxs::graphics
