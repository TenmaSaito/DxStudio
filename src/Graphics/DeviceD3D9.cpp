// Direct3D 9 backend for older GPUs and drivers.
//
// - The project shader is compiled as vs_3_0 / ps_3_0. If it cannot be built
//   for SM3 (or an export has no Default9.*.cso), the built-in flat shader is
//   used and a warning is logged, so the scene still renders.
// - Managed-pool resources survive device resets (resize, lock screen, ...).
// - Constants: VS c0..c3 = row-major mvp, c4 = color; PS c0 and c4 = color.

#include "GraphicsDevice.h"
#include "ShaderLoader.h"
#include "../Core/Log.h"

#include <d3d9.h>
#include <wrl/client.h>

#include <cstring>

namespace dxs::graphics {
namespace {
using Microsoft::WRL::ComPtr;

D3DCOLOR ToColor(const float (&aColor)[4]) {
    auto channel = [](float fValue) {
        const float fClamped = fValue < 0.0f ? 0.0f : fValue > 1.0f ? 1.0f : fValue;
        return static_cast<DWORD>(fClamped * 255.0f + 0.5f);
    };
    return D3DCOLOR_ARGB(channel(aColor[3]), channel(aColor[0]), channel(aColor[1]), channel(aColor[2]));
}

class DeviceD3D9 final : public IGraphicsDevice {
public:
    ~DeviceD3D9() override { Shutdown(); }

    GraphicsApi Api() const override { return GraphicsApi::DIRECT3D9; }

    bool Initialize(const DeviceCreateInfo& info, std::string& error) override {
        m_pD3D.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        if (!m_pD3D) {
            error = "Direct3D 9 is not available on this system";
            return false;
        }
        m_presentParameters = {};
        m_presentParameters.BackBufferWidth = info.uWidth;
        m_presentParameters.BackBufferHeight = info.uHeight;
        m_presentParameters.BackBufferFormat = D3DFMT_X8R8G8B8;
        m_presentParameters.BackBufferCount = 1;
        m_presentParameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        m_presentParameters.hDeviceWindow = info.hWnd;
        m_presentParameters.Windowed = TRUE;
        m_presentParameters.EnableAutoDepthStencil = TRUE;
        m_presentParameters.AutoDepthStencilFormat = D3DFMT_D24X8;
        m_presentParameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
        // FPU_PRESERVE keeps double precision for the rest of the engine.
        const DWORD aBehaviors[] = {D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
                                    D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE};
        HRESULT hr = E_FAIL;
        for (const DWORD dwBehavior : aBehaviors) {
            hr = m_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, info.hWnd, dwBehavior, &m_presentParameters, &m_pDevice);
            if (SUCCEEDED(hr)) break;
        }
        if (FAILED(hr)) return Fail(error, "Create Direct3D 9 device failed", hr);

        const D3DVERTEXELEMENT9 aElements[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                               D3DDECL_END()};
        hr = m_pDevice->CreateVertexDeclaration(aElements, &m_pDeclaration);
        if (FAILED(hr)) return Fail(error, "Create vertex declaration failed", hr);
        if (!CreateShaders(info.sShaderPath, error)) return false;
        hr = m_pDevice->CreateVertexBuffer(static_cast<UINT>(info.nMaxVertexCount * sizeof(Vertex)), D3DUSAGE_WRITEONLY, 0,
                                           D3DPOOL_MANAGED, &m_pVertexBuffer, nullptr);
        if (FAILED(hr)) return Fail(error, "Create vertex buffer failed", hr);
        return UploadVertices(0, info.pStaticVertices, info.nStaticVertexCount, error);
    }

