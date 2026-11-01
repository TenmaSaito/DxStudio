//==================================================================================
//
// エンジン本体のソースファイル [Engine.cpp]
//
// シーンの読み込み / 保存、メッシュの読み込み、コンポーネントの登録と実行、
// インスペクターの値の読み書き、親子関係の計算、ゲームの書き出しを行う。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
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
#include "../third_party/ufbx/ufbx.h" // FBX の読み込みライブラリ

namespace dxs {
namespace {
//**********************************************************************************
// *** 値の検証 ***
//**********************************************************************************
// --- カメラの値が有効か（有限の値で、距離・角度が範囲内か） ---
bool ValidCamera(const Camera& camera) {
    const float values[] = {camera.target.x,  camera.target.y,     camera.target.z,  camera.fYaw,    camera.fPitch,
                            camera.fDistance, camera.fFieldOfView, camera.fNearClip, camera.fFarClip};
    for (float value : values) {
        if (!std::isfinite(value)) return false;
    }

    return camera.fDistance >= 0.2f && camera.fDistance <= 80.0f && camera.fPitch >= -1.54f && camera.fPitch <= 1.54f &&
           camera.fFieldOfView >= 1.0f && camera.fFieldOfView <= 179.0f && camera.fNearClip > 0.0f &&
           camera.fFarClip > camera.fNearClip;
}

// --- エンティティの値が有効か（名前・コンポーネント・保存値・トランスフォーム） ---
bool Valid(const Entity& e) {
    const auto& d = e.draw;
    const float values[] = {d.position.x, d.position.y, d.position.z, d.rotation.x, d.rotation.y, d.rotation.z, d.scale.x,
                            d.scale.y,    d.scale.z,    d.color.x,    d.color.y,    d.color.z,    d.color.w};

    // 名前は 1..1024 文字で改行なし、コンポーネント数は上限以内
    if (e.sName.empty() || e.sName.size() > 1024 || e.sName.find_first_of("\r\n") != std::string::npos ||
        e.vComponents.size() > MaxComponentsPerEntity) {
        return false;
    }

    // コンポーネント名: 空でなく、128 文字以内、改行なし、重複なし
    for (size_t i = 0; i < e.vComponents.size(); ++i) {
        const auto& component = e.vComponents[i];
        if (component.empty() || component.size() > 128 || component.find_first_of("\r\n") != std::string::npos ||
            std::find(e.vComponents.begin(), e.vComponents.begin() + i, component) != e.vComponents.begin() + i) {
            return false;
        }
    }

    // 保存値: 名前と値の長さ、改行を含まないこと
    for (const auto& [component, fields] : e.componentProperties) {
        if (component.empty() || component.size() > 128 || fields.size() > 128) return false;
        for (const auto& [name, value] : fields) {
            if (name.empty() || name.size() > 128 || value.size() > 4096 || name.find_first_of("\r\n") != std::string::npos ||
                value.find_first_of("\r\n") != std::string::npos) {
                return false;
            }
        }
    }

    // トランスフォームと色はすべて有限の値
    for (float v : values) {
        if (!std::isfinite(v)) return false;
    }

    return true;
}

// --- パスを UTF-8 の文字列にする処理（エラーメッセージ用） ---
std::string PathText(const std::filesystem::path& p) {
    const auto text = p.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

//**********************************************************************************
// *** コンポーネントの登録表 ***
//**********************************************************************************
// 名前 → 生成関数。組み込みのコンポーネントは初回のアクセス時に登録する。
using Factory = std::function<std::unique_ptr<Component>()>;

std::unordered_map<std::string, Factory>& Components() {
    static std::unordered_map<std::string, Factory> components;
    static const bool builtins = [&] {
        components.emplace("Camera", [] { return std::make_unique<CameraComponent>(); });
        components.emplace("Rigidbody", [] { return std::make_unique<Rigidbody>(); });
        components.emplace("BoxCollider", [] { return std::make_unique<BoxCollider>(); });
        components.emplace("SphereCollider", [] { return std::make_unique<SphereCollider>(); });
        components.emplace("MeshCollider", [] { return std::make_unique<MeshCollider>(); });
        components.emplace("PlayerInput", [] { return std::make_unique<PlayerInput>(); });
        return true;
    }();
    (void)builtins;
    return components;
}

// --- 親子関係が有効か（範囲内で、自分自身や循環を含まない） ---
bool ValidParents(const Scene& scene) {
    for (size_t i = 0; i < scene.vEntities.size(); ++i) {
        int parent = scene.vEntities[i].nParent;
        if (parent < -1 || parent >= static_cast<int>(scene.vEntities.size()) || parent == static_cast<int>(i)) return false;

        // 親をたどり、エンティティ数より多く進んだら循環している
        size_t steps = 0;
        while (parent >= 0) {
            if (++steps > scene.vEntities.size()) return false;
            parent = scene.vEntities[static_cast<size_t>(parent)].nParent;
        }
    }

    return true;
}

//**********************************************************************************
// *** メッシュの読み込み ***
//**********************************************************************************
using Mesh = std::vector<DirectX::XMFLOAT3>;  // 三角形リスト（3 頂点で 1 つ）
using ShadeRuns = std::vector<std::uint32_t>; // 明るさごとの頂点数
constexpr int SHADE_LEVELS = 8;               // 陰影の段階数

// --- 三角形を明るさ順に並べ替える処理 ---
// メッシュ空間の固定の光で各三角形の明るさを求め、SHADE_LEVELS 段階に分ける。
// これにより、単色のシェーダーでも立体の形が分かる。
ShadeRuns SortByShade(Mesh& mesh) {
    using namespace DirectX;
    const XMVECTOR light = XMVector3Normalize(XMVectorSet(-0.35f, 0.85f, -0.4f, 0));
    std::vector<std::vector<XMFLOAT3>> vBuckets(SHADE_LEVELS);
    for (size_t nIndex = 0; nIndex + 2 < mesh.size(); nIndex += 3) {
        const XMVECTOR a = XMLoadFloat3(&mesh[nIndex]), b = XMLoadFloat3(&mesh[nIndex + 1]), c = XMLoadFloat3(&mesh[nIndex + 2]);

        // 面の法線と光の向きの内積を 0..1 の明るさにする
        XMVECTOR normal = XMVector3Cross(XMVectorSubtract(b, a), XMVectorSubtract(c, a));
        const float fLength = XMVectorGetX(XMVector3Length(normal));
        float fLight = 0.5f;
        if (fLength > 1e-12f) {
            fLight = (XMVectorGetX(XMVector3Dot(XMVectorScale(normal, 1.0f / fLength), light)) + 1.0f) * 0.5f;
        }

        const int nLevel = std::clamp(static_cast<int>(fLight * SHADE_LEVELS), 0, SHADE_LEVELS - 1);
        vBuckets[nLevel].insert(vBuckets[nLevel].end(), {mesh[nIndex], mesh[nIndex + 1], mesh[nIndex + 2]});
    }

    // 暗い順に連結し、区間ごとの頂点数を記録する
    ShadeRuns vRuns;
    mesh.clear();
    for (auto& bucket : vBuckets) {
        vRuns.push_back(static_cast<std::uint32_t>(bucket.size()));
        mesh.insert(mesh.end(), bucket.begin(), bucket.end());
    }

    return vRuns;
}

// 読み込んだメッシュ
struct LoadedMesh {
    std::shared_ptr<const Mesh> pVertices;       // 頂点（正規化済み）
    std::shared_ptr<const ShadeRuns> pShadeRuns; // 明るさごとの頂点数
    bool bFlat = false;                          // すべての頂点が z = 0 上にある（2D の基本図形）
};

// テキストから数値を順に読み取る（.x ファイル用。区切り文字は読み飛ばす）
struct NumberScanner {
    const char* p{};   // 現在の位置
    const char* end{}; // 終端

    // 次の数値を読む
    bool Next(double& value) {
        while (p < end && !((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.')) {
            ++p;
        }

        if (p >= end) return false;
        char* next{};
        value = std::strtod(p, &next);
        if (next == p) { // 数値として読めなかった（"-" だけなど）ので 1 文字進めてやり直す
            ++p;
            return Next(value);
        }

        p = next;
        return std::isfinite(value);
    }

    // 次の数値を整数として読む
    bool NextInt(int& value) {
        double number{};
        if (!Next(number)) return false;
        value = static_cast<int>(number);
        return true;
    }
};

// --- メッシュを単位ボックス [-0.5, 0.5] に収める処理（中心を原点へ、最も長い辺を 1 に） ---
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

//==================================================================================
// --- モデルファイル（.x / .fbx）の読み込み処理 ---
//==================================================================================
LoadedMesh LoadModelMesh(const std::filesystem::path& path, float animationTime = 0.0f, int animationIndex = 0) {
    // 静的なメッシュ（.x）はパスと更新日時ごとにキャッシュする（ディスク上で
    // ファイルを編集すれば反映される）。アニメーションする FBX は毎フレーム評価する。
    struct CacheEntry {
        std::filesystem::file_time_type time;
        LoadedMesh mesh;
    };

    static std::unordered_map<std::wstring, CacheEntry> cache;
    const auto key = path.lexically_normal().wstring();
    const auto ext = path.extension().wstring();
    std::error_code timeError;
    const auto writeTime = std::filesystem::last_write_time(path, timeError);
    if (_wcsicmp(ext.c_str(), L".fbx") != 0) {
        if (auto found = cache.find(key); found != cache.end() && !timeError && found->second.time == writeTime) {
            return found->second.mesh;
        }
    }

    // ファイル全体を読み込む（64MB まで）
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::string text((std::istreambuf_iterator<char>(input)), {});
    if (text.empty() || text.size() > 64u * 1024u * 1024u) return {};
    std::vector<DirectX::XMFLOAT3> points;
    Mesh triangles;
    if (_wcsicmp(ext.c_str(), L".x") == 0) {
        // DirectX のテキスト形式: Mesh { 頂点数; 頂点...; 面数; 面（頂点数; 番号...）... }
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
            for (int& id : ids) {
                if (!scan.NextInt(id) || id < 0 || id >= count) return {};
            }

            // 多角形を扇状に三角形へ分割する
            for (int i = 1; i + 1 < n; ++i) {
                triangles.push_back(points[ids[0]]);
                triangles.push_back(points[ids[i]]);
                triangles.push_back(points[ids[i + 1]]);
            }
        }
    } else if (_wcsicmp(ext.c_str(), L".fbx") == 0) {
        // FBX: ufbx で読み込む
        ufbx_load_opts opts{};
        opts.generate_missing_normals = true;
        ufbx_error loadError{};
        const auto pathUtf8 = PathText(path);
        ufbx_scene* source = ufbx_load_file(pathUtf8.c_str(), &opts, &loadError);
        if (!source) return {};
        ufbx_scene* evaluated = nullptr;
        ufbx_scene* active = source;

        // アニメーションがあれば、指定した時間（ループ）の姿勢を評価する
        if (source->anim_stacks.count > 0) {
            const size_t clip = std::min(static_cast<size_t>(std::max(animationIndex, 0)), source->anim_stacks.count - 1);
            const auto* stack = source->anim_stacks.data[clip];
            const double duration = stack->time_end - stack->time_begin;
            const double time =
                duration > 0.0 ? stack->time_begin + std::fmod(std::max(animationTime, 0.0f), duration) : stack->time_begin;
            ufbx_error evaluateError{};
            evaluated = ufbx_evaluate_scene(source, stack->anim, time, nullptr, &evaluateError);
            if (evaluated) {
                active = evaluated;
            }
        }

        // 全メッシュ・全インスタンスの面を三角形にし、ワールド座標へ変換して集める
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
                        if (matrix) {
                            value = ufbx_transform_position(matrix, value);
                        }

                        triangles.push_back(
                            {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)});
                    }
                }
            }
        }

