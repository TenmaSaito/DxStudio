//==================================================================================
//
// 物理のテスト [PhysicsTests.cpp]
//
// Rigidbody / コライダーのシミュレーションと、衝突 / トリガーのコールバック。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
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

//==================================================================================
// --- 確認処理 ---
//==================================================================================
void Check(bool bCondition, const char* pWhat) {
    if (bCondition) return;
    std::cerr << "FAILED: " << pWhat << '\n';
    ++g_nFailures;
}

//==================================================================================
// --- テスト用のエンティティの作成処理 ---
//==================================================================================
dxs::Entity MakeEntity(const char* pName, DirectX::XMFLOAT3 position, std::vector<std::string> vComponents) {
    dxs::Entity entity;
    entity.sName = pName;
    entity.draw.position = position;
    entity.vComponents = std::move(vComponents);
    return entity;
}

//==================================================================================
// --- 床の作成処理 ---
//==================================================================================
dxs::Entity Floor() {
    auto floor = MakeEntity("Floor", {0, 0, 0}, {"BoxCollider"});
    floor.draw.scale = {20, 1, 20}; // 上の面は y = 0.5
    return floor;
}

//==================================================================================
// --- fSeconds 秒分のシミュレーション処理 ---
//==================================================================================
void Run(dxs::Scene& scene, float fSeconds) {
    for (float fTime = 0; fTime < fSeconds; fTime += 1.0f / 60.0f) {
        dxs::UpdateUserComponents(scene, 1.0f / 60.0f);
    }
}
} // namespace

