#pragma once

// ---------------------------------------------------------------------------
// Unity-style coroutines built on C++20 <coroutine>.
//
//   Unity C#                                   DxStudio C++
//   IEnumerator Blink() {                      dxs::Coroutine Blink(dxs::Entity& entity) {
//       while (true) {                             while (true) {
//           yield return new WaitForSeconds(1);        co_yield dxs::WaitForSeconds(1.0f);
//           yield return null;                         co_yield dxs::NextFrame;   // or co_yield nullptr;
//           yield return new WaitUntil(() => ok);      co_yield dxs::WaitUntil([&] { return ok; });
//           yield return StartCoroutine(Sub());        co_yield Sub();            // waits for Sub()
//           yield break;                               co_return;
//       }                                          }
//   }                                          }
//   StartCoroutine(Blink());                   StartCoroutine(Blink(entity));   // inside a Component
//   StopCoroutine(handle) / StopAllCoroutines  StopCoroutine(handle) / StopAllCoroutines()
//
// As in Unity, StartCoroutine runs the body immediately until its first
// co_yield, and coroutines resume after Component::Update every frame.
// Coroutine bodies must not capture references to objects that may be
// destroyed before the coroutine finishes.
// ---------------------------------------------------------------------------

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

// ---- Yield instructions ----------------------------------------------------
// Waits for scaled time (affected by CoroutineRunner time scale).
struct WaitForSeconds {
    explicit WaitForSeconds(float fSecondsValue) : fSeconds(fSecondsValue) {}
    float fSeconds = 0.0f;
};
// Waits for real (unscaled) time.
struct WaitForSecondsRealtime {
    explicit WaitForSecondsRealtime(float fSecondsValue) : fSeconds(fSecondsValue) {}
    float fSeconds = 0.0f;
};
// Waits the given number of frames. WaitForFrames(1) equals `co_yield nullptr`.
struct WaitForFrames {
    explicit WaitForFrames(int nFramesValue) : nFrames(nFramesValue) {}
    int nFrames = 1;
};
// Resumes on the first frame the predicate returns true.
struct WaitUntil {
    explicit WaitUntil(std::function<bool()> predicateValue) : predicate(std::move(predicateValue)) {}
    std::function<bool()> predicate;
};
// Resumes on the first frame the predicate returns false.
struct WaitWhile {
    explicit WaitWhile(std::function<bool()> predicateValue) : predicate(std::move(predicateValue)) {}
    std::function<bool()> predicate;
};
// Equivalent of `yield return null`.
struct NextFrameTag {};
inline constexpr NextFrameTag NextFrame{};
// End-of-frame is approximated by the next frame (there is no separate phase).
using WaitForEndOfFrame = NextFrameTag;

class Coroutine;

namespace detail {
struct WaitNone {};
struct WaitTime {
    float fRemaining = 0.0f;
    bool bRealtime = false;
};
struct WaitFrames {
    int nRemaining = 0;
};
struct WaitPredicate {
    std::function<bool()> predicate;
    bool bResumeWhen = true; // WaitUntil: resume when true, WaitWhile: when false
};
struct WaitChild {
    std::shared_ptr<Coroutine> pChild;
};
using YieldInstruction = std::variant<WaitNone, WaitTime, WaitFrames, WaitPredicate, WaitChild>;
} // namespace detail

// ---- Coroutine -------------------------------------------------------------
class Coroutine {
public:
    struct promise_type {
        detail::YieldInstruction instruction;
        std::exception_ptr pException;

        Coroutine get_return_object() { return Coroutine(Handle::from_promise(*this)); }
        // Lazy start: CoroutineRunner::Start() runs it to the first co_yield.
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { pException = std::current_exception(); }

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
    Coroutine(const Coroutine&) = delete;
    Coroutine& operator=(const Coroutine&) = delete;
    Coroutine(Coroutine&& other) noexcept;
    Coroutine& operator=(Coroutine&& other) noexcept;

    [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_handle); }
    [[nodiscard]] bool IsDone() const noexcept { return !m_handle || m_handle.done(); }
    [[nodiscard]] bool HasStarted() const noexcept { return m_bStarted; }

    // Runs the body until the next suspension point. Returns false once the
    // coroutine has finished. Rethrows an exception thrown by the body.
    bool Start();
    // Advances one frame. Returns false once the coroutine has finished.
    bool Tick(float fDeltaSeconds, float fUnscaledDeltaSeconds);

private:
    explicit Coroutine(Handle handle) : m_handle(handle) {}
    bool Resume();
    // True when the current yield instruction has been satisfied this frame.
    bool UpdateWait(float fDeltaSeconds, float fUnscaledDeltaSeconds);

    Handle m_handle;
    bool m_bStarted = false;
};

// Identifies one coroutine started by a CoroutineRunner.
struct CoroutineHandle {
    std::uint64_t nId = 0;
    [[nodiscard]] bool IsValid() const noexcept { return nId != 0; }
    friend bool operator==(const CoroutineHandle&, const CoroutineHandle&) = default;
};

// Owns and advances a set of coroutines (one per Component, see Engine.h).
// Starting or stopping coroutines from inside a running coroutine is safe.
class CoroutineRunner {
public:
    CoroutineRunner() = default;
    CoroutineRunner(const CoroutineRunner&) = delete;
    CoroutineRunner& operator=(const CoroutineRunner&) = delete;
    CoroutineRunner(CoroutineRunner&&) noexcept = default;
    CoroutineRunner& operator=(CoroutineRunner&&) noexcept = default;

    CoroutineHandle Start(Coroutine coroutine);
    bool Stop(CoroutineHandle handle);
    void StopAll();
    void Tick(float fDeltaSeconds);
    void Tick(float fDeltaSeconds, float fUnscaledDeltaSeconds);

    [[nodiscard]] bool IsRunning(CoroutineHandle handle) const;
    [[nodiscard]] std::size_t RunningCount() const;

    // Multiplies the delta passed to WaitForSeconds (Unity: Time.timeScale).
    void SetTimeScale(float fScale) { m_fTimeScale = fScale < 0.0f ? 0.0f : fScale; }
    [[nodiscard]] float TimeScale() const noexcept { return m_fTimeScale; }

    // Fired when a coroutine body throws. The coroutine is stopped. When
    // nothing is bound, the message is written to Debug::LogError.
    MulticastDelegate<const std::string&> onError;

private:
    struct Entry {
        std::uint64_t nId = 0;
        Coroutine coroutine;
        bool bStopped = false;
    };
    void ReportError(const std::exception_ptr& pException);

    std::vector<Entry> m_vEntries;
    std::vector<Entry> m_vPending; // started while Tick() is iterating
    std::uint64_t m_nNextId = 1;
    int m_nTickDepth = 0;
    float m_fTimeScale = 1.0f;
};

} // namespace dxs
