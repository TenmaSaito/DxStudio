#pragma once
#include "Engine.h"

// ============================================================================
//  InspectorShowcase — Inspector に表示できるフィールドの種類をまとめたテンプレート
// ============================================================================
//  Add Component > Scripts > InspectorShowcase で付けると、次の行が表示されます。
//
//  書き方                                            Inspector の表示
//  ---------------------------------------------------------------------------
//  EditField("Speed", m_fSpeed)                      数値（ドラッグ / ホイールで調整）
//  EditRange("Volume", m_fVolume, 0.0f, 1.0f)        スライダー + 数値（Unity の [Range]）
//  EditEnum("Mode", m_mode, {"Idle", "Walk", "Run"}) ドロップダウン
//  EditField("Offset", m_offset)                     XMFLOAT3 → X Y Z を 1 行に
//  EditField("Size", m_size)                         XMFLOAT2 → X Y を 1 行に
//  EditField("Waypoints", m_vWaypoints)              std::vector → ▼ 折りたたみ + Size + Element 0..
//  EditField("Slots", m_aSlots)                      std::array  → Size は固定（読み取り専用）
//  EditEnum("Pattern", m_vPattern, {...})            enum の配列
//
//  ・配列の Size を変えると要素の行が増減します（最大 4096）。
//  ・値はシーンに "Waypoints[0]" のようなパスで保存され、Play 開始時（Start の前）に
//    復元されます。Play 中は実行中の値がリアルタイムに表示されます。
//  ・配列の要素に使える型: float / int / bool / std::string / XMFLOAT2 / XMFLOAT3 / enum

class InspectorShowcase final : public dxs::Component {
public:
    enum class MODE : unsigned char { IDLE, WALK, RUN };

    void Start(dxs::Entity& entity) override;
    void Update(dxs::Entity& entity, float fDeltaSeconds) override;
    std::vector<dxs::EditorField> EditorFields() override {
        return {dxs::EditField("Speed", m_fSpeed),
                dxs::EditRange("Volume", m_fVolume, 0.0f, 1.0f),
                dxs::EditRange("Lives", m_nLives, 1, 9),
                dxs::EditEnum("Mode", m_mode, {"Idle", "Walk", "Run"}),
                dxs::EditField("Offset", m_offset),
                dxs::EditField("Size", m_size),
                dxs::EditField("Visible", m_bVisible),
                dxs::EditField("Label", m_sLabel),
                dxs::EditField("Waypoints", m_vWaypoints),
                dxs::EditField("Slots", m_aSlots),
                dxs::EditEnum("Pattern", m_vPattern, {"Idle", "Walk", "Run"}),
                dxs::EditField("Elapsed", m_fElapsed, true)}; // read-only
    }

private:
    float m_fSpeed = 1.5f;
    float m_fVolume = 0.8f;
    int m_nLives = 3;
    MODE m_mode = MODE::WALK;
    DirectX::XMFLOAT3 m_offset{0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT2 m_size{1.0f, 1.0f};
    bool m_bVisible = true;
    std::string m_sLabel = "Player";
    std::vector<DirectX::XMFLOAT3> m_vWaypoints{{0, 0, 0}, {2, 0, 0}, {2, 0, 2}};
    std::array<int, 3> m_aSlots{10, 20, 30};
    std::vector<MODE> m_vPattern{MODE::IDLE, MODE::RUN};
    float m_fElapsed = 0.0f;
    size_t m_nWaypoint = 0;
};