        if (evaluated) {
            ufbx_free_scene(evaluated);
        }

        ufbx_free_scene(source);
    } else {
        return {};
    }

    // 正規化 → 2D 判定 → 明るさ順の並べ替え → キャッシュ
    if (triangles.empty() || triangles.size() > 1000000) return {};
    Normalize(triangles);
    LoadedMesh result;
    result.bFlat =
        std::all_of(triangles.begin(), triangles.end(), [](const DirectX::XMFLOAT3& v) { return std::abs(v.z) < 1e-5f; });
    result.pShadeRuns = std::make_shared<ShadeRuns>(SortByShade(triangles));
    result.pVertices = std::make_shared<Mesh>(std::move(triangles));
    if (_wcsicmp(ext.c_str(), L".fbx") != 0 && !timeError) {
        cache[key] = {writeTime, result};
    }

    return result;
}
} // namespace

//**********************************************************************************
// *** エンティティ ***
//**********************************************************************************
// コピーは保存される情報だけを複製し、実行中のインスタンスは複製しない
// （コピー先では Play 時に作り直される）。
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

        // 実行中のインスタンスは破棄する
        vRuntimeComponentNames.clear();
        vRuntimeComponents.clear();
        vRuntimeComponentStarted.clear();
    }

    return *this;
}

//**********************************************************************************
// *** マテリアル ***
//**********************************************************************************
//==================================================================================
// --- マテリアルの読み込み処理 ---
//==================================================================================
// 書式: DXMATERIAL 1 "名前" R G B A "テクスチャ" "シェーダー"
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
    for (float value : values) {
        if (!std::isfinite(value)) {
            error = "Material contains a non-finite color.";
            return false;
        }
    }

    // 余計なデータが続いていないか
    file >> std::ws;
    if (file.peek() != std::char_traits<char>::eof()) {
        error = "Unexpected data in material file.";
        return false;
    }

    material = std::move(loaded);
    return true;
}

