//==================================================================================
//
// モデルの変換のテスト [ModelConvertTests.cpp]
//
// ・X ファイルの Material の読み取りと .dxmat の作成（上書きしないこと）
// ・FBX → X の変換で、エンジンが描く形が変わらないこと
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Engine.h"
#include "ModelConvert.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>

namespace {

namespace fs = std::filesystem;

// テスト用の X ファイル（テンプレートの宣言、マテリアル 2 つ、テクスチャ付き）
constexpr const char* TEST_X = R"(xof 0303txt 0032
template Material {
 <3D82AB4D-62DA-11cf-AB39-0020AF71E433>
 ColorRGBA faceColor;
 FLOAT power;
 ColorRGB specularColor;
 ColorRGB emissiveColor;
 [...]
}

Material Red {
 1.000000;0.000000;0.000000;0.500000;;
 10.000000;
 0.100000;0.200000;0.300000;;
 0.000000;0.000000;0.000000;;
 TextureFilename {
  "Textures\\red.png";
 }
}

Mesh Box {
 3;
 0.0;0.0;0.0;,
 1.0;0.0;0.0;,
 0.0;1.0;0.0;;
 1;
 3;0,1,2;;
 MeshMaterialList {
  2;
  1;
  0;
  { Red }
  Material Blue {
   0.000000;0.000000;1.000000;1.000000;;
   1.000000;
   0.000000;0.000000;0.000000;;
   0.000000;0.000000;0.000000;;
  }
 }
}
)";

//==================================================================================
// --- X ファイルの Material の読み取りとマテリアルの作成のテスト ---
//==================================================================================
void TestMaterialsFromX(const fs::path& root) {
    const fs::path xFile = root / L"Assets" / L"Models" / L"Box.x";
    fs::create_directories(xFile.parent_path());
    {
        std::ofstream out(xFile, std::ios::binary);
        out << TEST_X;
    }

    // 読み取り: テンプレートの宣言と "MeshMaterialList" は Material として数えない
    std::vector<dxs::XMaterialInfo> vMaterials;
    std::string error;
    assert(dxs::ReadXMaterials(xFile, vMaterials, error));
    assert(vMaterials.size() == 2);
    assert(vMaterials[0].sName == "Red");
    assert(std::abs(vMaterials[0].diffuse.x - 1.0f) < 1e-6f && std::abs(vMaterials[0].diffuse.w - 0.5f) < 1e-6f);
    assert(std::abs(vMaterials[0].fPower - 10.0f) < 1e-6f);
    assert(std::abs(vMaterials[0].specular.z - 0.3f) < 1e-6f);
    assert(vMaterials[0].sTexture == "Textures/red.png");
    assert(vMaterials[1].sName == "Blue" && vMaterials[1].sTexture.empty());
    assert(std::abs(vMaterials[1].diffuse.z - 1.0f) < 1e-6f);

    // 作成: マテリアルごとに 1 ファイル、プロジェクトからの相対パスで返る
    std::vector<std::string> vCreated;
    assert(dxs::CreateMaterialsFromX(root, xFile, vCreated, error));
    assert(vCreated.size() == 2);
    assert(vCreated[0] == "Assets/Models/Box_Red.dxmat");
    assert(vCreated[1] == "Assets/Models/Box_Blue.dxmat");

    dxs::Material material;
    assert(dxs::LoadMaterial(root / fs::u8path(vCreated[0]), material, error));
    assert(material.sName == "Red");
    assert(std::abs(material.baseColor.x - 1.0f) < 1e-6f && std::abs(material.baseColor.w - 0.5f) < 1e-6f);
    assert(material.sTextureAsset == "Assets/Models/Textures/red.png");
    assert(material.sShaderAsset == "Default.hlsl");

    // もう一度作っても既存のファイルは上書きせず、_1 を付ける
    std::vector<std::string> vSecond;
    assert(dxs::CreateMaterialsFromX(root, xFile, vSecond, error));
    assert(vSecond.size() == 2 && vSecond[0] == "Assets/Models/Box_Red_1.dxmat");

    // マテリアルを使う物体は、どの API でも基本色が掛かる（描画情報の段階で反映される）
    dxs::Scene scene;
    scene.projectRoot = root;
    dxs::Entity entity;
    entity.sName = "Box";
    entity.renderer = dxs::Entity::RendererKind::Mesh;
    entity.sModelAsset = "Assets/Models/Box.x";
    entity.sMaterialAsset = vCreated[1];
    scene.vEntities.push_back(entity);
    const auto draws = dxs::ResolveWorldDraws(scene);
    assert(draws.size() == 1 && draws[0].pVertices && draws[0].pVertices->size() == 3);
    assert(std::abs(draws[0].color.x) < 1e-6f && std::abs(draws[0].color.z - 1.0f) < 1e-6f);

    // Material の無い X（組み込みの基本図形と同じ形）からは、白のマテリアルが 1 つできる
    const fs::path plain = root / L"Assets" / L"Models" / L"Plain.x";
    {
        std::ofstream out(plain, std::ios::binary);
        out << "xof 0303txt 0032\nMesh Tri {\n 3;\n 0;0;0;,\n 1;0;0;,\n 0;1;0;;\n 1;\n 3;0,1,2;;\n}\n";
    }
    assert(dxs::CreateMaterialsFromX(root, plain, vCreated, error));
    assert(vCreated.size() == 1 && vCreated[0] == "Assets/Models/Plain.dxmat");

    // バイナリ形式は失敗する（理由がエラーに入る）
    const fs::path binary = root / L"Assets" / L"Models" / L"Binary.x";
    {
        std::ofstream out(binary, std::ios::binary);
        out << "xof 0303bin 0032" << std::string(32, '\0');
    }
    assert(!dxs::ReadXMaterials(binary, vMaterials, error));
    assert(!error.empty());
}

