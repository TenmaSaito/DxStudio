#pragma once
#include "Engine.h"

// ============================================================================
//  PhysicsTemplate — 当たり判定と物理（Rigidbody / Collider）の使い方テンプレート
// ============================================================================
//  使い方（Unity と同じ手順です）
//    1. GameObject > 3D Object > Plane（または Cube を Scale で平たく）で床を作り、
//       Add Component > Physics > BoxCollider を付ける（Rigidbody なし = 動かない床）
//    2. GameObject > 3D Object > Sphere を床の上に置き、
//       Add Component > Physics > SphereCollider と Rigidbody を付ける
//    3. その Sphere に Add Component > Scripts > PhysicsTemplate を付けて Play
//       ← → ↑ ↓ で転がし、Space でジャンプ（接地中のみ）します。
//       Is Trigger にした Collider に入ると色が変わります。
//
//  組み込みコンポーネント（Add Component > Physics）
//  ---------------------------------------------------------------------------
//  Rigidbody       Mass / Drag / Use Gravity / Is Kinematic / Bounciness / Friction
//  BoxCollider     Is Trigger / Center / Size     （Size 1 = 組み込み Cube と同じ大きさ）
//  SphereCollider  Is Trigger / Center / Radius   （Radius 0.5 = 組み込み Sphere）
//  Scene ビューでは Collider が緑の線で表示されます。
//
//  Unity C# との対応
//  ---------------------------------------------------------------------------
//  GetComponent<Rigidbody>()               dxs::GetComponent<dxs::Rigidbody>(entity)
//  rb.AddForce(f)                          pBody->AddForce(f)            (ForceMode::FORCE)
//  rb.AddForce(f, ForceMode.Impulse)       pBody->AddForce(f, dxs::ForceMode::IMPULSE)
//  rb.velocity / rb.velocity = v           pBody->Velocity() / pBody->SetVelocity(v)
//  Physics.gravity                         dxs::physics::Gravity() / SetGravity()
//  void FixedUpdate()                      void FixedUpdate(dxs::Entity&, float) override
//  void OnCollisionEnter(Collision c)      void OnCollisionEnter(dxs::Entity&, const dxs::Collision&) override
//  c.gameObject / c.contacts[0].normal     c.pOther / c.normal（相手から自分へ向く法線）
//  void OnTriggerEnter(Collider other)     void OnTriggerEnter(dxs::Entity&, dxs::Entity& other) override
//
//  注意
//  ---------------------------------------------------------------------------
//  ・物理は 1/50 秒の固定ステップで、Update の後に実行されます（FixedUpdate →
//    移動 → 衝突解決 → OnCollision / OnTrigger）。力は FixedUpdate で加えると安定します。
//  ・衝突イベントは、どちらか一方に Rigidbody が必要です（Unity と同じ）。
//  ・物体は衝突で回転しません（Unity の Freeze Rotation 相当）。回転は
//    スクリプトから entity.draw.rotation を変更してください。
//  ・Is Kinematic の Rigidbody は重力で落ちず、スクリプトで動かします。
//    動的な物体を押し出し、衝突イベントも受け取ります。

class PhysicsTemplate final : public dxs::Component {
public:
    void Start(dxs::Entity& entity) override;
    void Update(dxs::Entity& entity, float fDeltaSeconds) override;
    void FixedUpdate(dxs::Entity& entity, float fFixedDeltaSeconds) override;
    void OnCollisionEnter(dxs::Entity& entity, const dxs::Collision& collision) override;
    void OnCollisionStay(dxs::Entity& entity, const dxs::Collision& collision) override;
    void OnCollisionExit(dxs::Entity& entity, const dxs::Collision& collision) override;
    void OnTriggerEnter(dxs::Entity& entity, dxs::Entity& other) override;
    void OnTriggerExit(dxs::Entity& entity, dxs::Entity& other) override;

    std::vector<dxs::EditorField> EditorFields() override {
        return {dxs::EditField("Move Force", m_fMoveForce), dxs::EditField("Jump Speed", m_fJumpSpeed),
                dxs::EditField("Grounded", m_bGrounded, true)};
    }

private:
    float m_fMoveForce = 12.0f; // N（Mass 1 なら 12 m/s^2）
    float m_fJumpSpeed = 5.0f;  // m/s
    bool m_bGrounded = false;   // 読み取り専用で Inspector に表示
    bool m_bJumpRequested = false;
    DirectX::XMFLOAT4 m_originalColor{1, 1, 1, 1};
};