//==================================================================================
// --- マテリアルの保存処理 ---
//==================================================================================
bool SaveMaterial(const std::filesystem::path& path, const Material& material, std::string& error) {
    error.clear();

    // 一時ファイルに書いてから置き換える（書き込み途中で壊れないように）
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

//**********************************************************************************
// *** シーン ***
//**********************************************************************************
//==================================================================================
// --- シーンの読み込み処理 ---
//==================================================================================
// 書式（バージョン 8）:
//   DXSCENE 8 <エンティティ数>
//   CAMERA <平行投影> <注視点 x y z> <yaw> <pitch> <距離>
//   "名前" <コンポーネント数> "名前"... <親> <レンダラー> "モデル" "マテリアル" "テクスチャ"
//     <アニメ再生> <アニメ時間> <アニメ番号> <保存値の数> ("コンポーネント" "変数" "値")...
//     <2D> <位置 x y z> <回転 x y z> <拡大率 x y z> <色 r g b a>
// 古いバージョン（1..7）も読める（無い項目は既定値）。
bool LoadScene(const std::filesystem::path& path, Scene& scene, std::string& error) {
    error.clear();
    std::ifstream file(path, std::ios::binary);
    file.imbue(std::locale::classic()); // 地域設定に関係なく "." を小数点にする
    if (!file) {
        error = "Cannot open scene: " + PathText(path);
        return false;
    }

    std::string magic;
    unsigned version = 0;
    size_t count = 0;
    if (!(file >> magic >> version >> count) || magic != "DXSCENE" || (version < 1 || version > 8) || count > 10000) {
        error = "Invalid scene header (expected DXSCENE 1-8 and an entity count up to 10000).";
        return false;
    }

    // 一時的なシーンに読み込み、成功したときだけ置き換える
    Scene loaded;
    if (version >= 4) { // カメラ（バージョン 4 以降）
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

        // 名前とコンポーネント（バージョン 2 は 1 つ、3 以降は個数付き）
        bool headerValid = static_cast<bool>(file >> std::quoted(e.sName));
        if (headerValid && version == 2) {
            std::string component;
            headerValid = static_cast<bool>(file >> std::quoted(component));
            if (headerValid && !component.empty()) {
                e.vComponents.push_back(std::move(component));
            }
        } else if (headerValid && version >= 3) {
            size_t componentCount = 0;
            headerValid = static_cast<bool>(file >> componentCount) && componentCount <= MaxComponentsPerEntity;
            for (size_t j = 0; headerValid && j < componentCount; ++j) {
                std::string component;
                headerValid = static_cast<bool>(file >> std::quoted(component));
                if (headerValid) {
                    e.vComponents.push_back(std::move(component));
                }
            }
        }

        if (headerValid && version >= 5) {
            headerValid = static_cast<bool>(file >> e.nParent); // 親（5 以降）
        }

        if (headerValid && version >= 6) { // レンダラーとモデル（6 以降。種類の上限はバージョンで変わる）
            unsigned renderer = 0;
            headerValid = static_cast<bool>(file >> renderer >> std::quoted(e.sModelAsset)) && renderer <= (version >= 8   ? 4u
                                                                                                            : version >= 7 ? 3u
                                                                                                                           : 2u);
            e.renderer = static_cast<Entity::RendererKind>(renderer);
        }

        if (headerValid && version >= 7) { // マテリアル・テクスチャ・アニメーション・保存値（7 以降）
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
                if (headerValid) {
                    e.componentProperties[component][field] = value;
                }
            }
        }

        // トランスフォームと色
        if (!headerValid ||
            !(file >> is2D >> d.position.x >> d.position.y >> d.position.z >> d.rotation.x >> d.rotation.y >> d.rotation.z >>
              d.scale.x >> d.scale.y >> d.scale.z >> d.color.x >> d.color.y >> d.color.z >> d.color.w) ||
            (is2D != 0 && is2D != 1) || !Valid(e)) {
            error = "Invalid entity at index " + std::to_string(i) + ".";
            return false;
        }

        d.bIs2D = is2D == 1;
        if (version < 6) {
            e.renderer = Entity::RendererKind::Polygon;
        }

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

    // 成功: シーンを置き換えて通知する
    loaded.projectRoot = path.parent_path();
    scene = std::move(loaded);
    EngineEvents::Get().onSceneLoaded.Broadcast(scene);
    return true;
}

//==================================================================================
// --- シーンの保存処理（常に最新のバージョン 8 で書く） ---
//==================================================================================
bool SaveScene(const std::filesystem::path& path, const Scene& scene, std::string& error) {
    error.clear();

    // 保存する前にすべての値を検証する
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

    // まずメモリ上に書き出す（float は元の値に戻せる桁数で）
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "DXSCENE 8 " << scene.vEntities.size() << '\n';
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    const auto& camera = scene.camera;
    out << "CAMERA " << (camera.bOrthographic ? 1 : 0) << ' ' << camera.target.x << ' ' << camera.target.y << ' '
        << camera.target.z << ' ' << camera.fYaw << ' ' << camera.fPitch << ' ' << camera.fDistance << '\n';
    for (const auto& e : scene.vEntities) {
        const auto& d = e.draw;
        out << std::quoted(e.sName) << ' ' << e.vComponents.size();
        for (const auto& component : e.vComponents) {
            out << ' ' << std::quoted(component);
        }

        size_t propertyCount = 0;
        for (const auto& [component, fields] : e.componentProperties) {
            propertyCount += fields.size();
        }

        out << ' ' << e.nParent << ' ' << static_cast<unsigned>(e.renderer) << ' ' << std::quoted(e.sModelAsset) << ' '
            << std::quoted(e.sMaterialAsset) << ' ' << std::quoted(e.sTextureAsset) << ' ' << (e.bPlayAnimation ? 1 : 0) << ' '
            << e.fAnimationTime << ' ' << e.nAnimationIndex << ' ' << propertyCount;
        for (const auto& [component, fields] : e.componentProperties) {
            for (const auto& [field, value] : fields) {
                out << ' ' << std::quoted(component) << ' ' << std::quoted(field) << ' ' << std::quoted(value);
            }
        }

        out << ' ' << (d.bIs2D ? 1 : 0) << ' ' << d.position.x << ' ' << d.position.y << ' ' << d.position.z << ' '
            << d.rotation.x << ' ' << d.rotation.y << ' ' << d.rotation.z << ' ' << d.scale.x << ' ' << d.scale.y << ' '
            << d.scale.z << ' ' << d.color.x << ' ' << d.color.y << ' ' << d.color.z << ' ' << d.color.w << '\n';
    }

    // 同じフォルダーの一時ファイルに書き、成功したときだけ置き換える
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

//**********************************************************************************
// *** レンダラー / コンポーネントの登録 ***
//**********************************************************************************
//==================================================================================
// --- 関数によるコンポーネントの登録処理 ---
//==================================================================================
bool RegisterComponent(const std::string& name, ComponentUpdate update) {
    if (name.empty() || name.size() > 128 || name.find_first_of("\r\n") != std::string::npos || !update) return false;

    // 関数を Update で呼ぶだけのコンポーネントで包む
    struct CallbackComponent final : Component {
        explicit CallbackComponent(ComponentUpdate callback) : callback_(callback) {}

        void Update(Entity& entity, float dt) override { callback_(entity, dt); }

        ComponentUpdate callback_;
    };

    return Components().emplace(name, [update]() { return std::make_unique<CallbackComponent>(update); }).second;
}

//==================================================================================
// --- 各レンダラーの描画情報の作成処理 ---
//==================================================================================
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
    const auto mesh = asset_.empty() ? LoadedMesh{} : LoadModelMesh(asset_, animationTime_, animationIndex_);
    result.pVertices = mesh.pVertices;
    result.pShadeRuns = mesh.pShadeRuns;
    result.bVisible = static_cast<bool>(result.pVertices); // 読めなければ描かない
    return result;
}

DrawItem MeshRenderer::BuildDrawItem(const Entity& entity) const {
    auto result = entity.draw;
    const auto mesh = m_mesh.empty() ? LoadedMesh{} : LoadModelMesh(m_mesh, m_fAnimationTime, m_nAnimationIndex);
    result.pVertices = mesh.pVertices;
    result.pShadeRuns = mesh.pShadeRuns;
    result.bIs2D = mesh.bFlat; // 円 / 四角形 / 三角形は四角形として選択・アウトライン表示する
    result.bVisible = entity.draw.bVisible && static_cast<bool>(result.pVertices);
    return result;
}

//==================================================================================
// --- クラスによるコンポーネントの登録処理 ---
//==================================================================================
bool RegisterComponentClass(const std::string& name, ComponentFactory factory) {
    if (name.empty() || name.size() > 128 || name.find_first_of("\r\n") != std::string::npos || !factory) return false;
    return Components().emplace(name, factory).second;
}

//==================================================================================
// --- 登録済みかの判定処理 ---
//==================================================================================
bool IsComponentRegistered(const std::string& name) {
    return Components().find(name) != Components().end();
}

//==================================================================================
// --- コンポーネントの追加処理 ---
//==================================================================================
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

//==================================================================================
// --- コンポーネントの削除処理 ---
//==================================================================================
bool DetachComponent(Entity& entity, std::size_t componentIndex) {
    if (componentIndex >= entity.vComponents.size()) return false;
    const auto removed = entity.vComponents[componentIndex];
    entity.vComponents.erase(entity.vComponents.begin() + static_cast<std::ptrdiff_t>(componentIndex));
    entity.componentProperties.erase(removed); // 保存値も消し、実行中のインスタンスは作り直させる
    entity.vRuntimeComponentNames.clear();
    entity.vRuntimeComponents.clear();
    entity.vRuntimeComponentStarted.clear();
    EngineEvents::Get().onComponentDetached.Broadcast(entity, removed);
    return true;
}

//==================================================================================
// --- 登録済みの名前の一覧の取得処理 ---
//==================================================================================
std::vector<std::string> RegisteredComponentNames() {
    std::vector<std::string> result;
    result.reserve(Components().size());
    for (const auto& component : Components()) {
        result.push_back(component.first);
    }

    std::sort(result.begin(), result.end());
    return result;
}

namespace {
void ApplySavedProperties(Component& component, const Entity::PropertyMap& saved); // インスペクターの値（下で定義）

//**********************************************************************************
// *** コンポーネントの実行 ***
//**********************************************************************************
// --- 付けているコンポーネントが変わったら、実行中のインスタンスを作り直す ---
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

// --- 保存されたインスペクターの値を反映し、Start() をちょうど 1 回呼ぶ ---
void StartComponentIfNeeded(Entity& entity, std::size_t nIndex) {
    if (nIndex >= entity.vRuntimeComponentStarted.size()) {
        entity.vRuntimeComponentStarted.resize(nIndex + 1, false);
    }

    if (entity.vRuntimeComponentStarted[nIndex]) return;
    auto& component = *entity.vRuntimeComponents[nIndex];
    const auto saved = entity.componentProperties.find(entity.vRuntimeComponentNames[nIndex]);
    if (saved != entity.componentProperties.end()) {
        const auto copy = saved->second; // 反映中に保存値が書き換わるのでコピーを使う
        ApplySavedProperties(component, copy);
    }

    component.Start(entity);
    entity.vRuntimeComponentStarted[nIndex] = true;
    EngineEvents::Get().onComponentStarted.Broadcast(entity, component, entity.vRuntimeComponentNames[nIndex]);
}
} // namespace

//==================================================================================
// --- 全コンポーネントの 1 フレーム分の更新処理 ---
//==================================================================================
void UpdateUserComponents(Scene& scene, float deltaSeconds) {
    const float fDelta = std::max(deltaSeconds, 0.0f);
    Time::Advance(fDelta); // Time の経過時間を進める
    auto& events = EngineEvents::Get();

    // 最初に入力を更新する（このフレームのすべての Update が同じ状態を見る）
    auto* pInput = CInputSystem::GetInstance();
    pInput->SetAssetRoot(scene.projectRoot);
    pInput->Update();
    events.onPreUpdate.Broadcast(scene, fDelta);

    // 1 周目: Start（初回のみ）と Update を、インスペクターの順に呼ぶ
    for (auto& entity : scene.vEntities) {
        if ((entity.renderer == Entity::RendererKind::Model || entity.renderer == Entity::RendererKind::Mesh) &&
            entity.bPlayAnimation) {
            entity.fAnimationTime += fDelta; // アニメーションを進める
        }

        SyncRuntimeComponents(entity);
        const size_t nCount = std::min(entity.vRuntimeComponentNames.size(), entity.vRuntimeComponents.size());
        for (size_t nIndex = 0; nIndex < nCount; ++nIndex) {
            if (!entity.vRuntimeComponents[nIndex]) continue;
            StartComponentIfNeeded(entity, nIndex);
            entity.vRuntimeComponents[nIndex]->Update(entity, fDelta);
        }
    }

    // 物理: タイムスケールを掛けた時間を固定ステップで進める（FixedUpdate、接触、イベント）
    SimulatePhysics(scene, fDelta * Time::TimeScale());

    // 2 周目: すべての Update の後にコルーチンを再開し（Unity と同じ順序）、
    // 次に LateUpdate、最後に実行中の値をインスペクター用の保存値へ写す
    for (auto& entity : scene.vEntities) {
        const size_t nCount = std::min(entity.vRuntimeComponentNames.size(), entity.vRuntimeComponents.size());
        for (size_t nIndex = 0; nIndex < nCount; ++nIndex) {
            if (entity.vRuntimeComponents[nIndex]) {
                entity.vRuntimeComponents[nIndex]->Coroutines().Tick(fDelta * Time::TimeScale(), fDelta);
            }
        }

        for (size_t nIndex = 0; nIndex < nCount; ++nIndex) {
            if (entity.vRuntimeComponents[nIndex]) {
                entity.vRuntimeComponents[nIndex]->LateUpdate(entity, fDelta);
            }
        }

        for (size_t nIndex = 0; nIndex < nCount; ++nIndex) {
            if (entity.vRuntimeComponents[nIndex]) {
                ComponentEditorValues(entity, nIndex);
            }
        }
    }

    events.onPostUpdate.Broadcast(scene, fDelta);
}

namespace {
// --- インスタンスが無ければ作る（エディターで値を表示・編集するため） ---
void EnsureComponentInstances(Entity& entity) {
    if (entity.vRuntimeComponentNames == entity.vComponents && entity.vRuntimeComponents.size() == entity.vComponents.size()) {
        return;
    }

    entity.vRuntimeComponentNames = entity.vComponents;
    entity.vRuntimeComponents.clear();
    entity.vRuntimeComponentStarted.clear();
    for (const auto& name : entity.vComponents) {
        auto found = Components().find(name);
        entity.vRuntimeComponents.push_back(found == Components().end() ? nullptr : found->second());
        entity.vRuntimeComponentStarted.push_back(false);
    }
}
} // namespace

//**********************************************************************************
// *** インスペクターの値の読み書き ***
//**********************************************************************************
namespace {
constexpr std::size_t EDITOR_ARRAY_MAX_SIZE = 4096; // インスペクターで設定できる配列の最大サイズ

// --- float を元の値に戻せる桁数で文字列にする処理 ---
std::string FloatText(float fValue) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10) << fValue;
    return out.str();
}

