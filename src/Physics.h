#pragma once

// Built-in physics components (Unity: Rigidbody, BoxCollider, SphereCollider).
//
// * A Collider gives an object a collision shape. Without a Rigidbody it is
//   static (floors, walls). Box colliders follow the object's rotation.
// * A Rigidbody makes the object move: gravity, drag, forces, bounce and
//   friction. Bodies move linearly; collisions do not make them spin
//   (Unity's "Freeze Rotation"). A kinematic body is moved only by scripts
//   but still pushes dynamic bodies and receives collision events.
// * Is Trigger colliders do not block; they report OnTriggerEnter/Stay/Exit.
// * Events need a Rigidbody on at least one of the two objects (as in Unity).
//
// Physics runs in fixed steps of PHYSICS_FIXED_DELTA seconds after Update
// (FixedUpdate -> integrate -> solve contacts -> collision/trigger events).

#include "Engine.h"

#include <DirectXMath.h>

namespace dxs {

inline constexpr float PHYSICS_FIXED_DELTA = 1.0f / 50.0f;  // Unity default Fixed Timestep
inline constexpr int PHYSICS_MAX_STEPS_PER_FRAME = 8;       // avoids a spiral after a long frame
inline constexpr int PHYSICS_SOLVER_ITERATIONS = 6;

enum class ForceMode : unsigned char {
    FORCE,           // continuous, uses mass (N)
    ACCELERATION,    // continuous, ignores mass
    IMPULSE,         // instant, uses mass (N*s)
    VELOCITY_CHANGE, // instant, ignores mass
};

class Rigidbody final : public Component {
public:
    float m_fMass = 1.0f;
    float m_fDrag = 0.0f;        // linear damping per second
    bool m_bUseGravity = true;
    bool m_bIsKinematic = false;
    float m_fBounciness = 0.0f;  // 0 = no bounce, 1 = perfectly elastic
    float m_fFriction = 0.6f;

    void Update(Entity&, float) override {}
    std::vector<EditorField> EditorFields() override {
        return {EditField("Mass", m_fMass),          EditField("Drag", m_fDrag),
                EditField("Use Gravity", m_bUseGravity), EditField("Is Kinematic", m_bIsKinematic),
                EditField("Bounciness", m_fBounciness),  EditField("Friction", m_fFriction)};
    }

    void AddForce(const DirectX::XMFLOAT3& force, ForceMode mode = ForceMode::FORCE);
    [[nodiscard]] const DirectX::XMFLOAT3& Velocity() const noexcept { return m_velocity; }
    void SetVelocity(const DirectX::XMFLOAT3& velocity) noexcept { m_velocity = velocity; }
    [[nodiscard]] float InverseMass() const noexcept { return m_bIsKinematic || m_fMass <= 0.0f ? 0.0f : 1.0f / m_fMass; }

private:
    friend void StepPhysics(Scene& scene, float fFixedDelta);
    DirectX::XMFLOAT3 m_velocity{0, 0, 0};
    DirectX::XMFLOAT3 m_acceleration{0, 0, 0}; // forces accumulated until the next step
};

class Collider : public Component {
public:
    bool m_bIsTrigger = false;
    float m_fCenterX = 0.0f, m_fCenterY = 0.0f, m_fCenterZ = 0.0f; // local offset

    void Update(Entity&, float) override {}

protected:
    std::vector<EditorField> BaseFields() {
        return {EditField("Is Trigger", m_bIsTrigger), EditField("Center X", m_fCenterX), EditField("Center Y", m_fCenterY),
                EditField("Center Z", m_fCenterZ)};
    }
};

// Default size 1 matches the built-in Cube / Square primitives.
class BoxCollider final : public Collider {
public:
    float m_fSizeX = 1.0f, m_fSizeY = 1.0f, m_fSizeZ = 1.0f;
    std::vector<EditorField> EditorFields() override {
        auto vFields = BaseFields();
        vFields.push_back(EditField("Size X", m_fSizeX));
        vFields.push_back(EditField("Size Y", m_fSizeY));
        vFields.push_back(EditField("Size Z", m_fSizeZ));
        return vFields;
    }
};

// Default radius 0.5 matches the built-in Sphere / Circle primitives. The
// radius is scaled by the largest axis of the object's scale (as in Unity).
class SphereCollider final : public Collider {
public:
    float m_fRadius = 0.5f;
    std::vector<EditorField> EditorFields() override {
        auto vFields = BaseFields();
        vFields.push_back(EditField("Radius", m_fRadius));
        return vFields;
    }
};

namespace physics {
[[nodiscard]] DirectX::XMFLOAT3 Gravity() noexcept;
void SetGravity(const DirectX::XMFLOAT3& gravity) noexcept; // default (0, -9.81, 0)
} // namespace physics

// Unity-style green collider outlines for the Scene view (pairs of points).
// Uses live components in Play Mode, else the values saved in the Inspector.
std::vector<DirectX::XMFLOAT3> ColliderOutlineSegments(const Scene& scene);

// Runs whole fixed steps for `fDeltaSeconds` of game time (called by
// UpdateUserComponents; scene.physics keeps the remainder).
void SimulatePhysics(Scene& scene, float fDeltaSeconds);
// One fixed step. Exposed for tests and custom loops.
void StepPhysics(Scene& scene, float fFixedDelta);

} // namespace dxs
