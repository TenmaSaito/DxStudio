//==================================================================================
//
// ログ出力クラスのヘッダーファイル [Log.h]
//
// Unity と同じ使い方のログ: dxs::Debug::Log / LogWarning / LogError。
// すべてのメッセージは上限付きの履歴に残り、Debug::OnLog() で通知される
// （エディターのコンソールはこれを購読して表示する）。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Delegate.h"

#include <string>
#include <string_view>
#include <vector>

namespace dxs {

//**********************************************************************************
// *** ログの重要度 ***
//**********************************************************************************
enum class LogLevel : unsigned char { LOG_INFO, LOG_WARNING, LOG_ERROR };

//**********************************************************************************
// *** ログ 1 件分の情報 ***
//**********************************************************************************
struct LogEntry {
    LogLevel level = LogLevel::LOG_INFO; // 重要度
    std::string sMessage;                // メッセージ（UTF-8）
    double dTimeSeconds = 0.0;           // プロセス開始からの経過秒
    std::uint64_t nSequence = 0;         // 通し番号（1 から）
};

//**********************************************************************************
// *** ログ出力クラス ***
//**********************************************************************************
class Debug {
public:
    static void Log(std::string_view message);        // 通常のログ
    static void LogWarning(std::string_view message); // 警告
    static void LogError(std::string_view message);   // エラー
    static void Write(LogLevel level, std::string_view message); // 重要度を指定して出力

    // 新しいログが出るたびに呼ばれる通知。呼び出したスレッドで通知されるが、
    // SetMainThread() 後はそのスレッドでのみ通知する（別スレッドのログは
    // DispatchPending() が呼ばれるまで待つ）。
    static MulticastDelegate<const LogEntry&>& OnLog();
    // UI を持つプログラム（エディター）が UI スレッドで 1 回呼ぶ
    static void SetMainThread();
    // 別スレッドから出て待っているログを通知する（メインスレッドで定期的に呼ぶ）
    static void DispatchPending();
    static std::vector<LogEntry> History(); // 履歴のコピーを取得
    static void ClearHistory();             // 履歴を消去

    static constexpr std::size_t MAX_HISTORY = 2000; // 履歴に残す最大件数
};

} // namespace dxs