// --- 前後の空白の除去処理 ---
std::string Trimmed(const std::string& sText) {
    const auto nBegin = sText.find_first_not_of(" \t");
    if (nBegin == std::string::npos) return {};
    return sText.substr(nBegin, sText.find_last_not_of(" \t") - nBegin + 1);
}

// --- 文字列全体を float として読む処理（余計な文字や無限大は失敗） ---
bool ParseFloat(const std::string& sText, float& fValue) {
    try {
        size_t nUsed = 0;
        const std::string sTrimmed = Trimmed(sText);
        fValue = std::stof(sTrimmed, &nUsed);
        return nUsed == sTrimmed.size() && std::isfinite(fValue);
    } catch (...) {
        return false;
    }
}

// --- "1, 2, 3" / "1 2 3" をベクトルの各成分に読む処理 ---
bool ParseVector(const std::string& sText, float* pValues, int nCount) {
    std::string sNormalized = sText;
    std::replace(sNormalized.begin(), sNormalized.end(), ',', ' ');
    std::istringstream in(sNormalized);
    in.imbue(std::locale::classic());
    std::vector<std::string> vParts;
    for (std::string sPart; in >> sPart;) {
        vParts.push_back(sPart);
    }

    if (static_cast<int>(vParts.size()) != nCount) return false;
    for (int nIndex = 0; nIndex < nCount; ++nIndex) {
        if (!ParseFloat(vParts[static_cast<size_t>(nIndex)], pValues[nIndex])) return false;
    }

    return true;
}

