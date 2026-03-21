#pragma once

#include <memory>
#include <optional>
#include <string>

#include "database/mysql_connection_pool.h"
#include "services/fastdfs_client.h"

/**
 * 管理模板文件在 FastDFS 中的映射关系（template_fastdfs_map 表）。
 *
 * 职责：
 *  - 建表（EnsureTable）
 *  - 查询指定模板的 FastDFS 文件 ID（pptx / thumbnail / analysis）
 *  - 保存映射记录（由迁移脚本或 OfficePlus 导入流程调用）
 *  - 删除映射记录并同步删除 FastDFS 对象（模板下架时调用）
 */
class TemplateFastDfsService {
 public:
  TemplateFastDfsService(std::shared_ptr<MySQLConnectionPool> pool,
                         std::shared_ptr<FastDfsClient> fastdfs_client);

  /** 确保 template_fastdfs_map 表存在（应用启动时调用一次） */
  void EnsureTable();

  struct TemplateEntry {
    std::string template_id;
    std::string pptx_file_id;        // FastDFS pptx 文件 ID
    std::string pptx_url;            // FastDFS pptx 访问 URL
    std::string thumbnail_file_id;   // FastDFS 缩略图文件 ID
    std::string thumbnail_url;       // FastDFS 缩略图访问 URL
    std::string analysis_file_id;    // FastDFS 分析 JSON 文件 ID（可选）
    std::string analysis_url;        // FastDFS 分析 JSON 访问 URL（可选）
  };

  /** 查询模板的 FastDFS 映射信息，不存在返回 nullopt */
  std::optional<TemplateEntry> GetEntry(const std::string& template_id) const;

  /** 保存/更新模板映射（INSERT ... ON DUPLICATE KEY UPDATE） */
  bool UpsertEntry(const TemplateEntry& entry, std::string& error);

  /** 删除模板映射，并可选地同步删除 FastDFS 中的文件 */
  bool RemoveEntry(const std::string& template_id,
                   bool delete_from_fastdfs,
                   std::string& error);

  /**
   * 将本地模板文件上传到 FastDFS 并保存映射。
   * @param template_id   模板 ID（与 templates.json catalog 中的 id 对应）
   * @param pptx_path     本地 .pptx 文件路径（空则跳过）
   * @param thumbnail_path 本地缩略图路径（空则跳过）
   * @param error         失败时填充错误描述
   * @return 成功返回 true
   */
  bool UploadTemplate(const std::string& template_id,
                      const std::string& pptx_path,
                      const std::string& thumbnail_path,
                      std::string& error);

 private:
  std::shared_ptr<MySQLConnectionPool> pool_;
  std::shared_ptr<FastDfsClient> fastdfs_client_;
};
