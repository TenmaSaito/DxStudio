// Renderer front-end: camera math, picking, gizmo interaction and the editor
// overlay draw order. All GPU work goes through graphics::IGraphicsDevice, so
// the same code drives Direct3D 12, 11 and 9 (see Graphics/GraphicsDevice.h).

#include "Renderer.h"
#include "Graphics/GraphicsDevice.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>

using namespace DirectX;

namespace dxs {
namespace {
using graphics::ObjectConstants;
using graphics::PipelineKind;
using graphics::Vertex;

constexpr std::size_t MAX_VERTEX_COUNT = 1u << 20;
constexpr float CLEAR_COLOR[4] = {0.16f, 0.31f, 0.50f, 1.0f}; // procedural sky backdrop

// ---- Static geometry layout --------------------------------------------------
// Cube: six independently specified faces, followed by a quad in the XY plane.
constexpr Vertex BASE_VERTICES[] = {
    {-0.5f,-0.5f,-0.5f},{-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},
    {-0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},
    { 0.5f,-0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},
    { 0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f,-0.5f, 0.5f},
    {-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f, 0.5f,-0.5f},
    {-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f,-0.5f},{-0.5f,-0.5f,-0.5f},
    { 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f},
    { 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f},
    {-0.5f, 0.5f,-0.5f},{-0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},
    {-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f,-0.5f},
    {-0.5f,-0.5f, 0.5f},{-0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},
    {-0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f, 0.5f},
    {-0.5f,-0.5f,0},{-0.5f, 0.5f,0},{0.5f, 0.5f,0},
    {-0.5f,-0.5f,0},{ 0.5f, 0.5f,0},{0.5f,-0.5f,0},
};

constexpr UINT CUBE_START = 0;
constexpr UINT QUAD_START = 36;
constexpr UINT GRID_START = 42;
constexpr UINT GRID_COUNT = 84; // 21 lines in each direction, two endpoints each
constexpr UINT AXIS_X_START = GRID_START + GRID_COUNT;
constexpr UINT AXIS_Z_START = AXIS_X_START + 2;
constexpr UINT CUBE_LINES_START = AXIS_Z_START + 2;
constexpr UINT QUAD_LINES_START = CUBE_LINES_START + 24;
constexpr UINT GIZMO_ARROW_START = QUAD_LINES_START + 8;
constexpr UINT GIZMO_SCALE_START = GIZMO_ARROW_START + 6;
constexpr UINT GIZMO_RING_START = GIZMO_SCALE_START + 10;
constexpr UINT GIZMO_RING_SEGMENTS = 64;
constexpr UINT GIZMO_PLANE_START = GIZMO_RING_START + GIZMO_RING_SEGMENTS * 2;
constexpr UINT GIZMO_CENTER_START = GIZMO_PLANE_START + 8;
constexpr UINT MARQUEE_START = GIZMO_CENTER_START + 8;
// Scene-view orientation gizmo letters (Unity/UE corner axis widget).
constexpr UINT LETTER_X_START = MARQUEE_START + 8;
constexpr UINT LETTER_Y_START = LETTER_X_START + 4;
constexpr UINT LETTER_Z_START = LETTER_Y_START + 6;
constexpr UINT LETTER_X_COUNT = 4, LETTER_Y_COUNT = 6, LETTER_Z_COUNT = 6;

std::vector<Vertex> BuildVertices() {
    std::vector<Vertex> v(std::begin(BASE_VERTICES), std::end(BASE_VERTICES));
    for (int nLine = -10; nLine <= 10; ++nLine) {
        const float n = static_cast<float>(nLine);
        v.push_back({n, 0, -10}); v.push_back({n, 0, 10});
        v.push_back({-10, 0, n}); v.push_back({10, 0, n});
    }
    v.push_back({-10, 0, 0}); v.push_back({10, 0, 0});
    v.push_back({0, 0, -10}); v.push_back({0, 0, 10});
    constexpr Vertex CORNERS[] = {
        {-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},
        {-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},{0.5f,0.5f,0.5f},{-0.5f,0.5f,0.5f}
    };
    constexpr int EDGES[][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (const auto& edge : EDGES) { v.push_back(CORNERS[edge[0]]); v.push_back(CORNERS[edge[1]]); }
    v.push_back({-0.5f,-0.5f,0}); v.push_back({0.5f,-0.5f,0});
    v.push_back({0.5f,-0.5f,0}); v.push_back({0.5f,0.5f,0});
    v.push_back({0.5f,0.5f,0}); v.push_back({-0.5f,0.5f,0});
    v.push_back({-0.5f,0.5f,0}); v.push_back({-0.5f,-0.5f,0});
    // A shaft along +X, followed by a small arrow and a scale-box tip.
    v.push_back({0,0,0}); v.push_back({1,0,0});
    v.push_back({1,0,0}); v.push_back({0.80f,0.09f,0});
    v.push_back({1,0,0}); v.push_back({0.80f,-0.09f,0});
    v.push_back({0,0,0}); v.push_back({1,0,0});
    v.push_back({0.91f,-0.06f,0}); v.push_back({1.03f,-0.06f,0});
    v.push_back({1.03f,-0.06f,0}); v.push_back({1.03f,0.06f,0});
    v.push_back({1.03f,0.06f,0}); v.push_back({0.91f,0.06f,0});
    v.push_back({0.91f,0.06f,0}); v.push_back({0.91f,-0.06f,0});
    for (UINT uSegment = 0; uSegment < GIZMO_RING_SEGMENTS; ++uSegment) {
        const float fA = XM_2PI * uSegment / GIZMO_RING_SEGMENTS;
        const float fB = XM_2PI * (uSegment + 1) / GIZMO_RING_SEGMENTS;
        v.push_back({std::cos(fA), std::sin(fA), 0});
        v.push_back({std::cos(fB), std::sin(fB), 0});
    }
    auto square = [&](float fLo, float fHi) {
        v.push_back({fLo,fLo,0}); v.push_back({fHi,fLo,0});
        v.push_back({fHi,fLo,0}); v.push_back({fHi,fHi,0});
        v.push_back({fHi,fHi,0}); v.push_back({fLo,fHi,0});
        v.push_back({fLo,fHi,0}); v.push_back({fLo,fLo,0});
    };
    square(0.23f, 0.45f); // plane movement handle
    square(-0.08f, 0.08f); // uniform-scale handle
    square(0, 1); // screen-space rectangle
    // Letters drawn in a [-1, 1] box.
    v.push_back({-1,-1,0}); v.push_back({1,1,0}); v.push_back({-1,1,0}); v.push_back({1,-1,0});            // X
    v.push_back({-1,1,0}); v.push_back({0,0,0}); v.push_back({1,1,0}); v.push_back({0,0,0});
    v.push_back({0,0,0}); v.push_back({0,-1,0});                                                          // Y
    v.push_back({-1,1,0}); v.push_back({1,1,0}); v.push_back({1,1,0}); v.push_back({-1,-1,0});
    v.push_back({-1,-1,0}); v.push_back({1,-1,0});                                                        // Z
    return v;
}

// ---- Camera and projection math ------------------------------------------------
XMMATRIX World(const DrawItem& item) {
    return XMMatrixScaling(item.scale.x, item.scale.y, item.scale.z) *
        XMMatrixRotationRollPitchYaw(item.rotation.x, item.rotation.y, item.rotation.z) *
        XMMatrixTranslation(item.position.x, item.position.y, item.position.z);
}

XMMATRIX View(const Camera& camera) {
    const XMVECTOR target = XMLoadFloat3(&camera.target);
    const float fCosPitch = std::cos(camera.pitch);
    const XMVECTOR offset = XMVectorSet(std::sin(camera.yaw) * fCosPitch * camera.distance,
        std::sin(camera.pitch) * camera.distance, -std::cos(camera.yaw) * fCosPitch * camera.distance, 0);
    return XMMatrixLookAtLH(XMVectorAdd(target, offset), target, XMVectorSet(0, 1, 0, 0));
}

XMMATRIX ViewProjection(const Camera& camera, float fAspect) {
    const float fHeight = camera.distance * (10.0f / 6.32455532f);
    const auto projection = camera.orthographic
        ? XMMatrixOrthographicLH(fHeight * fAspect, fHeight, camera.nearClip, camera.farClip)
        : XMMatrixPerspectiveFovLH(XMConvertToRadians(camera.fieldOfView), fAspect, camera.nearClip, camera.farClip);
    return View(camera) * projection;
}

XMMATRIX AxisRotation(int nAxis) {
    return nAxis == 0 ? XMMatrixIdentity() : nAxis == 1 ? XMMatrixRotationZ(XM_PIDIV2) : XMMatrixRotationY(-XM_PIDIV2);
}
XMMATRIX RingRotation(int nAxis) {
    return nAxis == 0 ? XMMatrixRotationY(XM_PIDIV2) : nAxis == 1 ? XMMatrixRotationX(-XM_PIDIV2) : XMMatrixIdentity();
}
XMMATRIX GizmoAxisRotation(const DrawItem& item, int nAxis, GizmoMode mode) {
    // Scaling changes the object's local axes, while position is in world space.
    const auto basis = AxisRotation(nAxis);
    return mode == GizmoMode::Scale ? basis * XMMatrixRotationRollPitchYaw(item.rotation.x, item.rotation.y, item.rotation.z) : basis;
}
bool GizmoLength(const Camera& camera, const DrawItem& item, unsigned uWidth, unsigned uHeight, float& fLength) {
    if (!uWidth || !uHeight) return false;
    const auto vp = ViewProjection(camera, static_cast<float>(uWidth) / uHeight);
    const auto clip = XMVector4Transform(XMVectorSet(item.position.x, item.position.y, item.position.z, 1), vp);
    const float fW = XMVectorGetW(clip), fZ = XMVectorGetZ(clip);
    if (!std::isfinite(fW) || fW <= 0 || fZ < 0 || fZ > fW) return false;
    const float fWorldHeight = camera.orthographic ? camera.distance * (10.0f / 6.32455532f) : 2.0f * std::tan(XM_PIDIV4 / 2) * fW;
    fLength = fWorldHeight * 90.0f / uHeight;
    return std::isfinite(fLength) && fLength > 0;
}
bool ScreenPoint(XMVECTOR point, FXMMATRIX mvp, unsigned uWidth, unsigned uHeight, XMFLOAT2& result) {
    const auto clip = XMVector4Transform(point, mvp);
    const float fW = XMVectorGetW(clip), fZ = XMVectorGetZ(clip);
    if (!std::isfinite(fW) || fW <= 0 || fZ < 0 || fZ > fW) return false;
    result = {(XMVectorGetX(clip) / fW + 1.0f) * 0.5f * uWidth,
              (1.0f - XMVectorGetY(clip) / fW) * 0.5f * uHeight};
    return std::isfinite(result.x) && std::isfinite(result.y);
}
float SegmentDistance(float fX, float fY, XMFLOAT2 a, XMFLOAT2 b) {
    const float fDx = b.x - a.x, fDy = b.y - a.y, fLength2 = fDx * fDx + fDy * fDy;
    if (fLength2 < 1e-5f) return std::numeric_limits<float>::infinity();
    const float fT = std::clamp(((fX - a.x) * fDx + (fY - a.y) * fDy) / fLength2, 0.0f, 1.0f);
    const float fPx = fX - (a.x + fT * fDx), fPy = fY - (a.y + fT * fDy);
    return std::sqrt(fPx * fPx + fPy * fPy);
}
bool PlaneHit(int nX, int nY, unsigned uWidth, unsigned uHeight, FXMMATRIX vp,
              XMVECTOR origin, XMVECTOR normal, XMVECTOR& hit) {
    const float fSx = 2.0f * nX / uWidth - 1.0f, fSy = 1.0f - 2.0f * nY / uHeight;
    XMVECTOR determinant;
    const auto inverse = XMMatrixInverse(&determinant, vp);
    if (std::abs(XMVectorGetX(determinant)) < 1e-12f) return false;
    const auto nearPoint = XMVector3TransformCoord(XMVectorSet(fSx, fSy, 0, 1), inverse);
    const auto farPoint = XMVector3TransformCoord(XMVectorSet(fSx, fSy, 1, 1), inverse);
    const auto ray = XMVectorSubtract(farPoint, nearPoint);
    const float fDenominator = XMVectorGetX(XMVector3Dot(ray, normal));
    if (std::abs(fDenominator) < 1e-7f) return false;
    const float fT = XMVectorGetX(XMVector3Dot(XMVectorSubtract(origin, nearPoint), normal)) / fDenominator;
    if (!std::isfinite(fT)) return false;
    hit = XMVectorSubtract(XMVectorAdd(nearPoint, XMVectorScale(ray, fT)), origin);
    return std::isfinite(XMVectorGetX(hit)) && std::isfinite(XMVectorGetY(hit)) && std::isfinite(XMVectorGetZ(hit));
}
bool PointInQuad(float fX, float fY, const XMFLOAT2 (&aQuad)[4]) {
    bool bPositive = false, bNegative = false;
    for (int nIndex = 0; nIndex < 4; ++nIndex) {
        const auto& a = aQuad[nIndex];
        const auto& b = aQuad[(nIndex + 1) % 4];
        const float fCross = (b.x - a.x) * (fY - a.y) - (b.y - a.y) * (fX - a.x);
        bPositive |= fCross > 0.001f;
        bNegative |= fCross < -0.001f;
    }
    return !(bPositive && bNegative);
}

ObjectConstants MakeConstants(FXMMATRIX mvp, const XMFLOAT4& color) {
    ObjectConstants constants{};
    XMStoreFloat4x4(&constants.mvp, mvp);
    constants.color = color;
    return constants;
}

constexpr XMFLOAT4 AXIS_COLORS[] = {{1, 0.24f, 0.22f, 1}, {0.28f, 0.9f, 0.28f, 1}, {0.28f, 0.52f, 1, 1}};
constexpr XMFLOAT4 HOT_COLOR{1, 0.88f, 0.15f, 1};
GraphicsApi g_defaultApi = GraphicsApi::DIRECT3D12;
} // namespace

// ---- Renderer state --------------------------------------------------------------
struct Renderer::Impl {
    std::unique_ptr<graphics::IGraphicsDevice> pDevice;
    unsigned uWidth = 0, uHeight = 0;
    UINT uBaseVertexCount = 0;
    Camera camera{};
    std::vector<int> vSelection;
    bool bMarquee = false;
    int nRectX0 = 0, nRectY0 = 0, nRectX1 = 0, nRectY1 = 0;
    int nGizmoHot = -1;
    bool bShowOrientation = true;
    DrawMode drawMode = DrawMode::SHADED;
    std::string sResizeError;
};

Renderer::Renderer() = default;
Renderer::~Renderer() { Shutdown(); }

void Renderer::SetDefaultApi(GraphicsApi api) { g_defaultApi = api; }
GraphicsApi Renderer::DefaultApi() { return g_defaultApi; }

bool Renderer::Initialize(HWND hwnd, unsigned width, unsigned height, const std::wstring& shaderPath, std::string& error) {
    return Initialize(hwnd, width, height, shaderPath, g_defaultApi, error);
}

bool Renderer::Initialize(HWND hwnd, unsigned width, unsigned height, const std::wstring& shaderPath, GraphicsApi api,
                          std::string& error) {
    Shutdown();
    error.clear();
    if (!hwnd || !width || !height) {
        error = "Invalid viewport dimensions or window";
        return false;
    }
    auto pImpl = std::make_unique<Impl>();
    pImpl->uWidth = width;
    pImpl->uHeight = height;
    pImpl->pDevice = graphics::CreateGraphicsDevice(api);
    if (!pImpl->pDevice) {
        error = std::string(GraphicsApiDisplayName(api)) + " backend is not available in this build";
        return false;
    }
    const auto vVertices = BuildVertices();
    pImpl->uBaseVertexCount = static_cast<UINT>(vVertices.size());
    graphics::DeviceCreateInfo info;
    info.hWnd = hwnd;
    info.uWidth = width;
    info.uHeight = height;
    info.sShaderPath = shaderPath;
    info.pStaticVertices = vVertices.data();
    info.nStaticVertexCount = vVertices.size();
    info.nMaxVertexCount = MAX_VERTEX_COUNT;
    if (!pImpl->pDevice->Initialize(info, error)) {
        error = std::string(GraphicsApiDisplayName(api)) + ": " + error;
        pImpl->pDevice->Shutdown();
        return false;
    }
    p_ = std::move(pImpl);
    return true;
}

bool Renderer::InitializeWithFallback(HWND hwnd, unsigned width, unsigned height, const std::wstring& shaderPath,
                                      GraphicsApi preferred, std::string& error, std::string* pReport) {
    std::string sReport;
    std::vector<GraphicsApi> vOrder{preferred};
    for (const auto api : ALL_GRAPHICS_APIS) {
        if (api != preferred) vOrder.push_back(api);
    }
    for (const auto api : vOrder) {
        std::string sAttemptError;
        if (Initialize(hwnd, width, height, shaderPath, api, sAttemptError)) {
            if (pReport) *pReport = sReport;
            error.clear();
            return true;
        }
        sReport += sAttemptError + "\n";
        if (error.empty()) error = sAttemptError; // keep the preferred API's reason
    }
    if (pReport) *pReport = sReport;
    return false;
}

bool Renderer::IsInitialized() const { return static_cast<bool>(p_); }
GraphicsApi Renderer::Api() const { return p_ ? p_->pDevice->Api() : g_defaultApi; }

void Renderer::Resize(unsigned width, unsigned height) {
    if (!p_ || !width || !height || (p_->uWidth == width && p_->uHeight == height)) return;
    p_->sResizeError.clear();
    if (p_->pDevice->Resize(width, height, p_->sResizeError)) {
        p_->uWidth = width;
        p_->uHeight = height;
    }
}

void Renderer::SetCamera(const Camera& camera) {
    if (!p_ || !std::isfinite(camera.distance) || !std::isfinite(camera.yaw) ||
        !std::isfinite(camera.pitch) || !std::isfinite(camera.target.x) ||
        !std::isfinite(camera.target.y) || !std::isfinite(camera.target.z)) return;
    p_->camera = camera;
    p_->camera.distance = std::clamp(camera.distance, 0.2f, 80.0f);
    p_->camera.pitch = std::clamp(camera.pitch, -1.54f, 1.54f);
}

void Renderer::SetDrawMode(DrawMode mode) {
    if (p_) p_->drawMode = mode;
}
DrawMode Renderer::GetDrawMode() const { return p_ ? p_->drawMode : DrawMode::SHADED; }

void Renderer::SetShowOrientationGizmo(bool bShow) {
    if (p_) p_->bShowOrientation = bShow;
}

// ---- Picking ---------------------------------------------------------------------
int Renderer::Pick(int x, int y, const std::vector<DrawItem>& items) const {
    if (!p_ || !p_->uWidth || !p_->uHeight || x < 0 || y < 0 ||
        x >= static_cast<int>(p_->uWidth) || y >= static_cast<int>(p_->uHeight)) return -1;
    const float fSx = (2.0f * (x + 0.5f) / p_->uWidth) - 1.0f;
    const float fSy = 1.0f - (2.0f * (y + 0.5f) / p_->uHeight);
    const float fAspect = static_cast<float>(p_->uWidth) / p_->uHeight;
    float fBestDepth = std::numeric_limits<float>::infinity();
    int nBest = -1;
    for (size_t nIndex = 0; nIndex < items.size(); ++nIndex) {
        const auto& item = items[nIndex];
        if (!item.visible) continue;
        const XMMATRIX mvp = World(item) * ViewProjection(p_->camera, fAspect);
        XMVECTOR determinant;
        const XMMATRIX inverse = XMMatrixInverse(&determinant, mvp);
        if (!std::isfinite(XMVectorGetX(determinant)) || std::abs(XMVectorGetX(determinant)) < 1e-12f) continue;
        XMFLOAT3 origin, end;
        XMStoreFloat3(&origin, XMVector3TransformCoord(XMVectorSet(fSx, fSy, 0, 1), inverse));
        XMStoreFloat3(&end, XMVector3TransformCoord(XMVectorSet(fSx, fSy, 1, 1), inverse));
        const float aOrigin[] = {origin.x, origin.y, origin.z};
        const float aDirection[] = {end.x - origin.x, end.y - origin.y, end.z - origin.z};
        // Slab test against the unit cube (or the z = 0 slab of a quad).
        float fMin = 0.0f, fMax = 1.0f;
        bool bHit = true;
        for (int nAxis = 0; nAxis < 3; ++nAxis) {
            const float fLo = item.is2D && nAxis == 2 ? 0.0f : -0.5f;
            const float fHi = item.is2D && nAxis == 2 ? 0.0f : 0.5f;
            if (std::abs(aDirection[nAxis]) < 1e-8f) {
                if (aOrigin[nAxis] < fLo || aOrigin[nAxis] > fHi) { bHit = false; break; }
            } else {
                float fA = (fLo - aOrigin[nAxis]) / aDirection[nAxis], fB = (fHi - aOrigin[nAxis]) / aDirection[nAxis];
                if (fA > fB) std::swap(fA, fB);
                fMin = std::max(fMin, fA);
                fMax = std::min(fMax, fB);
                if (fMin > fMax) { bHit = false; break; }
            }
        }
        if (!bHit) continue;
        const XMVECTOR local = XMVectorSet(aOrigin[0] + aDirection[0] * fMin, aOrigin[1] + aDirection[1] * fMin,
                                           aOrigin[2] + aDirection[2] * fMin, 1);
        const XMVECTOR clip = XMVector4Transform(local, mvp);
        const float fW = XMVectorGetW(clip);
        if (fW <= 0) continue;
        const float fDepth = XMVectorGetZ(clip) / fW;
        if (fDepth >= 0 && fDepth <= 1 && fDepth < fBestDepth) {
            fBestDepth = fDepth;
            nBest = static_cast<int>(nIndex);
        }
    }
    return nBest;
}

std::vector<int> Renderer::PickRect(int x0, int y0, int x1, int y1, const std::vector<DrawItem>& items) const {
    std::vector<int> vResult;
    if (!p_ || !p_->uWidth || !p_->uHeight) return vResult;
    const int nWidth = static_cast<int>(p_->uWidth), nHeight = static_cast<int>(p_->uHeight);
    const float fLeft = static_cast<float>(std::clamp(std::min(x0, x1), 0, nWidth));
    const float fRight = static_cast<float>(std::clamp(std::max(x0, x1), 0, nWidth));
    const float fTop = static_cast<float>(std::clamp(std::min(y0, y1), 0, nHeight));
    const float fBottom = static_cast<float>(std::clamp(std::max(y0, y1), 0, nHeight));
    const float fAspect = static_cast<float>(p_->uWidth) / p_->uHeight;
    for (size_t nIndex = 0; nIndex < items.size(); ++nIndex) {
        const auto& item = items[nIndex];
        if (!item.visible) continue;
        const auto mvp = World(item) * ViewProjection(p_->camera, fAspect);
        float fMinX = std::numeric_limits<float>::infinity(), fMinY = fMinX;
        float fMaxX = -fMinX, fMaxY = -fMinX;
        bool bProjected = false;
        for (int nZ = 0; nZ < (item.is2D ? 1 : 2); ++nZ)
            for (int nY = 0; nY < 2; ++nY)
                for (int nX = 0; nX < 2; ++nX) {
                    XMFLOAT2 point;
                    if (!ScreenPoint(XMVectorSet(nX ? 0.5f : -0.5f, nY ? 0.5f : -0.5f,
                                                 item.is2D ? 0.0f : (nZ ? 0.5f : -0.5f), 1),
                                     mvp, p_->uWidth, p_->uHeight, point)) continue;
                    bProjected = true;
                    fMinX = std::min(fMinX, point.x); fMaxX = std::max(fMaxX, point.x);
                    fMinY = std::min(fMinY, point.y); fMaxY = std::max(fMaxY, point.y);
                }
        if (bProjected && fMaxX >= fLeft && fMinX <= fRight && fMaxY >= fTop && fMinY <= fBottom)
            vResult.push_back(static_cast<int>(nIndex));
    }
    return vResult;
}

void Renderer::SetSelection(const std::vector<int>& indices) { if (p_) p_->vSelection = indices; }
void Renderer::SetMarquee(bool visible, int x0, int y0, int x1, int y1) {
    if (!p_) return;
    p_->bMarquee = visible;
    p_->nRectX0 = x0; p_->nRectY0 = y0; p_->nRectX1 = x1; p_->nRectY1 = y1;
}
void Renderer::SetGizmoHot(int handle) { if (p_) p_->nGizmoHot = handle; }

// ---- Gizmo interaction -----------------------------------------------------------
int Renderer::HitGizmo(int x, int y, const DrawItem& item, GizmoMode mode) const {
    if (!p_ || x < 0 || y < 0 || x >= static_cast<int>(p_->uWidth) || y >= static_cast<int>(p_->uHeight)) return -1;
    const unsigned uWidth = p_->uWidth, uHeight = p_->uHeight;
    float fLength;
    if (!GizmoLength(p_->camera, item, uWidth, uHeight, fLength)) return -1;
    const auto vp = ViewProjection(p_->camera, static_cast<float>(uWidth) / uHeight);
    const auto base = XMMatrixScaling(fLength, fLength, fLength) * XMMatrixTranslation(item.position.x, item.position.y, item.position.z) * vp;
    // Center box uniformly scales all dimensions. Plane handles take priority
    // over shafts, while the remaining shafts remain easy to pick.
    if (mode == GizmoMode::Scale) {
        XMFLOAT2 center;
        if (ScreenPoint(XMVectorSet(0, 0, 0, 1), base, uWidth, uHeight, center) &&
            std::hypot(center.x - x, center.y - y) <= 9.0f) return 6;
    }
    if (mode == GizmoMode::Translate) {
        for (int nNormal = 0; nNormal < 3; ++nNormal) {
            const auto mvp = RingRotation(nNormal) * base;
            XMFLOAT2 aQuad[4]{};
            const XMVECTOR aPoints[] = {XMVectorSet(.23f,.23f,0,1), XMVectorSet(.45f,.23f,0,1),
                                        XMVectorSet(.45f,.45f,0,1), XMVectorSet(.23f,.45f,0,1)};
            bool bValid = true;
            for (int nCorner = 0; nCorner < 4; ++nCorner) bValid &= ScreenPoint(aPoints[nCorner], mvp, uWidth, uHeight, aQuad[nCorner]);
            const float fArea = std::abs((aQuad[1].x - aQuad[0].x) * (aQuad[3].y - aQuad[0].y) -
                                         (aQuad[1].y - aQuad[0].y) * (aQuad[3].x - aQuad[0].x));
            if (bValid && fArea > 20.0f && PointInQuad(static_cast<float>(x), static_cast<float>(y), aQuad)) return 3 + nNormal;
        }
    }
    float fBest = 9.0f;
    int nSelected = -1;
    for (int nAxis = 0; nAxis < 3; ++nAxis) {
        const auto mvp = (mode == GizmoMode::Rotate ? RingRotation(nAxis) : GizmoAxisRotation(item, nAxis, mode)) * base;
        if (mode == GizmoMode::Rotate) {
            XMFLOAT2 center, u, v;
            if (!ScreenPoint(XMVectorSet(0,0,0,1), mvp, uWidth, uHeight, center) ||
                !ScreenPoint(XMVectorSet(1,0,0,1), mvp, uWidth, uHeight, u) ||
                !ScreenPoint(XMVectorSet(0,1,0,1), mvp, uWidth, uHeight, v)) continue;
            // Skip rings seen edge-on: they cannot be dragged meaningfully.
            if (std::abs((u.x - center.x) * (v.y - center.y) - (u.y - center.y) * (v.x - center.x)) < 64.0f) continue;
            for (UINT uSegment = 0; uSegment < GIZMO_RING_SEGMENTS; ++uSegment) {
                const float fA = XM_2PI * uSegment / GIZMO_RING_SEGMENTS, fB = XM_2PI * (uSegment + 1) / GIZMO_RING_SEGMENTS;
                XMFLOAT2 pointA, pointB;
                if (!ScreenPoint(XMVectorSet(std::cos(fA), std::sin(fA), 0, 1), mvp, uWidth, uHeight, pointA) ||
                    !ScreenPoint(XMVectorSet(std::cos(fB), std::sin(fB), 0, 1), mvp, uWidth, uHeight, pointB)) continue;
                const float fDistance = SegmentDistance(static_cast<float>(x), static_cast<float>(y), pointA, pointB);
                if (fDistance < fBest) { fBest = fDistance; nSelected = nAxis; }
            }
        } else {
            XMFLOAT2 pointA, pointB;
            if (!ScreenPoint(XMVectorSet(0.20f,0,0,1), mvp, uWidth, uHeight, pointA) ||
                !ScreenPoint(XMVectorSet(1,0,0,1), mvp, uWidth, uHeight, pointB)) continue;
            if (std::hypot(pointB.x - pointA.x, pointB.y - pointA.y) < 8.0f) continue;
            const float fDistance = SegmentDistance(static_cast<float>(x), static_cast<float>(y), pointA, pointB);
            if (fDistance < fBest) { fBest = fDistance; nSelected = nAxis; }
        }
    }
    return nSelected;
}

bool Renderer::DragGizmo(const DrawItem& start, GizmoMode mode, int axis,
                         int startX, int startY, int x, int y, DrawItem& out) const {
    if (!p_ || axis < 0 || axis > 6) return false;
    if (axis >= 3 && axis <= 5 && mode != GizmoMode::Translate) return false;
    if (axis == 6 && mode != GizmoMode::Scale) return false;
    const unsigned uWidth = p_->uWidth, uHeight = p_->uHeight;
    float fLength;
    if (!GizmoLength(p_->camera, start, uWidth, uHeight, fLength)) return false;
    const auto vp = ViewProjection(p_->camera, static_cast<float>(uWidth) / uHeight);
    out = start;
    auto axisNormal = [](int nAxis) {
        return nAxis == 0 ? XMVectorSet(1, 0, 0, 0) : nAxis == 1 ? XMVectorSet(0, 1, 0, 0) : XMVectorSet(0, 0, 1, 0);
    };
    const auto center = XMVectorSet(start.position.x, start.position.y, start.position.z, 1);
    if (axis >= 3 && axis <= 5) {
        // Plane handle: move along the plane whose normal is axis - 3.
        const auto normal = axisNormal(axis - 3);
        XMVECTOR hitA, hitB;
        if (!PlaneHit(startX, startY, uWidth, uHeight, vp, center, normal, hitA) ||
            !PlaneHit(x, y, uWidth, uHeight, vp, center, normal, hitB)) return false;
        XMFLOAT3 delta{};
        XMStoreFloat3(&delta, XMVectorSubtract(hitB, hitA));
        out.position.x += delta.x; out.position.y += delta.y; out.position.z += delta.z;
        return true;
    }
    if (axis == 6) {
        const float fFactor = std::max(0.01f, 1.0f + (static_cast<float>(x - startX) - static_cast<float>(y - startY)) / 90.0f);
        if (!std::isfinite(fFactor)) return false;
        out.scale.x *= fFactor; out.scale.y *= fFactor; out.scale.z *= fFactor;
        return true;
    }
    float fDelta = 0;
    if (mode == GizmoMode::Rotate) {
        const auto normal = axisNormal(axis);
        XMVECTOR hitA, hitB;
        if (!PlaneHit(startX, startY, uWidth, uHeight, vp, center, normal, hitA) ||
            !PlaneHit(x, y, uWidth, uHeight, vp, center, normal, hitB)) return false;
        if (XMVectorGetX(XMVector3LengthSq(hitA)) < 1e-10f || XMVectorGetX(XMVector3LengthSq(hitB)) < 1e-10f) return false;
        hitA = XMVector3Normalize(hitA);
        hitB = XMVector3Normalize(hitB);
        fDelta = std::atan2(XMVectorGetX(XMVector3Dot(normal, XMVector3Cross(hitA, hitB))), XMVectorGetX(XMVector3Dot(hitA, hitB)));
    } else {
        // Project the mouse delta onto the on-screen direction of the axis.
        const auto mvp = GizmoAxisRotation(start, axis, mode) * XMMatrixScaling(fLength, fLength, fLength) *
                         XMMatrixTranslation(start.position.x, start.position.y, start.position.z) * vp;
        XMFLOAT2 pointA, pointB;
        if (!ScreenPoint(XMVectorSet(0,0,0,1), mvp, uWidth, uHeight, pointA) ||
            !ScreenPoint(XMVectorSet(1,0,0,1), mvp, uWidth, uHeight, pointB)) return false;
        const float fVx = pointB.x - pointA.x, fVy = pointB.y - pointA.y, fDenominator = fVx * fVx + fVy * fVy;
        if (fDenominator < 64.0f) return false;
        const float fRatio = ((x - startX) * fVx + (y - startY) * fVy) / fDenominator;
        fDelta = mode == GizmoMode::Translate ? fRatio * fLength : fRatio;
    }
    if (!std::isfinite(fDelta)) return false;
    XMFLOAT3& target = mode == GizmoMode::Translate ? out.position : mode == GizmoMode::Rotate ? out.rotation : out.scale;
    float* pComponent = axis == 0 ? &target.x : axis == 1 ? &target.y : &target.z;
    *pComponent += fDelta;
    if (mode == GizmoMode::Scale && std::abs(*pComponent) < 0.01f) *pComponent = *pComponent < 0 ? -0.01f : 0.01f;
    return true;
}

// ---- Frame rendering -------------------------------------------------------------
bool Renderer::Render(const std::vector<DrawItem>& items, std::string& error, int selected, bool showGrid, GizmoMode mode, bool showGizmo) {
    error.clear();
    if (!p_ || !p_->pDevice) { error = "Renderer is not initialized"; return false; }
    auto& p = *p_;
    auto& device = *p.pDevice;
    if (!p.sResizeError.empty()) { error = p.sResizeError; return false; }

    // 1. Append model vertices behind the static geometry.
    std::vector<UINT> vModelStarts(items.size(), 0), vModelCounts(items.size(), 0);
    size_t nCursor = p.uBaseVertexCount;
    for (size_t nIndex = 0; nIndex < items.size(); ++nIndex) {
        if (!items[nIndex].visible || !items[nIndex].vertices) continue;
        if (nCursor + items[nIndex].vertices->size() > MAX_VERTEX_COUNT) {
            error = "Model vertex limit exceeded (1,048,576 vertices per frame).";
            return false;
        }
        vModelStarts[nIndex] = static_cast<UINT>(nCursor);
        vModelCounts[nIndex] = static_cast<UINT>(items[nIndex].vertices->size());
        nCursor += items[nIndex].vertices->size();
    }
    for (size_t nIndex = 0; nIndex < items.size(); ++nIndex) {
        if (!vModelCounts[nIndex]) continue;
        const auto& vModel = *items[nIndex].vertices;
        static_assert(sizeof(XMFLOAT3) == sizeof(Vertex));
        if (!device.UploadVertices(vModelStarts[nIndex], reinterpret_cast<const Vertex*>(vModel.data()), vModel.size(), error)) return false;
    }

    // 2. Scene objects with the project shader (shaded and/or wireframe pass).
    if (!device.BeginFrame(CLEAR_COLOR, error)) return false;
    const float fAspect = static_cast<float>(p.uWidth) / static_cast<float>(p.uHeight);
    const XMMATRIX vp = ViewProjection(p.camera, fAspect);
    const DrawMode drawMode = showGrid ? p.drawMode : DrawMode::SHADED;
    auto drawObjects = [&](bool bWireframe, const XMFLOAT4* pOverride) {
        device.SetPipeline(bWireframe ? PipelineKind::SCENE_WIREFRAME : PipelineKind::SCENE);
        for (size_t nIndex = 0; nIndex < items.size(); ++nIndex) {
            const auto& item = items[nIndex];
            if (!item.visible) continue;
            const XMMATRIX mvp = World(item) * vp;
            const XMFLOAT4 color = pOverride ? *pOverride : item.color;
            if (vModelCounts[nIndex]) {
                device.Draw(MakeConstants(mvp, color), vModelCounts[nIndex], vModelStarts[nIndex]);
            } else if (item.is2D) {
                device.Draw(MakeConstants(mvp, color), 6, QUAD_START);
            } else {
                // Distinguish the cube's faces even with the minimal default shader.
                constexpr float SHADES[] = {0.75f, 0.9f, 0.62f, 0.82f, 1.0f, 0.55f};
                for (UINT uFace = 0; uFace < 6; ++uFace) {
                    const float fShade = pOverride ? 1.0f : SHADES[uFace];
                    const XMFLOAT4 tint{color.x * fShade, color.y * fShade, color.z * fShade, color.w};
                    device.Draw(MakeConstants(mvp, tint), 6, CUBE_START + uFace * 6);
                }
            }
        }
    };
    if (drawMode != DrawMode::WIREFRAME) drawObjects(false, nullptr);
    if (drawMode == DrawMode::WIREFRAME) drawObjects(true, nullptr);
    if (drawMode == DrawMode::SHADED_WIREFRAME) {
        constexpr XMFLOAT4 WIRE_COLOR{0.08f, 0.08f, 0.08f, 1.0f};
        drawObjects(true, &WIRE_COLOR);
    }

    // 3. Editor grid and world axes.
    if (showGrid) {
        const bool bGrid2D = selected >= 0 && static_cast<size_t>(selected) < items.size() && items[selected].is2D;
        const XMMATRIX gridMvp = bGrid2D ? XMMatrixRotationX(XM_PIDIV2) * vp : vp;
        device.SetPipeline(PipelineKind::GRID);
        device.Draw(MakeConstants(gridMvp, {0.27f, 0.30f, 0.34f, 1.0f}), GRID_COUNT, GRID_START);
        device.Draw(MakeConstants(gridMvp, {0.65f, 0.20f, 0.20f, 1}), 2, AXIS_X_START);
        device.Draw(MakeConstants(gridMvp, {0.20f, 0.52f, 0.27f, 1}), 2, AXIS_Z_START);
    }

    // 4. Selection outlines (active object last, in the brighter orange).
    device.SetPipeline(PipelineKind::OVERLAY);
    auto outline = [&](int nIndex) {
        if (nIndex < 0 || static_cast<size_t>(nIndex) >= items.size()) return;
        const auto& item = items[nIndex];
        if (!item.visible) return;
        const auto color = nIndex == selected ? XMFLOAT4{1.0f, 0.63f, 0.08f, 1.0f} : XMFLOAT4{1.0f, 0.76f, 0.25f, 1.0f};
        device.Draw(MakeConstants(XMMatrixScaling(1.025f, 1.025f, 1.025f) * World(item) * vp, color),
                    item.is2D ? 8 : 24, item.is2D ? QUAD_LINES_START : CUBE_LINES_START);
    };
    if (showGrid || showGizmo) {
        for (int nIndex : p.vSelection) if (nIndex != selected) outline(nIndex);
    }
    outline(selected);

    // 5. Transform gizmo of the active selection.
    if (showGizmo && selected >= 0 && static_cast<size_t>(selected) < items.size()) {
        const auto& item = items[selected];
        float fLength;
        if (GizmoLength(p.camera, item, p.uWidth, p.uHeight, fLength)) {
            const auto base = XMMatrixScaling(fLength, fLength, fLength) * XMMatrixTranslation(item.position.x, item.position.y, item.position.z) * vp;
            for (int nAxis = 0; nAxis < 3; ++nAxis) {
                const auto gizmoMvp = (mode == GizmoMode::Rotate ? RingRotation(nAxis) : GizmoAxisRotation(item, nAxis, mode)) * base;
                const auto color = nAxis == p.nGizmoHot ? HOT_COLOR : AXIS_COLORS[nAxis];
                // Line lists are one pixel wide. Offset the same geometry in
                // clip space for a readable three-pixel cross-section.
                constexpr float OFFSETS[][2] = {{0,0},{-1,0},{1,0},{0,-1},{0,1}};
                for (const auto& offset : OFFSETS) {
                    const auto shifted = gizmoMvp * XMMatrixTranslation(2.0f * offset[0] / p.uWidth, 2.0f * offset[1] / p.uHeight, 0);
                    if (mode == GizmoMode::Rotate) device.Draw(MakeConstants(shifted, color), GIZMO_RING_SEGMENTS * 2, GIZMO_RING_START);
                    else if (mode == GizmoMode::Scale) device.Draw(MakeConstants(shifted, color), 10, GIZMO_SCALE_START);
                    else device.Draw(MakeConstants(shifted, color), 6, GIZMO_ARROW_START);
                }
            }
            if (mode == GizmoMode::Translate) {
                for (int nNormal = 0; nNormal < 3; ++nNormal) {
                    const auto color = p.nGizmoHot == 3 + nNormal ? HOT_COLOR : AXIS_COLORS[nNormal];
                    device.Draw(MakeConstants(RingRotation(nNormal) * base, color), 8, GIZMO_PLANE_START);
                }
            }
            if (mode == GizmoMode::Scale) {
                XMFLOAT2 center;
                if (ScreenPoint(XMVectorSet(0, 0, 0, 1), base, p.uWidth, p.uHeight, center)) {
                    const auto mvp = XMMatrixScaling(180.0f / p.uWidth, 180.0f / p.uHeight, 1) *
                                     XMMatrixTranslation(2 * center.x / p.uWidth - 1, 1 - 2 * center.y / p.uHeight, 0);
                    const auto color = p.nGizmoHot == 6 ? HOT_COLOR : XMFLOAT4{0.92f, 0.92f, 0.92f, 1};
                    device.Draw(MakeConstants(mvp, color), 8, GIZMO_CENTER_START);
                }
            }
        }
    }

    // 6. Marquee selection rectangle.
    if (p.bMarquee && (showGrid || showGizmo) && p.uWidth && p.uHeight) {
        const float fLeft = 2.0f * p.nRectX0 / p.uWidth - 1.0f;
        const float fTop = 1.0f - 2.0f * p.nRectY0 / p.uHeight;
        const float fDx = 2.0f * (p.nRectX1 - p.nRectX0) / p.uWidth;
        const float fDy = -2.0f * (p.nRectY1 - p.nRectY0) / p.uHeight;
        device.Draw(MakeConstants(XMMatrixScaling(fDx, fDy, 1) * XMMatrixTranslation(fLeft, fTop, 0), {0.45f, 0.75f, 1.0f, 1.0f}),
                    8, MARQUEE_START);
    }

    // 7. Orientation gizmo in the top-right corner (editor views only).
    if (showGrid && p.bShowOrientation && p.uWidth > 120 && p.uHeight > 120) {
        const float fPixelX = 2.0f / p.uWidth, fPixelY = 2.0f / p.uHeight;
        const float fCenterX = 1.0f - 52.0f * fPixelX, fCenterY = 1.0f - 52.0f * fPixelY;
        const XMMATRIX view = View(p.camera);
        constexpr UINT LETTER_STARTS[] = {LETTER_X_START, LETTER_Y_START, LETTER_Z_START};
        constexpr UINT LETTER_COUNTS[] = {LETTER_X_COUNT, LETTER_Y_COUNT, LETTER_Z_COUNT};
        for (int nAxis = 0; nAxis < 3; ++nAxis) {
            XMFLOAT3 direction{};
            XMStoreFloat3(&direction, XMVector3TransformNormal(XMVectorSet(nAxis == 0, nAxis == 1, nAxis == 2, 0), view));
            const float fDx = direction.x * 30.0f * fPixelX, fDy = direction.y * 30.0f * fPixelY;
            // Axes pointing away from the viewer are dimmed, as in Unity.
            const float fShade = direction.z > 0.0f ? 0.55f : 1.0f;
            const XMFLOAT4 color{AXIS_COLORS[nAxis].x * fShade, AXIS_COLORS[nAxis].y * fShade, AXIS_COLORS[nAxis].z * fShade, 1};
            // Maps the unit +X shaft (0,0,0)-(1,0,0) onto the projected axis.
            const XMMATRIX shaft(fDx, fDy, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, fCenterX, fCenterY, 0, 1);
            for (const float fOffset : {-1.0f, 0.0f, 1.0f}) {
                const auto thick = shaft * XMMatrixTranslation(fOffset * fPixelX * (std::abs(fDy) > std::abs(fDx) ? 1.0f : 0.0f),
                                                               fOffset * fPixelY * (std::abs(fDy) > std::abs(fDx) ? 0.0f : 1.0f), 0);
                device.Draw(MakeConstants(thick, color), 2, GIZMO_ARROW_START);
            }
            const auto letter = XMMatrixScaling(4.0f * fPixelX, 5.0f * fPixelY, 1) *
                                XMMatrixTranslation(fCenterX + fDx * 1.35f, fCenterY + fDy * 1.35f, 0);
            device.Draw(MakeConstants(letter, color), LETTER_COUNTS[nAxis], LETTER_STARTS[nAxis]);
        }
    }

    return device.EndFrame(error);
}

void Renderer::Shutdown() {
    if (!p_) return;
    if (p_->pDevice) p_->pDevice->Shutdown();
    p_.reset();
}
} // namespace dxs
