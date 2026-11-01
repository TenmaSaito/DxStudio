//==================================================================================
//
// デリゲート（関数の登録と呼び出し）のヘッダーファイル [Delegate.h]
//
//   Delegate<R(Args...)>        : 1 つの関数を登録する（UE の TDelegate、C# の Func / Action）
//   MulticastDelegate<Args...>  : 複数の関数を登録して一斉に呼ぶ（UE の TMulticastDelegate、C# の event）
//   DelegateHandle              : 登録 1 件を表す番号（後から解除するときに使う）
//   ScopedDelegateHandle        : 破棄されると自動で登録を解除する（RAII）
//
// 使用例
//   dxs::MulticastDelegate<int> onScore;
//   auto handle = onScore.AddLambda([](int nScore) { ... });
//   onScore.AddRaw(&player, &CPlayer::OnScore);
//   onScore.Broadcast(10);
//   onScore.Remove(handle);
//
// Broadcast() の実行中に登録・解除しても安全。実行中に登録した関数は、
// 次の Broadcast() から呼ばれる。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace dxs {

template<class Signature> class Delegate;

//**********************************************************************************
// *** シングルキャストデリゲート（関数 1 つ） ***
//**********************************************************************************
template<class R, class... Args>
class Delegate<R(Args...)> {
public:
    Delegate() = default;

    // 関数・ラムダ・関数オブジェクトから作る（Delegate 自身のコピーは除く）
    template<class F>
        requires std::invocable<F&, Args...> && (!std::same_as<std::decay_t<F>, Delegate>)
    Delegate(F&& function) : m_function(std::forward<F>(function)) {}

    // 通常の関数（static 関数）から作る
    static Delegate CreateStatic(R (*pFunction)(Args...)) { return Delegate(pFunction); }

    // オブジェクトとメンバー関数から作る（オブジェクトの寿命は呼び出し側が管理する）
    template<class T>
    static Delegate CreateRaw(T* pObject, R (T::*pMethod)(Args...)) {
        return Delegate([pObject, pMethod](Args... args) -> R { return (pObject->*pMethod)(std::forward<Args>(args)...); });
    }

    // const メンバー関数版
    template<class T>
    static Delegate CreateRaw(const T* pObject, R (T::*pMethod)(Args...) const) {
        return Delegate([pObject, pMethod](Args... args) -> R { return (pObject->*pMethod)(std::forward<Args>(args)...); });
    }

    // shared_ptr のオブジェクトから作る。オブジェクトが破棄された後は何もしない
    // （戻り値がある場合は R{} を返す）。
    template<class T>
    static Delegate CreateWeak(const std::shared_ptr<T>& pObject, R (T::*pMethod)(Args...)) {
        std::weak_ptr<T> pWeak = pObject;
        return Delegate([pWeak, pMethod](Args... args) -> R {
            if (auto pLocked = pWeak.lock()) return ((*pLocked).*pMethod)(std::forward<Args>(args)...);
            if constexpr (!std::is_void_v<R>) return R{};
        });
    }

    [[nodiscard]] bool IsBound() const noexcept { return static_cast<bool>(m_function); } // 関数が登録されているか
    explicit operator bool() const noexcept { return IsBound(); }
    void Unbind() noexcept { m_function = nullptr; } // 登録を解除

    // 呼び出し（未登録で呼ぶと std::bad_function_call 例外）
    R Execute(Args... args) const { return m_function(std::forward<Args>(args)...); }
    R operator()(Args... args) const { return m_function(std::forward<Args>(args)...); }

    // 登録が無くても安全な呼び出し（戻り値 void 用）。未登録なら false を返す。
    bool ExecuteIfBound(Args... args) const
        requires std::is_void_v<R>
    {
        if (!m_function) return false;
        m_function(std::forward<Args>(args)...);
        return true;
    }

private:
    std::function<R(Args...)> m_function; // 登録された関数
};

//**********************************************************************************
// *** 登録 1 件を表す番号 ***
//**********************************************************************************
// MulticastDelegate への登録 1 件を表す。既定値（0）は無効。
struct DelegateHandle {
    std::uint64_t nId = 0; // 登録番号（1 から）
    [[nodiscard]] bool IsValid() const noexcept { return nId != 0; }
    void Reset() noexcept { nId = 0; }
    friend bool operator==(const DelegateHandle&, const DelegateHandle&) = default;
};

namespace detail {
//**********************************************************************************
// *** 登録解除インターフェース（ScopedDelegateHandle 用） ***
//**********************************************************************************
// デリゲートの引数の型を知らなくても解除できるようにするための型消去
class IDelegateRemover {
public:
    virtual ~IDelegateRemover() = default;
    virtual bool Remove(DelegateHandle handle) = 0;
};
} // namespace detail

