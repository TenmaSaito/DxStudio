//==================================================================================
//
// レンダラー（描画の窓口）のソースファイル [Renderer.cpp]
//
// カメラの計算・ピック・ギズモの操作・エディター補助表示の描画順を受け持つ。
// GPU への命令はすべて graphics::IGraphicsDevice を通すので、同じコードで
// Direct3D 12 / 11 / 9 を動かせる（Graphics/GraphicsDevice.h を参照）。
//
// 頂点バッファの並び:
//   [固定の頂点（立方体・四角形・グリッド・ギズモ・文字など）][モデルの頂点][補助線]
//   固定の頂点は初期化時に 1 回だけ送り、モデルと補助線は毎フレーム後ろに書く。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
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

//**********************************************************************************
// *** 定数宣言 ***
//**********************************************************************************
constexpr std::size_t MAX_VERTEX_COUNT = 1u << 20;            // 1 フレームの最大頂点数（約 100 万）
constexpr float CLEAR_COLOR[4] = {0.16f, 0.31f, 0.50f, 1.0f}; // 背景色（空の青）

//**********************************************************************************
// *** 固定の頂点（初期化時に 1 回だけ送る） ***
//**********************************************************************************
// 立方体: 6 面をそれぞれ別の頂点で持つ（面ごとに色を変えるため）。続けて XY 平面の四角形。
constexpr Vertex BASE_VERTICES[] = {
    {-0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},  {0.5f, 0.5f, -0.5f},   {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f},
    {0.5f, -0.5f, -0.5f},  {0.5f, -0.5f, 0.5f},   {0.5f, 0.5f, 0.5f},    {-0.5f, 0.5f, 0.5f},   {0.5f, -0.5f, 0.5f},
    {-0.5f, 0.5f, 0.5f},   {-0.5f, -0.5f, 0.5f},  {-0.5f, -0.5f, 0.5f},  {-0.5f, 0.5f, 0.5f},   {-0.5f, 0.5f, -0.5f},
    {-0.5f, -0.5f, 0.5f},  {-0.5f, 0.5f, -0.5f},  {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},  {0.5f, 0.5f, -0.5f},
    {0.5f, 0.5f, 0.5f},    {0.5f, -0.5f, -0.5f},  {0.5f, 0.5f, 0.5f},    {0.5f, -0.5f, 0.5f},   {-0.5f, 0.5f, -0.5f},
    {-0.5f, 0.5f, 0.5f},   {0.5f, 0.5f, 0.5f},    {-0.5f, 0.5f, -0.5f},  {0.5f, 0.5f, 0.5f},    {0.5f, 0.5f, -0.5f},
    {-0.5f, -0.5f, 0.5f},  {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},  {-0.5f, -0.5f, 0.5f},  {0.5f, -0.5f, -0.5f},
    {0.5f, -0.5f, 0.5f},   {-0.5f, -0.5f, 0},     {-0.5f, 0.5f, 0},      {0.5f, 0.5f, 0},       {-0.5f, -0.5f, 0},
    {0.5f, 0.5f, 0},       {0.5f, -0.5f, 0},
};

// 各図形が頂点バッファの何番目から始まるか（BuildVertices の追加順と一致させる）
constexpr UINT CUBE_START = 0;  // 立方体（36 頂点）
constexpr UINT QUAD_START = 36; // 四角形（6 頂点）
constexpr UINT GRID_START = 42; // グリッド
constexpr UINT GRID_COUNT = 84; // 縦横 21 本ずつ、1 本 2 頂点
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
// シーンビュー右上の向き表示の文字 X / Y / Z（Unity / UE の隅の軸表示）
constexpr UINT LETTER_X_START = MARQUEE_START + 8;
constexpr UINT LETTER_Y_START = LETTER_X_START + 4;
constexpr UINT LETTER_Z_START = LETTER_Y_START + 6;
constexpr UINT LETTER_X_COUNT = 4, LETTER_Y_COUNT = 6, LETTER_Z_COUNT = 6;

