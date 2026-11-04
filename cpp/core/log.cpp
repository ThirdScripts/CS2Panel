#include "core/log.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <share.h>

namespace cs2n {
namespace {

double monotonicSeconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string timestampString() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local);
  return buffer;
}

}  // namespace

const char* logLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
  }
  return "INFO";
}

Logger& Logger::instance() {
  static Logger logger;
  return logger;
}

void Logger::startFile(const std::string& logsDirectory, const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (file_) {
    std::fclose(static_cast<FILE*>(file_));
    file_ = nullptr;
  }
  std::error_code ec;
  std::filesystem::create_directories(logsDirectory, ec);
  const std::string path = logsDirectory + "/" + name + "-" + timestampString() + ".log";
  FILE* file = nullptr;
  file=_fsopen(path.c_str(),"wb",_SH_DENYWR);
  if (file) {
    file_ = file;
    startedAt_ = monotonicSeconds();
  }
}

void Logger::stopFile() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (file_) {
    std::fclose(static_cast<FILE*>(file_));
    file_ = nullptr;
  }
}

void Logger::log(LogLevel level, const std::string& text) {
  std::lock_guard<std::mutex> lock(mutex_);
  LogEntry entry;
  entry.timestamp = startedAt_ > 0.0 ? monotonicSeconds() - startedAt_ : monotonicSeconds();
  entry.level = level;
  entry.text = text;
  if (level == LogLevel::Error) ++errorCount_;
  buffer_.push_back(entry);
  if (buffer_.size() > kCapacity) buffer_.pop_front();
  if (file_) {
    FILE* file = static_cast<FILE*>(file_);
    std::fprintf(file, "[%10.3f] %-5s %s\n", entry.timestamp, logLevelName(level), text.c_str());
    std::fflush(file);
  }
}

std::vector<LogEntry> Logger::recent(std::size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<LogEntry> out;
  const std::size_t count = limit < buffer_.size() ? limit : buffer_.size();
  out.reserve(count);
  for (std::size_t i = 0; i < count; ++i) out.push_back(buffer_[buffer_.size() - 1 - i]);
  return out;
}

std::size_t Logger::errorCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return errorCount_;
}

}  // namespace cs2n
