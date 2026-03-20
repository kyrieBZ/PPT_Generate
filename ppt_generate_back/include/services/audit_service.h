#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "database/mysql_connection_pool.h"

struct AuditLog {
  std::uint64_t id = 0;
  std::uint64_t operator_id = 0;
  std::string   operator_name;
  std::string   action;       // disable_user / delete_material / delete_ppt / …
  std::string   target_type;  // user / material / ppt / announcement
  std::string   target_id;
  std::string   detail;       // JSON 字符串（可选）
  std::string   ip;
  std::string   created_at;
};

struct AuditFilter {
  std::string action;      // 可选，精确匹配
  std::string start_date;  // 可选，YYYY-MM-DD
  std::string end_date;    // 可选，YYYY-MM-DD
  std::string keyword;     // 可选，匹配 operator/target_id
  int page = 1;
  int page_size = 30;
};

class AuditService {
 public:
  explicit AuditService(std::shared_ptr<MySQLConnectionPool> pool);

  /** 异步安全：可从任意线程调用。写失败只记录警告，不抛出。 */
  void Write(std::uint64_t operator_id,
             const std::string& operator_name,
             const std::string& action,
             const std::string& target_type,
             const std::string& target_id,
             const std::string& detail,
             const std::string& ip);

  bool List(const AuditFilter& filter,
            std::vector<AuditLog>& out,
            int& total,
            std::string& error);

  /** 导出所有符合条件的日志（不分页，供 CSV 导出，上限 5000 条）。 */
  bool Export(const AuditFilter& filter,
              std::vector<AuditLog>& out,
              std::string& error);

 private:
  std::shared_ptr<MySQLConnectionPool> pool_;
};
