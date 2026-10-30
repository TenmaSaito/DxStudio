#include "Engine.h"

#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <array>
#include <functional>
#include <cstdlib>
#include "../third_party/ufbx/ufbx.h"

namespace dxs {
namespace {
bool ValidCamera(const Camera& camera) {
    const float values[] = {camera.target.x, camera.target.y,    camera.target.z, camera.fYaw,    camera.fPitch,
                            camera.fDistance, camera.fFieldOfView, camera.fNearClip, camera.fFarClip};
    for (float value : values)
        if (!std::isfinite(value)) return false;
    return camera.fDistance >= 0.2f && camera.fDistance <= 80.0f && camera.fPitch >= -1.54f && camera.fPitch <= 1.54f &&
           camera.fFieldOfView >= 1.0f && camera.fFieldOfView <= 179.0f && camera.fNearClip > 0.0f &&
           camera.fFarClip > camera.fNearClip;
}

bool Valid(const Entity& e) {
    const auto& d = e.draw;
    const float values[] = {d.position.x, d.position.y, d.position.z, d.rotation.x, d.rotation.y, d.rotation.z, d.scale.x,
                            d.scale.y,    d.scale.z,    d.color.x,    d.color.y,    d.color.z,    d.color.w};
    if (e.sName.empty() || e.sName.size() > 1024 || e.sName.find_first_of("\r\n") != std::string::npos ||
        e.vComponents.size() > MaxComponentsPerEntity)
        return false;
    for (size_t i = 0; i < e.vComponents.size(); ++i) {
        const auto& component = e.vComponents[i];
        if (component.empty() || component.size() > 128 || component.find_first_of("\r\n") != std::string::npos ||
            std::find(e.vComponents.begin(), e.vComponents.begin() + i, component) != e.vComponents.begin() + i)
            return false;
    }
    for (const auto& [component, fields] : e.componentProperties) {
        if (component.empty() || component.size() > 128 || fields.size() > 128) return false;
        for (const auto& [name, value] : fields)
            if (name.empty() || name.size() > 128 || value.size() > 4096 || name.find_first_of("\r\n") != std::string::npos ||
                value.find_first_of("\r\n") != std::string::npos)
                return false;
    }
    for (float v : values)
        if (!std::isfinite(v)) return false;
    return true;
}

std::string PathText(const std::filesystem::path& p) {
    const auto text = p.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
using Factory = std::function<std::unique_ptr<Component>()>;
std::unordered_map<std::string, Factory>& Components() {
    static std::unordered_map<std::string, Factory> components;
    static const bool builtins = [&] {
        components.emplace("Camera", [] { return std::make_unique<CameraComponent>(); });
        return true;
    }();
    (void)builtins;
    return components;
}

bool ValidParents(const Scene& scene) {
    for (size_t i = 0; i < scene.vEntities.size(); ++i) {
        int parent = scene.vEntities[i].nParent;
        if (parent < -1 || parent >= static_cast<int>(scene.vEntities.size()) || parent == static_cast<int>(i)) return false;
        size_t steps = 0;
        while (parent >= 0) {
            if (++steps > scene.vEntities.size()) return false;
            parent = scene.vEntities[static_cast<size_t>(parent)].nParent;
        }
    }
    return true;
}

using Mesh = std::vector<DirectX::XMFLOAT3>;
struct NumberScanner {
    const char* p{};
    const char* end{};
    bool Next(double& value) {
        while (p < end && !((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.'))
            ++p;
        if (p >= end) return false;
        char* next{};
        value = std::strtod(p, &next);
        if (next == p) {
            ++p;
            return Next(value);
        }
        p = next;
        return std::isfinite(value);
    }
    bool NextInt(int& value) {
        double number{};
        if (!Next(number)) return false;
        value = static_cast<int>(number);
        return true;
    }
};
void Normalize(Mesh& mesh) {
    if (mesh.empty()) return;
    DirectX::XMFLOAT3 lo = mesh.front(), hi = mesh.front();
    for (const auto& v : mesh) {
        lo.x = std::min(lo.x, v.x);
        lo.y = std::min(lo.y, v.y);
        lo.z = std::min(lo.z, v.z);
        hi.x = std::max(hi.x, v.x);
        hi.y = std::max(hi.y, v.y);
        hi.z = std::max(hi.z, v.z);
    }
    const DirectX::XMFLOAT3 center{(lo.x + hi.x) * .5f, (lo.y + hi.y) * .5f, (lo.z + hi.z) * .5f};
    const float extent = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-6f});
    for (auto& v : mesh) {
        v.x = (v.x - center.x) / extent;
        v.y = (v.y - center.y) / extent;
        v.z = (v.z - center.z) / extent;
    }
}
std::shared_ptr<const Mesh> LoadModelMesh(const std::filesystem::path& path, float animationTime = 0.0f, int animationIndex = 0) {
    static std::unordered_map<std::wstring, std::weak_ptr<const Mesh>> cache;
    const auto key = path.lexically_normal().wstring();
    const auto ext = path.extension().wstring();
    if (_wcsicmp(ext.c_str(), L".fbx") != 0)
        if (auto found = cache.find(key); found != cache.end())
            if (auto mesh = found->second.lock()) return mesh;
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::string text((std::istreambuf_iterator<char>(input)), {});
    if (text.empty() || text.size() > 64u * 1024u * 1024u) return {};
    std::vector<DirectX::XMFLOAT3> points;
    Mesh triangles;
    if (_wcsicmp(ext.c_str(), L".x") == 0) {
        const auto begin = text.find("Mesh");
        const auto brace = begin == std::string::npos ? begin : text.find('{', begin);
        if (brace == std::string::npos) return {};
        NumberScanner scan{text.data() + brace + 1, text.data() + text.size()};
        int count{};
        if (!scan.NextInt(count) || count < 3 || count > 1000000) return {};
        points.resize(count);
        for (auto& v : points) {
            double x, y, z;
            if (!scan.Next(x) || !scan.Next(y) || !scan.Next(z)) return {};
            v = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        }
        int faces{};
        if (!scan.NextInt(faces) || faces < 1 || faces > 1000000) return {};
        for (int f = 0; f < faces; ++f) {
            int n{};
            if (!scan.NextInt(n) || n < 3 || n > 1024) return {};
            std::vector<int> ids(n);
            for (int& id : ids)
                if (!scan.NextInt(id) || id < 0 || id >= count) return {};
            for (int i = 1; i + 1 < n; ++i) {
                triangles.push_back(points[ids[0]]);
                triangles.push_back(points[ids[i]]);
                triangles.push_back(points[ids[i + 1]]);
            }
        }
    } else if (_wcsicmp(ext.c_str(), L".fbx") == 0) {
        ufbx_load_opts opts{};
        opts.generate_missing_normals = true;
        ufbx_error loadError{};
        const auto pathUtf8 = PathText(path);
        ufbx_scene* source = ufbx_load_file(pathUtf8.c_str(), &opts, &loadError);
        if (!source) return {};
        ufbx_scene* evaluated = nullptr;
        ufbx_scene* active = source;
        if (source->anim_stacks.count > 0) {
            const size_t clip = std::min(static_cast<size_t>(std::max(animationIndex, 0)), source->anim_stacks.count - 1);
            const auto* stack = source->anim_stacks.data[clip];
            const double duration = stack->time_end - stack->time_begin;
            const double time =
                duration > 0.0 ? stack->time_begin + std::fmod(std::max(animationTime, 0.0f), duration) : stack->time_begin;
            ufbx_error evaluateError{};
            evaluated = ufbx_evaluate_scene(source, stack->anim, time, nullptr, &evaluateError);
            if (evaluated) active = evaluated;
        }
        for (size_t m = 0; m < active->meshes.count; ++m) {
            const auto* mesh = active->meshes.data[m];
            if (!mesh || !mesh->vertex_position.exists) continue;
            std::vector<uint32_t> indices(std::max<size_t>(3, mesh->max_face_triangles * 3));
            const size_t instanceCount = std::max<size_t>(1, mesh->instances.count);
            for (size_t instance = 0; instance < instanceCount; ++instance) {
                const ufbx_matrix* matrix = mesh->instances.count ? &mesh->instances.data[instance]->geometry_to_world : nullptr;
                for (size_t f = 0; f < mesh->faces.count; ++f) {
                    const uint32_t count = ufbx_triangulate_face(indices.data(), indices.size(), mesh, mesh->faces.data[f]);
                    for (size_t t = 0; t < static_cast<size_t>(count) * 3; ++t) {
                        ufbx_vec3 value = ufbx_get_vertex_vec3(&mesh->skinned_position, indices[t]);
                        if (matrix) value = ufbx_transform_position(matrix, value);
                        triangles.push_back(
                            {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)});
                    }
                }
            }
        }
        if (evaluated) ufbx_free_scene(evaluated);
        ufbx_free_scene(source);
    } else
        return {};
    if (triangles.empty() || triangles.size() > 1000000) return {};
    Normalize(triangles);
    auto result = std::make_shared<Mesh>(std::move(triangles));
    if (_wcsicmp(ext.c_str(), L".fbx") != 0) cache[key] = result;
    return result;
}
} // namespace

Entity::~Entity() = default;
Entity::Entity(Entity&&) noexcept = default;
Entity& Entity::operator=(Entity&&) noexcept = default;
Entity::Entity(const Entity& other)
    : sName(other.sName), draw(other.draw), nParent(other.nParent), renderer(other.renderer), sModelAsset(other.sModelAsset),
      sMaterialAsset(other.sMaterialAsset), sTextureAsset(other.sTextureAsset), bPlayAnimation(other.bPlayAnimation),
      fAnimationTime(other.fAnimationTime), nAnimationIndex(other.nAnimationIndex), vComponents(other.vComponents),
      componentProperties(other.componentProperties) {}
Entity& Entity::operator=(const Entity& other) {
    if (this != &other) {
        sName = other.sName;
        draw = other.draw;
        nParent = other.nParent;
        renderer = other.renderer;
        sModelAsset = other.sModelAsset;
        sMaterialAsset = other.sMaterialAsset;
        sTextureAsset = other.sTextureAsset;
        bPlayAnimation = other.bPlayAnimation;
        fAnimationTime = other.fAnimationTime;
        nAnimationIndex = other.nAnimationIndex;
        vComponents = other.vComponents;
        componentProperties = other.componentProperties;
        vRuntimeComponentNames.clear();
        vRuntimeComponents.clear();
        vRuntimeComponentStarted.clear();
    }
    return *this;
}

bool LoadMaterial(const std::filesystem::path& path, Material& material, std::string& error) {
    error.clear();
    std::ifstream file(path, std::ios::binary);
    std::string magic;
    unsigned version = 0;
    Material loaded;
    if (!file ||
        !(file >> magic >> version >> std::quoted(loaded.sName) >> loaded.baseColor.x >> loaded.baseColor.y >>
          loaded.baseColor.z >> loaded.baseColor.w >> std::quoted(loaded.sTextureAsset) >> std::quoted(loaded.sShaderAsset)) ||
        magic != "DXMATERIAL" || version != 1) {
        error = "Invalid material file: " + PathText(path);
        return false;
    }
    const float values[] = {loaded.baseColor.x, loaded.baseColor.y, loaded.baseColor.z, loaded.baseColor.w};
    for (float value : values)
        if (!std::isfinite(value)) {
            error = "Material contains a non-finite color.";
            return false;
        }
    file >> std::ws;
    if (file.peek() != std::char_traits<char>::eof()) {
        error = "Unexpected data in material file.";
        return false;
    }
    material = std::move(loaded);
    return true;
}

bool SaveMaterial(const std::filesystem::path& path, const Material& material, std::string& error) {
    error.clear();
    auto temp = path;
    temp += L".tmp";
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "Cannot create material: " + PathText(path);
        return false;
    }
    file << "DXMATERIAL 1 " << std::quoted(material.sName) << ' ' << std::setprecision(std::numeric_limits<float>::max_digits10)
         << material.baseColor.x << ' ' << material.baseColor.y << ' ' << material.baseColor.z << ' ' << material.baseColor.w
         << ' ' << std::quoted(material.sTextureAsset) << ' ' << std::quoted(material.sShaderAsset) << '\n';
    file.close();
    if (!file || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Cannot save material (Windows error " + std::to_string(GetLastError()) + ").";
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return false;
    }
    return true;
}

bool LoadScene(const std::filesystem::path& path, Scene& scene, std::string& error) {
    error.clear();
    std::ifstream file(path, std::ios::binary);
    file.imbue(std::locale::classic());
    if (!file) {
        error = "Cannot open scene: " + PathText(path);
        return false;
    }
    std::string magic;
    unsigned version = 0;
    size_t count = 0;
    if (!(file >> magic >> version >> count) || magic != "DXSCENE" || (version < 1 || version > 7) || count > 10000) {
        error = "Invalid scene header (expected DXSCENE 1-7 and an entity count up to 10000).";
        return false;
    }
    Scene loaded;
    if (version >= 4) {
        int orthographic = 0;
        auto& camera = loaded.camera;
        if (!(file >> magic >> orthographic >> camera.target.x >> camera.target.y >> camera.target.z >> camera.fYaw >>
              camera.fPitch >> camera.fDistance) ||
            magic != "CAMERA" || (orthographic != 0 && orthographic != 1) || !ValidCamera(camera)) {
            error = "Invalid scene camera (projection, finite values, pitch or distance).";
            return false;
        }
        camera.bOrthographic = orthographic == 1;
    }
    loaded.vEntities.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        Entity e;
        int is2D = 0;
        auto& d = e.draw;
        bool headerValid = static_cast<bool>(file >> std::quoted(e.sName));
        if (headerValid && version == 2) {
            std::string component;
            headerValid = static_cast<bool>(file >> std::quoted(component));
            if (headerValid && !component.empty()) e.vComponents.push_back(std::move(component));
        } else if (headerValid && version >= 3) {
            size_t componentCount = 0;
            headerValid = static_cast<bool>(file >> componentCount) && componentCount <= MaxComponentsPerEntity;
            for (size_t j = 0; headerValid && j < componentCount; ++j) {
                std::string component;
                headerValid = static_cast<bool>(file >> std::quoted(component));
                if (headerValid) e.vComponents.push_back(std::move(component));
            }
        }
        if (headerValid && version >= 5) headerValid = static_cast<bool>(file >> e.nParent);
        if (headerValid && version >= 6) {
            unsigned renderer = 0;
            headerValid =
                static_cast<bool>(file >> renderer >> std::quoted(e.sModelAsset)) && renderer <= (version >= 7 ? 3u : 2u);
            e.renderer = static_cast<Entity::RendererKind>(renderer);
        }
        if (headerValid && version >= 7) {
            size_t propertyCount = 0;
            int playAnimation = 0;
            headerValid = static_cast<bool>(file >> std::quoted(e.sMaterialAsset) >> std::quoted(e.sTextureAsset) >>
                                            playAnimation >> e.fAnimationTime >> e.nAnimationIndex >> propertyCount) &&
                          (playAnimation == 0 || playAnimation == 1) && std::isfinite(e.fAnimationTime) &&
                          e.fAnimationTime >= 0.0f && e.nAnimationIndex >= 0 && propertyCount <= 8192;
            e.bPlayAnimation = playAnimation == 1;
            for (size_t p = 0; headerValid && p < propertyCount; ++p) {
                std::string component, field, value;
                headerValid = static_cast<bool>(file >> std::quoted(component) >> std::quoted(field) >> std::quoted(value));
                if (headerValid) e.componentProperties[component][field] = value;
            }
        }
        if (!headerValid ||
            !(file >> is2D >> d.position.x >> d.position.y >> d.position.z >> d.rotation.x >> d.rotation.y >> d.rotation.z >>
              d.scale.x >> d.scale.y >> d.scale.z >> d.color.x >> d.color.y >> d.color.z >> d.color.w) ||
            (is2D != 0 && is2D != 1) || !Valid(e)) {
            error = "Invalid entity at index " + std::to_string(i) + ".";
            return false;
        }
        d.bIs2D = is2D == 1;
        if (version < 6) e.renderer = Entity::RendererKind::Polygon;
        loaded.vEntities.push_back(std::move(e));
    }
    if (!ValidParents(loaded)) {
        error = "Invalid or cyclic entity parent relationship.";
        return false;
    }
    file >> std::ws;
    if (file.peek() != std::char_traits<char>::eof()) {
        error = "Unexpected data after the scene entities.";
        return false;
    }
    loaded.projectRoot = path.parent_path();
    scene = std::move(loaded);
    EngineEvents::Get().onSceneLoaded.Broadcast(scene);
    return true;
}

