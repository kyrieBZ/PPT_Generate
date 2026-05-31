#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app_config.h"
#include "database/mysql_connection_pool.h"
#include "models/material.h"
#include "services/fastdfs_client.h"
#include "services/knowledge_rag_service.h"

class MaterialService {
 public:
  MaterialService(std::shared_ptr<MySQLConnectionPool> pool,
                  MaterialConfig material_config,
                  std::string qwen_api_key,
                  std::string python_binary,
                  std::shared_ptr<FastDfsClient> fastdfs_client = nullptr,
                  std::shared_ptr<KnowledgeRagService> knowledge_rag_service = nullptr);

  /** 设置 RAG 服务（可在构造后注入，用于解决循环依赖） */
  void SetKnowledgeRagService(std::shared_ptr<KnowledgeRagService> service) {
    knowledge_rag_service_ = std::move(service);
  }

  /**
   * 将指定素材的提取文本写入 RAG 知识库（向量化 + 存入 Qdrant）。
   * 若 RAG 服务不可用则静默跳过。
   * @param material_id  素材 ID
   * @return             成功索引的块数；-1 表示服务不可用或失败
   */
  int IndexMaterialToRag(const std::string& material_id);

  /** 创建材料记录（status=pending），返回 material_id */
  bool CreateMaterial(std::uint64_t user_id,
                      const std::string& filename,
                      const std::string& file_type,
                      const std::string& file_path,
                      std::uint64_t file_size,
                      Material& out_material,
                      std::string& error);

  /** 异步触发提取（在调用线程中执行，建议在线程池中调用） */
  void RunExtraction(const std::string& material_id);

  /** 查询单条材料（仅限该用户） */
  bool GetMaterial(const std::string& material_id,
                   std::uint64_t user_id,
                   Material& out_material,
                   std::string& error);

  /** 查询用户材料列表 */
  std::vector<Material> ListMaterials(std::uint64_t user_id, std::string& error);

  /** 更新提取结果 */
  bool UpdateExtractResult(const std::string& material_id,
                           const std::string& status,
                           const std::string& extract_result,
                           const std::string& error_msg,
                           std::string& db_error);

  /** 删除材料记录及文件 */
  bool DeleteMaterial(const std::string& material_id,
                      std::uint64_t user_id,
                      std::string& error);

  /** 用户修改提取结果 */
  bool SaveExtractResult(const std::string& material_id,
                         std::uint64_t user_id,
                         const std::string& extract_result_json,
                         std::string& error);

  struct AdminMaterialFilter {
    std::uint64_t user_id = 0;   // 0 表示不过滤
    std::string status;          // "" 表示全部；提取状态：pending/extracting/completed/failed
    std::string file_type;       // "" 表示全部
    std::string review_status;   // "" 表示全部；审核状态：unreviewed/pass/violation/unknown
    int page = 1;
    int page_size = 20;
  };

  struct AdminMaterialStats {
    int total = 0;
    std::uint64_t total_size = 0;
    int completed = 0;
    int pending = 0;
    int failed = 0;
  };

  /** 管理员查询全量素材列表（分页+筛选） */
  std::vector<Material> AdminListMaterials(const AdminMaterialFilter& filter,
                                           int& out_total,
                                           std::string& error);

  /** 管理员查询素材存储统计 */
  AdminMaterialStats AdminGetStats(std::string& error);

  /**
   * 管理员强制删除任意素材（不限 user_id），并向素材所属用户写入删除通知。
   * @param material_id   素材 ID
   * @param delete_reason 管理员填写的删除原因
   * @param deleted_by    操作管理员用户名
   * @param error         错误信息
   */
  bool AdminDeleteMaterial(const std::string& material_id,
                           const std::string& delete_reason,
                           const std::string& deleted_by,
                           std::string& error);

  struct DeletionNotice {
    std::uint64_t id = 0;
    std::string filename;
    std::string file_type;
    std::uint64_t file_size = 0;
    std::string delete_reason;
    std::string deleted_by;
    std::uint64_t created_at = 0;
  };

  /** 获取用户未读删除通知列表 */
  std::vector<DeletionNotice> GetDeletionNotices(std::uint64_t user_id, std::string& error);

  /** 标记通知已读（ids 为空则标记该用户全部未读） */
  bool MarkNoticesRead(std::uint64_t user_id,
                       const std::vector<std::uint64_t>& ids,
                       std::string& error);

  struct ReviewResult {
    std::string result;    // "pass" / "violation" / "unknown"
    std::string reason;    // 审核结论说明
    std::uint64_t reviewed_at = 0;
  };

  /**
   * 管理员触发 AI 内容审核：读取素材提取文本，调用 Qwen 判断是否违规，
   * 将结论写入 materials.review_result 列，并返回审核结果。
   * @param material_id  素材 ID
   * @param api_key      通义千问 API Key
   * @param timeout_sec  超时秒数
   * @param out_review   审核结论（成功时填充）
   * @param error        错误信息（失败时填充）
   */
  bool AdminReviewMaterial(const std::string& material_id,
                           const std::string& api_key,
                           std::uint32_t timeout_sec,
                           ReviewResult& out_review,
                           std::string& error);

  /**
   * 获取任意素材的提取内容（管理员专用，不限 user_id）
   */
  bool AdminGetMaterial(const std::string& material_id,
                        Material& out_material,
                        std::string& error);

  const MaterialConfig& config() const { return material_config_; }

  /**
   * 将已提取完成的素材上传到 FastDFS，更新数据库中的 fastdfs_file_id/fastdfs_url/storage_type。
   * 若 FastDFS 未启用或上传失败，仅记录警告，不影响素材可用性。
   * @param material_id  素材 ID
   * @param delete_local 上传成功后是否删除本地文件
   */
  void UploadToFastDfs(const std::string& material_id, bool delete_local = false);

 private:
  bool UpdateFastDfsInfo(const std::string& material_id,
                         const std::string& fastdfs_file_id,
                         const std::string& fastdfs_url,
                         const std::string& storage_type);
  void DecorateRagStatus(Material& material) const;
  void DecorateRagStatus(std::vector<Material>& materials) const;

  std::shared_ptr<MySQLConnectionPool> pool_;
  MaterialConfig material_config_;
  std::string qwen_api_key_;
  std::string python_binary_;
  std::shared_ptr<FastDfsClient> fastdfs_client_;
  std::shared_ptr<KnowledgeRagService> knowledge_rag_service_;
};
