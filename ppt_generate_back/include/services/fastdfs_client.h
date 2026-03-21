#pragma once

#include <cstdint>
#include <string>

#include "app_config.h"

/**
 * FastDFS 客户端（通过命令行工具调用，兼容 FastDFS v6.x）。
 *
 * 上传：调用 fdfs_upload_file <client_conf> <local_path>（TCP 22122 端口）。
 *       新版 FastDFS(v6.x) 已移除 Tracker HTTP 上传接口，不再使用 8080 端口。
 * 下载/访问：通过 storage_http_url（Nginx 8888 端口）按 file_id 访问。
 * 删除：调用 fdfs_delete_file <client_conf> <file_id>。
 */
class FastDfsClient {
 public:
  explicit FastDfsClient(FastDfsConfig config);

  bool IsEnabled() const;

  /**
   * 上传文件到 FastDFS。
   * @param local_path  本地文件绝对路径
   * @param ext         文件扩展名（不含点，如 "pdf"）
   * @param file_id     输出：FastDFS 返回的文件 ID（如 group1/M00/00/00/xxx.pdf）
   * @param error       失败时填充错误描述
   * @return 成功返回 true
   */
  bool UploadFile(const std::string& local_path,
                  const std::string& ext,
                  std::string& file_id,
                  std::string& error) const;

  /**
   * 删除 FastDFS 中的文件。
   * @param file_id  FastDFS 文件 ID
   * @param error    失败时填充错误描述
   * @return 成功返回 true
   */
  bool DeleteFile(const std::string& file_id, std::string& error) const;

  /**
   * 构建 HTTP 访问 URL。
   * @param file_id  FastDFS 文件 ID
   * @return 完整访问 URL（storage_http_url + "/" + file_id）
   */
  std::string BuildAccessUrl(const std::string& file_id) const;

  const FastDfsConfig& config() const { return config_; }

 private:
  FastDfsConfig config_;
};
