//==================================================================================
//
// 組み込みの基本図形のソースファイル [Primitives.cpp]
//
// 図形を三角形リストとして組み立て、DirectX のテキスト形式 .x で書き出す。
// 面の向き（表裏）は MeshBuilder がそろえるので、陰影の計算が正しく働く。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Primitives.h"

#include <cmath>
#include <cstdio>
#include <fstream>

namespace dxs {
namespace {
using DirectX::XMFLOAT3;

//**********************************************************************************
// *** 定数宣言 ***
//**********************************************************************************
constexpr float PI = 3.14159265358979f; // 円周率
constexpr int SEGMENTS = 32;            // Y 軸まわりの分割数
constexpr int RINGS = 16;               // 球の緯度方向の分割数

// --- ベクトルの引き算 / 外積 / 内積 ---
XMFLOAT3 Sub(const XMFLOAT3& a, const XMFLOAT3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

XMFLOAT3 Cross(const XMFLOAT3& a, const XMFLOAT3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float Dot(const XMFLOAT3& a, const XMFLOAT3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

//**********************************************************************************
// *** 三角形の組み立てクラス ***
//**********************************************************************************
// 面の向きをそろえて三角形を集める。各面は、法線が center から外側を向くように
// （凸な立体）、または facing の方向を向くように（平面）頂点の順番を入れ替える。
class MeshBuilder {
public:
    explicit MeshBuilder(XMFLOAT3 center = {0, 0, 0}) : m_center(center) {}

    // 平面の図形用: 面が向く方向を指定する
    void SetFacing(XMFLOAT3 facing) {
        m_facing = facing;
        m_bUseFacing = true;
    }

    // --- 三角形の追加処理（向きが逆なら b と c を入れ替える） ---
    void Triangle(XMFLOAT3 a, XMFLOAT3 b, XMFLOAT3 c) {
        const XMFLOAT3 normal = Cross(Sub(b, a), Sub(c, a));
        const XMFLOAT3 centroid{(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3};
        const float fSide = m_bUseFacing ? Dot(normal, m_facing) : Dot(normal, Sub(centroid, m_center));
        m_vVertices.push_back(a);
        if (fSide < 0) {
            m_vVertices.push_back(c);
            m_vVertices.push_back(b);
        } else {
            m_vVertices.push_back(b);
            m_vVertices.push_back(c);
        }
    }

    // --- 四角形の追加処理（三角形 2 つに分ける） ---
    void Quad(XMFLOAT3 a, XMFLOAT3 b, XMFLOAT3 c, XMFLOAT3 d) {
        Triangle(a, b, c);
        Triangle(a, c, d);
    }

    // 集めた頂点を取り出す
    std::vector<XMFLOAT3> Take() { return std::move(m_vVertices); }

private:
    std::vector<XMFLOAT3> m_vVertices; // 三角形リスト
    XMFLOAT3 m_center;                 // 立体の中心（外向き判定用）
    XMFLOAT3 m_facing{0, 0, -1};       // 平面の向き
    bool m_bUseFacing = false;         // 平面の向きで判定するか
};

// --- 半径 fRadius・高さ fY の円周上の nIndex / nCount 番目の点 ---
XMFLOAT3 Ring(float fRadius, float fY, int nIndex, int nCount) {
    const float fAngle = 2.0f * PI * static_cast<float>(nIndex) / static_cast<float>(nCount);
    return {fRadius * std::cos(fAngle), fY, fRadius * std::sin(fAngle)};
}

// --- 2 つの円の間の側面（円柱・円錐・カプセルの胴体）の作成処理 ---
void Wall(MeshBuilder& builder, float fRadius0, float fY0, float fRadius1, float fY1) {
    for (int nIndex = 0; nIndex < SEGMENTS; ++nIndex) {
        const XMFLOAT3 a = Ring(fRadius0, fY0, nIndex, SEGMENTS), b = Ring(fRadius0, fY0, nIndex + 1, SEGMENTS);
        const XMFLOAT3 c = Ring(fRadius1, fY1, nIndex + 1, SEGMENTS), d = Ring(fRadius1, fY1, nIndex, SEGMENTS);
        if (fRadius1 <= 0.0f) {
            builder.Triangle(a, b, d); // 上の半径が 0 なら頂点（円錐の先）
        } else {
            builder.Quad(a, b, c, d);
        }
    }
}

// --- 円形のふたの作成処理 ---
void Cap(MeshBuilder& builder, float fRadius, float fY) {
    for (int nIndex = 0; nIndex < SEGMENTS; ++nIndex) {
        builder.Triangle({0, fY, 0}, Ring(fRadius, fY, nIndex, SEGMENTS), Ring(fRadius, fY, nIndex + 1, SEGMENTS));
    }
}

// --- 球の帯の作成処理 ---
// 中心 (0, fCenterY, 0)・半径 fRadius の球の、緯度 fFrom から fTo まで
// （ラジアン、-PI/2 = 下の極）を nBands 本の帯で作る。
void SphereBands(MeshBuilder& builder, float fRadius, float fCenterY, float fFrom, float fTo, int nBands) {
    for (int nBand = 0; nBand < nBands; ++nBand) {
        const float fLat0 = fFrom + (fTo - fFrom) * nBand / nBands, fLat1 = fFrom + (fTo - fFrom) * (nBand + 1) / nBands;
        const float fR0 = fRadius * std::cos(fLat0), fR1 = fRadius * std::cos(fLat1);
        const float fY0 = fCenterY + fRadius * std::sin(fLat0), fY1 = fCenterY + fRadius * std::sin(fLat1);
        for (int nIndex = 0; nIndex < SEGMENTS; ++nIndex) {
            const XMFLOAT3 a = Ring(fR0, fY0, nIndex, SEGMENTS), b = Ring(fR0, fY0, nIndex + 1, SEGMENTS);
            const XMFLOAT3 c = Ring(fR1, fY1, nIndex + 1, SEGMENTS), d = Ring(fR1, fY1, nIndex, SEGMENTS);
            if (fR0 < 1e-5f) {
                builder.Triangle(a, c, d); // 下の極（四角形がつぶれて三角形になる）
            } else if (fR1 < 1e-5f) {
                builder.Triangle(a, b, d); // 上の極
            } else {
                builder.Quad(a, b, c, d);
            }
        }
    }
}
} // namespace

//==================================================================================
// --- 基本図形の作成処理 ---
//==================================================================================
std::vector<XMFLOAT3> BuildPrimitive(PrimitiveShape shape) {
    MeshBuilder builder;
    switch (shape) {
    case PrimitiveShape::CUBE: { // 立方体: 6 面
        const float h = 0.5f;
        builder.Quad({-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h});
        builder.Quad({-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h});
        builder.Quad({-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h});
        builder.Quad({h, -h, -h}, {h, -h, h}, {h, h, h}, {h, h, -h});
        builder.Quad({-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h});
        builder.Quad({-h, h, -h}, {h, h, -h}, {h, h, h}, {-h, h, h});
        break;
    }
    case PrimitiveShape::SPHERE: SphereBands(builder, 0.5f, 0.0f, -PI / 2, PI / 2, RINGS); break; // 球
    case PrimitiveShape::CAPSULE: {
        // カプセル: Unity と同じ比率（高さ 2 × 直径 1）を単位ボックスに縮める
        const float fRadius = 0.25f, fHalfBody = 0.25f;
        SphereBands(builder, fRadius, -fHalfBody, -PI / 2, 0.0f, RINGS / 2); // 下の半球
        Wall(builder, fRadius, -fHalfBody, fRadius, fHalfBody);              // 胴体
        SphereBands(builder, fRadius, fHalfBody, 0.0f, PI / 2, RINGS / 2);   // 上の半球
        break;
    }
    case PrimitiveShape::CYLINDER: // 円柱: 側面 + 上下のふた
        Wall(builder, 0.5f, -0.5f, 0.5f, 0.5f);
        Cap(builder, 0.5f, -0.5f);
        Cap(builder, 0.5f, 0.5f);
        break;
    case PrimitiveShape::CONE: // 円錐: 側面 + 底のふた
        Wall(builder, 0.5f, -0.5f, 0.0f, 0.5f);
        Cap(builder, 0.5f, -0.5f);
        break;
    case PrimitiveShape::PYRAMID: { // 四角錐: 側面 4 枚 + 底面
        const XMFLOAT3 apex{0, 0.5f, 0};
        const XMFLOAT3 base[4] = {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}};
        for (int nIndex = 0; nIndex < 4; ++nIndex) {
            builder.Triangle(base[nIndex], base[(nIndex + 1) % 4], apex);
        }

        builder.Quad(base[0], base[1], base[2], base[3]);
        break;
    }
    case PrimitiveShape::PLANE: // 平面: XZ 平面上、上（+Y）向き
        builder.SetFacing({0, 1, 0});
        builder.Quad({-0.5f, 0, -0.5f}, {0.5f, 0, -0.5f}, {0.5f, 0, 0.5f}, {-0.5f, 0, 0.5f});
        break;
    case PrimitiveShape::SQUARE: // 正方形（2D）: XY 平面上、手前（-Z）向き
        builder.SetFacing({0, 0, -1});
        builder.Quad({-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.5f, 0.5f, 0}, {-0.5f, 0.5f, 0});
        break;
    case PrimitiveShape::CIRCLE: // 円（2D）: 中心から扇形を並べる
        builder.SetFacing({0, 0, -1});
        for (int nIndex = 0; nIndex < SEGMENTS * 2; ++nIndex) {
            const float fA = 2.0f * PI * nIndex / (SEGMENTS * 2), fB = 2.0f * PI * (nIndex + 1) / (SEGMENTS * 2);
            builder.Triangle({0, 0, 0}, {0.5f * std::cos(fA), 0.5f * std::sin(fA), 0},
                             {0.5f * std::cos(fB), 0.5f * std::sin(fB), 0});
        }

        break;
    case PrimitiveShape::TRIANGLE: // 三角形（2D）
        builder.SetFacing({0, 0, -1});
        builder.Triangle({-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.0f, 0.5f, 0});
        break;
    }

    return builder.Take();
}

//==================================================================================
// --- アセットのパスの取得処理 ---
//==================================================================================
std::string PrimitiveAssetPath(PrimitiveShape shape) {
    for (const auto& info : PRIMITIVES) {
        if (info.shape == shape) return std::string(PRIMITIVE_FOLDER) + "/" + info.pName + ".x";
    }

    return {};
}

//==================================================================================
// --- .x ファイルの書き出し処理 ---
//==================================================================================
bool WritePrimitiveFile(const std::filesystem::path& file, PrimitiveShape shape, std::string& error) {
    const auto vVertices = BuildPrimitive(shape);
    std::string sName = "Primitive";
    for (const auto& info : PRIMITIVES) {
        if (info.shape == shape) {
            sName = info.pName;
        }
    }

    // 途中で失敗しても元のファイルを壊さないよう、一時ファイルに書いてから置き換える
    auto temp = file;
    temp += L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "Cannot write primitive mesh: " + file.string();
            return false;
        }

        // ヘッダー → Mesh { 頂点数; 頂点…; 面数; 面… }
        out << "xof 0303txt 0032\n// DxStudio built-in primitive (generated). Unit box, triangle list.\n";
        out << "Mesh " << sName << " {\n " << vVertices.size() << ";\n";
        char aLine[96];
        for (size_t nIndex = 0; nIndex < vVertices.size(); ++nIndex) {
            const auto& v = vVertices[nIndex];

            // 最後の頂点だけ ";;"、それ以外は ";," で区切る（.x の書式）
            std::snprintf(aLine, sizeof(aLine), " %.6f;%.6f;%.6f;%s\n", v.x, v.y, v.z,
                          nIndex + 1 == vVertices.size() ? ";" : ",");
            out << aLine;
        }

        const size_t nFaces = vVertices.size() / 3;
        out << " " << nFaces << ";\n";
        for (size_t nFace = 0; nFace < nFaces; ++nFace) {
            out << " 3;" << nFace * 3 << ',' << nFace * 3 + 1 << ',' << nFace * 3 + 2 << (nFace + 1 == nFaces ? ";;\n" : ";,\n");
        }

        out << "}\n";
        if (!out) {
            error = "Failed to write primitive mesh: " + file.string();
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(temp, file, ec);
    if (ec) {
        error = "Cannot finalize primitive mesh: " + ec.message();
        std::filesystem::remove(temp, ec);
        return false;
    }

    return true;
}

//==================================================================================
// --- 足りない基本図形ファイルの作成処理 ---
//==================================================================================
bool EnsurePrimitiveAssets(const std::filesystem::path& projectRoot, std::string& error) {
    error.clear();
    std::error_code ec;
    const auto folder = projectRoot / std::filesystem::path(PRIMITIVE_FOLDER);
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        error = "Cannot create " + folder.string() + ": " + ec.message();
        return false;
    }

    for (const auto& info : PRIMITIVES) {
        const auto file = folder / (std::string(info.pName) + ".x");
        if (std::filesystem::exists(file, ec)) continue; // 既にある物は残す
        if (!WritePrimitiveFile(file, info.shape, error)) return false;
    }

    return true;
}

} // namespace dxs