bool SaveScene(const std::filesystem::path& path, const Scene& scene, std::string& error) {
    error.clear();
    if (scene.vEntities.size() > 10000) {
        error = "Too many entities (maximum 10000).";
        return false;
    }
    if (!ValidCamera(scene.camera)) {
        error = "Invalid scene camera (finite values, pitch or distance).";
        return false;
    }
    if (!ValidParents(scene)) {
        error = "Invalid or cyclic entity parent relationship.";
        return false;
    }
    for (const auto& entity : scene.vEntities) {
        if (!Valid(entity)) {
            error = "Invalid entity name or non-finite transform/color.";
            return false;
        }
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "DXSCENE 7 " << scene.vEntities.size() << '\n';
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    const auto& camera = scene.camera;
    out << "CAMERA " << (camera.bOrthographic ? 1 : 0) << ' ' << camera.target.x << ' ' << camera.target.y << ' '
        << camera.target.z << ' ' << camera.fYaw << ' ' << camera.fPitch << ' ' << camera.fDistance << '\n';
    for (const auto& e : scene.vEntities) {
        const auto& d = e.draw;
        out << std::quoted(e.sName) << ' ' << e.vComponents.size();
        for (const auto& component : e.vComponents)
            out << ' ' << std::quoted(component);
        size_t propertyCount = 0;
        for (const auto& [component, fields] : e.componentProperties)
            propertyCount += fields.size();
        out << ' ' << e.nParent << ' ' << static_cast<unsigned>(e.renderer) << ' ' << std::quoted(e.sModelAsset) << ' '
            << std::quoted(e.sMaterialAsset) << ' ' << std::quoted(e.sTextureAsset) << ' ' << (e.bPlayAnimation ? 1 : 0) << ' '
            << e.fAnimationTime << ' ' << e.nAnimationIndex << ' ' << propertyCount;
        for (const auto& [component, fields] : e.componentProperties)
            for (const auto& [field, value] : fields)
                out << ' ' << std::quoted(component) << ' ' << std::quoted(field) << ' ' << std::quoted(value);
        out << ' ' << (d.bIs2D ? 1 : 0) << ' ' << d.position.x << ' ' << d.position.y << ' ' << d.position.z << ' ' << d.rotation.x
            << ' ' << d.rotation.y << ' ' << d.rotation.z << ' ' << d.scale.x << ' ' << d.scale.y << ' ' << d.scale.z << ' '
            << d.color.x << ' ' << d.color.y << ' ' << d.color.z << ' ' << d.color.w << '\n';
    }
    // Stage in the same directory and replace only after writing succeeds.
    auto temp = path;
    temp += L".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "Cannot write scene: " + PathText(temp);
            return false;
        }
        file << out.str();
        file.close();
        if (!file) {
            error = "Failed to write scene: " + PathText(temp);
            return false;
        }
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Cannot replace scene (Windows error " + std::to_string(GetLastError()) + "): " + PathText(path);
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return false;
    }
    EngineEvents::Get().onSceneSaved.Broadcast(scene);
    return true;
}