// --- 列挙型の読み書き処理（列挙型のバイト数に合わせる） ---
long long ReadEnum(const EditorField& field) {
    switch (field.nEnumBytes) {
    case 1: return *static_cast<const signed char*>(field.pValue);
    case 2: return *static_cast<const short*>(field.pValue);
    case 8: return *static_cast<const long long*>(field.pValue);
    default: return *static_cast<const int*>(field.pValue);
    }
}

void WriteEnum(EditorField& field, long long nValue) {
    switch (field.nEnumBytes) {
    case 1: *static_cast<signed char*>(field.pValue) = static_cast<signed char>(nValue); break;
    case 2: *static_cast<short*>(field.pValue) = static_cast<short>(nValue); break;
    case 8: *static_cast<long long*>(field.pValue) = nValue; break;
    default: *static_cast<int*>(field.pValue) = static_cast<int>(nValue); break;
    }
}

// 配列を行に展開した 1 行分（"Points"、"Points.Size"、"Points[0]"、...）
struct FlatField {
    EditorFieldInfo info;                      // 表示用の情報
    EditorField field;                         // 通常の行の値（pValue が有効）
    std::shared_ptr<EditorArrayAccess> pArray; // 所属する配列
    std::size_t nElement = 0;                  // 要素の番号
    // 行の種類: 通常 / 配列の見出し / 配列のサイズ / 配列の要素
    enum class KIND : unsigned char { LEAF, HEADER, SIZE, ELEMENT } kind = KIND::LEAF;
};

// --- フィールドから表示用の情報を作る処理 ---
EditorFieldInfo LeafInfo(const EditorField& field, std::string sPath, std::string sLabel) {
    EditorFieldInfo info;
    info.sPath = std::move(sPath);
    info.sLabel = std::move(sLabel);
    info.type = field.type;
    info.bReadOnly = field.bReadOnly;
    info.vEnumNames = field.vEnumNames;
    info.bRange = field.bRange;
    info.fMin = field.fMin;
    info.fMax = field.fMax;
    return info;
}

//==================================================================================
// --- フィールドの一覧を行に展開する処理 ---
//==================================================================================
std::vector<FlatField> Flatten(std::vector<EditorField>& vFields) {
    std::vector<FlatField> vFlat;
    for (auto& field : vFields) {
        if (field.type != EditorFieldType::Array || !field.pArray) { // 配列以外はそのまま 1 行
            FlatField flat;
            flat.info = LeafInfo(field, field.sName, field.sName);
            flat.field = field;
            vFlat.push_back(std::move(flat));
            continue;
        }

        const auto& pArray = field.pArray;

        // 配列: 見出し → サイズ → 要素の順に並べる
        FlatField header;
        header.kind = FlatField::KIND::HEADER;
        header.info.sPath = header.info.sLabel = field.sName;
        header.info.type = EditorFieldType::Array;
        header.info.bArrayHeader = true;
        header.pArray = pArray;
        vFlat.push_back(header);
        FlatField size;
        size.kind = FlatField::KIND::SIZE;
        size.info.sPath = field.sName + ".Size";
        size.info.sLabel = "Size";
        size.info.type = EditorFieldType::Integer;
        size.info.bReadOnly = field.bReadOnly || pArray->bFixedSize || !pArray->resize;
        size.info.nIndent = 1;
        size.info.bArraySize = true;
        size.info.sArrayPath = field.sName;
        size.pArray = pArray;
        vFlat.push_back(size);
        const std::size_t nCount = pArray->size ? pArray->size() : 0;
        for (std::size_t nElement = 0; nElement < nCount; ++nElement) {
            FlatField element;
            element.kind = FlatField::KIND::ELEMENT;
            element.info = LeafInfo(pArray->element, field.sName + "[" + std::to_string(nElement) + "]",
                                    "Element " + std::to_string(nElement));
            element.info.bReadOnly = field.bReadOnly || pArray->element.bReadOnly;
            element.info.nIndent = 1;
            element.info.sArrayPath = field.sName;
            element.pArray = pArray;
            element.nElement = nElement;
            vFlat.push_back(std::move(element));
        }
    }

    return vFlat;
}

