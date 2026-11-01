//==================================================================================
//
// ログ出力クラスのソースファイル [Log.cpp]
//
// 履歴は複数スレッドから書かれるため mutex で守る。通知（OnLog）は
// メインスレッドが決まっていればそのスレッドだけで行う。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Log.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace dxs {
namespace {
//**********************************************************************************
// *** ログの内部状態 ***
//**********************************************************************************
struct LogState {
    std::mutex mutex;                    // 以下のメンバーを守るロック
    std::deque<LogEntry> dEntries;       // 履歴（古い順）
    std::uint64_t nNextSequence = 1;     // 次に振る通し番号
    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now(); // 経過時間の基準
    std::vector<LogEntry> vPending;      // 別スレッドで書かれ、まだ通知していないログ
    std::atomic<bool> bHasMainThread{false}; // メインスレッドが登録済みか
    std::thread::id mainThread;          // 通知を行うスレッド
};

// --- 内部状態の取得処理（初回呼び出し時に作成） ---
LogState& State() {
    static LogState state;
    return state;
}

// --- 重要度ごとの行頭文字列の取得処理 ---
const char* LevelPrefix(LogLevel level) {
    switch (level) {
    case LogLevel::LOG_WARNING: return "[Warning] ";
    case LogLevel::LOG_ERROR: return "[Error] ";
    default: return "[Log] ";
    }
}
} // namespace

//==================================================================================
// --- ログ通知の取得処理 ---
//==================================================================================
MulticastDelegate<const LogEntry&>& Debug::OnLog() {
    static MulticastDelegate<const LogEntry&> onLog;
    return onLog;
}

//==================================================================================
// --- 重要度別の出力処理 ---
//==================================================================================
void Debug::Log(std::string_view message) { Write(LogLevel::LOG_INFO, message); }
void Debug::LogWarning(std::string_view message) { Write(LogLevel::LOG_WARNING, message); }
void Debug::LogError(std::string_view message) { Write(LogLevel::LOG_ERROR, message); }

//==================================================================================
// --- ログの出力処理 ---
//==================================================================================
void Debug::Write(LogLevel level, std::string_view message) {
    LogEntry entry;
    { // 履歴へ追加（ロック中）
        auto& state = State();
        std::lock_guard lock(state.mutex);
        entry.level = level;
        entry.sMessage.assign(message.data(), message.size());
        entry.dTimeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - state.startTime).count();
        entry.nSequence = state.nNextSequence++;
        state.dEntries.push_back(entry);
        while (state.dEntries.size() > MAX_HISTORY) state.dEntries.pop_front(); // 上限を超えたら古い物から捨てる
    }
    // Visual Studio の出力ウィンドウにも出す
    const std::string sDebugLine = std::string(LevelPrefix(level)) + entry.sMessage + "\n";
    OutputDebugStringA(sDebugLine.c_str());
    auto& state = State();
    if (state.bHasMainThread && std::this_thread::get_id() != state.mainThread) {
        // 通知先（コンソールのウィンドウ）はメインスレッドの物なので、ここでは貯めておくだけ
        std::lock_guard lock(state.mutex);
        state.vPending.push_back(std::move(entry));
        return;
    }
    DispatchPending(); // 順番を守るため、先に別スレッドの分を通知する
    OnLog().Broadcast(entry);
}

//==================================================================================
// --- メインスレッドの登録処理 ---
//==================================================================================
void Debug::SetMainThread() {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    state.mainThread = std::this_thread::get_id();
    state.bHasMainThread = true;
}

//==================================================================================
// --- 待機中ログの通知処理 ---
//==================================================================================
void Debug::DispatchPending() {
    auto& state = State();
    std::vector<LogEntry> vEntries;
    { // 待機中の分を取り出す（通知はロックの外で行う）
        std::lock_guard lock(state.mutex);
        if (state.vPending.empty()) return;
        vEntries.swap(state.vPending);
    }
    for (const auto& entry : vEntries) OnLog().Broadcast(entry);
}

//==================================================================================
// --- 履歴の取得処理 ---
//==================================================================================
std::vector<LogEntry> Debug::History() {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    return {state.dEntries.begin(), state.dEntries.end()};
}

//==================================================================================
// --- 履歴の消去処理 ---
//==================================================================================
void Debug::ClearHistory() {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    state.dEntries.clear();
}

} // namespace dxs