bool RegisterComponent(const std::string& name, ComponentUpdate update) {
    if (name.empty() || name.size() > 128 || name.find_first_of("\r\n") != std::string::npos || !update) return false;
    struct CallbackComponent final : Component {
        explicit CallbackComponent(ComponentUpdate callback) : callback_(callback) {}
        void Update(Entity& entity, float dt) override { callback_(entity, dt); }
        ComponentUpdate callback_;
    };
    return Components().emplace(name, [update]() { return std::make_unique<CallbackComponent>(update); }).second;
}

DrawItem PolygonRenderer::BuildDrawItem(const Entity& entity) const {
    return entity.draw;
}
DrawItem Image::BuildDrawItem(const Entity& entity) const {
    auto result = entity.draw;
    result.bIs2D = true;
    return result;
}
DrawItem ModelRenderer::BuildDrawItem(const Entity& entity) const {
    auto result = entity.draw;
    result.bIs2D = false;
    result.pVertices = asset_.empty() ? nullptr : LoadModelMesh(asset_, animationTime_, animationIndex_);
    result.bVisible = static_cast<bool>(result.pVertices);
    return result;
}

bool RegisterComponentClass(const std::string& name, ComponentFactory factory) {
    if (name.empty() || name.size() > 128 || name.find_first_of("\r\n") != std::string::npos || !factory) return false;
    return Components().emplace(name, factory).second;
}

