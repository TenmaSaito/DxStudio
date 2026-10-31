// Rigidbody / collider simulation and collision / trigger callbacks.

#include "Engine.h"

#include <cmath>
#include <iostream>

namespace {
class HitCounter final : public dxs::Component {
public:
    int m_nEnter = 0, m_nStay = 0, m_nExit = 0, m_nTriggerEnter = 0, m_nTriggerExit = 0, m_nFixed = 0;
    float m_fNormalY = 0.0f;
    void Update(dxs::Entity&, float) override {}
    void FixedUpdate(dxs::Entity&, float) override { ++m_nFixed; }
    void OnCollisionEnter(dxs::Entity&, const dxs::Collision& collision) override {
        ++m_nEnter;
        m_fNormalY = collision.normal.y;
    }
    void OnCollisionStay(dxs::Entity&, const dxs::Collision&) override { ++m_nStay; }
    void OnCollisionExit(dxs::Entity&, const dxs::Collision&) override { ++m_nExit; }
    void OnTriggerEnter(dxs::Entity&, dxs::Entity&) override { ++m_nTriggerEnter; }
    void OnTriggerExit(dxs::Entity&, dxs::Entity&) override { ++m_nTriggerExit; }
};
DXS_REGISTER_COMPONENT(HitCounter)

int g_nFailures = 0;
void Check(bool bCondition, const char* pWhat) {
    if (bCondition) return;
    std::cerr << "FAILED: " << pWhat << '\n';
    ++g_nFailures;
}

dxs::Entity MakeEntity(const char* pName, DirectX::XMFLOAT3 position, std::vector<std::string> vComponents) {
    dxs::Entity entity;
    entity.sName = pName;
    entity.draw.position = position;
    entity.vComponents = std::move(vComponents);
    return entity;
}

dxs::Entity Floor() {
    auto floor = MakeEntity("Floor", {0, 0, 0}, {"BoxCollider"});
    floor.draw.scale = {20, 1, 20}; // top face at y = 0.5
    return floor;
}

void Run(dxs::Scene& scene, float fSeconds) {
    for (float fTime = 0; fTime < fSeconds; fTime += 1.0f / 60.0f) dxs::UpdateUserComponents(scene, 1.0f / 60.0f);
}
} // namespace

