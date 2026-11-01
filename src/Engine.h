//==================================================================================
//
// エンジン本体（シーン・エンティティ・コンポーネント）のヘッダーファイル [Engine.h]
//
// エンジンの公開 API をまとめたヘッダー。ゲームのスクリプトはこれ 1 つを
// インクルードすれば、コンポーネント・シーン・物理・入力をすべて使える。
//
//   ・Entity    : シーン内の物体 1 つ（トランスフォーム + 描画 + コンポーネント）
//   ・Component : ユーザーが作る振る舞い（Unity の MonoBehaviour 相当）
//   ・Scene     : エンティティの一覧とカメラ
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
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

//**********************************************************************************
// *** マテリアル ***
//**********************************************************************************
// モデルの面が使うマテリアル 1 つ分（スロット）。モデルファイルの面ごとのマテリアル番号の順に並ぶ。
struct MaterialSlot {
    std::string sName;                   // 名前（無ければ空）
    DirectX::XMFLOAT4 color{1, 1, 1, 1}; // 色（拡散色）
    std::string sTexture;                // テクスチャ（プロジェクトからの相対パス。まだ描画には使わない）
};

struct Material {
    std::string sName = "Material";            // 名前
    DirectX::XMFLOAT4 baseColor{1, 1, 1, 1};   // 基本色（全体に掛ける色）
    std::string sTextureAsset;                 // テクスチャのアセット
    std::string sShaderAsset = "Default.hlsl"; // シェーダーのアセット
    // 面ごとの色（X / FBX のマテリアルをまとめた物）。空なら全体を baseColor の 1 色で描く。
    std::vector<MaterialSlot> vSlots;
};

// マテリアルファイル（.dxmat）の読み込み / 保存
//   バージョン 1: DXMATERIAL 1 "名前" R G B A "テクスチャ" "シェーダー"
//   バージョン 2: バージョン 1 の後に スロット数 ("名前" R G B A "テクスチャ")...（vSlots があるとき）
bool LoadMaterial(const std::filesystem::path& path, Material& material, std::string& error);
bool SaveMaterial(const std::filesystem::path& path, const Material& material, std::string& error);

// モデルファイル（.x / .fbx）が持つマテリアルを、面ごとのマテリアル番号の順に取得する
// （MeshRenderer の描画と同じ順）。マテリアルが無いモデルは空で true。
bool LoadModelMaterials(const std::filesystem::path& file, std::vector<MaterialSlot>& vSlots);

//**********************************************************************************
// *** 描画コンポーネント ***
//**********************************************************************************
// 描画は差し替え可能なコンポーネントの約束事（インターフェース）として扱う。
// 振る舞いのコンポーネントは DirectX 12 のバックエンドに依存せず、
// 描画しないエンティティ（レンダラーなし）も意図的に作れる。
class IRender {
public:
    virtual ~IRender() = default;

    // エンティティから、レンダラーに渡す描画情報を作る
    [[nodiscard]] virtual DrawItem BuildDrawItem(const Entity& entity) const = 0;
};

// 組み込みの立方体 / 四角形を描くレンダラー
class PolygonRenderer final : public IRender {
public:
    [[nodiscard]] DrawItem BuildDrawItem(const Entity& entity) const override;
};

// モデルファイル（X / FBX）を描くレンダラー
class ModelRenderer final : public IRender {
public:
    explicit ModelRenderer(std::filesystem::path asset = {}, float animationTime = 0.0f, int animationIndex = 0)
        : asset_(std::move(asset)), animationTime_(animationTime), animationIndex_(animationIndex) {}

    [[nodiscard]] DrawItem BuildDrawItem(const Entity& entity) const override;

    [[nodiscard]] const std::filesystem::path& Asset() const noexcept { return asset_; }

private:
    std::filesystem::path asset_; // モデルのアセット
    float animationTime_{};       // アニメーションの再生時間（秒）
    int animationIndex_{};        // 再生するアニメーションの番号
};