// --- 1 行の値を文字列で取得する処理 ---
std::string FlatText(const FlatField& flat) {
    switch (flat.kind) {
    case FlatField::KIND::HEADER: return {};
    case FlatField::KIND::SIZE: return std::to_string(flat.pArray->size ? flat.pArray->size() : 0);
    case FlatField::KIND::ELEMENT: return flat.pArray->get ? flat.pArray->get(flat.nElement) : std::string{};
    default: return EditorFieldText(flat.field);
    }
}

// --- 1 行に文字列の値を設定する処理 ---
bool FlatAssign(FlatField& flat, const std::string& sValue, std::string& error) {
    switch (flat.kind) {
    case FlatField::KIND::HEADER: error = "An array header has no value."; return false;
    case FlatField::KIND::SIZE: { // サイズ: 0..上限の整数のみ
        if (flat.info.bReadOnly) {
            error = "The size of a fixed array cannot change.";
            return false;
        }

        float fSize = 0;
        if (!ParseFloat(sValue, fSize) || fSize < 0 || fSize != std::floor(fSize) || fSize > EDITOR_ARRAY_MAX_SIZE) {
            error = "Size must be a whole number from 0 to " + std::to_string(EDITOR_ARRAY_MAX_SIZE) + ".";
            return false;
        }

        flat.pArray->resize(static_cast<std::size_t>(fSize));
        return true;
    }
    case FlatField::KIND::ELEMENT:
        if (flat.info.bReadOnly) {
            error = "The selected field is read-only.";
            return false;
        }

        return flat.pArray->set && flat.pArray->set(flat.nElement, sValue, error);
    default: return AssignEditorField(flat.field, sValue, error);
    }
}

// --- 保存値の反映処理: 先に配列のサイズ（要素が作られる）、次に残りの値 ---
void ApplySavedProperties(Component& component, const Entity::PropertyMap& saved) {
    auto vFields = component.EditorFields();
    for (auto& flat : Flatten(vFields)) {
        if (flat.kind != FlatField::KIND::SIZE || flat.info.bReadOnly) continue;
        const auto found = saved.find(flat.info.sPath);
        std::string ignored;
        if (found != saved.end()) {
            FlatAssign(flat, found->second, ignored);
        }
    }

    for (auto& flat : Flatten(vFields)) {
        if (flat.kind == FlatField::KIND::SIZE || flat.kind == FlatField::KIND::HEADER) continue;
        const auto found = saved.find(flat.info.sPath);
        std::string ignored;
        if (found != saved.end()) {
            FlatAssign(flat, found->second, ignored);
        }
    }
}

// --- 現在の値で保存値を書き直す処理（サイズ変更で消えた要素の値も消える） ---
void StoreProperties(std::vector<FlatField>& vFlat, Entity::PropertyMap& saved) {
    saved.clear();
    for (auto& flat : vFlat) {
        if (flat.kind == FlatField::KIND::HEADER) continue;
        flat.info.sValue = FlatText(flat);
        saved[flat.info.sPath] = flat.info.sValue;
    }
}
} // namespace

//==================================================================================
// --- 単一の値の文字列への変換処理 ---
//==================================================================================
std::string EditorFieldText(const EditorField& field) {
    if (!field.pValue) return {};
    switch (field.type) {
    case EditorFieldType::Float: return FloatText(*static_cast<const float*>(field.pValue));
    case EditorFieldType::Integer: return std::to_string(*static_cast<const int*>(field.pValue));
    case EditorFieldType::Boolean: return *static_cast<const bool*>(field.pValue) ? "true" : "false";
    case EditorFieldType::String: return *static_cast<const std::string*>(field.pValue);
    case EditorFieldType::Enum: {
        const long long nValue = ReadEnum(field);

        // 名前があれば名前、無ければ数値
        return nValue >= 0 && static_cast<size_t>(nValue) < field.vEnumNames.size()
                   ? field.vEnumNames[static_cast<size_t>(nValue)]
                   : std::to_string(nValue);
    }
    case EditorFieldType::Vector2: {
        const auto& value = *static_cast<const DirectX::XMFLOAT2*>(field.pValue);
        return FloatText(value.x) + "," + FloatText(value.y);
    }
    case EditorFieldType::Vector3: {
        const auto& value = *static_cast<const DirectX::XMFLOAT3*>(field.pValue);
        return FloatText(value.x) + "," + FloatText(value.y) + "," + FloatText(value.z);
    }
    default: return {};
    }
}