//==================================================================================
// --- 固定の頂点の作成処理 ---
//==================================================================================
std::vector<Vertex> BuildVertices() {
    std::vector<Vertex> v(std::begin(BASE_VERTICES), std::end(BASE_VERTICES));

    // グリッド: -10 から 10 まで 1 単位ごとの線（XZ 平面）
    for (int nLine = -10; nLine <= 10; ++nLine) {
        const float n = static_cast<float>(nLine);
        v.push_back({n, 0, -10});
        v.push_back({n, 0, 10});
        v.push_back({-10, 0, n});
        v.push_back({10, 0, n});
    }

    // X 軸・Z 軸（グリッドの上に色付きで描く）
    v.push_back({-10, 0, 0});
    v.push_back({10, 0, 0});
    v.push_back({0, 0, -10});
    v.push_back({0, 0, 10});

    // 立方体の 12 本の辺（選択時のアウトライン）
    constexpr Vertex CORNERS[] = {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
                                  {-0.5f, -0.5f, 0.5f},  {0.5f, -0.5f, 0.5f},  {0.5f, 0.5f, 0.5f},  {-0.5f, 0.5f, 0.5f}};
    constexpr int EDGES[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& edge : EDGES) {
        v.push_back(CORNERS[edge[0]]);
        v.push_back(CORNERS[edge[1]]);
    }

    // 四角形の 4 本の辺（2D のアウトライン）
    v.push_back({-0.5f, -0.5f, 0});
    v.push_back({0.5f, -0.5f, 0});
    v.push_back({0.5f, -0.5f, 0});
    v.push_back({0.5f, 0.5f, 0});
    v.push_back({0.5f, 0.5f, 0});
    v.push_back({-0.5f, 0.5f, 0});
    v.push_back({-0.5f, 0.5f, 0});
    v.push_back({-0.5f, -0.5f, 0});

    // ギズモ: +X 方向の軸、矢印の先、拡大縮小用の小さな箱
    v.push_back({0, 0, 0});
    v.push_back({1, 0, 0});
    v.push_back({1, 0, 0});
    v.push_back({0.80f, 0.09f, 0});
    v.push_back({1, 0, 0});
    v.push_back({0.80f, -0.09f, 0});
    v.push_back({0, 0, 0});
    v.push_back({1, 0, 0});
    v.push_back({0.91f, -0.06f, 0});
    v.push_back({1.03f, -0.06f, 0});
    v.push_back({1.03f, -0.06f, 0});
    v.push_back({1.03f, 0.06f, 0});
    v.push_back({1.03f, 0.06f, 0});
    v.push_back({0.91f, 0.06f, 0});
    v.push_back({0.91f, 0.06f, 0});
    v.push_back({0.91f, -0.06f, 0});

    // 回転ギズモの円（XY 平面上の単位円）
    for (UINT uSegment = 0; uSegment < GIZMO_RING_SEGMENTS; ++uSegment) {
        const float fA = XM_2PI * uSegment / GIZMO_RING_SEGMENTS;
        const float fB = XM_2PI * (uSegment + 1) / GIZMO_RING_SEGMENTS;
        v.push_back({std::cos(fA), std::sin(fA), 0});
        v.push_back({std::cos(fB), std::sin(fB), 0});
    }

    auto square = [&](float fLo, float fHi) {
        v.push_back({fLo, fLo, 0});
        v.push_back({fHi, fLo, 0});
        v.push_back({fHi, fLo, 0});
        v.push_back({fHi, fHi, 0});
        v.push_back({fHi, fHi, 0});
        v.push_back({fLo, fHi, 0});
        v.push_back({fLo, fHi, 0});
        v.push_back({fLo, fLo, 0});
    };
    square(0.23f, 0.45f);  // 平面移動のハンドル
    square(-0.08f, 0.08f); // 均等スケールのハンドル
    square(0, 1);          // 画面上の四角形（範囲選択）
    // 文字（[-1, 1] の箱の中に線で描く）
    v.push_back({-1, -1, 0});
    v.push_back({1, 1, 0});
    v.push_back({-1, 1, 0});
    v.push_back({1, -1, 0}); // X
    v.push_back({-1, 1, 0});
    v.push_back({0, 0, 0});
    v.push_back({1, 1, 0});
    v.push_back({0, 0, 0});
    v.push_back({0, 0, 0});
    v.push_back({0, -1, 0}); // Y
    v.push_back({-1, 1, 0});
    v.push_back({1, 1, 0});
    v.push_back({1, 1, 0});
    v.push_back({-1, -1, 0});
    v.push_back({-1, -1, 0});
    v.push_back({1, -1, 0}); // Z
    return v;
}

//**********************************************************************************
// *** カメラと射影の計算 ***
//**********************************************************************************
// --- ワールド行列（拡大 → 回転 → 移動）の作成処理 ---
XMMATRIX World(const DrawItem& item) {
    return XMMatrixScaling(item.scale.x, item.scale.y, item.scale.z) *
           XMMatrixRotationRollPitchYaw(item.rotation.x, item.rotation.y, item.rotation.z) *
           XMMatrixTranslation(item.position.x, item.position.y, item.position.z);
}

// --- ビュー行列の作成処理（注視点の周りを回るカメラ） ---
XMMATRIX View(const Camera& camera) {
    const XMVECTOR target = XMLoadFloat3(&camera.target);
    const float fCosPitch = std::cos(camera.fPitch);
    const XMVECTOR offset =
        XMVectorSet(std::sin(camera.fYaw) * fCosPitch * camera.fDistance, std::sin(camera.fPitch) * camera.fDistance,
                    -std::cos(camera.fYaw) * fCosPitch * camera.fDistance, 0);
    return XMMatrixLookAtLH(XMVectorAdd(target, offset), target, XMVectorSet(0, 1, 0, 0));
}

// --- ビュー × 射影 行列の作成処理 ---
XMMATRIX ViewProjection(const Camera& camera, float fAspect) {
    const float fHeight = camera.fDistance * (10.0f / 6.32455532f); // 平行投影の見える高さ（距離に比例）
    const auto projection =
        camera.bOrthographic
            ? XMMatrixOrthographicLH(fHeight * fAspect, fHeight, camera.fNearClip, camera.fFarClip)
            : XMMatrixPerspectiveFovLH(XMConvertToRadians(camera.fFieldOfView), fAspect, camera.fNearClip, camera.fFarClip);
    return View(camera) * projection;
}

// --- +X 向きの図形を各軸の向きへ回す行列 ---
XMMATRIX AxisRotation(int nAxis) {
    return nAxis == 0 ? XMMatrixIdentity() : nAxis == 1 ? XMMatrixRotationZ(XM_PIDIV2) : XMMatrixRotationY(-XM_PIDIV2);
}

// --- XY 平面の円を各軸に垂直な面へ回す行列 ---
XMMATRIX RingRotation(int nAxis) {
    return nAxis == 0 ? XMMatrixRotationY(XM_PIDIV2) : nAxis == 1 ? XMMatrixRotationX(-XM_PIDIV2) : XMMatrixIdentity();
}

// --- ギズモの軸の向きの行列 ---
XMMATRIX GizmoAxisRotation(const DrawItem& item, int nAxis, GizmoMode mode) {
    // 拡大縮小はオブジェクト自身の軸で行い、移動はワールドの軸で行う
    const auto basis = AxisRotation(nAxis);
    return mode == GizmoMode::Scale ? basis * XMMatrixRotationRollPitchYaw(item.rotation.x, item.rotation.y, item.rotation.z)
                                    : basis;
}

// --- 画面上でほぼ一定の大きさ（約 90 ピクセル）になるギズモの長さの計算処理 ---
bool GizmoLength(const Camera& camera, const DrawItem& item, unsigned uWidth, unsigned uHeight, float& fLength) {
    if (!uWidth || !uHeight) return false;
    const auto vp = ViewProjection(camera, static_cast<float>(uWidth) / uHeight);
    const auto clip = XMVector4Transform(XMVectorSet(item.position.x, item.position.y, item.position.z, 1), vp);
    const float fW = XMVectorGetW(clip), fZ = XMVectorGetZ(clip);
    if (!std::isfinite(fW) || fW <= 0 || fZ < 0 || fZ > fW) return false; // カメラの後ろ・視野外
    const float fWorldHeight =
        camera.bOrthographic ? camera.fDistance * (10.0f / 6.32455532f) : 2.0f * std::tan(XM_PIDIV4 / 2) * fW;
    fLength = fWorldHeight * 90.0f / uHeight;
    return std::isfinite(fLength) && fLength > 0;
}

