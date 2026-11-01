//==================================================================================
//
// 回転する箱のコンポーネントのヘッダーファイル [BoxController.h]
//
// Inspector に public / private の値を出す例。
//
//==================================================================================
#pragma once
//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Engine.h"

class BoxController final : public dxs::Component {
public:
    float m_fRotationSpeed = 1.0f; // 回転の速さ（ラジアン / 秒）
    void Start(dxs::Entity& entity) override;
    void Update(dxs::Entity& entity, float fDeltaSeconds) override;

    std::vector<dxs::EditorField> EditorFields() override {
        // public の値はそのまま出せる。private の値も、クラスの中から
        // ここで返せば Inspector に公開できる（エディター用の公開指定のような書き方）。
        return {dxs::EditField("Rotation Speed", m_fRotationSpeed), dxs::EditField("Enabled", m_bEnabled)};
    }

private:
    bool m_bEnabled = true; // 回転するか（private だが Inspector に出す）
};
