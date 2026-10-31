#pragma once

#include "Renderer.h"
#include "Core/Coroutine.h"
#include "Core/Delegate.h"
#include "Core/EngineCallbacks.h"
#include "Core/Log.h"
#include "Core/Time.h"
#include "Graphics/GraphicsApi.h"
#include "Primitives.h"
#include <filesystem>
#include <concepts>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace dxs {

struct Entity;

struct Material {
    std::string sName = "Material";
    DirectX::XMFLOAT4 baseColor{1, 1, 1, 1};
    std::string sTextureAsset;
    std::string sShaderAsset = "Default.hlsl";
};
bool LoadMaterial(const std::filesystem::path& path, Material& material, std::string& error);
bool SaveMaterial(const std::filesystem::path& path, const Material& material, std::string& error);

// Rendering is a replaceable component contract. Behavior components never
// depend on the DirectX 12 backend and entities may intentionally have no
// renderer at all.
class IRender {
public:
    virtual ~IRender() = default;
    [[nodiscard]] virtual DrawItem BuildDrawItem(const Entity& entity) const = 0;
};

class PolygonRenderer final : public IRender {
public:
    [[nodiscard]] DrawItem BuildDrawItem(const Entity& entity) const override;
};

class ModelRenderer final : public IRender {
public:
    explicit ModelRenderer(std::filesystem::path asset = {}, float animationTime = 0.0f, int animationIndex = 0)
        : asset_(std::move(asset)), animationTime_(animationTime), animationIndex_(animationIndex) {}
    [[nodiscard]] DrawItem BuildDrawItem(const Entity& entity) const override;
    [[nodiscard]] const std::filesystem::path& Asset() const noexcept { return asset_; }
private:
    std::filesystem::path asset_;
    float animationTime_{};
    int animationIndex_{};
};

// Unity MeshRenderer: draws the mesh asset (built-in primitive, X or FBX) set
// in its Mesh field. Flat meshes in the XY plane are picked and outlined as 2D.
class MeshRenderer final : public IRender {
public:
    explicit MeshRenderer(std::filesystem::path mesh = {}, float fAnimationTime = 0.0f, int nAnimationIndex = 0)
        : m_mesh(std::move(mesh)), m_fAnimationTime(fAnimationTime), m_nAnimationIndex(nAnimationIndex) {}
    [[nodiscard]] DrawItem BuildDrawItem(const Entity& entity) const override;
    [[nodiscard]] const std::filesystem::path& Mesh() const noexcept { return m_mesh; }
private:
    std::filesystem::path m_mesh;
    float m_fAnimationTime = 0.0f;
    int m_nAnimationIndex = 0;
};

class Image final : public IRender {
public:
    [[nodiscard]] DrawItem BuildDrawItem(const Entity& entity) const override;
};

template<class T> concept RenderComponent = std::derived_from<T, IRender>;
template<class T> inline constexpr bool IsRenderComponent = RenderComponent<T>;
static_assert(IsRenderComponent<PolygonRenderer> && IsRenderComponent<ModelRenderer> && IsRenderComponent<MeshRenderer> && IsRenderComponent<Image>);

enum class EditorFieldType : unsigned char { Float, Integer, Boolean, String };
struct EditorField {
    std::string sName;
    EditorFieldType type{};
    void* pValue{};
    bool bReadOnly = false;
};

template<class T> constexpr EditorField EditField(std::string sName, T& value, bool bReadOnly = false) {
    if constexpr (std::same_as<T, float>) return {std::move(sName), EditorFieldType::Float, &value, bReadOnly};
    else if constexpr (std::same_as<T, int>) return {std::move(sName), EditorFieldType::Integer, &value, bReadOnly};
    else if constexpr (std::same_as<T, bool>) return {std::move(sName), EditorFieldType::Boolean, &value, bReadOnly};
    else if constexpr (std::same_as<T, std::string>) return {std::move(sName), EditorFieldType::String, &value, bReadOnly};
    else static_assert(!sizeof(T), "DxStudio editor fields support float, int, bool and std::string");
}