//==================================================================================
// --- 文字列からの単一の値の設定処理 ---
//==================================================================================
bool AssignEditorField(EditorField& field, const std::string& value, std::string& error) {
    if (field.bReadOnly) {
        error = "The selected field is read-only.";
        return false;
    }

    if (!field.pValue) {
        error = "The field has no storage.";
        return false;
    }

    try {
        size_t used = 0;
        if (field.type == EditorFieldType::Float) {
            float parsed = 0;
            if (!ParseFloat(value, parsed)) {
                throw std::invalid_argument("float");
            }

            if (field.bRange) {
                parsed = std::clamp(parsed, field.fMin, field.fMax); // スライダーの範囲に収める
            }

            *static_cast<float*>(field.pValue) = parsed;
        } else if (field.type == EditorFieldType::Integer) {
            int parsed = std::stoi(value, &used);
            if (used != value.size()) {
                throw std::invalid_argument("int");
            }

            if (field.bRange) {
                parsed = std::clamp(parsed, static_cast<int>(std::ceil(field.fMin)), static_cast<int>(std::floor(field.fMax)));
            }

            *static_cast<int*>(field.pValue) = parsed;
        } else if (field.type == EditorFieldType::Boolean) {
            if (value == "true" || value == "1") {
                *static_cast<bool*>(field.pValue) = true;
            } else if (value == "false" || value == "0") {
                *static_cast<bool*>(field.pValue) = false;
            } else {
                throw std::invalid_argument("bool");
            }
        } else if (field.type == EditorFieldType::String) {
            *static_cast<std::string*>(field.pValue) = value;
        } else if (field.type == EditorFieldType::Enum) {
            // 値の名前（大文字小文字は区別しない）または数値
            const std::string sValue = Trimmed(value);
            long long nValue = -1;
            for (size_t nIndex = 0; nIndex < field.vEnumNames.size(); ++nIndex) {
                if (_stricmp(field.vEnumNames[nIndex].c_str(), sValue.c_str()) == 0) {
                    nValue = static_cast<long long>(nIndex);
                }
            }

            if (nValue < 0) {
                nValue = std::stoll(sValue, &used);
                if (used != sValue.size()) {
                    throw std::invalid_argument("enum");
                }
            }

            WriteEnum(field, nValue);
        } else if (field.type == EditorFieldType::Vector2 || field.type == EditorFieldType::Vector3) {
            float aValues[3]{};
            const int nCount = field.type == EditorFieldType::Vector2 ? 2 : 3;
            if (!ParseVector(value, aValues, nCount)) {
                throw std::invalid_argument("vector");
            }

            if (nCount == 2) {
                *static_cast<DirectX::XMFLOAT2*>(field.pValue) = {aValues[0], aValues[1]};
            } else {
                *static_cast<DirectX::XMFLOAT3*>(field.pValue) = {aValues[0], aValues[1], aValues[2]};
            }
        } else {
            throw std::invalid_argument("type");
        }
    } catch (...) {
        error = "The value does not match the component field type.";
        return false;
    }

    return true;
}

//==================================================================================
// --- インスペクターの全行の取得処理 ---
//==================================================================================
std::vector<EditorFieldInfo> ComponentFieldInfos(Entity& entity, std::size_t componentIndex) {
    EnsureComponentInstances(entity);
    std::vector<EditorFieldInfo> result;
    if (componentIndex >= entity.vRuntimeComponents.size() || !entity.vRuntimeComponents[componentIndex]) return result;
    auto& component = *entity.vRuntimeComponents[componentIndex];
    auto& saved = entity.componentProperties[entity.vRuntimeComponentNames[componentIndex]];

    // Play モード外では保存値が正、Play 中は実行中の値が正
    if (!entity.vRuntimeComponentStarted[componentIndex]) {
        const auto copy = saved;
        ApplySavedProperties(component, copy);
    }

    auto vFields = component.EditorFields();
    auto vFlat = Flatten(vFields);
    StoreProperties(vFlat, saved);
    result.reserve(vFlat.size());
    for (auto& flat : vFlat) {
        result.push_back(std::move(flat.info));
    }

    return result;
}

//==================================================================================
// --- 変数名と値の一覧の取得処理（見出し行を除く） ---
//==================================================================================
std::vector<std::pair<std::string, std::string>> ComponentEditorValues(Entity& entity, std::size_t componentIndex) {
    std::vector<std::pair<std::string, std::string>> result;
    for (auto& info : ComponentFieldInfos(entity, componentIndex)) {
        if (!info.bArrayHeader) {
            result.emplace_back(std::move(info.sPath), std::move(info.sValue));
        }
    }

    return result;
}

//==================================================================================
// --- インスペクターからの値の設定処理 ---
//==================================================================================
bool SetComponentEditorValue(Entity& entity, std::size_t componentIndex, const std::string& name, const std::string& value,
                             std::string& error) {
    error.clear();
    EnsureComponentInstances(entity);
    if (componentIndex >= entity.vRuntimeComponents.size() || !entity.vRuntimeComponents[componentIndex]) {
        error = "The component class is not registered.";
        return false;
    }

    auto& component = *entity.vRuntimeComponents[componentIndex];
    auto& saved = entity.componentProperties[entity.vRuntimeComponentNames[componentIndex]];
    if (!entity.vRuntimeComponentStarted[componentIndex]) {
        const auto copy = saved;
        ApplySavedProperties(component, copy);
    }

    auto vFields = component.EditorFields();
    auto vFlat = Flatten(vFields);
    for (auto& flat : vFlat) {
        if (flat.info.sPath == name) {
            if (!FlatAssign(flat, value, error)) return false;
            auto vAfter = Flatten(vFields); // サイズを変えると要素が増減するので展開し直す
            StoreProperties(vAfter, saved);
            return true;
        }
    }

    error = "The component field no longer exists.";
    return false;
}

//**********************************************************************************
// *** ゲームのカメラ ***
//**********************************************************************************
//==================================================================================
// --- ゲームの視点になるカメラの取得処理 ---
//==================================================================================
// Camera コンポーネントを持ち、Active な最初のエンティティの位置・向きを使う。
// 無ければシーンのカメラ（エディターの視点）を返す。
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

        // 向いている方向の前方に注視点を置く
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
            // 数値の保存値（読めなければ既定値）
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