// --- 3D の点 → 画面座標（ピクセル）の変換処理。視野外なら false ---
bool ScreenPoint(XMVECTOR point, FXMMATRIX mvp, unsigned uWidth, unsigned uHeight, XMFLOAT2& result) {
    const auto clip = XMVector4Transform(point, mvp);
    const float fW = XMVectorGetW(clip), fZ = XMVectorGetZ(clip);
    if (!std::isfinite(fW) || fW <= 0 || fZ < 0 || fZ > fW) return false;
    result = {(XMVectorGetX(clip) / fW + 1.0f) * 0.5f * uWidth, (1.0f - XMVectorGetY(clip) / fW) * 0.5f * uHeight};
    return std::isfinite(result.x) && std::isfinite(result.y);
}

// --- 点 (fX, fY) と線分 ab の距離 ---
float SegmentDistance(float fX, float fY, XMFLOAT2 a, XMFLOAT2 b) {
    const float fDx = b.x - a.x, fDy = b.y - a.y, fLength2 = fDx * fDx + fDy * fDy;
    if (fLength2 < 1e-5f) return std::numeric_limits<float>::infinity();
    const float fT = std::clamp(((fX - a.x) * fDx + (fY - a.y) * fDy) / fLength2, 0.0f, 1.0f);
    const float fPx = fX - (a.x + fT * fDx), fPy = fY - (a.y + fT * fDy);
    return std::sqrt(fPx * fPx + fPy * fPy);
}

// --- マウス位置からの視線と平面の交点（origin からの相対位置）の計算処理 ---
bool PlaneHit(int nX, int nY, unsigned uWidth, unsigned uHeight, FXMMATRIX vp, XMVECTOR origin, XMVECTOR normal, XMVECTOR& hit) {
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

// --- 点が凸四角形の中にあるか（全辺で外積の符号がそろうか）の判定処理 ---
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

// --- シェーダーに渡す定数の作成処理 ---
ObjectConstants MakeConstants(FXMMATRIX mvp, const XMFLOAT4& color) {
    ObjectConstants constants{};
    XMStoreFloat4x4(&constants.mvp, mvp);
    constants.color = color;
    return constants;
}

constexpr XMFLOAT4 AXIS_COLORS[] = {{1, 0.24f, 0.22f, 1}, {0.28f, 0.9f, 0.28f, 1}, {0.28f, 0.52f, 1, 1}}; // X 赤 / Y 緑 / Z 青
constexpr XMFLOAT4 HOT_COLOR{1, 0.88f, 0.15f, 1};   // マウスが乗っているハンドルの色（黄）
GraphicsApi g_defaultApi = GraphicsApi::DIRECT3D12; // API を指定しないときに使う API
} // namespace

//**********************************************************************************
// *** レンダラーの内部状態 ***
//**********************************************************************************
struct Renderer::Impl {
    std::unique_ptr<graphics::IGraphicsDevice> pDevice;     // 描画デバイス（API ごとの実装）
    unsigned uWidth = 0, uHeight = 0;                       // 描画領域の大きさ
    UINT uBaseVertexCount = 0;                              // 固定の頂点数（この後ろにモデルを書く）
    Camera camera{};                                        // カメラ
    std::vector<int> vSelection;                            // 選択中のオブジェクト番号
    std::vector<XMFLOAT3> vDebugLines;                      // 補助線（2 点で 1 本）
    XMFLOAT4 debugColor{0.55f, 0.95f, 0.45f, 1.0f};         // 補助線の色
    bool bMarquee = false;                                  // 範囲選択の四角形を表示するか
    int nRectX0 = 0, nRectY0 = 0, nRectX1 = 0, nRectY1 = 0; // 範囲選択の四角形（ピクセル）
    int nGizmoHot = -1;                                     // マウスが乗っているハンドル
    bool bShowOrientation = true;                           // 右上の向き表示
    DrawMode drawMode = DrawMode::SHADED;                   // シーンビューの描画モード
    std::string sResizeError;                               // 大きさ変更の失敗理由（次の Render で返す）
};

//==================================================================================
// --- コンストラクタ / デストラクタ ---
//==================================================================================
Renderer::Renderer() = default;

Renderer::~Renderer() {
    Shutdown();
}

//==================================================================================
// --- 既定の API の設定 / 取得処理 ---
//==================================================================================
void Renderer::SetDefaultApi(GraphicsApi api) {
    g_defaultApi = api;
}

GraphicsApi Renderer::DefaultApi() {
    return g_defaultApi;
}

//==================================================================================
// --- 初期化処理（既定の API） ---
//==================================================================================
bool Renderer::Initialize(HWND hwnd, unsigned width, unsigned height, const std::wstring& shaderPath, std::string& error) {
    return Initialize(hwnd, width, height, shaderPath, g_defaultApi, error);
}

//==================================================================================
// --- 初期化処理（API を指定） ---
//==================================================================================
bool Renderer::Initialize(HWND hwnd, unsigned width, unsigned height, const std::wstring& shaderPath, GraphicsApi api,
                          std::string& error) {
    Shutdown(); // 初期化し直す場合は先に解放
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

    // 固定の頂点をデバイスの作成時に送る
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

    p_ = std::move(pImpl); // 成功したときだけ内部状態を差し替える
    return true;
}

