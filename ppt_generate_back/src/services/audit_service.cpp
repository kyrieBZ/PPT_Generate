#include "services/audit_service.h"

#include <cstring>
#include <sstream>

#include <mysql/mysql.h>

#include "logger.h"

AuditService::AuditService(std::shared_ptr<MySQLConnectionPool> pool)
    : pool_(std::move(pool)) {}

namespace {
// 对 val 中所有单引号、反斜杠进行转义，返回可安全嵌入 SQL 的字符串
std::string EscStr(MYSQL* conn, const std::string& val) {
  std::string buf(val.size() * 2 + 1, '\0');
  unsigned long len = mysql_real_escape_string(
      conn, buf.data(), val.c_str(), static_cast<unsigned long>(val.size()));
  buf.resize(len);
  return buf;
}

AuditLog RowToLog(MYSQL_ROW row, MYSQL_FIELD* fields, unsigned int num_fields) {
  AuditLog log;
  // 列顺序：id, operator_id, operator, action, target_type, target_id, detail, ip, created_at
  for (unsigned int i = 0; i < num_fields; ++i) {
    std::string col = fields[i].name;
    std::string val = row[i] ? row[i] : "";
    if      (col == "id")          log.id            = std::stoull(val.empty() ? "0" : val);
    else if (col == "operator_id") log.operator_id   = std::stoull(val.empty() ? "0" : val);
    else if (col == "operator")    log.operator_name = val;
    else if (col == "action")      log.action        = val;
    else if (col == "target_type") log.target_type   = val;
    else if (col == "target_id")   log.target_id     = val;
    else if (col == "detail")      log.detail        = val;
    else if (col == "ip")          log.ip            = val;
    else if (col == "created_at")  log.created_at    = val;
  }
  return log;
}
}  // namespace

void AuditService::Write(std::uint64_t   operator_id,
                         const std::string& operator_name,
                         const std::string& action,
                         const std::string& target_type,
                         const std::string& target_id,
                         const std::string& detail,
                         const std::string& ip) {
  try {
    auto conn_guard = pool_->GetConnection();
    MYSQL* conn = conn_guard.Get();
    if (!conn) {
      Logger::Warn("AuditService::Write — no DB connection");
      return;
    }

    std::ostringstream sql;
    sql << "INSERT INTO admin_audit_logs"
           " (operator_id, operator, action, target_type, target_id, detail, ip)"
           " VALUES ("
        << operator_id << ","
        << "'" << EscStr(conn, operator_name) << "',"
        << "'" << EscStr(conn, action)        << "',"
        << "'" << EscStr(conn, target_type)   << "',"
        << "'" << EscStr(conn, target_id)     << "',"
        << "'" << EscStr(conn, detail)        << "',"
        << "'" << EscStr(conn, ip)            << "'"
        << ")";

    if (mysql_query(conn, sql.str().c_str()) != 0) {
      Logger::Warn(std::string("AuditService::Write — mysql_query failed: ") +
                   mysql_error(conn));
    }
  } catch (const std::exception& ex) {
    Logger::Warn(std::string("AuditService::Write exception: ") + ex.what());
  }
}

bool AuditService::List(const AuditFilter& filter,
                        std::vector<AuditLog>& out,
                        int& total,
                        std::string& error) {
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "无法获取数据库连接"; return false; }

  // 构造 WHERE 子句
  std::ostringstream where;
  bool has_cond = false;
  auto AddCond = [&](const std::string& cond) {
    where << (has_cond ? " AND " : " WHERE ") << cond;
    has_cond = true;
  };

  if (!filter.action.empty()) {
    AddCond("action = '" + EscStr(conn, filter.action) + "'");
  }
  if (!filter.start_date.empty()) {
    AddCond("DATE(created_at) >= '" + EscStr(conn, filter.start_date) + "'");
  }
  if (!filter.end_date.empty()) {
    AddCond("DATE(created_at) <= '" + EscStr(conn, filter.end_date) + "'");
  }
  if (!filter.keyword.empty()) {
    std::string kw = EscStr(conn, filter.keyword);
    AddCond("(operator LIKE '%" + kw + "%' OR target_id LIKE '%" + kw + "%')");
  }

  // COUNT
  std::string count_sql = "SELECT COUNT(*) FROM admin_audit_logs" + where.str();
  if (mysql_query(conn, count_sql.c_str()) != 0) {
    error = std::string("查询总数失败: ") + mysql_error(conn);
    return false;
  }
  {
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) { error = "store_result failed"; return false; }
    MYSQL_ROW row = mysql_fetch_row(res);
    total = row && row[0] ? std::stoi(row[0]) : 0;
    mysql_free_result(res);
  }

  int page = std::max(1, filter.page);
  int page_size = std::max(1, std::min(100, filter.page_size));
  int offset = (page - 1) * page_size;

  std::ostringstream sql;
  sql << "SELECT id, operator_id, operator, action, target_type, target_id, detail, ip, created_at"
         " FROM admin_audit_logs"
      << where.str()
      << " ORDER BY created_at DESC"
      << " LIMIT " << page_size << " OFFSET " << offset;

  if (mysql_query(conn, sql.str().c_str()) != 0) {
    error = std::string("查询失败: ") + mysql_error(conn);
    return false;
  }
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) { error = "store_result failed"; return false; }

  MYSQL_FIELD* fields = mysql_fetch_fields(res);
  unsigned int num_fields = mysql_num_fields(res);
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(res))) {
    out.push_back(RowToLog(row, fields, num_fields));
  }
  mysql_free_result(res);
  return true;
}

bool AuditService::Export(const AuditFilter& filter,
                          std::vector<AuditLog>& out,
                          std::string& error) {
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "无法获取数据库连接"; return false; }

  std::ostringstream where;
  bool has_cond = false;
  auto AddCond = [&](const std::string& cond) {
    where << (has_cond ? " AND " : " WHERE ") << cond;
    has_cond = true;
  };

  if (!filter.action.empty()) {
    AddCond("action = '" + EscStr(conn, filter.action) + "'");
  }
  if (!filter.start_date.empty()) {
    AddCond("DATE(created_at) >= '" + EscStr(conn, filter.start_date) + "'");
  }
  if (!filter.end_date.empty()) {
    AddCond("DATE(created_at) <= '" + EscStr(conn, filter.end_date) + "'");
  }
  if (!filter.keyword.empty()) {
    std::string kw = EscStr(conn, filter.keyword);
    AddCond("(operator LIKE '%" + kw + "%' OR target_id LIKE '%" + kw + "%')");
  }

  std::ostringstream sql;
  sql << "SELECT id, operator_id, operator, action, target_type, target_id, detail, ip, created_at"
         " FROM admin_audit_logs"
      << where.str()
      << " ORDER BY created_at DESC LIMIT 5000";

  if (mysql_query(conn, sql.str().c_str()) != 0) {
    error = std::string("查询失败: ") + mysql_error(conn);
    return false;
  }
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) { error = "store_result failed"; return false; }

  MYSQL_FIELD* fields = mysql_fetch_fields(res);
  unsigned int num_fields = mysql_num_fields(res);
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(res))) {
    out.push_back(RowToLog(row, fields, num_fields));
  }
  mysql_free_result(res);
  return true;
}
