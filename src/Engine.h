#pragma once

#include "Renderer.h"
#include "Core/Coroutine.h"
#include "Core/Delegate.h"
#include "Core/EngineCallbacks.h"
#include "Core/Log.h"
#include "Core/Time.h"
#include "Graphics/GraphicsApi.h"
#include "Primitives.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <concepts>
#include <functional>
#include <type_traits>
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

// ---- Inspector fields --------------------------------------------------------
//   EditField("Speed", m_fSpeed)                    float / int / bool / std::string
//   EditField("Offset", m_offset)                   DirectX::XMFLOAT2 / XMFLOAT3 (X Y Z on one row)
//   EditField("Points", m_vPoints)                  std::vector<T>   (Size is editable)
//   EditField("Slots", m_aSlots)                    std::array<T, N> (fixed Size)
//   EditEnum("Mode", m_mode, {"Idle", "Walk"})      any enum, shown as a drop-down
//   EditRange("Volume", m_fVolume, 0.0f, 1.0f)      float / int with a slider (Unity [Range])
// Array elements may be any of the single-value types above (or enums via EditEnum).
enum class EditorFieldType : unsigned char { Float, Integer, Boolean, String, Enum, Vector2, Vector3, Array };

struct EditorArrayAccess;
struct EditorField {
    std::string sName;
    EditorFieldType type{};
    void* pValue{};
    bool bReadOnly = false;
    // Enum: display names of the values 0..n-1; pValue points to an enum of nEnumBytes bytes.
    std::vector<std::string> vEnumNames;
    unsigned char nEnumBytes = 0;
    // Slider range for Float / Integer (values are clamped to it).
    bool bRange = false;
    float fMin = 0.0f, fMax = 1.0f;
    // Array: element description and access.
    std::shared_ptr<EditorArrayAccess> pArray;
};

// Text form of a single value ("1.5", "true", "Walk", "1,2,3") and its parser.
std::string EditorFieldText(const EditorField& field);
bool AssignEditorField(EditorField& field, const std::string& sValue, std::string& error);

struct EditorArrayAccess {
    EditorField element;      // type / enum names / range of every element (pValue unused)
    bool bFixedSize = false;  // std::array: Size is read-only
    std::function<std::size_t()> size;
    std::function<void(std::size_t)> resize; // dynamic arrays only
    std::function<std::string(std::size_t)> get;
    std::function<bool(std::size_t, const std::string&, std::string&)> set;
};

namespace detail {
template<class T> inline constexpr bool IS_EDITOR_LEAF =
    std::same_as<T, float> || std::same_as<T, int> || std::same_as<T, bool> || std::same_as<T, std::string> ||
    std::same_as<T, DirectX::XMFLOAT2> || std::same_as<T, DirectX::XMFLOAT3>;
template<class T> struct StdVector : std::false_type {};
template<class U, class A> struct StdVector<std::vector<U, A>> : std::true_type { using Element = U; };
template<class T> struct StdArray : std::false_type {};
template<class U, std::size_t N> struct StdArray<std::array<U, N>> : std::true_type { using Element = U; };

template<class T> EditorField LeafField(std::string sName, T* pValue, bool bReadOnly) {
    EditorField field{std::move(sName), EditorFieldType::Float, pValue, bReadOnly};
    if constexpr (std::same_as<T, int>) field.type = EditorFieldType::Integer;
    else if constexpr (std::same_as<T, bool>) field.type = EditorFieldType::Boolean;
    else if constexpr (std::same_as<T, std::string>) field.type = EditorFieldType::String;
    else if constexpr (std::same_as<T, DirectX::XMFLOAT2>) field.type = EditorFieldType::Vector2;
    else if constexpr (std::same_as<T, DirectX::XMFLOAT3>) field.type = EditorFieldType::Vector3;
    return field;
}

// Array field over any container with operator[] (elements copied through a
// temporary, so std::vector<bool> works too).
template<class Container> EditorField ArrayField(std::string sName, Container& container, EditorField element, bool bFixed, bool bReadOnly) {
    using U = typename Container::value_type; // not decltype(container[0]): std::vector<bool> returns a proxy
    EditorField field{std::move(sName), EditorFieldType::Array, &container, bReadOnly};
    auto pAccess = std::make_shared<EditorArrayAccess>();
    pAccess->element = std::move(element);
    pAccess->element.pValue = nullptr;
    pAccess->bFixedSize = bFixed;
    pAccess->size = [&container] { return static_cast<std::size_t>(container.size()); };
    if constexpr (requires { container.resize(std::size_t{}); })
        pAccess->resize = [&container](std::size_t nSize) { container.resize(nSize); };
    const EditorField elementInfo = pAccess->element;
    pAccess->get = [&container, elementInfo](std::size_t nIndex) {
        U temp = container[nIndex];
        EditorField leaf = elementInfo;
        leaf.pValue = &temp;
        return EditorFieldText(leaf);
    };
    pAccess->set = [&container, elementInfo](std::size_t nIndex, const std::string& sValue, std::string& error) {
        U temp = container[nIndex];
        EditorField leaf = elementInfo;
        leaf.pValue = &temp;
        if (!AssignEditorField(leaf, sValue, error)) return false;
        container[nIndex] = temp;
        return true;
    };
    field.pArray = std::move(pAccess);
    return field;
}

template<class E> EditorField EnumField(std::string sName, E* pValue, std::vector<std::string> vNames, bool bReadOnly) {
    EditorField field{std::move(sName), EditorFieldType::Enum, pValue, bReadOnly};
    field.vEnumNames = std::move(vNames);
    field.nEnumBytes = static_cast<unsigned char>(sizeof(E));
    return field;
}
} // namespace detail