//==================================================================================
// --- 初期化処理（使える API を順に試す） ---
//==================================================================================
bool Renderer::InitializeWithFallback(HWND hwnd, unsigned width, unsigned height, const std::wstring& shaderPath,
                                      GraphicsApi preferred, std::string& error, std::string* pReport) {
    std::string sReport;

    // 希望の API → それ以外を優先度順
    std::vector<GraphicsApi> vOrder{preferred};
    for (const auto api : ALL_GRAPHICS_APIS) {
        if (api != preferred) {
            vOrder.push_back(api);
        }
    }

    for (const auto api : vOrder) {
        std::string sAttemptError;
        if (Initialize(hwnd, width, height, shaderPath, api, sAttemptError)) {
            if (pReport) {
                *pReport = sReport;
            }

            error.clear();
            return true;
        }

        sReport += sAttemptError + "\n";
        if (error.empty()) {
            error = sAttemptError; // 希望した API の失敗理由を返す
        }
    }

    if (pReport) {
        *pReport = sReport;
    }

    return false;
}

//==================================================================================
// --- 状態の取得処理 ---
//==================================================================================
bool Renderer::IsInitialized() const {
    return static_cast<bool>(p_);
}

GraphicsApi Renderer::Api() const {
    return p_ ? p_->pDevice->Api() : g_defaultApi;
}

//==================================================================================
// --- 描画領域の大きさ変更処理 ---
//==================================================================================
void Renderer::Resize(unsigned width, unsigned height) {
    if (!p_ || !width || !height || (p_->uWidth == width && p_->uHeight == height)) return;
    p_->sResizeError.clear();
    if (p_->pDevice->Resize(width, height, p_->sResizeError)) {
        p_->uWidth = width;
        p_->uHeight = height;
    }
}

//==================================================================================
// --- カメラの設定処理 ---
//==================================================================================
void Renderer::SetCamera(const Camera& camera) {
    if (!p_ || !std::isfinite(camera.fDistance) || !std::isfinite(camera.fYaw) || !std::isfinite(camera.fPitch) ||
        !std::isfinite(camera.target.x) || !std::isfinite(camera.target.y) || !std::isfinite(camera.target.z)) {
        return;
    }

    // 不正な値（NaN / 無限大）は無視し、距離と上下の角度は範囲内に収める
    p_->camera = camera;
    p_->camera.fDistance = std::clamp(camera.fDistance, 0.2f, 80.0f);
    p_->camera.fPitch = std::clamp(camera.fPitch, -1.54f, 1.54f);
}

//==================================================================================
// --- 描画モードの設定 / 取得処理 ---
//==================================================================================
void Renderer::SetDrawMode(DrawMode mode) {
    if (p_) {
        p_->drawMode = mode;
    }
}

DrawMode Renderer::GetDrawMode() const {
    return p_ ? p_->drawMode : DrawMode::SHADED;
}

//==================================================================================
// --- 向き表示の切り替え処理 ---
//==================================================================================
void Renderer::SetShowOrientationGizmo(bool bShow) {
    if (p_) {
        p_->bShowOrientation = bShow;
    }
}

//==================================================================================
// --- クリック位置のオブジェクトの取得処理（ピック） ---
//==================================================================================
// 各オブジェクトのローカル空間で視線と単位立方体（2D は z = 0 の面）の交差を
// 調べ、一番手前に当たった物を返す。
int Renderer::Pick(int x, int y, const std::vector<DrawItem>& items) const {
    if (!p_ || !p_->uWidth || !p_->uHeight || x < 0 || y < 0 || x >= static_cast<int>(p_->uWidth) ||
        y >= static_cast<int>(p_->uHeight)) {
        return -1;
    }

    // ピクセル中心の画面座標を -1..1 の正規化座標へ
    const float fSx = (2.0f * (x + 0.5f) / p_->uWidth) - 1.0f;
    const float fSy = 1.0f - (2.0f * (y + 0.5f) / p_->uHeight);
    const float fAspect = static_cast<float>(p_->uWidth) / p_->uHeight;
    float fBestDepth = std::numeric_limits<float>::infinity();
    int nBest = -1;
    for (size_t nIndex = 0; nIndex < items.size(); ++nIndex) {
        const auto& item = items[nIndex];
        if (!item.bVisible) continue;
        const XMMATRIX mvp = World(item) * ViewProjection(p_->camera, fAspect);
        XMVECTOR determinant;
        const XMMATRIX inverse = XMMatrixInverse(&determinant, mvp);
        if (!std::isfinite(XMVectorGetX(determinant)) || std::abs(XMVectorGetX(determinant)) < 1e-12f) continue;

        // 画面の点を逆変換して、ローカル空間での視線（手前 → 奥）を作る
        XMFLOAT3 origin, end;
        XMStoreFloat3(&origin, XMVector3TransformCoord(XMVectorSet(fSx, fSy, 0, 1), inverse));
        XMStoreFloat3(&end, XMVector3TransformCoord(XMVectorSet(fSx, fSy, 1, 1), inverse));
        const float aOrigin[] = {origin.x, origin.y, origin.z};
        const float aDirection[] = {end.x - origin.x, end.y - origin.y, end.z - origin.z};

        // スラブ法: 軸ごとの「入る」「出る」範囲を重ねて単位立方体との交差を判定（2D は z = 0 の薄い板）
        float fMin = 0.0f, fMax = 1.0f;
        bool bHit = true;
        for (int nAxis = 0; nAxis < 3; ++nAxis) {
            const float fLo = item.bIs2D && nAxis == 2 ? 0.0f : -0.5f;
            const float fHi = item.bIs2D && nAxis == 2 ? 0.0f : 0.5f;
            if (std::abs(aDirection[nAxis]) < 1e-8f) {
                if (aOrigin[nAxis] < fLo || aOrigin[nAxis] > fHi) {
                    bHit = false;
                    break;
                }
            } else {
                float fA = (fLo - aOrigin[nAxis]) / aDirection[nAxis], fB = (fHi - aOrigin[nAxis]) / aDirection[nAxis];
                if (fA > fB) {
                    std::swap(fA, fB);
                }

                fMin = std::max(fMin, fA);
                fMax = std::min(fMax, fB);
                if (fMin > fMax) {
                    bHit = false;
                    break;
                }
            }
        }

        if (!bHit) continue;

        // 当たった点の深度で一番手前を選ぶ
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

//==================================================================================
// --- 範囲選択処理（画面上の外接四角形が重なる物） ---
//==================================================================================
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
        if (!item.bVisible) continue;
        const auto mvp = World(item) * ViewProjection(p_->camera, fAspect);
        float fMinX = std::numeric_limits<float>::infinity(), fMinY = fMinX;
        float fMaxX = -fMinX, fMaxY = -fMinX;
        bool bProjected = false;

        // 立方体の 8 頂点（2D は 4 頂点）を画面に投影して外接四角形を求める
        for (int nZ = 0; nZ < (item.bIs2D ? 1 : 2); ++nZ) {
            for (int nY = 0; nY < 2; ++nY) {
                for (int nX = 0; nX < 2; ++nX) {
                    XMFLOAT2 point;
                    if (!ScreenPoint(
                            XMVectorSet(nX ? 0.5f : -0.5f, nY ? 0.5f : -0.5f, item.bIs2D ? 0.0f : (nZ ? 0.5f : -0.5f), 1), mvp,
                            p_->uWidth, p_->uHeight, point)) {
                        continue;
                    }

                    bProjected = true;
                    fMinX = std::min(fMinX, point.x);
                    fMaxX = std::max(fMaxX, point.x);
                    fMinY = std::min(fMinY, point.y);
                    fMaxY = std::max(fMaxY, point.y);
                }
            }
        }

        if (bProjected && fMaxX >= fLeft && fMinX <= fRight && fMaxY >= fTop && fMinY <= fBottom) {
            vResult.push_back(static_cast<int>(nIndex));
        }
    }

    return vResult;
}

