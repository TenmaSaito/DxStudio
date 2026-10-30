// Direct3D 12 backend. Behaviour matches the original DxStudio renderer:
// two flip-model back buffers, one upload-heap vertex buffer, 20 root
// constants for ObjectConstants and a full GPU wait after every Present.

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

constexpr UINT BUFFER_COUNT = 2;
constexpr DXGI_FORMAT COLOR_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;
constexpr UINT CONSTANT_COUNT = sizeof(ObjectConstants) / 4;

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* pResource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = pResource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = from;
    barrier.Transition.StateAfter = to;
    return barrier;
}

class DeviceD3D12 final : public IGraphicsDevice {
public:
    ~DeviceD3D12() override { Shutdown(); }

    GraphicsApi Api() const override { return GraphicsApi::DIRECT3D12; }

    bool Initialize(const DeviceCreateInfo& info, std::string& error) override {
        m_uWidth = info.uWidth;
        m_uHeight = info.uHeight;
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_pFactory));
        if (FAILED(hr)) return Fail(error, "Create DXGI factory failed", hr);
        hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice));
        if (FAILED(hr)) return Fail(error, "Create Direct3D 12 device failed", hr);
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        hr = m_pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_pQueue));
        if (FAILED(hr)) return Fail(error, "Create command queue failed", hr);

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
        hr = pBaseSwapChain.As(&m_pSwapChain);
        if (FAILED(hr)) return Fail(error, "Query swap chain failed", hr);
        m_pFactory->MakeWindowAssociation(info.hWnd, DXGI_MWA_NO_ALT_ENTER);
        m_uFrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

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

        if (!CreatePipelines(info.sShaderPath, error)) return false;
        hr = m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pAllocator.Get(), m_pScenePipeline.Get(),
                                          IID_PPV_ARGS(&m_pCommands));
        if (FAILED(hr)) return Fail(error, "Create command list failed", hr);
        hr = m_pCommands->Close();
        if (FAILED(hr)) return Fail(error, "Close command list failed", hr);
        if (!CreateVertexBuffer(info, error)) return false;

        hr = m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence));
        if (FAILED(hr)) return Fail(error, "Create fence failed", hr);
        m_hFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_hFenceEvent) {
            error = "Create fence event failed";
            return false;
        }
        return true;
    }

    bool Resize(unsigned uWidth, unsigned uHeight, std::string& error) override {
        if (!m_pSwapChain) return true;
        if (!WaitForGpu(error)) return false;
        for (auto& pTarget : m_apTargets) pTarget.Reset();
        m_pDepth.Reset();
        const HRESULT hr = m_pSwapChain->ResizeBuffers(BUFFER_COUNT, uWidth, uHeight, COLOR_FORMAT, 0);
        if (FAILED(hr)) return Fail(error, "Resize swap chain failed", hr);
        m_uWidth = uWidth;
        m_uHeight = uHeight;
        m_uFrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();
        return CreateTargets(error);
    }

    bool UploadVertices(std::size_t nFirstVertex, const Vertex* pVertices, std::size_t nCount, std::string& error) override {
        if (!nCount) return true;
        void* pMapped = nullptr;
        const D3D12_RANGE noRead{0, 0};
        const HRESULT hr = m_pVertexBuffer->Map(0, &noRead, &pMapped);
        if (FAILED(hr)) return Fail(error, "Map model vertex buffer failed", hr);
        std::memcpy(static_cast<Vertex*>(pMapped) + nFirstVertex, pVertices, nCount * sizeof(Vertex));
        m_pVertexBuffer->Unmap(0, nullptr);
        return true;
    }

    bool BeginFrame(const float (&aClearColor)[4], std::string& error) override {
        HRESULT hr = m_pAllocator->Reset();
        if (FAILED(hr)) return Fail(error, "Reset command allocator failed", hr);
        hr = m_pCommands->Reset(m_pAllocator.Get(), m_pScenePipeline.Get());
        if (FAILED(hr)) return Fail(error, "Reset command list failed", hr);
        const auto barrier = Transition(m_apTargets[m_uFrameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_pCommands->ResourceBarrier(1, &barrier);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_pRtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(m_uFrameIndex) * m_uRtvStride;
        const auto dsv = m_pDsvHeap->GetCPUDescriptorHandleForHeapStart();
        m_pCommands->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        m_pCommands->ClearRenderTargetView(rtv, aClearColor, 0, nullptr);
        m_pCommands->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(m_uWidth), static_cast<float>(m_uHeight), 0, 1};
        const D3D12_RECT scissor{0, 0, static_cast<LONG>(m_uWidth), static_cast<LONG>(m_uHeight)};
        m_pCommands->RSSetViewports(1, &viewport);
        m_pCommands->RSSetScissorRects(1, &scissor);
        m_pCommands->SetGraphicsRootSignature(m_pRootSignature.Get());
        m_pCommands->IASetVertexBuffers(0, 1, &m_vertexView);
        SetPipeline(PipelineKind::SCENE);
        return true;
    }

    void SetPipeline(PipelineKind kind) override {
        ID3D12PipelineState* pState = kind == PipelineKind::SCENE ? m_pScenePipeline.Get()
                                      : kind == PipelineKind::GRID ? m_pGridPipeline.Get()
                                                                   : m_pOverlayPipeline.Get();
        m_pCommands->SetPipelineState(pState);
        m_pCommands->IASetPrimitiveTopology(kind == PipelineKind::SCENE ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
                                                                        : D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    }

    void Draw(const ObjectConstants& constants, unsigned uVertexCount, unsigned uFirstVertex) override {
        m_pCommands->SetGraphicsRoot32BitConstants(0, CONSTANT_COUNT, &constants, 0);
        m_pCommands->DrawInstanced(uVertexCount, 1, uFirstVertex, 0);
    }

    bool EndFrame(std::string& error) override {
        const auto barrier = Transition(m_apTargets[m_uFrameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                        D3D12_RESOURCE_STATE_PRESENT);
        m_pCommands->ResourceBarrier(1, &barrier);
        HRESULT hr = m_pCommands->Close();
        if (FAILED(hr)) return Fail(error, "Close command list failed", hr);
        ID3D12CommandList* apLists[] = {m_pCommands.Get()};
        m_pQueue->ExecuteCommandLists(1, apLists);
        hr = m_pSwapChain->Present(1, 0);
        if (FAILED(hr)) return Fail(error, "Present failed", hr);
        if (!WaitForGpu(error)) return false;
        m_uFrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();
        return true;
    }

    void Shutdown() override {
        std::string ignored;
        WaitForGpu(ignored);
        if (m_hFenceEvent) CloseHandle(m_hFenceEvent);
        m_hFenceEvent = nullptr;
        m_pCommands.Reset();
        m_pAllocator.Reset();
        m_pFence.Reset();
        m_pVertexBuffer.Reset();
        m_pScenePipeline.Reset();
        m_pGridPipeline.Reset();
        m_pOverlayPipeline.Reset();
        m_pRootSignature.Reset();
        m_pDepth.Reset();
        for (auto& pTarget : m_apTargets) pTarget.Reset();
        m_pRtvHeap.Reset();
        m_pDsvHeap.Reset();
        m_pSwapChain.Reset();
        m_pQueue.Reset();
        m_pDevice.Reset();
        m_pFactory.Reset();
    }

private:
    static bool Fail(std::string& error, const char* pAction, HRESULT hr) {
        error = FormatHResult(pAction, hr);
        return false;
    }

    bool WaitForGpu(std::string& error) {
        if (!m_pQueue || !m_pFence || !m_hFenceEvent) return true;
        const UINT64 nValue = ++m_nFenceValue;
        HRESULT hr = m_pQueue->Signal(m_pFence.Get(), nValue);
        if (FAILED(hr)) return Fail(error, "Command queue signal failed", hr);
        if (m_pFence->GetCompletedValue() < nValue) {
            hr = m_pFence->SetEventOnCompletion(nValue, m_hFenceEvent);
            if (FAILED(hr)) return Fail(error, "Fence event failed", hr);
            if (WaitForSingleObject(m_hFenceEvent, INFINITE) != WAIT_OBJECT_0) {
                error = "Waiting for the GPU failed";
                return false;
            }
        }
        return true;
    }

    bool CreateTargets(std::string& error) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_pRtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT uIndex = 0; uIndex < BUFFER_COUNT; ++uIndex) {
            const HRESULT hr = m_pSwapChain->GetBuffer(uIndex, IID_PPV_ARGS(&m_apTargets[uIndex]));
            if (FAILED(hr)) return Fail(error, "Get swap-chain buffer failed", hr);
            m_pDevice->CreateRenderTargetView(m_apTargets[uIndex].Get(), nullptr, handle);
            handle.ptr += m_uRtvStride;
        }
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
        D3D12_CLEAR_VALUE clear{};
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

    bool CreatePipelines(const std::wstring& sShaderPath, std::string& error) {
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
            error = pDiagnostics ? std::string(static_cast<const char*>(pDiagnostics->GetBufferPointer()), pDiagnostics->GetBufferSize())
                                 : FormatHResult("Serialize root signature failed", hr);
            return false;
        }
        hr = m_pDevice->CreateRootSignature(0, pSignature->GetBufferPointer(), pSignature->GetBufferSize(),
                                            IID_PPV_ARGS(&m_pRootSignature));
        if (FAILED(hr)) return Fail(error, "Create root signature failed", hr);

        ShaderPair shaders;
        if (!LoadShaderPair(sShaderPath, SHADER_PROFILE_SM5, shaders, error)) return false;

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

        // Editor guides use baked shaders, so a project shader cannot hide or
        // distort the grid and selection outline. Game exports need no HLSL file.
        state.VS = {g_overlayVS, sizeof(g_overlayVS)};
        state.PS = {g_overlayPS, sizeof(g_overlayPS)};
        state.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        hr = m_pDevice->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&m_pGridPipeline));
        if (FAILED(hr)) return Fail(error, "Create grid pipeline failed", hr);
        depth.DepthEnable = FALSE;
        hr = m_pDevice->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&m_pOverlayPipeline));
        if (FAILED(hr)) return Fail(error, "Create outline pipeline failed", hr);
        return true;
    }

    bool CreateVertexBuffer(const DeviceCreateInfo& info, std::string& error) {
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
        if (!UploadVertices(0, info.pStaticVertices, info.nStaticVertexCount, error)) return false;
        m_vertexView.BufferLocation = m_pVertexBuffer->GetGPUVirtualAddress();
        m_vertexView.SizeInBytes = static_cast<UINT>(nBytes);
        m_vertexView.StrideInBytes = sizeof(Vertex);
        return true;
    }

    unsigned m_uWidth = 0;
    unsigned m_uHeight = 0;
    UINT m_uRtvStride = 0;
    UINT m_uFrameIndex = 0;
    UINT64 m_nFenceValue = 0;
    HANDLE m_hFenceEvent = nullptr;
    ComPtr<IDXGIFactory4> m_pFactory;
    ComPtr<ID3D12Device> m_pDevice;
    ComPtr<ID3D12CommandQueue> m_pQueue;
    ComPtr<IDXGISwapChain3> m_pSwapChain;
    ComPtr<ID3D12DescriptorHeap> m_pRtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_pDsvHeap;
    ComPtr<ID3D12Resource> m_apTargets[BUFFER_COUNT];
    ComPtr<ID3D12Resource> m_pDepth;
    ComPtr<ID3D12Resource> m_pVertexBuffer;
    ComPtr<ID3D12CommandAllocator> m_pAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_pCommands;
    ComPtr<ID3D12RootSignature> m_pRootSignature;
    ComPtr<ID3D12PipelineState> m_pScenePipeline;
    ComPtr<ID3D12PipelineState> m_pGridPipeline;
    ComPtr<ID3D12PipelineState> m_pOverlayPipeline;
    ComPtr<ID3D12Fence> m_pFence;
    D3D12_VERTEX_BUFFER_VIEW m_vertexView{};
};
} // namespace

std::unique_ptr<IGraphicsDevice> CreateDeviceD3D12() { return std::make_unique<DeviceD3D12>(); }

} // namespace dxs::graphics
