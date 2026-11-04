// Thread-safe logger: ring buffer for the UI plus an optional file sink.
#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace cs2n {

enum class LogLevel { Info = 0, Warning = 1, Error = 2 };

struct LogEntry {
  double timestamp = 0.0;  // seconds since logger start
  LogLevel level = LogLevel::Info;
  std::string text;
};

class Logger {
 public:
  static Logger& instance();

  // Opens logs/<name>-<timestamp>.log. Safe to call once at startup.
  void startFile(const std::string& logsDirectory, const std::string& name);
  void stopFile();

  void log(LogLevel level, const std::string& text);
  void info(const std::string& text) { log(LogLevel::Info, text); }
  void warn(const std::string& text) { log(LogLevel::Warning, text); }
  void error(const std::string& text) { log(LogLevel::Error, text); }

  // Newest entries first, at most `limit` of them.
  std::vector<LogEntry> recent(std::size_t limit) const;
  std::size_t errorCount() const;

 private:
  Logger() = default;
  ~Logger() { stopFile(); }
  mutable std::mutex mutex_;
  std::deque<LogEntry> buffer_;
  void* file_ = nullptr;  // FILE*, kept opaque to avoid including <cstdio> in the header
  double startedAt_ = 0.0;
  std::size_t errorCount_ = 0;
  static constexpr std::size_t kCapacity = 2000;
};

const char* logLevelName(LogLevel level);

}  // namespace cs2n