//**********************************************************************************
// *** マルチキャストデリゲート（複数の関数） ***
//**********************************************************************************
template<class... Args>
class MulticastDelegate {
public:
    using Callback = std::function<void(Args...)>; // 登録する関数の型

    MulticastDelegate() : m_pState(std::make_shared<State>()) {}
    // 登録リストのコピーはほぼ確実に不具合の元になる（解除した相手をコピー側が
    // 呼び続けてしまう）ため禁止する。ムーブは可。
    MulticastDelegate(const MulticastDelegate&) = delete;
    MulticastDelegate& operator=(const MulticastDelegate&) = delete;
    MulticastDelegate(MulticastDelegate&&) noexcept = default;
    MulticastDelegate& operator=(MulticastDelegate&&) noexcept = default;

    // ラムダ・関数オブジェクトを登録
    template<class F>
        requires std::invocable<F&, Args...>
    DelegateHandle AddLambda(F&& function) {
        return m_pState->Add(nullptr, Callback(std::forward<F>(function)));
    }

    // 通常の関数を登録
    DelegateHandle AddStatic(void (*pFunction)(Args...)) { return m_pState->Add(nullptr, Callback(pFunction)); }

    // オブジェクトとメンバー関数を登録（RemoveAll(pObject) でまとめて解除できる）
    template<class T>
    DelegateHandle AddRaw(T* pObject, void (T::*pMethod)(Args...)) {
        return m_pState->Add(pObject, [pObject, pMethod](Args... args) { (pObject->*pMethod)(std::forward<Args>(args)...); });
    }

    // const メンバー関数版
    template<class T>
    DelegateHandle AddRaw(const T* pObject, void (T::*pMethod)(Args...) const) {
        return m_pState->Add(pObject, [pObject, pMethod](Args... args) { (pObject->*pMethod)(std::forward<Args>(args)...); });
    }

    // shared_ptr のオブジェクトを登録（破棄後は呼ばれない）
    template<class T>
    DelegateHandle AddWeak(const std::shared_ptr<T>& pObject, void (T::*pMethod)(Args...)) {
        std::weak_ptr<T> pWeak = pObject;
        return m_pState->Add(pObject.get(), [pWeak, pMethod](Args... args) {
            if (auto pLocked = pWeak.lock()) ((*pLocked).*pMethod)(std::forward<Args>(args)...);
        });
    }

    // シングルキャストデリゲートを登録（未登録の物は無視）
    DelegateHandle Add(Delegate<void(Args...)> delegate) {
        if (!delegate.IsBound()) return {};
        return m_pState->Add(nullptr, [delegate](Args... args) { delegate.Execute(std::forward<Args>(args)...); });
    }

    // C# 風の書き方: auto handle = onChanged += [](...) { ... };
    template<class F>
        requires std::invocable<F&, Args...>
    DelegateHandle operator+=(F&& function) { return AddLambda(std::forward<F>(function)); }
    bool operator-=(DelegateHandle handle) { return Remove(handle); }

    // 登録 1 件を解除
    bool Remove(DelegateHandle handle) { return m_pState->Remove(handle); }
    // AddRaw / AddWeak でこのオブジェクトに対して登録した物をすべて解除
    std::size_t RemoveAll(const void* pOwner) { return m_pState->RemoveAll(pOwner); }
    // すべて解除
    void Clear() { m_pState->Clear(); }

    [[nodiscard]] bool IsBound() const { return m_pState->Count() > 0; } // 1 件以上登録されているか
    [[nodiscard]] std::size_t Count() const { return m_pState->Count(); } // 登録件数

    // 登録されたすべての関数を登録順に呼ぶ
    void Broadcast(Args... args) const { m_pState->Broadcast(args...); }

    // 内部用: ScopedDelegateHandle が、このデリゲートが先に破棄されても
    // 安全に解除できるようにするための参照
    [[nodiscard]] std::weak_ptr<detail::IDelegateRemover> Remover() const { return m_pState; }

private:
    //******************************************************************************
    // *** 登録 1 件分の情報 ***
    //******************************************************************************
    struct Entry {
        std::uint64_t nId = 0;        // 登録番号
        const void* pOwner = nullptr; // 登録したオブジェクト（RemoveAll 用、無ければ nullptr）
        Callback function;            // 呼び出す関数
        bool bRemoved = false;        // 解除済み（呼び出し中は印だけ付けて後で消す）
    };

    //******************************************************************************
    // *** 登録リスト本体 ***
    //******************************************************************************
    // shared_ptr で持つことで、ScopedDelegateHandle が weak_ptr で参照できる
    class State final : public detail::IDelegateRemover {
    public:
        // --- 登録処理 ---
        DelegateHandle Add(const void* pOwner, Callback function) {
            if (!function) return {}; // 空の関数は登録しない
            const std::uint64_t nId = m_nNextId++;
            m_vEntries.push_back({nId, pOwner, std::move(function), false});
            return {nId};
        }