    bool Resize(unsigned uWidth, unsigned uHeight, std::string& error) override {
        if (!m_pDevice) return true;
        m_presentParameters.BackBufferWidth = uWidth;
        m_presentParameters.BackBufferHeight = uHeight;
        const HRESULT hr = m_pDevice->Reset(&m_presentParameters);
        if (hr == D3DERR_DEVICELOST) {
            m_bDeviceLost = true; // retried in BeginFrame
            return true;
        }
        if (FAILED(hr)) return Fail(error, "Reset Direct3D 9 device failed", hr);
        m_bDeviceLost = false;
        return true;
    }

    bool UploadVertices(std::size_t nFirstVertex, const Vertex* pVertices, std::size_t nCount, std::string& error) override {
        if (!nCount) return true;
        void* pMapped = nullptr;
        const HRESULT hr = m_pVertexBuffer->Lock(static_cast<UINT>(nFirstVertex * sizeof(Vertex)),
                                                 static_cast<UINT>(nCount * sizeof(Vertex)), &pMapped, 0);
        if (FAILED(hr)) return Fail(error, "Lock vertex buffer failed", hr);
        std::memcpy(pMapped, pVertices, nCount * sizeof(Vertex));
        m_pVertexBuffer->Unlock();
        return true;
    }

    bool BeginFrame(const float (&aClearColor)[4], std::string& error) override {
        m_bSkipFrame = false;
        HRESULT hr = m_pDevice->TestCooperativeLevel();
        if (hr == D3DERR_DEVICELOST) {
            m_bSkipFrame = true; // e.g. the workstation is locked
            return true;
        }
        if (hr == D3DERR_DEVICENOTRESET || m_bDeviceLost) {
            hr = m_pDevice->Reset(&m_presentParameters);
            if (FAILED(hr)) {
                m_bSkipFrame = true;
                return true;
            }
            m_bDeviceLost = false;
        }
        hr = m_pDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, ToColor(aClearColor), 1.0f, 0);
        if (FAILED(hr)) return Fail(error, "Clear failed", hr);
        hr = m_pDevice->BeginScene();
        if (FAILED(hr)) return Fail(error, "BeginScene failed", hr);
        m_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
        m_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        m_pDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        m_pDevice->SetVertexDeclaration(m_pDeclaration.Get());
        m_pDevice->SetStreamSource(0, m_pVertexBuffer.Get(), 0, sizeof(Vertex));
        SetPipeline(PipelineKind::SCENE);
        return true;
    }

    void SetPipeline(PipelineKind kind) override {
        if (m_bSkipFrame) return;
        m_kind = kind;
        const bool bScene = kind == PipelineKind::SCENE || kind == PipelineKind::SCENE_WIREFRAME;
        m_pDevice->SetRenderState(D3DRS_FILLMODE, kind == PipelineKind::SCENE_WIREFRAME ? D3DFILL_WIREFRAME : D3DFILL_SOLID);
        m_pDevice->SetVertexShader(bScene ? m_pSceneVertexShader.Get() : m_pOverlayVertexShader.Get());
        m_pDevice->SetPixelShader(bScene ? m_pScenePixelShader.Get() : m_pOverlayPixelShader.Get());
        m_pDevice->SetRenderState(D3DRS_ZENABLE, kind == PipelineKind::OVERLAY ? D3DZB_FALSE : D3DZB_TRUE);
        m_pDevice->SetRenderState(D3DRS_ZWRITEENABLE, bScene ? TRUE : FALSE);
    }

    void Draw(const ObjectConstants& constants, unsigned uVertexCount, unsigned uFirstVertex) override {
        if (m_bSkipFrame) return;
        m_pDevice->SetVertexShaderConstantF(0, &constants.mvp.m[0][0], 4);
        m_pDevice->SetVertexShaderConstantF(4, &constants.color.x, 1);
        m_pDevice->SetPixelShaderConstantF(0, &constants.color.x, 1);
        m_pDevice->SetPixelShaderConstantF(4, &constants.color.x, 1);
        const bool bTriangles = m_kind == PipelineKind::SCENE || m_kind == PipelineKind::SCENE_WIREFRAME;
        const UINT uPrimitives = bTriangles ? uVertexCount / 3 : uVertexCount / 2;
        if (uPrimitives) m_pDevice->DrawPrimitive(bTriangles ? D3DPT_TRIANGLELIST : D3DPT_LINELIST, uFirstVertex, uPrimitives);
    }

