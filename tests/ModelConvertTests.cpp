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

    // モデルのマテリアル（スロット）: MeshMaterialList の順（{ Red } の参照、埋め込みの Blue）
    std::vector<dxs::MaterialSlot> vSlots;
    assert(dxs::LoadModelMaterials(xFile, vSlots));
    assert(vSlots.size() == 2 && vSlots[0].sName == "Red" && vSlots[1].sName == "Blue");
    assert(std::abs(vSlots[0].color.x - 1.0f) < 1e-6f && std::abs(vSlots[1].color.z - 1.0f) < 1e-6f);

    // 作成: すべてのマテリアルを 1 つのファイルにまとめる（プロジェクトからの相対パスで返る）
    std::vector<std::string> vCreated;
    assert(dxs::CreateMaterialsFromX(root, xFile, vCreated, error));
    assert(vCreated.size() == 1 && vCreated[0] == "Assets/Models/Box.dxmat");

    dxs::Material material;
    assert(dxs::LoadMaterial(root / fs::u8path(vCreated[0]), material, error));
    assert(material.sName == "Box" && material.sShaderAsset == "Default.hlsl");
    assert(std::abs(material.baseColor.x - 1.0f) < 1e-6f && std::abs(material.baseColor.w - 1.0f) < 1e-6f); // 全体は白
    assert(material.vSlots.size() == 2);
    assert(material.vSlots[0].sName == "Red" && std::abs(material.vSlots[0].color.w - 0.5f) < 1e-6f);
    assert(material.vSlots[0].sTexture == "Assets/Models/Textures/red.png");
    assert(material.vSlots[1].sName == "Blue" && material.vSlots[1].sTexture.empty());
    assert(material.sTextureAsset == "Assets/Models/Textures/red.png");

    // もう一度作っても既存のファイルは上書きせず、_1 を付ける
    std::vector<std::string> vSecond;
    assert(dxs::CreateMaterialsFromX(root, xFile, vSecond, error));
    assert(vSecond.size() == 1 && vSecond[0] == "Assets/Models/Box_1.dxmat");

    // マテリアル未設定の物体: モデルファイルの面ごとの色で描く（面は Red を使う）
    dxs::Scene scene;
    scene.projectRoot = root;
    dxs::Entity entity;
    entity.sName = "Box";
    entity.renderer = dxs::Entity::RendererKind::Mesh;
    entity.sModelAsset = "Assets/Models/Box.x";
    scene.vEntities.push_back(entity);
    auto draws = dxs::ResolveWorldDraws(scene);
    assert(draws.size() == 1 && draws[0].pVertices && draws[0].pVertices->size() == 3);
    assert(draws[0].pRunColors && draws[0].pRunColors->size() == 2);
    assert(draws[0].pShadeRuns && draws[0].pShadeRuns->size() == 2 * 8); // スロット × 明るさ 8 段階
    assert(std::abs((*draws[0].pRunColors)[0].x - 1.0f) < 1e-6f);

    // まとめたマテリアルの色を変えて設定すると、面ごとの色が置き換わる（どの API でも同じ）
    material.vSlots[0].color = {0.0f, 1.0f, 0.0f, 1.0f};
    material.baseColor = {0.5f, 0.5f, 0.5f, 1.0f};
    assert(dxs::SaveMaterial(root / fs::u8path(vCreated[0]), material, error));
    scene.vEntities[0].sMaterialAsset = vCreated[0];
    draws = dxs::ResolveWorldDraws(scene);
    assert(draws[0].pRunColors && draws[0].pRunColors->size() == 2);
    assert(std::abs((*draws[0].pRunColors)[0].y - 1.0f) < 1e-6f && std::abs((*draws[0].pRunColors)[0].x) < 1e-6f);
    assert(std::abs(draws[0].color.x - 0.5f) < 1e-6f); // 基本色は全体に掛かる

    // 1 色だけのマテリアル（バージョン 1）は、モデル全体をその色で描く
    dxs::Material single;
    single.baseColor = {0.0f, 0.0f, 1.0f, 1.0f};
    assert(dxs::SaveMaterial(root / L"Assets" / L"Models" / L"Single.dxmat", single, error));
    scene.vEntities[0].sMaterialAsset = "Assets/Models/Single.dxmat";
    draws = dxs::ResolveWorldDraws(scene);
    assert(!draws[0].pRunColors && std::abs(draws[0].color.z - 1.0f) < 1e-6f && std::abs(draws[0].color.x) < 1e-6f);

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

