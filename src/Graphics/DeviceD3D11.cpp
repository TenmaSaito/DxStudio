//==================================================================================
//
// Direct3D 11 描画デバイスのソースファイル [DeviceD3D11.cpp]
//
// Direct3D 12 と同じ SM5 のシェーダー（プロジェクトのシェーダーと組み込みの
// 補助線用シェーダー）を使う。頂点バッファは DEFAULT（UpdateSubresource で
// 書き換え）、定数バッファは 80 バイトの DYNAMIC を 1 つ。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "GraphicsDevice.h"
#include "ShaderLoader.h"
#include "../OverlayPS.h"
#include "../OverlayVS.h"

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cstring>

namespace dxs::graphics {
namespace {
using Microsoft::WRL::ComPtr;

//**********************************************************************************
// *** 定数宣言 ***
//**********************************************************************************
constexpr DXGI_FORMAT COLOR_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM; // 画面の色の形式
constexpr UINT BUFFER_COUNT = 2;                                 // バックバッファの枚数（フリップモデル時）

//**********************************************************************************
// *** Direct3D 11 描画デバイスクラス ***
//**********************************************************************************
class DeviceD3D11 final : public IGraphicsDevice {
public:
    ~DeviceD3D11() override { Shutdown(); }

    GraphicsApi Api() const override { return GraphicsApi::DIRECT3D11; }

    //==============================================================================
    // --- 初期化処理 ---
    //==============================================================================
    bool Initialize(const DeviceCreateInfo& info, std::string& error) override {
        m_uWidth = info.uWidth;
        m_uHeight = info.uHeight;
        // スワップチェーンの設定（まずフリップモデルで試す）
        DXGI_SWAP_CHAIN_DESC swapDesc{};
        swapDesc.BufferDesc.Width = m_uWidth;
        swapDesc.BufferDesc.Height = m_uHeight;
        swapDesc.BufferDesc.Format = COLOR_FORMAT;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount = BUFFER_COUNT;
        swapDesc.OutputWindow = info.hWnd;
        swapDesc.Windowed = TRUE;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        const D3D_FEATURE_LEVEL aLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        UINT uFlags = 0;
#if defined(_DEBUG)
        uFlags |= D3D11_CREATE_DEVICE_DEBUG; // デバッグビルドはデバッグレイヤーを使う
#endif
        // 失敗したら条件を緩めて作り直す
        HRESULT hr = E_FAIL;
        for (int nAttempt = 0; nAttempt < 2 && FAILED(hr); ++nAttempt) {
            hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, uFlags, aLevels, 2, D3D11_SDK_VERSION,
                                               &swapDesc, &m_pSwapChain, &m_pDevice, nullptr, &m_pContext);
            uFlags &= ~D3D11_CREATE_DEVICE_DEBUG; // 2 回目は SDK のデバッグレイヤー無しで試す
            if (FAILED(hr) && nAttempt == 1) {
                // フリップモデルに対応していない古い環境: 従来の方式で 1 枚
                swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
                swapDesc.BufferCount = 1;
                hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, uFlags, aLevels, 2, D3D11_SDK_VERSION,
                                                   &swapDesc, &m_pSwapChain, &m_pDevice, nullptr, &m_pContext);
            }
        }
        if (FAILED(hr)) return Fail(error, "Create Direct3D 11 device failed", hr);
        m_uBufferCount = swapDesc.BufferCount;
        // Alt+Enter の全画面切り替えを無効化
        ComPtr<IDXGIFactory> pFactory;
        if (SUCCEEDED(m_pSwapChain->GetParent(IID_PPV_ARGS(&pFactory)))) {
            pFactory->MakeWindowAssociation(info.hWnd, DXGI_MWA_NO_ALT_ENTER);
        }
        if (!CreateTargets(error)) return false;
        if (!CreateShaders(info.sShaderPath, error)) return false;
        if (!CreateStates(error)) return false;
        return CreateBuffers(info, error);
    }

