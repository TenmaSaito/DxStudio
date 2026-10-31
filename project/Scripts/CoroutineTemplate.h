#pragma once
#include "Engine.h"

// ============================================================================
//  CoroutineTemplate — DxStudio コルーチンの使い方をまとめたテンプレート
// ============================================================================
//  このファイルをコピーしてクラス名を変えれば、そのまま自分のコンポーネントに
//  なります。Inspector の Add Component > Scripts > CoroutineTemplate で
//  アタッチし、Play を押すと次の順で動きます。
//
//    1. 0.5 秒待ってから右へ移動（WaitForSeconds）
//    2. 1 フレームずつ回転（co_yield nullptr）
//    3. 別のコルーチンを呼んで完了を待つ（co_yield 子コルーチン）
//    4. 条件が成立するまで待つ（WaitUntil / WaitWhile）
//    5. 点滅コルーチンを止める（StopCoroutine）
//
//  Unity C# との対応
//  ---------------------------------------------------------------------------
//  IEnumerator Foo()                       dxs::Coroutine Foo(dxs::Entity& entity)
//  yield return null;                      co_yield nullptr;   (または dxs::NextFrame)
//  yield return new WaitForSeconds(1);     co_yield dxs::WaitForSeconds(1.0f);
//  WaitForSecondsRealtime(1)               co_yield dxs::WaitForSecondsRealtime(1.0f);
//  (n フレーム待つ)                        co_yield dxs::WaitForFrames(n);
//  yield return new WaitUntil(() => b);    co_yield dxs::WaitUntil([&] { return b; });
//  yield return new WaitWhile(() => b);    co_yield dxs::WaitWhile([&] { return b; });
//  yield return StartCoroutine(Sub());     co_yield Sub(entity);
//  yield break;                            co_return;
//  Coroutine c = StartCoroutine(Foo());    dxs::CoroutineHandle h = StartCoroutine(Foo(entity));
//  StopCoroutine(c);                       StopCoroutine(h);
//  StopAllCoroutines();                    StopAllCoroutines();
//  Time.deltaTime / Time.timeScale         dxs::Time::DeltaTime() / dxs::Time::SetTimeScale()
//
//  注意
//  ---------------------------------------------------------------------------
//  ・コルーチンはメンバー関数にすると、メンバー変数（m_...）をそのまま使えます。
//    コルーチンは Component が持つので、Component より長く生きることはありません。
//  ・ラムダをコルーチンにする場合、ラムダのキャプチャは使わず引数で渡してください
//    （ラムダ本体が先に消えると、キャプチャした値も消えるためです）。
//  ・StartCoroutine は最初の co_yield まで即座に実行し、その後は毎フレーム
//    Update の後に再開します（Unity と同じ順序）。
//  ・コルーチン内で例外が起きると、そのコルーチンだけが止まり Console に出ます。
// ============================================================================
class CoroutineTemplate final : public dxs::Component {
public:
    void Start(dxs::Entity& entity) override;
    void Update(dxs::Entity& entity, float fDeltaSeconds) override;
    void LateUpdate(dxs::Entity& entity, float fDeltaSeconds) override;

    // Inspector に表示する値（Play 中は実行中の値が表示されます）。
    std::vector<dxs::EditorField> EditorFields() override {
        return {dxs::EditField("Move Distance", m_fMoveDistance), dxs::EditField("Move Seconds", m_fMoveSeconds),
                dxs::EditField("Spin Frames", m_nSpinFrames), dxs::EditField("Blink", m_bBlink),
                dxs::EditField("Step", m_nStep, true /* 読み取り専用 */)};
    }

private:
    // ---- コルーチン本体 ----
    dxs::Coroutine MainSequence(dxs::Entity& entity); // 手順 1〜5 を順に実行
    dxs::Coroutine MoveBy(dxs::Entity& entity, float fDistance, float fSeconds); // 子コルーチン
    dxs::Coroutine BlinkForever(dxs::Entity& entity); // 止めるまで続くコルーチン

    float m_fMoveDistance = 2.0f;
    float m_fMoveSeconds = 1.0f;
    int m_nSpinFrames = 60;
    bool m_bBlink = true;
    int m_nStep = 0;          // いま何番目の手順か（Inspector で確認用）
    float m_fElapsed = 0.0f;  // Update で数える経過時間（WaitUntil の例で使用）
    dxs::CoroutineHandle m_blinkHandle; // StopCoroutine 用に保持するハンドル
};