// Unity の MeshRenderer: Mesh 欄に設定したメッシュアセット（組み込みの基本図形 /
// X / FBX）を描く。XY 平面上の平らなメッシュは 2D として選択・アウトライン表示される。
class MeshRenderer final : public IRender {
public:
    explicit MeshRenderer(std::filesystem::path mesh = {}, float fAnimationTime = 0.0f, int nAnimationIndex = 0)
        : m_mesh(std::move(mesh)), m_fAnimationTime(fAnimationTime), m_nAnimationIndex(nAnimationIndex) {}

    [[nodiscard]] DrawItem BuildDrawItem(const Entity& entity) const override;

    [[nodiscard]] const std::filesystem::path& Mesh() const noexcept { return m_mesh; }

private:
    std::filesystem::path m_mesh;  // メッシュのアセット
    float m_fAnimationTime = 0.0f; // アニメーションの再生時間（秒）
    int m_nAnimationIndex = 0;     // 再生するアニメーションの番号
};

// 2D の画像（XY 平面の四角形）を描くレンダラー
class Image final : public IRender {
public:
    [[nodiscard]] DrawItem BuildDrawItem(const Entity& entity) const override;
};

// IRender を継承した型だけを描画コンポーネントとして認める（コンパイル時の確認）
template<class T>
concept RenderComponent = std::derived_from<T, IRender>;
template<class T>
inline constexpr bool IsRenderComponent = RenderComponent<T>;
static_assert(IsRenderComponent<PolygonRenderer> && IsRenderComponent<ModelRenderer> && IsRenderComponent<MeshRenderer> &&
              IsRenderComponent<Image>);

//**********************************************************************************
// *** インスペクターに表示する変数（フィールド） ***
//**********************************************************************************
//   EditField("Speed", m_fSpeed)                    float / int / bool / std::string
//   EditField("Offset", m_offset)                   DirectX::XMFLOAT2 / XMFLOAT3（X Y Z を 1 行に表示）
//   EditField("Points", m_vPoints)                  std::vector<T>   （Size を変更できる）
//   EditField("Slots", m_aSlots)                    std::array<T, N> （Size は固定）
//   EditEnum("Mode", m_mode, {"Idle", "Walk"})      任意の列挙型をドロップダウンで表示
//   EditRange("Volume", m_fVolume, 0.0f, 1.0f)      float / int をスライダーで調整（Unity の [Range]）
// 配列の要素には上の単一値の型（列挙型は EditEnum）を使える。
enum class EditorFieldType : unsigned char { Float, Integer, Boolean, String, Enum, Vector2, Vector3, Array };

struct EditorArrayAccess;

// インスペクターに表示する変数 1 つ分の情報
struct EditorField {
    std::string sName;      // 表示名
    EditorFieldType type{}; // 値の種類
    void* pValue{};         // 値へのポインタ
    bool bReadOnly = false; // 読み取り専用か
    // 列挙型: 値 0..n-1 の表示名。pValue は nEnumBytes バイトの列挙型を指す。
    std::vector<std::string> vEnumNames;
    unsigned char nEnumBytes = 0;

    // Float / Integer のスライダーの範囲（値はこの範囲に収められる）
    bool bRange = false;
    float fMin = 0.0f, fMax = 1.0f;

    // 配列: 要素の情報と読み書きの方法
    std::shared_ptr<EditorArrayAccess> pArray;
};

// 単一の値の文字列表現（"1.5"、"true"、"Walk"、"1,2,3"）と、その読み取り
std::string EditorFieldText(const EditorField& field);
bool AssignEditorField(EditorField& field, const std::string& sValue, std::string& error);

// 配列フィールドの読み書きの方法（型に依存しない形で保持する）
struct EditorArrayAccess {
    EditorField element;                         // 全要素に共通する種類 / 列挙名 / 範囲（pValue は使わない）
    bool bFixedSize = false;                     // std::array: Size は読み取り専用
    std::function<std::size_t()> size;           // 要素数の取得
    std::function<void(std::size_t)> resize;     // 要素数の変更（動的配列のみ）
    std::function<std::string(std::size_t)> get; // 要素の値の取得（文字列）
    std::function<bool(std::size_t, const std::string&, std::string&)> set; // 要素の値の設定（文字列）
};