//==================================================================================
// --- 選択・範囲選択・ハンドル強調・補助線の設定処理 ---
//==================================================================================
void Renderer::SetSelection(const std::vector<int>& indices) {
    if (p_) {
        p_->vSelection = indices;
    }
}

void Renderer::SetMarquee(bool visible, int x0, int y0, int x1, int y1) {
    if (!p_) return;
    p_->bMarquee = visible;
    p_->nRectX0 = x0;
    p_->nRectY0 = y0;
    p_->nRectX1 = x1;
    p_->nRectY1 = y1;
}

void Renderer::SetGizmoHot(int handle) {
    if (p_) {
        p_->nGizmoHot = handle;
    }
}

void Renderer::SetDebugLines(std::vector<XMFLOAT3> vSegments, const XMFLOAT4& color) {
    if (!p_) return;
    if (vSegments.size() % 2) {
        vSegments.pop_back(); // 2 点で 1 本なので端数は捨てる
    }

    p_->vDebugLines = std::move(vSegments);
    p_->debugColor = color;
}

//==================================================================================
// --- マウス位置のギズモのハンドルの取得処理 ---
//==================================================================================
int Renderer::HitGizmo(int x, int y, const DrawItem& item, GizmoMode mode) const {
    if (!p_ || x < 0 || y < 0 || x >= static_cast<int>(p_->uWidth) || y >= static_cast<int>(p_->uHeight)) return -1;
    const unsigned uWidth = p_->uWidth, uHeight = p_->uHeight;
    float fLength;
    if (!GizmoLength(p_->camera, item, uWidth, uHeight, fLength)) return -1;
    const auto vp = ViewProjection(p_->camera, static_cast<float>(uWidth) / uHeight);
    const auto base =
        XMMatrixScaling(fLength, fLength, fLength) * XMMatrixTranslation(item.position.x, item.position.y, item.position.z) * vp;

    // 中心の箱は全方向の均等スケール。平面ハンドルは軸より優先し、
    // それ以外の場所では軸を選びやすくする。
    if (mode == GizmoMode::Scale) {
        XMFLOAT2 center;
        if (ScreenPoint(XMVectorSet(0, 0, 0, 1), base, uWidth, uHeight, center) &&
            std::hypot(center.x - x, center.y - y) <= 9.0f) {
            return 6;
        }
    }

    if (mode == GizmoMode::Translate) {
        for (int nNormal = 0; nNormal < 3; ++nNormal) {
            const auto mvp = RingRotation(nNormal) * base;
            XMFLOAT2 aQuad[4]{};
            const XMVECTOR aPoints[] = {XMVectorSet(.23f, .23f, 0, 1), XMVectorSet(.45f, .23f, 0, 1),
                                        XMVectorSet(.45f, .45f, 0, 1), XMVectorSet(.23f, .45f, 0, 1)};
            bool bValid = true;
            for (int nCorner = 0; nCorner < 4; ++nCorner) {
                bValid &= ScreenPoint(aPoints[nCorner], mvp, uWidth, uHeight, aQuad[nCorner]);
            }

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
            if (!ScreenPoint(XMVectorSet(0, 0, 0, 1), mvp, uWidth, uHeight, center) ||
                !ScreenPoint(XMVectorSet(1, 0, 0, 1), mvp, uWidth, uHeight, u) ||
                !ScreenPoint(XMVectorSet(0, 1, 0, 1), mvp, uWidth, uHeight, v)) {
                continue;
            }

            // 真横から見えている円は（細い線になり）うまくドラッグできないので除外
            if (std::abs((u.x - center.x) * (v.y - center.y) - (u.y - center.y) * (v.x - center.x)) < 64.0f) continue;
            for (UINT uSegment = 0; uSegment < GIZMO_RING_SEGMENTS; ++uSegment) {
                const float fA = XM_2PI * uSegment / GIZMO_RING_SEGMENTS, fB = XM_2PI * (uSegment + 1) / GIZMO_RING_SEGMENTS;
                XMFLOAT2 pointA, pointB;
                if (!ScreenPoint(XMVectorSet(std::cos(fA), std::sin(fA), 0, 1), mvp, uWidth, uHeight, pointA) ||
                    !ScreenPoint(XMVectorSet(std::cos(fB), std::sin(fB), 0, 1), mvp, uWidth, uHeight, pointB)) {
                    continue;
                }

                const float fDistance = SegmentDistance(static_cast<float>(x), static_cast<float>(y), pointA, pointB);
                if (fDistance < fBest) {
                    fBest = fDistance;
                    nSelected = nAxis;
                }
            }
        } else {
            // 移動・拡縮: 軸の線分（中心付近は除く）との距離
            XMFLOAT2 pointA, pointB;
            if (!ScreenPoint(XMVectorSet(0.20f, 0, 0, 1), mvp, uWidth, uHeight, pointA) ||
                !ScreenPoint(XMVectorSet(1, 0, 0, 1), mvp, uWidth, uHeight, pointB)) {
                continue;
            }

            if (std::hypot(pointB.x - pointA.x, pointB.y - pointA.y) < 8.0f) continue;
            const float fDistance = SegmentDistance(static_cast<float>(x), static_cast<float>(y), pointA, pointB);
            if (fDistance < fBest) {
                fBest = fDistance;
                nSelected = nAxis;
            }
        }
    }

    return nSelected;
}