// Contact passed to OnCollisionEnter / Stay / Exit (see Physics.h).
struct Collision {
    Entity* pOther = nullptr;            // the other object (valid during the call)
    int nOtherIndex = -1;                // index in Scene::vEntities
    DirectX::XMFLOAT3 normal{0, 1, 0};   // points away from the other object, toward this one
    DirectX::XMFLOAT3 point{0, 0, 0};    // approximate world contact point
    float fRelativeSpeed = 0.0f;         // closing speed along the normal when it started
};

// User behavior is defined as a class, matching the component model used by
// scene-based editors. One instance is kept for each attachment while running.
//
// Frame order:  Start (once) -> Update -> physics (FixedUpdate, collision /
// trigger events, 0..n fixed steps) -> coroutines -> LateUpdate
class Component {
public:
    virtual ~Component() = default;
    virtual void Start(Entity&) {}
    virtual void Update(Entity&, float fDeltaSeconds) = 0;
    // Runs after every Update and coroutine of the frame (camera follow, etc.).
    virtual void LateUpdate(Entity&, float /*fDeltaSeconds*/) {}
    // Physics (Physics.h). FixedUpdate runs once per fixed step before the
    // simulation; the events need a Rigidbody on one of the two objects.
    virtual void FixedUpdate(Entity&, float /*fFixedDeltaSeconds*/) {}
    virtual void OnCollisionEnter(Entity&, const Collision&) {}
    virtual void OnCollisionStay(Entity&, const Collision&) {}
    virtual void OnCollisionExit(Entity&, const Collision&) {}
    virtual void OnTriggerEnter(Entity&, Entity& /*other*/) {}
    virtual void OnTriggerStay(Entity&, Entity& /*other*/) {}
    virtual void OnTriggerExit(Entity&, Entity& /*other*/) {}
    // Return fields that should be editable in Inspector. A private member may
    // deliberately be exposed by returning EditField() from this member function.
    virtual std::vector<EditorField> EditorFields() { return {}; }

    // ---- Coroutines (see Core/Coroutine.h) -------------------------------
    // Runs `coroutine` until its first co_yield now, then once per frame.
    CoroutineHandle StartCoroutine(Coroutine coroutine) { return m_coroutines.Start(std::move(coroutine)); }
    bool StopCoroutine(CoroutineHandle handle) { return m_coroutines.Stop(handle); }
    void StopAllCoroutines() { m_coroutines.StopAll(); }
    [[nodiscard]] bool IsCoroutineRunning(CoroutineHandle handle) const { return m_coroutines.IsRunning(handle); }
    CoroutineRunner& Coroutines() noexcept { return m_coroutines; }

private:
    CoroutineRunner m_coroutines;
};

class CameraComponent final : public Component {
public:
    bool m_bActive = true;
    bool m_bOrthographic = false;
    float m_fFieldOfView = 60.0f;
    float m_fNearClip = 0.1f;
    float m_fFarClip = 1000.0f;
    void Update(Entity&, float) override {}
    std::vector<EditorField> EditorFields() override {
        return {EditField("Active", m_bActive), EditField("Orthographic", m_bOrthographic),
                EditField("Field of View", m_fFieldOfView), EditField("Near Clip", m_fNearClip),
                EditField("Far Clip", m_fFarClip)};
    }
};

using ComponentFactory = std::unique_ptr<Component> (*)();

struct Entity {
    enum class RendererKind : unsigned char { None, Polygon, Model, Image, Mesh };
    std::string sName;
    DrawItem draw;
    int nParent = -1;
    RendererKind renderer = RendererKind::Polygon;
    std::string sModelAsset;
    std::string sMaterialAsset;
    std::string sTextureAsset;
    bool bPlayAnimation = true;
    float fAnimationTime = 0.0f;
    int nAnimationIndex = 0;
    // Attachments are resolved against the project component registry.
    std::vector<std::string> vComponents;
    using PropertyMap = std::unordered_map<std::string, std::string>;
    std::unordered_map<std::string, PropertyMap> componentProperties;
    std::vector<std::string> vRuntimeComponentNames;
    std::vector<std::unique_ptr<Component>> vRuntimeComponents;
    std::vector<bool> vRuntimeComponentStarted;

    Entity() = default;
    ~Entity();
    Entity(const Entity& other);
    Entity& operator=(const Entity& other);
    Entity(Entity&&) noexcept;
    Entity& operator=(Entity&&) noexcept;
};

