//==================================================================================
//
// Direct3D 12 描画デバイスのソースファイル [DeviceD3D12.cpp]
//
// 元の DxStudio のレンダラーと同じ動作:
//   ・フリップモデルのバックバッファ 2 枚
//   ・頂点バッファはアップロードヒープ上に 1 つ
//   ・ObjectConstants はルート定数（32bit × 20 個）で渡す
//   ・Present の後は毎回 GPU の完了を待つ（単純さ優先）
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "GraphicsDevice.h"
#include "ShaderLoader.h"
#include "../OverlayPS.h"
#include "../OverlayVS.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <climits>
#include <cstring>

namespace dxs::graphics {
namespace {
using Microsoft::WRL::ComPtr;

//**********************************************************************************
// *** 定数宣言 ***
//**********************************************************************************
constexpr UINT BUFFER_COUNT = 2;                                 // バックバッファの枚数
constexpr DXGI_FORMAT COLOR_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM; // 画面の色の形式
constexpr DXGI_FORMAT DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;      // 深度バッファの形式
constexpr UINT CONSTANT_COUNT = sizeof(ObjectConstants) / 4;     // ルート定数の個数（32bit 単位）

//==================================================================================
// --- リソースの状態遷移バリアの作成処理 ---
//==================================================================================
D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* pResource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = pResource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = from;
    barrier.Transition.StateAfter = to;
    return barrier;
}

//**********************************************************************************
// *** Direct3D 12 描画デバイスクラス ***
//**********************************************************************************
class DeviceD3D12 final : public IGraphicsDevice {
public:
    ~DeviceD3D12() override { Shutdown(); }

    GraphicsApi Api() const override { return GraphicsApi::DIRECT3D12; }

