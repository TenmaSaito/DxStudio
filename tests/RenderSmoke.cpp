#include "Engine.h"
#include <cmath>
#include <iostream>

int main() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = L"DxStudioSmokeTest";
    if (!RegisterClassW(&wc)) return 1;
    HWND window = CreateWindowW(wc.lpszClassName, L"Smoke test", WS_OVERLAPPEDWINDOW,
        0, 0, 640, 480, nullptr, nullptr, instance, nullptr);
    if (!window) return 2;
    std::string error;
    dxs::Renderer renderer;
    if (!renderer.Initialize(window, 640, 480, L"project/Default.hlsl", error)) {
        std::cerr << "Renderer initialization: " << error << '\n'; return 3;
    }
    // Explicit front orthographic camera: handles are 90 pixels at the origin.
    // Quad geometry itself no longer selects an independent projection.
    dxs::Camera frontCamera;
    frontCamera.bOrthographic = true;
    frontCamera.fPitch = 0;
    renderer.SetCamera(frontCamera);
    // Verify hit tests,
    // drag mapping and a quarter-turn without depending on scene contents.
    dxs::DrawItem sprite; sprite.bIs2D = true;
    dxs::DrawItem changed;
    if (renderer.HitGizmo(350, 210, sprite, dxs::GizmoMode::Translate) != 5 ||
        !renderer.DragGizmo(sprite, dxs::GizmoMode::Translate, 5, 350, 210, 380, 190, changed) ||
        changed.position.x <= 0 || changed.position.y <= 0 || changed.position.z != 0) return 28;
    if (renderer.HitGizmo(320, 240, sprite, dxs::GizmoMode::Scale) != 6 ||
        !renderer.DragGizmo(sprite, dxs::GizmoMode::Scale, 6, 320, 240, 365, 195, changed) ||
        changed.scale.x < 1.9f || changed.scale.y < 1.9f || changed.scale.z < 1.9f) return 29;
    dxs::DrawItem cubePlane;
    if (renderer.HitGizmo(350, 210, cubePlane, dxs::GizmoMode::Translate) != 5 ||
        !renderer.DragGizmo(cubePlane, dxs::GizmoMode::Translate, 5, 350, 210, 380, 190, changed) ||
        changed.position.x <= 0 || changed.position.y <= 0 || std::abs(changed.position.z) > 0.0001f) return 33;
    dxs::DrawItem farSprite = sprite; farSprite.position.x = 3.0f;
    auto hits = renderer.PickRect(290,210,350,270,{sprite,farSprite});
    if (hits.size() != 1 || hits[0] != 0) return 30;
    hits = renderer.PickRect(500,280,280,200,{sprite,farSprite});
    if (hits.size() != 2 || hits[0] != 0 || hits[1] != 1) return 31;
    renderer.SetSelection({0,1}); renderer.SetMarquee(true, 280,200,500,280); renderer.SetGizmoHot(5);
    if (!renderer.Render({sprite,farSprite}, error, 0, true, dxs::GizmoMode::Translate, true)) return 32;
    renderer.SetSelection({}); renderer.SetMarquee(false,0,0,0,0); renderer.SetGizmoHot(-1);
    if (renderer.HitGizmo(365, 240, sprite, dxs::GizmoMode::Translate) != 0 ||
        !renderer.DragGizmo(sprite, dxs::GizmoMode::Translate, 0, 365, 240, 410, 240, changed) ||
        changed.position.x < 0.8f || changed.position.x > 1.1f || changed.position.y != 0) return 21;
    if (renderer.HitGizmo(320, 195, sprite, dxs::GizmoMode::Scale) != 1 ||
        !renderer.DragGizmo(sprite, dxs::GizmoMode::Scale, 1, 320, 195, 320, 150, changed) ||
        changed.scale.y < 1.4f || changed.scale.y > 1.6f) return 22;
    if (renderer.HitGizmo(410, 240, sprite, dxs::GizmoMode::Rotate) != 2 ||
        !renderer.DragGizmo(sprite, dxs::GizmoMode::Rotate, 2, 410, 240, 320, 150, changed) ||
        changed.rotation.z < 1.5f || changed.rotation.z > 1.7f) return 23;
    if (renderer.HitGizmo(10, 10, sprite, dxs::GizmoMode::Translate) != -1) return 24;
    sprite.rotation.z = DirectX::XM_PIDIV2;
    if (renderer.HitGizmo(320, 195, sprite, dxs::GizmoMode::Scale) != 0 ||
        !renderer.DragGizmo(sprite, dxs::GizmoMode::Scale, 0, 320, 195, 320, 150, changed) ||
        changed.scale.x < 1.4f || changed.scale.x > 1.6f) return 27;
    sprite.rotation.z = 0;
    dxs::DrawItem solid;
    if (renderer.HitGizmo(365, 240, solid, dxs::GizmoMode::Translate) != 0 ||
        !renderer.DragGizmo(solid, dxs::GizmoMode::Translate, 0, 365, 240, 410, 240, changed) || changed.position.x <= 0) return 25;
    for (auto mode : {dxs::GizmoMode::Translate, dxs::GizmoMode::Rotate, dxs::GizmoMode::Scale})
        if (!renderer.Render({sprite}, error, 0, true, mode, true)) return 26;
    // A world-space quad follows perspective depth and shares occlusion with
    // cubes. Its screen bounds differ from orthographic projection.
    dxs::Camera perspective = frontCamera;
    perspective.bOrthographic = false;
    renderer.SetCamera(perspective);
    dxs::DrawItem nearQuad; nearQuad.bIs2D = true; nearQuad.position.x = 1.2f;
    dxs::DrawItem farQuad = nearQuad; farQuad.position.z = 6;
    if (renderer.Pick(430, 240, {nearQuad}) != 0 || renderer.Pick(430, 240, {farQuad}) != -1) return 34;
    if (renderer.Pick(377, 240, {farQuad}) != 0 || renderer.Pick(377, 240, {nearQuad}) != -1) return 35;
    renderer.SetCamera(frontCamera);
    if (renderer.Pick(377, 240, {nearQuad}) != 0 || renderer.Pick(377, 240, {farQuad}) != 0 ||
        renderer.Pick(430, 240, {nearQuad}) != -1) return 36;
    renderer.SetCamera(perspective);
    dxs::DrawItem frontQuad; frontQuad.bIs2D = true; frontQuad.position.z = -1;
    if (renderer.Pick(320, 240, {solid, frontQuad}) != 1) return 37;
    frontQuad.position.z = 1;
    if (renderer.Pick(320, 240, {frontQuad, solid}) != 1) return 38;
    dxs::DrawItem tiltedQuad; tiltedQuad.bIs2D = true;
    if (renderer.Pick(355, 240, {tiltedQuad}) != 0) return 39;
    tiltedQuad.rotation.y = DirectX::XM_PI / 3;
    if (renderer.Pick(320, 240, {tiltedQuad}) != 0 || renderer.Pick(355, 240, {tiltedQuad}) != -1 ||
        !renderer.PickRect(352, 237, 356, 243, {tiltedQuad}).empty()) return 40;
    for (bool ortho : {false, true}) {
        perspective.bOrthographic = ortho;
        renderer.SetCamera(perspective);
        if (!renderer.Render({solid, tiltedQuad, farQuad}, error, 1, true, dxs::GizmoMode::Rotate, true)) return 41;
    }
    renderer.SetCamera(dxs::Camera{});
    // Test picking with both projections and transformed geometry, as well as
    // misses, changed camera target and resized viewport.
    dxs::DrawItem cube;
    cube.rotation.y = 0.4f;
    std::vector<dxs::DrawItem> one{cube};
    if (renderer.Pick(320, 240, one) != 0 || renderer.Pick(0, 0, one) != -1) return 11;
    one[0].bIs2D = true;
    one[0].rotation.z = 0.3f;
    if (renderer.Pick(320, 240, one) != 0 || renderer.Pick(0, 0, one) != -1) return 12;
    dxs::Camera camera;
    camera.target.x = 2.0f;
    renderer.SetCamera(camera);
    one[0].position.x = 2.0f;
    if (renderer.Pick(320, 240, one) != 0) return 13;
    one[0].bIs2D = false;
    if (renderer.Pick(320, 240, one) != 0) return 14;
    renderer.Resize(320, 240);
    if (renderer.Pick(160, 120, one) != 0) return 15;
    if (!renderer.Render(one, error, 0, true)) { std::cerr << "Grid/outline: " << error << '\n'; return 16; }
    one[0].bIs2D = true;
    if (!renderer.Render(one, error, 0, true)) { std::cerr << "2D grid/outline: " << error << '\n'; return 17; }
    renderer.SetCamera(dxs::Camera{});
    dxs::Scene scene;
    if (!dxs::LoadScene(L"project/scene.dxs", scene, error)) { std::cerr << error << '\n'; return 4; }
    std::vector<dxs::DrawItem> draws;
    auto modelVertices=std::make_shared<std::vector<DirectX::XMFLOAT3>>();
    modelVertices->push_back({-.5f,-.5f,0});modelVertices->push_back({0,.5f,0});modelVertices->push_back({.5f,-.5f,0});
    dxs::DrawItem modelItem;modelItem.pVertices=modelVertices;modelItem.position.x=-1.5f;draws.push_back(modelItem);
    dxs::DrawItem hidden;hidden.bVisible=false;draws.push_back(hidden);
    for (const auto& e : scene.vEntities) draws.push_back(e.draw);
    if (!renderer.Render(draws, error)) { std::cerr << error << '\n'; return 5; }
    renderer.Resize(320, 240);
    if (!renderer.Render(draws, error)) { std::cerr << error << '\n'; return 6; }
    renderer.Shutdown();
    const auto exportDir = std::filesystem::path(L"build-direct") / (L"test-export-" + std::to_wstring(GetCurrentProcessId()));
    if (!dxs::ExportGame(std::filesystem::current_path(), exportDir, scene, error)) {
        std::cerr << "Export: " << error << '\n'; return 7;
    }
    dxs::Scene exported;
    if (!dxs::LoadScene(exportDir / L"scene.dxs", exported, error) || exported.vEntities.size() != scene.vEntities.size()) return 8;
    if (!std::filesystem::is_regular_file(exportDir / L"Game.exe") ||
        !std::filesystem::is_regular_file(exportDir / L"Default.vs.cso") ||
        !std::filesystem::is_regular_file(exportDir / L"Default.ps.cso") ||
        std::filesystem::exists(exportDir / L"Default.hlsl")) return 9;
    if (!renderer.Initialize(window, 320, 240, (exportDir / L"Default.cso").wstring(), error) ||
        !renderer.Render(draws, error)) { std::cerr << "Compiled shader: " << error << '\n'; return 10; }
    renderer.Shutdown();
    DestroyWindow(window);
    for (const auto* file : {L"Game.exe", L"scene.dxs", L"Default.vs.cso", L"Default.ps.cso", L"Default9.vs.cso", L"Default9.ps.cso", L"ProjectSettings.dxsettings"})
        std::filesystem::remove(exportDir / file);
    std::filesystem::remove_all(exportDir / L"Assets");
    std::filesystem::remove(exportDir);
    std::cout << "DirectX 12 render, shared perspective/orthographic camera, world quads, picking, gizmos, grid, resize and source-free export smoke test passed.\n";
    return 0;
}
