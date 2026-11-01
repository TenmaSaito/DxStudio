//==================================================================================
//
// コルーチンのテンプレートのソースファイル [CoroutineTemplate.cpp]
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "CoroutineTemplate.h"

#include <algorithm>

// ---------------------------------------------------------------------------
// Start: コルーチンを開始する場所（Unity の Start と同じ）
// ---------------------------------------------------------------------------
void CoroutineTemplate::Start(dxs::Entity& entity) {
    // 戻り値のハンドルを保存しておくと、後で StopCoroutine で止められます。
    if (m_bBlink) {
        m_blinkHandle = StartCoroutine(BlinkForever(entity));
    }

    // ハンドルが不要なら、戻り値を使わなくて構いません。
    StartCoroutine(MainSequence(entity));
}

// Update は毎フレーム、コルーチンより先に呼ばれます。
void CoroutineTemplate::Update(dxs::Entity&, float fDeltaSeconds) {
    m_fElapsed += fDeltaSeconds;
}

// LateUpdate は Update とコルーチンの後に呼ばれます（カメラ追従などに使用）。
void CoroutineTemplate::LateUpdate(dxs::Entity&, float) {}

// ---------------------------------------------------------------------------
// 手順 1〜5 を上から順に実行するコルーチン
// ---------------------------------------------------------------------------
dxs::Coroutine CoroutineTemplate::MainSequence(dxs::Entity& entity) {
    // 1. 指定秒数待つ。WaitForSeconds は Time::SetTimeScale の影響を受けます。
    m_nStep = 1;
    co_yield dxs::WaitForSeconds(0.5f);

    // 2. 子コルーチンを呼び、完了するまで待つ（Unity: yield return StartCoroutine(...)）。
    m_nStep = 2;
    co_yield MoveBy(entity, m_fMoveDistance, m_fMoveSeconds);

    // 3. 1 フレームずつ処理を進める（Unity: yield return null）。
    m_nStep = 3;
    for (int nFrame = 0; nFrame < m_nSpinFrames; ++nFrame) {
        entity.draw.rotation.y += 0.05f;
        co_yield nullptr; // dxs::NextFrame と書いても同じ
    }

    // 4. 条件を待つ。
    m_nStep = 4;
    co_yield dxs::WaitUntil([this] { return m_fElapsed >= 3.0f; });            // 条件が true になるまで
    co_yield dxs::WaitWhile([&entity] { return entity.draw.scale.x > 1.5f; }); // 条件が false になるまで
    co_yield dxs::WaitForFrames(10);                                           // 10 フレーム待つ
    co_yield dxs::WaitForSecondsRealtime(0.25f);                               // TimeScale の影響を受けない

    // 5. 別のコルーチンを止める（Unity: StopCoroutine）。
    m_nStep = 5;
    if (IsCoroutineRunning(m_blinkHandle)) {
        StopCoroutine(m_blinkHandle);
    }

    entity.draw.bVisible = true;

    // ここで関数を抜けるとコルーチンは終了します。途中で終える場合は co_return;
    // すべてのコルーチンを止めたいときは StopAllCoroutines(); を呼びます。
    co_return;
}

// ---------------------------------------------------------------------------
// 子コルーチン: fSeconds 秒かけて X 方向へ fDistance 移動する
// ---------------------------------------------------------------------------
dxs::Coroutine CoroutineTemplate::MoveBy(dxs::Entity& entity, float fDistance, float fSeconds) {
    const float fStartX = entity.draw.position.x;
    float fTime = 0.0f;
    while (fTime < fSeconds) {
        fTime += dxs::Time::DeltaTime(); // 今フレームの経過秒（TimeScale 適用後）
        const float fRate = std::clamp(fTime / std::max(fSeconds, 0.0001f), 0.0f, 1.0f);
        entity.draw.position.x = fStartX + fDistance * fRate;
        co_yield nullptr;
    }
}

// ---------------------------------------------------------------------------
// 止められるまで続くコルーチン（無限ループでも毎回 co_yield すれば問題ありません）
// ---------------------------------------------------------------------------
dxs::Coroutine CoroutineTemplate::BlinkForever(dxs::Entity& entity) {
    while (true) {
        entity.draw.bVisible = !entity.draw.bVisible;
        co_yield dxs::WaitForSeconds(0.2f);
    }
}

DXS_REGISTER_COMPONENT(CoroutineTemplate)
