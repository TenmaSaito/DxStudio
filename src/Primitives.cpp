#include "Primitives.h"

#include <cmath>
#include <cstdio>
#include <fstream>

namespace dxs {
namespace {
using DirectX::XMFLOAT3;
constexpr float PI = 3.14159265358979f;
constexpr int SEGMENTS = 32;   // around the Y axis
constexpr int RINGS = 16;      // sphere latitude bands

XMFLOAT3 Sub(const XMFLOAT3& a, const XMFLOAT3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
XMFLOAT3 Cross(const XMFLOAT3& a, const XMFLOAT3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Dot(const XMFLOAT3& a, const XMFLOAT3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Collects triangles with a consistent winding: each face is flipped so its
// normal points away from `center` (convex shapes) or along `facing` (flat).
class MeshBuilder {
public:
    explicit MeshBuilder(XMFLOAT3 center = {0, 0, 0}) : m_center(center) {}
    void SetFacing(XMFLOAT3 facing) { m_facing = facing; m_bUseFacing = true; }
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
    void Quad(XMFLOAT3 a, XMFLOAT3 b, XMFLOAT3 c, XMFLOAT3 d) {
        Triangle(a, b, c);
        Triangle(a, c, d);
    }
    std::vector<XMFLOAT3> Take() { return std::move(m_vVertices); }

private:
    std::vector<XMFLOAT3> m_vVertices;
    XMFLOAT3 m_center;
    XMFLOAT3 m_facing{0, 0, -1};
    bool m_bUseFacing = false;
};

XMFLOAT3 Ring(float fRadius, float fY, int nIndex, int nCount) {
    const float fAngle = 2.0f * PI * static_cast<float>(nIndex) / static_cast<float>(nCount);
    return {fRadius * std::cos(fAngle), fY, fRadius * std::sin(fAngle)};
}

// Side wall between two rings (cylinder / cone / capsule body).
void Wall(MeshBuilder& builder, float fRadius0, float fY0, float fRadius1, float fY1) {
    for (int nIndex = 0; nIndex < SEGMENTS; ++nIndex) {
        const XMFLOAT3 a = Ring(fRadius0, fY0, nIndex, SEGMENTS), b = Ring(fRadius0, fY0, nIndex + 1, SEGMENTS);
        const XMFLOAT3 c = Ring(fRadius1, fY1, nIndex + 1, SEGMENTS), d = Ring(fRadius1, fY1, nIndex, SEGMENTS);
        if (fRadius1 <= 0.0f) builder.Triangle(a, b, d); // apex
        else builder.Quad(a, b, c, d);
    }
}

void Cap(MeshBuilder& builder, float fRadius, float fY) {
    for (int nIndex = 0; nIndex < SEGMENTS; ++nIndex)
        builder.Triangle({0, fY, 0}, Ring(fRadius, fY, nIndex, SEGMENTS), Ring(fRadius, fY, nIndex + 1, SEGMENTS));
}

// Latitude bands of a sphere of radius r centred at (0, fCenterY, 0), from
// latitude fFrom to fTo (radians, -PI/2 = bottom pole).
void SphereBands(MeshBuilder& builder, float fRadius, float fCenterY, float fFrom, float fTo, int nBands) {
    for (int nBand = 0; nBand < nBands; ++nBand) {
        const float fLat0 = fFrom + (fTo - fFrom) * nBand / nBands, fLat1 = fFrom + (fTo - fFrom) * (nBand + 1) / nBands;
        const float fR0 = fRadius * std::cos(fLat0), fR1 = fRadius * std::cos(fLat1);
        const float fY0 = fCenterY + fRadius * std::sin(fLat0), fY1 = fCenterY + fRadius * std::sin(fLat1);
        for (int nIndex = 0; nIndex < SEGMENTS; ++nIndex) {
            const XMFLOAT3 a = Ring(fR0, fY0, nIndex, SEGMENTS), b = Ring(fR0, fY0, nIndex + 1, SEGMENTS);
            const XMFLOAT3 c = Ring(fR1, fY1, nIndex + 1, SEGMENTS), d = Ring(fR1, fY1, nIndex, SEGMENTS);
            if (fR0 < 1e-5f) builder.Triangle(a, c, d);       // bottom pole
            else if (fR1 < 1e-5f) builder.Triangle(a, b, d);  // top pole
            else builder.Quad(a, b, c, d);
        }
    }
}
} // namespace

std::vector<XMFLOAT3> BuildPrimitive(PrimitiveShape shape) {
    MeshBuilder builder;
    switch (shape) {
    case PrimitiveShape::CUBE: {
        const float h = 0.5f;
        builder.Quad({-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h});
        builder.Quad({-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h});
        builder.Quad({-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h});
        builder.Quad({h, -h, -h}, {h, -h, h}, {h, h, h}, {h, h, -h});
        builder.Quad({-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h});
        builder.Quad({-h, h, -h}, {h, h, -h}, {h, h, h}, {-h, h, h});
        break;
    }
    case PrimitiveShape::SPHERE: SphereBands(builder, 0.5f, 0.0f, -PI / 2, PI / 2, RINGS); break;
    case PrimitiveShape::CAPSULE: {
        // Unity proportions: height 2 x diameter 1, scaled into the unit box.
        const float fRadius = 0.25f, fHalfBody = 0.25f;
        SphereBands(builder, fRadius, -fHalfBody, -PI / 2, 0.0f, RINGS / 2);
        Wall(builder, fRadius, -fHalfBody, fRadius, fHalfBody);
        SphereBands(builder, fRadius, fHalfBody, 0.0f, PI / 2, RINGS / 2);
        break;
    }
    case PrimitiveShape::CYLINDER:
        Wall(builder, 0.5f, -0.5f, 0.5f, 0.5f);
        Cap(builder, 0.5f, -0.5f);
        Cap(builder, 0.5f, 0.5f);
        break;
    case PrimitiveShape::CONE:
        Wall(builder, 0.5f, -0.5f, 0.0f, 0.5f);
        Cap(builder, 0.5f, -0.5f);
        break;
    case PrimitiveShape::PYRAMID: {
        const XMFLOAT3 apex{0, 0.5f, 0};
        const XMFLOAT3 base[4] = {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}};
        for (int nIndex = 0; nIndex < 4; ++nIndex) builder.Triangle(base[nIndex], base[(nIndex + 1) % 4], apex);
        builder.Quad(base[0], base[1], base[2], base[3]);
        break;
    }
    case PrimitiveShape::PLANE:
        builder.SetFacing({0, 1, 0});
        builder.Quad({-0.5f, 0, -0.5f}, {0.5f, 0, -0.5f}, {0.5f, 0, 0.5f}, {-0.5f, 0, 0.5f});
        break;
    case PrimitiveShape::SQUARE:
        builder.SetFacing({0, 0, -1});
        builder.Quad({-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.5f, 0.5f, 0}, {-0.5f, 0.5f, 0});
        break;
    case PrimitiveShape::CIRCLE:
        builder.SetFacing({0, 0, -1});
        for (int nIndex = 0; nIndex < SEGMENTS * 2; ++nIndex) {
            const float fA = 2.0f * PI * nIndex / (SEGMENTS * 2), fB = 2.0f * PI * (nIndex + 1) / (SEGMENTS * 2);
            builder.Triangle({0, 0, 0}, {0.5f * std::cos(fA), 0.5f * std::sin(fA), 0}, {0.5f * std::cos(fB), 0.5f * std::sin(fB), 0});
        }
        break;
    case PrimitiveShape::TRIANGLE:
        builder.SetFacing({0, 0, -1});
        builder.Triangle({-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.0f, 0.5f, 0});
        break;
    }
    return builder.Take();
}

std::string PrimitiveAssetPath(PrimitiveShape shape) {
    for (const auto& info : PRIMITIVES)
        if (info.shape == shape) return std::string(PRIMITIVE_FOLDER) + "/" + info.pName + ".x";
    return {};
}

bool WritePrimitiveFile(const std::filesystem::path& file, PrimitiveShape shape, std::string& error) {
    const auto vVertices = BuildPrimitive(shape);
    std::string sName = "Primitive";
    for (const auto& info : PRIMITIVES)
        if (info.shape == shape) sName = info.pName;
    auto temp = file;
    temp += L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "Cannot write primitive mesh: " + file.string();
            return false;
        }
        out << "xof 0303txt 0032\n// DxStudio built-in primitive (generated). Unit box, triangle list.\n";
        out << "Mesh " << sName << " {\n " << vVertices.size() << ";\n";
        char aLine[96];
        for (size_t nIndex = 0; nIndex < vVertices.size(); ++nIndex) {
            const auto& v = vVertices[nIndex];
            std::snprintf(aLine, sizeof(aLine), " %.6f;%.6f;%.6f;%s\n", v.x, v.y, v.z, nIndex + 1 == vVertices.size() ? ";" : ",");
            out << aLine;
        }
        const size_t nFaces = vVertices.size() / 3;
        out << " " << nFaces << ";\n";
        for (size_t nFace = 0; nFace < nFaces; ++nFace)
            out << " 3;" << nFace * 3 << ',' << nFace * 3 + 1 << ',' << nFace * 3 + 2 << (nFace + 1 == nFaces ? ";;\n" : ";,\n");
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
        if (std::filesystem::exists(file, ec)) continue;
        if (!WritePrimitiveFile(file, info.shape, error)) return false;
    }
    return true;
}

} // namespace dxs