namespace detail {
// 単一の値として扱える型か
template<class T>
inline constexpr bool IS_EDITOR_LEAF =
    std::same_as<T, float> || std::same_as<T, int> || std::same_as<T, bool> || std::same_as<T, std::string> ||
    std::same_as<T, DirectX::XMFLOAT2> || std::same_as<T, DirectX::XMFLOAT3>;

// std::vector / std::array かを判定する型（要素の型も取り出す）
template<class T>
struct StdVector : std::false_type {};

template<class U, class A>
struct StdVector<std::vector<U, A>> : std::true_type {
    using Element = U;
};

template<class T>
struct StdArray : std::false_type {};

template<class U, std::size_t N>
struct StdArray<std::array<U, N>> : std::true_type {
    using Element = U;
};

// --- 単一の値のフィールドの作成処理（型から種類を決める） ---
template<class T>
EditorField LeafField(std::string sName, T* pValue, bool bReadOnly) {
    EditorField field{std::move(sName), EditorFieldType::Float, pValue, bReadOnly};
    if constexpr (std::same_as<T, int>) {
        field.type = EditorFieldType::Integer;
    } else if constexpr (std::same_as<T, bool>) {
        field.type = EditorFieldType::Boolean;
    } else if constexpr (std::same_as<T, std::string>) {
        field.type = EditorFieldType::String;
    } else if constexpr (std::same_as<T, DirectX::XMFLOAT2>) {
        field.type = EditorFieldType::Vector2;
    } else if constexpr (std::same_as<T, DirectX::XMFLOAT3>) {
        field.type = EditorFieldType::Vector3;
    }

    return field;
}

// --- 配列のフィールドの作成処理 ---
// operator[] を持つ任意のコンテナに対応する（要素は一時変数を経由してコピー
// するため、std::vector<bool> でも動く）。
template<class Container>
EditorField ArrayField(std::string sName, Container& container, EditorField element, bool bFixed, bool bReadOnly) {
    using U = typename Container::value_type; // decltype(container[0]) は使わない: std::vector<bool> はプロキシを返すため
    EditorField field{std::move(sName), EditorFieldType::Array, &container, bReadOnly};
    auto pAccess = std::make_shared<EditorArrayAccess>();
    pAccess->element = std::move(element);
    pAccess->element.pValue = nullptr;
    pAccess->bFixedSize = bFixed;
    pAccess->size = [&container] {
        return static_cast<std::size_t>(container.size());
    };

    // resize を持つコンテナ（std::vector）だけサイズ変更できるようにする
    if constexpr (requires { container.resize(std::size_t{}); }) {
        pAccess->resize = [&container](std::size_t nSize) {
            container.resize(nSize);
        };
    }

    const EditorField elementInfo = pAccess->element;

    // 要素の取得: 一時変数にコピーし、単一の値と同じ方法で文字列にする
    pAccess->get = [&container, elementInfo](std::size_t nIndex) {
        U temp = container[nIndex];
        EditorField leaf = elementInfo;
        leaf.pValue = &temp;
        return EditorFieldText(leaf);
    };

    // 要素の設定: 一時変数で読み取りに成功したときだけ書き戻す
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

// --- 列挙型のフィールドの作成処理 ---
template<class E>
EditorField EnumField(std::string sName, E* pValue, std::vector<std::string> vNames, bool bReadOnly) {
    EditorField field{std::move(sName), EditorFieldType::Enum, pValue, bReadOnly};
    field.vEnumNames = std::move(vNames);
    field.nEnumBytes = static_cast<unsigned char>(sizeof(E));
    return field;
}
} // namespace detail

//==================================================================================
// --- 変数をインスペクターに表示する処理 ---
//==================================================================================
template<class T>
EditorField EditField(std::string sName, T& value, bool bReadOnly = false) {
    if constexpr (detail::IS_EDITOR_LEAF<T>) {
        // 単一の値
        return detail::LeafField(std::move(sName), &value, bReadOnly);
    } else if constexpr (detail::StdVector<T>::value || detail::StdArray<T>::value) {
        // 動的配列 / 静的配列
        using U = typename T::value_type;
        static_assert(detail::IS_EDITOR_LEAF<U>,
                      "Array elements must be float, int, bool, std::string, XMFLOAT2 or XMFLOAT3 (enums: EditEnum)");
        return detail::ArrayField(std::move(sName), value, detail::LeafField<U>("", nullptr, bReadOnly),
                                  detail::StdArray<T>::value, bReadOnly);
    } else {
        // 対応していない型はコンパイルエラーにする
        static_assert(!sizeof(T), "Unsupported editor field type (see the list above EditorFieldType)");
    }
}

//==================================================================================
// --- 列挙型をドロップダウンで表示する処理（vNames[i] が値 i の名前） ---
//==================================================================================
template<class E>
    requires std::is_enum_v<E>
EditorField EditEnum(std::string sName, E& value, std::vector<std::string> vNames, bool bReadOnly = false) {
    return detail::EnumField(std::move(sName), &value, std::move(vNames), bReadOnly);
}

// 列挙型の動的配列
template<class E, class A>
    requires std::is_enum_v<E>
EditorField EditEnum(std::string sName, std::vector<E, A>& vValues, std::vector<std::string> vNames, bool bReadOnly = false) {
    return detail::ArrayField(std::move(sName), vValues, detail::EnumField<E>("", nullptr, std::move(vNames), bReadOnly), false,
                              bReadOnly);
}

// 列挙型の静的配列
template<class E, std::size_t N>
    requires std::is_enum_v<E>
EditorField EditEnum(std::string sName, std::array<E, N>& aValues, std::vector<std::string> vNames, bool bReadOnly = false) {
    return detail::ArrayField(std::move(sName), aValues, detail::EnumField<E>("", nullptr, std::move(vNames), bReadOnly), true,
                              bReadOnly);
}

//==================================================================================
// --- スライダー + 数値欄で表示する処理（Unity の [Range(min, max)]） ---
//==================================================================================
template<class T>
    requires std::same_as<T, float> || std::same_as<T, int>
EditorField EditRange(std::string sName, T& value, float fMin, float fMax, bool bReadOnly = false) {
    EditorField field = detail::LeafField(std::move(sName), &value, bReadOnly);
    field.bRange = true;
    field.fMin = std::min(fMin, fMax); // min と max を逆に渡しても動くようにする
    field.fMax = std::max(fMin, fMax);
    return field;
}

// 配列を展開した後のインスペクターの 1 行（Unity のプロパティパスと同じ形）:
//   "Points"（見出し）、"Points.Size"、"Points[0]"、"Points[1]"、...
struct EditorFieldInfo {
    std::string sPath;                   // SetComponentEditorValue とシーンファイルで使うキー
    std::string sLabel;                  // 表示名（"Speed"、"Size"、"Element 0"）
    EditorFieldType type{};              // 値の種類
    bool bReadOnly = false;              // 読み取り専用か
    std::vector<std::string> vEnumNames; // 列挙型の表示名
    bool bRange = false;                 // スライダーを表示するか
    float fMin = 0.0f, fMax = 1.0f;      // スライダーの範囲
    int nIndent = 0;                     // 字下げ（配列のサイズ / 要素は 1）
    bool bArrayHeader = false;           // 折りたたみの見出し行（値なし）
    bool bArraySize = false;             // 配列のサイズの行か
    std::string sArrayPath;              // 所属する配列（サイズ / 要素の行）
    std::string sValue;                  // 現在の値（文字列）
};

//**********************************************************************************
// *** 衝突の情報（OnCollisionEnter / Stay / Exit に渡される。Physics.h を参照） ***
//**********************************************************************************
struct Collision {
    Entity* pOther = nullptr;          // 相手の物体（呼び出し中のみ有効）
    int nOtherIndex = -1;              // Scene::vEntities での相手の番号
    DirectX::XMFLOAT3 normal{0, 1, 0}; // 法線（相手からこちらへ向かう向き）
    DirectX::XMFLOAT3 point{0, 0, 0};  // ワールド座標でのおおよその接触点
    float fRelativeSpeed = 0.0f;       // 接触し始めたときの法線方向の接近速度
};

//**********************************************************************************
// *** コンポーネント（ユーザーが作る振る舞いの基底クラス） ***
//**********************************************************************************
// ユーザーの振る舞いはクラスとして定義する（シーンベースのエディターと同じ
// コンポーネント方式）。実行中は、付けられた数だけインスタンスが作られる。
//
// 1 フレームの処理順:  Start（1 回だけ）→ Update → 物理（FixedUpdate、衝突 /
// トリガーのイベント、固定ステップを 0..n 回）→ コルーチン → LateUpdate
class Component {
public:
    virtual ~Component() = default;

