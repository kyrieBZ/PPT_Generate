#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "database/mysql_connection_pool.h"

/** 数据库中存储的模板上架记录 */
struct TemplateEntry {
  std::uint64_t id = 0;
  std::string   template_id;    // 对应 catalog JSON 中的模板 id
  std::string   template_name;  // 冗余存储，方便展示
  bool          is_active = false;
  std::string   available_from; // DATETIME，上架开始时间（ISO 8601）
  std::string   available_to;   // DATETIME，上架结束时间（ISO 8601），空=永久
  std::uint64_t created_by = 0;
  std::string   created_at;
  std::string   updated_at;
};

class TemplateManagerService {
 public:
  explicit TemplateManagerService(std::shared_ptr<MySQLConnectionPool> pool);

  /** 确保数据库表存在（启动时调用一次）。 */
  void EnsureTable();

  /** 获取全部模板上架记录（管理员用）。 */
  bool ListAll(std::vector<TemplateEntry>& out, std::string& error);

  /** 上架/更新模板：不存在则 INSERT，存在则 UPDATE。 */
  bool Upsert(const TemplateEntry& entry, std::string& error);

  /** 下架模板（将 is_active 置 false）。 */
  bool Deactivate(const std::string& template_id, std::string& error);

  /** 删除模板记录（彻底移除）。 */
  bool Remove(const std::string& template_id, std::string& error);

  /**
   * 获取当前"对用户可见"的模板 id 列表：
   * is_active=true AND available_from <= NOW() AND (available_to IS NULL OR available_to >= NOW())
   */
  bool ListActiveIds(std::vector<std::string>& out, std::string& error);

 private:
  std::shared_ptr<MySQLConnectionPool> pool_;
};
