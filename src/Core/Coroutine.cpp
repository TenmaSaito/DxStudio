#include "Coroutine.h"
#include "Log.h"

#include <algorithm>
#include <utility>

namespace dxs {

// ---- promise_type ------------------------------------------------------------
std::suspend_always Coroutine::promise_type::yield_value(std::nullptr_t) noexcept {
    instruction = detail::WaitFrames{1};
    return {};
}
std::suspend_always Coroutine::promise_type::yield_value(NextFrameTag) noexcept {
    instruction = detail::WaitFrames{1};
    return {};
}
std::suspend_always Coroutine::promise_type::yield_value(WaitForSeconds wait) noexcept {
    instruction = detail::WaitTime{wait.fSeconds, false};
    return {};
}
std::suspend_always Coroutine::promise_type::yield_value(WaitForSecondsRealtime wait) noexcept {
    instruction = detail::WaitTime{wait.fSeconds, true};
    return {};
}
std::suspend_always Coroutine::promise_type::yield_value(WaitForFrames wait) noexcept {
    instruction = detail::WaitFrames{std::max(1, wait.nFrames)};
    return {};
}
std::suspend_always Coroutine::promise_type::yield_value(WaitUntil wait) {
    instruction = detail::WaitPredicate{std::move(wait.predicate), true};
    return {};
}
std::suspend_always Coroutine::promise_type::yield_value(WaitWhile wait) {
    instruction = detail::WaitPredicate{std::move(wait.predicate), false};
    return {};
}
std::suspend_always Coroutine::promise_type::yield_value(Coroutine&& child) {
    instruction = detail::WaitChild{std::make_shared<Coroutine>(std::move(child))};
    return {};
}

// ---- Coroutine -------------------------------------------------------------
Coroutine::~Coroutine() {
    if (m_handle) m_handle.destroy();
}

Coroutine::Coroutine(Coroutine&& other) noexcept
    : m_handle(std::exchange(other.m_handle, nullptr)), m_bStarted(std::exchange(other.m_bStarted, false)) {}

Coroutine& Coroutine::operator=(Coroutine&& other) noexcept {
    if (this != &other) {
        if (m_handle) m_handle.destroy();
        m_handle = std::exchange(other.m_handle, nullptr);
        m_bStarted = std::exchange(other.m_bStarted, false);
    }
    return *this;
}

bool Coroutine::Start() {
    if (IsDone()) return false;
    if (m_bStarted) return true;
    m_bStarted = true;
    return Resume();
}

bool Coroutine::Resume() {
    auto& promise = m_handle.promise();
    promise.instruction = detail::WaitNone{};
    m_handle.resume();
    if (promise.pException) {
        auto pException = std::exchange(promise.pException, nullptr);
        std::rethrow_exception(pException);
    }
    if (m_handle.done()) return false;
    // `co_yield child` starts the child immediately, like Unity's
    // `yield return StartCoroutine(child)`.
    if (auto* pWait = std::get_if<detail::WaitChild>(&promise.instruction); pWait && pWait->pChild) {
        pWait->pChild->Start();
    }
    return true;
}

bool Coroutine::UpdateWait(float fDeltaSeconds, float fUnscaledDeltaSeconds) {
    auto& instruction = m_handle.promise().instruction;
    if (auto* pTime = std::get_if<detail::WaitTime>(&instruction)) {
        pTime->fRemaining -= pTime->bRealtime ? fUnscaledDeltaSeconds : fDeltaSeconds;
        return pTime->fRemaining <= 0.0f;
    }
    if (auto* pFrames = std::get_if<detail::WaitFrames>(&instruction)) {
        return --pFrames->nRemaining <= 0;
    }
    if (auto* pPredicate = std::get_if<detail::WaitPredicate>(&instruction)) {
        if (!pPredicate->predicate) return true;
        return pPredicate->predicate() == pPredicate->bResumeWhen;
    }
    if (auto* pChild = std::get_if<detail::WaitChild>(&instruction)) {
        if (!pChild->pChild || pChild->pChild->IsDone()) return true;
        return !pChild->pChild->Tick(fDeltaSeconds, fUnscaledDeltaSeconds);
    }
    return true;
}

bool Coroutine::Tick(float fDeltaSeconds, float fUnscaledDeltaSeconds) {
    if (IsDone()) return false;
    if (!m_bStarted) return Start();
    if (!UpdateWait(fDeltaSeconds, fUnscaledDeltaSeconds)) return true;
    return Resume();
}

// ---- CoroutineRunner -------------------------------------------------------
CoroutineHandle CoroutineRunner::Start(Coroutine coroutine) {
    if (!coroutine.IsValid()) return {};
    const CoroutineHandle handle{m_nNextId++};
    bool bRunning = false;
    try {
        bRunning = coroutine.Start();
    } catch (...) {
        ReportError(std::current_exception());
        return {};
    }
    if (!bRunning) return handle; // finished synchronously, nothing to keep
    Entry entry{handle.nId, std::move(coroutine), false};
    if (m_nTickDepth > 0) m_vPending.push_back(std::move(entry));
    else m_vEntries.push_back(std::move(entry));
    return handle;
}

bool CoroutineRunner::Stop(CoroutineHandle handle) {
    for (auto* pList : {&m_vEntries, &m_vPending}) {
        for (auto& entry : *pList) {
            if (entry.nId == handle.nId && !entry.bStopped) {
                entry.bStopped = true;
                return true;
            }
        }
    }
    return false;
}

void CoroutineRunner::StopAll() {
    for (auto& entry : m_vEntries) entry.bStopped = true;
    for (auto& entry : m_vPending) entry.bStopped = true;
    if (m_nTickDepth == 0) {
        m_vEntries.clear();
        m_vPending.clear();
    }
}

void CoroutineRunner::Tick(float fDeltaSeconds) { Tick(fDeltaSeconds, fDeltaSeconds); }

void CoroutineRunner::Tick(float fDeltaSeconds, float fUnscaledDeltaSeconds) {
    ++m_nTickDepth;
    const float fScaled = fDeltaSeconds * m_fTimeScale;
    // A frame's coroutine set is fixed when Tick starts. Coroutines started
    // inside it first advance on the next Tick (they already ran to their
    // first yield inside Start).
    for (std::size_t nIndex = 0; nIndex < m_vEntries.size(); ++nIndex) {
        if (m_vEntries[nIndex].bStopped) continue;
        try {
            if (!m_vEntries[nIndex].coroutine.Tick(fScaled, fUnscaledDeltaSeconds)) m_vEntries[nIndex].bStopped = true;
        } catch (...) {
            m_vEntries[nIndex].bStopped = true;
            ReportError(std::current_exception());
        }
    }
    --m_nTickDepth;
    if (m_nTickDepth == 0) {
        std::erase_if(m_vEntries, [](const Entry& entry) { return entry.bStopped; });
        for (auto& entry : m_vPending) {
            if (!entry.bStopped) m_vEntries.push_back(std::move(entry));
        }
        m_vPending.clear();
    }
}

bool CoroutineRunner::IsRunning(CoroutineHandle handle) const {
    for (const auto* pList : {&m_vEntries, &m_vPending}) {
        for (const auto& entry : *pList) {
            if (entry.nId == handle.nId) return !entry.bStopped && !entry.coroutine.IsDone();
        }
    }
    return false;
}

std::size_t CoroutineRunner::RunningCount() const {
    std::size_t nCount = 0;
    for (const auto* pList : {&m_vEntries, &m_vPending}) {
        for (const auto& entry : *pList) nCount += (!entry.bStopped && !entry.coroutine.IsDone()) ? 1 : 0;
    }
    return nCount;
}

void CoroutineRunner::ReportError(const std::exception_ptr& pException) {
    std::string sMessage = "Coroutine stopped by an exception";
    try {
        if (pException) std::rethrow_exception(pException);
    } catch (const std::exception& exception) {
        sMessage += ": ";
        sMessage += exception.what();
    } catch (...) {
        sMessage += ".";
    }
    if (onError.IsBound()) onError.Broadcast(sMessage);
    else Debug::LogError(sMessage);
}

} // namespace dxs
