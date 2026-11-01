//==================================================================================
//
// シーンとコンポーネントのテスト [SceneTests.cpp]
//
// シーンの読み込み / 保存、コンポーネントの実行、インスペクターの値、基本図形。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Engine.h"
#include <array>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>

// テスト用のコンポーネント: X 方向へ動く
class MoveTest final : public dxs::Component {
public:
    float speed = 1.0f;
    bool enabled = true;

    void Start(dxs::Entity& entity) override { entity.draw.position.z += 1.0f; }

    void Update(dxs::Entity& entity, float dt) override {
        if (enabled) {
            entity.draw.position.x += dt * speed;
        }
    }

    std::vector<dxs::EditorField> EditorFields() override {
        return {dxs::EditField("Speed", speed), dxs::EditField("Enabled", enabled)};
    }
};

std::unique_ptr<dxs::Component> CreateMoveTest() {
    return std::make_unique<MoveTest>();
}

// インスペクターのすべての種類の欄（列挙型、ベクトル、スライダー、動的 / 静的配列）
enum class Mode : unsigned char { Idle, Walk, Run };

class FieldKinds final : public dxs::Component {
public:
    Mode mode = Mode::Walk;
    DirectX::XMFLOAT2 size2{1, 2};
    DirectX::XMFLOAT3 offset{1, 2, 3};
    float fVolume = 0.5f;
    int nLevel = 3;
    std::vector<float> vWeights{0.25f};
    std::vector<bool> vFlags;
    std::array<DirectX::XMFLOAT3, 2> aPoints{};
    std::vector<Mode> vModes;
    float fStartedVolume = -1.0f;
    size_t nStartedWeights = 0;

    void Start(dxs::Entity&) override {
        fStartedVolume = fVolume;
        nStartedWeights = vWeights.size();
    }

    void Update(dxs::Entity&, float) override {}

    std::vector<dxs::EditorField> EditorFields() override {
        return {dxs::EditEnum("Mode", mode, {"Idle", "Walk", "Run"}),
                dxs::EditField("Size", size2),
                dxs::EditField("Offset", offset),
                dxs::EditRange("Volume", fVolume, 0.0f, 1.0f),
                dxs::EditRange("Level", nLevel, 1, 5),
                dxs::EditField("Weights", vWeights),
                dxs::EditField("Flags", vFlags),
                dxs::EditField("Points", aPoints),
                dxs::EditEnum("Modes", vModes, {"Idle", "Walk", "Run"})};
    }
};