        // --- 1 件の解除処理 ---
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

        // --- オブジェクト単位の解除処理 ---
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

        // --- 全解除処理 ---
        void Clear() {
            for (auto& entry : m_vEntries) entry.bRemoved = true;
            Compact();
        }

        // --- 有効な登録件数の取得処理 ---
        std::size_t Count() const {
            std::size_t nCount = 0;
            for (const auto& entry : m_vEntries) nCount += entry.bRemoved ? 0 : 1;
            return nCount;
        }

        // --- 一斉呼び出し処理 ---
        void Broadcast(Args&... args) {
            ++m_nBroadcastDepth;
            // 添字で回す: 呼び出し中に登録されても（配列が伸びても）安全で、
            // 解除は印だけ付けて一番外側の呼び出しが終わってから詰める。
            // 呼び出し開始時点の件数までしか回さないので、途中で増えた分は次回から。
            const std::size_t nCount = m_vEntries.size();
            for (std::size_t nIndex = 0; nIndex < nCount; ++nIndex) {
                if (m_vEntries[nIndex].bRemoved) continue;
                Callback function = m_vEntries[nIndex].function; // コピーしてから呼ぶ（配列の再確保対策）
                function(args...);
            }
            --m_nBroadcastDepth;
            Compact();
        }

    private:
        // --- 解除済みの登録を配列から取り除く処理（呼び出し中は行わない） ---
        void Compact() {
            if (m_nBroadcastDepth > 0) return;
            std::erase_if(m_vEntries, [](const Entry& entry) { return entry.bRemoved; });
        }

        std::vector<Entry> m_vEntries;   // 登録リスト（登録順）
        std::uint64_t m_nNextId = 1;     // 次に振る登録番号
        int m_nBroadcastDepth = 0;       // Broadcast の入れ子の深さ（呼び出し中は 1 以上）
    };

    std::shared_ptr<State> m_pState; // 登録リスト
};

//**********************************************************************************
// *** 自動解除ハンドル ***
//**********************************************************************************
// 破棄されると登録を解除する。購読する側のメンバー変数として 1 つずつ持てば、
// 破棄された後に呼ばれることがなくなる。
class ScopedDelegateHandle {
public:
    ScopedDelegateHandle() = default;
    template<class... Args>
    ScopedDelegateHandle(const MulticastDelegate<Args...>& delegate, DelegateHandle handle)
        : m_pRemover(delegate.Remover()), m_handle(handle) {}
    ~ScopedDelegateHandle() { Release(); }

    // コピー禁止（同じ登録を 2 回解除しないように）、ムーブは可
    ScopedDelegateHandle(const ScopedDelegateHandle&) = delete;
    ScopedDelegateHandle& operator=(const ScopedDelegateHandle&) = delete;
    ScopedDelegateHandle(ScopedDelegateHandle&& other) noexcept
        : m_pRemover(std::move(other.m_pRemover)), m_handle(other.m_handle) { other.m_handle.Reset(); }
    ScopedDelegateHandle& operator=(ScopedDelegateHandle&& other) noexcept {
        if (this != &other) {
            Release(); // 今持っている登録を先に解除
            m_pRemover = std::move(other.m_pRemover);
            m_handle = other.m_handle;
            other.m_handle.Reset();
        }
        return *this;
    }

    // --- 登録の解除処理（デリゲートが既に破棄されていれば何もしない） ---
    void Release() {
        if (auto pRemover = m_pRemover.lock(); pRemover && m_handle.IsValid()) pRemover->Remove(m_handle);
        m_pRemover.reset();
        m_handle.Reset();
    }
    // 登録がまだ有効か
    [[nodiscard]] bool IsValid() const { return m_handle.IsValid() && !m_pRemover.expired(); }

private:
    std::weak_ptr<detail::IDelegateRemover> m_pRemover; // 登録先（破棄済みかを判定できる）
    DelegateHandle m_handle;                            // 解除する登録
};

//**********************************************************************************
// *** 自動解除ハンドルのまとめ ***
//**********************************************************************************
// 複数のイベントを購読するオブジェクト向け（EngineCallbacks.h を参照）。
// 破棄または Clear() ですべての登録を解除する。
class DelegateConnections {
public:
    template<class... Args>
    void Add(const MulticastDelegate<Args...>& delegate, DelegateHandle handle) {
        m_vHandles.emplace_back(delegate, handle);
    }
    void Clear() { m_vHandles.clear(); }                               // すべて解除
    [[nodiscard]] std::size_t Count() const { return m_vHandles.size(); } // 保持している登録数

private:
    std::vector<ScopedDelegateHandle> m_vHandles; // 保持している登録
};

} // namespace dxs
