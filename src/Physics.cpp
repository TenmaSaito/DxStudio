#include "Physics.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace dxs {
namespace {
using namespace DirectX;

XMFLOAT3 g_gravity{0.0f, -9.81f, 0.0f};

constexpr float PENETRATION_SLOP = 0.002f;    // allowed overlap, keeps resting contacts stable
constexpr float POSITION_CORRECTION = 0.8f;   // share of the overlap removed per iteration
constexpr float BOUNCE_THRESHOLD = 0.5f;      // slower impacts do not bounce (Unity: 2 m/s)
constexpr float CONTACT_EPSILON = 1e-6f;

XMFLOAT3 Add(const XMFLOAT3& a, const XMFLOAT3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
XMFLOAT3 Sub(const XMFLOAT3& a, const XMFLOAT3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
XMFLOAT3 Mul(const XMFLOAT3& a, float f) { return {a.x * f, a.y * f, a.z * f}; }
float Dot(const XMFLOAT3& a, const XMFLOAT3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
XMFLOAT3 Cross(const XMFLOAT3& a, const XMFLOAT3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(const XMFLOAT3& a) { return std::sqrt(Dot(a, a)); }

// World-space collision shape.
struct Shape {
    bool bSphere = false;
    XMFLOAT3 center{};
    XMFLOAT3 aAxes[3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // box orientation
    XMFLOAT3 halfExtents{};                             // box
    float fRadius = 0.0f;                               // sphere
};

struct ColliderRef {
    int nEntity = -1;
    Collider* pCollider = nullptr;
};

struct Contact {
    XMFLOAT3 normal{}; // from A toward B
    float fDepth = 0.0f;
    XMFLOAT3 point{};
};

Shape MakeShape(const DrawItem& world, const Collider& collider) {
    Shape shape;
    const XMMATRIX rotation = XMMatrixRotationRollPitchYaw(world.rotation.x, world.rotation.y, world.rotation.z);
    for (int nAxis = 0; nAxis < 3; ++nAxis) XMStoreFloat3(&shape.aAxes[nAxis], rotation.r[nAxis]);
    const XMFLOAT3 scale{std::abs(world.scale.x), std::abs(world.scale.y), std::abs(world.scale.z)};
    // Local centre offset is scaled and rotated like a vertex.
    const XMFLOAT3 offset{collider.m_fCenterX * world.scale.x, collider.m_fCenterY * world.scale.y, collider.m_fCenterZ * world.scale.z};
    shape.center = world.position;
    for (int nAxis = 0; nAxis < 3; ++nAxis)
        shape.center = Add(shape.center, Mul(shape.aAxes[nAxis], nAxis == 0 ? offset.x : nAxis == 1 ? offset.y : offset.z));
    if (const auto* pSphere = dynamic_cast<const SphereCollider*>(&collider)) {
        shape.bSphere = true;
        shape.fRadius = std::abs(pSphere->m_fRadius) * std::max({scale.x, scale.y, scale.z});
    } else if (const auto* pBox = dynamic_cast<const BoxCollider*>(&collider)) {
        shape.halfExtents = {std::abs(pBox->m_fSizeX) * scale.x * 0.5f, std::abs(pBox->m_fSizeY) * scale.y * 0.5f,
                             std::abs(pBox->m_fSizeZ) * scale.z * 0.5f};
    }
    return shape;
}

float Extent(const Shape& box, int nAxis) { return nAxis == 0 ? box.halfExtents.x : nAxis == 1 ? box.halfExtents.y : box.halfExtents.z; }

bool SphereSphere(const Shape& a, const Shape& b, Contact& contact) {
    const XMFLOAT3 delta = Sub(b.center, a.center);
    const float fDistance = Length(delta), fRadii = a.fRadius + b.fRadius;
    if (fDistance >= fRadii) return false;
    contact.normal = fDistance > CONTACT_EPSILON ? Mul(delta, 1.0f / fDistance) : XMFLOAT3{0, 1, 0};
    contact.fDepth = fRadii - fDistance;
    contact.point = Add(a.center, Mul(contact.normal, a.fRadius - contact.fDepth * 0.5f));
    return true;
}

// Box A, sphere B. Normal points from the box toward the sphere.
bool BoxSphere(const Shape& box, const Shape& sphere, Contact& contact) {
    const XMFLOAT3 delta = Sub(sphere.center, box.center);
    float aLocal[3], aClamped[3];
    bool bInside = true;
    for (int nAxis = 0; nAxis < 3; ++nAxis) {
        aLocal[nAxis] = Dot(delta, box.aAxes[nAxis]);
        aClamped[nAxis] = std::clamp(aLocal[nAxis], -Extent(box, nAxis), Extent(box, nAxis));
        if (aClamped[nAxis] != aLocal[nAxis]) bInside = false;
    }
    if (!bInside) {
        XMFLOAT3 closest = box.center;
        for (int nAxis = 0; nAxis < 3; ++nAxis) closest = Add(closest, Mul(box.aAxes[nAxis], aClamped[nAxis]));
        const XMFLOAT3 toSphere = Sub(sphere.center, closest);
        const float fDistance = Length(toSphere);
        if (fDistance >= sphere.fRadius) return false;
        contact.normal = fDistance > CONTACT_EPSILON ? Mul(toSphere, 1.0f / fDistance) : box.aAxes[1];
        contact.fDepth = sphere.fRadius - fDistance;
        contact.point = closest;
        return true;
    }
    // Centre inside the box: push out through the nearest face.
    int nBest = 0;
    float fBest = Extent(box, 0) - std::abs(aLocal[0]);
    for (int nAxis = 1; nAxis < 3; ++nAxis) {
        const float fFace = Extent(box, nAxis) - std::abs(aLocal[nAxis]);
        if (fFace < fBest) {
            fBest = fFace;
            nBest = nAxis;
        }
    }
    contact.normal = Mul(box.aAxes[nBest], aLocal[nBest] < 0 ? -1.0f : 1.0f);
    contact.fDepth = fBest + sphere.fRadius;
    contact.point = sphere.center;
    return true;
}

// Oriented boxes: separating axis test over 3 + 3 face axes and 9 edge axes;
// the axis of least overlap is the contact normal.
bool BoxBox(const Shape& a, const Shape& b, Contact& contact) {
    const XMFLOAT3 delta = Sub(b.center, a.center);
    float fBestOverlap = 3.4e38f;
    XMFLOAT3 bestAxis{0, 1, 0};
    auto testAxis = [&](XMFLOAT3 axis, float fBias) {
        const float fLength = Length(axis);
        if (fLength < 1e-4f) return true; // parallel edges: no information
        axis = Mul(axis, 1.0f / fLength);
        float fProjectA = 0.0f, fProjectB = 0.0f;
        for (int nAxis = 0; nAxis < 3; ++nAxis) {
            fProjectA += std::abs(Dot(a.aAxes[nAxis], axis)) * Extent(a, nAxis);
            fProjectB += std::abs(Dot(b.aAxes[nAxis], axis)) * Extent(b, nAxis);
        }
        const float fDistance = Dot(delta, axis);
        const float fOverlap = fProjectA + fProjectB - std::abs(fDistance);
        if (fOverlap < 0.0f) return false;
        // Face axes are preferred over edge axes of nearly equal depth (stable stacking).
        if (fOverlap * fBias < fBestOverlap) {
            fBestOverlap = fOverlap * fBias;
            bestAxis = fDistance < 0 ? Mul(axis, -1.0f) : axis;
            contact.fDepth = fOverlap;
        }
        return true;
    };
    for (int nAxis = 0; nAxis < 3; ++nAxis)
        if (!testAxis(a.aAxes[nAxis], 1.0f) || !testAxis(b.aAxes[nAxis], 1.0f)) return false;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (!testAxis(Cross(a.aAxes[i], b.aAxes[j]), 1.05f)) return false;
    contact.normal = bestAxis;
    // Approximate contact point: the deepest corner of B along -normal, moved
    // half the overlap back toward A.
    XMFLOAT3 support = b.center;
    for (int nAxis = 0; nAxis < 3; ++nAxis)
        support = Add(support, Mul(b.aAxes[nAxis], (Dot(b.aAxes[nAxis], bestAxis) > 0 ? -1.0f : 1.0f) * Extent(b, nAxis)));
    contact.point = Add(support, Mul(bestAxis, contact.fDepth * 0.5f));
    return true;
}

bool Collide(const Shape& a, const Shape& b, Contact& contact) {
    if (a.bSphere && b.bSphere) return SphereSphere(a, b, contact);
    if (!a.bSphere && !b.bSphere) return BoxBox(a, b, contact);
    if (!a.bSphere) return BoxSphere(a, b, contact);
    if (!BoxSphere(b, a, contact)) return false;
    contact.normal = Mul(contact.normal, -1.0f); // was box(B) -> sphere(A)
    return true;
}

std::uint64_t PairKey(int nA, int nB) {
    if (nA > nB) std::swap(nA, nB);
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(nA)) << 32) | static_cast<std::uint32_t>(nB);
}

template<class Callback> void ForEachComponent(Scene& scene, int nEntity, Callback&& callback) {
    if (nEntity < 0 || static_cast<size_t>(nEntity) >= scene.vEntities.size()) return;
    auto& entity = scene.vEntities[static_cast<size_t>(nEntity)];
    // Index loop: a callback may attach components (the vector can grow).
    for (size_t nIndex = 0; nIndex < entity.vRuntimeComponents.size(); ++nIndex)
        if (auto* pComponent = entity.vRuntimeComponents[nIndex].get()) callback(*pComponent, entity);
}

struct PairEvent {
    std::uint64_t uKey = 0;
    int nA = -1, nB = -1;
    Contact contact;
    float fSpeed = 0.0f;
};

// Collider with the Inspector values saved on the entity (edit mode).
std::unique_ptr<Collider> SavedCollider(const Entity& entity, const std::string& sName) {
    std::unique_ptr<Collider> pCollider;
    if (sName == "BoxCollider") pCollider = std::make_unique<BoxCollider>();
    else if (sName == "SphereCollider") pCollider = std::make_unique<SphereCollider>();
    else return nullptr;
    const auto saved = entity.componentProperties.find(sName);
    if (saved == entity.componentProperties.end()) return pCollider;
    for (auto& field : pCollider->EditorFields()) {
        const auto value = saved->second.find(field.sName);
        if (value == saved->second.end()) continue;
        try {
            if (field.type == EditorFieldType::Float) *static_cast<float*>(field.pValue) = std::stof(value->second);
            else if (field.type == EditorFieldType::Boolean) *static_cast<bool*>(field.pValue) = value->second == "true" || value->second == "1";
        } catch (...) { /* keep the default for a malformed value */ }
    }
    return pCollider;
}

void AppendOutline(const Shape& shape, std::vector<XMFLOAT3>& vSegments) {
    if (shape.bSphere) {
        // Three great circles (XY, YZ, XZ), like Unity's sphere gizmo.
        constexpr int SEGMENTS = 32;
        for (int nPlane = 0; nPlane < 3; ++nPlane) {
            for (int nIndex = 0; nIndex < SEGMENTS; ++nIndex) {
                for (const int nStep : {nIndex, nIndex + 1}) {
                    const float fAngle = XM_2PI * static_cast<float>(nStep) / SEGMENTS;
                    const float fC = std::cos(fAngle) * shape.fRadius, fS = std::sin(fAngle) * shape.fRadius;
                    const XMFLOAT3 offset = nPlane == 0 ? XMFLOAT3{fC, fS, 0} : nPlane == 1 ? XMFLOAT3{0, fC, fS} : XMFLOAT3{fC, 0, fS};
                    vSegments.push_back(Add(shape.center, offset));
                }
            }
        }
        return;
    }
    XMFLOAT3 aCorners[8];
    for (int nCorner = 0; nCorner < 8; ++nCorner) {
        aCorners[nCorner] = shape.center;
        for (int nAxis = 0; nAxis < 3; ++nAxis)
            aCorners[nCorner] = Add(aCorners[nCorner], Mul(shape.aAxes[nAxis], ((nCorner >> nAxis) & 1 ? 1.0f : -1.0f) * Extent(shape, nAxis)));
    }
    for (int nCorner = 0; nCorner < 8; ++nCorner)
        for (int nAxis = 0; nAxis < 3; ++nAxis)
            if (!((nCorner >> nAxis) & 1)) {
                vSegments.push_back(aCorners[nCorner]);
                vSegments.push_back(aCorners[nCorner | (1 << nAxis)]);
            }
}
} // namespace

std::vector<XMFLOAT3> ColliderOutlineSegments(const Scene& scene) {
    std::vector<XMFLOAT3> vSegments;
    std::vector<DrawItem> vWorld;
    for (size_t nEntity = 0; nEntity < scene.vEntities.size(); ++nEntity) {
        const auto& entity = scene.vEntities[nEntity];
        const bool bLive = entity.vRuntimeComponentNames == entity.vComponents && entity.vRuntimeComponents.size() == entity.vComponents.size();
        for (size_t nIndex = 0; nIndex < entity.vComponents.size(); ++nIndex) {
            const auto& sName = entity.vComponents[nIndex];
            if (sName != "BoxCollider" && sName != "SphereCollider") continue;
            if (vWorld.empty()) vWorld = ResolveWorldDraws(scene);
            const auto* pLive = bLive ? dynamic_cast<const Collider*>(entity.vRuntimeComponents[nIndex].get()) : nullptr;
            const bool bStarted = pLive && nIndex < entity.vRuntimeComponentStarted.size() && entity.vRuntimeComponentStarted[nIndex];
            std::unique_ptr<Collider> pSaved;
            if (!bStarted) pSaved = SavedCollider(entity, sName);
            const Collider& collider = bStarted ? *pLive : *pSaved;
            AppendOutline(MakeShape(vWorld[nEntity], collider), vSegments);
        }
    }
    return vSegments;
}

namespace physics {
XMFLOAT3 Gravity() noexcept { return g_gravity; }
void SetGravity(const XMFLOAT3& gravity) noexcept { g_gravity = gravity; }
} // namespace physics

void Rigidbody::AddForce(const XMFLOAT3& force, ForceMode mode) {
    const float fInverseMass = m_fMass > 0.0f ? 1.0f / m_fMass : 0.0f;
    switch (mode) {
    case ForceMode::FORCE: m_acceleration = Add(m_acceleration, Mul(force, fInverseMass)); break;
    case ForceMode::ACCELERATION: m_acceleration = Add(m_acceleration, force); break;
    case ForceMode::IMPULSE: m_velocity = Add(m_velocity, Mul(force, fInverseMass)); break;
    case ForceMode::VELOCITY_CHANGE: m_velocity = Add(m_velocity, force); break;
    }
}

void SimulatePhysics(Scene& scene, float fDeltaSeconds) {
    auto& state = scene.physics;
    state.fAccumulator = std::min(state.fAccumulator + std::max(fDeltaSeconds, 0.0f),
                                  PHYSICS_FIXED_DELTA * PHYSICS_MAX_STEPS_PER_FRAME);
    while (state.fAccumulator >= PHYSICS_FIXED_DELTA) {
        StepPhysics(scene, PHYSICS_FIXED_DELTA);
        state.fAccumulator -= PHYSICS_FIXED_DELTA;
    }
}

void StepPhysics(Scene& scene, float fFixedDelta) {
    const int nEntityCount = static_cast<int>(scene.vEntities.size());
    for (int nEntity = 0; nEntity < nEntityCount; ++nEntity)
        ForEachComponent(scene, nEntity, [&](Component& component, Entity& entity) { component.FixedUpdate(entity, fFixedDelta); });

    // Gather bodies and colliders (components exist once the scene ran a frame).
    std::vector<Rigidbody*> vBodies(scene.vEntities.size(), nullptr);
    std::vector<ColliderRef> vColliders;
    for (int nEntity = 0; nEntity < nEntityCount; ++nEntity) {
        auto& entity = scene.vEntities[static_cast<size_t>(nEntity)];
        vBodies[static_cast<size_t>(nEntity)] = GetComponent<Rigidbody>(entity);
        for (auto& pComponent : entity.vRuntimeComponents)
            if (auto* pCollider = dynamic_cast<Collider*>(pComponent.get())) vColliders.push_back({nEntity, pCollider});
    }
    bool bAnyBody = false;
    for (auto* pBody : vBodies) bAnyBody = bAnyBody || pBody;
    if (!bAnyBody) {
        scene.physics.vContacts.clear();
        scene.physics.vTriggers.clear();
        return;
    }

    // Integrate velocities and positions (semi-implicit Euler) in world space.
    auto vWorld = ResolveWorldDraws(scene);
    std::vector<bool> vMoved(vWorld.size(), false);
    for (int nEntity = 0; nEntity < nEntityCount; ++nEntity) {
        auto* pBody = vBodies[static_cast<size_t>(nEntity)];
        if (!pBody) continue;
        if (pBody->m_bIsKinematic) {
            pBody->m_acceleration = {0, 0, 0};
            continue; // moved by scripts only
        }
        XMFLOAT3 acceleration = pBody->m_acceleration;
        if (pBody->m_bUseGravity) acceleration = Add(acceleration, g_gravity);
        pBody->m_velocity = Add(pBody->m_velocity, Mul(acceleration, fFixedDelta));
        pBody->m_velocity = Mul(pBody->m_velocity, 1.0f / (1.0f + std::max(pBody->m_fDrag, 0.0f) * fFixedDelta));
        pBody->m_acceleration = {0, 0, 0};
        auto& position = vWorld[static_cast<size_t>(nEntity)].position;
        position = Add(position, Mul(pBody->m_velocity, fFixedDelta));
        vMoved[static_cast<size_t>(nEntity)] = true;
    }

    // Narrow phase + sequential impulse solver.
    std::vector<PairEvent> vContactEvents, vTriggerEvents;
    auto inverseMass = [&](int nEntity) { return vBodies[static_cast<size_t>(nEntity)] ? vBodies[static_cast<size_t>(nEntity)]->InverseMass() : 0.0f; };
    auto velocity = [&](int nEntity) { return vBodies[static_cast<size_t>(nEntity)] ? vBodies[static_cast<size_t>(nEntity)]->m_velocity : XMFLOAT3{0, 0, 0}; };
    for (int nIteration = 0; nIteration < PHYSICS_SOLVER_ITERATIONS; ++nIteration) {
        for (size_t i = 0; i < vColliders.size(); ++i) {
            for (size_t j = i + 1; j < vColliders.size(); ++j) {
                const int nA = vColliders[i].nEntity, nB = vColliders[j].nEntity;
                if (nA == nB) continue;
                if (!vBodies[static_cast<size_t>(nA)] && !vBodies[static_cast<size_t>(nB)]) continue; // static-static
                const bool bTrigger = vColliders[i].pCollider->m_bIsTrigger || vColliders[j].pCollider->m_bIsTrigger;
                const float fInvA = inverseMass(nA), fInvB = inverseMass(nB);
                if (!bTrigger && fInvA + fInvB <= 0.0f && nIteration > 0) continue; // kinematic vs static: events only
                Contact contact;
                if (!Collide(MakeShape(vWorld[static_cast<size_t>(nA)], *vColliders[i].pCollider),
                             MakeShape(vWorld[static_cast<size_t>(nB)], *vColliders[j].pCollider), contact))
                    continue;
                const std::uint64_t uKey = PairKey(nA, nB);
                auto& vEvents = bTrigger ? vTriggerEvents : vContactEvents;
                const XMFLOAT3 relative = Sub(velocity(nB), velocity(nA));
                const float fNormalSpeed = Dot(relative, contact.normal);
                if (nIteration == 0 && std::none_of(vEvents.begin(), vEvents.end(), [&](const PairEvent& e) { return e.uKey == uKey; }))
                    vEvents.push_back({uKey, nA, nB, contact, std::max(-fNormalSpeed, 0.0f)});
                if (bTrigger || fInvA + fInvB <= 0.0f) continue;

                // Push the bodies apart (split by inverse mass).
                const float fCorrection = std::max(contact.fDepth - PENETRATION_SLOP, 0.0f) * POSITION_CORRECTION / (fInvA + fInvB);
                if (fInvA > 0) {
                    auto& position = vWorld[static_cast<size_t>(nA)].position;
                    position = Sub(position, Mul(contact.normal, fCorrection * fInvA));
                }
                if (fInvB > 0) {
                    auto& position = vWorld[static_cast<size_t>(nB)].position;
                    position = Add(position, Mul(contact.normal, fCorrection * fInvB));
                }
                if (fNormalSpeed >= 0.0f) continue; // already separating

                // Normal impulse with bounciness (Unity combines with Maximum here).
                auto* pA = vBodies[static_cast<size_t>(nA)];
                auto* pB = vBodies[static_cast<size_t>(nB)];
                float fBounce = std::max(pA ? pA->m_fBounciness : 0.0f, pB ? pB->m_fBounciness : 0.0f);
                if (-fNormalSpeed < BOUNCE_THRESHOLD) fBounce = 0.0f;
                const float fImpulse = -(1.0f + std::clamp(fBounce, 0.0f, 1.0f)) * fNormalSpeed / (fInvA + fInvB);
                if (pA && fInvA > 0) pA->m_velocity = Sub(pA->m_velocity, Mul(contact.normal, fImpulse * fInvA));
                if (pB && fInvB > 0) pB->m_velocity = Add(pB->m_velocity, Mul(contact.normal, fImpulse * fInvB));

                // Coulomb friction along the sliding direction (average of both).
                const XMFLOAT3 after = Sub(velocity(nB), velocity(nA));
                XMFLOAT3 tangent = Sub(after, Mul(contact.normal, Dot(after, contact.normal)));
                const float fTangentSpeed = Length(tangent);
                if (fTangentSpeed < CONTACT_EPSILON) continue;
                tangent = Mul(tangent, 1.0f / fTangentSpeed);
                const float fFriction = 0.5f * ((pA ? pA->m_fFriction : 0.6f) + (pB ? pB->m_fFriction : 0.6f));
                const float fTangentImpulse = std::min(fTangentSpeed / (fInvA + fInvB), std::max(fFriction, 0.0f) * fImpulse);
                if (pA && fInvA > 0) pA->m_velocity = Add(pA->m_velocity, Mul(tangent, fTangentImpulse * fInvA));
                if (pB && fInvB > 0) pB->m_velocity = Sub(pB->m_velocity, Mul(tangent, fTangentImpulse * fInvB));
            }
        }
    }

    // Write moved bodies back as local (parent-relative) transforms.
    for (int nEntity = 0; nEntity < nEntityCount; ++nEntity) {
        if (!vMoved[static_cast<size_t>(nEntity)]) continue;
        auto& draw = scene.vEntities[static_cast<size_t>(nEntity)].draw;
        auto local = LocalDrawFromWorld(scene, nEntity, vWorld[static_cast<size_t>(nEntity)]);
        draw.position = local.position;
    }

    // Enter / Stay / Exit events for both objects of each pair.
    auto dispatch = [&](const std::vector<PairEvent>& vEvents, std::vector<std::uint64_t>& vPrevious, bool bTrigger) {
        std::vector<std::uint64_t> vCurrent;
        for (const auto& event : vEvents) vCurrent.push_back(event.uKey);
        std::sort(vCurrent.begin(), vCurrent.end());
        for (const auto& event : vEvents) {
            const bool bEnter = !std::binary_search(vPrevious.begin(), vPrevious.end(), event.uKey);
            for (const int nSelf : {event.nA, event.nB}) {
                const int nOther = nSelf == event.nA ? event.nB : event.nA;
                if (nOther < 0 || static_cast<size_t>(nOther) >= scene.vEntities.size()) continue;
                Collision collision;
                collision.nOtherIndex = nOther;
                collision.normal = nSelf == event.nA ? Mul(event.contact.normal, -1.0f) : event.contact.normal;
                collision.point = event.contact.point;
                collision.fRelativeSpeed = event.fSpeed;
                ForEachComponent(scene, nSelf, [&](Component& component, Entity& self) {
                    if (static_cast<size_t>(nOther) >= scene.vEntities.size()) return;
                    auto& other = scene.vEntities[static_cast<size_t>(nOther)];
                    collision.pOther = &other;
                    if (bTrigger) bEnter ? component.OnTriggerEnter(self, other) : component.OnTriggerStay(self, other);
                    else bEnter ? component.OnCollisionEnter(self, collision) : component.OnCollisionStay(self, collision);
                });
            }
        }
        for (const auto uKey : vPrevious) {
            if (std::binary_search(vCurrent.begin(), vCurrent.end(), uKey)) continue;
            const int nA = static_cast<int>(uKey >> 32), nB = static_cast<int>(uKey & 0xFFFFFFFFu);
            for (const int nSelf : {nA, nB}) {
                const int nOther = nSelf == nA ? nB : nA;
                ForEachComponent(scene, nSelf, [&](Component& component, Entity& self) {
                    if (static_cast<size_t>(nOther) >= scene.vEntities.size()) return;
                    auto& other = scene.vEntities[static_cast<size_t>(nOther)];
                    Collision collision;
                    collision.pOther = &other;
                    collision.nOtherIndex = nOther;
                    if (bTrigger) component.OnTriggerExit(self, other);
                    else component.OnCollisionExit(self, collision);
                });
            }
        }
        vPrevious = std::move(vCurrent);
    };
    dispatch(vContactEvents, scene.physics.vContacts, false);
    dispatch(vTriggerEvents, scene.physics.vTriggers, true);
}

} // namespace dxs
