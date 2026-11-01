#include "PhysicsTemplate.h"

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

// Event style: PlayerInput calls this when an action starts / performs / ends.
void PhysicsTemplate::OnInputAction(dxs::Entity&, const dxs::CInputAction& action) {
    if (action.IsEqualName("Jump") && action.WasPressedThisFrame() && m_bGrounded) {
        m_bJumpRequested = true;
    }
}

// Polling style: read the Move action and push the body every physics step.
void PhysicsTemplate::FixedUpdate(dxs::Entity& entity, float) {
    auto* pBody = dxs::GetComponent<dxs::Rigidbody>(entity);
    const auto* pInput = dxs::GetComponent<dxs::PlayerInput>(entity);
    if (!pBody || !pInput) return;
    const dxs::Vector3 move = pInput->ReadValue2D("Move");                 // x = right, y = forward (-1..1)
    pBody->AddForce({move.x * m_fMoveForce, 0.0f, move.y * m_fMoveForce}); // ForceMode::FORCE: this step only
    if (m_bJumpRequested) {
        // Jump by setting the vertical speed directly.
        const auto velocity = pBody->Velocity();
        pBody->SetVelocity({velocity.x, m_fJumpSpeed, velocity.z});
        m_bJumpRequested = false;
    }
}

// Called once, on the first step two objects touch.
void PhysicsTemplate::OnCollisionEnter(dxs::Entity& entity, const dxs::Collision& collision) {
    if (collision.fRelativeSpeed > 2.0f) {
        dxs::Debug::Log(entity.sName + " hit " + collision.pOther->sName + " at " + std::to_string(collision.fRelativeSpeed) +
                        " m/s");
    }
}

// Every physics step while touching. An upward normal means standing on it.
void PhysicsTemplate::OnCollisionStay(dxs::Entity&, const dxs::Collision& collision) {
    if (collision.normal.y > 0.7f) {
        m_bGrounded = true;
    }
}

void PhysicsTemplate::OnCollisionExit(dxs::Entity&, const dxs::Collision&) {
    m_bGrounded = false;
}

// Is Trigger colliders report entering / leaving (they do not push back).
void PhysicsTemplate::OnTriggerEnter(dxs::Entity& entity, dxs::Entity& other) {
    entity.draw.color = {1.0f, 0.85f, 0.2f, m_originalColor.w};
    dxs::Debug::Log(entity.sName + " entered trigger " + other.sName);
}

void PhysicsTemplate::OnTriggerExit(dxs::Entity& entity, dxs::Entity&) {
    entity.draw.color = m_originalColor;
}

DXS_REGISTER_COMPONENT(PhysicsTemplate)
