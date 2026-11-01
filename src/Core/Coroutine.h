//==================================================================================
//
// コルーチンのヘッダーファイル [Coroutine.h]
//
// C++20 の <coroutine> で作った、Unity と同じ使い方のコルーチン。
//
//   Unity C#                                   DxStudio C++
//   IEnumerator Blink() {                      dxs::Coroutine Blink(dxs::Entity& entity) {
//       while (true) {                             while (true) {
//           yield return new WaitForSeconds(1);        co_yield dxs::WaitForSeconds(1.0f);
//           yield return null;                         co_yield dxs::NextFrame;   // または co_yield nullptr;
//           yield return new WaitUntil(() => ok);      co_yield dxs::WaitUntil([&] { return ok; });
//           yield return StartCoroutine(Sub());        co_yield Sub();            // Sub() の終了を待つ
//           yield break;                               co_return;
//       }                                          }
//   }                                          }
//   StartCoroutine(Blink());                   StartCoroutine(Blink(entity));   // Component の中で
//   StopCoroutine(handle) / StopAllCoroutines  StopCoroutine(handle) / StopAllCoroutines()
//
// Unity と同じく、StartCoroutine は最初の co_yield まで即座に実行し、
// その後は毎フレーム Component::Update の後に再開する。
// コルーチンの中で、コルーチンより先に破棄されるかもしれない物の参照を
// 持たないこと。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Delegate.h"

#include <coroutine>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace dxs {

//**********************************************************************************
// *** 待機命令（co_yield に渡す物） ***
//**********************************************************************************
// 指定秒数待つ（CoroutineRunner の TimeScale の影響を受ける）
struct WaitForSeconds {
    explicit WaitForSeconds(float fSecondsValue) : fSeconds(fSecondsValue) {}
    float fSeconds = 0.0f; // 待つ秒数
};
// 実時間で指定秒数待つ（TimeScale の影響を受けない）
struct WaitForSecondsRealtime {
    explicit WaitForSecondsRealtime(float fSecondsValue) : fSeconds(fSecondsValue) {}
    float fSeconds = 0.0f; // 待つ秒数
};
// 指定フレーム数待つ。WaitForFrames(1) は `co_yield nullptr` と同じ
struct WaitForFrames {
    explicit WaitForFrames(int nFramesValue) : nFrames(nFramesValue) {}
    int nFrames = 1; // 待つフレーム数
};
// 条件が true になったフレームで再開する
struct WaitUntil {
    explicit WaitUntil(std::function<bool()> predicateValue) : predicate(std::move(predicateValue)) {}
    std::function<bool()> predicate; // 毎フレーム呼ばれる判定関数
};
// 条件が false になったフレームで再開する
struct WaitWhile {
    explicit WaitWhile(std::function<bool()> predicateValue) : predicate(std::move(predicateValue)) {}
    std::function<bool()> predicate; // 毎フレーム呼ばれる判定関数
};
// `yield return null` に相当（次のフレームまで待つ）
struct NextFrameTag {};
inline constexpr NextFrameTag NextFrame{};
// フレームの終わりは別の段階が無いため「次のフレーム」で代用する
using WaitForEndOfFrame = NextFrameTag;

class Coroutine;

namespace detail {
//**********************************************************************************
// *** 待機状態（内部用） ***
//**********************************************************************************
struct WaitNone {};      // 待機なし（すぐ再開）
struct WaitTime {        // 時間待ち
    float fRemaining = 0.0f; // 残り秒数
    bool bRealtime = false;  // 実時間で数えるか
};
struct WaitFrames {      // フレーム数待ち
    int nRemaining = 0;      // 残りフレーム数
};
struct WaitPredicate {   // 条件待ち
    std::function<bool()> predicate; // 判定関数
    bool bResumeWhen = true;         // WaitUntil: true で再開 / WaitWhile: false で再開
};
struct WaitChild {       // 子コルーチンの終了待ち
    std::shared_ptr<Coroutine> pChild; // 子コルーチン
};
// 現在の待機状態（どれか 1 つ）
using YieldInstruction = std::variant<WaitNone, WaitTime, WaitFrames, WaitPredicate, WaitChild>;
} // namespace detail

//**********************************************************************************
// *** コルーチンクラス ***
//**********************************************************************************
class Coroutine {
public:
    //******************************************************************************
    // *** C++20 コルーチンの約束事（コンパイラが使う型） ***
    //******************************************************************************
    struct promise_type {
        detail::YieldInstruction instruction; // 最後の co_yield で指定された待機状態
        std::exception_ptr pException;        // 本体で投げられた例外

        Coroutine get_return_object() { return Coroutine(Handle::from_promise(*this)); }
        // 生成時は止めておく: CoroutineRunner::Start() が最初の co_yield まで実行する
        std::suspend_always initial_suspend() noexcept { return {}; }
        // 終了後も破棄せず止めておく（IsDone() で終了を判定するため）
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { pException = std::current_exception(); }