//==================================================================================
// --- メイン関数 ---
//==================================================================================
int main() {
    using dxs::Entity;

    // 1. 球が動かない床に落ち、その上で止まる
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

        // 球を床から持ち上げると OnCollisionExit が呼ばれる
        pBody->AddForce({0, 8, 0}, dxs::ForceMode::VELOCITY_CHANGE);
        Run(scene, 0.2f);
        Check(pCounter->m_nExit == 1, "jumping off the floor reports OnCollisionExit");
    }

    // 2. 箱が平らに着地し、45 度回転した箱も着地する（OBB）
    {
        dxs::Scene scene;
        scene.vEntities.push_back(Floor());
        scene.vEntities.push_back(MakeEntity("Box", {-2, 3, 0}, {"BoxCollider", "Rigidbody"}));
        auto rotated = MakeEntity("Rotated", {2, 3, 0}, {"BoxCollider", "Rigidbody"});
        rotated.draw.rotation = {0, DirectX::XM_PIDIV4, 0};
        scene.vEntities.push_back(rotated);
        auto tilted = MakeEntity("Tilted", {0, 3, 3}, {"BoxCollider", "Rigidbody"});
        tilted.draw.rotation = {0, 0, DirectX::XM_PIDIV4}; // 辺で立つ: 一番低い点は y - 0.707
        scene.vEntities.push_back(tilted);
        Run(scene, 3.0f);
        Check(std::abs(scene.vEntities[1].draw.position.y - 1.0f) < 0.03f, "box rests on the floor (y = 1)");
        Check(std::abs(scene.vEntities[2].draw.position.y - 1.0f) < 0.03f, "Y-rotated box rests on the floor");
        Check(std::abs(scene.vEntities[3].draw.position.y - (0.5f + 0.7071f)) < 0.04f, "Z-rotated box rests on its edge");
        Check(std::abs(scene.vEntities[1].draw.position.x + 2.0f) < 0.01f, "box does not slide sideways");
    }

    // 3. Bounciness 1 なら跳ね返り、Kinematic の物体は重力を受けない
    {
        dxs::Scene scene;
        scene.vEntities.push_back(Floor());
        auto ball = MakeEntity("Bouncy", {0, 4, 0}, {"SphereCollider", "Rigidbody"});
        ball.componentProperties["Rigidbody"]["Bounciness"] = "1";
        scene.vEntities.push_back(ball);
        auto kinematic = MakeEntity("Platform", {3, 4, 0}, {"BoxCollider", "Rigidbody"});
        kinematic.componentProperties["Rigidbody"]["Is Kinematic"] = "true";
        scene.vEntities.push_back(kinematic);
        Run(scene, 1.0f); // 約 0.78 秒で 3m 落ちてから跳ねる
        float fHighest = 0.0f;
        for (int nFrame = 0; nFrame < 40; ++nFrame) {
            Run(scene, 1.0f / 60.0f);
            fHighest = std::max(fHighest, scene.vEntities[1].draw.position.y);
        }

        Check(fHighest > 2.0f, "bounciness 1 bounces the ball back up");
        Check(scene.vEntities[2].draw.position.y == 4.0f, "kinematic body is not moved by gravity");
    }

    // 4. トリガーは入った / 出たことを知らせ、押し返さない
    {
        dxs::Scene scene;
        auto zone = MakeEntity("Zone", {0, 2, 0}, {"BoxCollider"});
        zone.componentProperties["BoxCollider"]["Is Trigger"] = "true";
        scene.vEntities.push_back(zone);
        scene.vEntities.push_back(MakeEntity("Ball", {0, 4, 0}, {"SphereCollider", "Rigidbody", "HitCounter"}));
        Run(scene, 2.0f);
        auto* pCounter = dxs::GetComponent<HitCounter>(scene.vEntities[1]);
        Check(scene.vEntities[1].draw.position.y < 0.0f, "trigger does not block the ball");
        Check(pCounter && pCounter->m_nTriggerEnter == 1 && pCounter->m_nTriggerExit == 1,
              "OnTriggerEnter and OnTriggerExit once each");
        Check(pCounter && pCounter->m_nEnter == 0, "triggers do not send OnCollisionEnter");
    }

    // 5. 動く箱がもう 1 つを押す（運動量は質量に応じて分かれる）
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

    // 6. シーンビューのコライダーの輪郭（編集中は保存値を使う）
    {
        dxs::Scene scene;
        auto box = MakeEntity("Box", {0, 0, 0}, {"BoxCollider"});
        box.componentProperties["BoxCollider"]["Size X"] = "4";
        scene.vEntities.push_back(box);
        scene.vEntities.push_back(MakeEntity("Ball", {0, 0, 0}, {"SphereCollider"}));
        const auto vLines = dxs::ColliderOutlineSegments(scene);
        Check(vLines.size() == 24 + 3 * 32 * 2, "box (12 edges) and sphere (3 circles) outlines");
        float fMaxX = 0;
        for (size_t n = 0; n < 24; ++n) {
            fMaxX = std::max(fMaxX, vLines[n].x);
        }

        Check(std::abs(fMaxX - 2.0f) < 1e-4f, "box outline uses the saved Size X");
    }

    // 7. MeshCollider: 動かない凹んだメッシュの床と、凸包のメッシュの物体
    {
        dxs::Scene scene;
        scene.projectRoot = "project"; // 組み込みの基本図形は project/Assets/Primitives にある
        auto ground = MakeEntity("MeshFloor", {0, 0, 0}, {"MeshCollider"});
        ground.renderer = Entity::RendererKind::Mesh;
        ground.sModelAsset = dxs::PrimitiveAssetPath(dxs::PrimitiveShape::CUBE);
        ground.draw.scale = {20, 1, 20};
        scene.vEntities.push_back(ground);
        scene.vEntities.push_back(MakeEntity("Ball", {-3, 3, 0}, {"SphereCollider", "Rigidbody"}));
        scene.vEntities.push_back(MakeEntity("Box", {0, 3, 0}, {"BoxCollider", "Rigidbody"}));
        auto convex = MakeEntity("ConvexSphere", {3, 3, 0}, {"MeshCollider", "Rigidbody"});
        convex.renderer = Entity::RendererKind::Mesh;
        convex.sModelAsset = dxs::PrimitiveAssetPath(dxs::PrimitiveShape::SPHERE);
        convex.componentProperties["MeshCollider"]["Convex"] = "true";
        scene.vEntities.push_back(convex);
        auto pyramid = MakeEntity("ConvexPyramid", {6, 3, 0}, {"MeshCollider", "Rigidbody"});
        pyramid.renderer = Entity::RendererKind::Mesh;
        pyramid.sModelAsset = dxs::PrimitiveAssetPath(dxs::PrimitiveShape::PYRAMID);
        pyramid.componentProperties["MeshCollider"]["Convex"] = "true";
        scene.vEntities.push_back(pyramid);
        Run(scene, 3.0f);
        Check(std::abs(scene.vEntities[1].draw.position.y - 1.0f) < 0.03f, "sphere rests on a concave MeshCollider");
        Check(std::abs(scene.vEntities[2].draw.position.y - 1.0f) < 0.03f, "box rests on a concave MeshCollider");
        Check(std::abs(scene.vEntities[3].draw.position.y - 1.0f) < 0.06f, "convex sphere mesh rests on a mesh floor");
        Check(std::abs(scene.vEntities[4].draw.position.y - 1.0f) < 0.03f, "convex pyramid mesh rests on its base");
        Check(!dxs::ColliderOutlineSegments(scene).empty(), "mesh collider outline");
    }

    // 8. コライダーの組み合わせ: 1 つの物体に 2 つの形、および子のコライダー
    //    （Rigidbody なし）が親の一部になる複合の物体
    {
        dxs::Scene scene;
        scene.vEntities.push_back(Floor());
        auto multi = MakeEntity("Multi", {-4, 4, 0}, {"BoxCollider", "SphereCollider", "Rigidbody"});
        multi.componentProperties["SphereCollider"]["Center Y"] = "-1"; // 箱の下の「足」
        scene.vEntities.push_back(multi);
        scene.vEntities.push_back(MakeEntity("Parent", {3, 4, 0}, {"BoxCollider", "Rigidbody", "HitCounter"}));
        auto child = MakeEntity("Child", {0.8f, 0, 0}, {"SphereCollider", "HitCounter"}); // 親の箱と重なる
        child.nParent = 2;
        scene.vEntities.push_back(child);
        Run(scene, 3.0f);
        Check(std::abs(scene.vEntities[1].draw.position.y - 2.0f) < 0.03f, "box + sphere foot on one object rests on the foot");
        Check(std::abs(scene.vEntities[2].draw.position.y - 1.0f) < 0.03f, "compound body rests on the floor");
        Check(std::abs(scene.vEntities[2].draw.position.x - 3.0f) < 0.01f,
              "overlapping parts of one body do not push each other");
        Check(scene.vEntities[3].draw.position.x == 0.8f && scene.vEntities[3].draw.position.y == 0.0f,
              "child keeps its local transform");
        const auto vWorld = dxs::ResolveWorldDraws(scene);
        Check(std::abs(vWorld[3].position.y - 1.0f) < 0.03f, "child collider moved with its parent body");
        auto* pParentHits = dxs::GetComponent<HitCounter>(scene.vEntities[2]);
        Check(pParentHits && pParentHits->m_nEnter == 1, "compound body gets one OnCollisionEnter for the floor");
    }

    if (g_nFailures) return 1;
    std::cout << "Physics (gravity, resting contact, OBB, bounce, kinematic, triggers, momentum, outlines, mesh / convex / "
                 "compound colliders) tests passed.\n";
    return 0;
}
