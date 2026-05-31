#pragma once

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

namespace upload_debug_log {

struct State {
  std::filesystem::path file_path;
  bool configured = false;
  std::mutex mutex;
};

inline State& GetState() {
  static State state;
  return state;
}

inline std::filesystem::path DetectRepoRoot() {
  auto current = std::filesystem::current_path();
  for (auto probe = current; !probe.empty(); probe = probe.parent_path()) {
    if (std::filesystem::exists(probe / "ppt_generate_back") &&
        std::filesystem::exists(probe / "ppt_generate_front")) {
      return probe;
    }
    if (probe == probe.parent_path()) {
      break;
    }
  }
  return current;
}

inline void Configure(const std::filesystem::path& repo_root) {
  auto& state = GetState();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.file_path = repo_root / "Log" / "image_upload_debug.log";
  std::filesystem::create_directories(state.file_path.parent_path());
  std::ofstream(state.file_path, std::ios::out | std::ios::trunc);
  state.configured = true;
}

inline const std::filesystem::path& EnsureConfigured() {
  auto& state = GetState();
  std::lock_guard<std::mutex> lock(state.mutex);
  if (!state.configured) {
    state.file_path = DetectRepoRoot() / "Log" / "image_upload_debug.log";
    std::filesystem::create_directories(state.file_path.parent_path());
    std::ofstream(state.file_path, std::ios::out | std::ios::trunc);
    state.configured = true;
  }
  return state.file_path;
}

inline void Append(const std::string& message) {
  auto& state = GetState();
  const auto file_path = EnsureConfigured();

  std::lock_guard<std::mutex> lock(state.mutex);
  std::ofstream ofs(file_path, std::ios::out | std::ios::app);
  if (!ofs) return;

  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm_now;
#ifdef _WIN32
  localtime_s(&tm_now, &time);
#else
  localtime_r(&time, &tm_now);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
  ofs << oss.str() << " " << message << "\n";
}

inline std::string PathString() {
  return EnsureConfigured().string();
}

}  // namespace upload_debug_log
