#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app_config.h"
#include "database/mysql_connection_pool.h"
#include "models/material.h"

class MaterialService {
 public:
  MaterialService(std::shared_ptr<MySQLConnectionPool> pool,
                  MaterialConfig material_config,
                  std::string qwen_api_key,
                  std::string python_binary);

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

  const MaterialConfig& config() const { return material_config_; }

 private:
  std::shared_ptr<MySQLConnectionPool> pool_;
  MaterialConfig material_config_;
  std::string qwen_api_key_;
  std::string python_binary_;
};
