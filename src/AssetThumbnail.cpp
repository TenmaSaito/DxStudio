//==================================================================================
//
// アセットのサムネイルのソースファイル [AssetThumbnail.cpp]
//
// 2 倍の大きさで描いてから縮小し、輪郭を滑らかにする（2x2 のスーパーサンプリング）。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "AssetThumbnail.h"

#include "Engine.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cwctype>
#include <limits>

namespace dxs {
namespace {

//**********************************************************************************
// *** 定数宣言 ***
//**********************************************************************************
constexpr int SUPER_SAMPLE = 2;                               // 縦横それぞれ何倍で描くか
constexpr float FIT_RATE = 0.84f;                             // サムネイルの大きさに対する形の大きさ
constexpr float VIEW_YAW = -0.6f;                             // 斜めから見る角度（Y 軸、ラジアン）
constexpr float VIEW_PITCH = -0.42f;                          // 上から見る角度（X 軸、ラジアン）
constexpr DirectX::XMFLOAT3 MODEL_COLOR{0.80f, 0.82f, 0.86f}; // モデルの色（明るい灰色）

// 光の向き（左上の手前から）
const DirectX::XMVECTOR LIGHT = DirectX::XMVector3Normalize(DirectX::XMVectorSet(-0.45f, 0.75f, -0.55f, 0.0f));

//**********************************************************************************
// *** 描画先（色 + 奥行き） ***
//**********************************************************************************
struct Canvas {
    int nSize = 0;                         // 1 辺のピクセル数（スーパーサンプリング後）
    std::vector<DirectX::XMFLOAT3> vColor; // 色（0..1）
    std::vector<float> vDepth;             // 奥行き（小さいほど手前。描いていない所は無限大）

    explicit Canvas(int nPixels)
        : nSize(nPixels), vColor(static_cast<size_t>(nPixels) * nPixels),
          vDepth(static_cast<size_t>(nPixels) * nPixels, std::numeric_limits<float>::infinity()) {}