int main() {
    using dxs::Entity;
    // 1. A sphere falls onto a static floor and comes to rest on it.
    {
        dxs::Scene scene;
        scene.vEntities.push_back(Floor());
        scene.vEntities.push_back(MakeEntity("Ball", {0, 4, 0}, {"SphereCollider", "Rigidbody", "HitCounter"}));
        Run(scene, 0.3f);
        Check(scene.vEntities[1].draw.position.y < 3.8f, "ball falls under gravity");
        Run(scene, 3.0f);
        const auto& ball = scene.vEntities[1];
        Check(std::abs(ball.draw.position.y - 1.0f) < 0.03f, "ball rests on the floor (y = 1)");
        auto* pBody = dxs::GetComponent<dxs::Rigidbody>(scene.vEntities[1]);
        Check(pBody && std::abs(pBody->Velocity().y) < 0.05f, "resting ball has no vertical velocity");
        auto* pCounter = dxs::GetComponent<HitCounter>(scene.vEntities[1]);
        Check(pCounter && pCounter->m_nEnter == 1, "exactly one OnCollisionEnter while resting");
        Check(pCounter && pCounter->m_nStay > 10, "OnCollisionStay every step while resting");
        Check(pCounter && pCounter->m_nExit == 0, "no OnCollisionExit while resting");
        Check(pCounter && pCounter->m_fNormalY > 0.9f, "collision normal points up, away from the floor");
        Check(pCounter && pCounter->m_nFixed >= 150 && pCounter->m_nFixed <= 180, "FixedUpdate runs at 50 Hz");
        Check(scene.vEntities[0].draw.position.y == 0.0f, "static floor never moves");
        // Lifting the ball off the floor reports OnCollisionExit.
        pBody->AddForce({0, 8, 0}, dxs::ForceMode::VELOCITY_CHANGE);
        Run(scene, 0.2f);
        Check(pCounter->m_nExit == 1, "jumping off the floor reports OnCollisionExit");
    }
    // 2. A box lands flat; a box rotated 45 degrees around Y lands too (OBB).
    {
        dxs::Scene scene;
        scene.vEntities.push_back(Floor());
        scene.vEntities.push_back(MakeEntity("Box", {-2, 3, 0}, {"BoxCollider", "Rigidbody"}));
        auto rotated = MakeEntity("Rotated", {2, 3, 0}, {"BoxCollider", "Rigidbody"});
        rotated.draw.rotation = {0, DirectX::XM_PIDIV4, 0};
        scene.vEntities.push_back(rotated);
        auto tilted = MakeEntity("Tilted", {0, 3, 3}, {"BoxCollider", "Rigidbody"});
        tilted.draw.rotation = {0, 0, DirectX::XM_PIDIV4}; // stands on an edge: lowest point at y - 0.707
        scene.vEntities.push_back(tilted);
        Run(scene, 3.0f);
        Check(std::abs(scene.vEntities[1].draw.position.y - 1.0f) < 0.03f, "box rests on the floor (y = 1)");
        Check(std::abs(scene.vEntities[2].draw.position.y - 1.0f) < 0.03f, "Y-rotated box rests on the floor");
        Check(std::abs(scene.vEntities[3].draw.position.y - (0.5f + 0.7071f)) < 0.04f, "Z-rotated box rests on its edge");
        Check(std::abs(scene.vEntities[1].draw.position.x + 2.0f) < 0.01f, "box does not slide sideways");
    }
    // 3. Bounciness 1 bounces back up; a kinematic body ignores gravity.
    {
        dxs::Scene scene;
        scene.vEntities.push_back(Floor());
        auto ball = MakeEntity("Bouncy", {0, 4, 0}, {"SphereCollider", "Rigidbody"});
        ball.componentProperties["Rigidbody"]["Bounciness"] = "1";
        scene.vEntities.push_back(ball);
        auto kinematic = MakeEntity("Platform", {3, 4, 0}, {"BoxCollider", "Rigidbody"});
        kinematic.componentProperties["Rigidbody"]["Is Kinematic"] = "true";
        scene.vEntities.push_back(kinematic);
        Run(scene, 1.0f); // falls 3 m in ~0.78 s, then bounces
        float fHighest = 0.0f;
        for (int nFrame = 0; nFrame < 40; ++nFrame) {
            Run(scene, 1.0f / 60.0f);
            fHighest = std::max(fHighest, scene.vEntities[1].draw.position.y);
        }
        Check(fHighest > 2.0f, "bounciness 1 bounces the ball back up");
        Check(scene.vEntities[2].draw.position.y == 4.0f, "kinematic body is not moved by gravity");
    }
    // 4. Triggers report enter / exit and do not block.
    {
        dxs::Scene scene;
        auto zone = MakeEntity("Zone", {0, 2, 0}, {"BoxCollider"});
        zone.componentProperties["BoxCollider"]["Is Trigger"] = "true";
        scene.vEntities.push_back(zone);
        scene.vEntities.push_back(MakeEntity("Ball", {0, 4, 0}, {"SphereCollider", "Rigidbody", "HitCounter"}));
        Run(scene, 2.0f);
        auto* pCounter = dxs::GetComponent<HitCounter>(scene.vEntities[1]);
        Check(scene.vEntities[1].draw.position.y < 0.0f, "trigger does not block the ball");
        Check(pCounter && pCounter->m_nTriggerEnter == 1 && pCounter->m_nTriggerExit == 1, "OnTriggerEnter and OnTriggerExit once each");
        Check(pCounter && pCounter->m_nEnter == 0, "triggers do not send OnCollisionEnter");
    }
    // 5. A dynamic box pushes another (momentum is shared by mass).
    {
        dxs::Scene scene;
        dxs::physics::SetGravity({0, 0, 0});
        scene.vEntities.push_back(MakeEntity("A", {-2, 0, 0}, {"BoxCollider", "Rigidbody"}));
        scene.vEntities.push_back(MakeEntity("B", {0, 0, 0}, {"BoxCollider", "Rigidbody"}));
        Run(scene, 1.0f / 60.0f);
        dxs::GetComponent<dxs::Rigidbody>(scene.vEntities[0])->SetVelocity({4, 0, 0});
        Run(scene, 1.0f);
        const float fVa = dxs::GetComponent<dxs::Rigidbody>(scene.vEntities[0])->Velocity().x;
        const float fVb = dxs::GetComponent<dxs::Rigidbody>(scene.vEntities[1])->Velocity().x;
        Check(std::abs(fVa + fVb - 4.0f) < 0.05f, "momentum is conserved in a head-on collision");
        Check(fVb > 1.9f, "the struck box moves away");
        Check(scene.vEntities[1].draw.position.x - scene.vEntities[0].draw.position.x >= 0.99f, "boxes do not overlap");
        dxs::physics::SetGravity({0, -9.81f, 0});
    }
    // 6. Collider outlines for the Scene view (edit mode uses saved values).
    {
        dxs::Scene scene;
        auto box = MakeEntity("Box", {0, 0, 0}, {"BoxCollider"});
        box.componentProperties["BoxCollider"]["Size X"] = "4";
        scene.vEntities.push_back(box);
        scene.vEntities.push_back(MakeEntity("Ball", {0, 0, 0}, {"SphereCollider"}));
        const auto vLines = dxs::ColliderOutlineSegments(scene);
        Check(vLines.size() == 24 + 3 * 32 * 2, "box (12 edges) and sphere (3 circles) outlines");
        float fMaxX = 0;
        for (size_t n = 0; n < 24; ++n) fMaxX = std::max(fMaxX, vLines[n].x);
        Check(std::abs(fMaxX - 2.0f) < 1e-4f, "box outline uses the saved Size X");
    }
    if (g_nFailures) return 1;
    std::cout << "Physics (gravity, resting contact, OBB, bounce, kinematic, triggers, momentum, outlines) tests passed.\n";
    return 0;
}