constexpr std::size_t MaxComponentsPerEntity = 64;

// Running instance of component T on entity (Play Mode), or nullptr.
// Example: if (auto* pBody = GetComponent<Rigidbody>(entity)) pBody->AddForce(...);
template<class T> T* GetComponent(Entity& entity) {
    for (auto& pComponent : entity.vRuntimeComponents)
        if (auto* pFound = dynamic_cast<T*>(pComponent.get())) return pFound;
    return nullptr;
}

// Runtime-only physics bookkeeping (never serialized).
struct PhysicsState {
    float fAccumulator = 0.0f;
    std::vector<std::uint64_t> vContacts; // entity index pairs touching last step
    std::vector<std::uint64_t> vTriggers;
};

struct Scene {
    std::vector<Entity> vEntities;
    Camera camera;
    PhysicsState physics;
    // Runtime-only base for project-relative model assets; never serialized.
    std::filesystem::path projectRoot;
};

// Load into a temporary scene first, so a malformed file never destroys the
// scene currently being edited. Text is UTF-8 and entity names are quoted.
bool LoadScene(const std::filesystem::path& path, Scene& scene, std::string& error);
bool SaveScene(const std::filesystem::path& path, const Scene& scene, std::string& error);
bool ExportGame(const std::filesystem::path& baseDir, const std::filesystem::path& dest,
                const Scene& scene, std::string& error);
bool ExportGame(const std::filesystem::path& baseDir, const std::filesystem::path& projectDir,
                const std::filesystem::path& dest, const Scene& scene, std::string& error);

// Resolve local parent-relative transforms into world-space draw items.
std::vector<DrawItem> ResolveWorldDraws(const Scene& scene);
DrawItem WorldDraw(const Scene& scene, int index);
DrawItem LocalDrawFromWorld(const Scene& scene, int index, const DrawItem& world);
bool SetEntityParent(Scene& scene, int child, int parent, std::string& error);

// Register callbacks at startup. The callback receives the owning entity and
// elapsed seconds; it may change its transform or color.
using ComponentUpdate = void (*)(Entity&, float);
bool RegisterComponent(const std::string& name, ComponentUpdate update);
bool RegisterComponentClass(const std::string& name, ComponentFactory factory);
bool IsComponentRegistered(const std::string& name);
// Appends a registered component (or "Camera"). Fails on duplicates and when
// the entity already has MaxComponentsPerEntity attachments.
bool AttachComponent(Entity& entity, const std::string& name, std::string& error);
bool DetachComponent(Entity& entity, std::size_t componentIndex);
// Alphabetically sorted registry snapshot for the editor's component picker.
std::vector<std::string> RegisteredComponentNames();
void UpdateUserComponents(Scene& scene, float fDeltaSeconds);
std::vector<std::pair<std::string, std::string>> ComponentEditorValues(Entity& entity, std::size_t componentIndex);
bool SetComponentEditorValue(Entity& entity, std::size_t componentIndex,
                             const std::string& field, const std::string& value, std::string& error);
Camera ActiveGameCamera(Scene& scene);

// ---- Project settings ----------------------------------------------------
// Stored as <project>/ProjectSettings.dxsettings and copied next to exported
// games. Missing files fall back to the defaults below.
inline constexpr const wchar_t* PROJECT_SETTINGS_FILE_NAME = L"ProjectSettings.dxsettings";
struct ProjectSettings {
    GraphicsApi graphicsApi = GraphicsApi::DIRECT3D12;
};
bool LoadProjectSettings(const std::filesystem::path& path, ProjectSettings& settings, std::string& error);
bool SaveProjectSettings(const std::filesystem::path& path, const ProjectSettings& settings, std::string& error);

// Implemented by the project. Register each custom component here.
void RegisterProjectComponents();

#define DXS_REGISTER_COMPONENT(Type) \
    namespace { \
    static_assert(std::derived_from<Type, dxs::Component>, "DXS component classes must derive from dxs::Component"); \
    std::unique_ptr<dxs::Component> Create_##Type() { return std::make_unique<Type>(); } \
    const bool Registered_##Type = dxs::RegisterComponentClass(#Type, Create_##Type); \
    }

} // namespace dxs

#include "Physics.h" // Rigidbody / colliders are part of the engine API