    //==============================================================================
    // --- 描画領域の大きさ変更処理 ---
    //==============================================================================
    bool Resize(unsigned uWidth, unsigned uHeight, std::string& error) override {
        if (!m_pSwapChain) return true;
        // バックバッファへの参照をすべて外してから大きさを変える
        m_pContext->OMSetRenderTargets(0, nullptr, nullptr);
        m_pRenderTarget.Reset();
        m_pDepthView.Reset();
        m_pDepth.Reset();
        const HRESULT hr = m_pSwapChain->ResizeBuffers(m_uBufferCount, uWidth, uHeight, COLOR_FORMAT, 0);
        if (FAILED(hr)) return Fail(error, "Resize swap chain failed", hr);
        m_uWidth = uWidth;
        m_uHeight = uHeight;
        return CreateTargets(error);
    }

    //==============================================================================
    // --- 頂点の書き込み処理 ---
    //==============================================================================
    bool UploadVertices(std::size_t nFirstVertex, const Vertex* pVertices, std::size_t nCount, std::string&) override {
        if (!nCount) return true;
        // 書き込む範囲（バイト単位）だけを更新する
        const D3D11_BOX box{static_cast<UINT>(nFirstVertex * sizeof(Vertex)), 0, 0,
                            static_cast<UINT>((nFirstVertex + nCount) * sizeof(Vertex)), 1, 1};
        m_pContext->UpdateSubresource(m_pVertexBuffer.Get(), 0, &box, pVertices, 0, 0);
        return true;
    }

    //==============================================================================
    // --- フレーム開始処理 ---
    //==============================================================================
    bool BeginFrame(const float (&aClearColor)[4], std::string&) override {
        // 描画先の設定とクリア
        ID3D11RenderTargetView* apTargets[] = {m_pRenderTarget.Get()};
        m_pContext->OMSetRenderTargets(1, apTargets, m_pDepthView.Get());
        m_pContext->ClearRenderTargetView(m_pRenderTarget.Get(), aClearColor);
        m_pContext->ClearDepthStencilView(m_pDepthView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        // ビューポート・描画状態・頂点バッファ・定数バッファ
        const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(m_uWidth), static_cast<float>(m_uHeight), 0, 1};
        m_pContext->RSSetViewports(1, &viewport);
        m_pContext->RSSetState(m_pRasterizer.Get());
        m_pContext->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        const UINT uStride = sizeof(Vertex), uOffset = 0;
        ID3D11Buffer* apVertexBuffers[] = {m_pVertexBuffer.Get()};
        m_pContext->IASetVertexBuffers(0, 1, apVertexBuffers, &uStride, &uOffset);
        m_pContext->IASetInputLayout(m_pInputLayout.Get());
        ID3D11Buffer* apConstants[] = {m_pConstantBuffer.Get()};
        m_pContext->VSSetConstantBuffers(0, 1, apConstants);
        m_pContext->PSSetConstantBuffers(0, 1, apConstants);
        SetPipeline(PipelineKind::SCENE);
        return true;
    }