bool IsComponentRegistered(const std::string& name) {
    return Components().find(name) != Components().end();
}

bool AttachComponent(Entity& entity, const std::string& name, std::string& error) {
    error.clear();
    if (!IsComponentRegistered(name)) {
        error = "The component is not registered: " + name;
        return false;
    }
    if (std::find(entity.vComponents.begin(), entity.vComponents.end(), name) != entity.vComponents.end()) {
        error = "This component is already attached.";
        return false;
    }
    if (entity.vComponents.size() >= MaxComponentsPerEntity) {
        error = "An object can have up to " + std::to_string(MaxComponentsPerEntity) + " components.";
        return false;
    }
    entity.vComponents.push_back(name);
    EngineEvents::Get().onComponentAttached.Broadcast(entity, name);
    return true;
}

bool DetachComponent(Entity& entity, std::size_t componentIndex) {
    if (componentIndex >= entity.vComponents.size()) return false;
    const auto removed = entity.vComponents[componentIndex];
    entity.vComponents.erase(entity.vComponents.begin() + static_cast<std::ptrdiff_t>(componentIndex));
    entity.componentProperties.erase(removed);
    entity.vRuntimeComponentNames.clear();
    entity.vRuntimeComponents.clear();
    entity.vRuntimeComponentStarted.clear();
    EngineEvents::Get().onComponentDetached.Broadcast(entity, removed);
    return true;
}

std::vector<std::string> RegisteredComponentNames() {
    std::vector<std::string> result;
    result.reserve(Components().size());
    for (const auto& component : Components())
        result.push_back(component.first);
    std::sort(result.begin(), result.end());
    return result;
}

namespace {
// Rebuilds the runtime instances when the attachment list changed.
void SyncRuntimeComponents(Entity& entity) {
    if (entity.vRuntimeComponentNames == entity.vComponents) return;
    entity.vRuntimeComponentNames = entity.vComponents;
    entity.vRuntimeComponents.clear();
    entity.vRuntimeComponentStarted.clear();
    for (const auto& name : entity.vComponents) {
        auto found = Components().find(name);
        entity.vRuntimeComponents.push_back(found == Components().end() ? nullptr : found->second());
        entity.vRuntimeComponentStarted.push_back(false);
    }
}

// Applies serialized Inspector values, then calls Start() exactly once.
void StartComponentIfNeeded(Entity& entity, std::size_t nIndex) {
    if (nIndex >= entity.vRuntimeComponentStarted.size()) entity.vRuntimeComponentStarted.resize(nIndex + 1, false);
    if (entity.vRuntimeComponentStarted[nIndex]) return;
    auto& component = *entity.vRuntimeComponents[nIndex];
    const auto saved = entity.componentProperties.find(entity.vRuntimeComponentNames[nIndex]);
    if (saved != entity.componentProperties.end()) {
        for (auto& field : component.EditorFields()) {
            const auto value = saved->second.find(field.sName);
            if (value == saved->second.end()) continue;
            std::string ignored;
            SetComponentEditorValue(entity, nIndex, field.sName, value->second, ignored);
        }
    }
    component.Start(entity);
    entity.vRuntimeComponentStarted[nIndex] = true;
    EngineEvents::Get().onComponentStarted.Broadcast(entity, component, entity.vRuntimeComponentNames[nIndex]);
}
} // namespace

