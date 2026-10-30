#include "Log.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <chrono>
#include <deque>
#include <mutex>

namespace dxs {
namespace {
struct LogState {
    std::mutex mutex;
    std::deque<LogEntry> dEntries;
    std::uint64_t nNextSequence = 1;
    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
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
    OnLog().Broadcast(entry);
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
