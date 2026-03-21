#include "services/fastdfs_client.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

#include "logger.h"

namespace {

// 执行外部命令，返回其 stdout 输出；失败抛出 std::runtime_error。
std::string ExecCommand(const std::string& cmd) {
  std::array<char, 512> buf{};
  std::string result;
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    throw std::runtime_error("popen 失败: " + cmd);
  }
  while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
    result += buf.data();
  }
  const int rc = pclose(pipe);
  if (rc != 0) {
    throw std::runtime_error("命令返回非零退出码 " + std::to_string(rc) +
                             "，输出: " + result.substr(0, 256));
  }
  return result;
}

// 去除字符串首尾空白字符。
std::string Trim(const std::string& s) {
  const auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return {};
  const auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

}  // namespace

FastDfsClient::FastDfsClient(FastDfsConfig config) : config_(std::move(config)) {}

bool FastDfsClient::IsEnabled() const {
  return config_.enabled && !config_.storage_http_url.empty();
}

std::string FastDfsClient::BuildAccessUrl(const std::string& file_id) const {
  if (file_id.empty()) return {};
  std::string base = config_.storage_http_url;
  if (!base.empty() && base.back() == '/') {
    base.pop_back();
  }
  return base + "/" + file_id;
}

bool FastDfsClient::UploadFile(const std::string& local_path,
                               const std::string& /*ext*/,
                               std::string& file_id,
                               std::string& error) const {
  if (!IsEnabled()) {
    error = "FastDFS 未启用";
    return false;
  }
  if (!std::filesystem::exists(local_path)) {
    error = "本地文件不存在: " + local_path;
    return false;
  }
  if (!std::filesystem::exists(config_.client_conf)) {
    error = "找不到 FastDFS 客户端配置文件: " + config_.client_conf;
    return false;
  }

  // 新版 FastDFS(v6.x) 已移除 Tracker HTTP 上传接口，改用命令行工具通过 TCP 上传。
  const std::string cmd = "fdfs_upload_file " +
                          config_.client_conf + " " +
                          local_path + " 2>&1";
  try {
    const std::string output = ExecCommand(cmd);
    const std::string fid = Trim(output);
    if (fid.empty() || fid.rfind("group", 0) != 0) {
      error = "无法解析 fdfs_upload_file 输出: " + fid.substr(0, 256);
      return false;
    }
    file_id = fid;
    return true;
  } catch (const std::runtime_error& e) {
    error = std::string("fdfs_upload_file 失败: ") + e.what();
    return false;
  }
}

bool FastDfsClient::DeleteFile(const std::string& file_id, std::string& error) const {
  if (!IsEnabled()) {
    error = "FastDFS 未启用";
    return false;
  }
  if (file_id.empty()) {
    error = "file_id 为空";
    return false;
  }
  if (!std::filesystem::exists(config_.client_conf)) {
    error = "找不到 FastDFS 客户端配置文件: " + config_.client_conf;
    return false;
  }

  const std::string cmd = "fdfs_delete_file " +
                          config_.client_conf + " " +
                          file_id + " 2>&1";
  try {
    ExecCommand(cmd);
    return true;
  } catch (const std::runtime_error& e) {
    const std::string msg = e.what();
    // errno 2 = ENOENT（文件不存在），视为删除成功
    if (msg.find("No such file") != std::string::npos ||
        msg.find("errno: 2") != std::string::npos ||
        msg.find("not exist") != std::string::npos) {
      return true;
    }
    error = std::string("fdfs_delete_file 失败: ") + msg;
    return false;
  }
}
