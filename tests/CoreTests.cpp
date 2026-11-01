//==================================================================================
//
// エンジンの基本機能のテスト [CoreTests.cpp]
//
// デリゲート、コールバックのインターフェース、Unity 風のコルーチン、ログのスレッド。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Engine.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
int g_nFailures = 0;

//==================================================================================
// --- 確認処理（失敗したら内容を出して終了） ---
//==================================================================================
void Check(bool bCondition, const char* pWhat) {
    if (!bCondition) {
        std::cerr << "FAILED: " << pWhat << '\n';
        ++g_nFailures;
    }
}

class CCounter {
public:
    void Add(int nValue) { m_nTotal += nValue; }

    int Twice(int nValue) const { return nValue * 2; }

    int m_nTotal = 0;
};

//==================================================================================
// --- デリゲートのテスト ---
//==================================================================================
void TestDelegates() {
    dxs::Delegate<int(int)> doubler = dxs::Delegate<int(int)>::CreateRaw(static_cast<const CCounter*>(nullptr), &CCounter::Twice);
    CCounter counter;
    doubler = dxs::Delegate<int(int)>::CreateRaw(static_cast<const CCounter*>(&counter), &CCounter::Twice);
    Check(doubler.IsBound() && doubler(21) == 42, "Delegate::CreateRaw const method");
    dxs::Delegate<void()> empty;
    Check(!empty.ExecuteIfBound(), "Delegate::ExecuteIfBound on an unbound delegate");

    dxs::MulticastDelegate<int> onValue;
    int nLambdaTotal = 0;
    const auto lambdaHandle = onValue.AddLambda([&](int nValue) { nLambdaTotal += nValue; });
    onValue.AddRaw(&counter, &CCounter::Add);
    onValue.Broadcast(5);
    Check(nLambdaTotal == 5 && counter.m_nTotal == 5, "MulticastDelegate broadcast to lambda and member");
    Check(onValue.Remove(lambdaHandle) && !onValue.Remove(lambdaHandle), "MulticastDelegate::Remove once");
    Check(onValue.RemoveAll(&counter) == 1 && !onValue.IsBound(), "MulticastDelegate::RemoveAll by owner");

    // 通知の途中で削除 / 追加しても安全
    int nCalls = 0;
    dxs::DelegateHandle selfHandle;
    selfHandle = onValue.AddLambda([&](int) {
        ++nCalls;
        onValue.Remove(selfHandle);
        onValue.AddLambda([&](int) { ++nCalls; });
    });
    onValue.Broadcast(1);
    Check(nCalls == 1, "binding added during broadcast is not called in the same broadcast");
    onValue.Broadcast(1);
    Check(nCalls == 2, "binding removed during broadcast is not called again");

    // スコープ付きのハンドルは自動で解除される（デリゲートが先に消えても）
    auto pDelegate = std::make_unique<dxs::MulticastDelegate<>>();
    int nScoped = 0;
    {
        dxs::ScopedDelegateHandle scoped(*pDelegate, pDelegate->AddLambda([&] { ++nScoped; }));
        pDelegate->Broadcast();
    }

    pDelegate->Broadcast();
    Check(nScoped == 1, "ScopedDelegateHandle removes its binding");
    dxs::ScopedDelegateHandle outlives(*pDelegate, *pDelegate += [] {});
    pDelegate.reset(); // outlives が破棄されても落ちないこと
}

class CPlayModeListener final : public dxs::IPlayModeCallbacks, public dxs::ISceneCallbacks {
public:
    void OnEnterPlayMode(dxs::Scene&) override { ++m_nEnter; }

    void OnSceneSaved(const dxs::Scene&) override { ++m_nSaved; }

    int m_nEnter = 0;
    int m_nSaved = 0;
};

//==================================================================================
// --- コールバックのインターフェースのテスト ---
//==================================================================================
void TestCallbackInterfaces() {
    CPlayModeListener listener;
    dxs::Scene scene;
    {
        auto registration = dxs::RegisterCallbacks(listener);
        Check(registration.Count() == 5, "RegisterCallbacks subscribes every interface method");
        dxs::EngineEvents::Get().onEnterPlayMode.Broadcast(scene);
        dxs::EngineEvents::Get().onSceneSaved.Broadcast(scene);
    }

    dxs::EngineEvents::Get().onEnterPlayMode.Broadcast(scene);
    Check(listener.m_nEnter == 1 && listener.m_nSaved == 1, "interface callbacks fire until the registration ends");
}