    virtual void Start(Entity&) {} // 最初の Update の前に 1 回

    virtual void Update(Entity&, float fDeltaSeconds) = 0; // 毎フレーム

    // そのフレームのすべての Update とコルーチンの後に呼ばれる（カメラの追従など）
    virtual void LateUpdate(Entity&, float /*fDeltaSeconds*/) {}

    // 物理（Physics.h）。FixedUpdate は固定ステップごとにシミュレーションの前に
    // 呼ばれる。イベントは、2 つの物体のどちらかに Rigidbody が必要。
    virtual void FixedUpdate(Entity&, float /*fFixedDeltaSeconds*/) {}

    virtual void OnCollisionEnter(Entity&, const Collision&) {} // 衝突し始めた

    virtual void OnCollisionStay(Entity&, const Collision&) {} // 衝突し続けている

    virtual void OnCollisionExit(Entity&, const Collision&) {} // 離れた

    virtual void OnTriggerEnter(Entity&, Entity& /*other*/) {} // トリガーに入った

    virtual void OnTriggerStay(Entity&, Entity& /*other*/) {} // トリガーの中にいる

    virtual void OnTriggerExit(Entity&, Entity& /*other*/) {} // トリガーから出た

    // インスペクターで編集できる変数を返す。private のメンバーも、ここで
    // EditField() を返せば意図的に公開できる。
    virtual std::vector<EditorField> EditorFields() { return {}; }