void UpdateUserComponents(Scene& scene, float deltaSeconds) {
    const float fDelta = std::max(deltaSeconds, 0.0f);
    Time::Advance(fDelta);
    auto& events = EngineEvents::Get();
    events.onPreUpdate.Broadcast(scene, fDelta);

    // Pass 1: Start (first frame) and Update, in Inspector order.
    for (auto& entity : scene.vEntities) {
        if (entity.renderer == Entity::RendererKind::Model && entity.bPlayAnimation) entity.fAnimationTime += fDelta;
        SyncRuntimeComponents(entity);
        const size_t nCount = std::min(entity.vRuntimeComponentNames.size(), entity.vRuntimeComponents.size());
        for (size_t nIndex = 0; nIndex < nCount; ++nIndex) {
            if (!entity.vRuntimeComponents[nIndex]) continue;
            StartComponentIfNeeded(entity, nIndex);
            entity.vRuntimeComponents[nIndex]->Update(entity, fDelta);
        }
    }

    // Pass 2: coroutines resume after every Update (Unity order), then
    // LateUpdate, then the Inspector mirror of the live field values.
    for (auto& entity : scene.vEntities) {
        const size_t nCount = std::min(entity.vRuntimeComponentNames.size(), entity.vRuntimeComponents.size());
        for (size_t nIndex = 0; nIndex < nCount; ++nIndex) {
            if (entity.vRuntimeComponents[nIndex])
                entity.vRuntimeComponents[nIndex]->Coroutines().Tick(fDelta * Time::TimeScale(), fDelta);
        }
        for (size_t nIndex = 0; nIndex < nCount; ++nIndex) {
            if (entity.vRuntimeComponents[nIndex]) entity.vRuntimeComponents[nIndex]->LateUpdate(entity, fDelta);
        }
        for (size_t nIndex = 0; nIndex < nCount; ++nIndex) {
            if (entity.vRuntimeComponents[nIndex]) ComponentEditorValues(entity, nIndex);
        }
    }

    events.onPostUpdate.Broadcast(scene, fDelta);
}

namespace {
void EnsureComponentInstances(Entity& entity) {
    if (entity.vRuntimeComponentNames == entity.vComponents && entity.vRuntimeComponents.size() == entity.vComponents.size()) return;
    entity.vRuntimeComponentNames = entity.vComponents;
    entity.vRuntimeComponents.clear();
    entity.vRuntimeComponentStarted.clear();
    for (const auto& name : entity.vComponents) {
        auto found = Components().find(name);
        entity.vRuntimeComponents.push_back(found == Components().end() ? nullptr : found->second());
        entity.vRuntimeComponentStarted.push_back(false);
    }
}
std::string FieldText(const EditorField& field) {
    switch (field.type) {
    case EditorFieldType::Float: {
        std::ostringstream out;
        out << std::setprecision(std::numeric_limits<float>::max_digits10) << *static_cast<float*>(field.pValue);
        return out.str();
    }
    case EditorFieldType::Integer: return std::to_string(*static_cast<int*>(field.pValue));
    case EditorFieldType::Boolean: return *static_cast<bool*>(field.pValue) ? "true" : "false";
    case EditorFieldType::String: return *static_cast<std::string*>(field.pValue);
    }
    return {};
}
bool AssignField(EditorField& field, const std::string& value, std::string& error) {
    if (field.bReadOnly) {
        error = "The selected field is read-only.";
        return false;
    }
    try {
        size_t used = 0;
        if (field.type == EditorFieldType::Float) {
            const float parsed = std::stof(value, &used);
            if (used != value.size() || !std::isfinite(parsed)) throw std::invalid_argument("float");
            *static_cast<float*>(field.pValue) = parsed;
        } else if (field.type == EditorFieldType::Integer) {
            const int parsed = std::stoi(value, &used);
            if (used != value.size()) throw std::invalid_argument("int");
            *static_cast<int*>(field.pValue) = parsed;
        } else if (field.type == EditorFieldType::Boolean) {
            if (value == "true" || value == "1")
                *static_cast<bool*>(field.pValue) = true;
            else if (value == "false" || value == "0")
                *static_cast<bool*>(field.pValue) = false;
            else
                throw std::invalid_argument("bool");
        } else
            *static_cast<std::string*>(field.pValue) = value;
    } catch (...) {
        error = "The value does not match the component field type.";
        return false;
    }
    return true;
}
} // namespace

std::vector<std::pair<std::string, std::string>> ComponentEditorValues(Entity& entity, std::size_t componentIndex) {
    EnsureComponentInstances(entity);
    std::vector<std::pair<std::string, std::string>> result;
    if (componentIndex >= entity.vRuntimeComponents.size() || !entity.vRuntimeComponents[componentIndex]) return result;
    const auto component = entity.vRuntimeComponentNames[componentIndex];
    auto fields = entity.vRuntimeComponents[componentIndex]->EditorFields();
    auto& saved = entity.componentProperties[component];
    for (auto& field : fields) {
        if (auto found = saved.find(field.sName); found != saved.end() && !entity.vRuntimeComponentStarted[componentIndex]) {
            std::string ignored;
            AssignField(field, found->second, ignored);
        }
        auto value = FieldText(field);
        saved[field.sName] = value;
        result.emplace_back(field.sName, std::move(value));
    }
    return result;
}

bool SetComponentEditorValue(Entity& entity, std::size_t componentIndex, const std::string& name, const std::string& value,
                             std::string& error) {
    error.clear();
    EnsureComponentInstances(entity);
    if (componentIndex >= entity.vRuntimeComponents.size() || !entity.vRuntimeComponents[componentIndex]) {
        error = "The component class is not registered.";
        return false;
    }
    auto fields = entity.vRuntimeComponents[componentIndex]->EditorFields();
    for (auto& field : fields)
        if (field.sName == name) {
            if (!AssignField(field, value, error)) return false;
            entity.componentProperties[entity.vRuntimeComponentNames[componentIndex]][name] = FieldText(field);
            return true;
        }
    error = "The component field no longer exists.";
    return false;
}

