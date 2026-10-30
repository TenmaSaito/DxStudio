// Direct3D 11 backend. Uses the same SM5 bytecode as Direct3D 12 (project
// shader and the baked overlay shader), a DEFAULT-usage vertex buffer updated
// with UpdateSubresource and one dynamic 80-byte constant buffer.

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

constexpr DXGI_FORMAT COLOR_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr UINT BUFFER_COUNT = 2;

class DeviceD3D11 final : public IGraphicsDevice {
public:
    ~DeviceD3D11() override { Shutdown(); }

    GraphicsApi Api() const override { return GraphicsApi::DIRECT3D11; }

    bool Initialize(const DeviceCreateInfo& info, std::string& error) override {
        m_uWidth = info.uWidth;
        m_uHeight = info.uHeight;
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
        uFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        HRESULT hr = E_FAIL;
        for (int nAttempt = 0; nAttempt < 2 && FAILED(hr); ++nAttempt) {
            hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, uFlags, aLevels, 2, D3D11_SDK_VERSION,
                                               &swapDesc, &m_pSwapChain, &m_pDevice, nullptr, &m_pContext);
            uFlags &= ~D3D11_CREATE_DEVICE_DEBUG; // retry without the SDK layers
            if (FAILED(hr) && nAttempt == 1) {
                // Older systems without flip-model support.
                swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
                swapDesc.BufferCount = 1;
                hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, uFlags, aLevels, 2, D3D11_SDK_VERSION,
                                                   &swapDesc, &m_pSwapChain, &m_pDevice, nullptr, &m_pContext);
            }
        }
        if (FAILED(hr)) return Fail(error, "Create Direct3D 11 device failed", hr);
        m_uBufferCount = swapDesc.BufferCount;
        ComPtr<IDXGIFactory> pFactory;
        if (SUCCEEDED(m_pSwapChain->GetParent(IID_PPV_ARGS(&pFactory)))) {
            pFactory->MakeWindowAssociation(info.hWnd, DXGI_MWA_NO_ALT_ENTER);
        }
        if (!CreateTargets(error)) return false;
        if (!CreateShaders(info.sShaderPath, error)) return false;
        if (!CreateStates(error)) return false;
        return CreateBuffers(info, error);
    }

    bool Resize(unsigned uWidth, unsigned uHeight, std::string& error) override {
        if (!m_pSwapChain) return true;
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

    bool UploadVertices(std::size_t nFirstVertex, const Vertex* pVertices, std::size_t nCount, std::string&) override {
        if (!nCount) return true;
        const D3D11_BOX box{static_cast<UINT>(nFirstVertex * sizeof(Vertex)), 0, 0,
                            static_cast<UINT>((nFirstVertex + nCount) * sizeof(Vertex)), 1, 1};
        m_pContext->UpdateSubresource(m_pVertexBuffer.Get(), 0, &box, pVertices, 0, 0);
        return true;
    }

    bool BeginFrame(const float (&aClearColor)[4], std::string&) override {
        ID3D11RenderTargetView* apTargets[] = {m_pRenderTarget.Get()};
        m_pContext->OMSetRenderTargets(1, apTargets, m_pDepthView.Get());
        m_pContext->ClearRenderTargetView(m_pRenderTarget.Get(), aClearColor);
        m_pContext->ClearDepthStencilView(m_pDepthView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
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

    void SetPipeline(PipelineKind kind) override {
        const bool bScene = kind == PipelineKind::SCENE || kind == PipelineKind::SCENE_WIREFRAME;
        m_pContext->RSSetState(kind == PipelineKind::SCENE_WIREFRAME ? m_pWireframeRasterizer.Get() : m_pRasterizer.Get());
        m_pContext->VSSetShader(bScene ? m_pSceneVertexShader.Get() : m_pOverlayVertexShader.Get(), nullptr, 0);
        m_pContext->PSSetShader(bScene ? m_pScenePixelShader.Get() : m_pOverlayPixelShader.Get(), nullptr, 0);
        ID3D11DepthStencilState* pDepth = bScene ? m_pDepthScene.Get() : kind == PipelineKind::GRID ? m_pDepthGrid.Get()
                                                                                                    : m_pDepthOverlay.Get();
        m_pContext->OMSetDepthStencilState(pDepth, 0);
        m_pContext->IASetPrimitiveTopology(bScene ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST : D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    }

    void Draw(const ObjectConstants& constants, unsigned uVertexCount, unsigned uFirstVertex) override {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(m_pContext->Map(m_pConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &constants, sizeof(constants));
            m_pContext->Unmap(m_pConstantBuffer.Get(), 0);
        }
        m_pContext->Draw(uVertexCount, uFirstVertex);
    }

    bool EndFrame(std::string& error) override {
        const HRESULT hr = m_pSwapChain->Present(1, 0);
        if (FAILED(hr)) return Fail(error, "Present failed", hr);
        return true;
    }

    void Shutdown() override {
        if (m_pContext) {
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
    static bool Fail(std::string& error, const char* pAction, HRESULT hr) {
        error = FormatHResult(pAction, hr);
        return false;
    }

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

    bool CreateShaders(const std::wstring& sShaderPath, std::string& error) {
        ShaderPair shaders;
        if (!LoadShaderPair(sShaderPath, SHADER_PROFILE_SM5, shaders, error)) return false;
        auto* pVs = shaders.pVertexShader.Get();
        auto* pPs = shaders.pPixelShader.Get();
        HRESULT hr = m_pDevice->CreateVertexShader(pVs->GetBufferPointer(), pVs->GetBufferSize(), nullptr, &m_pSceneVertexShader);
        if (FAILED(hr)) return Fail(error, "Create vertex shader failed", hr);
        hr = m_pDevice->CreatePixelShader(pPs->GetBufferPointer(), pPs->GetBufferSize(), nullptr, &m_pScenePixelShader);
        if (FAILED(hr)) return Fail(error, "Create pixel shader failed", hr);
        hr = m_pDevice->CreateVertexShader(g_overlayVS, sizeof(g_overlayVS), nullptr, &m_pOverlayVertexShader);
        if (FAILED(hr)) return Fail(error, "Create overlay vertex shader failed", hr);
        hr = m_pDevice->CreatePixelShader(g_overlayPS, sizeof(g_overlayPS), nullptr, &m_pOverlayPixelShader);
        if (FAILED(hr)) return Fail(error, "Create overlay pixel shader failed", hr);
        const D3D11_INPUT_ELEMENT_DESC aInput[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        hr = m_pDevice->CreateInputLayout(aInput, 1, pVs->GetBufferPointer(), pVs->GetBufferSize(), &m_pInputLayout);
        if (FAILED(hr)) return Fail(error, "Create input layout failed", hr);
        return true;
    }

    bool CreateStates(std::string& error) {
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        HRESULT hr = m_pDevice->CreateRasterizerState(&raster, &m_pRasterizer);
        if (FAILED(hr)) return Fail(error, "Create rasterizer state failed", hr);
        raster.FillMode = D3D11_FILL_WIREFRAME;
        hr = m_pDevice->CreateRasterizerState(&raster, &m_pWireframeRasterizer);
        if (FAILED(hr)) return Fail(error, "Create wireframe rasterizer state failed", hr);
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
        return true;
    }

    bool CreateBuffers(const DeviceCreateInfo& info, std::string& error) {
        D3D11_BUFFER_DESC vertexDesc{};
        vertexDesc.ByteWidth = static_cast<UINT>(info.nMaxVertexCount * sizeof(Vertex));
        vertexDesc.Usage = D3D11_USAGE_DEFAULT;
        vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        HRESULT hr = m_pDevice->CreateBuffer(&vertexDesc, nullptr, &m_pVertexBuffer);
        if (FAILED(hr)) return Fail(error, "Create vertex buffer failed", hr);
        if (!UploadVertices(0, info.pStaticVertices, info.nStaticVertexCount, error)) return false;
        D3D11_BUFFER_DESC constantDesc{};
        constantDesc.ByteWidth = sizeof(ObjectConstants);
        constantDesc.Usage = D3D11_USAGE_DYNAMIC;
        constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constantDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = m_pDevice->CreateBuffer(&constantDesc, nullptr, &m_pConstantBuffer);
        if (FAILED(hr)) return Fail(error, "Create constant buffer failed", hr);
        return true;
    }

    unsigned m_uWidth = 0;
    unsigned m_uHeight = 0;
    UINT m_uBufferCount = BUFFER_COUNT;
    ComPtr<ID3D11Device> m_pDevice;
    ComPtr<ID3D11DeviceContext> m_pContext;
    ComPtr<IDXGISwapChain> m_pSwapChain;
    ComPtr<ID3D11RenderTargetView> m_pRenderTarget;
    ComPtr<ID3D11Texture2D> m_pDepth;
    ComPtr<ID3D11DepthStencilView> m_pDepthView;
    ComPtr<ID3D11VertexShader> m_pSceneVertexShader;
    ComPtr<ID3D11PixelShader> m_pScenePixelShader;
    ComPtr<ID3D11VertexShader> m_pOverlayVertexShader;
    ComPtr<ID3D11PixelShader> m_pOverlayPixelShader;
    ComPtr<ID3D11InputLayout> m_pInputLayout;
    ComPtr<ID3D11RasterizerState> m_pRasterizer;
    ComPtr<ID3D11RasterizerState> m_pWireframeRasterizer;
    ComPtr<ID3D11DepthStencilState> m_pDepthScene;
    ComPtr<ID3D11DepthStencilState> m_pDepthGrid;
    ComPtr<ID3D11DepthStencilState> m_pDepthOverlay;
    ComPtr<ID3D11Buffer> m_pVertexBuffer;
    ComPtr<ID3D11Buffer> m_pConstantBuffer;
};
} // namespace

std::unique_ptr<IGraphicsDevice> CreateDeviceD3D11() { return std::make_unique<DeviceD3D11>(); }

} // namespace dxs::graphics
