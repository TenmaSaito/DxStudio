#pragma once

// ---------------------------------------------------------------------------
// DxStudio delegates
//
//   Delegate<R(Args...)>        : single-cast callback (UE: TDelegate, C#: Func/Action)
//   MulticastDelegate<Args...>  : multi-cast callback list (UE: TMulticastDelegate, C#: event)
//   DelegateHandle              : identifies one binding so it can be removed later
//   ScopedDelegateHandle        : RAII binding, removed automatically on destruction
//
// Example
//   dxs::MulticastDelegate<int> onScore;
//   auto handle = onScore.AddLambda([](int nScore) { ... });
//   onScore.AddRaw(&player, &CPlayer::OnScore);
//   onScore.Broadcast(10);
//   onScore.Remove(handle);
//
// Bindings may be added or removed while Broadcast() is running. A binding that
// is added during a broadcast is first invoked by the next broadcast.
// ---------------------------------------------------------------------------

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace dxs {

template<class Signature> class Delegate;

template<class R, class... Args>
class Delegate<R(Args...)> {
public:
    Delegate() = default;

    template<class F>
        requires std::invocable<F&, Args...> && (!std::same_as<std::decay_t<F>, Delegate>)
    Delegate(F&& function) : m_function(std::forward<F>(function)) {}

    static Delegate CreateStatic(R (*pFunction)(Args...)) { return Delegate(pFunction); }

    template<class T>
    static Delegate CreateRaw(T* pObject, R (T::*pMethod)(Args...)) {
        return Delegate([pObject, pMethod](Args... args) -> R { return (pObject->*pMethod)(std::forward<Args>(args)...); });
    }

    template<class T>
    static Delegate CreateRaw(const T* pObject, R (T::*pMethod)(Args...) const) {
        return Delegate([pObject, pMethod](Args... args) -> R { return (pObject->*pMethod)(std::forward<Args>(args)...); });
    }

    // The binding silently becomes a no-op once the shared object is destroyed.
    template<class T>
    static Delegate CreateWeak(const std::shared_ptr<T>& pObject, R (T::*pMethod)(Args...)) {
        std::weak_ptr<T> pWeak = pObject;
        return Delegate([pWeak, pMethod](Args... args) -> R {
            if (auto pLocked = pWeak.lock()) return ((*pLocked).*pMethod)(std::forward<Args>(args)...);
            if constexpr (!std::is_void_v<R>) return R{};
        });
    }

    [[nodiscard]] bool IsBound() const noexcept { return static_cast<bool>(m_function); }
    explicit operator bool() const noexcept { return IsBound(); }
    void Unbind() noexcept { m_function = nullptr; }

    R Execute(Args... args) const { return m_function(std::forward<Args>(args)...); }
    R operator()(Args... args) const { return m_function(std::forward<Args>(args)...); }

    // Safe call for optional callbacks. Returns false when nothing is bound.
    bool ExecuteIfBound(Args... args) const
        requires std::is_void_v<R>
    {
        if (!m_function) return false;
        m_function(std::forward<Args>(args)...);
        return true;
    }

private:
    std::function<R(Args...)> m_function;
};

// Opaque id of one MulticastDelegate binding. A default handle is invalid.
struct DelegateHandle {
    std::uint64_t nId = 0;
    [[nodiscard]] bool IsValid() const noexcept { return nId != 0; }
    void Reset() noexcept { nId = 0; }
    friend bool operator==(const DelegateHandle&, const DelegateHandle&) = default;
};

namespace detail {
// Type-erased removal interface used by ScopedDelegateHandle.
class IDelegateRemover {
public:
    virtual ~IDelegateRemover() = default;
    virtual bool Remove(DelegateHandle handle) = 0;
};
} // namespace detail

template<class... Args>
class MulticastDelegate {
public:
    using Callback = std::function<void(Args...)>;

    MulticastDelegate() : m_pState(std::make_shared<State>()) {}
    // Copying a list of callbacks is almost always a bug (a copy would keep
    // calling objects that unsubscribed from the original), so it is disabled.
    MulticastDelegate(const MulticastDelegate&) = delete;
    MulticastDelegate& operator=(const MulticastDelegate&) = delete;
    MulticastDelegate(MulticastDelegate&&) noexcept = default;
    MulticastDelegate& operator=(MulticastDelegate&&) noexcept = default;

    template<class F>
        requires std::invocable<F&, Args...>
    DelegateHandle AddLambda(F&& function) {
        return m_pState->Add(nullptr, Callback(std::forward<F>(function)));
    }

    DelegateHandle AddStatic(void (*pFunction)(Args...)) { return m_pState->Add(nullptr, Callback(pFunction)); }

    template<class T>
    DelegateHandle AddRaw(T* pObject, void (T::*pMethod)(Args...)) {
        return m_pState->Add(pObject, [pObject, pMethod](Args... args) { (pObject->*pMethod)(std::forward<Args>(args)...); });
    }

    template<class T>
    DelegateHandle AddRaw(const T* pObject, void (T::*pMethod)(Args...) const) {
        return m_pState->Add(pObject, [pObject, pMethod](Args... args) { (pObject->*pMethod)(std::forward<Args>(args)...); });
    }

    template<class T>
    DelegateHandle AddWeak(const std::shared_ptr<T>& pObject, void (T::*pMethod)(Args...)) {
        std::weak_ptr<T> pWeak = pObject;
        return m_pState->Add(pObject.get(), [pWeak, pMethod](Args... args) {
            if (auto pLocked = pWeak.lock()) ((*pLocked).*pMethod)(std::forward<Args>(args)...);
        });
    }

    DelegateHandle Add(Delegate<void(Args...)> delegate) {
        if (!delegate.IsBound()) return {};
        return m_pState->Add(nullptr, [delegate](Args... args) { delegate.Execute(std::forward<Args>(args)...); });
    }

    // C#-style sugar: auto handle = onChanged += [](...) { ... };
    template<class F>
        requires std::invocable<F&, Args...>
    DelegateHandle operator+=(F&& function) { return AddLambda(std::forward<F>(function)); }
    bool operator-=(DelegateHandle handle) { return Remove(handle); }

    bool Remove(DelegateHandle handle) { return m_pState->Remove(handle); }
    // Removes every binding registered with AddRaw/AddWeak for this object.
    std::size_t RemoveAll(const void* pOwner) { return m_pState->RemoveAll(pOwner); }
    void Clear() { m_pState->Clear(); }

    [[nodiscard]] bool IsBound() const { return m_pState->Count() > 0; }
    [[nodiscard]] std::size_t Count() const { return m_pState->Count(); }

    void Broadcast(Args... args) const { m_pState->Broadcast(args...); }

    // Internal: used by ScopedDelegateHandle to remove safely even if this
    // delegate was destroyed first.
    [[nodiscard]] std::weak_ptr<detail::IDelegateRemover> Remover() const { return m_pState; }

private:
    struct Entry {
        std::uint64_t nId = 0;
        const void* pOwner = nullptr;
        Callback function;
        bool bRemoved = false;
    };

    class State final : public detail::IDelegateRemover {
    public:
        DelegateHandle Add(const void* pOwner, Callback function) {
            if (!function) return {};
            const std::uint64_t nId = m_nNextId++;
            m_vEntries.push_back({nId, pOwner, std::move(function), false});
            return {nId};
        }

        bool Remove(DelegateHandle handle) override {
            for (auto& entry : m_vEntries) {
                if (entry.nId == handle.nId && !entry.bRemoved) {
                    entry.bRemoved = true;
                    Compact();
                    return true;
                }
            }
            return false;
        }

        std::size_t RemoveAll(const void* pOwner) {
            std::size_t nRemoved = 0;
            for (auto& entry : m_vEntries) {
                if (pOwner && entry.pOwner == pOwner && !entry.bRemoved) {
                    entry.bRemoved = true;
                    ++nRemoved;
                }
            }
            Compact();
            return nRemoved;
        }

        void Clear() {
            for (auto& entry : m_vEntries) entry.bRemoved = true;
            Compact();
        }

        std::size_t Count() const {
            std::size_t nCount = 0;
            for (const auto& entry : m_vEntries) nCount += entry.bRemoved ? 0 : 1;
            return nCount;
        }

        void Broadcast(Args&... args) {
            ++m_nBroadcastDepth;
            // Index-based iteration: callbacks may add bindings (vector growth)
            // or remove bindings (marked, compacted after the outermost call).
            const std::size_t nCount = m_vEntries.size();
            for (std::size_t nIndex = 0; nIndex < nCount; ++nIndex) {
                if (m_vEntries[nIndex].bRemoved) continue;
                Callback function = m_vEntries[nIndex].function;
                function(args...);
            }
            --m_nBroadcastDepth;
            Compact();
        }

    private:
        void Compact() {
            if (m_nBroadcastDepth > 0) return;
            std::erase_if(m_vEntries, [](const Entry& entry) { return entry.bRemoved; });
        }

        std::vector<Entry> m_vEntries;
        std::uint64_t m_nNextId = 1;
        int m_nBroadcastDepth = 0;
    };

    std::shared_ptr<State> m_pState;
};

// Removes its binding when destroyed. Store one per subscription as a member
// so a listener can never be called after it is destroyed.
class ScopedDelegateHandle {
public:
    ScopedDelegateHandle() = default;
    template<class... Args>
    ScopedDelegateHandle(const MulticastDelegate<Args...>& delegate, DelegateHandle handle)
        : m_pRemover(delegate.Remover()), m_handle(handle) {}
    ~ScopedDelegateHandle() { Release(); }

    ScopedDelegateHandle(const ScopedDelegateHandle&) = delete;
    ScopedDelegateHandle& operator=(const ScopedDelegateHandle&) = delete;
    ScopedDelegateHandle(ScopedDelegateHandle&& other) noexcept
        : m_pRemover(std::move(other.m_pRemover)), m_handle(other.m_handle) { other.m_handle.Reset(); }
    ScopedDelegateHandle& operator=(ScopedDelegateHandle&& other) noexcept {
        if (this != &other) {
            Release();
            m_pRemover = std::move(other.m_pRemover);
            m_handle = other.m_handle;
            other.m_handle.Reset();
        }
        return *this;
    }

    void Release() {
        if (auto pRemover = m_pRemover.lock(); pRemover && m_handle.IsValid()) pRemover->Remove(m_handle);
        m_pRemover.reset();
        m_handle.Reset();
    }
    [[nodiscard]] bool IsValid() const { return m_handle.IsValid() && !m_pRemover.expired(); }

private:
    std::weak_ptr<detail::IDelegateRemover> m_pRemover;
    DelegateHandle m_handle;
};

// A bundle of scoped subscriptions, convenient for objects that listen to
// several events (see EngineCallbacks.h).
class DelegateConnections {
public:
    template<class... Args>
    void Add(const MulticastDelegate<Args...>& delegate, DelegateHandle handle) {
        m_vHandles.emplace_back(delegate, handle);
    }
    void Clear() { m_vHandles.clear(); }
    [[nodiscard]] std::size_t Count() const { return m_vHandles.size(); }

private:
    std::vector<ScopedDelegateHandle> m_vHandles;
};

} // namespace dxs
