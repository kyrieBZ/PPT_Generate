#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

class Logger {
 public:
  enum class Level { kInfo, kWarn, kError, kDebug };

  static void SetLevel(Level level) { GetInstance().level_ = level; }

  static void Info(const std::string& message) { Log(Level::kInfo, "INFO", message); }
  static void Warn(const std::string& message) { Log(Level::kWarn, "WARN", message); }
  static void Error(const std::string& message) { Log(Level::kError, "ERROR", message); }
  static void Debug(const std::string& message) { Log(Level::kDebug, "DEBUG", message); }

 private:
  Logger() = default;

  static Logger& GetInstance() {
    static Logger instance;
    return instance;
  }

  static void Log(Level level, const char* label, const std::string& message) {
    auto& self = GetInstance();
    if (level < self.level_) {
      return;
    }

    std::lock_guard<std::mutex> lock(self.mutex_);
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &time);
#else
    localtime_r(&time, &tm_now);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
    std::cerr << oss.str() << " [" << label << "] " << message << std::endl;
  }

  Level level_ = Level::kInfo;
  std::mutex mutex_;
};
