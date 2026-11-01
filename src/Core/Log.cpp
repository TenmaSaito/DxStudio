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
struct LogState {
    std::mutex mutex;
    std::deque<LogEntry> dEntries;
    std::uint64_t nNextSequence = 1;
    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
    std::vector<LogEntry> vPending; // written off the main thread, not broadcast yet
    std::atomic<bool> bHasMainThread{false};
    std::thread::id mainThread;
};

LogState& State() {
    static LogState state;
    return state;
}

const char* LevelPrefix(LogLevel level) {
    switch (level) {
    case LogLevel::LOG_WARNING: return "[Warning] ";
    case LogLevel::LOG_ERROR: return "[Error] ";
    default: return "[Log] ";
    }
}
} // namespace

MulticastDelegate<const LogEntry&>& Debug::OnLog() {
    static MulticastDelegate<const LogEntry&> onLog;
    return onLog;
}

void Debug::Log(std::string_view message) { Write(LogLevel::LOG_INFO, message); }
void Debug::LogWarning(std::string_view message) { Write(LogLevel::LOG_WARNING, message); }
void Debug::LogError(std::string_view message) { Write(LogLevel::LOG_ERROR, message); }

void Debug::Write(LogLevel level, std::string_view message) {
    LogEntry entry;
    {
        auto& state = State();
        std::lock_guard lock(state.mutex);
        entry.level = level;
        entry.sMessage.assign(message.data(), message.size());
        entry.dTimeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - state.startTime).count();
        entry.nSequence = state.nNextSequence++;
        state.dEntries.push_back(entry);
        while (state.dEntries.size() > MAX_HISTORY) state.dEntries.pop_front();
    }
    const std::string sDebugLine = std::string(LevelPrefix(level)) + entry.sMessage + "\n";
    OutputDebugStringA(sDebugLine.c_str());
    auto& state = State();
    if (state.bHasMainThread && std::this_thread::get_id() != state.mainThread) {
        // Listeners (the Console window) belong to the main thread.
        std::lock_guard lock(state.mutex);
        state.vPending.push_back(std::move(entry));
        return;
    }
    DispatchPending(); // keep the order: earlier worker entries first
    OnLog().Broadcast(entry);
}

void Debug::SetMainThread() {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    state.mainThread = std::this_thread::get_id();
    state.bHasMainThread = true;
}

void Debug::DispatchPending() {
    auto& state = State();
    std::vector<LogEntry> vEntries;
    {
        std::lock_guard lock(state.mutex);
        if (state.vPending.empty()) return;
        vEntries.swap(state.vPending);
    }
    for (const auto& entry : vEntries) OnLog().Broadcast(entry);
}

std::vector<LogEntry> Debug::History() {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    return {state.dEntries.begin(), state.dEntries.end()};
}

void Debug::ClearHistory() {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    state.dEntries.clear();
}

} // namespace dxs