    //==============================================================================
    // --- 描画状態の切り替え処理 ---
    //==============================================================================
    void SetPipeline(PipelineKind kind) override {
        const bool bScene = kind == PipelineKind::SCENE || kind == PipelineKind::SCENE_WIREFRAME || kind == PipelineKind::SCENE_TRANSPARENT;
        const bool bTransparent = kind == PipelineKind::SCENE_TRANSPARENT;
        m_pContext->OMSetBlendState(bTransparent ? m_pAlphaBlend.Get() : nullptr, nullptr, 0xFFFFFFFF); // 半透明だけブレンド
        m_pContext->RSSetState(kind == PipelineKind::SCENE_WIREFRAME ? m_pWireframeRasterizer.Get() : m_pRasterizer.Get());
        // シーンはプロジェクトのシェーダー、それ以外は組み込みの補助線用シェーダー
        m_pContext->VSSetShader(bScene ? m_pSceneVertexShader.Get() : m_pOverlayVertexShader.Get(), nullptr, 0);
        m_pContext->PSSetShader(bScene ? m_pScenePixelShader.Get() : m_pOverlayPixelShader.Get(), nullptr, 0);
        // 半透明: 深度テストはするが書き込まない（グリッドと同じ状態）
        ID3D11DepthStencilState* pDepth = bTransparent ? m_pDepthGrid.Get() : bScene ? m_pDepthScene.Get() : kind == PipelineKind::GRID ? m_pDepthGrid.Get()
                                                                                                    : m_pDepthOverlay.Get();
        m_pContext->OMSetDepthStencilState(pDepth, 0);
        m_pContext->IASetPrimitiveTopology(bScene ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST : D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    }

    //==============================================================================
    // --- 描画処理 ---
    //==============================================================================
    void Draw(const ObjectConstants& constants, unsigned uVertexCount, unsigned uFirstVertex) override {
        // 定数バッファを丸ごと書き換えてから描く（WRITE_DISCARD で前の内容は捨てる）
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(m_pContext->Map(m_pConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &constants, sizeof(constants));
            m_pContext->Unmap(m_pConstantBuffer.Get(), 0);
        }
        m_pContext->Draw(uVertexCount, uFirstVertex);
    }

    //==============================================================================
    // --- フレーム終了処理（垂直同期ありで表示） ---
    //==============================================================================
    bool EndFrame(std::string& error) override {
        const HRESULT hr = m_pSwapChain->Present(1, 0);
        if (FAILED(hr)) return Fail(error, "Present failed", hr);
        return true;
    }

    //==============================================================================
    // --- 解放処理 ---
    //==============================================================================
    void Shutdown() override {
        if (m_pContext) { // 設定済みの状態を外し、溜まっている命令を流す
            m_pContext->ClearState();
            m_pContext->Flush();
        }
        m_pConstantBuffer.Reset();
        m_pVertexBuffer.Reset();
        m_pInputLayout.Reset();
        m_pSceneVertexShader.Reset();
        m_pScenePixelShader.Reset();
        m_pOverlayVertexShader.Reset();
        m_pOverlayPixelShader.Reset();
        m_pRasterizer.Reset();
        m_pWireframeRasterizer.Reset();
        m_pAlphaBlend.Reset();
        m_pDepthScene.Reset();
        m_pDepthGrid.Reset();
        m_pDepthOverlay.Reset();
        m_pDepthView.Reset();
        m_pDepth.Reset();
        m_pRenderTarget.Reset();
        m_pSwapChain.Reset();
        m_pContext.Reset();
        m_pDevice.Reset();
    }

private:
    // --- エラー文を設定して false を返す処理 ---
    static bool Fail(std::string& error, const char* pAction, HRESULT hr) {
        error = FormatHResult(pAction, hr);
        return false;
    }

    //==============================================================================
    // --- 描画先（バックバッファ + 深度バッファ）の作成処理 ---
    //==============================================================================
    bool CreateTargets(std::string& error) {
        ComPtr<ID3D11Texture2D> pBackBuffer;
        HRESULT hr = m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
        if (FAILED(hr)) return Fail(error, "Get swap-chain buffer failed", hr);
        hr = m_pDevice->CreateRenderTargetView(pBackBuffer.Get(), nullptr, &m_pRenderTarget);
        if (FAILED(hr)) return Fail(error, "Create render target view failed", hr);
        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = m_uWidth;
        depthDesc.Height = m_uHeight;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        hr = m_pDevice->CreateTexture2D(&depthDesc, nullptr, &m_pDepth);
        if (FAILED(hr)) return Fail(error, "Create depth buffer failed", hr);
        hr = m_pDevice->CreateDepthStencilView(m_pDepth.Get(), nullptr, &m_pDepthView);
        if (FAILED(hr)) return Fail(error, "Create depth view failed", hr);
        return true;
    }

    //==============================================================================
    // --- シェーダーと入力レイアウトの作成処理 ---
    //==============================================================================
    bool CreateShaders(const std::wstring& sShaderPath, std::string& error) {
        // プロジェクトのシェーダー
        ShaderPair shaders;
        if (!LoadShaderPair(sShaderPath, SHADER_PROFILE_SM5, shaders, error)) return false;
        auto* pVs = shaders.pVertexShader.Get();
        auto* pPs = shaders.pPixelShader.Get();
        HRESULT hr = m_pDevice->CreateVertexShader(pVs->GetBufferPointer(), pVs->GetBufferSize(), nullptr, &m_pSceneVertexShader);
        if (FAILED(hr)) return Fail(error, "Create vertex shader failed", hr);
        hr = m_pDevice->CreatePixelShader(pPs->GetBufferPointer(), pPs->GetBufferSize(), nullptr, &m_pScenePixelShader);
        if (FAILED(hr)) return Fail(error, "Create pixel shader failed", hr);
        // 組み込みの補助線用シェーダー（コンパイル済みのバイトコード）
        hr = m_pDevice->CreateVertexShader(g_overlayVS, sizeof(g_overlayVS), nullptr, &m_pOverlayVertexShader);
        if (FAILED(hr)) return Fail(error, "Create overlay vertex shader failed", hr);
        hr = m_pDevice->CreatePixelShader(g_overlayPS, sizeof(g_overlayPS), nullptr, &m_pOverlayPixelShader);
        if (FAILED(hr)) return Fail(error, "Create overlay pixel shader failed", hr);
        // 入力は float3 POSITION だけ
        const D3D11_INPUT_ELEMENT_DESC aInput[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        hr = m_pDevice->CreateInputLayout(aInput, 1, pVs->GetBufferPointer(), pVs->GetBufferSize(), &m_pInputLayout);
        if (FAILED(hr)) return Fail(error, "Create input layout failed", hr);
        return true;
    }

    //==============================================================================
    // --- 描画状態オブジェクトの作成処理 ---
    //==============================================================================
    bool CreateStates(std::string& error) {
        // ラスタライザー: 両面描画（塗りつぶし / ワイヤーフレーム）
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        HRESULT hr = m_pDevice->CreateRasterizerState(&raster, &m_pRasterizer);
        if (FAILED(hr)) return Fail(error, "Create rasterizer state failed", hr);
        raster.FillMode = D3D11_FILL_WIREFRAME;
        hr = m_pDevice->CreateRasterizerState(&raster, &m_pWireframeRasterizer);
        if (FAILED(hr)) return Fail(error, "Create wireframe rasterizer state failed", hr);
        // 深度: シーン（テスト + 書き込み）/ グリッド（テストのみ）/ オーバーレイ（なし）
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        hr = m_pDevice->CreateDepthStencilState(&depth, &m_pDepthScene);
        if (FAILED(hr)) return Fail(error, "Create depth state failed", hr);
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        hr = m_pDevice->CreateDepthStencilState(&depth, &m_pDepthGrid);
        if (FAILED(hr)) return Fail(error, "Create grid depth state failed", hr);
        depth.DepthEnable = FALSE;
        hr = m_pDevice->CreateDepthStencilState(&depth, &m_pDepthOverlay);
        if (FAILED(hr)) return Fail(error, "Create overlay depth state failed", hr);
        // ブレンド: 半透明用の一般的なアルファブレンド
        D3D11_BLEND_DESC blend{};
        auto& target = blend.RenderTarget[0];
        target.BlendEnable = TRUE;
        target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        target.BlendOp = D3D11_BLEND_OP_ADD;
        target.SrcBlendAlpha = D3D11_BLEND_ONE;
        target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = m_pDevice->CreateBlendState(&blend, &m_pAlphaBlend);
        if (FAILED(hr)) return Fail(error, "Create alpha blend state failed", hr);
        return true;
    }

    //==============================================================================
    // --- 頂点バッファと定数バッファの作成処理 ---
    //==============================================================================
    bool CreateBuffers(const DeviceCreateInfo& info, std::string& error) {
        // 頂点バッファ（GPU 側、UpdateSubresource で書き換える）
        D3D11_BUFFER_DESC vertexDesc{};
        vertexDesc.ByteWidth = static_cast<UINT>(info.nMaxVertexCount * sizeof(Vertex));
        vertexDesc.Usage = D3D11_USAGE_DEFAULT;
        vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        HRESULT hr = m_pDevice->CreateBuffer(&vertexDesc, nullptr, &m_pVertexBuffer);
        if (FAILED(hr)) return Fail(error, "Create vertex buffer failed", hr);
        if (!UploadVertices(0, info.pStaticVertices, info.nStaticVertexCount, error)) return false; // 固定の頂点を先頭に書く
        // 定数バッファ（CPU から毎回書き換える DYNAMIC）
        D3D11_BUFFER_DESC constantDesc{};
        constantDesc.ByteWidth = sizeof(ObjectConstants);
        constantDesc.Usage = D3D11_USAGE_DYNAMIC;
        constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constantDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = m_pDevice->CreateBuffer(&constantDesc, nullptr, &m_pConstantBuffer);
        if (FAILED(hr)) return Fail(error, "Create constant buffer failed", hr);
        return true;
    }

    unsigned m_uWidth = 0;                                 // 描画領域の幅
    unsigned m_uHeight = 0;                                // 描画領域の高さ
    UINT m_uBufferCount = BUFFER_COUNT;                    // 実際のバックバッファ枚数
    ComPtr<ID3D11Device> m_pDevice;                        // デバイス
    ComPtr<ID3D11DeviceContext> m_pContext;                // デバイスコンテキスト（描画命令）
    ComPtr<IDXGISwapChain> m_pSwapChain;                   // スワップチェーン
    ComPtr<ID3D11RenderTargetView> m_pRenderTarget;        // バックバッファのビュー
    ComPtr<ID3D11Texture2D> m_pDepth;                      // 深度バッファ
    ComPtr<ID3D11DepthStencilView> m_pDepthView;           // 深度バッファのビュー
    ComPtr<ID3D11VertexShader> m_pSceneVertexShader;       // シーン用頂点シェーダー
    ComPtr<ID3D11PixelShader> m_pScenePixelShader;         // シーン用ピクセルシェーダー
    ComPtr<ID3D11VertexShader> m_pOverlayVertexShader;     // 補助線用頂点シェーダー
    ComPtr<ID3D11PixelShader> m_pOverlayPixelShader;       // 補助線用ピクセルシェーダー
    ComPtr<ID3D11InputLayout> m_pInputLayout;              // 入力レイアウト
    ComPtr<ID3D11RasterizerState> m_pRasterizer;           // 塗りつぶし
    ComPtr<ID3D11RasterizerState> m_pWireframeRasterizer;  // ワイヤーフレーム
    ComPtr<ID3D11BlendState> m_pAlphaBlend;                // 半透明用ブレンド
    ComPtr<ID3D11DepthStencilState> m_pDepthScene;         // 深度: テスト + 書き込み
    ComPtr<ID3D11DepthStencilState> m_pDepthGrid;          // 深度: テストのみ
    ComPtr<ID3D11DepthStencilState> m_pDepthOverlay;       // 深度: なし
    ComPtr<ID3D11Buffer> m_pVertexBuffer;                  // 頂点バッファ
    ComPtr<ID3D11Buffer> m_pConstantBuffer;                // 定数バッファ
};
} // namespace

//==================================================================================
// --- Direct3D 11 デバイスの生成処理 ---
//==================================================================================
std::unique_ptr<IGraphicsDevice> CreateDeviceD3D11() { return std::make_unique<DeviceD3D11>(); }

} // namespace dxs::graphics