    // 描いたピクセルか
    bool Covered(size_t nIndex) const { return std::isfinite(vDepth[nIndex]); }
};

// --- 小文字の拡張子の取得処理 ---
std::wstring LowerExtension(const std::filesystem::path& file) {
    std::wstring sExtension = file.extension().wstring();
    std::transform(sExtension.begin(), sExtension.end(), sExtension.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return sExtension;
}

// --- 陰影の計算処理（法線と光の向きから明るさ 0..1） ---
float Lighting(DirectX::FXMVECTOR normal) {
    const float fDiffuse = std::max(0.0f, DirectX::XMVectorGetX(DirectX::XMVector3Dot(normal, LIGHT)));
    return 0.30f + 0.70f * fDiffuse;
}

//==================================================================================
// --- 三角形の描画処理（奥行きを比べ、手前の物だけ描く） ---
//==================================================================================
void FillTriangle(Canvas& canvas, const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, const DirectX::XMFLOAT3& c,
                  const DirectX::XMFLOAT3& color) {
    // 三角形の面積（2 倍）。0 なら線になっているので描かない
    const float fArea = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::abs(fArea) < 1e-8f) return;

    // 三角形を囲む範囲だけを調べる
    const int nLeft = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int nRight = std::min(canvas.nSize - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int nTop = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int nBottom = std::min(canvas.nSize - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));

    for (int nY = nTop; nY <= nBottom; ++nY) {
        for (int nX = nLeft; nX <= nRight; ++nX) {
            // ピクセルの中心が三角形の中にあるか（3 辺の符号で判定。表裏どちらの向きでも描く）
            const float fPx = nX + 0.5f, fPy = nY + 0.5f;
            const float fW0 = ((b.x - fPx) * (c.y - fPy) - (b.y - fPy) * (c.x - fPx)) / fArea;
            const float fW1 = ((c.x - fPx) * (a.y - fPy) - (c.y - fPy) * (a.x - fPx)) / fArea;
            const float fW2 = 1.0f - fW0 - fW1;

            // 隣り合う三角形の境目に隙間ができないよう、ごくわずかにはみ出しても描く
            constexpr float EDGE_TOLERANCE = -1e-4f;
            if (fW0 < EDGE_TOLERANCE || fW1 < EDGE_TOLERANCE || fW2 < EDGE_TOLERANCE) continue;

            // 手前の物だけを描く
            const float fDepth = fW0 * a.z + fW1 * b.z + fW2 * c.z;
            const size_t nIndex = static_cast<size_t>(nY) * canvas.nSize + nX;
            if (fDepth >= canvas.vDepth[nIndex]) continue;
            canvas.vDepth[nIndex] = fDepth;
            canvas.vColor[nIndex] = color;
        }
    }
}

//==================================================================================
// --- モデルの描画処理 ---
//==================================================================================
bool DrawModel(const std::filesystem::path& file, Canvas& canvas) {
    using namespace DirectX;

    // エンジンと同じ読み込み（単位ボックスに正規化済み）
    const DrawItem draw = MeshRenderer(file).BuildDrawItem(Entity{});
    if (!draw.pVertices || draw.pVertices->size() < 3) return false;
    const auto& vSource = *draw.pVertices;

    // 斜め上から見る向きに回す（2D の図形は正面から）
    const XMMATRIX rotation = draw.bIs2D ? XMMatrixIdentity() : XMMatrixRotationY(VIEW_YAW) * XMMatrixRotationX(VIEW_PITCH);
    std::vector<XMFLOAT3> vPoints(vSource.size());
    XMFLOAT3 lo{FLT_MAX, FLT_MAX, FLT_MAX}, hi{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (size_t nIndex = 0; nIndex < vSource.size(); ++nIndex) {
        XMStoreFloat3(&vPoints[nIndex], XMVector3Transform(XMLoadFloat3(&vSource[nIndex]), rotation));
        lo = {std::min(lo.x, vPoints[nIndex].x), std::min(lo.y, vPoints[nIndex].y), std::min(lo.z, vPoints[nIndex].z)};
        hi = {std::max(hi.x, vPoints[nIndex].x), std::max(hi.y, vPoints[nIndex].y), std::max(hi.z, vPoints[nIndex].z)};
    }

    // 画面に収まる大きさにして中央に置く（画面の y は下向きなので反転する）
    const float fExtent = std::max({hi.x - lo.x, hi.y - lo.y, 1e-6f});
    const float fScale = canvas.nSize * FIT_RATE / fExtent;
    const float fCenterX = (lo.x + hi.x) * 0.5f, fCenterY = (lo.y + hi.y) * 0.5f;
    for (auto& point : vPoints) {
        point = {canvas.nSize * 0.5f + (point.x - fCenterX) * fScale, canvas.nSize * 0.5f - (point.y - fCenterY) * fScale,
                 point.z};
    }

    // 頂点ごとのマテリアルの色（モデルファイルの面ごとの色。区間は「スロット × 明るさ」の順）
    std::vector<XMFLOAT3> vVertexColors(vSource.size(), MODEL_COLOR);
    if (draw.pShadeRuns && draw.pRunColors && !draw.pRunColors->empty() &&
        draw.pShadeRuns->size() % draw.pRunColors->size() == 0) {
        const size_t nLevels = draw.pShadeRuns->size() / draw.pRunColors->size();
        size_t nVertex = 0;
        for (size_t nRun = 0; nRun < draw.pShadeRuns->size() && nVertex < vSource.size(); ++nRun) {
            const XMFLOAT4& slot = (*draw.pRunColors)[nRun / nLevels];
            for (std::uint32_t nCount = 0; nCount < (*draw.pShadeRuns)[nRun] && nVertex < vSource.size(); ++nCount) {
                vVertexColors[nVertex++] = {slot.x, slot.y, slot.z};
            }
        }
    }

    // 三角形ごとに、面の向きで陰影を付けて描く
    for (size_t nIndex = 0; nIndex + 2 < vSource.size(); nIndex += 3) {
        XMVECTOR a = XMVector3Transform(XMLoadFloat3(&vSource[nIndex]), rotation);
        XMVECTOR b = XMVector3Transform(XMLoadFloat3(&vSource[nIndex + 1]), rotation);
        XMVECTOR c = XMVector3Transform(XMLoadFloat3(&vSource[nIndex + 2]), rotation);
        XMVECTOR normal = XMVector3Cross(XMVectorSubtract(b, a), XMVectorSubtract(c, a));
        if (XMVectorGetX(XMVector3LengthSq(normal)) < 1e-20f) continue;
        normal = XMVector3Normalize(normal);

        // 見ている側（-Z）を向くように法線をそろえる（裏面も表と同じように明るくする）
        if (XMVectorGetZ(normal) > 0.0f) {
            normal = XMVectorNegate(normal);
        }

        const float fLight = Lighting(normal);
        const XMFLOAT3& base = vVertexColors[nIndex];
        const XMFLOAT3 color{std::min(1.0f, base.x) * fLight, std::min(1.0f, base.y) * fLight, std::min(1.0f, base.z) * fLight};
        FillTriangle(canvas, vPoints[nIndex], vPoints[nIndex + 1], vPoints[nIndex + 2], color);
    }

    return true;
}

//==================================================================================
// --- マテリアルの描画処理（Base Color の球） ---
//==================================================================================
bool DrawMaterial(const std::filesystem::path& file, Canvas& canvas) {
    using namespace DirectX;

    Material material;
    std::string error;
    if (!LoadMaterial(file, material, error)) return false;

    // 球の色: 面ごとの色（スロット）があれば縦の帯に分けて並べる。無ければ基本色の 1 色。
    std::vector<XMFLOAT3> vBands;
    for (const auto& slot : material.vSlots) {
        vBands.push_back({std::clamp(slot.color.x * material.baseColor.x, 0.0f, 1.0f),
                          std::clamp(slot.color.y * material.baseColor.y, 0.0f, 1.0f),
                          std::clamp(slot.color.z * material.baseColor.z, 0.0f, 1.0f)});
    }

    if (vBands.empty()) {
        vBands.push_back({std::clamp(material.baseColor.x, 0.0f, 1.0f), std::clamp(material.baseColor.y, 0.0f, 1.0f),
                          std::clamp(material.baseColor.z, 0.0f, 1.0f)});
    }

    // 鏡面反射の向き（光の向きと視線の中間）
    const XMVECTOR half = XMVector3Normalize(XMVectorAdd(LIGHT, XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f)));
    const float fRadius = canvas.nSize * FIT_RATE * 0.5f;
    const float fCenter = canvas.nSize * 0.5f;

    for (int nY = 0; nY < canvas.nSize; ++nY) {
        for (int nX = 0; nX < canvas.nSize; ++nX) {
            // 球の表面の法線（手前を向く半球）
            const float fNx = (nX + 0.5f - fCenter) / fRadius;
            const float fNy = -(nY + 0.5f - fCenter) / fRadius;
            const float fRest = 1.0f - fNx * fNx - fNy * fNy;
            if (fRest < 0.0f) continue;
            const XMVECTOR normal = XMVectorSet(fNx, fNy, -std::sqrt(fRest), 0.0f);

            // 拡散 + 鏡面反射
            const float fLight = Lighting(normal);
            const float fSpecular = 0.35f * std::pow(std::max(0.0f, XMVectorGetX(XMVector3Dot(normal, half))), 32.0f);
            const size_t nIndex = static_cast<size_t>(nY) * canvas.nSize + nX;
            const size_t nBand = std::min(vBands.size() - 1, static_cast<size_t>((fNx + 1.0f) * 0.5f * vBands.size()));
            const XMFLOAT3& base = vBands[nBand];
            canvas.vDepth[nIndex] = 0.0f;
            canvas.vColor[nIndex] = {std::min(1.0f, base.x * fLight + fSpecular), std::min(1.0f, base.y * fLight + fSpecular),
                                     std::min(1.0f, base.z * fLight + fSpecular)};
        }
    }

    return true;
}

} // namespace

//==================================================================================
// --- サムネイルを作れる種類かの判定処理 ---
//==================================================================================
bool HasAssetThumbnail(const std::filesystem::path& file) {
    const std::wstring sExtension = LowerExtension(file);
    return sExtension == L".x" || sExtension == L".fbx" || sExtension == L".dxmat";
}

//==================================================================================
// --- サムネイルの描画処理 ---
//==================================================================================
bool RenderAssetThumbnail(const std::filesystem::path& file, int nSize, std::vector<std::uint32_t>& vPixels) {
    if (nSize < 4 || nSize > 1024 || !HasAssetThumbnail(file)) return false;

    // 2 倍の大きさで描く
    Canvas canvas(nSize * SUPER_SAMPLE);
    try {
        const bool bDrawn = LowerExtension(file) == L".dxmat" ? DrawMaterial(file, canvas) : DrawModel(file, canvas);
        if (!bDrawn) return false;
    } catch (const std::exception&) {
        return false; // 読み込みの途中の例外（ファイル名の変換など）
    }

    // 縮小: 2x2 の平均の色と、描いた割合を不透明度にする
    vPixels.assign(static_cast<size_t>(nSize) * nSize, 0);
    for (int nY = 0; nY < nSize; ++nY) {
        for (int nX = 0; nX < nSize; ++nX) {
            float fR = 0.0f, fG = 0.0f, fB = 0.0f;
            int nCovered = 0;
            for (int nSy = 0; nSy < SUPER_SAMPLE; ++nSy) {
                for (int nSx = 0; nSx < SUPER_SAMPLE; ++nSx) {
                    const size_t nIndex = static_cast<size_t>(nY * SUPER_SAMPLE + nSy) * canvas.nSize + (nX * SUPER_SAMPLE + nSx);
                    if (!canvas.Covered(nIndex)) continue;
                    fR += canvas.vColor[nIndex].x;
                    fG += canvas.vColor[nIndex].y;
                    fB += canvas.vColor[nIndex].z;
                    ++nCovered;
                }
            }

            if (nCovered == 0) continue;
            const auto toByte = [nCovered](float fSum) {
                return static_cast<std::uint32_t>(std::clamp(fSum / nCovered, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            const std::uint32_t uAlpha = static_cast<std::uint32_t>(255 * nCovered / (SUPER_SAMPLE * SUPER_SAMPLE));
            vPixels[static_cast<size_t>(nY) * nSize + nX] = (uAlpha << 24) | (toByte(fR) << 16) | (toByte(fG) << 8) | toByte(fB);
        }
    }

    return true;
}

} // namespace dxs