        // co_yield に渡せる物ごとの処理（待機状態を記録して止まる）
        std::suspend_always yield_value(std::nullptr_t) noexcept;
        std::suspend_always yield_value(NextFrameTag) noexcept;
        std::suspend_always yield_value(WaitForSeconds wait) noexcept;
        std::suspend_always yield_value(WaitForSecondsRealtime wait) noexcept;
        std::suspend_always yield_value(WaitForFrames wait) noexcept;
        std::suspend_always yield_value(WaitUntil wait);
        std::suspend_always yield_value(WaitWhile wait);
        std::suspend_always yield_value(Coroutine&& child);
    };
    using Handle = std::coroutine_handle<promise_type>;

    Coroutine() = default;
    ~Coroutine();
    // コピー禁止（同じコルーチンを 2 回破棄しないように）、ムーブは可
    Coroutine(const Coroutine&) = delete;
    Coroutine& operator=(const Coroutine&) = delete;
    Coroutine(Coroutine&& other) noexcept;
    Coroutine& operator=(Coroutine&& other) noexcept;

    [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_handle); } // 中身があるか
    [[nodiscard]] bool IsDone() const noexcept { return !m_handle || m_handle.done(); } // 終了したか
    [[nodiscard]] bool HasStarted() const noexcept { return m_bStarted; }               // 開始済みか

    // 次の待機地点まで本体を実行する。終了したら false。
    // 本体が投げた例外はここから再送出される。
    bool Start();
    // 1 フレーム進める。終了したら false。
    bool Tick(float fDeltaSeconds, float fUnscaledDeltaSeconds);

private:
    explicit Coroutine(Handle handle) : m_handle(handle) {}
    bool Resume(); // 本体を次の待機地点まで再開する
    // 今の待機状態がこのフレームで満たされたら true
    bool UpdateWait(float fDeltaSeconds, float fUnscaledDeltaSeconds);

    Handle m_handle;         // コルーチン本体
    bool m_bStarted = false; // 開始済みか
};

//**********************************************************************************
// *** 開始したコルーチンを表す番号 ***
//**********************************************************************************
struct CoroutineHandle {
    std::uint64_t nId = 0; // 番号（0 は無効）
    [[nodiscard]] bool IsValid() const noexcept { return nId != 0; }
    friend bool operator==(const CoroutineHandle&, const CoroutineHandle&) = default;
};

//**********************************************************************************
// *** コルーチンの実行管理クラス ***
//**********************************************************************************
// 複数のコルーチンを持ち、毎フレーム進める（Component ごとに 1 つ。Engine.h 参照）。
// 実行中のコルーチンの中から開始・停止しても安全。
class CoroutineRunner {
public:
    CoroutineRunner() = default;
    CoroutineRunner(const CoroutineRunner&) = delete;
    CoroutineRunner& operator=(const CoroutineRunner&) = delete;
    CoroutineRunner(CoroutineRunner&&) noexcept = default;
    CoroutineRunner& operator=(CoroutineRunner&&) noexcept = default;

    CoroutineHandle Start(Coroutine coroutine); // 開始（最初の co_yield まで即実行）
    bool Stop(CoroutineHandle handle);           // 停止
    void StopAll();                              // すべて停止
    void Tick(float fDeltaSeconds);              // 1 フレーム進める（実時間 = 経過時間）
    void Tick(float fDeltaSeconds, float fUnscaledDeltaSeconds); // 1 フレーム進める

    [[nodiscard]] bool IsRunning(CoroutineHandle handle) const; // 実行中か
    [[nodiscard]] std::size_t RunningCount() const;             // 実行中の数

    // WaitForSeconds に渡す経過時間に掛ける倍率（Unity の Time.timeScale）
    void SetTimeScale(float fScale) { m_fTimeScale = fScale < 0.0f ? 0.0f : fScale; }
    [[nodiscard]] float TimeScale() const noexcept { return m_fTimeScale; }

    // コルーチンの本体が例外を投げたときの通知（そのコルーチンは停止する）。
    // 誰も登録していなければ Debug::LogError に出力する。
    MulticastDelegate<const std::string&> onError;

private:
    //******************************************************************************
    // *** 実行中のコルーチン 1 件分 ***
    //******************************************************************************
    struct Entry {
        std::uint64_t nId = 0; // 番号
        Coroutine coroutine;   // コルーチン本体
        bool bStopped = false; // 停止済み（Tick 中は印だけ付ける）
    };
    void ReportError(const std::exception_ptr& pException); // 例外の通知

    std::vector<Entry> m_vEntries; // 実行中のコルーチン
    std::vector<Entry> m_vPending; // Tick() の最中に開始された物（Tick 後に合流）
    std::uint64_t m_nNextId = 1;   // 次に振る番号
    int m_nTickDepth = 0;          // Tick の入れ子の深さ
    float m_fTimeScale = 1.0f;     // 時間の倍率
};

} // namespace dxs