//==================================================================================
// --- 日本語のツールが書き出した形の X ファイルのテスト ---
//==================================================================================
// ・先頭に template の宣言（template MeshFace / template Mesh など）が並ぶ
// ・Frame の中に Mesh があり、FrameTransformMatrix で位置がずれている
// ・テクスチャのパスとマテリアル名が Shift-JIS（2 バイト目が \ と同じ値の「表」を含む）
void TestToolExportedX(const fs::path& root) {
    // 「素材」= 91 66 8D DE、「表」= 95 5C、「赤」= 90 D4（Shift-JIS）
    const std::string sTexture = "C:\\\\Users\\\\\x91\x66\x8D\xDE\\\\\x95\x5C.png";
    const std::string sText = std::string(R"(xof 0302txt 0064
template MeshFace {
 <3D82AB5F-62DA-11cf-AB39-0020AF71E433>
 DWORD nFaceVertexIndices;
 array DWORD faceVertexIndices[nFaceVertexIndices];
}

template Mesh {
 <3D82AB44-62DA-11cf-AB39-0020AF71E433>
 DWORD nVertices;
 array Vector vertices[nVertices];
 DWORD nFaces;
 array MeshFace faces[nFaces];
 [...]
}

template Material {
 <3D82AB4D-62DA-11cf-AB39-0020AF71E433>
 ColorRGBA faceColor;
 FLOAT power;
 ColorRGB specularColor;
 ColorRGB emissiveColor;
 [...]
}

Header {
 1;
 0;
 1;
}

Frame Left {
 FrameTransformMatrix {
  1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0;;
 }
 Mesh {
  3;
  0.0;0.0;0.0;,
  1.0;0.0;0.0;,
  0.0;1.0;0.0;;
  1;
  3;0,1,2;;
  MeshMaterialList {
   1;
   1;
   0;;
   Material )") + "\x90\xD4" + R"( {
    1.000000;0.000000;0.000000;1.000000;;
    5.000000;
    0.000000;0.000000;0.000000;;
    0.000000;0.000000;0.000000;;
    TextureFilename {
     ")" + sTexture + R"(";
    }
   }
  }
 }
}

Frame Right {
 FrameTransformMatrix {
  1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0,10.0,0.0,0.0,1.0;;
 }
 Mesh RightMesh {
  3;
  0.0;0.0;0.0;,
  1.0;0.0;0.0;,
  0.0;1.0;0.0;;
  1;
  3;0,1,2;;
 }
}
)";

    const fs::path xFile = root / L"Assets" / L"Models" / L"Tool.x";
    fs::create_directories(xFile.parent_path());
    {
        std::ofstream out(xFile, std::ios::binary);
        out << sText;
    }

    // メッシュ: template の宣言を読み飛ばし、2 つの Mesh を両方読む。
    // 右の Mesh は Frame の行列で x = +10 にずれるので、正規化後は左右の端に分かれる。
    const auto draw = dxs::MeshRenderer(xFile).BuildDrawItem(dxs::Entity{});
    assert(draw.pVertices && draw.pVertices->size() == 6);
    float fMinX = 1.0f, fMaxX = -1.0f;
    for (const auto& vertex : *draw.pVertices) {
        fMinX = std::min(fMinX, vertex.x);
        fMaxX = std::max(fMaxX, vertex.x);
    }

    assert(std::abs(fMinX + 0.5f) < 1e-4f && std::abs(fMaxX - 0.5f) < 1e-4f);

    // マテリアル: Shift-JIS の名前とテクスチャのパスを UTF-8 にして読む（落ちないこと）
    std::vector<dxs::XMaterialInfo> vMaterials;
    std::string error;
    assert(dxs::ReadXMaterials(xFile, vMaterials, error));
    assert(vMaterials.size() == 1);
    assert(vMaterials[0].sName == "\xE8\xB5\xA4");                                          // 「赤」
    assert(vMaterials[0].sTexture == "C:/Users/\xE7\xB4\xA0\xE6\x9D\x90/\xE8\xA1\xA8.png"); // C:/Users/素材/表.png

    std::vector<std::string> vCreated;
    assert(dxs::CreateMaterialsFromX(root, xFile, vCreated, error));
    assert(vCreated.size() == 1 && vCreated[0] == "Assets/Models/Tool.dxmat");
    dxs::Material material;
    assert(dxs::LoadMaterial(root / fs::u8path(vCreated[0]), material, error));
    assert(material.sTextureAsset == vMaterials[0].sTexture); // プロジェクトの外のパスは書いてあるまま

    // スロット: 左の Mesh の「赤」と、マテリアルの無い右の Mesh の白（1 つのファイルにまとまる）
    assert(material.vSlots.size() == 2);
    assert(material.vSlots[0].sName == "\xE8\xB5\xA4" && material.vSlots[0].sTexture == vMaterials[0].sTexture);
    assert(std::abs(material.vSlots[0].color.x - 1.0f) < 1e-6f && std::abs(material.vSlots[0].color.y) < 1e-6f);
    assert(std::abs(material.vSlots[1].color.y - 1.0f) < 1e-6f);

    // 描画: 左の三角形は赤、右の三角形は白（区間は スロット × 明るさ）
    const auto colored = dxs::MeshRenderer(xFile).BuildDrawItem(dxs::Entity{});
    assert(colored.pRunColors && colored.pRunColors->size() == 2 && colored.pShadeRuns->size() == 16);
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
    TestToolExportedX(root);

    fs::remove_all(root, ec);
    std::cout << "ModelConvertTests passed\n";
    return 0;
}