    // ---- コルーチン（Core/Coroutine.h を参照） ---------------------------
    // coroutine を最初の co_yield まで今すぐ実行し、以降は 1 フレームに 1 回進める
    CoroutineHandle StartCoroutine(Coroutine coroutine) { return m_coroutines.Start(std::move(coroutine)); }

    bool StopCoroutine(CoroutineHandle handle) { return m_coroutines.Stop(handle); } // 1 つ停止

    void StopAllCoroutines() { m_coroutines.StopAll(); } // すべて停止

    [[nodiscard]] bool IsCoroutineRunning(CoroutineHandle handle) const { return m_coroutines.IsRunning(handle); }

    CoroutineRunner& Coroutines() noexcept { return m_coroutines; }

private:
    CoroutineRunner m_coroutines; // このコンポーネントが動かしているコルーチン
};

//**********************************************************************************
// *** カメラのコンポーネント ***
//**********************************************************************************
class CameraComponent final : public Component {
public:
    bool m_bActive = true;        // 使用するか（有効なカメラのうち最初の物がゲームの視点）
    bool m_bOrthographic = false; // 平行投影か
    float m_fFieldOfView = 60.0f; // 視野角（度）
    float m_fNearClip = 0.1f;     // 近クリップ面
    float m_fFarClip = 1000.0f;   // 遠クリップ面

    void Update(Entity&, float) override {}