Camera ActiveGameCamera(Scene& scene) {
    for (size_t i = 0; i < scene.vEntities.size(); ++i) {
        const auto& entity = scene.vEntities[i];
        const auto found = std::find(entity.vComponents.begin(), entity.vComponents.end(), "Camera");
        if (found == entity.vComponents.end()) continue;
        const auto props = entity.componentProperties.find("Camera");
        if (props != entity.componentProperties.end()) {
            auto active = props->second.find("Active");
            if (active != props->second.end() && (active->second == "false" || active->second == "0")) continue;
        }
        const auto world = WorldDraw(scene, static_cast<int>(i));
        Camera result;
        result.fYaw = world.rotation.y;
        result.fPitch = world.rotation.x;
        result.fDistance = 10.0f;
        const float cp = std::cos(result.fPitch);
        DirectX::XMFLOAT3 forward{std::sin(result.fYaw) * cp, std::sin(result.fPitch), std::cos(result.fYaw) * cp};
        result.target = {world.position.x + forward.x * result.fDistance, world.position.y + forward.y * result.fDistance,
                         world.position.z + forward.z * result.fDistance};
        if (props != entity.componentProperties.end()) {
            auto ortho = props->second.find("Orthographic");
            result.bOrthographic = ortho != props->second.end() && (ortho->second == "true" || ortho->second == "1");
        }
        if (props != entity.componentProperties.end()) {
            auto number = [&](const char* name, float fallback) {
                auto found = props->second.find(name);
                if (found == props->second.end()) return fallback;
                try {
                    return std::stof(found->second);
                } catch (...) {
                    return fallback;
                }
            };
            result.fFieldOfView = std::clamp(number("Field of View", 60.0f), 1.0f, 179.0f);
            result.fNearClip = std::max(number("Near Clip", 0.1f), 0.001f);
            result.fFarClip = std::max(number("Far Clip", 1000.0f), result.fNearClip + 0.001f);
        }
        return result;
    }
    return scene.camera;
}

std::vector<DrawItem> ResolveWorldDraws(const Scene& scene) {
    using namespace DirectX;
    std::vector<DrawItem> result(scene.vEntities.size());
    std::vector<unsigned char> state(scene.vEntities.size(), 0);
    std::function<void(int)> resolve = [&](int index) {
        if (index < 0 || static_cast<size_t>(index) >= scene.vEntities.size() || state[index] == 2) return;
        if (state[index] == 1) {
            result[index] = scene.vEntities[index].draw;
            state[index] = 2;
            return;
        }
        state[index] = 1;
        const auto& entity = scene.vEntities[index];
        auto world = entity.draw;
        if (entity.renderer == Entity::RendererKind::None)
            world.bVisible = false;
        else if (entity.renderer == Entity::RendererKind::Polygon)
            world = PolygonRenderer{}.BuildDrawItem(entity);
        else if (entity.renderer == Entity::RendererKind::Model)
            world = ModelRenderer(scene.projectRoot / std::filesystem::u8path(entity.sModelAsset), entity.fAnimationTime,
                                  entity.nAnimationIndex)
                        .BuildDrawItem(entity);
        else
            world = Image{}.BuildDrawItem(entity);
        if (!entity.sMaterialAsset.empty()) {
            Material material;
            std::string ignored;
            if (LoadMaterial(scene.projectRoot / std::filesystem::u8path(entity.sMaterialAsset), material, ignored)) {
                world.color.x *= material.baseColor.x;
                world.color.y *= material.baseColor.y;
                world.color.z *= material.baseColor.z;
                world.color.w *= material.baseColor.w;
            }
        }
        if (entity.nParent >= 0 && static_cast<size_t>(entity.nParent) < scene.vEntities.size()) {
            resolve(entity.nParent);
            const auto& parent = result[entity.nParent];
            XMVECTOR position = XMVectorSet(world.position.x * parent.scale.x, world.position.y * parent.scale.y,
                                            world.position.z * parent.scale.z, 1.0f);
            position = XMVector3TransformCoord(
                position, XMMatrixRotationRollPitchYaw(parent.rotation.x, parent.rotation.y, parent.rotation.z));
            position = XMVectorAdd(position, XMLoadFloat3(&parent.position));
            XMStoreFloat3(&world.position, position);
            world.rotation = {world.rotation.x + parent.rotation.x, world.rotation.y + parent.rotation.y,
                              world.rotation.z + parent.rotation.z};
            world.scale = {world.scale.x * parent.scale.x, world.scale.y * parent.scale.y, world.scale.z * parent.scale.z};
        }
        result[index] = world;
        state[index] = 2;
    };
    for (int i = 0; i < static_cast<int>(scene.vEntities.size()); ++i)
        resolve(i);
    return result;
}

DrawItem WorldDraw(const Scene& scene, int index) {
    const auto all = ResolveWorldDraws(scene);
    return index >= 0 && static_cast<size_t>(index) < all.size() ? all[index] : DrawItem{};
}

DrawItem LocalDrawFromWorld(const Scene& scene, int index, const DrawItem& world) {
    using namespace DirectX;
    if (index < 0 || static_cast<size_t>(index) >= scene.vEntities.size()) return world;
    const int parentIndex = scene.vEntities[index].nParent;
    if (parentIndex < 0 || static_cast<size_t>(parentIndex) >= scene.vEntities.size()) return world;
    const auto parent = WorldDraw(scene, parentIndex);
    DrawItem local = world;
    local.rotation = {world.rotation.x - parent.rotation.x, world.rotation.y - parent.rotation.y,
                      world.rotation.z - parent.rotation.z};
    local.scale = {std::abs(parent.scale.x) > 1e-6f ? world.scale.x / parent.scale.x : world.scale.x,
                   std::abs(parent.scale.y) > 1e-6f ? world.scale.y / parent.scale.y : world.scale.y,
                   std::abs(parent.scale.z) > 1e-6f ? world.scale.z / parent.scale.z : world.scale.z};
    auto relative = XMVectorSubtract(XMLoadFloat3(&world.position), XMLoadFloat3(&parent.position));
    relative = XMVector3TransformCoord(relative,
                                       XMMatrixRotationRollPitchYaw(-parent.rotation.x, -parent.rotation.y, -parent.rotation.z));
    XMFLOAT3 value{};
    XMStoreFloat3(&value, relative);
    local.position = {std::abs(parent.scale.x) > 1e-6f ? value.x / parent.scale.x : value.x,
                      std::abs(parent.scale.y) > 1e-6f ? value.y / parent.scale.y : value.y,
                      std::abs(parent.scale.z) > 1e-6f ? value.z / parent.scale.z : value.z};
    return local;
}

