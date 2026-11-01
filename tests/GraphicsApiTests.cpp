//==================================================================================
//
// グラフィックス API のテスト [GraphicsApiTests.cpp]
//
// 同じシーン・エディターの補助表示・モデルを Direct3D 12 / 11 / 9 で描く。
// 大きさの変更と、ExportGame が書き出したコンパイル済みのシェーダーも確認する。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Engine.h"

#include <filesystem>
#include <iostream>

namespace {
//==================================================================================
// --- 数フレームの描画処理 ---
//==================================================================================
bool RenderFrames(dxs::Renderer& renderer, const std::vector<dxs::DrawItem>& vDraws, std::string& error) {
    for (const auto mode : {dxs::GizmoMode::Translate, dxs::GizmoMode::Rotate, dxs::GizmoMode::Scale}) {
        renderer.SetSelection({0, 1});
        renderer.SetMarquee(true, 10, 10, 120, 90);
        if (!renderer.Render(vDraws, error, 0, true, mode, true)) return false;
    }

    // シーンビューの描画モード（ワイヤーフレームはどのバックエンドにもある）
    for (const auto drawMode : {dxs::DrawMode::WIREFRAME, dxs::DrawMode::SHADED_WIREFRAME, dxs::DrawMode::SHADED}) {
        renderer.SetDrawMode(drawMode);
        if (renderer.GetDrawMode() != drawMode || !renderer.Render(vDraws, error, 0, true)) return false;
    }

    renderer.SetMarquee(false, 0, 0, 0, 0);
    return renderer.Render(vDraws, error); // 補助表示の無いゲームの描画
}
} // namespace

//==================================================================================
// --- メイン関数 ---
//==================================================================================
int main() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.lpszClassName = L"DxStudioGraphicsApiTest";
    if (!RegisterClassW(&windowClass)) return 1;
    HWND hWindow = CreateWindowW(windowClass.lpszClassName, L"Graphics API test", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr,
                                 nullptr, instance, nullptr);
    if (!hWindow) return 2;

    std::string error;
    dxs::Scene scene;
    if (!dxs::LoadScene(L"project/scene.dxs", scene, error)) {
        std::cerr << error << '\n';
        return 3;
    }

    auto pModel = std::make_shared<std::vector<DirectX::XMFLOAT3>>();
    pModel->push_back({-.5f, -.5f, 0});
    pModel->push_back({0, .5f, 0});
    pModel->push_back({.5f, -.5f, 0});
    std::vector<dxs::DrawItem> vDraws;
    dxs::DrawItem model;
    model.pVertices = pModel;
    vDraws.push_back(model);

    // 陰影付きのメッシュと半透明の物体で SCENE_TRANSPARENT を通す
    model.pShadeRuns = std::make_shared<std::vector<std::uint32_t>>(std::vector<std::uint32_t>{0, 3});
    model.color.w = 0.4f;
    vDraws.push_back(model);
    dxs::DrawItem glass;
    glass.color = {0.2f, 0.6f, 1.0f, 0.5f};
    vDraws.push_back(glass);
    for (const auto& entity : scene.vEntities) {
        vDraws.push_back(entity.draw);
    }

    const auto exportDir = std::filesystem::path(L"build-direct") / (L"api-export-" + std::to_wstring(GetCurrentProcessId()));
    if (!dxs::ExportGame(std::filesystem::current_path(), exportDir, scene, error)) {
        std::cerr << "Export: " << error << '\n';
        return 4;
    }

    if (!std::filesystem::is_regular_file(exportDir / L"Default9.vs.cso") ||
        !std::filesystem::is_regular_file(exportDir / L"Default9.ps.cso")) {
        std::cerr << "Export did not write Direct3D 9 shaders\n";
        return 5;
    }

    int nResult = 0;
    for (const auto api : dxs::ALL_GRAPHICS_APIS) {
        const char* pName = dxs::GraphicsApiDisplayName(api);
        for (const auto& shader : {std::filesystem::path(L"project/Default.hlsl"), exportDir / L"Default.cso"}) {
            dxs::Renderer renderer;
            if (!renderer.Initialize(hWindow, 640, 480, shader.wstring(), api, error)) {
                std::cerr << pName << " initialize (" << shader.string() << "): " << error << '\n';
                nResult = 10;
                continue;
            }

            if (renderer.Api() != api) {
                nResult = 11;
            }

            renderer.SetCamera(dxs::Camera{});
            if (!RenderFrames(renderer, vDraws, error)) {
                std::cerr << pName << " render: " << error << '\n';
                nResult = 12;
            }

            renderer.Resize(320, 240);
            if (!RenderFrames(renderer, vDraws, error)) {
                std::cerr << pName << " render after resize: " << error << '\n';
                nResult = 13;
            }

            // CPU での選択はバックエンドに依存しないこと
            dxs::DrawItem cube;
            if (renderer.Pick(160, 120, {cube}) != 0) {
                nResult = 14;
            }

            renderer.Shutdown();
        }

        std::cout << pName << " passed.\n";
    }

    dxs::Renderer fallback;
    if (!fallback.InitializeWithFallback(hWindow, 64, 64, L"project/Default.hlsl", dxs::GraphicsApi::DIRECT3D9, error) ||
        fallback.Api() != dxs::GraphicsApi::DIRECT3D9) {
        nResult = 15;
    }

    fallback.Shutdown();

    dxs::GraphicsApi parsed{};
    if (!dxs::ParseGraphicsApi("-dx11", parsed) || parsed != dxs::GraphicsApi::DIRECT3D11 ||
        !dxs::ParseGraphicsApi("D3D9", parsed) || parsed != dxs::GraphicsApi::DIRECT3D9 ||
        !dxs::ParseGraphicsApi("direct3d12", parsed) || parsed != dxs::GraphicsApi::DIRECT3D12 ||
        dxs::ParseGraphicsApi("dx10", parsed)) {
        nResult = 16;
    }

    DestroyWindow(hWindow);
    std::error_code ignored;
    std::filesystem::remove_all(exportDir, ignored);
    if (!nResult) {
        std::cout << "Direct3D 12 / 11 / 9 render, resize, export shaders and fallback tests passed.\n";
    }

    return nResult;
}
