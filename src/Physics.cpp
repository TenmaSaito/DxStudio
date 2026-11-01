//==================================================================================
//
// 物理（当たり判定と剛体）のソースファイル [Physics.cpp]
//
// 1 ステップの流れ（StepPhysics）:
//   1. 全コンポーネントの FixedUpdate
//   2. Rigidbody とコライダーを集め、子のコライダーは親の Rigidbody に所属させる
//   3. 速度 → 位置 の順に積分（半陰的オイラー法）
//   4. 全コライダーの組み合わせで接触を調べ、押し出し + 衝撃（反発・摩擦）で解決
//      （PHYSICS_SOLVER_ITERATIONS 回繰り返す）
//   5. 動いた物体の位置を親からの相対位置に戻して書き込む
//   6. 衝突 / トリガーの Enter / Stay / Exit を両方の物体に通知
//
// 当たり判定の形: 球 / 箱（回転あり）/ 凸メッシュ / 凹メッシュ（三角形の集まり）。
// 箱と凸メッシュは分離軸判定（SAT）、三角形と球は最近点で判定する。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Physics.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <tuple>

namespace dxs {
namespace {
using namespace DirectX;

XMFLOAT3 g_gravity{0.0f, -9.81f, 0.0f}; // 重力加速度

//**********************************************************************************
// *** 定数宣言 ***
//**********************************************************************************

constexpr float PENETRATION_SLOP = 0.002f;    // 許容するめり込み量（止まっている物体が震えないように）
constexpr float POSITION_CORRECTION = 0.8f;   // 1 回の解決で取り除くめり込みの割合
constexpr float BOUNCE_THRESHOLD = 0.5f;      // これより遅い衝突は跳ねない（Unity は 2 m/s）
constexpr float CONTACT_EPSILON = 1e-6f;       // 0 とみなす小さな値
constexpr float EDGE_AXIS_BIAS = 1.05f;       // 同じくらいの深さなら辺の軸より面の法線を優先する倍率
constexpr size_t CONVEX_MAX_AXES = 64;        // これより面の向きが多い凸メッシュは方向を間引いて近似する
constexpr size_t CONVEX_MAX_EDGES = 24;       // 辺 × 辺の軸は面の少ない凸メッシュだけで使う
constexpr size_t OUTLINE_MAX_TRIANGLES = 4096; // シーンビューに輪郭を描く三角形の上限

// --- ベクトルの計算（足し算 / 引き算 / 倍 / 内積 / 外積 / 長さ / 成分ごとの最小・最大） ---
XMFLOAT3 Add(const XMFLOAT3& a, const XMFLOAT3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
XMFLOAT3 Sub(const XMFLOAT3& a, const XMFLOAT3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
XMFLOAT3 Mul(const XMFLOAT3& a, float f) { return {a.x * f, a.y * f, a.z * f}; }
float Dot(const XMFLOAT3& a, const XMFLOAT3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
XMFLOAT3 Cross(const XMFLOAT3& a, const XMFLOAT3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(const XMFLOAT3& a) { return std::sqrt(Dot(a, a)); }
XMFLOAT3 Min3(const XMFLOAT3& a, const XMFLOAT3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
XMFLOAT3 Max3(const XMFLOAT3& a, const XMFLOAT3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

//**********************************************************************************
// *** メッシュコライダーのデータ（ステップ開始時のワールド座標） ***
//**********************************************************************************
struct MeshData {
    std::vector<XMFLOAT3> vTriangles;   // 三角形リスト
    std::vector<XMFLOAT3> vTriNormals;  // 三角形ごとの法線（つぶれた三角形は 0）
    std::vector<XMFLOAT3> vHull;        // 重複を除いた頂点（凸包の投影に使う）
    std::vector<XMFLOAT3> vAxes;        // 凸包の面の向き（分離軸の候補）
    std::vector<XMFLOAT3> vEdges;       // 凸包の辺の向き（空 = 使わない）
    XMFLOAT3 boundsMin{}, boundsMax{};  // 全体を囲む箱（大まかな判定用）
};

// 形の種類: 球 / 箱 / 凸メッシュ / 凹メッシュ
enum class ShapeKind : unsigned char { SPHERE, BOX, CONVEX, MESH };

//**********************************************************************************
// *** ワールド座標の当たり判定の形 ***
//**********************************************************************************
// offset はステップ開始からの物体の移動量（メッシュの頂点に足して使う）
struct Shape {
    ShapeKind kind = ShapeKind::BOX;                    // 形の種類
    XMFLOAT3 center{};                                  // 中心
    XMFLOAT3 aAxes[3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // 箱の 3 軸の向き
    XMFLOAT3 halfExtents{};                             // 箱の半分の大きさ
    float fRadius = 0.0f;                               // 球の半径
    const MeshData* pMesh = nullptr;                    // 凸 / 凹メッシュのデータ
    XMFLOAT3 offset{};                                  // メッシュの頂点に足す移動量
    XMFLOAT3 boundsMin{}, boundsMax{};                  // 全体を囲む箱
};

//**********************************************************************************
// *** コライダー 1 つ分の参照 ***
//**********************************************************************************
struct ColliderRef {
    int nEntity = -1;        // コライダーを持つ物体の番号
    int nOwner = -1;         // これを動かす Rigidbody を持つ物体（自分か親）、-1 = 動かない
    Collider* pCollider = nullptr; // コライダー本体
    std::shared_ptr<MeshData> pMesh; // メッシュコライダーのデータ（それ以外は空）
};

//**********************************************************************************
// *** 接触情報 ***
//**********************************************************************************

struct Contact {
    XMFLOAT3 normal{}; // 法線（A から B へ向く）
    float fDepth = 0.0f; // めり込みの深さ
    XMFLOAT3 point{};    // 接触点（おおよそ）
};

// --- 箱の nAxis 軸方向の半分の大きさ ---
float Extent(const Shape& box, int nAxis) { return nAxis == 0 ? box.halfExtents.x : nAxis == 1 ? box.halfExtents.y : box.halfExtents.z; }

// --- ワールド行列（Renderer と同じ 拡大 → 回転 → 移動） ---
XMMATRIX WorldMatrix(const DrawItem& world) {
    return XMMatrixScaling(world.scale.x, world.scale.y, world.scale.z) *
           XMMatrixRotationRollPitchYaw(world.rotation.x, world.rotation.y, world.rotation.z) *
           XMMatrixTranslation(world.position.x, world.position.y, world.position.z);
}

// --- 軸の正規化（符号をそろえ、n と -n を同じ軸として扱えるようにする） ---
XMFLOAT3 Canonical(XMFLOAT3 axis) {
    const float fLength = Length(axis);
    if (fLength < 1e-8f) return {0, 0, 0};
    axis = Mul(axis, 1.0f / fLength);
    if (axis.x < -1e-6f || (std::abs(axis.x) <= 1e-6f && (axis.y < -1e-6f || (std::abs(axis.y) <= 1e-6f && axis.z < 0))))
        axis = Mul(axis, -1.0f);
    return axis;
}

// --- ほぼ同じ向きの軸が無ければ追加する処理 ---
void AddUnique(std::vector<XMFLOAT3>& vAxes, const XMFLOAT3& axis) {
    const XMFLOAT3 canonical = Canonical(axis);
    if (Dot(canonical, canonical) < 0.5f) return;
    for (const auto& existing : vAxes)
        if (Dot(existing, canonical) > 0.9995f) return;
    vAxes.push_back(canonical);
}

//==================================================================================
// --- メッシュコライダーのデータの作成処理 ---
//==================================================================================
// 物体のレンダラー（MeshRenderer / ModelRenderer）が描いているメッシュ、
// または PolygonRenderer の組み込みの立方体 / 四角形を使う。
std::shared_ptr<MeshData> BuildMeshData(const DrawItem& world, const Collider& collider, bool bConvex) {
    std::vector<XMFLOAT3> vLocal;
    if (world.pVertices && !world.pVertices->empty()) vLocal = *world.pVertices;
    else vLocal = BuildPrimitive(world.bIs2D ? PrimitiveShape::SQUARE : PrimitiveShape::CUBE);
    auto pMesh = std::make_shared<MeshData>();
    const XMMATRIX matrix = WorldMatrix(world);
    const XMFLOAT3 center{collider.m_fCenterX, collider.m_fCenterY, collider.m_fCenterZ};
    pMesh->vTriangles.reserve(vLocal.size());
    for (const auto& vertex : vLocal) {
        const XMFLOAT3 local = Add(vertex, center);
        XMFLOAT3 transformed;
        XMStoreFloat3(&transformed, XMVector3TransformCoord(XMLoadFloat3(&local), matrix));
        pMesh->vTriangles.push_back(transformed);
    }
    pMesh->vTriangles.resize(pMesh->vTriangles.size() / 3 * 3);
    if (pMesh->vTriangles.empty()) return nullptr;
    pMesh->boundsMin = pMesh->boundsMax = pMesh->vTriangles[0];
    for (const auto& vertex : pMesh->vTriangles) {
        pMesh->boundsMin = Min3(pMesh->boundsMin, vertex);
        pMesh->boundsMax = Max3(pMesh->boundsMax, vertex);
    }
    for (size_t nIndex = 0; nIndex < pMesh->vTriangles.size(); nIndex += 3) {
        const XMFLOAT3 normal = Cross(Sub(pMesh->vTriangles[nIndex + 1], pMesh->vTriangles[nIndex]),
                                      Sub(pMesh->vTriangles[nIndex + 2], pMesh->vTriangles[nIndex]));
        const float fLength = Length(normal);
        pMesh->vTriNormals.push_back(fLength > 1e-10f ? Mul(normal, 1.0f / fLength) : XMFLOAT3{0, 0, 0});
    }
    if (!bConvex) return pMesh;

    // 凸: 重複を除いた頂点 / 面の向き（多い場合は間引く）/ 辺の向き（面の少ない形だけ）
    std::map<std::tuple<long, long, long>, bool> seen;
    for (const auto& vertex : pMesh->vTriangles) {
        const auto key = std::make_tuple(std::lround(vertex.x * 1e4f), std::lround(vertex.y * 1e4f), std::lround(vertex.z * 1e4f));
        if (seen.emplace(key, true).second) pMesh->vHull.push_back(vertex);
    }
    for (const auto& normal : pMesh->vTriNormals) {
        AddUnique(pMesh->vAxes, normal);
        if (pMesh->vAxes.size() > CONVEX_MAX_AXES) break;
    }
    if (pMesh->vAxes.size() > CONVEX_MAX_AXES) {
        // 滑らかな形（球・円柱など）: 均等に散らばった方向で凸包を近似する
        pMesh->vAxes.clear();
        for (size_t nIndex = 0; nIndex < CONVEX_MAX_AXES; ++nIndex) {
            const float fY = 1.0f - 2.0f * (static_cast<float>(nIndex) + 0.5f) / CONVEX_MAX_AXES;
            const float fRadius = std::sqrt(std::max(0.0f, 1.0f - fY * fY));
            const float fAngle = 2.39996323f * static_cast<float>(nIndex); // 黄金角（方向が均等に散らばる）
            AddUnique(pMesh->vAxes, {std::cos(fAngle) * fRadius, fY, std::sin(fAngle) * fRadius});
        }
        for (const XMFLOAT3 axis : {XMFLOAT3{1, 0, 0}, XMFLOAT3{0, 1, 0}, XMFLOAT3{0, 0, 1}}) AddUnique(pMesh->vAxes, axis);
    } else {
        for (size_t nIndex = 0; nIndex < pMesh->vTriangles.size() && pMesh->vEdges.size() <= CONVEX_MAX_EDGES; nIndex += 3)
            for (int nEdge = 0; nEdge < 3; ++nEdge)
                AddUnique(pMesh->vEdges, Sub(pMesh->vTriangles[nIndex + (nEdge + 1) % 3], pMesh->vTriangles[nIndex + nEdge]));
        if (pMesh->vEdges.size() > CONVEX_MAX_EDGES) pMesh->vEdges.clear();
    }
    return pMesh;
}

//==================================================================================
// --- 当たり判定の形の作成処理（ステップ開始時の姿勢 + 移動量） ---
//==================================================================================
Shape MakeShape(const DrawItem& world, const ColliderRef& ref, const XMFLOAT3& movement) {
    const Collider& collider = *ref.pCollider;
    Shape shape;
    if (ref.pMesh) {
        shape.kind = dynamic_cast<const MeshCollider*>(&collider) && !ref.pMesh->vHull.empty() ? ShapeKind::CONVEX : ShapeKind::MESH;
        shape.pMesh = ref.pMesh.get();
        shape.offset = movement;
        shape.boundsMin = Add(ref.pMesh->boundsMin, movement);
        shape.boundsMax = Add(ref.pMesh->boundsMax, movement);
        shape.center = Mul(Add(shape.boundsMin, shape.boundsMax), 0.5f);
        return shape;
    }
    const XMMATRIX rotation = XMMatrixRotationRollPitchYaw(world.rotation.x, world.rotation.y, world.rotation.z);
    for (int nAxis = 0; nAxis < 3; ++nAxis) XMStoreFloat3(&shape.aAxes[nAxis], rotation.r[nAxis]);
    const XMFLOAT3 scale{std::abs(world.scale.x), std::abs(world.scale.y), std::abs(world.scale.z)};
    // 中心のずれ（ローカル）は頂点と同じく拡大・回転してから足す
    const XMFLOAT3 offset{collider.m_fCenterX * world.scale.x, collider.m_fCenterY * world.scale.y, collider.m_fCenterZ * world.scale.z};
    shape.center = Add(world.position, movement);
    for (int nAxis = 0; nAxis < 3; ++nAxis)
        shape.center = Add(shape.center, Mul(shape.aAxes[nAxis], nAxis == 0 ? offset.x : nAxis == 1 ? offset.y : offset.z));
    if (const auto* pSphere = dynamic_cast<const SphereCollider*>(&collider)) {
        shape.kind = ShapeKind::SPHERE;
        shape.fRadius = std::abs(pSphere->m_fRadius) * std::max({scale.x, scale.y, scale.z});
        shape.boundsMin = Sub(shape.center, {shape.fRadius, shape.fRadius, shape.fRadius});
        shape.boundsMax = Add(shape.center, {shape.fRadius, shape.fRadius, shape.fRadius});
    } else if (const auto* pBox = dynamic_cast<const BoxCollider*>(&collider)) {
        shape.kind = ShapeKind::BOX;
        shape.halfExtents = {std::abs(pBox->m_fSizeX) * scale.x * 0.5f, std::abs(pBox->m_fSizeY) * scale.y * 0.5f,
                             std::abs(pBox->m_fSizeZ) * scale.z * 0.5f};
        XMFLOAT3 reach{0, 0, 0};
        for (int nAxis = 0; nAxis < 3; ++nAxis) {
            const XMFLOAT3& axis = shape.aAxes[nAxis];
            reach = Add(reach, Mul({std::abs(axis.x), std::abs(axis.y), std::abs(axis.z)}, Extent(shape, nAxis)));
        }
        shape.boundsMin = Sub(shape.center, reach);
        shape.boundsMax = Add(shape.center, reach);
    }
    return shape;
}

// --- 囲む箱どうしが重なるかの判定 ---
bool BoundsOverlap(const XMFLOAT3& minA, const XMFLOAT3& maxA, const XMFLOAT3& minB, const XMFLOAT3& maxB) {
    return minA.x <= maxB.x && maxA.x >= minB.x && minA.y <= maxB.y && maxA.y >= minB.y && minA.z <= maxB.z && maxA.z >= minB.z;
}

// --- 球 × 球 ---
bool SphereSphere(const Shape& a, const Shape& b, Contact& contact) {
    const XMFLOAT3 delta = Sub(b.center, a.center);
    const float fDistance = Length(delta), fRadii = a.fRadius + b.fRadius;
    if (fDistance >= fRadii) return false;
    contact.normal = fDistance > CONTACT_EPSILON ? Mul(delta, 1.0f / fDistance) : XMFLOAT3{0, 1, 0};
    contact.fDepth = fRadii - fDistance;
    contact.point = Add(a.center, Mul(contact.normal, a.fRadius - contact.fDepth * 0.5f));
    return true;
}

// --- 箱 × 球（法線は箱から球へ向く） ---
bool BoxSphere(const Shape& box, const Shape& sphere, Contact& contact) {
    const XMFLOAT3 delta = Sub(sphere.center, box.center);
    float aLocal[3], aClamped[3];
    bool bInside = true;
    for (int nAxis = 0; nAxis < 3; ++nAxis) {
        aLocal[nAxis] = Dot(delta, box.aAxes[nAxis]);
        aClamped[nAxis] = std::clamp(aLocal[nAxis], -Extent(box, nAxis), Extent(box, nAxis));
        if (aClamped[nAxis] != aLocal[nAxis]) bInside = false;
    }
    if (!bInside) {
        XMFLOAT3 closest = box.center;
        for (int nAxis = 0; nAxis < 3; ++nAxis) closest = Add(closest, Mul(box.aAxes[nAxis], aClamped[nAxis]));
        const XMFLOAT3 toSphere = Sub(sphere.center, closest);
        const float fDistance = Length(toSphere);
        if (fDistance >= sphere.fRadius) return false;
        contact.normal = fDistance > CONTACT_EPSILON ? Mul(toSphere, 1.0f / fDistance) : box.aAxes[1];
        contact.fDepth = sphere.fRadius - fDistance;
        contact.point = closest;
        return true;
    }
    // 球の中心が箱の中: 一番近い面から押し出す
    int nBest = 0;
    float fBest = Extent(box, 0) - std::abs(aLocal[0]);
    for (int nAxis = 1; nAxis < 3; ++nAxis) {
        const float fFace = Extent(box, nAxis) - std::abs(aLocal[nAxis]);
        if (fFace < fBest) {
            fBest = fFace;
            nBest = nAxis;
        }
    }
    contact.normal = Mul(box.aAxes[nBest], aLocal[nBest] < 0 ? -1.0f : 1.0f);
    contact.fDepth = fBest + sphere.fRadius;
    contact.point = sphere.center;
    return true;
}

// --- 箱 × 箱（回転あり） ---
// 分離軸判定: 両方の面の軸 3 + 3 本と、辺どうしの外積 9 本を調べ、
// どれか 1 本でも重なっていなければ離れている。一番重なりの浅い軸が法線になる。
bool BoxBox(const Shape& a, const Shape& b, Contact& contact) {
    const XMFLOAT3 delta = Sub(b.center, a.center);
    float fBestOverlap = 3.4e38f;
    XMFLOAT3 bestAxis{0, 1, 0};
    auto testAxis = [&](XMFLOAT3 axis, float fBias) {
        const float fLength = Length(axis);
        if (fLength < 1e-4f) return true; // 平行な辺: この軸からは何も分からない
        axis = Mul(axis, 1.0f / fLength);
        float fProjectA = 0.0f, fProjectB = 0.0f;
        for (int nAxis = 0; nAxis < 3; ++nAxis) {
            fProjectA += std::abs(Dot(a.aAxes[nAxis], axis)) * Extent(a, nAxis);
            fProjectB += std::abs(Dot(b.aAxes[nAxis], axis)) * Extent(b, nAxis);
        }
        const float fDistance = Dot(delta, axis);
        const float fOverlap = fProjectA + fProjectB - std::abs(fDistance);
        if (fOverlap < 0.0f) return false;
        // 深さがほぼ同じなら辺の軸より面の軸を選ぶ（積み重ねが安定する）
        if (fOverlap * fBias < fBestOverlap) {
            fBestOverlap = fOverlap * fBias;
            bestAxis = fDistance < 0 ? Mul(axis, -1.0f) : axis;
            contact.fDepth = fOverlap;
        }
        return true;
    };
    for (int nAxis = 0; nAxis < 3; ++nAxis)
        if (!testAxis(a.aAxes[nAxis], 1.0f) || !testAxis(b.aAxes[nAxis], 1.0f)) return false;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (!testAxis(Cross(a.aAxes[i], b.aAxes[j]), EDGE_AXIS_BIAS)) return false;
    contact.normal = bestAxis;
    // おおよその接触点: B の一番めり込んだ角から、めり込みの半分だけ A 側へ戻した点
    XMFLOAT3 support = b.center;
    for (int nAxis = 0; nAxis < 3; ++nAxis)
        support = Add(support, Mul(b.aAxes[nAxis], (Dot(b.aAxes[nAxis], bestAxis) > 0 ? -1.0f : 1.0f) * Extent(b, nAxis)));
    contact.point = Add(support, Mul(bestAxis, contact.fDepth * 0.5f));
    return true;
}

//**********************************************************************************
// *** 汎用の凸多面体（箱・三角形・凸メッシュ） ***
//**********************************************************************************
struct Polytope {
    const XMFLOAT3* pVertices = nullptr; // 頂点
    size_t nVertices = 0;                // 頂点数
    XMFLOAT3 offset{};                   // 頂点に足す移動量
    const XMFLOAT3* pAxes = nullptr; // 面の向き
    size_t nAxes = 0;                    // 面の向きの数
    const XMFLOAT3* pEdges = nullptr;    // 辺の向き
    size_t nEdges = 0;                   // 辺の向きの数
};

// 箱を凸多面体として扱うための配列を持つ（8 頂点・3 軸）
struct BoxPolytope {
    XMFLOAT3 aCorners[8];
    Polytope poly;
    explicit BoxPolytope(const Shape& box) {
        for (int nCorner = 0; nCorner < 8; ++nCorner) {
            aCorners[nCorner] = box.center;
            for (int nAxis = 0; nAxis < 3; ++nAxis)
                aCorners[nCorner] = Add(aCorners[nCorner], Mul(box.aAxes[nAxis], ((nCorner >> nAxis) & 1 ? 1.0f : -1.0f) * Extent(box, nAxis)));
        }
        poly = {aCorners, 8, {0, 0, 0}, box.aAxes, 3, box.aAxes, 3};
    }
};

// --- 凸多面体を軸に投影した範囲の計算 ---
void Project(const Polytope& poly, const XMFLOAT3& axis, float& fMin, float& fMax) {
    fMin = 3.4e38f;
    fMax = -3.4e38f;
    const float fShift = Dot(poly.offset, axis);
    for (size_t nIndex = 0; nIndex < poly.nVertices; ++nIndex) {
        const float fValue = Dot(poly.pVertices[nIndex], axis) + fShift;
        fMin = std::min(fMin, fValue);
        fMax = std::max(fMax, fValue);
    }
}

// --- 凸多面体 × 凸多面体（分離軸判定） ---
bool PolyPoly(const Polytope& a, const Polytope& b, Contact& contact) {
    float fBest = 3.4e38f;
    auto testAxis = [&](XMFLOAT3 axis, float fBias) {
        const float fLength = Length(axis);
        if (fLength < 1e-4f) return true;
        axis = Mul(axis, 1.0f / fLength);
        float fMinA, fMaxA, fMinB, fMaxB;
        Project(a, axis, fMinA, fMaxA);
        Project(b, axis, fMinB, fMaxB);
        const float fForward = fMaxA - fMinB;  // B が軸の + 側にあるときの重なり
        const float fBackward = fMaxB - fMinA; // B が軸の - 側にあるときの重なり
        if (fForward < 0.0f || fBackward < 0.0f) return false;
        const float fOverlap = std::min(fForward, fBackward);
        if (fOverlap * fBias < fBest) {
            fBest = fOverlap * fBias;
            contact.fDepth = fOverlap;
            contact.normal = fForward <= fBackward ? axis : Mul(axis, -1.0f);
        }
        return true;
    };
    for (size_t n = 0; n < a.nAxes; ++n) if (!testAxis(a.pAxes[n], 1.0f)) return false;
    for (size_t n = 0; n < b.nAxes; ++n) if (!testAxis(b.pAxes[n], 1.0f)) return false;
    for (size_t i = 0; i < a.nEdges; ++i)
        for (size_t j = 0; j < b.nEdges; ++j)
            if (!testAxis(Cross(a.pEdges[i], b.pEdges[j]), EDGE_AXIS_BIAS)) return false;
    // 接触点: B の一番めり込んだ頂点から、深さの半分だけ戻した点
    float fDeepest = 3.4e38f;
    XMFLOAT3 support{};
    for (size_t nIndex = 0; nIndex < b.nVertices; ++nIndex) {
        const XMFLOAT3 vertex = Add(b.pVertices[nIndex], b.offset);
        const float fValue = Dot(vertex, contact.normal);
        if (fValue < fDeepest) {
            fDeepest = fValue;
            support = vertex;
        }
    }
    contact.point = Add(support, Mul(contact.normal, contact.fDepth * 0.5f));
    return true;
}

// --- 凸多面体 × 球（法線は A から球へ向く） ---
bool PolySphere(const Polytope& a, const Shape& sphere, Contact& contact) {
    float fBest = 3.4e38f;
    auto testAxis = [&](XMFLOAT3 axis) {
        const float fLength = Length(axis);
        if (fLength < 1e-6f) return true;
        axis = Mul(axis, 1.0f / fLength);
        float fMinA, fMaxA;
        Project(a, axis, fMinA, fMaxA);
        const float fCenter = Dot(sphere.center, axis);
        const float fForward = fMaxA - (fCenter - sphere.fRadius);
        const float fBackward = (fCenter + sphere.fRadius) - fMinA;
        if (fForward < 0.0f || fBackward < 0.0f) return false;
        const float fOverlap = std::min(fForward, fBackward);
        if (fOverlap < fBest) {
            fBest = fOverlap;
            contact.fDepth = fOverlap;
            contact.normal = fForward <= fBackward ? axis : Mul(axis, -1.0f);
        }
        return true;
    };
    for (size_t n = 0; n < a.nAxes; ++n) if (!testAxis(a.pAxes[n])) return false;
    // 一番近い頂点への向きも調べる（角に当たる場合を捉える）
    float fNearest = 3.4e38f;
    XMFLOAT3 nearest{};
    for (size_t nIndex = 0; nIndex < a.nVertices; ++nIndex) {
        const XMFLOAT3 vertex = Add(a.pVertices[nIndex], a.offset);
        const float fDistance = Dot(Sub(sphere.center, vertex), Sub(sphere.center, vertex));
        if (fDistance < fNearest) {
            fNearest = fDistance;
            nearest = vertex;
        }
    }
    if (!testAxis(Sub(sphere.center, nearest))) return false;
    contact.point = Sub(sphere.center, Mul(contact.normal, sphere.fRadius - contact.fDepth * 0.5f));
    return true;
}

// --- 三角形 abc 上の、点 p に一番近い点（Ericson『Real-Time Collision Detection』5.1.5） ---
XMFLOAT3 ClosestOnTriangle(const XMFLOAT3& p, const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c) {
    const XMFLOAT3 ab = Sub(b, a), ac = Sub(c, a), ap = Sub(p, a);
    const float d1 = Dot(ab, ap), d2 = Dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    const XMFLOAT3 bp = Sub(p, b);
    const float d3 = Dot(ab, bp), d4 = Dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return Add(a, Mul(ab, d1 / (d1 - d3)));
    const XMFLOAT3 cp = Sub(p, c);
    const float d5 = Dot(ab, cp), d6 = Dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return Add(a, Mul(ac, d2 / (d2 - d6)));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return Add(b, Mul(Sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    const float fDenominator = 1.0f / (va + vb + vc);
    return Add(a, Add(Mul(ab, vb * fDenominator), Mul(ac, vc * fDenominator)));
}

// --- 凸メッシュの形を凸多面体として見る ---
Polytope ConvexPolytope(const Shape& shape) {
    const auto& mesh = *shape.pMesh;
    return {mesh.vHull.data(), mesh.vHull.size(), shape.offset, mesh.vAxes.data(), mesh.vAxes.size(), mesh.vEdges.data(), mesh.vEdges.size()};
}

// --- 凹メッシュ A × それ以外の形 B（一番深く当たった三角形の接触を返す） ---
bool MeshVersus(const Shape& mesh, const Shape& other, Contact& contact) {
    if (!BoundsOverlap(mesh.boundsMin, mesh.boundsMax, other.boundsMin, other.boundsMax)) return false;
    std::unique_ptr<BoxPolytope> pBox;
    Polytope otherPoly;
    if (other.kind == ShapeKind::BOX) {
        pBox = std::make_unique<BoxPolytope>(other);
        otherPoly = pBox->poly;
    } else if (other.kind == ShapeKind::CONVEX) {
        otherPoly = ConvexPolytope(other);
    }
    const auto& data = *mesh.pMesh;
    bool bHit = false;
    for (size_t nTriangle = 0; nTriangle * 3 + 2 < data.vTriangles.size(); ++nTriangle) {
        const XMFLOAT3& normal = data.vTriNormals[nTriangle];
        if (Dot(normal, normal) < 0.5f) continue;
        const XMFLOAT3 a = Add(data.vTriangles[nTriangle * 3], mesh.offset);
        const XMFLOAT3 b = Add(data.vTriangles[nTriangle * 3 + 1], mesh.offset);
        const XMFLOAT3 c = Add(data.vTriangles[nTriangle * 3 + 2], mesh.offset);
        if (!BoundsOverlap(Min3(a, Min3(b, c)), Max3(a, Max3(b, c)), other.boundsMin, other.boundsMax)) continue;
        Contact candidate;
        if (other.kind == ShapeKind::SPHERE) {
            const XMFLOAT3 closest = ClosestOnTriangle(other.center, a, b, c);
            const XMFLOAT3 toSphere = Sub(other.center, closest);
            const float fDistance = Length(toSphere);
            if (fDistance >= other.fRadius) continue;
            candidate.normal = fDistance > CONTACT_EPSILON ? Mul(toSphere, 1.0f / fDistance) : normal;
            candidate.fDepth = other.fRadius - fDistance;
            candidate.point = closest;
        } else {
            const XMFLOAT3 aCorners[3] = {a, b, c};
            const XMFLOAT3 aEdges[3] = {Sub(b, a), Sub(c, b), Sub(a, c)};
            const Polytope triangle{aCorners, 3, {0, 0, 0}, &normal, 1, aEdges, 3};
            if (!PolyPoly(triangle, otherPoly, candidate)) continue;
        }
        if (!bHit || candidate.fDepth > contact.fDepth) contact = candidate;
        bHit = true;
    }
    return bHit;
}

//==================================================================================
// --- 2 つの形の当たり判定（形の組み合わせで振り分け） ---
//==================================================================================
bool Collide(const Shape& a, const Shape& b, Contact& contact) {
    if (!BoundsOverlap(a.boundsMin, a.boundsMax, b.boundsMin, b.boundsMax)) return false;
    const ShapeKind kindA = a.kind, kindB = b.kind;
    auto flipped = [&](bool bHit) {
        if (bHit) contact.normal = Mul(contact.normal, -1.0f);
        return bHit;
    };
    if (kindA == ShapeKind::MESH && kindB == ShapeKind::MESH) return false; // Unity と同じく、どちらかは凸である必要がある
    if (kindA == ShapeKind::MESH) return MeshVersus(a, b, contact);
    if (kindB == ShapeKind::MESH) return flipped(MeshVersus(b, a, contact));
    if (kindA == ShapeKind::SPHERE && kindB == ShapeKind::SPHERE) return SphereSphere(a, b, contact);
    if (kindA == ShapeKind::BOX && kindB == ShapeKind::BOX) return BoxBox(a, b, contact);
    if (kindA == ShapeKind::BOX && kindB == ShapeKind::SPHERE) return BoxSphere(a, b, contact);
    if (kindA == ShapeKind::SPHERE && kindB == ShapeKind::BOX) return flipped(BoxSphere(b, a, contact));
    // 凸メッシュを含む組み合わせ
    if (kindB == ShapeKind::SPHERE) return PolySphere(ConvexPolytope(a), b, contact);
    if (kindA == ShapeKind::SPHERE) return flipped(PolySphere(ConvexPolytope(b), a, contact));
    std::unique_ptr<BoxPolytope> pBoxA, pBoxB;
    Polytope polyA, polyB;
    if (kindA == ShapeKind::BOX) { pBoxA = std::make_unique<BoxPolytope>(a); polyA = pBoxA->poly; } else polyA = ConvexPolytope(a);
    if (kindB == ShapeKind::BOX) { pBoxB = std::make_unique<BoxPolytope>(b); polyB = pBoxB->poly; } else polyB = ConvexPolytope(b);
    return PolyPoly(polyA, polyB, contact);
}

// --- 物体の組を 1 つの数にする（小さい番号を上位に） ---
std::uint64_t PairKey(int nA, int nB) {
    if (nA > nB) std::swap(nA, nB);
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(nA)) << 32) | static_cast<std::uint32_t>(nB);
}

// --- 物体の全コンポーネントに処理を行う ---
template<class Callback> void ForEachComponent(Scene& scene, int nEntity, Callback&& callback) {
    if (nEntity < 0 || static_cast<size_t>(nEntity) >= scene.vEntities.size()) return;
    auto& entity = scene.vEntities[static_cast<size_t>(nEntity)];
    // 添字で回す: 処理の中でコンポーネントが追加される（配列が伸びる）こともあるため
    for (size_t nIndex = 0; nIndex < entity.vRuntimeComponents.size(); ++nIndex)
        if (auto* pComponent = entity.vRuntimeComponents[nIndex].get()) callback(*pComponent, entity);
}

// 衝突 / トリガー 1 組分のイベント
struct PairEvent {
    std::uint64_t uKey = 0; // 物体の組の番号
    int nA = -1, nB = -1;   // 物体の番号
    Contact contact;        // 接触情報
    float fSpeed = 0.0f;    // ぶつかった速さ
};

// --- コライダーのコンポーネント名か ---
bool IsColliderName(const std::string& sName) {
    return sName == "BoxCollider" || sName == "SphereCollider" || sName == "MeshCollider";
}

// --- インスペクターに保存された値でコライダーを作る（編集中の輪郭表示用） ---
std::unique_ptr<Collider> SavedCollider(const Entity& entity, const std::string& sName) {
    std::unique_ptr<Collider> pCollider;
    if (sName == "BoxCollider") pCollider = std::make_unique<BoxCollider>();
    else if (sName == "SphereCollider") pCollider = std::make_unique<SphereCollider>();
    else if (sName == "MeshCollider") pCollider = std::make_unique<MeshCollider>();
    else return nullptr;
    const auto saved = entity.componentProperties.find(sName);
    if (saved == entity.componentProperties.end()) return pCollider;
    for (auto& field : pCollider->EditorFields()) {
        const auto value = saved->second.find(field.sName);
        if (value == saved->second.end()) continue;
        try {
            if (field.type == EditorFieldType::Float) *static_cast<float*>(field.pValue) = std::stof(value->second);
            else if (field.type == EditorFieldType::Boolean) *static_cast<bool*>(field.pValue) = value->second == "true" || value->second == "1";
        } catch (...) { /* keep the default for a malformed value */ }
    }
    return pCollider;
}

// --- シーンビュー用の輪郭線の追加処理 ---
void AppendOutline(const Shape& shape, std::vector<XMFLOAT3>& vSegments) {
    if (shape.kind == ShapeKind::SPHERE) {
        // 3 つの大円（XY / YZ / XZ）。Unity の球の表示と同じ
        constexpr int SEGMENTS = 32;
        for (int nPlane = 0; nPlane < 3; ++nPlane) {
            for (int nIndex = 0; nIndex < SEGMENTS; ++nIndex) {
                for (const int nStep : {nIndex, nIndex + 1}) {
                    const float fAngle = XM_2PI * static_cast<float>(nStep) / SEGMENTS;
                    const float fC = std::cos(fAngle) * shape.fRadius, fS = std::sin(fAngle) * shape.fRadius;
                    const XMFLOAT3 offset = nPlane == 0 ? XMFLOAT3{fC, fS, 0} : nPlane == 1 ? XMFLOAT3{0, fC, fS} : XMFLOAT3{fC, 0, fS};
                    vSegments.push_back(Add(shape.center, offset));
                }
            }
        }
        return;
    }
    if (shape.pMesh) {
        // メッシュ: 三角形の辺（とても細かいメッシュは上限まで）
        const auto& vTriangles = shape.pMesh->vTriangles;
        const size_t nTriangles = std::min(vTriangles.size() / 3, OUTLINE_MAX_TRIANGLES);
        for (size_t nTriangle = 0; nTriangle < nTriangles; ++nTriangle)
            for (int nEdge = 0; nEdge < 3; ++nEdge) {
                vSegments.push_back(Add(vTriangles[nTriangle * 3 + nEdge], shape.offset));
                vSegments.push_back(Add(vTriangles[nTriangle * 3 + (nEdge + 1) % 3], shape.offset));
            }
        return;
    }
    XMFLOAT3 aCorners[8];
    for (int nCorner = 0; nCorner < 8; ++nCorner) {
        aCorners[nCorner] = shape.center;
        for (int nAxis = 0; nAxis < 3; ++nAxis)
            aCorners[nCorner] = Add(aCorners[nCorner], Mul(shape.aAxes[nAxis], ((nCorner >> nAxis) & 1 ? 1.0f : -1.0f) * Extent(shape, nAxis)));
    }
    for (int nCorner = 0; nCorner < 8; ++nCorner)
        for (int nAxis = 0; nAxis < 3; ++nAxis)
            if (!((nCorner >> nAxis) & 1)) {
                vSegments.push_back(aCorners[nCorner]);
                vSegments.push_back(aCorners[nCorner | (1 << nAxis)]);
            }
}
} // namespace

//==================================================================================
// --- コライダーの輪郭線の取得処理（シーンビュー用） ---
//==================================================================================
std::vector<XMFLOAT3> ColliderOutlineSegments(const Scene& scene) {
    std::vector<XMFLOAT3> vSegments;
    std::vector<DrawItem> vWorld;
    for (size_t nEntity = 0; nEntity < scene.vEntities.size(); ++nEntity) {
        const auto& entity = scene.vEntities[nEntity];
        const bool bLive = entity.vRuntimeComponentNames == entity.vComponents && entity.vRuntimeComponents.size() == entity.vComponents.size();
        for (size_t nIndex = 0; nIndex < entity.vComponents.size(); ++nIndex) {
            const auto& sName = entity.vComponents[nIndex];
            if (!IsColliderName(sName)) continue;
            if (vWorld.empty()) vWorld = ResolveWorldDraws(scene);
            const auto* pLive = bLive ? dynamic_cast<Collider*>(entity.vRuntimeComponents[nIndex].get()) : nullptr;
            const bool bStarted = pLive && nIndex < entity.vRuntimeComponentStarted.size() && entity.vRuntimeComponentStarted[nIndex];
            std::unique_ptr<Collider> pSaved;
            if (!bStarted) pSaved = SavedCollider(entity, sName);
            ColliderRef ref;
            ref.pCollider = bStarted ? const_cast<Collider*>(pLive) : pSaved.get();
            if (dynamic_cast<const MeshCollider*>(ref.pCollider))
                ref.pMesh = BuildMeshData(vWorld[nEntity], *ref.pCollider, false);
            AppendOutline(MakeShape(vWorld[nEntity], ref, {0, 0, 0}), vSegments);
        }
    }
    return vSegments;
}

//==================================================================================
// --- 重力の取得 / 設定処理 ---
//==================================================================================
namespace physics {
XMFLOAT3 Gravity() noexcept { return g_gravity; }
void SetGravity(const XMFLOAT3& gravity) noexcept { g_gravity = gravity; }
} // namespace physics

//==================================================================================
// --- 力を加える処理 ---
//==================================================================================
void Rigidbody::AddForce(const XMFLOAT3& force, ForceMode mode) {
    const float fInverseMass = m_fMass > 0.0f ? 1.0f / m_fMass : 0.0f;
    switch (mode) {
    case ForceMode::FORCE: m_acceleration = Add(m_acceleration, Mul(force, fInverseMass)); break;
    case ForceMode::ACCELERATION: m_acceleration = Add(m_acceleration, force); break;
    case ForceMode::IMPULSE: m_velocity = Add(m_velocity, Mul(force, fInverseMass)); break;
    case ForceMode::VELOCITY_CHANGE: m_velocity = Add(m_velocity, force); break;
    }
}

//==================================================================================
// --- 経過時間分の固定ステップの実行処理 ---
//==================================================================================
void SimulatePhysics(Scene& scene, float fDeltaSeconds) {
    auto& state = scene.physics;
    // 貯めた時間が 1 ステップ分を超えるたびに進める（上限ありで、端数は次のフレームへ）
    state.fAccumulator = std::min(state.fAccumulator + std::max(fDeltaSeconds, 0.0f),
                                  PHYSICS_FIXED_DELTA * PHYSICS_MAX_STEPS_PER_FRAME);
    while (state.fAccumulator >= PHYSICS_FIXED_DELTA) {
        StepPhysics(scene, PHYSICS_FIXED_DELTA);
        state.fAccumulator -= PHYSICS_FIXED_DELTA;
    }
}

//==================================================================================
// --- 物理の 1 ステップ処理 ---
//==================================================================================
void StepPhysics(Scene& scene, float fFixedDelta) {
    const int nEntityCount = static_cast<int>(scene.vEntities.size());
    for (int nEntity = 0; nEntity < nEntityCount; ++nEntity)
        ForEachComponent(scene, nEntity, [&](Component& component, Entity& entity) { component.FixedUpdate(entity, fFixedDelta); }); // 1. FixedUpdate

    // 2. Rigidbody を集め、続いてコライダーと所属先を決める: Rigidbody を持たない
    //    物体のコライダーは、Rigidbody を持つ一番近い親に所属する（Unity の複合コライダー）
    std::vector<Rigidbody*> vBodies(scene.vEntities.size(), nullptr);
    bool bAnyBody = false;
    for (int nEntity = 0; nEntity < nEntityCount; ++nEntity) {
        vBodies[static_cast<size_t>(nEntity)] = GetComponent<Rigidbody>(scene.vEntities[static_cast<size_t>(nEntity)]);
        bAnyBody = bAnyBody || vBodies[static_cast<size_t>(nEntity)];
    }
    if (!bAnyBody) { // 動く物体が 1 つも無ければ何もしない
        scene.physics.vContacts.clear();
        scene.physics.vTriggers.clear();
        return;
    }
    // 自分または親をたどって、Rigidbody を持つ物体を探す（無ければ -1）
    auto ownerOf = [&](int nEntity) {
        for (int nGuard = 0; nEntity >= 0 && nGuard <= nEntityCount; ++nGuard) {
            if (vBodies[static_cast<size_t>(nEntity)]) return nEntity;
            nEntity = scene.vEntities[static_cast<size_t>(nEntity)].nParent;
            if (nEntity >= nEntityCount) return -1;
        }
        return -1;
    };
    const auto vBase = ResolveWorldDraws(scene); // ステップ開始時のワールドでの姿勢
    std::vector<ColliderRef> vColliders;
    for (int nEntity = 0; nEntity < nEntityCount; ++nEntity) {
        auto& entity = scene.vEntities[static_cast<size_t>(nEntity)];
        for (auto& pComponent : entity.vRuntimeComponents) {
            auto* pCollider = dynamic_cast<Collider*>(pComponent.get());
            if (!pCollider) continue;
            ColliderRef ref{nEntity, ownerOf(nEntity), pCollider, nullptr};
            if (const auto* pMeshCollider = dynamic_cast<const MeshCollider*>(pCollider)) {
                // 動く物体は凹メッシュを使えない（Unity では Convex が必須）ので凸として扱う
                const bool bConvex = pMeshCollider->m_bConvex ||
                                     (ref.nOwner >= 0 && !vBodies[static_cast<size_t>(ref.nOwner)]->m_bIsKinematic);
                ref.pMesh = BuildMeshData(vBase[static_cast<size_t>(nEntity)], *pCollider, bConvex);
                if (!ref.pMesh) continue;
            }
            vColliders.push_back(std::move(ref));
        }
    }

    // 3. 速度 → 位置 の順に積分する（半陰的オイラー法、ワールド座標）
    std::vector<XMFLOAT3> vPosition(vBase.size());
    for (size_t nEntity = 0; nEntity < vBase.size(); ++nEntity) vPosition[nEntity] = vBase[nEntity].position;
    std::vector<bool> vMoved(vBase.size(), false);
    for (int nEntity = 0; nEntity < nEntityCount; ++nEntity) {
        auto* pBody = vBodies[static_cast<size_t>(nEntity)];
        if (!pBody) continue;
        if (pBody->m_bIsKinematic) {
            pBody->m_acceleration = {0, 0, 0};
            continue; // キネマティックはスクリプトでのみ動く
        }
        XMFLOAT3 acceleration = pBody->m_acceleration;
        if (pBody->m_bUseGravity) acceleration = Add(acceleration, g_gravity);
        pBody->m_velocity = Add(pBody->m_velocity, Mul(acceleration, fFixedDelta));
        pBody->m_velocity = Mul(pBody->m_velocity, 1.0f / (1.0f + std::max(pBody->m_fDrag, 0.0f) * fFixedDelta));
        pBody->m_acceleration = {0, 0, 0}; // 加えられた力はこのステップで使い切る
        vPosition[static_cast<size_t>(nEntity)] = Add(vPosition[static_cast<size_t>(nEntity)], Mul(pBody->m_velocity, fFixedDelta));
        vMoved[static_cast<size_t>(nEntity)] = true;
    }
    // コライダーは所属先と一緒に動く: ステップ開始時の姿勢 + 所属先の移動量
    auto shapeOf = [&](const ColliderRef& ref) {
        XMFLOAT3 movement{0, 0, 0};
        if (ref.nOwner >= 0) movement = Sub(vPosition[static_cast<size_t>(ref.nOwner)], vBase[static_cast<size_t>(ref.nOwner)].position);
        return MakeShape(vBase[static_cast<size_t>(ref.nEntity)], ref, movement);
    };

    // 4. 接触の判定と解決（逐次インパルス法。質量・速度は所属先の Rigidbody の物）
    std::vector<PairEvent> vContactEvents, vTriggerEvents;
    auto inverseMass = [&](int nOwner) { return nOwner >= 0 ? vBodies[static_cast<size_t>(nOwner)]->InverseMass() : 0.0f; };
    auto velocity = [&](int nOwner) { return nOwner >= 0 ? vBodies[static_cast<size_t>(nOwner)]->m_velocity : XMFLOAT3{0, 0, 0}; };
    for (int nIteration = 0; nIteration < PHYSICS_SOLVER_ITERATIONS; ++nIteration) {
        for (size_t i = 0; i < vColliders.size(); ++i) {
            for (size_t j = i + 1; j < vColliders.size(); ++j) {
                const int nA = vColliders[i].nOwner, nB = vColliders[j].nOwner;
                if (nA < 0 && nB < 0) continue;       // 動かない物どうし
                if (nA == nB) continue;               // 同じ物体の部品どうし
                const bool bTrigger = vColliders[i].pCollider->m_bIsTrigger || vColliders[j].pCollider->m_bIsTrigger;
                const float fInvA = inverseMass(nA), fInvB = inverseMass(nB);
                if (!bTrigger && fInvA + fInvB <= 0.0f && nIteration > 0) continue; // キネマティック × 動かない物: イベントだけ（初回のみ判定）
                Contact contact;
                if (!Collide(shapeOf(vColliders[i]), shapeOf(vColliders[j]), contact)) continue;
                // イベントは Rigidbody を持つ物体へ（動かない物はコライダーの物体へ）
                const int nEventA = nA >= 0 ? nA : vColliders[i].nEntity, nEventB = nB >= 0 ? nB : vColliders[j].nEntity;
                const std::uint64_t uKey = PairKey(nEventA, nEventB);
                auto& vEvents = bTrigger ? vTriggerEvents : vContactEvents;
                const XMFLOAT3 relative = Sub(velocity(nB), velocity(nA));
                const float fNormalSpeed = Dot(relative, contact.normal);
                if (nIteration == 0 && std::none_of(vEvents.begin(), vEvents.end(), [&](const PairEvent& e) { return e.uKey == uKey; }))
                    vEvents.push_back({uKey, nEventA, nEventB, contact, std::max(-fNormalSpeed, 0.0f)});
                if (bTrigger || fInvA + fInvB <= 0.0f) continue;

                // めり込みを押し戻す（質量の逆数の比で分ける）
                const float fCorrection = std::max(contact.fDepth - PENETRATION_SLOP, 0.0f) * POSITION_CORRECTION / (fInvA + fInvB);
                if (fInvA > 0) vPosition[static_cast<size_t>(nA)] = Sub(vPosition[static_cast<size_t>(nA)], Mul(contact.normal, fCorrection * fInvA));
                if (fInvB > 0) vPosition[static_cast<size_t>(nB)] = Add(vPosition[static_cast<size_t>(nB)], Mul(contact.normal, fCorrection * fInvB));
                if (fNormalSpeed >= 0.0f) continue; // 既に離れようとしている

                // 法線方向の衝撃（反発係数は大きい方を使う。Unity の Maximum と同じ）
                auto* pA = nA >= 0 ? vBodies[static_cast<size_t>(nA)] : nullptr;
                auto* pB = nB >= 0 ? vBodies[static_cast<size_t>(nB)] : nullptr;
                float fBounce = std::max(pA ? pA->m_fBounciness : 0.0f, pB ? pB->m_fBounciness : 0.0f);
                if (-fNormalSpeed < BOUNCE_THRESHOLD) fBounce = 0.0f;
                const float fImpulse = -(1.0f + std::clamp(fBounce, 0.0f, 1.0f)) * fNormalSpeed / (fInvA + fInvB);
                if (pA && fInvA > 0) pA->m_velocity = Sub(pA->m_velocity, Mul(contact.normal, fImpulse * fInvA));
                if (pB && fInvB > 0) pB->m_velocity = Add(pB->m_velocity, Mul(contact.normal, fImpulse * fInvB));

                // 滑る方向のクーロン摩擦（摩擦係数は両方の平均）
                const XMFLOAT3 after = Sub(velocity(nB), velocity(nA));
                XMFLOAT3 tangent = Sub(after, Mul(contact.normal, Dot(after, contact.normal)));
                const float fTangentSpeed = Length(tangent);
                if (fTangentSpeed < CONTACT_EPSILON) continue;
                tangent = Mul(tangent, 1.0f / fTangentSpeed);
                const float fFriction = 0.5f * ((pA ? pA->m_fFriction : 0.6f) + (pB ? pB->m_fFriction : 0.6f));
                const float fTangentImpulse = std::min(fTangentSpeed / (fInvA + fInvB), std::max(fFriction, 0.0f) * fImpulse);
                if (pA && fInvA > 0) pA->m_velocity = Add(pA->m_velocity, Mul(tangent, fTangentImpulse * fInvA));
                if (pB && fInvB > 0) pB->m_velocity = Sub(pB->m_velocity, Mul(tangent, fTangentImpulse * fInvB));
            }
        }
    }

    // 5. 動いた物体を親からの相対位置に戻して書き込む（子のコライダーは親子関係で付いてくる）
    for (int nEntity = 0; nEntity < nEntityCount; ++nEntity) {
        if (!vMoved[static_cast<size_t>(nEntity)]) continue;
        DrawItem world = vBase[static_cast<size_t>(nEntity)];
        world.position = vPosition[static_cast<size_t>(nEntity)];
        scene.vEntities[static_cast<size_t>(nEntity)].draw.position = LocalDrawFromWorld(scene, nEntity, world).position;
    }

    // 6. 各組の両方の物体へ Enter / Stay / Exit を通知する（前のステップの組と比べる）
    auto dispatch = [&](const std::vector<PairEvent>& vEvents, std::vector<std::uint64_t>& vPrevious, bool bTrigger) {
        std::vector<std::uint64_t> vCurrent;
        for (const auto& event : vEvents) vCurrent.push_back(event.uKey);
        std::sort(vCurrent.begin(), vCurrent.end());
        for (const auto& event : vEvents) {
            const bool bEnter = !std::binary_search(vPrevious.begin(), vPrevious.end(), event.uKey);
            for (const int nSelf : {event.nA, event.nB}) {
                const int nOther = nSelf == event.nA ? event.nB : event.nA;
                if (nOther < 0 || static_cast<size_t>(nOther) >= scene.vEntities.size()) continue;
                Collision collision;
                collision.nOtherIndex = nOther;
                collision.normal = nSelf == event.nA ? Mul(event.contact.normal, -1.0f) : event.contact.normal;
                collision.point = event.contact.point;
                collision.fRelativeSpeed = event.fSpeed;
                ForEachComponent(scene, nSelf, [&](Component& component, Entity& self) {
                    if (static_cast<size_t>(nOther) >= scene.vEntities.size()) return;
                    auto& other = scene.vEntities[static_cast<size_t>(nOther)];
                    collision.pOther = &other;
                    if (bTrigger) bEnter ? component.OnTriggerEnter(self, other) : component.OnTriggerStay(self, other);
                    else bEnter ? component.OnCollisionEnter(self, collision) : component.OnCollisionStay(self, collision);
                });
            }
        }
        for (const auto uKey : vPrevious) {
            if (std::binary_search(vCurrent.begin(), vCurrent.end(), uKey)) continue;
            const int nA = static_cast<int>(uKey >> 32), nB = static_cast<int>(uKey & 0xFFFFFFFFu);
            for (const int nSelf : {nA, nB}) {
                const int nOther = nSelf == nA ? nB : nA;
                ForEachComponent(scene, nSelf, [&](Component& component, Entity& self) {
                    if (static_cast<size_t>(nOther) >= scene.vEntities.size()) return;
                    auto& other = scene.vEntities[static_cast<size_t>(nOther)];
                    Collision collision;
                    collision.pOther = &other;
                    collision.nOtherIndex = nOther;
                    if (bTrigger) component.OnTriggerExit(self, other);
                    else component.OnCollisionExit(self, collision);
                });
            }
        }
        vPrevious = std::move(vCurrent);
    };
    dispatch(vContactEvents, scene.physics.vContacts, false);
    dispatch(vTriggerEvents, scene.physics.vTriggers, true);
}

} // namespace dxs