    std::vector<EditorField> EditorFields() override {
        return {EditField("Active", m_bActive), EditField("Orthographic", m_bOrthographic),
                EditField("Field of View", m_fFieldOfView), EditField("Near Clip", m_fNearClip),
                EditField("Far Clip", m_fFarClip)};
    }
};

// コンポーネントを生成する関数の型
using ComponentFactory = std::unique_ptr<Component> (*)();

//**********************************************************************************
// *** エンティティ（シーン内の物体 1 つ） ***
//**********************************************************************************
struct Entity {
    enum class RendererKind : unsigned char { None, Polygon, Model, Image, Mesh }; // レンダラーの種類
    std::string sName;                                                             // 名前
    DrawItem draw;                                                                 // トランスフォーム（親からの相対）と色
    int nParent = -1;                                                              // 親の番号（-1 = 親なし）
    RendererKind renderer = RendererKind::Polygon;                                 // レンダラーの種類
    std::string sModelAsset;                                                       // モデル / メッシュのアセット
    std::string sMaterialAsset;                                                    // マテリアルのアセット
    std::string sTextureAsset;                                                     // テクスチャのアセット
    bool bPlayAnimation = true;                                                    // アニメーションを再生するか
    float fAnimationTime = 0.0f;                                                   // アニメーションの再生時間（秒）
    int nAnimationIndex = 0;                                                       // 再生するアニメーションの番号
    // 付けているコンポーネントの名前（プロジェクトのコンポーネント登録から解決する）
    std::vector<std::string> vComponents;
    using PropertyMap = std::unordered_map<std::string, std::string>;
    std::unordered_map<std::string, PropertyMap> componentProperties; // コンポーネント名 → (変数名 → 値) の保存値
    std::vector<std::string> vRuntimeComponentNames;                  // 実行中のインスタンスの名前
    std::vector<std::unique_ptr<Component>> vRuntimeComponents;       // 実行中のインスタンス
    std::vector<bool> vRuntimeComponentStarted;                       // Start を呼んだか