bool SetEntityParent(Scene& scene, int child, int parent, std::string& error) {
    error.clear();
    if (child < 0 || static_cast<size_t>(child) >= scene.vEntities.size() || parent < -1 ||
        parent >= static_cast<int>(scene.vEntities.size()) || child == parent) {
        error = "Invalid parent selection.";
        return false;
    }
    for (int p = parent, steps = 0; p >= 0 && steps <= static_cast<int>(scene.vEntities.size()); ++steps) {
        if (p == child) {
            error = "An object cannot be parented to one of its descendants.";
            return false;
        }
        p = scene.vEntities[p].nParent;
    }
    const auto world = WorldDraw(scene, child);
    const int old = scene.vEntities[child].nParent;
    scene.vEntities[child].nParent = parent;
    scene.vEntities[child].draw = LocalDrawFromWorld(scene, child, world);
    if (!ValidParents(scene)) {
        scene.vEntities[child].nParent = old;
        error = "The parent relationship would be cyclic.";
        return false;
    }
    return true;
}

bool LoadProjectSettings(const std::filesystem::path& path, ProjectSettings& settings, std::string& error) {
    error.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "Project settings were not found: " + PathText(path);
        return false;
    }
    std::string sMagic;
    unsigned uVersion = 0;
    if (!(file >> sMagic >> uVersion) || sMagic != "DXSETTINGS" || uVersion != 1) {
        error = "Invalid project settings header (expected DXSETTINGS 1).";
        return false;
    }
    ProjectSettings loaded;
    std::string sKey, sValue;
    while (file >> sKey >> std::quoted(sValue)) {
        if (sKey == "GraphicsApi" && !ParseGraphicsApi(sValue, loaded.graphicsApi)) {
            error = "Unknown GraphicsApi in project settings: " + sValue;
            return false;
        }
        // Unknown keys are ignored so newer settings files stay readable.
    }
    settings = loaded;
    return true;
}

bool SaveProjectSettings(const std::filesystem::path& path, const ProjectSettings& settings, std::string& error) {
    error.clear();
    auto temp = path;
    temp += L".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "Cannot write project settings: " + PathText(temp);
            return false;
        }
        file << "DXSETTINGS 1\n";
        file << "GraphicsApi " << std::quoted(GraphicsApiToken(settings.graphicsApi)) << '\n';
        file.close();
        if (!file) {
            error = "Failed to write project settings: " + PathText(temp);
            return false;
        }
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Cannot replace project settings (Windows error " + std::to_string(GetLastError()) + ").";
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return false;
    }
    return true;
}

namespace {
// Optional extras of an export: Direct3D 9 shader bytecode and the project
// settings (selected graphics API). Failure here never breaks the export,
// because the player falls back to the built-in SM3 shader and to Direct3D 12.
void ExportOptionalFiles(const std::filesystem::path& projectDir, const std::filesystem::path& dest) {
    using Microsoft::WRL::ComPtr;
    const auto shader = projectDir / "Default.hlsl";
    ComPtr<ID3DBlob> pVertex, pPixel, pDiagnostics;
    const bool bCompiled =
        SUCCEEDED(D3DCompileFromFile(shader.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_3_0",
                                     D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY, 0, &pVertex, &pDiagnostics)) &&
        SUCCEEDED(D3DCompileFromFile(shader.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_3_0",
                                     D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY, 0, &pPixel, &pDiagnostics));
    if (bCompiled) {
        for (const auto& [name, pBlob] :
             {std::pair{L"Default9.vs.cso", pVertex.Get()}, std::pair{L"Default9.ps.cso", pPixel.Get()}}) {
            std::ofstream out(dest / name, std::ios::binary | std::ios::trunc);
            out.write(static_cast<const char*>(pBlob->GetBufferPointer()), static_cast<std::streamsize>(pBlob->GetBufferSize()));
        }
    } else {
        Debug::LogWarning(
            "Export: Default.hlsl does not compile for Shader Model 3; Direct3D 9 players use the built-in shader.");
    }
    std::error_code ec;
    const auto settings = projectDir / PROJECT_SETTINGS_FILE_NAME;
    if (std::filesystem::is_regular_file(settings, ec)) {
        std::filesystem::copy_file(settings, dest / PROJECT_SETTINGS_FILE_NAME, std::filesystem::copy_options::overwrite_existing,
                                   ec);
        if (ec) Debug::LogWarning("Export: could not copy project settings: " + ec.message());
    }
}
} // namespace

bool ExportGame(const std::filesystem::path& baseDir, const std::filesystem::path& dest, const Scene& scene, std::string& error) {
    return ExportGame(baseDir, baseDir / L"project", dest, scene, error);
}

