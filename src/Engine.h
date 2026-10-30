#pragma once

#include "Renderer.h"
#include <filesystem>
#include <concepts>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace dxs {

struct Entity;

struct Material {
    std::string name = "Material";
    DirectX::XMFLOAT4 baseColor{1, 1, 1, 1};
    std::string textureAsset;
    std::string shaderAsset = "Default.hlsl";
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

class Image final : public IRender {
public:
    [[nodiscard]] DrawItem BuildDrawItem(const Entity& entity) const override;
};

template<class T> concept RenderComponent = std::derived_from<T, IRender>;
template<class T> inline constexpr bool IsRenderComponent = RenderComponent<T>;
static_assert(IsRenderComponent<PolygonRenderer> && IsRenderComponent<ModelRenderer> && IsRenderComponent<Image>);

enum class EditorFieldType : unsigned char { Float, Integer, Boolean, String };
struct EditorField {
    std::string name;
    EditorFieldType type{};
    void* value{};
    bool readOnly = false;
};

template<class T> constexpr EditorField EditField(std::string name, T& value, bool readOnly = false) {
    if constexpr (std::same_as<T, float>) return {std::move(name), EditorFieldType::Float, &value, readOnly};
    else if constexpr (std::same_as<T, int>) return {std::move(name), EditorFieldType::Integer, &value, readOnly};
    else if constexpr (std::same_as<T, bool>) return {std::move(name), EditorFieldType::Boolean, &value, readOnly};
    else if constexpr (std::same_as<T, std::string>) return {std::move(name), EditorFieldType::String, &value, readOnly};
    else static_assert(!sizeof(T), "DxStudio editor fields support float, int, bool and std::string");
}

// User behavior is defined as a class, matching the component model used by
// scene-based editors. One instance is kept for each attachment while running.
class Component {
public:
    virtual ~Component() = default;
    virtual void Start(Entity&) {}
    virtual void Update(Entity&, float deltaSeconds) = 0;
    // Return fields that should be editable in Inspector. A private member may
    // deliberately be exposed by returning EditField() from this member function.
    virtual std::vector<EditorField> EditorFields() { return {}; }
};

class CameraComponent final : public Component {
public:
    bool active = true;
    bool orthographic = false;
    float fieldOfView = 60.0f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    void Update(Entity&, float) override {}
    std::vector<EditorField> EditorFields() override {
        return {EditField("Active", active), EditField("Orthographic", orthographic),
                EditField("Field of View", fieldOfView), EditField("Near Clip", nearClip),
                EditField("Far Clip", farClip)};
    }
};

using ComponentFactory = std::unique_ptr<Component> (*)();

struct Entity {
    enum class RendererKind : unsigned char { None, Polygon, Model, Image };
    std::string name;
    DrawItem draw;
    int parent = -1;
    RendererKind renderer = RendererKind::Polygon;
    std::string modelAsset;
    std::string materialAsset;
    std::string textureAsset;
    bool playAnimation = true;
    float animationTime = 0.0f;
    int animationIndex = 0;
    // Attachments are resolved against the project component registry.
    std::vector<std::string> components;
    using PropertyMap = std::unordered_map<std::string, std::string>;
    std::unordered_map<std::string, PropertyMap> componentProperties;
    std::vector<std::string> runtimeComponentNames;
    std::vector<std::unique_ptr<Component>> runtimeComponents;
    std::vector<bool> runtimeComponentStarted;

    Entity() = default;
    ~Entity();
    Entity(const Entity& other);
    Entity& operator=(const Entity& other);
    Entity(Entity&&) noexcept;
    Entity& operator=(Entity&&) noexcept;
};

constexpr std::size_t MaxComponentsPerEntity = 64;

struct Scene {
    std::vector<Entity> entities;
    Camera camera;
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
bool DetachComponent(Entity& entity, std::size_t componentIndex);
// Alphabetically sorted registry snapshot for the editor's component picker.
std::vector<std::string> RegisteredComponentNames();
void UpdateUserComponents(Scene& scene, float deltaSeconds);
std::vector<std::pair<std::string, std::string>> ComponentEditorValues(Entity& entity, std::size_t componentIndex);
bool SetComponentEditorValue(Entity& entity, std::size_t componentIndex,
                             const std::string& field, const std::string& value, std::string& error);
Camera ActiveGameCamera(Scene& scene);

// Implemented by the project. Register each custom component here.
void RegisterProjectComponents();

#define DXS_REGISTER_COMPONENT(Type) \
    namespace { \
    static_assert(std::derived_from<Type, dxs::Component>, "DXS component classes must derive from dxs::Component"); \
    std::unique_ptr<dxs::Component> Create_##Type() { return std::make_unique<Type>(); } \
    const bool Registered_##Type = dxs::RegisterComponentClass(#Type, Create_##Type); \
    }

} // namespace dxs
