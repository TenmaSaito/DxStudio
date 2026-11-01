//==================================================================================
//
// Direct3D 9 描画デバイスのソースファイル [DeviceD3D9.cpp]
//
// 古い GPU / ドライバー向けのバックエンド。
//   ・プロジェクトのシェーダーは vs_3_0 / ps_3_0 でコンパイルする。SM3 で
//     作れない場合（または書き出したゲームに Default9.*.cso が無い場合）は
//     組み込みの単色シェーダーを使い、警告をログに出す（シーンは表示され続ける）。
//   ・MANAGED プールのリソースはデバイスのリセット（大きさ変更・画面ロック等）
//     後も残る。
//   ・定数: 頂点シェーダー c0..c3 = 行優先の mvp、c4 = 色 / ピクセルシェーダー c0 と c4 = 色。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "GraphicsDevice.h"
#include "ShaderLoader.h"
#include "../Core/Log.h"

#include <d3d9.h>
#include <wrl/client.h>

#include <cstring>

namespace dxs::graphics {
namespace {
using Microsoft::WRL::ComPtr;

//==================================================================================
// --- 0..1 の RGBA → D3DCOLOR（ARGB 各 8bit）の変換処理 ---
//==================================================================================
D3DCOLOR ToColor(const float (&aColor)[4]) {
    auto channel = [](float fValue) {
        const float fClamped = fValue < 0.0f ? 0.0f : fValue > 1.0f ? 1.0f : fValue; // 0..1 に収める
        return static_cast<DWORD>(fClamped * 255.0f + 0.5f);                        // 四捨五入して 0..255
    };
    return D3DCOLOR_ARGB(channel(aColor[3]), channel(aColor[0]), channel(aColor[1]), channel(aColor[2]));
}

//**********************************************************************************
// *** Direct3D 9 描画デバイスクラス ***
//**********************************************************************************
class DeviceD3D9 final : public IGraphicsDevice {
public:
    ~DeviceD3D9() override { Shutdown(); }

    GraphicsApi Api() const override { return GraphicsApi::DIRECT3D9; }

    //==============================================================================
    // --- 初期化処理 ---
    //==============================================================================
    bool Initialize(const DeviceCreateInfo& info, std::string& error) override {
        m_pD3D.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        if (!m_pD3D) {
            error = "Direct3D 9 is not available on this system";
            return false;
        }
        // 表示の設定（ウィンドウモード・自動深度バッファ・垂直同期あり）
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
        // FPU_PRESERVE: エンジンの他の部分の double 精度を保つ（D3D9 は既定で精度を下げる）。
        // ハードウェアの頂点処理で作れなければソフトウェアで作る。
        const DWORD aBehaviors[] = {D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
                                    D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE};
        HRESULT hr = E_FAIL;
        for (const DWORD dwBehavior : aBehaviors) {
            hr = m_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, info.hWnd, dwBehavior, &m_presentParameters, &m_pDevice);
            if (SUCCEEDED(hr)) break;
        }
        if (FAILED(hr)) return Fail(error, "Create Direct3D 9 device failed", hr);

        // 頂点宣言（float3 POSITION のみ）→ シェーダー → 頂点バッファ
        const D3DVERTEXELEMENT9 aElements[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                               D3DDECL_END()};
        hr = m_pDevice->CreateVertexDeclaration(aElements, &m_pDeclaration);
        if (FAILED(hr)) return Fail(error, "Create vertex declaration failed", hr);
        if (!CreateShaders(info.sShaderPath, error)) return false;
        hr = m_pDevice->CreateVertexBuffer(static_cast<UINT>(info.nMaxVertexCount * sizeof(Vertex)), D3DUSAGE_WRITEONLY, 0,
                                           D3DPOOL_MANAGED, &m_pVertexBuffer, nullptr);
        if (FAILED(hr)) return Fail(error, "Create vertex buffer failed", hr);
        return UploadVertices(0, info.pStaticVertices, info.nStaticVertexCount, error); // 固定の頂点を先頭に書く
    }

    //==============================================================================
    // --- 描画領域の大きさ変更処理（デバイスのリセット） ---
    //==============================================================================
    bool Resize(unsigned uWidth, unsigned uHeight, std::string& error) override {
        if (!m_pDevice) return true;
        m_presentParameters.BackBufferWidth = uWidth;
        m_presentParameters.BackBufferHeight = uHeight;
        const HRESULT hr = m_pDevice->Reset(&m_presentParameters);
        if (hr == D3DERR_DEVICELOST) {
            m_bDeviceLost = true; // 今はリセットできない: BeginFrame でやり直す
            return true;
        }
        if (FAILED(hr)) return Fail(error, "Reset Direct3D 9 device failed", hr);
        m_bDeviceLost = false;
        return true;
    }