//==================================================================================
// --- ギズモのドラッグによる変形の計算処理 ---
//==================================================================================
bool Renderer::DragGizmo(const DrawItem& start, GizmoMode mode, int axis, int startX, int startY, int x, int y,
                         DrawItem& out) const {
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
        // 平面ハンドル: 法線が (axis - 3) 軸の平面に沿って動かす
        const auto normal = axisNormal(axis - 3);
        XMVECTOR hitA, hitB;
        if (!PlaneHit(startX, startY, uWidth, uHeight, vp, center, normal, hitA) ||
            !PlaneHit(x, y, uWidth, uHeight, vp, center, normal, hitB)) {
            return false;
        }

        XMFLOAT3 delta{};
        XMStoreFloat3(&delta, XMVectorSubtract(hitB, hitA));
        out.position.x += delta.x;
        out.position.y += delta.y;
        out.position.z += delta.z;
        return true;
    }

    if (axis == 6) { // 均等スケール: 右上へのドラッグで拡大
        const float fFactor = std::max(0.01f, 1.0f + (static_cast<float>(x - startX) - static_cast<float>(y - startY)) / 90.0f);
        if (!std::isfinite(fFactor)) return false;
        out.scale.x *= fFactor;
        out.scale.y *= fFactor;
        out.scale.z *= fFactor;
        return true;
    }

    float fDelta = 0;
    if (mode == GizmoMode::Rotate) {
        // 回転: 回転軸に垂直な平面上で、開始点と現在点のなす角度
        const auto normal = axisNormal(axis);
        XMVECTOR hitA, hitB;
        if (!PlaneHit(startX, startY, uWidth, uHeight, vp, center, normal, hitA) ||
            !PlaneHit(x, y, uWidth, uHeight, vp, center, normal, hitB)) {
            return false;
        }

        if (XMVectorGetX(XMVector3LengthSq(hitA)) < 1e-10f || XMVectorGetX(XMVector3LengthSq(hitB)) < 1e-10f) return false;
        hitA = XMVector3Normalize(hitA);
        hitB = XMVector3Normalize(hitB);
        fDelta =
            std::atan2(XMVectorGetX(XMVector3Dot(normal, XMVector3Cross(hitA, hitB))), XMVectorGetX(XMVector3Dot(hitA, hitB)));
    } else {
        // 移動・拡縮: マウスの移動量を、画面上での軸の向きに投影する
        const auto mvp = GizmoAxisRotation(start, axis, mode) * XMMatrixScaling(fLength, fLength, fLength) *
                         XMMatrixTranslation(start.position.x, start.position.y, start.position.z) * vp;
        XMFLOAT2 pointA, pointB;
        if (!ScreenPoint(XMVectorSet(0, 0, 0, 1), mvp, uWidth, uHeight, pointA) ||
            !ScreenPoint(XMVectorSet(1, 0, 0, 1), mvp, uWidth, uHeight, pointB)) {
            return false;
        }

        const float fVx = pointB.x - pointA.x, fVy = pointB.y - pointA.y, fDenominator = fVx * fVx + fVy * fVy;
        if (fDenominator < 64.0f) return false;
        const float fRatio = ((x - startX) * fVx + (y - startY) * fVy) / fDenominator;
        fDelta = mode == GizmoMode::Translate ? fRatio * fLength : fRatio;
    }

    if (!std::isfinite(fDelta)) return false;

    // 対象の成分（移動 / 回転 / 拡縮 の X / Y / Z）に加える
    XMFLOAT3& target = mode == GizmoMode::Translate ? out.position : mode == GizmoMode::Rotate ? out.rotation : out.scale;
    float* pComponent = axis == 0 ? &target.x : axis == 1 ? &target.y : &target.z;
    *pComponent += fDelta;
    if (mode == GizmoMode::Scale && std::abs(*pComponent) < 0.01f) {
        *pComponent = *pComponent < 0 ? -0.01f : 0.01f; // 0 にはしない
    }

    return true;
}

