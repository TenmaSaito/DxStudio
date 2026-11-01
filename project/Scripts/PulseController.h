//==================================================================================
//
// 伸び縮みするコンポーネントのヘッダーファイル [PulseController.h]
//
// コルーチンの例（Unity の IEnumerator + StartCoroutine）。
// 物体に「PulseController」を付けて Play を押すと、大きくなる → 待つ → 小さくなる →
// 3 回点滅する、を繰り返す。Inspector の値は変更できる。
//
//==================================================================================
#pragma once
//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Engine.h"
class PulseController final : public dxs::Component {
public:
    void Start(dxs::Entity& entity) override;
    void Update(dxs::Entity& entity, float fDeltaSeconds) override;

    std::vector<dxs::EditorField> EditorFields() override {
        return {dxs::EditField("Pulse Seconds", m_fPulseSeconds), dxs::EditField("Wait Seconds", m_fWaitSeconds),
                dxs::EditField("Blink Count", m_nBlinkCount)};
    }

private:
    dxs::Coroutine PulseLoop(dxs::Entity& entity);              // 繰り返しの本体
    dxs::Coroutine ScaleTo(dxs::Entity& entity, float fTarget); // 拡大率を fTarget まで変える
    dxs::Coroutine Blink(dxs::Entity& entity);                  // 点滅する

    float m_fPulseSeconds = 0.5f; // 大きさを変えるのにかける秒数
    float m_fWaitSeconds = 1.0f;  // 大きいまま待つ秒数
    int m_nBlinkCount = 3;        // 点滅する回数
};