    //==============================================================================
    // --- 頂点の書き込み処理 ---
    //==============================================================================
    bool UploadVertices(std::size_t nFirstVertex, const Vertex* pVertices, std::size_t nCount, std::string& error) override {
        if (!nCount) return true;
        void* pMapped = nullptr;
        // 書き込む範囲だけをロックする
        const HRESULT hr = m_pVertexBuffer->Lock(static_cast<UINT>(nFirstVertex * sizeof(Vertex)),
                                                 static_cast<UINT>(nCount * sizeof(Vertex)), &pMapped, 0);
        if (FAILED(hr)) return Fail(error, "Lock vertex buffer failed", hr);
        std::memcpy(pMapped, pVertices, nCount * sizeof(Vertex));
        m_pVertexBuffer->Unlock();
        return true;
    }

    //==============================================================================
    // --- フレーム開始処理 ---
    //==============================================================================
    bool BeginFrame(const float (&aClearColor)[4], std::string& error) override {
        m_bSkipFrame = false;
        // デバイスロスト（画面ロックなど）の確認
        HRESULT hr = m_pDevice->TestCooperativeLevel();
        if (hr == D3DERR_DEVICELOST) {
            m_bSkipFrame = true; // まだ戻っていない: このフレームは描かない
            return true;
        }
        if (hr == D3DERR_DEVICENOTRESET || m_bDeviceLost) { // 戻ったのでリセットする
            hr = m_pDevice->Reset(&m_presentParameters);
            if (FAILED(hr)) {
                m_bSkipFrame = true;
                return true;
            }
            m_bDeviceLost = false;
        }
        // クリア → 描画開始 → 共通の描画状態
        hr = m_pDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, ToColor(aClearColor), 1.0f, 0);
        if (FAILED(hr)) return Fail(error, "Clear failed", hr);
        hr = m_pDevice->BeginScene();
        if (FAILED(hr)) return Fail(error, "BeginScene failed", hr);
        m_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);       // 固定機能のライティングは使わない
        m_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE); // 両面描画
        m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        m_pDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        m_pDevice->SetVertexDeclaration(m_pDeclaration.Get());
        m_pDevice->SetStreamSource(0, m_pVertexBuffer.Get(), 0, sizeof(Vertex));
        SetPipeline(PipelineKind::SCENE);
        return true;
    }

    //==============================================================================
    // --- 描画状態の切り替え処理 ---
    //==============================================================================
    void SetPipeline(PipelineKind kind) override {
        if (m_bSkipFrame) return;
        m_kind = kind; // Draw で三角形 / 線を判定するために覚えておく
        const bool bScene = kind == PipelineKind::SCENE || kind == PipelineKind::SCENE_WIREFRAME || kind == PipelineKind::SCENE_TRANSPARENT;
        const bool bTransparent = kind == PipelineKind::SCENE_TRANSPARENT;
        // 半透明だけアルファブレンド
        m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, bTransparent ? TRUE : FALSE);
        m_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        m_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        m_pDevice->SetRenderState(D3DRS_FILLMODE, kind == PipelineKind::SCENE_WIREFRAME ? D3DFILL_WIREFRAME : D3DFILL_SOLID);
        // シーンはプロジェクトのシェーダー、それ以外は組み込みの単色シェーダー
        m_pDevice->SetVertexShader(bScene ? m_pSceneVertexShader.Get() : m_pOverlayVertexShader.Get());
        m_pDevice->SetPixelShader(bScene ? m_pScenePixelShader.Get() : m_pOverlayPixelShader.Get());
        // 深度: オーバーレイはテストなし / 書き込みは不透明のシーンだけ
        m_pDevice->SetRenderState(D3DRS_ZENABLE, kind == PipelineKind::OVERLAY ? D3DZB_FALSE : D3DZB_TRUE);
        m_pDevice->SetRenderState(D3DRS_ZWRITEENABLE, bScene && !bTransparent ? TRUE : FALSE);
    }

    //==============================================================================
    // --- 描画処理 ---
    //==============================================================================
    void Draw(const ObjectConstants& constants, unsigned uVertexCount, unsigned uFirstVertex) override {
        if (m_bSkipFrame) return;
        // 定数レジスタへ書き込む（ファイル先頭の説明を参照）
        m_pDevice->SetVertexShaderConstantF(0, &constants.mvp.m[0][0], 4);
        m_pDevice->SetVertexShaderConstantF(4, &constants.color.x, 1);
        m_pDevice->SetPixelShaderConstantF(0, &constants.color.x, 1);
        m_pDevice->SetPixelShaderConstantF(4, &constants.color.x, 1);
        // D3D9 は頂点数ではなくプリミティブ数を渡す
        const bool bTriangles = m_kind == PipelineKind::SCENE || m_kind == PipelineKind::SCENE_WIREFRAME || m_kind == PipelineKind::SCENE_TRANSPARENT;
        const UINT uPrimitives = bTriangles ? uVertexCount / 3 : uVertexCount / 2;
        if (uPrimitives) m_pDevice->DrawPrimitive(bTriangles ? D3DPT_TRIANGLELIST : D3DPT_LINELIST, uFirstVertex, uPrimitives);
    }

    //==============================================================================
    // --- フレーム終了処理 ---
    //==============================================================================
    bool EndFrame(std::string& error) override {
        if (m_bSkipFrame) {
            Sleep(16); // 描けない間は CPU を空回りさせない
            return true;
        }
        m_pDevice->EndScene();
        const HRESULT hr = m_pDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (hr == D3DERR_DEVICELOST) {
            m_bDeviceLost = true; // 次のフレームでリセットを試す
            return true;
        }
        if (FAILED(hr)) return Fail(error, "Present failed", hr);
        return true;
    }

    //==============================================================================
    // --- 解放処理 ---
    //==============================================================================
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
    // --- エラー文を設定して false を返す処理 ---
    static bool Fail(std::string& error, const char* pAction, HRESULT hr) {
        error = FormatHResult(pAction, hr);
        return false;
    }

    //==============================================================================
    // --- コンパイル済みシェーダーからシェーダーオブジェクトを作る処理 ---
    //==============================================================================
    bool CreateShaderObjects(const ShaderPair& shaders, ComPtr<IDirect3DVertexShader9>& pVertex,
                             ComPtr<IDirect3DPixelShader9>& pPixel, std::string& error) {
        HRESULT hr = m_pDevice->CreateVertexShader(static_cast<const DWORD*>(shaders.pVertexShader->GetBufferPointer()), &pVertex);
        if (FAILED(hr)) return Fail(error, "Create SM3 vertex shader failed", hr);
        hr = m_pDevice->CreatePixelShader(static_cast<const DWORD*>(shaders.pPixelShader->GetBufferPointer()), &pPixel);
        if (FAILED(hr)) return Fail(error, "Create SM3 pixel shader failed", hr);
        return true;
    }

    //==============================================================================
    // --- シェーダーの作成処理 ---
    //==============================================================================
    bool CreateShaders(const std::wstring& sShaderPath, std::string& error) {
        // 組み込みの単色シェーダー（補助線用、およびプロジェクトのシェーダーの代わり）
        ShaderPair builtin;
        if (!CompileShaderSource(BUILTIN_FLAT_SHADER_SOURCE, SHADER_PROFILE_SM3, builtin, error)) return false;
        if (!CreateShaderObjects(builtin, m_pOverlayVertexShader, m_pOverlayPixelShader, error)) return false;

        // プロジェクトのシェーダー（SM3 で作れればそれを使う）
        ShaderPair project;
        std::string projectError;
        if (LoadShaderPair(sShaderPath, SHADER_PROFILE_SM3, project, projectError) &&
            CreateShaderObjects(project, m_pSceneVertexShader, m_pScenePixelShader, projectError)) {
            return true;
        }
        // 作れなかった: 単色シェーダーで代用して警告を出す
        Debug::LogWarning("Direct3D 9: the project shader is not available for Shader Model 3, "
                          "so the built-in flat shader is used. " + projectError);
        m_pSceneVertexShader = m_pOverlayVertexShader;
        m_pScenePixelShader = m_pOverlayPixelShader;
        return true;
    }

    ComPtr<IDirect3D9> m_pD3D;                              // Direct3D 9 本体
    ComPtr<IDirect3DDevice9> m_pDevice;                     // デバイス
    D3DPRESENT_PARAMETERS m_presentParameters{};            // 表示の設定（リセット時にも使う）
    ComPtr<IDirect3DVertexDeclaration9> m_pDeclaration;     // 頂点宣言
    ComPtr<IDirect3DVertexBuffer9> m_pVertexBuffer;         // 頂点バッファ
    ComPtr<IDirect3DVertexShader9> m_pSceneVertexShader;    // シーン用頂点シェーダー
    ComPtr<IDirect3DPixelShader9> m_pScenePixelShader;      // シーン用ピクセルシェーダー
    ComPtr<IDirect3DVertexShader9> m_pOverlayVertexShader;  // 補助線用頂点シェーダー
    ComPtr<IDirect3DPixelShader9> m_pOverlayPixelShader;    // 補助線用ピクセルシェーダー
    PipelineKind m_kind = PipelineKind::SCENE;              // 現在の描画状態
    bool m_bDeviceLost = false;                             // デバイスロスト中（リセット待ち）
    bool m_bSkipFrame = false;                              // このフレームは描画しない
};
} // namespace

//==================================================================================
// --- Direct3D 9 デバイスの生成処理 ---
//==================================================================================
std::unique_ptr<IGraphicsDevice> CreateDeviceD3D9() { return std::make_unique<DeviceD3D9>(); }

} // namespace dxs::graphics