// テスト用のコルーチン: nFrames フレーム数える / 順番に進む / 例外を投げる
dxs::Coroutine CountFrames(int& nCounter, int nFrames) {
    for (int nIndex = 0; nIndex < nFrames; ++nIndex) {
        ++nCounter;
        co_yield nullptr;
    }
}

dxs::Coroutine Sequence(std::vector<int>& vSteps, bool& bGate) {
    vSteps.push_back(1); // StartCoroutine の中で実行される
    co_yield dxs::WaitForSeconds(0.5f);
    vSteps.push_back(2);
    co_yield dxs::WaitUntil([&] { return bGate; });
    vSteps.push_back(3);
    int nChildFrames = 0;
    co_yield CountFrames(nChildFrames, 2); // 子コルーチンの終わりを待つ
    vSteps.push_back(10 + nChildFrames);
    co_yield dxs::WaitForFrames(2);
    vSteps.push_back(4);
}

dxs::Coroutine Throws() {
    co_yield dxs::NextFrame;
    throw std::runtime_error("boom");
}

//==================================================================================
// --- コルーチンのテスト ---
//==================================================================================
void TestCoroutines() {
    dxs::CoroutineRunner runner;
    std::vector<int> vSteps;
    bool bGate = false;
    const auto handle = runner.Start(Sequence(vSteps, bGate));
    Check(vSteps == std::vector<int>{1}, "StartCoroutine runs until the first yield");
    runner.Tick(0.25f);
    Check(vSteps.size() == 1, "WaitForSeconds still waiting");
    runner.Tick(0.25f);
    Check(vSteps.size() == 2, "WaitForSeconds finished");
    runner.Tick(0.1f);
    Check(vSteps.size() == 2, "WaitUntil keeps waiting while false");
    bGate = true;
    runner.Tick(0.1f); // ゲートが開く → 3 を追加し、子を開始（1 フレーム目）
    Check(vSteps.size() == 3, "WaitUntil resumes when true");
    runner.Tick(0.1f); // 子の 2 フレーム目
    runner.Tick(0.1f); // 子が終わる → 12 を追加
    Check(vSteps.size() == 4 && vSteps[3] == 12, "nested coroutine completes before the parent resumes");
    runner.Tick(0.1f);
    Check(runner.IsRunning(handle), "WaitForFrames(2) still waiting after one frame");
    runner.Tick(0.1f);
    Check(vSteps.size() == 5 && !runner.IsRunning(handle) && runner.RunningCount() == 0, "coroutine finished");

    int nCounter = 0;
    const auto stopped = runner.Start(CountFrames(nCounter, 100));
    runner.Tick(0.016f);
    Check(runner.Stop(stopped), "StopCoroutine");
    runner.Tick(0.016f);
    Check(nCounter == 2 && runner.RunningCount() == 0, "a stopped coroutine no longer runs");

    std::string sError;
    runner.onError.AddLambda([&](const std::string& sMessage) { sError = sMessage; });
    runner.Start(Throws());
    runner.Tick(0.016f);
    Check(sError.find("boom") != std::string::npos && runner.RunningCount() == 0,
          "exceptions stop the coroutine and are reported");

    runner.SetTimeScale(0.0f);
    vSteps.clear();
    bGate = false;
    runner.Start(Sequence(vSteps, bGate));
    runner.Tick(10.0f);
    Check(vSteps.size() == 1, "time scale 0 pauses WaitForSeconds");
}

//==================================================================================
// --- コンポーネントのコルーチンのテスト ---
//==================================================================================
// コンポーネントのコルーチンは UpdateUserComponents で進む
class CBlinker final : public dxs::Component {
public:
    void Start(dxs::Entity& entity) override { StartCoroutine(Blink(entity)); }

    void Update(dxs::Entity&, float) override { ++m_nUpdates; }

    void LateUpdate(dxs::Entity&, float) override { m_bLateAfterUpdate = m_nUpdates > 0; }

    dxs::Coroutine Blink(dxs::Entity& entity) {
        while (true) {
            co_yield dxs::WaitForSeconds(0.5f);
            entity.draw.bVisible = !entity.draw.bVisible;
        }
    }

    int m_nUpdates = 0;
    bool m_bLateAfterUpdate = false;
};
DXS_REGISTER_COMPONENT(CBlinker)

