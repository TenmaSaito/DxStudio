#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DirectXMath.h>
#include "Graphics/GraphicsApi.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dxs {

struct DrawItem {
    DirectX::XMFLOAT3 position{0, 0, 0};
    DirectX::XMFLOAT3 rotation{0, 0, 0};
    DirectX::XMFLOAT3 scale{1, 1, 1};
    DirectX::XMFLOAT4 color{1, 1, 1, 1};
    // Mesh choice only: true is a world-space XY quad, false is a cube.
    bool bIs2D = false;
    // False keeps the transform/selectable entity alive without submitting
    // polygon work to DirectX 12.
    bool bVisible = true;
    // Optional normalized triangle list used by ModelRenderer. Empty selects
    // the built-in cube/quad polygon geometry.
    std::shared_ptr<const std::vector<DirectX::XMFLOAT3>> pVertices;
    // Optional: pVertices is sorted into consecutive runs of equal brightness
    // (one count per run, darkest first). Gives meshes a faceted 3D look with
    // the flat-colour default shader. Empty draws the mesh in one colour.
    std::shared_ptr<const std::vector<std::uint32_t>> pShadeRuns;
};

// Shared scene camera. Target is in world units, angles in radians. Orthographic
// projection retains the same view direction and uses distance to control zoom.
struct Camera {
    DirectX::XMFLOAT3 target{0, 0, 0};
    float fYaw = 0.0f;
    float fPitch = 0.32175055f;
    float fDistance = 6.32455532f;
    bool bOrthographic = false;
    float fFieldOfView = 45.0f;
    float fNearClip = 0.1f;
    float fFarClip = 1000.0f;
};

// Editor-only transform handles: 0=X, 1=Y, 2=Z, 3/4/5=YZ/XZ/XY
// translation planes, 6=uniform scale.
enum class GizmoMode { Translate, Rotate, Scale };

// Scene view draw mode (Unity: Shaded / Wireframe / Shaded Wireframe).
enum class DrawMode : unsigned char { SHADED, WIREFRAME, SHADED_WIREFRAME };

// API-agnostic renderer front-end. The GPU backend (Direct3D 12 / 11 / 9) is
// chosen at Initialize time; every other member behaves identically on all
// backends (picking and gizmo math run on the CPU).
class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Backend used by the overload without an explicit GraphicsApi.
    static void SetDefaultApi(GraphicsApi api);
    static GraphicsApi DefaultApi();

    bool Initialize(HWND hwnd, unsigned width, unsigned height, const std::wstring& shaderPath, std::string& error);
    bool Initialize(HWND hwnd, unsigned width, unsigned height, const std::wstring& shaderPath, GraphicsApi api,
                    std::string& error);
    // Tries `preferred` first, then the remaining APIs (12 -> 11 -> 9).
    // `pReport` receives the failure reason of every API that was skipped.
    bool InitializeWithFallback(HWND hwnd, unsigned width, unsigned height, const std::wstring& shaderPath,
                                GraphicsApi preferred, std::string& error, std::string* pReport = nullptr);
    [[nodiscard]] bool IsInitialized() const;
    [[nodiscard]] GraphicsApi Api() const;

    void Resize(unsigned width, unsigned height);
    void SetCamera(const Camera& camera);
    // Corner X/Y/Z axis widget drawn in editor views (showGrid == true).
    void SetShowOrientationGizmo(bool bShow);
    // Applies to editor frames (showGrid == true); game frames are always shaded.
    void SetDrawMode(DrawMode mode);
    [[nodiscard]] DrawMode GetDrawMode() const;
    // Client coordinates relative to the viewport. Returns -1 for a miss.
    int Pick(int x, int y, const std::vector<DrawItem>& items) const;
    // Select objects whose projected bounds intersect the rectangle.
    std::vector<int> PickRect(int x0, int y0, int x1, int y1, const std::vector<DrawItem>& items) const;
    void SetSelection(const std::vector<int>& indices);
    void SetMarquee(bool visible, int x0, int y0, int x1, int y1);
    void SetGizmoHot(int handle);
    int HitGizmo(int x, int y, const DrawItem& item, GizmoMode mode) const;
    bool DragGizmo(const DrawItem& start, GizmoMode mode, int axis,
                   int startX, int startY, int x, int y, DrawItem& out) const;
    bool Render(const std::vector<DrawItem>& items, std::string& error,
                int selected = -1, bool showGrid = false,
                GizmoMode mode = GizmoMode::Translate, bool showGizmo = false);
    void Shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