    //==============================================================================
    // --- 初期化処理 ---
    //==============================================================================
    bool Initialize(const DeviceCreateInfo& info, std::string& error) override {
        m_uWidth = info.uWidth;
        m_uHeight = info.uHeight;

        // DXGI ファクトリ → デバイス → コマンドキュー
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_pFactory));
        if (FAILED(hr)) return Fail(error, "Create DXGI factory failed", hr);
        hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice));
        if (FAILED(hr)) return Fail(error, "Create Direct3D 12 device failed", hr);
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        hr = m_pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_pQueue));
        if (FAILED(hr)) return Fail(error, "Create command queue failed", hr);

        // スワップチェーン（フリップモデル、バックバッファ 2 枚）
        DXGI_SWAP_CHAIN_DESC1 swapDesc{};
        swapDesc.Width = m_uWidth;
        swapDesc.Height = m_uHeight;
        swapDesc.Format = COLOR_FORMAT;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount = BUFFER_COUNT;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> pBaseSwapChain;
        hr = m_pFactory->CreateSwapChainForHwnd(m_pQueue.Get(), info.hWnd, &swapDesc, nullptr, nullptr, &pBaseSwapChain);
        if (FAILED(hr)) return Fail(error, "Create swap chain failed", hr);
        hr = pBaseSwapChain.As(&m_pSwapChain); // 現在のバックバッファ番号が取れる版へ
        if (FAILED(hr)) return Fail(error, "Query swap chain failed", hr);
        m_pFactory->MakeWindowAssociation(info.hWnd, DXGI_MWA_NO_ALT_ENTER); // Alt+Enter の全画面切り替えを無効化
        m_uFrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

        // レンダーターゲット用と深度用のディスクリプタヒープ
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = BUFFER_COUNT;
        hr = m_pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_pRtvHeap));
        if (FAILED(hr)) return Fail(error, "Create RTV heap failed", hr);
        m_uRtvStride = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        heapDesc.NumDescriptors = 1;
        hr = m_pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_pDsvHeap));
        if (FAILED(hr)) return Fail(error, "Create DSV heap failed", hr);
        if (!CreateTargets(error)) return false;
        hr = m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_pAllocator));
        if (FAILED(hr)) return Fail(error, "Create command allocator failed", hr);

        // パイプライン → コマンドリスト（作成直後は記録中なので閉じておく）→ 頂点バッファ
        if (!CreatePipelines(info.sShaderPath, error)) return false;
        hr = m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pAllocator.Get(), m_pScenePipeline.Get(),
                                          IID_PPV_ARGS(&m_pCommands));
        if (FAILED(hr)) return Fail(error, "Create command list failed", hr);
        hr = m_pCommands->Close();
        if (FAILED(hr)) return Fail(error, "Close command list failed", hr);
        if (!CreateVertexBuffer(info, error)) return false;

        // GPU の完了待ちに使うフェンスとイベント
        hr = m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence));
        if (FAILED(hr)) return Fail(error, "Create fence failed", hr);
        m_hFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_hFenceEvent) {
            error = "Create fence event failed";
            return false;
        }

        return true;
    }

    //==============================================================================
    // --- 描画領域の大きさ変更処理 ---
    //==============================================================================
    bool Resize(unsigned uWidth, unsigned uHeight, std::string& error) override {
        if (!m_pSwapChain) return true;
        if (!WaitForGpu(error)) return false; // 使用中のバッファは作り直せないので完了を待つ
        for (auto& pTarget : m_apTargets) {
            pTarget.Reset();
        }

        m_pDepth.Reset();
        const HRESULT hr = m_pSwapChain->ResizeBuffers(BUFFER_COUNT, uWidth, uHeight, COLOR_FORMAT, 0);
        if (FAILED(hr)) return Fail(error, "Resize swap chain failed", hr);
        m_uWidth = uWidth;
        m_uHeight = uHeight;
        m_uFrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();
        return CreateTargets(error);
    }

    //==============================================================================
    // --- 頂点の書き込み処理 ---
    //==============================================================================
    bool UploadVertices(std::size_t nFirstVertex, const Vertex* pVertices, std::size_t nCount, std::string& error) override {
        if (!nCount) return true;
        void* pMapped = nullptr;
        const D3D12_RANGE noRead{0, 0}; // CPU からは読まない
        const HRESULT hr = m_pVertexBuffer->Map(0, &noRead, &pMapped);
        if (FAILED(hr)) return Fail(error, "Map model vertex buffer failed", hr);
        std::memcpy(static_cast<Vertex*>(pMapped) + nFirstVertex, pVertices, nCount * sizeof(Vertex));
        m_pVertexBuffer->Unmap(0, nullptr);
        return true;
    }

    //==============================================================================
    // --- フレーム開始処理 ---
    //==============================================================================
    bool BeginFrame(const float (&aClearColor)[4], std::string& error) override {
        // コマンドの記録を始める（前フレームの完了は EndFrame で待っている）
        HRESULT hr = m_pAllocator->Reset();
        if (FAILED(hr)) return Fail(error, "Reset command allocator failed", hr);
        hr = m_pCommands->Reset(m_pAllocator.Get(), m_pScenePipeline.Get());
        if (FAILED(hr)) return Fail(error, "Reset command list failed", hr);

        // バックバッファを「表示用」→「描画先」へ
        const auto barrier =
            Transition(m_apTargets[m_uFrameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_pCommands->ResourceBarrier(1, &barrier);

        // 描画先の設定とクリア
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_pRtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(m_uFrameIndex) * m_uRtvStride;
        const auto dsv = m_pDsvHeap->GetCPUDescriptorHandleForHeapStart();
        m_pCommands->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        m_pCommands->ClearRenderTargetView(rtv, aClearColor, 0, nullptr);
        m_pCommands->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        // ビューポート・ルートシグネチャ・頂点バッファ
        const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(m_uWidth), static_cast<float>(m_uHeight), 0, 1};
        const D3D12_RECT scissor{0, 0, static_cast<LONG>(m_uWidth), static_cast<LONG>(m_uHeight)};
        m_pCommands->RSSetViewports(1, &viewport);
        m_pCommands->RSSetScissorRects(1, &scissor);
        m_pCommands->SetGraphicsRootSignature(m_pRootSignature.Get());
        m_pCommands->IASetVertexBuffers(0, 1, &m_vertexView);
        SetPipeline(PipelineKind::SCENE);
        return true;
    }

    //==============================================================================
    // --- 描画状態の切り替え処理 ---
    //==============================================================================
    void SetPipeline(PipelineKind kind) override {
        ID3D12PipelineState* pState = kind == PipelineKind::SCENE               ? m_pScenePipeline.Get()
                                      : kind == PipelineKind::SCENE_WIREFRAME   ? m_pWireframePipeline.Get()
                                      : kind == PipelineKind::SCENE_TRANSPARENT ? m_pTransparentPipeline.Get()
                                      : kind == PipelineKind::GRID              ? m_pGridPipeline.Get()
                                                                                : m_pOverlayPipeline.Get();
        m_pCommands->SetPipelineState(pState);

        // シーンは三角形、グリッドと補助線は線
        const bool bTriangles =
            kind == PipelineKind::SCENE || kind == PipelineKind::SCENE_WIREFRAME || kind == PipelineKind::SCENE_TRANSPARENT;
        m_pCommands->IASetPrimitiveTopology(bTriangles ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST : D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    }

    //==============================================================================
    // --- 描画処理 ---
    //==============================================================================
    void Draw(const ObjectConstants& constants, unsigned uVertexCount, unsigned uFirstVertex) override {
        m_pCommands->SetGraphicsRoot32BitConstants(0, CONSTANT_COUNT, &constants, 0); // 定数を直接コマンドに積む
        m_pCommands->DrawInstanced(uVertexCount, 1, uFirstVertex, 0);
    }

    //==============================================================================
    // --- フレーム終了処理 ---
    //==============================================================================
    bool EndFrame(std::string& error) override {
        // バックバッファを「描画先」→「表示用」へ戻して記録を閉じる
        const auto barrier =
            Transition(m_apTargets[m_uFrameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        m_pCommands->ResourceBarrier(1, &barrier);
        HRESULT hr = m_pCommands->Close();
        if (FAILED(hr)) return Fail(error, "Close command list failed", hr);

        // 実行 → 表示（垂直同期あり）→ 完了待ち
        ID3D12CommandList* apLists[] = {m_pCommands.Get()};
        m_pQueue->ExecuteCommandLists(1, apLists);
        hr = m_pSwapChain->Present(1, 0);
        if (FAILED(hr)) return Fail(error, "Present failed", hr);
        if (!WaitForGpu(error)) return false;
        m_uFrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();
        return true;
    }

    //==============================================================================
    // --- 解放処理 ---
    //==============================================================================
    void Shutdown() override {
        std::string ignored;
        WaitForGpu(ignored); // GPU が使っている間は解放しない
        if (m_hFenceEvent) {
            CloseHandle(m_hFenceEvent);
        }

        m_hFenceEvent = nullptr;
        m_pCommands.Reset();
        m_pAllocator.Reset();
        m_pFence.Reset();
        m_pVertexBuffer.Reset();
        m_pScenePipeline.Reset();
        m_pWireframePipeline.Reset();
        m_pTransparentPipeline.Reset();
        m_pGridPipeline.Reset();
        m_pOverlayPipeline.Reset();
        m_pRootSignature.Reset();
        m_pDepth.Reset();
        for (auto& pTarget : m_apTargets) {
            pTarget.Reset();
        }

        m_pRtvHeap.Reset();
        m_pDsvHeap.Reset();
        m_pSwapChain.Reset();
        m_pQueue.Reset();
        m_pDevice.Reset();
        m_pFactory.Reset();
    }

private:
    // --- エラー文を設定して false を返す処理 ---
    static bool Fail(std::string& error, const char* pAction, HRESULT hr) {
        error = FormatHResult(pAction, hr);
        return false;
    }

    //==============================================================================
    // --- GPU の完了待ち処理 ---
    //==============================================================================
    bool WaitForGpu(std::string& error) {
        if (!m_pQueue || !m_pFence || !m_hFenceEvent) return true; // 初期化前
        const UINT64 nValue = ++m_nFenceValue;
        HRESULT hr = m_pQueue->Signal(m_pFence.Get(), nValue); // キューの最後に「この値にする」命令を積む
        if (FAILED(hr)) return Fail(error, "Command queue signal failed", hr);
        if (m_pFence->GetCompletedValue() < nValue) { // まだ届いていなければイベントで待つ
            hr = m_pFence->SetEventOnCompletion(nValue, m_hFenceEvent);
            if (FAILED(hr)) return Fail(error, "Fence event failed", hr);
            if (WaitForSingleObject(m_hFenceEvent, INFINITE) != WAIT_OBJECT_0) {
                error = "Waiting for the GPU failed";
                return false;
            }
        }

        return true;
    }

    //==============================================================================
    // --- 描画先（バックバッファ + 深度バッファ）の作成処理 ---
    //==============================================================================
    bool CreateTargets(std::string& error) {
        // バックバッファごとのレンダーターゲットビュー
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_pRtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT uIndex = 0; uIndex < BUFFER_COUNT; ++uIndex) {
            const HRESULT hr = m_pSwapChain->GetBuffer(uIndex, IID_PPV_ARGS(&m_apTargets[uIndex]));
            if (FAILED(hr)) return Fail(error, "Get swap-chain buffer failed", hr);
            m_pDevice->CreateRenderTargetView(m_apTargets[uIndex].Get(), nullptr, handle);
            handle.ptr += m_uRtvStride;
        }

        // 深度バッファ（GPU 専用のメモリ）
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = m_uWidth;
        desc.Height = m_uHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DEPTH_FORMAT;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{}; // クリア値を指定すると高速にクリアできる
        clear.Format = DEPTH_FORMAT;
        clear.DepthStencil.Depth = 1.0f;
        const HRESULT hr = m_pDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                              D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&m_pDepth));
        if (FAILED(hr)) return Fail(error, "Create depth buffer failed", hr);
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DEPTH_FORMAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        m_pDevice->CreateDepthStencilView(m_pDepth.Get(), &dsv, m_pDsvHeap->GetCPUDescriptorHandleForHeapStart());
        return true;
    }

    //==============================================================================
    // --- ルートシグネチャとパイプラインの作成処理 ---
    //==============================================================================
    bool CreatePipelines(const std::wstring& sShaderPath, std::string& error) {
        // ルートシグネチャ: b0 に 32bit 定数を CONSTANT_COUNT 個（= ObjectConstants）
        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter.Constants.ShaderRegister = 0;
        parameter.Constants.Num32BitValues = CONSTANT_COUNT;
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = 1;
        rootDesc.pParameters = &parameter;
        rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> pSignature, pDiagnostics;
        HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pSignature, &pDiagnostics);
        if (FAILED(hr)) {
            error = pDiagnostics
                        ? std::string(static_cast<const char*>(pDiagnostics->GetBufferPointer()), pDiagnostics->GetBufferSize())
                        : FormatHResult("Serialize root signature failed", hr);
            return false;
        }

        hr = m_pDevice->CreateRootSignature(0, pSignature->GetBufferPointer(), pSignature->GetBufferSize(),
                                            IID_PPV_ARGS(&m_pRootSignature));
        if (FAILED(hr)) return Fail(error, "Create root signature failed", hr);

        // プロジェクトのシェーダー（シーンのオブジェクト用）
        ShaderPair shaders;
        if (!LoadShaderPair(sShaderPath, SHADER_PROFILE_SM5, shaders, error)) return false;

        // シーン用パイプライン: 三角形・両面描画・不透明・深度テスト + 書き込み
        const D3D12_INPUT_ELEMENT_DESC aInput[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};
        state.pRootSignature = m_pRootSignature.Get();
        state.VS = {shaders.pVertexShader->GetBufferPointer(), shaders.pVertexShader->GetBufferSize()};
        state.PS = {shaders.pPixelShader->GetBufferPointer(), shaders.pPixelShader->GetBufferSize()};
        state.InputLayout = {aInput, 1};
        state.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        state.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        state.RasterizerState.DepthClipEnable = TRUE;
        auto& blend = state.BlendState.RenderTarget[0];
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        blend.SrcBlend = D3D12_BLEND_ONE;
        blend.DestBlend = D3D12_BLEND_ZERO;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        auto& depth = state.DepthStencilState;
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        depth.StencilEnable = FALSE;
        depth.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        depth.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        depth.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
        depth.BackFace = depth.FrontFace;
        state.SampleMask = UINT_MAX;
        state.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        state.NumRenderTargets = 1;
        state.RTVFormats[0] = COLOR_FORMAT;
        state.DSVFormat = DEPTH_FORMAT;
        state.SampleDesc.Count = 1;
        hr = m_pDevice->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&m_pScenePipeline));
        if (FAILED(hr)) return Fail(error, "Create graphics pipeline failed", hr);

        // ワイヤーフレーム版（塗りつぶしだけ変更）
        state.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        hr = m_pDevice->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&m_pWireframePipeline));
        if (FAILED(hr)) return Fail(error, "Create wireframe pipeline failed", hr);
        state.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;

        // 半透明版: 一般的なアルファブレンド。深度はテストするが書き込まない
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        hr = m_pDevice->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&m_pTransparentPipeline));
        if (FAILED(hr)) return Fail(error, "Create transparent pipeline failed", hr);

        // 不透明の設定へ戻す
        blend.BlendEnable = FALSE;
        blend.SrcBlend = D3D12_BLEND_ONE;
        blend.DestBlend = D3D12_BLEND_ZERO;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_ZERO;
        depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

        // エディターの補助線は組み込み済みのシェーダーを使う。プロジェクトの
        // シェーダーでグリッドや選択枠が消えたり歪んだりせず、書き出したゲームにも
        // HLSL ファイルが要らない。
        state.VS = {g_overlayVS, sizeof(g_overlayVS)};
        state.PS = {g_overlayPS, sizeof(g_overlayPS)};
        state.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

        // グリッド: 深度テストあり（物体の奥に隠れる）
        hr = m_pDevice->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&m_pGridPipeline));
        if (FAILED(hr)) return Fail(error, "Create grid pipeline failed", hr);

        // オーバーレイ: 深度テストなし（常に手前に描く）
        depth.DepthEnable = FALSE;
        hr = m_pDevice->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&m_pOverlayPipeline));
        if (FAILED(hr)) return Fail(error, "Create outline pipeline failed", hr);
        return true;
    }

    //==============================================================================
    // --- 頂点バッファの作成処理 ---
    //==============================================================================
    bool CreateVertexBuffer(const DeviceCreateInfo& info, std::string& error) {
        // CPU から毎フレーム書き換えるのでアップロードヒープに置く
        D3D12_HEAP_PROPERTIES upload{};
        upload.Type = D3D12_HEAP_TYPE_UPLOAD;
        upload.CreationNodeMask = 1;
        upload.VisibleNodeMask = 1;
        const auto nBytes = info.nMaxVertexCount * sizeof(Vertex);
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = nBytes;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HRESULT hr = m_pDevice->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(&m_pVertexBuffer));
        if (FAILED(hr)) return Fail(error, "Create vertex buffer failed", hr);
        if (!UploadVertices(0, info.pStaticVertices, info.nStaticVertexCount, error)) return false; // 固定の頂点を先頭に書く
        m_vertexView.BufferLocation = m_pVertexBuffer->GetGPUVirtualAddress();
        m_vertexView.SizeInBytes = static_cast<UINT>(nBytes);
        m_vertexView.StrideInBytes = sizeof(Vertex);
        return true;
    }

    unsigned m_uWidth = 0;                              // 描画領域の幅
    unsigned m_uHeight = 0;                             // 描画領域の高さ
    UINT m_uRtvStride = 0;                              // RTV ディスクリプタ 1 つ分の大きさ
    UINT m_uFrameIndex = 0;                             // 現在のバックバッファ番号
    UINT64 m_nFenceValue = 0;                           // 最後に Signal したフェンス値
    HANDLE m_hFenceEvent = nullptr;                     // フェンス到達を待つイベント
    ComPtr<IDXGIFactory4> m_pFactory;                   // DXGI ファクトリ
    ComPtr<ID3D12Device> m_pDevice;                     // デバイス
    ComPtr<ID3D12CommandQueue> m_pQueue;                // コマンドキュー
    ComPtr<IDXGISwapChain3> m_pSwapChain;               // スワップチェーン
    ComPtr<ID3D12DescriptorHeap> m_pRtvHeap;            // レンダーターゲットビュー用ヒープ
    ComPtr<ID3D12DescriptorHeap> m_pDsvHeap;            // 深度ステンシルビュー用ヒープ
    ComPtr<ID3D12Resource> m_apTargets[BUFFER_COUNT];   // バックバッファ
    ComPtr<ID3D12Resource> m_pDepth;                    // 深度バッファ
    ComPtr<ID3D12Resource> m_pVertexBuffer;             // 頂点バッファ
    ComPtr<ID3D12CommandAllocator> m_pAllocator;        // コマンドアロケーター
    ComPtr<ID3D12GraphicsCommandList> m_pCommands;      // コマンドリスト
    ComPtr<ID3D12RootSignature> m_pRootSignature;       // ルートシグネチャ
    ComPtr<ID3D12PipelineState> m_pScenePipeline;       // シーン（不透明）
    ComPtr<ID3D12PipelineState> m_pWireframePipeline;   // シーン（ワイヤーフレーム）
    ComPtr<ID3D12PipelineState> m_pTransparentPipeline; // シーン（半透明）
    ComPtr<ID3D12PipelineState> m_pGridPipeline;        // グリッド
    ComPtr<ID3D12PipelineState> m_pOverlayPipeline;     // 補助線（常に手前）
    ComPtr<ID3D12Fence> m_pFence;                       // GPU 完了待ち用フェンス
    D3D12_VERTEX_BUFFER_VIEW m_vertexView{};            // 頂点バッファビュー
};
} // namespace

//==================================================================================
// --- Direct3D 12 デバイスの生成処理 ---
//==================================================================================
std::unique_ptr<IGraphicsDevice> CreateDeviceD3D12() {
    return std::make_unique<DeviceD3D12>();
}

} // namespace dxs::graphics