    // 実行中のインスタンスはコピーしない（コピー / ムーブは Engine.cpp で定義）
    Entity() = default;
    ~Entity();
    Entity(const Entity& other);
    Entity& operator=(const Entity& other);
    Entity(Entity&&) noexcept;
    Entity& operator=(Entity&&) noexcept;
};

constexpr std::size_t MaxComponentsPerEntity = 64; // 1 つのエンティティに付けられるコンポーネントの最大数

//==================================================================================
// --- コンポーネントの取得処理 ---
//==================================================================================
// entity で動いている T のインスタンス（実行中のみ）。無ければ nullptr。
// 例: if (auto* pBody = GetComponent<Rigidbody>(entity)) pBody->AddForce(...);
template<class T>
T* GetComponent(Entity& entity) {
    for (auto& pComponent : entity.vRuntimeComponents) {
        if (auto* pFound = dynamic_cast<T*>(pComponent.get())) return pFound;
    }

    return nullptr;
}

//**********************************************************************************
// *** 物理の実行時の情報（保存しない） ***
//**********************************************************************************
struct PhysicsState {
    float fAccumulator = 0.0f;            // 固定ステップに満たない残り時間
    std::vector<std::uint64_t> vContacts; // 前のステップで接触していたエンティティ番号の組
    std::vector<std::uint64_t> vTriggers; // 前のステップで重なっていたトリガーの組
};

//**********************************************************************************
// *** シーン ***
//**********************************************************************************
struct Scene {
    std::vector<Entity> vEntities; // エンティティの一覧
    Camera camera;                 // エディターのカメラ
    PhysicsState physics;          // 物理の実行時の情報
    // モデルアセットの相対パスの基準（実行時のみ。保存しない）
    std::filesystem::path projectRoot;
};

//**********************************************************************************
// *** シーンの読み込み / 保存 / ゲームの書き出し ***
//**********************************************************************************
// まず一時的なシーンに読み込むため、壊れたファイルを読んでも編集中のシーンは
// 失われない。テキストは UTF-8 で、エンティティ名は "" で囲む。
bool LoadScene(const std::filesystem::path& path, Scene& scene, std::string& error);
bool SaveScene(const std::filesystem::path& path, const Scene& scene, std::string& error);
bool ExportGame(const std::filesystem::path& baseDir, const std::filesystem::path& dest, const Scene& scene, std::string& error);
bool ExportGame(const std::filesystem::path& baseDir, const std::filesystem::path& projectDir, const std::filesystem::path& dest,
                const Scene& scene, std::string& error);

//**********************************************************************************
// *** 親子関係とトランスフォーム ***
//**********************************************************************************
// 親からの相対トランスフォームを、ワールド空間の描画情報に変換する
std::vector<DrawItem> ResolveWorldDraws(const Scene& scene);
DrawItem WorldDraw(const Scene& scene, int index);                                 // 1 つ分のワールド変換
DrawItem LocalDrawFromWorld(const Scene& scene, int index, const DrawItem& world); // ワールド → 親からの相対
bool SetEntityParent(Scene& scene, int child, int parent, std::string& error);     // 親の設定（循環は失敗）

//**********************************************************************************
// *** コンポーネントの登録と実行 ***
//**********************************************************************************
// 起動時にコールバックを登録する。コールバックには持ち主のエンティティと
// 経過秒数が渡され、トランスフォームや色を変更できる。
using ComponentUpdate = void (*)(Entity&, float);
bool RegisterComponent(const std::string& name, ComponentUpdate update);        // 関数で登録
bool RegisterComponentClass(const std::string& name, ComponentFactory factory); // クラスで登録
bool IsComponentRegistered(const std::string& name);
// 登録済みのコンポーネント（または "Camera"）を追加する。重複している場合と、
// 既に MaxComponentsPerEntity 個付いている場合は失敗する。
bool AttachComponent(Entity& entity, const std::string& name, std::string& error);
bool DetachComponent(Entity& entity, std::size_t componentIndex);
// エディターのコンポーネント選択用: 登録済みの名前をアルファベット順で返す
std::vector<std::string> RegisteredComponentNames();
void UpdateUserComponents(Scene& scene, float fDeltaSeconds); // 1 フレーム分の全コンポーネントの更新
std::vector<std::pair<std::string, std::string>> ComponentEditorValues(Entity& entity, std::size_t componentIndex);
// コンポーネントのインスペクターの全行（配列は展開済み）と現在の値
std::vector<EditorFieldInfo> ComponentFieldInfos(Entity& entity, std::size_t componentIndex);
bool SetComponentEditorValue(Entity& entity, std::size_t componentIndex, const std::string& field, const std::string& value,
                             std::string& error);
Camera ActiveGameCamera(Scene& scene); // ゲームの視点になるカメラ

//**********************************************************************************
// *** プロジェクトの設定 ***
//**********************************************************************************
// <プロジェクト>/ProjectSettings.dxsettings に保存し、書き出したゲームの横にも
// コピーする。ファイルが無い場合は下の既定値を使う。
inline constexpr const wchar_t* PROJECT_SETTINGS_FILE_NAME = L"ProjectSettings.dxsettings";

struct ProjectSettings {
    GraphicsApi graphicsApi = GraphicsApi::DIRECT3D12; // 使用するグラフィックス API
};

bool LoadProjectSettings(const std::filesystem::path& path, ProjectSettings& settings, std::string& error);
bool SaveProjectSettings(const std::filesystem::path& path, const ProjectSettings& settings, std::string& error);

// プロジェクト側で実装する。自作のコンポーネントはここで登録する。
void RegisterProjectComponents();

// コンポーネントのクラスを登録するマクロ（.cpp に 1 行書くだけで登録される）
#define DXS_REGISTER_COMPONENT(Type)                                                                                             \
    namespace {                                                                                                                  \
    static_assert(std::derived_from<Type, dxs::Component>, "DXS component classes must derive from dxs::Component");             \
    std::unique_ptr<dxs::Component> Create_##Type() {                                                                            \
        return std::make_unique<Type>();                                                                                         \
    }                                                                                                                            \
    const bool Registered_##Type = dxs::RegisterComponentClass(#Type, Create_##Type);                                            \
    }

} // namespace dxs

#include "Physics.h"           // Rigidbody / コライダーもエンジンの API に含める
#include "Input/playerInput.h" // CInputSystem + PlayerInput
