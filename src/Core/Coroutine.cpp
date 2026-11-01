//==================================================================================
//
// コルーチンのソースファイル [Coroutine.cpp]
//
// co_yield に渡された物は promise_type::yield_value で「待機状態」として記録され、
// CoroutineRunner::Tick が毎フレームその状態を進めて、満たされたら本体を再開する。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Coroutine.h"
#include "Log.h"

#include <algorithm>
#include <utility>

namespace dxs {

//==================================================================================
// --- co_yield ごとの待機状態の記録処理 ---
//==================================================================================
// co_yield nullptr: 次のフレームまで待つ
std::suspend_always Coroutine::promise_type::yield_value(std::nullptr_t) noexcept {
    instruction = detail::WaitFrames{1};
    return {};
}
// co_yield dxs::NextFrame: 次のフレームまで待つ
std::suspend_always Coroutine::promise_type::yield_value(NextFrameTag) noexcept {
    instruction = detail::WaitFrames{1};
    return {};
}
// co_yield WaitForSeconds: スケール済みの時間で待つ
std::suspend_always Coroutine::promise_type::yield_value(WaitForSeconds wait) noexcept {
    instruction = detail::WaitTime{wait.fSeconds, false};
    return {};
}
// co_yield WaitForSecondsRealtime: 実時間で待つ
std::suspend_always Coroutine::promise_type::yield_value(WaitForSecondsRealtime wait) noexcept {
    instruction = detail::WaitTime{wait.fSeconds, true};
    return {};
}
// co_yield WaitForFrames: 指定フレーム数待つ（最低 1 フレーム）
std::suspend_always Coroutine::promise_type::yield_value(WaitForFrames wait) noexcept {
    instruction = detail::WaitFrames{std::max(1, wait.nFrames)};
    return {};
}
// co_yield WaitUntil: 条件が true になるまで待つ
std::suspend_always Coroutine::promise_type::yield_value(WaitUntil wait) {
    instruction = detail::WaitPredicate{std::move(wait.predicate), true};
    return {};
}
// co_yield WaitWhile: 条件が false になるまで待つ
std::suspend_always Coroutine::promise_type::yield_value(WaitWhile wait) {
    instruction = detail::WaitPredicate{std::move(wait.predicate), false};
    return {};
}
// co_yield 子コルーチン: 子が終わるまで待つ（子は Resume() で開始される）
std::suspend_always Coroutine::promise_type::yield_value(Coroutine&& child) {
    instruction = detail::WaitChild{std::make_shared<Coroutine>(std::move(child))};
    return {};
}

//==================================================================================
// --- デストラクタ ---
//==================================================================================
Coroutine::~Coroutine() {
    if (m_handle) m_handle.destroy(); // コルーチンのフレーム（ローカル変数など）を解放
}

//==================================================================================
// --- ムーブコンストラクタ / ムーブ代入 ---
//==================================================================================
Coroutine::Coroutine(Coroutine&& other) noexcept
    : m_handle(std::exchange(other.m_handle, nullptr)), m_bStarted(std::exchange(other.m_bStarted, false)) {}

Coroutine& Coroutine::operator=(Coroutine&& other) noexcept {
    if (this != &other) {
        if (m_handle) m_handle.destroy(); // 今持っている物を先に解放
        m_handle = std::exchange(other.m_handle, nullptr);
        m_bStarted = std::exchange(other.m_bStarted, false);
    }
    return *this;
}

//==================================================================================
// --- 開始処理（最初の co_yield まで実行） ---
//==================================================================================
bool Coroutine::Start() {
    if (IsDone()) return false;
    if (m_bStarted) return true; // 開始済みなら何もしない
    m_bStarted = true;
    return Resume();
}

//==================================================================================
// --- 再開処理（次の co_yield まで実行） ---
//==================================================================================
bool Coroutine::Resume() {
    auto& promise = m_handle.promise();
    promise.instruction = detail::WaitNone{}; // 次の co_yield で上書きされる
    m_handle.resume();
    if (promise.pException) { // 本体が例外を投げた場合は呼び出し側へ再送出
        auto pException = std::exchange(promise.pException, nullptr);
        std::rethrow_exception(pException);
    }
    if (m_handle.done()) return false; // co_return / 末尾まで到達
    // `co_yield 子` は子をすぐに開始する（Unity の
    // `yield return StartCoroutine(child)` と同じ）
    if (auto* pWait = std::get_if<detail::WaitChild>(&promise.instruction); pWait && pWait->pChild) {
        pWait->pChild->Start();
    }
    return true;
}

//==================================================================================
// --- 待機状態の更新処理（満たされたら true） ---
//==================================================================================
bool Coroutine::UpdateWait(float fDeltaSeconds, float fUnscaledDeltaSeconds) {
    auto& instruction = m_handle.promise().instruction;
    if (auto* pTime = std::get_if<detail::WaitTime>(&instruction)) { // 時間待ち: 残り時間を減らす
        pTime->fRemaining -= pTime->bRealtime ? fUnscaledDeltaSeconds : fDeltaSeconds;
        return pTime->fRemaining <= 0.0f;
    }
    if (auto* pFrames = std::get_if<detail::WaitFrames>(&instruction)) { // フレーム待ち: 残りを 1 減らす
        return --pFrames->nRemaining <= 0;
    }
    if (auto* pPredicate = std::get_if<detail::WaitPredicate>(&instruction)) { // 条件待ち: 判定関数を呼ぶ
        if (!pPredicate->predicate) return true;
        return pPredicate->predicate() == pPredicate->bResumeWhen;
    }
    if (auto* pChild = std::get_if<detail::WaitChild>(&instruction)) { // 子待ち: 子を 1 フレーム進める
        if (!pChild->pChild || pChild->pChild->IsDone()) return true;
        return !pChild->pChild->Tick(fDeltaSeconds, fUnscaledDeltaSeconds);
    }
    return true; // 待機なし
}

//==================================================================================
// --- 1 フレーム分の更新処理 ---
//==================================================================================
bool Coroutine::Tick(float fDeltaSeconds, float fUnscaledDeltaSeconds) {
    if (IsDone()) return false;
    if (!m_bStarted) return Start();                                   // 未開始なら開始
    if (!UpdateWait(fDeltaSeconds, fUnscaledDeltaSeconds)) return true; // まだ待機中
    return Resume();                                                    // 待機が終わったので再開
}

//==================================================================================
// --- コルーチンの開始処理（実行管理） ---
//==================================================================================
CoroutineHandle CoroutineRunner::Start(Coroutine coroutine) {
    if (!coroutine.IsValid()) return {};
    const CoroutineHandle handle{m_nNextId++};
    bool bRunning = false;
    try {
        bRunning = coroutine.Start(); // 最初の co_yield まで実行
    } catch (...) {
        ReportError(std::current_exception());
        return {};
    }
    if (!bRunning) return handle; // その場で終わったので保持しない
    Entry entry{handle.nId, std::move(coroutine), false};
    // Tick 中に開始された物は、回している配列を壊さないよう別に貯める
    if (m_nTickDepth > 0) m_vPending.push_back(std::move(entry));
    else m_vEntries.push_back(std::move(entry));
    return handle;
}

//==================================================================================
// --- コルーチンの停止処理 ---
//==================================================================================
bool CoroutineRunner::Stop(CoroutineHandle handle) {
    for (auto* pList : {&m_vEntries, &m_vPending}) {
        for (auto& entry : *pList) {
            if (entry.nId == handle.nId && !entry.bStopped) {
                entry.bStopped = true; // 実際の削除は Tick の後
                return true;
            }
        }
    }
    return false;
}

//==================================================================================
// --- 全コルーチンの停止処理 ---
//==================================================================================
void CoroutineRunner::StopAll() {
    for (auto& entry : m_vEntries) entry.bStopped = true;
    for (auto& entry : m_vPending) entry.bStopped = true;
    if (m_nTickDepth == 0) { // Tick 中でなければすぐに消す
        m_vEntries.clear();
        m_vPending.clear();
    }
}

//==================================================================================
// --- 1 フレーム分の更新処理（実行管理） ---
//==================================================================================
void CoroutineRunner::Tick(float fDeltaSeconds) { Tick(fDeltaSeconds, fDeltaSeconds); }

void CoroutineRunner::Tick(float fDeltaSeconds, float fUnscaledDeltaSeconds) {
    ++m_nTickDepth;
    const float fScaled = fDeltaSeconds * m_fTimeScale;
    // このフレームで進める物は Tick 開始時点で決まる。Tick 中に開始された物は
    // 次の Tick から進む（Start の中で最初の co_yield まで実行済みのため）。
    for (std::size_t nIndex = 0; nIndex < m_vEntries.size(); ++nIndex) {
        if (m_vEntries[nIndex].bStopped) continue;
        try {
            if (!m_vEntries[nIndex].coroutine.Tick(fScaled, fUnscaledDeltaSeconds)) m_vEntries[nIndex].bStopped = true; // 終了
        } catch (...) { // 本体の例外: そのコルーチンだけ止めて通知する
            m_vEntries[nIndex].bStopped = true;
            ReportError(std::current_exception());
        }
    }
    --m_nTickDepth;
    if (m_nTickDepth == 0) {
        // 停止・終了した物を消し、Tick 中に開始された物を合流させる
        std::erase_if(m_vEntries, [](const Entry& entry) { return entry.bStopped; });
        for (auto& entry : m_vPending) {
            if (!entry.bStopped) m_vEntries.push_back(std::move(entry));
        }
        m_vPending.clear();
    }
}

//==================================================================================
// --- 実行中かの判定処理 ---
//==================================================================================
bool CoroutineRunner::IsRunning(CoroutineHandle handle) const {
    for (const auto* pList : {&m_vEntries, &m_vPending}) {
        for (const auto& entry : *pList) {
            if (entry.nId == handle.nId) return !entry.bStopped && !entry.coroutine.IsDone();
        }
    }
    return false;
}

//==================================================================================
// --- 実行中の数の取得処理 ---
//==================================================================================
std::size_t CoroutineRunner::RunningCount() const {
    std::size_t nCount = 0;
    for (const auto* pList : {&m_vEntries, &m_vPending}) {
        for (const auto& entry : *pList) nCount += (!entry.bStopped && !entry.coroutine.IsDone()) ? 1 : 0;
    }
    return nCount;
}

//==================================================================================
// --- 例外の通知処理 ---
//==================================================================================
void CoroutineRunner::ReportError(const std::exception_ptr& pException) {
    std::string sMessage = "Coroutine stopped by an exception";
    try { // 例外の種類からメッセージを取り出す
        if (pException) std::rethrow_exception(pException);
    } catch (const std::exception& exception) {
        sMessage += ": ";
        sMessage += exception.what();
    } catch (...) {
        sMessage += ".";
    }
    if (onError.IsBound()) onError.Broadcast(sMessage); // 登録先があればそちらへ
    else Debug::LogError(sMessage);                      // 無ければログへ
}

} // namespace dxs
