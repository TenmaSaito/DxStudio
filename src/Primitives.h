#pragma once

// Built-in primitive meshes (Unity: GameObject > 3D Object / 2D Object).
//
// Every primitive fits the unit box [-0.5, 0.5] and is written as a DirectX
// text .x file under <project>/Assets/Primitives/, so it is an ordinary mesh
// asset: MeshRenderer draws it, the Mesh field can swap it for any X / FBX
// file, and exported games ship it with the other project assets.

#include <DirectXMath.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace dxs {

enum class PrimitiveShape : unsigned char {
    CUBE, SPHERE, CAPSULE, CYLINDER, CONE, PYRAMID, PLANE, // 3D
    SQUARE, CIRCLE, TRIANGLE,                             // 2D (XY plane, facing -Z)
};

struct PrimitiveInfo {
    PrimitiveShape shape;
    const char* pName;   // file stem and menu label, e.g. "Sphere"
    bool bFlat;          // 2D shape
};

inline constexpr std::array<PrimitiveInfo, 10> PRIMITIVES = {{
    {PrimitiveShape::CUBE, "Cube", false},       {PrimitiveShape::SPHERE, "Sphere", false},
    {PrimitiveShape::CAPSULE, "Capsule", false}, {PrimitiveShape::CYLINDER, "Cylinder", false},
    {PrimitiveShape::CONE, "Cone", false},       {PrimitiveShape::PYRAMID, "Pyramid", false},
    {PrimitiveShape::PLANE, "Plane", false},     {PrimitiveShape::SQUARE, "Square", true},
    {PrimitiveShape::CIRCLE, "Circle", true},    {PrimitiveShape::TRIANGLE, "Triangle", true},
}};

// Project-relative folder of the built-in primitive files.
inline constexpr const char* PRIMITIVE_FOLDER = "Assets/Primitives";

// Triangle list (three vertices per triangle) inside the unit box.
std::vector<DirectX::XMFLOAT3> BuildPrimitive(PrimitiveShape shape);
// "Assets/Primitives/Sphere.x"
std::string PrimitiveAssetPath(PrimitiveShape shape);
// Writes <file> in DirectX .x text format.
bool WritePrimitiveFile(const std::filesystem::path& file, PrimitiveShape shape, std::string& error);
// Creates any missing primitive file in <projectRoot>/Assets/Primitives.
// Existing files are left untouched, so users may edit them.
bool EnsurePrimitiveAssets(const std::filesystem::path& projectRoot, std::string& error);

} // namespace dxs
