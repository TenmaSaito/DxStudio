#pragma once

// Unity-style logging: dxs::Debug::Log / LogWarning / LogError.
// Every message is kept in a bounded history and broadcast through
// Debug::OnLog(), which the editor Console subscribes to.

#include "Delegate.h"

#include <string>
#include <string_view>
#include <vector>

namespace dxs {

enum class LogLevel : unsigned char { LOG_INFO, LOG_WARNING, LOG_ERROR };

struct LogEntry {
    LogLevel level = LogLevel::LOG_INFO;
    std::string sMessage;
    double dTimeSeconds = 0.0; // seconds since the process started
    std::uint64_t nSequence = 0;
};

class Debug {
public:
    static void Log(std::string_view message);
    static void LogWarning(std::string_view message);
    static void LogError(std::string_view message);
    static void Write(LogLevel level, std::string_view message);

    // Fired on the calling thread for every new log entry.
    static MulticastDelegate<const LogEntry&>& OnLog();
    static std::vector<LogEntry> History();
    static void ClearHistory();

    static constexpr std::size_t MAX_HISTORY = 2000;
};

} // namespace dxs
