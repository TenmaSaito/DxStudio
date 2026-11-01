//==================================================================================
//
// 物理（当たり判定と剛体）のヘッダーファイル [Physics.h]
//
// エンジン組み込みの物理コンポーネント（Unity の Rigidbody / BoxCollider /
// SphereCollider / MeshCollider に相当）。
//
//   ・Collider は当たり判定の形を与える。Rigidbody が無ければ動かない物
//     （床・壁）。Box は物体の回転に追従する。
//   ・Rigidbody を付けると動く: 重力・空気抵抗・力・反発・摩擦。物体は平行移動
//     のみで、衝突で回転はしない（Unity の Freeze Rotation 相当）。キネマティック
//     はスクリプトだけで動かすが、動く物体を押し出し、衝突イベントも受け取る。
//   ・Is Trigger の Collider は押し返さず、OnTriggerEnter / Stay / Exit を通知する。
//   ・組み合わせ: 1 つの物体に Box / Sphere / Mesh を同時に付けられる。
//     Rigidbody を持たない子の Collider は、一番近い親の Rigidbody の一部になる
//     （複合コライダー）。同じ物体の部品同士はぶつからず、イベントは
//     Rigidbody を持つ物体に届く。
//   ・衝突イベントには、2 つのうち少なくとも一方に Rigidbody が必要（Unity と同じ）。
//
// 物理は Update の後に PHYSICS_FIXED_DELTA 秒の固定ステップで進む
// （FixedUpdate → 移動 → 接触の解決 → 衝突 / トリガーのイベント）。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Engine.h"

#include <DirectXMath.h>

namespace dxs {

//**********************************************************************************
// *** 定数宣言 ***
//**********************************************************************************
inline constexpr float PHYSICS_FIXED_DELTA = 1.0f / 50.0f; // 固定ステップの秒数（Unity の Fixed Timestep の既定値）
inline constexpr int PHYSICS_MAX_STEPS_PER_FRAME =
    8; // 1 フレームの最大ステップ数（重いフレームの後に処理が雪だるま式に増えるのを防ぐ）
inline constexpr int PHYSICS_SOLVER_ITERATIONS = 6; // 接触の解決を繰り返す回数

//**********************************************************************************
// *** 力の加え方 ***
//**********************************************************************************
enum class ForceMode : unsigned char {
    FORCE,           // 継続的な力、質量の影響あり（N）
    ACCELERATION,    // 継続的な加速度、質量を無視
    IMPULSE,         // 瞬間的な力積、質量の影響あり（N・s）
    VELOCITY_CHANGE, // 瞬間的な速度変化、質量を無視
};

//**********************************************************************************
// *** 剛体コンポーネント ***
//**********************************************************************************
class Rigidbody final : public Component {
public:
    float m_fMass = 1.0f;        // 質量
    float m_fDrag = 0.0f;        // 空気抵抗（1 秒あたりの速度の減衰）
    bool m_bUseGravity = true;   // 重力を使うか
    bool m_bIsKinematic = false; // キネマティック（物理で動かず、スクリプトで動かす）
    float m_fBounciness = 0.0f;  // 反発係数（0 = 跳ねない、1 = 完全に跳ね返る）
    float m_fFriction = 0.6f;    // 摩擦係数

    void Update(Entity&, float) override {}

    // インスペクターに表示する項目
    std::vector<EditorField> EditorFields() override {
        return {EditField("Mass", m_fMass),
                EditField("Drag", m_fDrag),
                EditField("Use Gravity", m_bUseGravity),
                EditField("Is Kinematic", m_bIsKinematic),
                EditField("Bounciness", m_fBounciness),
                EditField("Friction", m_fFriction)};
    }

    // 力を加える（FORCE / ACCELERATION は次のステップの間だけ働く）
    void AddForce(const DirectX::XMFLOAT3& force, ForceMode mode = ForceMode::FORCE);

    [[nodiscard]] const DirectX::XMFLOAT3& Velocity() const noexcept { return m_velocity; } // 速度

    void SetVelocity(const DirectX::XMFLOAT3& velocity) noexcept { m_velocity = velocity; } // 速度を直接設定