bool ExportGame(const std::filesystem::path& baseDir, const std::filesystem::path& projectDir, const std::filesystem::path& dest,
                const Scene& scene, std::string& error) {
    error.clear();
    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(baseDir, ec);
    if (ec) {
        error = "Cannot resolve application directory: " + ec.message();
        return false;
    }
    const auto target = std::filesystem::weakly_canonical(dest, ec);
    if (ec) {
        error = "Cannot resolve export directory: " + ec.message();
        return false;
    }
    const auto project = std::filesystem::weakly_canonical(projectDir, ec);
    if (ec) {
        error = "Cannot resolve project directory: " + ec.message();
        return false;
    }
    auto inside = [](const std::filesystem::path& parent, const std::filesystem::path& child) {
        auto a = parent.begin(), b = child.begin();
        for (; a != parent.end(); ++a, ++b) {
            if (b == child.end() || _wcsicmp(a->c_str(), b->c_str()) != 0) return false;
        }
        return true;
    };
    if ((inside(root, target) && inside(target, root)) || inside(project, target)) {
        error = "Choose an export directory outside the application root and project folder.";
        return false;
    }
    const auto game = baseDir / "Game.exe";
    const auto shader = projectDir / "Default.hlsl";
    if (!std::filesystem::is_regular_file(game, ec) || ec) {
        error = "Game.exe was not found next to the editor. Build the Game target first.";
        return false;
    }
    if (!std::filesystem::is_regular_file(shader, ec) || ec) {
        error = "Project shader was not found: " + PathText(shader);
        return false;
    }
    if (scene.vEntities.size() > 10000) {
        error = "Too many entities (maximum 10000).";
        return false;
    }
    if (!ValidParents(scene)) {
        error = "Invalid or cyclic entity parent relationship.";
        return false;
    }
    for (const auto& entity : scene.vEntities) {
        if (!Valid(entity)) {
            error = "Invalid entity name or non-finite transform/color.";
            return false;
        }
    }
    // Compile before creating or overwriting any export files. Includes are
    // resolved now, so no HLSL sources are required by the player.
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3DBlob> vs, ps, diagnostics;
    auto compile = [&](const char* entry, const char* profile, ComPtr<ID3DBlob>& blob) {
        diagnostics.Reset();
        HRESULT hr = D3DCompileFromFile(shader.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, profile,
                                        D3DCOMPILE_ENABLE_STRICTNESS, 0, &blob, &diagnostics);
        if (FAILED(hr)) {
            error = diagnostics
                        ? std::string(static_cast<const char*>(diagnostics->GetBufferPointer()), diagnostics->GetBufferSize())
                        : "Failed to compile project shader (HRESULT " + std::to_string(static_cast<unsigned long>(hr)) + ").";
            return false;
        }
        return true;
    };
    if (!compile("VSMain", "vs_5_0", vs) || !compile("PSMain", "ps_5_0", ps)) return false;
    std::filesystem::create_directories(dest, ec);
    if (ec) {
        error = "Cannot create export directory: " + ec.message();
        return false;
    }
    const std::array<const wchar_t*, 4> names = {L"Game.exe", L"scene.dxs", L"Default.vs.cso", L"Default.ps.cso"};
    for (const auto* name : names) {
        const auto output = dest / name;
        if (std::filesystem::exists(output, ec) && !std::filesystem::is_regular_file(output, ec)) {
            error = "The export output is not a regular file: " + PathText(output);
            return false;
        }
        if (ec) {
            error = "Cannot inspect export output: " + ec.message();
            return false;
        }
    }
    // Stage all four files separately before replacing any existing output.
    // A unique directory also avoids clobbering unrelated .tmp files.
    std::filesystem::path stage;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        stage = dest / (L".dxstudio-stage-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(attempt));
        if (std::filesystem::create_directory(stage, ec)) break;
        if (ec) {
            error = "Cannot create export staging directory: " + ec.message();
            return false;
        }
        stage.clear();
    }
    if (stage.empty()) {
        error = "Cannot reserve a unique export staging directory.";
        return false;
    }
    auto cleanup = [&]() {
        std::error_code ignored;
        for (size_t i = 0; i < names.size(); ++i) {
            std::filesystem::remove(stage / names[i], ignored);
            std::filesystem::remove(stage / (L"backup-" + std::to_wstring(i)), ignored);
        }
        std::filesystem::remove(stage / L"scene.dxs.tmp", ignored);
        std::filesystem::remove(stage, ignored);
    };
    auto writeBlob = [&](const std::filesystem::path& name, ID3DBlob* blob) {
        std::ofstream out(name, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "Cannot write shader data: " + PathText(name);
            return false;
        }
        out.write(static_cast<const char*>(blob->GetBufferPointer()), static_cast<std::streamsize>(blob->GetBufferSize()));
        out.close();
        if (!out) {
            error = "Failed to write shader data: " + PathText(name);
            return false;
        }
        return true;
    };
    std::filesystem::copy_file(game, stage / L"Game.exe", ec);
    if (ec) {
        error = "Cannot copy Game.exe: " + ec.message();
        cleanup();
        return false;
    }
    if (!writeBlob(stage / L"Default.vs.cso", vs.Get()) || !writeBlob(stage / L"Default.ps.cso", ps.Get()) ||
        !SaveScene(stage / L"scene.dxs", scene, error)) {
        cleanup();
        return false;
    }

    std::array<bool, 4> hadOriginal{};
    for (size_t i = 0; i < names.size(); ++i) {
        hadOriginal[i] = std::filesystem::exists(dest / names[i], ec);
        if (ec) {
            error = "Cannot inspect existing export: " + ec.message();
            cleanup();
            return false;
        }
        if (hadOriginal[i]) {
            std::filesystem::copy_file(dest / names[i], stage / (L"backup-" + std::to_wstring(i)), ec);
            if (ec) {
                error = "Cannot back up existing export: " + ec.message();
                cleanup();
                return false;
            }
        }
    }
    for (size_t i = 0; i < names.size(); ++i) {
        if (!MoveFileExW((stage / names[i]).c_str(), (dest / names[i]).c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            error =
                "Cannot replace export file (Windows error " + std::to_string(GetLastError()) + "): " + PathText(dest / names[i]);
            bool rollbackFailed = false;
            for (size_t j = 0; j < i; ++j) {
                if (hadOriginal[j])
                    std::filesystem::copy_file(stage / (L"backup-" + std::to_wstring(j)), dest / names[j],
                                               std::filesystem::copy_options::overwrite_existing, ec);
                else
                    std::filesystem::remove(dest / names[j], ec);
                if (ec) {
                    rollbackFailed = true;
                    ec.clear();
                }
            }
            if (rollbackFailed) {
                error += " Rollback failed; backups were retained in " + PathText(stage);
                return false;
            }
            cleanup();
            return false;
        }
    }
    const auto projectAssets = projectDir / L"Assets";
    if (std::filesystem::is_directory(projectAssets, ec)) {
        ec.clear();
        std::filesystem::create_directories(dest / L"Assets", ec);
        if (!ec)
            std::filesystem::copy(projectAssets, dest / L"Assets",
                                  std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                                  ec);
        if (ec) {
            error = "The game was exported, but project Assets could not be copied: " + ec.message();
            cleanup();
            return false;
        }
    }
    cleanup();
    ExportOptionalFiles(projectDir, dest);
    Debug::Log("Exported game to " + PathText(dest));
    return true;
}
} // namespace dxs