//==================================================================================
// --- メイン関数 ---
//==================================================================================
int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::current_path() / (L"dxstudio-scene-test-" + std::to_wstring(GetCurrentProcessId()));
    if (!fs::create_directory(dir)) return 1;
    std::string error;
    dxs::Scene original;
    dxs::Entity entity;
    entity.sName = "A name with spaces";
    entity.vComponents = {"Spin", "MoveTest", "Unregistered"};
    entity.draw.position = {1, 2, 3};
    entity.sMaterialAsset = "Assets/Materials/Test.dxmat";
    entity.sTextureAsset = "Assets/Textures/Test.png";
    entity.nAnimationIndex = 2;
    entity.componentProperties["MoveTest"]["Speed"] = "2";
    original.vEntities.push_back(entity);
    original.camera.target = {1.25f, -2.5f, 7.125f};
    original.camera.fYaw = 0.7f;
    original.camera.fPitch = -0.4f;
    original.camera.fDistance = 12.5f;
    original.camera.bOrthographic = true;
    if (!dxs::SaveScene(dir / "scene.dxs", original, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    dxs::Scene loaded;
    assert(dxs::LoadScene(dir / "scene.dxs", loaded, error));
    assert(loaded.vEntities.size() == 1 && loaded.vEntities[0].sName == entity.sName);
    assert(loaded.vEntities[0].vComponents == entity.vComponents && loaded.vEntities[0].draw.position.y == 2);
    assert(loaded.vEntities[0].sMaterialAsset == entity.sMaterialAsset &&
           loaded.vEntities[0].sTextureAsset == entity.sTextureAsset);
    assert(loaded.vEntities[0].nAnimationIndex == 2 && loaded.vEntities[0].componentProperties["MoveTest"]["Speed"] == "2");
    assert(loaded.camera.bOrthographic && loaded.camera.target.x == original.camera.target.x &&
           loaded.camera.target.y == original.camera.target.y && loaded.camera.target.z == original.camera.target.z &&
           loaded.camera.fYaw == original.camera.fYaw && loaded.camera.fPitch == original.camera.fPitch &&
           loaded.camera.fDistance == original.camera.fDistance);
    dxs::RegisterProjectComponents();
    assert(dxs::RegisterComponentClass("MoveTest", CreateMoveTest));
    assert(!dxs::RegisterComponentClass("MoveTest", CreateMoveTest));
    assert(dxs::IsComponentRegistered("Spin"));
    assert(!dxs::IsComponentRegistered("Missing"));
    const auto registered = dxs::RegisteredComponentNames();
    assert(std::find(registered.begin(), registered.end(), "Spin") != registered.end());
    assert(std::is_sorted(registered.begin(), registered.end()));
    dxs::UpdateUserComponents(loaded, 1.0f);
    assert(std::abs(loaded.vEntities[0].draw.rotation.y - 0.7f) < 0.0001f);
    assert(std::abs(loaded.vEntities[0].draw.position.x - 3.0f) < 0.0001f);
    assert(std::abs(loaded.vEntities[0].draw.position.z - 4.0f) < 0.0001f);
    dxs::UpdateUserComponents(loaded, 1.0f);
    assert(std::abs(loaded.vEntities[0].draw.position.x - 5.0f) < 0.0001f);
    assert(std::abs(loaded.vEntities[0].draw.position.z - 4.0f) < 0.0001f);
    dxs::Entity detachable = loaded.vEntities[0];
    assert(detachable.componentProperties.contains("MoveTest"));
    assert(dxs::DetachComponent(detachable, 1));
    assert(std::find(detachable.vComponents.begin(), detachable.vComponents.end(), "MoveTest") == detachable.vComponents.end());
    assert(!detachable.componentProperties.contains("MoveTest") && detachable.vRuntimeComponents.empty());
    assert(!dxs::DetachComponent(detachable, 99));
    {
        std::ofstream old(dir / "old.dxs");
        old << "DXSCENE 1 1\n\"Old\" 1 0 0 0 0 0 0 1 1 1 1 1 1 1\n";
    }

    assert(dxs::LoadScene(dir / "old.dxs", loaded, error));
    assert(loaded.vEntities.size() == 1 && loaded.vEntities[0].vComponents.empty() && loaded.vEntities[0].draw.bIs2D);
    assert(!loaded.camera.bOrthographic && loaded.camera.fDistance == dxs::Camera{}.fDistance);
    {
        std::ofstream old2(dir / "old2.dxs");
        old2 << "DXSCENE 2 1\n\"Legacy\" \"Unregistered\" 0 0 0 0 0 0 0 1 1 1 1 1 1 1\n";
    }

    assert(dxs::LoadScene(dir / "old2.dxs", loaded, error));
    assert(loaded.vEntities.size() == 1 && loaded.vEntities[0].vComponents == std::vector<std::string>{"Unregistered"});
    {
        std::ofstream bad(dir / "bad.dxs");
        bad << "DXSCENE 2 1\nbroken\n";
    }

    assert(!dxs::LoadScene(dir / "bad.dxs", loaded, error));
    assert(loaded.vEntities.size() == 1 && loaded.vEntities[0].sName == "Legacy");
    dxs::Scene duplicate = original;
    duplicate.vEntities[0].vComponents.push_back("Spin");
    assert(!dxs::SaveScene(dir / "duplicate.dxs", duplicate, error));
    {
        std::ofstream bad3(dir / "duplicate.dxs");
        bad3 << "DXSCENE 3 1\n\"Dup\" 2 \"Spin\" \"Spin\" 0 0 0 0 0 0 0 1 1 1 1 1 1 1\n";
    }

    assert(!dxs::LoadScene(dir / "duplicate.dxs", loaded, error));
    assert(loaded.vEntities.size() == 1 && loaded.vEntities[0].sName == "Legacy");
    {
        std::ofstream old3(dir / "old3.dxs");
        old3 << "DXSCENE 3 1\n\"Legacy3\" 1 \"Spin\" 0 0 0 0 0 0 0 1 1 1 1 1 1 1\n";
    }

    assert(dxs::LoadScene(dir / "old3.dxs", loaded, error));
    assert(loaded.vEntities[0].vComponents == std::vector<std::string>{"Spin"} && !loaded.camera.bOrthographic);
    assert(dxs::LoadScene(dir / "scene.dxs", loaded, error));
    for (const char* badCamera :
         {"CAMERA 2 0 0 0 0 0 6", "CAMERA 1 0 0 0 0 0 0", "CAMERA 1 0 0 0 0 2 6", "CAMERA 1 0 0 0 0 0 81", "CAMERA 1 0 0"}) {
        {
            std::ofstream bad4(dir / "camera.dxs");
            bad4 << "DXSCENE 4 0\n" << badCamera << '\n';
        }

        assert(!dxs::LoadScene(dir / "camera.dxs", loaded, error));
        assert(loaded.vEntities.size() == 1 && loaded.vEntities[0].sName == entity.sName && loaded.camera.bOrthographic &&
               loaded.camera.fDistance == original.camera.fDistance);
    }

    dxs::Scene badCamera = original;
    badCamera.camera.fDistance = std::numeric_limits<float>::infinity();
    assert(!dxs::SaveScene(dir / "scene.dxs", badCamera, error));
    assert(dxs::LoadScene(dir / "scene.dxs", loaded, error) && loaded.camera.fDistance == original.camera.fDistance);
    dxs::Scene hierarchy;
    dxs::Entity parent;
    parent.sName = "Parent";
    parent.draw.position = {10.0f, 0.0f, 0.0f};
    dxs::Entity child;
    child.sName = "Child";
    child.draw.position = {12.0f, 2.0f, 0.0f};
    hierarchy.vEntities.push_back(parent);
    hierarchy.vEntities.push_back(child);
    assert(dxs::SetEntityParent(hierarchy, 1, 0, error));
    assert(hierarchy.vEntities[1].nParent == 0);
    auto childWorld = dxs::WorldDraw(hierarchy, 1);
    assert(std::abs(childWorld.position.x - 12.0f) < 0.0001f);
    hierarchy.vEntities[0].draw.position.x += 3.0f;
    childWorld = dxs::WorldDraw(hierarchy, 1);
    assert(std::abs(childWorld.position.x - 15.0f) < 0.0001f);
    assert(!dxs::SetEntityParent(hierarchy, 0, 1, error));
    assert(hierarchy.vEntities[0].nParent == -1);
    assert(dxs::SaveScene(dir / "hierarchy.dxs", hierarchy, error));
    dxs::Scene hierarchyLoaded;
    assert(dxs::LoadScene(dir / "hierarchy.dxs", hierarchyLoaded, error));
    assert(hierarchyLoaded.vEntities.size() == 2 && hierarchyLoaded.vEntities[1].nParent == 0);
    assert(std::abs(dxs::WorldDraw(hierarchyLoaded, 1).position.x - 15.0f) < 0.0001f);
    {
        std::ofstream model(dir / "triangle.x");
        model << "xof 0303txt 0032\nMesh { 3; 0;0;0;, 1;0;0;, 0;1;0;; 1; 3;0,1,2;; }\n";
    }

    dxs::Scene renderScene;
    renderScene.projectRoot = dir;
    dxs::Entity empty;
    empty.sName = "Empty";
    empty.renderer = dxs::Entity::RendererKind::None;
    dxs::Entity model;
    model.sName = "Model";
    model.renderer = dxs::Entity::RendererKind::Model;
    model.sModelAsset = "triangle.x";
    fs::copy_file(fs::current_path() / "tests" / "assets" / "blender_272_cube_7400_binary.fbx", dir / "binary.fbx");
    dxs::Entity binary;
    binary.sName = "Binary FBX";
    binary.renderer = dxs::Entity::RendererKind::Model;
    binary.sModelAsset = "binary.fbx";
    renderScene.vEntities.push_back(empty);
    renderScene.vEntities.push_back(model);
    renderScene.vEntities.push_back(binary);
    const auto renderItems = dxs::ResolveWorldDraws(renderScene);
    assert(!renderItems[0].bVisible && renderItems[1].bVisible && renderItems[1].pVertices &&
           renderItems[1].pVertices->size() == 3);
    assert(renderItems[2].bVisible && renderItems[2].pVertices && renderItems[2].pVertices->size() >= 3);
    assert(dxs::SaveScene(dir / "renderers.dxs", renderScene, error));
    dxs::Scene renderLoaded;
    assert(dxs::LoadScene(dir / "renderers.dxs", renderLoaded, error));
    assert(renderLoaded.vEntities[0].renderer == dxs::Entity::RendererKind::None &&
           renderLoaded.vEntities[1].renderer == dxs::Entity::RendererKind::Model);
    dxs::Material material;
    material.sName = "Test Material";
    material.baseColor = {0.5f, 0.25f, 1.0f, 1.0f};
    material.sTextureAsset = "Assets/Textures/Test.png";
    assert(dxs::SaveMaterial(dir / "test.dxmat", material, error));
    dxs::Material loadedMaterial;
    assert(dxs::LoadMaterial(dir / "test.dxmat", loadedMaterial, error));
    assert(loadedMaterial.sName == material.sName && loadedMaterial.sTextureAsset == material.sTextureAsset &&
           loadedMaterial.baseColor.y == 0.25f);
    dxs::Entity cameraEntity;
    cameraEntity.sName = "Camera A";
    cameraEntity.renderer = dxs::Entity::RendererKind::None;
    cameraEntity.vComponents = {"Camera"};
    cameraEntity.draw.position = {3, 4, 5};
    cameraEntity.draw.rotation = {0.1f, 0.2f, 0};
    renderScene.vEntities.push_back(cameraEntity);
    const auto gameCamera = dxs::ActiveGameCamera(renderScene);
    assert(std::abs(gameCamera.fYaw - 0.2f) < 0.0001f && std::abs(gameCamera.fPitch - 0.1f) < 0.0001f);

    // 組み込みの基本図形: .x ファイルに書き出し、MeshRenderer で読み込む
    {
        assert(dxs::EnsurePrimitiveAssets(dir, error));
        dxs::Scene meshScene;
        meshScene.projectRoot = dir;
        for (const auto& info : dxs::PRIMITIVES) {
            const auto vBuilt = dxs::BuildPrimitive(info.shape);
            assert(!vBuilt.empty() && vBuilt.size() % 3 == 0);
            for (const auto& v : vBuilt) {
                assert(std::abs(v.x) <= 0.5001f && std::abs(v.y) <= 0.5001f && std::abs(v.z) <= 0.5001f);
            }

            assert(fs::is_regular_file(dir / fs::u8path(dxs::PrimitiveAssetPath(info.shape))));
            dxs::Entity mesh;
            mesh.sName = info.pName;
            mesh.renderer = dxs::Entity::RendererKind::Mesh;
            mesh.sModelAsset = dxs::PrimitiveAssetPath(info.shape);
            meshScene.vEntities.push_back(mesh);
        }

        const auto vMeshDraws = dxs::ResolveWorldDraws(meshScene);
        for (size_t n = 0; n < dxs::PRIMITIVES.size(); ++n) {
            const auto& draw = vMeshDraws[n];
            assert(draw.bVisible && draw.pVertices && draw.pShadeRuns);
            assert(draw.pVertices->size() == dxs::BuildPrimitive(dxs::PRIMITIVES[n].shape).size());
            std::uint64_t nRuns = 0;
            for (auto nRun : *draw.pShadeRuns) {
                nRuns += nRun;
            }

            assert(nRuns == draw.pVertices->size());

            // 2D の図形（だけ）が XY 平面上で平ら。Plane は XZ 平面上にある
            assert(draw.bIs2D == dxs::PRIMITIVES[n].bFlat);
        }

        assert(dxs::SaveScene(dir / "mesh.dxs", meshScene, error));
        dxs::Scene meshLoaded;
        assert(dxs::LoadScene(dir / "mesh.dxs", meshLoaded, error));
        assert(meshLoaded.vEntities.size() == dxs::PRIMITIVES.size() &&
               meshLoaded.vEntities[1].renderer == dxs::Entity::RendererKind::Mesh &&
               meshLoaded.vEntities[1].sModelAsset == "Assets/Primitives/Sphere.x");
        fs::remove(dir / "mesh.dxs");
        fs::remove_all(dir / "Assets");
    }

    // インスペクターの欄の種類: 行、値、範囲内に収める、サイズの変更、保存 / 読み込み、Start
    {
        assert(dxs::RegisterComponentClass("FieldKinds",
                                           [] { return std::unique_ptr<dxs::Component>(std::make_unique<FieldKinds>()); }));
        dxs::Scene fieldScene;
        dxs::Entity holder;
        holder.sName = "Fields";
        holder.vComponents = {"FieldKinds"};
        fieldScene.vEntities.push_back(holder);
        auto& e = fieldScene.vEntities[0];
        auto infos = dxs::ComponentFieldInfos(e, 0);
        auto findInfo = [&](const std::string& path) -> const dxs::EditorFieldInfo* {
            for (const auto& info : infos) {
                if (info.sPath == path) return &info;
            }

            return nullptr;
        };
        assert(findInfo("Mode") && findInfo("Mode")->type == dxs::EditorFieldType::Enum && findInfo("Mode")->sValue == "Walk" &&
               findInfo("Mode")->vEnumNames.size() == 3);
        assert(findInfo("Size") && findInfo("Size")->type == dxs::EditorFieldType::Vector2 && findInfo("Size")->sValue == "1,2");
        assert(findInfo("Offset") && findInfo("Offset")->sValue == "1,2,3");
        assert(findInfo("Volume") && findInfo("Volume")->bRange && findInfo("Volume")->fMax == 1.0f);
        assert(findInfo("Weights") && findInfo("Weights")->bArrayHeader);
        assert(findInfo("Weights.Size") && findInfo("Weights.Size")->sValue == "1" && !findInfo("Weights.Size")->bReadOnly);
        assert(findInfo("Weights[0]") && findInfo("Weights[0]")->sLabel == "Element 0" && findInfo("Weights[0]")->nIndent == 1);
        assert(findInfo("Points.Size") && findInfo("Points.Size")->bReadOnly && findInfo("Points[1]")); // 固定の std::array
        std::string err;
        assert(dxs::SetComponentEditorValue(e, 0, "Mode", "run", err)); // 名前は大文字小文字を区別しない
        assert(dxs::SetComponentEditorValue(e, 0, "Offset", "4, 5, 6", err));
        assert(!dxs::SetComponentEditorValue(e, 0, "Offset", "4,5", err)); // 数値が 3 つ必要
        assert(dxs::SetComponentEditorValue(e, 0, "Volume", "7", err));    // 範囲内に収められる
        assert(dxs::SetComponentEditorValue(e, 0, "Level", "0", err));
        assert(dxs::SetComponentEditorValue(e, 0, "Weights.Size", "3", err));
        assert(dxs::SetComponentEditorValue(e, 0, "Weights[2]", "9.5", err));
        assert(dxs::SetComponentEditorValue(e, 0, "Flags.Size", "2", err) &&
               dxs::SetComponentEditorValue(e, 0, "Flags[1]", "true", err));
        assert(dxs::SetComponentEditorValue(e, 0, "Points[1]", "7,8,9", err));
        assert(!dxs::SetComponentEditorValue(e, 0, "Points.Size", "5", err)); // サイズは固定
        assert(!dxs::SetComponentEditorValue(e, 0, "Weights.Size", "-1", err));
        assert(dxs::SetComponentEditorValue(e, 0, "Modes.Size", "1", err) &&
               dxs::SetComponentEditorValue(e, 0, "Modes[0]", "Idle", err));
        auto& props = e.componentProperties["FieldKinds"];
        assert(props["Mode"] == "Run" && props["Volume"] == "1" && props["Level"] == "1" && props["Weights.Size"] == "3");
        assert(props["Weights[2]"] == "9.5" && props["Flags[1]"] == "true" && props["Points[1]"] == "7,8,9" &&
               props["Modes[0]"] == "Idle");
        assert(dxs::SetComponentEditorValue(e, 0, "Weights.Size", "1", err) &&
               !props.count("Weights[2]")); // 消えた要素の値は残らない
        assert(dxs::SetComponentEditorValue(e, 0, "Weights.Size", "3", err) &&
               dxs::SetComponentEditorValue(e, 0, "Weights[2]", "9.5", err));

        // 保存 / 読み込みですべてのパスが残り、Start では保存値が見える
        assert(dxs::SaveScene(dir / "fields.dxs", fieldScene, err));
        dxs::Scene loadedFields;
        assert(dxs::LoadScene(dir / "fields.dxs", loadedFields, err));
        dxs::UpdateUserComponents(loadedFields, 0.0f);
        auto* pFields = dxs::GetComponent<FieldKinds>(loadedFields.vEntities[0]);
        assert(pFields && pFields->mode == Mode::Run && pFields->offset.z == 6.0f && pFields->nLevel == 1);
        assert(pFields->vWeights.size() == 3 && pFields->vWeights[2] == 9.5f && pFields->vFlags.size() == 2 &&
               pFields->vFlags[1]);
        assert(pFields->aPoints[1].y == 8.0f && pFields->vModes.size() == 1 && pFields->vModes[0] == Mode::Idle);
        assert(pFields->fStartedVolume == 1.0f && pFields->nStartedWeights == 3);
        fs::remove(dir / "fields.dxs");
    }

    fs::remove(dir / "scene.dxs");
    fs::remove(dir / "old.dxs");
    fs::remove(dir / "old2.dxs");
    fs::remove(dir / "bad.dxs");
    fs::remove(dir / "duplicate.dxs");
    fs::remove(dir / "old3.dxs");
    fs::remove(dir / "camera.dxs");
    fs::remove(dir / "hierarchy.dxs");
    fs::remove(dir / "triangle.x");
    fs::remove(dir / "binary.fbx");
    fs::remove(dir / "renderers.dxs");
    fs::remove(dir / "test.dxmat");
    fs::remove(dir);
    std::cout << "Scene and component tests passed.\n";
}
