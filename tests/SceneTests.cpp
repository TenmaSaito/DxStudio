#include "Engine.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>

class MoveTest final : public dxs::Component {
public:
    float speed = 1.0f;
    bool enabled = true;
    void Start(dxs::Entity& entity) override { entity.draw.position.z += 1.0f; }
    void Update(dxs::Entity& entity, float dt) override { if (enabled) entity.draw.position.x += dt * speed; }
    std::vector<dxs::EditorField> EditorFields() override {
        return {dxs::EditField("Speed", speed), dxs::EditField("Enabled", enabled)};
    }
};
std::unique_ptr<dxs::Component> CreateMoveTest() { return std::make_unique<MoveTest>(); }

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
    assert(loaded.vEntities[0].sMaterialAsset == entity.sMaterialAsset && loaded.vEntities[0].sTextureAsset == entity.sTextureAsset);
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
    dxs::Entity detachable=loaded.vEntities[0];
    assert(detachable.componentProperties.contains("MoveTest"));
    assert(dxs::DetachComponent(detachable,1));
    assert(std::find(detachable.vComponents.begin(),detachable.vComponents.end(),"MoveTest")==detachable.vComponents.end());
    assert(!detachable.componentProperties.contains("MoveTest")&&detachable.vRuntimeComponents.empty());
    assert(!dxs::DetachComponent(detachable,99));
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
    for (const char* badCamera : {"CAMERA 2 0 0 0 0 0 6", "CAMERA 1 0 0 0 0 0 0",
            "CAMERA 1 0 0 0 0 2 6", "CAMERA 1 0 0 0 0 0 81", "CAMERA 1 0 0"}) {
        { std::ofstream bad4(dir / "camera.dxs"); bad4 << "DXSCENE 4 0\n" << badCamera << '\n'; }
        assert(!dxs::LoadScene(dir / "camera.dxs", loaded, error));
        assert(loaded.vEntities.size() == 1 && loaded.vEntities[0].sName == entity.sName &&
            loaded.camera.bOrthographic && loaded.camera.fDistance == original.camera.fDistance);
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
    dxs::Scene renderScene; renderScene.projectRoot = dir;
    dxs::Entity empty; empty.sName="Empty"; empty.renderer=dxs::Entity::RendererKind::None;
    dxs::Entity model; model.sName="Model"; model.renderer=dxs::Entity::RendererKind::Model; model.sModelAsset="triangle.x";
    fs::copy_file(fs::current_path()/"tests"/"assets"/"blender_272_cube_7400_binary.fbx",dir/"binary.fbx");
    dxs::Entity binary; binary.sName="Binary FBX"; binary.renderer=dxs::Entity::RendererKind::Model; binary.sModelAsset="binary.fbx";
    renderScene.vEntities.push_back(empty); renderScene.vEntities.push_back(model); renderScene.vEntities.push_back(binary);
    const auto renderItems=dxs::ResolveWorldDraws(renderScene);
    assert(!renderItems[0].bVisible && renderItems[1].bVisible && renderItems[1].pVertices && renderItems[1].pVertices->size()==3);
    assert(renderItems[2].bVisible && renderItems[2].pVertices && renderItems[2].pVertices->size()>=3);
    assert(dxs::SaveScene(dir / "renderers.dxs",renderScene,error));
    dxs::Scene renderLoaded; assert(dxs::LoadScene(dir / "renderers.dxs",renderLoaded,error));
    assert(renderLoaded.vEntities[0].renderer==dxs::Entity::RendererKind::None && renderLoaded.vEntities[1].renderer==dxs::Entity::RendererKind::Model);
    dxs::Material material; material.sName="Test Material"; material.baseColor={0.5f,0.25f,1.0f,1.0f}; material.sTextureAsset="Assets/Textures/Test.png";
    assert(dxs::SaveMaterial(dir/"test.dxmat",material,error)); dxs::Material loadedMaterial;
    assert(dxs::LoadMaterial(dir/"test.dxmat",loadedMaterial,error));
    assert(loadedMaterial.sName==material.sName && loadedMaterial.sTextureAsset==material.sTextureAsset && loadedMaterial.baseColor.y==0.25f);
    dxs::Entity cameraEntity; cameraEntity.sName="Camera A"; cameraEntity.renderer=dxs::Entity::RendererKind::None;
    cameraEntity.vComponents={"Camera"}; cameraEntity.draw.position={3,4,5}; cameraEntity.draw.rotation={0.1f,0.2f,0};
    renderScene.vEntities.push_back(cameraEntity); const auto gameCamera=dxs::ActiveGameCamera(renderScene);
    assert(std::abs(gameCamera.fYaw-0.2f)<0.0001f && std::abs(gameCamera.fPitch-0.1f)<0.0001f);
    // Built-in primitives: written as .x files, loaded through MeshRenderer.
    {
        assert(dxs::EnsurePrimitiveAssets(dir, error));
        dxs::Scene meshScene; meshScene.projectRoot = dir;
        for (const auto& info : dxs::PRIMITIVES) {
            const auto vBuilt = dxs::BuildPrimitive(info.shape);
            assert(!vBuilt.empty() && vBuilt.size() % 3 == 0);
            for (const auto& v : vBuilt) assert(std::abs(v.x) <= 0.5001f && std::abs(v.y) <= 0.5001f && std::abs(v.z) <= 0.5001f);
            assert(fs::is_regular_file(dir / fs::u8path(dxs::PrimitiveAssetPath(info.shape))));
            dxs::Entity mesh; mesh.sName = info.pName; mesh.renderer = dxs::Entity::RendererKind::Mesh;
            mesh.sModelAsset = dxs::PrimitiveAssetPath(info.shape);
            meshScene.vEntities.push_back(mesh);
        }
        const auto vMeshDraws = dxs::ResolveWorldDraws(meshScene);
        for (size_t n = 0; n < dxs::PRIMITIVES.size(); ++n) {
            const auto& draw = vMeshDraws[n];
            assert(draw.bVisible && draw.pVertices && draw.pShadeRuns);
            assert(draw.pVertices->size() == dxs::BuildPrimitive(dxs::PRIMITIVES[n].shape).size());
            std::uint64_t nRuns = 0; for (auto nRun : *draw.pShadeRuns) nRuns += nRun;
            assert(nRuns == draw.pVertices->size());
            // 2D shapes (and only those) are flat in XY; Plane lies in XZ.
            assert(draw.bIs2D == dxs::PRIMITIVES[n].bFlat);
        }
        assert(dxs::SaveScene(dir / "mesh.dxs", meshScene, error));
        dxs::Scene meshLoaded; assert(dxs::LoadScene(dir / "mesh.dxs", meshLoaded, error));
        assert(meshLoaded.vEntities.size() == dxs::PRIMITIVES.size() && meshLoaded.vEntities[1].renderer == dxs::Entity::RendererKind::Mesh &&
               meshLoaded.vEntities[1].sModelAsset == "Assets/Primitives/Sphere.x");
        fs::remove(dir / "mesh.dxs");
        fs::remove_all(dir / "Assets");
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