//==================================================================================
// --- 1 フレームの描画処理 ---
//==================================================================================
// 描画順: 1. モデルの頂点を送る → 2. シーンのオブジェクト → 3. グリッド →
//         4. 補助線と選択のアウトライン → 5. ギズモ → 6. 範囲選択 → 7. 向き表示
bool Renderer::Render(const std::vector<DrawItem>& items, std::string& error, int selected, bool showGrid, GizmoMode mode,
                      bool showGizmo) {
    error.clear();
    if (!p_ || !p_->pDevice) {
        error = "Renderer is not initialized";
        return false;
    }

    auto& p = *p_;
    auto& device = *p.pDevice;
    if (!p.sResizeError.empty()) {
        error = p.sResizeError;
        return false;
    } // 大きさ変更の失敗を報告

    // 1. モデルの頂点を固定の頂点の後ろに並べて送る
    std::vector<UINT> vModelStarts(items.size(), 0), vModelCounts(items.size(), 0);
    size_t nCursor = p.uBaseVertexCount;
    for (size_t nIndex = 0; nIndex < items.size(); ++nIndex) {
        if (!items[nIndex].bVisible || !items[nIndex].pVertices) continue;
        if (nCursor + items[nIndex].pVertices->size() > MAX_VERTEX_COUNT) {
            error = "Model vertex limit exceeded (1,048,576 vertices per frame).";
            return false;
        }

        vModelStarts[nIndex] = static_cast<UINT>(nCursor);
        vModelCounts[nIndex] = static_cast<UINT>(items[nIndex].pVertices->size());
        nCursor += items[nIndex].pVertices->size();
    }

    for (size_t nIndex = 0; nIndex < items.size(); ++nIndex) {
        if (!vModelCounts[nIndex]) continue;
        const auto& vModel = *items[nIndex].pVertices;
        static_assert(sizeof(XMFLOAT3) == sizeof(Vertex));
        if (!device.UploadVertices(vModelStarts[nIndex], reinterpret_cast<const Vertex*>(vModel.data()), vModel.size(), error)) {
            return false;
        }
    }

    // 補助線はモデルのさらに後ろに書く
    UINT uDebugStart = 0, uDebugCount = 0;
    if (showGrid && !p.vDebugLines.empty() && nCursor + p.vDebugLines.size() <= MAX_VERTEX_COUNT) {
        uDebugStart = static_cast<UINT>(nCursor);
        uDebugCount = static_cast<UINT>(p.vDebugLines.size());
        if (!device.UploadVertices(uDebugStart, reinterpret_cast<const Vertex*>(p.vDebugLines.data()), p.vDebugLines.size(),
                                   error)) {
            return false;
        }
    }

    // 2. シーンのオブジェクト（プロジェクトのシェーダー。シェーディング / ワイヤーフレーム）
    if (!device.BeginFrame(CLEAR_COLOR, error)) return false;
    const float fAspect = static_cast<float>(p.uWidth) / static_cast<float>(p.uHeight);
    const XMMATRIX vp = ViewProjection(p.camera, fAspect);
    const DrawMode drawMode = showGrid ? p.drawMode : DrawMode::SHADED; // ゲームの描画は常にシェーディング
    // 不透明な物を先に描き、A < 1 の物は奥から手前の順に描く（後ろの物の上に
    // 重ねて混ぜるため。Unity の Transparent キューと同じ）。
    std::vector<size_t> vOpaque, vTransparent;
    for (size_t nIndex = 0; nIndex < items.size(); ++nIndex) {
        if (!items[nIndex].bVisible) continue;
        (items[nIndex].color.w < 0.999f ? vTransparent : vOpaque).push_back(nIndex);
    }

    { // カメラ位置からの距離で半透明の物を並べ替える
        const float fCosPitch = std::cos(p.camera.fPitch);
        const XMFLOAT3 eye{p.camera.target.x + std::sin(p.camera.fYaw) * fCosPitch * p.camera.fDistance,
                           p.camera.target.y + std::sin(p.camera.fPitch) * p.camera.fDistance,
                           p.camera.target.z - std::cos(p.camera.fYaw) * fCosPitch * p.camera.fDistance};
        auto distance2 = [&](size_t nIndex) {
            const auto& position = items[nIndex].position;
            const float dx = position.x - eye.x, dy = position.y - eye.y, dz = position.z - eye.z;
            return dx * dx + dy * dy + dz * dz;
        };
        std::sort(vTransparent.begin(), vTransparent.end(), [&](size_t a, size_t b) { return distance2(a) > distance2(b); });
    }

    // オブジェクト 1 つを描く（pOverride があればその色で描く。ワイヤーフレームの線色など）
    auto drawItem = [&](size_t nIndex, const XMFLOAT4* pOverride) {
        const auto& item = items[nIndex];
        const XMMATRIX mvp = World(item) * vp;
        const XMFLOAT4 color = pOverride ? *pOverride : item.color;
        if (vModelCounts[nIndex]) {
            // 明るさの区間ごとに色を変えて描き、メッシュを立体的に見せる（暗い区間から順）
            const auto* pRuns = item.pShadeRuns.get();
            std::uint64_t nRunTotal = 0;
            if (pRuns && !pOverride) {
                for (const auto nRun : *pRuns) {
                    nRunTotal += nRun;
                }
            }

            if (pRuns && !pOverride && nRunTotal == vModelCounts[nIndex]) {
                UINT uStart = vModelStarts[nIndex];
                for (size_t nLevel = 0; nLevel < pRuns->size(); ++nLevel) {
                    const UINT uCount = (*pRuns)[nLevel];
                    if (uCount) {
                        const float fShade =
                            0.45f + 0.55f * (static_cast<float>(nLevel) + 0.5f) / static_cast<float>(pRuns->size());
                        device.Draw(MakeConstants(mvp, {color.x * fShade, color.y * fShade, color.z * fShade, color.w}), uCount,
                                    uStart);
                    }

                    uStart += uCount;
                }
            } else { // 区間情報が無い / 合わない場合は 1 色で描く
                device.Draw(MakeConstants(mvp, color), vModelCounts[nIndex], vModelStarts[nIndex]);
            }

            return;
        }

        {
            if (item.bIs2D) {
                device.Draw(MakeConstants(mvp, color), 6, QUAD_START);
            } else {
                // 単純な既定のシェーダーでも面の区別が付くよう、面ごとに明るさを変える
                constexpr float SHADES[] = {0.75f, 0.9f, 0.62f, 0.82f, 1.0f, 0.55f};
                for (UINT uFace = 0; uFace < 6; ++uFace) {
                    const float fShade = pOverride ? 1.0f : SHADES[uFace];
                    const XMFLOAT4 tint{color.x * fShade, color.y * fShade, color.z * fShade, color.w};
                    device.Draw(MakeConstants(mvp, tint), 6, CUBE_START + uFace * 6);
                }
            }
        }
    };

    // 全オブジェクトを描く（不透明 → 半透明）
    auto drawObjects = [&](bool bWireframe, const XMFLOAT4* pOverride) {
        device.SetPipeline(bWireframe ? PipelineKind::SCENE_WIREFRAME : PipelineKind::SCENE);
        for (const size_t nIndex : vOpaque) {
            drawItem(nIndex, pOverride);
        }

        if (!bWireframe && !vTransparent.empty()) {
            device.SetPipeline(PipelineKind::SCENE_TRANSPARENT);
        }

        for (const size_t nIndex : vTransparent) {
            drawItem(nIndex, pOverride);
        }
    };
    if (drawMode != DrawMode::WIREFRAME) {
        drawObjects(false, nullptr);
    }

    if (drawMode == DrawMode::WIREFRAME) {
        drawObjects(true, nullptr);
    }

    if (drawMode == DrawMode::SHADED_WIREFRAME) { // シェーディングの上に暗い線を重ねる
        constexpr XMFLOAT4 WIRE_COLOR{0.08f, 0.08f, 0.08f, 1.0f};
        drawObjects(true, &WIRE_COLOR);
    }

    // 3. エディターのグリッドとワールドの軸
    if (showGrid) {
        // 2D の物を選んでいる時は XY 平面にグリッドを立てる
        const bool bGrid2D = selected >= 0 && static_cast<size_t>(selected) < items.size() && items[selected].bIs2D;
        const XMMATRIX gridMvp = bGrid2D ? XMMatrixRotationX(XM_PIDIV2) * vp : vp;
        device.SetPipeline(PipelineKind::GRID);
        device.Draw(MakeConstants(gridMvp, {0.27f, 0.30f, 0.34f, 1.0f}), GRID_COUNT, GRID_START);
        device.Draw(MakeConstants(gridMvp, {0.65f, 0.20f, 0.20f, 1}), 2, AXIS_X_START);
        device.Draw(MakeConstants(gridMvp, {0.20f, 0.52f, 0.27f, 1}), 2, AXIS_Z_START);
    }

    // 4. 補助線と選択のアウトライン（主に選択中の物は最後に、明るいオレンジで）
    device.SetPipeline(PipelineKind::OVERLAY);
    if (uDebugCount) {
        device.Draw(MakeConstants(vp, p.debugColor), uDebugCount, uDebugStart);
    }

    auto outline = [&](int nIndex) {
        if (nIndex < 0 || static_cast<size_t>(nIndex) >= items.size()) return;
        const auto& item = items[nIndex];
        if (!item.bVisible) return;
        const auto color = nIndex == selected ? XMFLOAT4{1.0f, 0.63f, 0.08f, 1.0f} : XMFLOAT4{1.0f, 0.76f, 0.25f, 1.0f};
        device.Draw(MakeConstants(XMMatrixScaling(1.025f, 1.025f, 1.025f) * World(item) * vp, color), item.bIs2D ? 8 : 24,
                    item.bIs2D ? QUAD_LINES_START : CUBE_LINES_START);
    };
    if (showGrid || showGizmo) {
        for (int nIndex : p.vSelection) {
            if (nIndex != selected) {
                outline(nIndex);
            }
        }
    }

    outline(selected);

    // 5. 主に選択中の物の変形ギズモ
    if (showGizmo && selected >= 0 && static_cast<size_t>(selected) < items.size()) {
        const auto& item = items[selected];
        float fLength;
        if (GizmoLength(p.camera, item, p.uWidth, p.uHeight, fLength)) {
            const auto base = XMMatrixScaling(fLength, fLength, fLength) *
                              XMMatrixTranslation(item.position.x, item.position.y, item.position.z) * vp;
            for (int nAxis = 0; nAxis < 3; ++nAxis) {
                const auto gizmoMvp =
                    (mode == GizmoMode::Rotate ? RingRotation(nAxis) : GizmoAxisRotation(item, nAxis, mode)) * base;
                const auto color = nAxis == p.nGizmoHot ? HOT_COLOR : AXIS_COLORS[nAxis];

                // 線は 1 ピクセル幅しかないので、同じ図形を上下左右に 1 ピクセルずつ
                // ずらして重ね描きし、3 ピクセル幅に見せる
                constexpr float OFFSETS[][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                for (const auto& offset : OFFSETS) {
                    const auto shifted =
                        gizmoMvp * XMMatrixTranslation(2.0f * offset[0] / p.uWidth, 2.0f * offset[1] / p.uHeight, 0);
                    if (mode == GizmoMode::Rotate) {
                        device.Draw(MakeConstants(shifted, color), GIZMO_RING_SEGMENTS * 2, GIZMO_RING_START);
                    } else if (mode == GizmoMode::Scale) {
                        device.Draw(MakeConstants(shifted, color), 10, GIZMO_SCALE_START);
                    } else {
                        device.Draw(MakeConstants(shifted, color), 6, GIZMO_ARROW_START);
                    }
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

    // 6. 範囲選択の四角形（画面座標をクリップ空間へ直して描く）
    if (p.bMarquee && (showGrid || showGizmo) && p.uWidth && p.uHeight) {
        const float fLeft = 2.0f * p.nRectX0 / p.uWidth - 1.0f;
        const float fTop = 1.0f - 2.0f * p.nRectY0 / p.uHeight;
        const float fDx = 2.0f * (p.nRectX1 - p.nRectX0) / p.uWidth;
        const float fDy = -2.0f * (p.nRectY1 - p.nRectY0) / p.uHeight;
        device.Draw(MakeConstants(XMMatrixScaling(fDx, fDy, 1) * XMMatrixTranslation(fLeft, fTop, 0), {0.45f, 0.75f, 1.0f, 1.0f}),
                    8, MARQUEE_START);
    }

    // 7. 右上の向き表示（エディターのビューのみ）
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

            // 奥を向いている軸は暗くする（Unity と同じ）
            const float fShade = direction.z > 0.0f ? 0.55f : 1.0f;
            const XMFLOAT4 color{AXIS_COLORS[nAxis].x * fShade, AXIS_COLORS[nAxis].y * fShade, AXIS_COLORS[nAxis].z * fShade, 1};

            // +X の単位線分 (0,0,0)-(1,0,0) を、画面に投影した軸の向きへ写す行列
            const XMMATRIX shaft(fDx, fDy, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, fCenterX, fCenterY, 0, 1);
            for (const float fOffset : {-1.0f, 0.0f, 1.0f}) {
                const auto thick =
                    shaft * XMMatrixTranslation(fOffset * fPixelX * (std::abs(fDy) > std::abs(fDx) ? 1.0f : 0.0f),
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

//==================================================================================
// --- 解放処理 ---
//==================================================================================
void Renderer::Shutdown() {
    if (!p_) return;
    if (p_->pDevice) {
        p_->pDevice->Shutdown();
    }

    p_.reset();
}
} // namespace dxs