//**********************************************************************************
// *** 親子関係とトランスフォーム ***
//**********************************************************************************
//==================================================================================
// --- 全エンティティのワールド空間の描画情報の計算処理 ---
//==================================================================================
std::vector<DrawItem> ResolveWorldDraws(const Scene& scene) {
    using namespace DirectX;
    std::vector<DrawItem> result(scene.vEntities.size());
    std::vector<unsigned char> state(scene.vEntities.size(), 0); // 0 = 未計算 / 1 = 計算中 / 2 = 計算済み
    // 親を先に計算する再帰関数
    std::function<void(int)> resolve = [&](int index) {
        if (index < 0 || static_cast<size_t>(index) >= scene.vEntities.size() || state[index] == 2) return;
        if (state[index] == 1) { // 循環している場合は親を無視する
            result[index] = scene.vEntities[index].draw;
            state[index] = 2;
            return;
        }

        state[index] = 1;
        const auto& entity = scene.vEntities[index];

        // レンダラーの種類ごとに描画情報を作る
        auto world = entity.draw;
        if (entity.renderer == Entity::RendererKind::None) {
            world.bVisible = false;
        } else if (entity.renderer == Entity::RendererKind::Polygon) {
            world = PolygonRenderer{}.BuildDrawItem(entity);
        } else if (entity.renderer == Entity::RendererKind::Mesh) {
            world = MeshRenderer(scene.projectRoot / std::filesystem::u8path(entity.sModelAsset), entity.fAnimationTime,
                                 entity.nAnimationIndex)
                        .BuildDrawItem(entity);
        } else if (entity.renderer == Entity::RendererKind::Model) {
            world = ModelRenderer(scene.projectRoot / std::filesystem::u8path(entity.sModelAsset), entity.fAnimationTime,
                                  entity.nAnimationIndex)
                        .BuildDrawItem(entity);
        } else {
            world = Image{}.BuildDrawItem(entity);
        }

        // マテリアルの基本色を掛ける
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

        // 親のトランスフォームを適用する（拡大 → 回転 → 移動）
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
    for (int i = 0; i < static_cast<int>(scene.vEntities.size()); ++i) {
        resolve(i);
    }

    return result;
}

//==================================================================================
// --- 1 つ分のワールド空間の描画情報の取得処理 ---
//==================================================================================
DrawItem WorldDraw(const Scene& scene, int index) {
    const auto all = ResolveWorldDraws(scene);
    return index >= 0 && static_cast<size_t>(index) < all.size() ? all[index] : DrawItem{};
}

//==================================================================================
// --- ワールド → 親からの相対への変換処理（ResolveWorldDraws の逆） ---
//==================================================================================
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

//==================================================================================
// --- 親の設定処理（ワールドでの見た目は変えない） ---
//==================================================================================
bool SetEntityParent(Scene& scene, int child, int parent, std::string& error) {
    error.clear();
    if (child < 0 || static_cast<size_t>(child) >= scene.vEntities.size() || parent < -1 ||
        parent >= static_cast<int>(scene.vEntities.size()) || child == parent) {
        error = "Invalid parent selection.";
        return false;
    }

    // 新しい親が自分の子孫なら循環するので失敗
    for (int p = parent, steps = 0; p >= 0 && steps <= static_cast<int>(scene.vEntities.size()); ++steps) {
        if (p == child) {
            error = "An object cannot be parented to one of its descendants.";
            return false;
        }

        p = scene.vEntities[p].nParent;
    }

    // ワールドでの位置を保ったまま親を変える
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

//**********************************************************************************
// *** プロジェクトの設定 ***
//**********************************************************************************
//==================================================================================
// --- プロジェクトの設定の読み込み処理 ---
//==================================================================================
// 書式: DXSETTINGS 1 の後に「キー "値"」が並ぶ
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

        // 知らないキーは無視する（新しい版の設定ファイルも読めるように）
    }

    settings = loaded;
    return true;
}

//==================================================================================
// --- プロジェクトの設定の保存処理 ---
//==================================================================================
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
//**********************************************************************************
// *** ゲームの書き出し ***
//**********************************************************************************
// --- 書き出しの追加ファイルの処理 ---
// Direct3D 9 用のシェーダーと、プロジェクトの設定（使用するグラフィックス API）。
// ここで失敗しても書き出しは失敗にしない（プレイヤーは組み込みの SM3 シェーダーと
// Direct3D 12 を代わりに使うため）。
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

    // プロジェクトの設定をコピーする
    std::error_code ec;
    const auto settings = projectDir / PROJECT_SETTINGS_FILE_NAME;
    if (std::filesystem::is_regular_file(settings, ec)) {
        std::filesystem::copy_file(settings, dest / PROJECT_SETTINGS_FILE_NAME, std::filesystem::copy_options::overwrite_existing,
                                   ec);
        if (ec) {
            Debug::LogWarning("Export: could not copy project settings: " + ec.message());
        }
    }
}
} // namespace

//==================================================================================
// --- ゲームの書き出し処理（プロジェクトは baseDir/project） ---
//==================================================================================
bool ExportGame(const std::filesystem::path& baseDir, const std::filesystem::path& dest, const Scene& scene, std::string& error) {
    return ExportGame(baseDir, baseDir / L"project", dest, scene, error);
}

//==================================================================================
// --- ゲームの書き出し処理 ---
//==================================================================================
bool ExportGame(const std::filesystem::path& baseDir, const std::filesystem::path& projectDir, const std::filesystem::path& dest,
                const Scene& scene, std::string& error) {
    error.clear();
    std::error_code ec;

    // パスを正規化する
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

    // child が parent の中（または同じ）か
    auto inside = [](const std::filesystem::path& parent, const std::filesystem::path& child) {
        auto a = parent.begin(), b = child.begin();
        for (; a != parent.end(); ++a, ++b) {
            if (b == child.end() || _wcsicmp(a->c_str(), b->c_str()) != 0) return false;
        }

        return true;
    };

    // アプリのフォルダーそのものやプロジェクトの中には書き出さない
    if ((inside(root, target) && inside(target, root)) || inside(project, target)) {
        error = "Choose an export directory outside the application root and project folder.";
        return false;
    }

    // 必要なファイルとシーンの検証
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

    // 書き出し先のファイルを作る / 上書きする前にコンパイルする。インクルードも
    // ここで解決するため、プレイヤーには HLSL のソースが要らない。
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

    // 書き出すファイル。既にある場合は通常のファイルであること
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

    // 既存のファイルを置き換える前に、4 つのファイルを別のフォルダーに用意する。
    // 専用のフォルダーにすることで、関係のない .tmp ファイルを壊さない。
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

    // 準備用のフォルダーの後片付け
    auto cleanup = [&]() {
        std::error_code ignored;
        for (size_t i = 0; i < names.size(); ++i) {
            std::filesystem::remove(stage / names[i], ignored);
            std::filesystem::remove(stage / (L"backup-" + std::to_wstring(i)), ignored);
        }

        std::filesystem::remove(stage / L"scene.dxs.tmp", ignored);
        std::filesystem::remove(stage, ignored);
    };

    // シェーダーのバイトコードの書き込み
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

    // 準備用のフォルダーに 4 つのファイルを用意する
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

    // 既存のファイルをバックアップする
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

    // 置き換える。途中で失敗したら、置き換え済みのファイルをバックアップから戻す
    for (size_t i = 0; i < names.size(); ++i) {
        if (!MoveFileExW((stage / names[i]).c_str(), (dest / names[i]).c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            error =
                "Cannot replace export file (Windows error " + std::to_string(GetLastError()) + "): " + PathText(dest / names[i]);
            bool rollbackFailed = false;
            for (size_t j = 0; j < i; ++j) {
                if (hadOriginal[j]) {
                    std::filesystem::copy_file(stage / (L"backup-" + std::to_wstring(j)), dest / names[j],
                                               std::filesystem::copy_options::overwrite_existing, ec);
                } else {
                    std::filesystem::remove(dest / names[j], ec);
                }

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

    // プロジェクトの Assets をコピーする
    const auto projectAssets = projectDir / L"Assets";
    if (std::filesystem::is_directory(projectAssets, ec)) {
        ec.clear();
        std::filesystem::create_directories(dest / L"Assets", ec);
        if (!ec) {
            std::filesystem::copy(projectAssets, dest / L"Assets",
                                  std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                                  ec);
        }

        if (ec) {
            error = "The game was exported, but project Assets could not be copied: " + ec.message();
            cleanup();
            return false;
        }
    }

    cleanup();
    ExportOptionalFiles(projectDir, dest); // 追加ファイル（失敗しても続行）
    Debug::Log("Exported game to " + PathText(dest));
    return true;
}
} // namespace dxs