void TestComponentCoroutines() {
    dxs::Scene scene;
    dxs::Entity entity;
    entity.sName = "Blinker";
    std::string error;
    Check(dxs::AttachComponent(entity, "CBlinker", error), "AttachComponent registered class");
    Check(!dxs::AttachComponent(entity, "CBlinker", error), "AttachComponent rejects duplicates");
    scene.vEntities.push_back(entity);
    int nStarted = 0;
    dxs::ScopedDelegateHandle started(
        dxs::EngineEvents::Get().onComponentStarted,
        dxs::EngineEvents::Get().onComponentStarted.AddLambda(
            [&](dxs::Entity&, dxs::Component&, const std::string& sName) { nStarted += sName == "CBlinker"; }));
    dxs::UpdateUserComponents(scene, 0.3f);
    Check(scene.vEntities[0].draw.bVisible, "blink not yet toggled");
    dxs::UpdateUserComponents(scene, 0.3f);
    Check(!scene.vEntities[0].draw.bVisible, "component coroutine toggled visibility after 0.5 s");
    auto* pBlinker = dynamic_cast<CBlinker*>(scene.vEntities[0].vRuntimeComponents[0].get());
    Check(pBlinker && pBlinker->m_nUpdates == 2 && pBlinker->m_bLateAfterUpdate, "Update and LateUpdate order");
    Check(nStarted == 1, "onComponentStarted fired once");
}

//==================================================================================
// --- CoroutineTemplate のテスト ---
//==================================================================================
// 付属の CoroutineTemplate は 5 つの手順をすべて実行すること
void TestCoroutineTemplate() {
    dxs::Scene scene;
    dxs::Entity entity;
    entity.sName = "Template";
    std::string error;
    Check(dxs::AttachComponent(entity, "CoroutineTemplate", error), "CoroutineTemplate is registered");
    scene.vEntities.push_back(entity);
    for (int nFrame = 0; nFrame < 300; ++nFrame) {
        dxs::UpdateUserComponents(scene, 1.0f / 60.0f); // 5 秒
    }

    const auto values = dxs::ComponentEditorValues(scene.vEntities[0], 0);
    const auto step = std::find_if(values.begin(), values.end(), [](const auto& pair) { return pair.first == "Step"; });
    Check(step != values.end() && step->second == "5", "CoroutineTemplate reaches step 5 within 5 seconds");
    Check(scene.vEntities[0].draw.position.x > 1.9f && scene.vEntities[0].draw.bVisible,
          "CoroutineTemplate moved and stopped blinking");
}

//==================================================================================
// --- ログのスレッドのテスト ---
//==================================================================================
// 作業スレッド（バックグラウンドの書き出し）が書いたログは、
// メインスレッドでだけ、順番どおりに通知されること
void TestLogThreads() {
    dxs::Debug::SetMainThread();
    std::vector<std::string> vSeen;
    dxs::ScopedDelegateHandle listener(
        dxs::Debug::OnLog(), dxs::Debug::OnLog().AddLambda([&](const dxs::LogEntry& entry) { vSeen.push_back(entry.sMessage); }));
    std::thread worker([] { dxs::Debug::Log("from worker"); });
    worker.join();
    const auto vHistory = dxs::Debug::History();
    Check(!vHistory.empty() && vHistory.back().sMessage == "from worker", "worker entry is in the history at once");
    Check(vSeen.empty(), "worker entry is not broadcast on the worker thread");
    dxs::Debug::Log("from main");
    Check(vSeen.size() == 2 && vSeen[0] == "from worker" && vSeen[1] == "from main",
          "queued entries are broadcast first, on the main thread");
    std::thread([] { dxs::Debug::LogWarning("later"); }).join();
    dxs::Debug::DispatchPending();
    Check(vSeen.size() == 3 && vSeen[2] == "later", "DispatchPending delivers queued entries");
}
} // namespace

namespace dxs {
void RegisterProjectComponents() {}
} // namespace dxs

//==================================================================================
// --- メイン関数 ---
//==================================================================================
int main() {
    TestDelegates();
    TestCallbackInterfaces();
    TestCoroutines();
    TestComponentCoroutines();
    TestCoroutineTemplate();
    TestLogThreads(); // 最後に行う: このスレッドをログのメインスレッドにするため
    if (g_nFailures) return 1;
    std::cout << "Delegate, callback interface, coroutine and log threading tests passed.\n";
    return 0;
}
