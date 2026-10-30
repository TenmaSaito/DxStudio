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
    entity.name = "A name with spaces";
    entity.components = {"Spin", "MoveTest", "Unregistered"};
    entity.draw.position = {1, 2, 3};
    entity.materialAsset = "Assets/Materials/Test.dxmat";
    entity.textureAsset = "Assets/Textures/Test.png";
    entity.animationIndex = 2;
    entity.componentProperties["MoveTest"]["Speed"] = "2";
    original.entities.push_back(entity);
    original.camera.target = {1.25f, -2.5f, 7.125f};
    original.camera.yaw = 0.7f;
    original.camera.pitch = -0.4f;
    original.camera.distance = 12.5f;
    original.camera.orthographic = true;
    if (!dxs::SaveScene(dir / "scene.dxs", original, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    dxs::Scene loaded;
    assert(dxs::LoadScene(dir / "scene.dxs", loaded, error));
    assert(loaded.entities.size() == 1 && loaded.entities[0].name == entity.name);
    assert(loaded.entities[0].components == entity.components && loaded.entities[0].draw.position.y == 2);
    assert(loaded.entities[0].materialAsset == entity.materialAsset && loaded.entities[0].textureAsset == entity.textureAsset);
    assert(loaded.entities[0].animationIndex == 2 && loaded.entities[0].componentProperties["MoveTest"]["Speed"] == "2");
    assert(loaded.camera.orthographic && loaded.camera.target.x == original.camera.target.x &&
        loaded.camera.target.y == original.camera.target.y && loaded.camera.target.z == original.camera.target.z &&
        loaded.camera.yaw == original.camera.yaw && loaded.camera.pitch == original.camera.pitch &&
        loaded.camera.distance == original.camera.distance);
    dxs::RegisterProjectComponents();
    assert(dxs::RegisterComponentClass("MoveTest", CreateMoveTest));
    assert(!dxs::RegisterComponentClass("MoveTest", CreateMoveTest));
    assert(dxs::IsComponentRegistered("Spin"));
    assert(!dxs::IsComponentRegistered("Missing"));
    const auto registered = dxs::RegisteredComponentNames();
    assert(std::find(registered.begin(), registered.end(), "Spin") != registered.end());
    assert(std::is_sorted(registered.begin(), registered.end()));
    dxs::UpdateUserComponents(loaded, 1.0f);
    assert(std::abs(loaded.entities[0].draw.rotation.y - 0.7f) < 0.0001f);
    assert(std::abs(loaded.entities[0].draw.position.x - 3.0f) < 0.0001f);
    assert(std::abs(loaded.entities[0].draw.position.z - 4.0f) < 0.0001f);
    dxs::UpdateUserComponents(loaded, 1.0f);
    assert(std::abs(loaded.entities[0].draw.position.x - 5.0f) < 0.0001f);
    assert(std::abs(loaded.entities[0].draw.position.z - 4.0f) < 0.0001f);
    dxs::Entity detachable=loaded.entities[0];
    assert(detachable.componentProperties.contains("MoveTest"));
    assert(dxs::DetachComponent(detachable,1));
    assert(std::find(detachable.components.begin(),detachable.components.end(),"MoveTest")==detachable.components.end());
    assert(!detachable.componentProperties.contains("MoveTest")&&detachable.runtimeComponents.empty());
    assert(!dxs::DetachComponent(detachable,99));
    {
        std::ofstream old(dir / "old.dxs");
        old << "DXSCENE 1 1\n\"Old\" 1 0 0 0 0 0 0 1 1 1 1 1 1 1\n";
    }
    assert(dxs::LoadScene(dir / "old.dxs", loaded, error));
    assert(loaded.entities.size() == 1 && loaded.entities[0].components.empty() && loaded.entities[0].draw.is2D);
    assert(!loaded.camera.orthographic && loaded.camera.distance == dxs::Camera{}.distance);
    {
        std::ofstream old2(dir / "old2.dxs");
        old2 << "DXSCENE 2 1\n\"Legacy\" \"Unregistered\" 0 0 0 0 0 0 0 1 1 1 1 1 1 1\n";
    }
    assert(dxs::LoadScene(dir / "old2.dxs", loaded, error));
    assert(loaded.entities.size() == 1 && loaded.entities[0].components == std::vector<std::string>{"Unregistered"});
    {
        std::ofstream bad(dir / "bad.dxs");
        bad << "DXSCENE 2 1\nbroken\n";
    }
    assert(!dxs::LoadScene(dir / "bad.dxs", loaded, error));
    assert(loaded.entities.size() == 1 && loaded.entities[0].name == "Legacy");
    dxs::Scene duplicate = original;
    duplicate.entities[0].components.push_back("Spin");
    assert(!dxs::SaveScene(dir / "duplicate.dxs", duplicate, error));
    {
        std::ofstream bad3(dir / "duplicate.dxs");
        bad3 << "DXSCENE 3 1\n\"Dup\" 2 \"Spin\" \"Spin\" 0 0 0 0 0 0 0 1 1 1 1 1 1 1\n";
    }
    assert(!dxs::LoadScene(dir / "duplicate.dxs", loaded, error));
    assert(loaded.entities.size() == 1 && loaded.entities[0].name == "Legacy");
    {
        std::ofstream old3(dir / "old3.dxs");
        old3 << "DXSCENE 3 1\n\"Legacy3\" 1 \"Spin\" 0 0 0 0 0 0 0 1 1 1 1 1 1 1\n";
    }
    assert(dxs::LoadScene(dir / "old3.dxs", loaded, error));
    assert(loaded.entities[0].components == std::vector<std::string>{"Spin"} && !loaded.camera.orthographic);
    assert(dxs::LoadScene(dir / "scene.dxs", loaded, error));
    for (const char* badCamera : {"CAMERA 2 0 0 0 0 0 6", "CAMERA 1 0 0 0 0 0 0",
            "CAMERA 1 0 0 0 0 2 6", "CAMERA 1 0 0 0 0 0 81", "CAMERA 1 0 0"}) {
        { std::ofstream bad4(dir / "camera.dxs"); bad4 << "DXSCENE 4 0\n" << badCamera << '\n'; }
        assert(!dxs::LoadScene(dir / "camera.dxs", loaded, error));
        assert(loaded.entities.size() == 1 && loaded.entities[0].name == entity.name &&
            loaded.camera.orthographic && loaded.camera.distance == original.camera.distance);
    }
    dxs::Scene badCamera = original;
    badCamera.camera.distance = std::numeric_limits<float>::infinity();
    assert(!dxs::SaveScene(dir / "scene.dxs", badCamera, error));
    assert(dxs::LoadScene(dir / "scene.dxs", loaded, error) && loaded.camera.distance == original.camera.distance);
    dxs::Scene hierarchy;
    dxs::Entity parent;
    parent.name = "Parent";
    parent.draw.position = {10.0f, 0.0f, 0.0f};
    dxs::Entity child;
    child.name = "Child";
    child.draw.position = {12.0f, 2.0f, 0.0f};
    hierarchy.entities.push_back(parent);
    hierarchy.entities.push_back(child);
    assert(dxs::SetEntityParent(hierarchy, 1, 0, error));
    assert(hierarchy.entities[1].parent == 0);
    auto childWorld = dxs::WorldDraw(hierarchy, 1);
    assert(std::abs(childWorld.position.x - 12.0f) < 0.0001f);
    hierarchy.entities[0].draw.position.x += 3.0f;
    childWorld = dxs::WorldDraw(hierarchy, 1);
    assert(std::abs(childWorld.position.x - 15.0f) < 0.0001f);
    assert(!dxs::SetEntityParent(hierarchy, 0, 1, error));
    assert(hierarchy.entities[0].parent == -1);
    assert(dxs::SaveScene(dir / "hierarchy.dxs", hierarchy, error));
    dxs::Scene hierarchyLoaded;
    assert(dxs::LoadScene(dir / "hierarchy.dxs", hierarchyLoaded, error));
    assert(hierarchyLoaded.entities.size() == 2 && hierarchyLoaded.entities[1].parent == 0);
    assert(std::abs(dxs::WorldDraw(hierarchyLoaded, 1).position.x - 15.0f) < 0.0001f);
    {
        std::ofstream model(dir / "triangle.x");
        model << "xof 0303txt 0032\nMesh { 3; 0;0;0;, 1;0;0;, 0;1;0;; 1; 3;0,1,2;; }\n";
    }
    dxs::Scene renderScene; renderScene.projectRoot = dir;
    dxs::Entity empty; empty.name="Empty"; empty.renderer=dxs::Entity::RendererKind::None;
    dxs::Entity model; model.name="Model"; model.renderer=dxs::Entity::RendererKind::Model; model.modelAsset="triangle.x";
    fs::copy_file(fs::current_path()/"tests"/"assets"/"blender_272_cube_7400_binary.fbx",dir/"binary.fbx");
    dxs::Entity binary; binary.name="Binary FBX"; binary.renderer=dxs::Entity::RendererKind::Model; binary.modelAsset="binary.fbx";
    renderScene.entities.push_back(empty); renderScene.entities.push_back(model); renderScene.entities.push_back(binary);
    const auto renderItems=dxs::ResolveWorldDraws(renderScene);
    assert(!renderItems[0].visible && renderItems[1].visible && renderItems[1].vertices && renderItems[1].vertices->size()==3);
    assert(renderItems[2].visible && renderItems[2].vertices && renderItems[2].vertices->size()>=3);
    assert(dxs::SaveScene(dir / "renderers.dxs",renderScene,error));
    dxs::Scene renderLoaded; assert(dxs::LoadScene(dir / "renderers.dxs",renderLoaded,error));
    assert(renderLoaded.entities[0].renderer==dxs::Entity::RendererKind::None && renderLoaded.entities[1].renderer==dxs::Entity::RendererKind::Model);
    dxs::Material material; material.name="Test Material"; material.baseColor={0.5f,0.25f,1.0f,1.0f}; material.textureAsset="Assets/Textures/Test.png";
    assert(dxs::SaveMaterial(dir/"test.dxmat",material,error)); dxs::Material loadedMaterial;
    assert(dxs::LoadMaterial(dir/"test.dxmat",loadedMaterial,error));
    assert(loadedMaterial.name==material.name && loadedMaterial.textureAsset==material.textureAsset && loadedMaterial.baseColor.y==0.25f);
    dxs::Entity cameraEntity; cameraEntity.name="Camera A"; cameraEntity.renderer=dxs::Entity::RendererKind::None;
    cameraEntity.components={"Camera"}; cameraEntity.draw.position={3,4,5}; cameraEntity.draw.rotation={0.1f,0.2f,0};
    renderScene.entities.push_back(cameraEntity); const auto gameCamera=dxs::ActiveGameCamera(renderScene);
    assert(std::abs(gameCamera.yaw-0.2f)<0.0001f && std::abs(gameCamera.pitch-0.1f)<0.0001f);
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