    // 質量の逆数（キネマティックや質量 0 以下は 0 = 動かない扱い）
    [[nodiscard]] float InverseMass() const noexcept { return m_bIsKinematic || m_fMass <= 0.0f ? 0.0f : 1.0f / m_fMass; }

private:
    friend void StepPhysics(Scene& scene, float fFixedDelta); // 物理のステップが速度を直接更新する
    DirectX::XMFLOAT3 m_velocity{0, 0, 0};                    // 速度
    DirectX::XMFLOAT3 m_acceleration{0, 0, 0};                // 次のステップまでに加えられた力による加速度
};

//**********************************************************************************
// *** コライダーの基底クラス ***
//**********************************************************************************
class Collider : public Component {
public:
    bool m_bIsTrigger = false;                                     // トリガー（押し返さずイベントだけ通知）
    float m_fCenterX = 0.0f, m_fCenterY = 0.0f, m_fCenterZ = 0.0f; // 形の中心のずれ（ローカル座標）

    void Update(Entity&, float) override {}

protected:
    // 全コライダー共通のインスペクター項目
    std::vector<EditorField> BaseFields() {
        return {EditField("Is Trigger", m_bIsTrigger), EditField("Center X", m_fCenterX), EditField("Center Y", m_fCenterY),
                EditField("Center Z", m_fCenterZ)};
    }
};

//**********************************************************************************
// *** 箱のコライダー ***
//**********************************************************************************
// 既定の大きさ 1 は、組み込みの Cube / Square と同じ大きさ。
class BoxCollider final : public Collider {
public:
    float m_fSizeX = 1.0f, m_fSizeY = 1.0f, m_fSizeZ = 1.0f; // 箱の大きさ（物体の拡大率が掛かる）

    std::vector<EditorField> EditorFields() override {
        auto vFields = BaseFields();
        vFields.push_back(EditField("Size X", m_fSizeX));
        vFields.push_back(EditField("Size Y", m_fSizeY));
        vFields.push_back(EditField("Size Z", m_fSizeZ));
        return vFields;
    }
};

//**********************************************************************************
// *** 球のコライダー ***
//**********************************************************************************
// 既定の半径 0.5 は、組み込みの Sphere / Circle と同じ大きさ。半径には物体の
// 拡大率の一番大きい軸の値が掛かる（Unity と同じ）。
class SphereCollider final : public Collider {
public:
    float m_fRadius = 0.5f; // 半径

    std::vector<EditorField> EditorFields() override {
        auto vFields = BaseFields();
        vFields.push_back(EditField("Radius", m_fRadius));
        return vFields;
    }
};

//**********************************************************************************
// *** メッシュのコライダー ***
//**********************************************************************************
// 物体の MeshRenderer / ModelRenderer が描いているメッシュで判定する
// （PolygonRenderer の物体は立方体 / 四角形を使う）。Unity と同じく:
//   ・Convex オフ: 三角形そのまま。静止物かキネマティック向け。凹メッシュ同士は衝突しない。
//   ・Convex オン: メッシュの凸包。動く Rigidbody でも使える（動く物体に凹メッシュを
//     付けた場合は自動で凸として扱う）。面の向きが 64 を超える滑らかな形は近似になる。
class MeshCollider final : public Collider {
public:
    bool m_bConvex = false; // 凸包で判定するか

    std::vector<EditorField> EditorFields() override {
        auto vFields = BaseFields();
        vFields.push_back(EditField("Convex", m_bConvex));
        return vFields;
    }
};

//**********************************************************************************
// *** 重力 ***
//**********************************************************************************
namespace physics {
[[nodiscard]] DirectX::XMFLOAT3 Gravity() noexcept;
void SetGravity(const DirectX::XMFLOAT3& gravity) noexcept; // 既定値は (0, -9.81, 0)
} // namespace physics

// シーンビュー用の緑のコライダーの輪郭（点 2 つで 1 本）。
// 再生中は実行中のコンポーネントの値、それ以外はインスペクターに保存された値を使う。
std::vector<DirectX::XMFLOAT3> ColliderOutlineSegments(const Scene& scene);

// fDeltaSeconds 秒分のゲーム時間を固定ステップで進める
// （UpdateUserComponents から呼ばれる。端数は scene.physics に残る）。
void SimulatePhysics(Scene& scene, float fDeltaSeconds);
// 固定ステップを 1 回進める（テストや独自のループ用に公開）。
void StepPhysics(Scene& scene, float fFixedDelta);

} // namespace dxs