template<class T> EditorField EditField(std::string sName, T& value, bool bReadOnly = false) {
    if constexpr (detail::IS_EDITOR_LEAF<T>) {
        return detail::LeafField(std::move(sName), &value, bReadOnly);
    } else if constexpr (detail::StdVector<T>::value || detail::StdArray<T>::value) {
        using U = typename T::value_type;
        static_assert(detail::IS_EDITOR_LEAF<U>, "Array elements must be float, int, bool, std::string, XMFLOAT2 or XMFLOAT3 (enums: EditEnum)");
        return detail::ArrayField(std::move(sName), value, detail::LeafField<U>("", nullptr, bReadOnly), detail::StdArray<T>::value, bReadOnly);
    } else {
        static_assert(!sizeof(T), "Unsupported editor field type (see the list above EditorFieldType)");
    }
}

// Enum as a drop-down; vNames[i] is the name of value i.
template<class E> requires std::is_enum_v<E>
EditorField EditEnum(std::string sName, E& value, std::vector<std::string> vNames, bool bReadOnly = false) {
    return detail::EnumField(std::move(sName), &value, std::move(vNames), bReadOnly);
}
template<class E, class A> requires std::is_enum_v<E>
EditorField EditEnum(std::string sName, std::vector<E, A>& vValues, std::vector<std::string> vNames, bool bReadOnly = false) {
    return detail::ArrayField(std::move(sName), vValues, detail::EnumField<E>("", nullptr, std::move(vNames), bReadOnly), false, bReadOnly);
}
template<class E, std::size_t N> requires std::is_enum_v<E>
EditorField EditEnum(std::string sName, std::array<E, N>& aValues, std::vector<std::string> vNames, bool bReadOnly = false) {
    return detail::ArrayField(std::move(sName), aValues, detail::EnumField<E>("", nullptr, std::move(vNames), bReadOnly), true, bReadOnly);
}

// Slider + value box (Unity [Range(min, max)]).
template<class T> requires std::same_as<T, float> || std::same_as<T, int>
EditorField EditRange(std::string sName, T& value, float fMin, float fMax, bool bReadOnly = false) {
    EditorField field = detail::LeafField(std::move(sName), &value, bReadOnly);
    field.bRange = true;
    field.fMin = std::min(fMin, fMax);
    field.fMax = std::max(fMin, fMax);
    return field;
}

// One Inspector row after arrays are expanded (Unity property paths):
//   "Points" (header), "Points.Size", "Points[0]", "Points[1]", ...
struct EditorFieldInfo {
    std::string sPath;   // key used by SetComponentEditorValue and the scene file
    std::string sLabel;  // "Speed", "Size", "Element 0"
    EditorFieldType type{};
    bool bReadOnly = false;
    std::vector<std::string> vEnumNames;
    bool bRange = false;
    float fMin = 0.0f, fMax = 1.0f;
    int nIndent = 0;           // 1 for array size / elements
    bool bArrayHeader = false; // foldout row, no value
    bool bArraySize = false;
    std::string sArrayPath;    // owning array (size / element rows)
    std::string sValue;
};

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
// Every Inspector row of a component (arrays expanded), with its current value.
std::vector<EditorFieldInfo> ComponentFieldInfos(Entity& entity, std::size_t componentIndex);
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
#include "Input/playerInput.h" // CInputSystem + PlayerInput
