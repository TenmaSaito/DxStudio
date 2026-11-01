//==================================================================================
//
// 物理のテンプレートのソースファイル [PhysicsTemplate.cpp]
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "PhysicsTemplate.h"

//==================================================================================
// --- 開始処理（必要なコンポーネントの確認） ---
//==================================================================================
void PhysicsTemplate::Start(dxs::Entity& entity) {
    m_originalColor = entity.draw.color;
    if (!dxs::GetComponent<dxs::Rigidbody>(entity)) {
        dxs::Debug::LogWarning(entity.sName + ": PhysicsTemplate needs a Rigidbody (Add Component > Physics > Rigidbody).");
    }

    if (!dxs::GetComponent<dxs::PlayerInput>(entity)) {
        dxs::Debug::LogWarning(entity.sName + ": PhysicsTemplate needs a PlayerInput (Add Component > Input > PlayerInput).");
    }
}

void PhysicsTemplate::Update(dxs::Entity&, float) {}

//==================================================================================
// --- 入力のイベントの処理 ---
//==================================================================================
// イベントで受け取る方法: アクションの開始 / 成立 / 終了のときに PlayerInput が呼ぶ
void PhysicsTemplate::OnInputAction(dxs::Entity&, const dxs::CInputAction& action) {
    if (action.IsEqualName("Jump") && action.WasPressedThisFrame() && m_bGrounded) {
        m_bJumpRequested = true;
    }
}

//==================================================================================
// --- 物理ステップごとの処理 ---
//==================================================================================
// 読みに行く方法: 物理ステップごとに Move のアクションを読み、物体を押す
void PhysicsTemplate::FixedUpdate(dxs::Entity& entity, float) {
    auto* pBody = dxs::GetComponent<dxs::Rigidbody>(entity);
    const auto* pInput = dxs::GetComponent<dxs::PlayerInput>(entity);
    if (!pBody || !pInput) return;
    const dxs::Vector3 move = pInput->ReadValue2D("Move");                 // x = 右、y = 前（-1..1）
    pBody->AddForce({move.x * m_fMoveForce, 0.0f, move.y * m_fMoveForce}); // ForceMode::FORCE: このステップだけ力を加える
    if (m_bJumpRequested) {
        // 上向きの速さを直接設定してジャンプする
        const auto velocity = pBody->Velocity();
        pBody->SetVelocity({velocity.x, m_fJumpSpeed, velocity.z});
        m_bJumpRequested = false;
    }
}

//==================================================================================
// --- 衝突の開始の処理 ---
//==================================================================================
// 2 つの物体が触れた最初のステップで 1 回呼ばれる
void PhysicsTemplate::OnCollisionEnter(dxs::Entity& entity, const dxs::Collision& collision) {
    if (collision.fRelativeSpeed > 2.0f) {
        dxs::Debug::Log(entity.sName + " hit " + collision.pOther->sName + " at " + std::to_string(collision.fRelativeSpeed) +
                        " m/s");
    }
}

//==================================================================================
// --- 衝突中の処理 ---
//==================================================================================
// 触れている間、物理ステップごとに呼ばれる。法線が上向きなら、その上に立っている。
void PhysicsTemplate::OnCollisionStay(dxs::Entity&, const dxs::Collision& collision) {
    if (collision.normal.y > 0.7f) {
        m_bGrounded = true;
    }
}

//==================================================================================
// --- 衝突の終了の処理 ---
//==================================================================================
void PhysicsTemplate::OnCollisionExit(dxs::Entity&, const dxs::Collision&) {
    m_bGrounded = false;
}

//==================================================================================
// --- トリガーに入った / 出たときの処理 ---
//==================================================================================
// Is Trigger のコライダーは入った / 出たことだけを知らせる（押し返さない）
void PhysicsTemplate::OnTriggerEnter(dxs::Entity& entity, dxs::Entity& other) {
    entity.draw.color = {1.0f, 0.85f, 0.2f, m_originalColor.w};
    dxs::Debug::Log(entity.sName + " entered trigger " + other.sName);
}

void PhysicsTemplate::OnTriggerExit(dxs::Entity& entity, dxs::Entity&) {
    entity.draw.color = m_originalColor;
}

DXS_REGISTER_COMPONENT(PhysicsTemplate)