    bool EndFrame(std::string& error) override {
        if (m_bSkipFrame) {
            Sleep(16);
            return true;
        }
        m_pDevice->EndScene();
        const HRESULT hr = m_pDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (hr == D3DERR_DEVICELOST) {
            m_bDeviceLost = true;
            return true;
        }
        if (FAILED(hr)) return Fail(error, "Present failed", hr);
        return true;
    }

    void Shutdown() override {
        m_pVertexBuffer.Reset();
        m_pDeclaration.Reset();
        m_pSceneVertexShader.Reset();
        m_pScenePixelShader.Reset();
        m_pOverlayVertexShader.Reset();
        m_pOverlayPixelShader.Reset();
        m_pDevice.Reset();
        m_pD3D.Reset();
    }

private:
    static bool Fail(std::string& error, const char* pAction, HRESULT hr) {
        error = FormatHResult(pAction, hr);
        return false;
    }

    bool CreateShaderObjects(const ShaderPair& shaders, ComPtr<IDirect3DVertexShader9>& pVertex,
                             ComPtr<IDirect3DPixelShader9>& pPixel, std::string& error) {
        HRESULT hr = m_pDevice->CreateVertexShader(static_cast<const DWORD*>(shaders.pVertexShader->GetBufferPointer()), &pVertex);
        if (FAILED(hr)) return Fail(error, "Create SM3 vertex shader failed", hr);
        hr = m_pDevice->CreatePixelShader(static_cast<const DWORD*>(shaders.pPixelShader->GetBufferPointer()), &pPixel);
        if (FAILED(hr)) return Fail(error, "Create SM3 pixel shader failed", hr);
        return true;
    }

    bool CreateShaders(const std::wstring& sShaderPath, std::string& error) {
        ShaderPair builtin;
        if (!CompileShaderSource(BUILTIN_FLAT_SHADER_SOURCE, SHADER_PROFILE_SM3, builtin, error)) return false;
        if (!CreateShaderObjects(builtin, m_pOverlayVertexShader, m_pOverlayPixelShader, error)) return false;

        ShaderPair project;
        std::string projectError;
        if (LoadShaderPair(sShaderPath, SHADER_PROFILE_SM3, project, projectError) &&
            CreateShaderObjects(project, m_pSceneVertexShader, m_pScenePixelShader, projectError)) {
            return true;
        }
        Debug::LogWarning("Direct3D 9: the project shader is not available for Shader Model 3, "
                          "so the built-in flat shader is used. " + projectError);
        m_pSceneVertexShader = m_pOverlayVertexShader;
        m_pScenePixelShader = m_pOverlayPixelShader;
        return true;
    }

    ComPtr<IDirect3D9> m_pD3D;
    ComPtr<IDirect3DDevice9> m_pDevice;
    D3DPRESENT_PARAMETERS m_presentParameters{};
    ComPtr<IDirect3DVertexDeclaration9> m_pDeclaration;
    ComPtr<IDirect3DVertexBuffer9> m_pVertexBuffer;
    ComPtr<IDirect3DVertexShader9> m_pSceneVertexShader;
    ComPtr<IDirect3DPixelShader9> m_pScenePixelShader;
    ComPtr<IDirect3DVertexShader9> m_pOverlayVertexShader;
    ComPtr<IDirect3DPixelShader9> m_pOverlayPixelShader;
    PipelineKind m_kind = PipelineKind::SCENE;
    bool m_bDeviceLost = false;
    bool m_bSkipFrame = false;
};
} // namespace

std::unique_ptr<IGraphicsDevice> CreateDeviceD3D9() { return std::make_unique<DeviceD3D9>(); }

} // namespace dxs::graphics