//==================================================================================
// --- FBX → X の変換のテスト ---
//==================================================================================
void TestFbxToX(const fs::path& root) {
    const fs::path fbx = fs::absolute(L"tests/assets/blender_272_cube_7400_binary.fbx");
    const fs::path xFile = root / L"Converted.x";

    std::string error;
    assert(dxs::ConvertFbxToX(fbx, xFile, error));
    assert(fs::exists(xFile));

    // 書き出した X は、マテリアルを読み取れる（FBX のマテリアル、無ければ白が 1 つ）
    std::vector<dxs::XMaterialInfo> vMaterials;
    assert(dxs::ReadXMaterials(xFile, vMaterials, error));
    assert(!vMaterials.empty());

    // エンジンが描く形は、変換前の FBX と同じ（同じ三角形が同じ順で並ぶ）
    const auto fromFbx = dxs::MeshRenderer(fbx).BuildDrawItem(dxs::Entity{});
    const auto fromX = dxs::MeshRenderer(xFile).BuildDrawItem(dxs::Entity{});
    assert(fromFbx.pVertices && fromX.pVertices);
    assert(fromFbx.pVertices->size() == fromX.pVertices->size());
    for (std::size_t nIndex = 0; nIndex < fromX.pVertices->size(); ++nIndex) {
        const auto& a = (*fromFbx.pVertices)[nIndex];
        const auto& b = (*fromX.pVertices)[nIndex];
        assert(std::abs(a.x - b.x) < 1e-4f && std::abs(a.y - b.y) < 1e-4f && std::abs(a.z - b.z) < 1e-4f);
    }

    // 変換した X からもマテリアルを作れる（FBX → X → .dxmat の流れ）
    std::vector<std::string> vCreated;
    assert(dxs::CreateMaterialsFromX(root, xFile, vCreated, error));
    assert(!vCreated.empty());

    // 存在しない FBX は失敗する
    assert(!dxs::ConvertFbxToX(root / L"missing.fbx", root / L"missing.x", error));
    assert(!error.empty());
}

} // namespace

//==================================================================================
// --- メイン関数 ---
//==================================================================================
int main() {
    const fs::path root = fs::temp_directory_path() / L"DxStudioModelConvertTests";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);

    TestMaterialsFromX(root);
    TestFbxToX(root);

    fs::remove_all(root, ec);
    std::cout << "ModelConvertTests passed\n";
    return 0;
}
