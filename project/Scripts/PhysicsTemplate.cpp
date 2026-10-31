#include "PhysicsTemplate.h"

#include <windows.h>

namespace {
bool IsKeyDown(int nKey) { return (GetAsyncKeyState(nKey) & 0x8000) != 0; }
} // namespace

void PhysicsTemplate::Start(dxs::Entity& entity) {
    m_originalColor = entity.draw.color;
    if (!dxs::GetComponent<dxs::Rigidbody>(entity))
        dxs::Debug::LogWarning(entity.sName + ": PhysicsTemplate needs a Rigidbody (Add Component > Physics > Rigidbody).");
}

// 入力は毎フレームの Update で読み、力は FixedUpdate で加えます（Unity の定石）。
void PhysicsTemplate::Update(dxs::Entity&, float) {
    if (IsKeyDown(VK_SPACE) && m_bGrounded) m_bJumpRequested = true;
}

void PhysicsTemplate::FixedUpdate(dxs::Entity& entity, float) {
    auto* pBody = dxs::GetComponent<dxs::Rigidbody>(entity);
    if (!pBody) return;
    DirectX::XMFLOAT3 force{0, 0, 0};
    if (IsKeyDown(VK_LEFT)) force.x -= m_fMoveForce;
    if (IsKeyDown(VK_RIGHT)) force.x += m_fMoveForce;
    if (IsKeyDown(VK_UP)) force.z += m_fMoveForce;
    if (IsKeyDown(VK_DOWN)) force.z -= m_fMoveForce;
    pBody->AddForce(force); // ForceMode::FORCE: 質量で割られ、このステップの間だけ働く
    if (m_bJumpRequested) {
        // 速度を直接変えるジャンプ（ForceMode::VELOCITY_CHANGE は質量を無視）
        const auto velocity = pBody->Velocity();
        pBody->SetVelocity({velocity.x, m_fJumpSpeed, velocity.z});
        m_bJumpRequested = false;
    }
}

// 衝突した最初のステップに 1 回だけ呼ばれます。
void PhysicsTemplate::OnCollisionEnter(dxs::Entity& entity, const dxs::Collision& collision) {
    if (collision.fRelativeSpeed > 2.0f)
        dxs::Debug::Log(entity.sName + " hit " + collision.pOther->sName + " at " + std::to_string(collision.fRelativeSpeed) + " m/s");
}

// 接触している間、物理ステップごとに呼ばれます。法線が上向きなら床の上です。
void PhysicsTemplate::OnCollisionStay(dxs::Entity&, const dxs::Collision& collision) {
    if (collision.normal.y > 0.7f) m_bGrounded = true;
}

void PhysicsTemplate::OnCollisionExit(dxs::Entity&, const dxs::Collision&) { m_bGrounded = false; }

// Is Trigger の Collider に入った / 出た時に呼ばれます（押し返されません）。
void PhysicsTemplate::OnTriggerEnter(dxs::Entity& entity, dxs::Entity& other) {
    entity.draw.color = {1.0f, 0.85f, 0.2f, m_originalColor.w};
    dxs::Debug::Log(entity.sName + " entered trigger " + other.sName);
}

void PhysicsTemplate::OnTriggerExit(dxs::Entity& entity, dxs::Entity&) { entity.draw.color = m_originalColor; }

DXS_REGISTER_COMPONENT(PhysicsTemplate)
