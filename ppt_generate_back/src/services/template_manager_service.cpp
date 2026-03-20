#include "services/template_manager_service.h"

#include <cstring>
#include <sstream>

#include <mysql/mysql.h>

#include "logger.h"

namespace {

std::string EscStr(MYSQL* conn, const std::string& val) {
  std::string buf(val.size() * 2 + 1, '\0');
  unsigned long len = mysql_real_escape_string(
      conn, buf.data(), val.c_str(), static_cast<unsigned long>(val.size()));
  buf.resize(len);
  return buf;
}

TemplateEntry RowToEntry(MYSQL_ROW row, MYSQL_FIELD* fields, unsigned int num_fields) {
  TemplateEntry e;
  for (unsigned int i = 0; i < num_fields; ++i) {
    std::string col = fields[i].name;
    std::string val = row[i] ? row[i] : "";
    if      (col == "id")            e.id            = val.empty() ? 0 : std::stoull(val);
    else if (col == "template_id")   e.template_id   = val;
    else if (col == "template_name") e.template_name = val;
    else if (col == "is_active")     e.is_active     = (val == "1");
    else if (col == "available_from") e.available_from = val;
    else if (col == "available_to")  e.available_to  = val;
    else if (col == "created_by")    e.created_by    = val.empty() ? 0 : std::stoull(val);
    else if (col == "created_at")    e.created_at    = val;
    else if (col == "updated_at")    e.updated_at    = val;
  }
  return e;
}

}  // namespace

TemplateManagerService::TemplateManagerService(std::shared_ptr<MySQLConnectionPool> pool)
    : pool_(std::move(pool)) {}

void TemplateManagerService::EnsureTable() {
  auto conn = pool_->Acquire();
  if (!conn) {
    Logger::Error("TemplateManagerService::EnsureTable: no DB connection");
    return;
  }

  const char* sql = R"(
    CREATE TABLE IF NOT EXISTS template_listings (
      id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
      template_id   VARCHAR(128)    NOT NULL,
      template_name VARCHAR(256)    NOT NULL DEFAULT '',
      is_active     TINYINT(1)      NOT NULL DEFAULT 0,
      available_from DATETIME       NOT NULL DEFAULT CURRENT_TIMESTAMP,
      available_to  DATETIME        NULL     DEFAULT NULL,
      created_by    BIGINT UNSIGNED NOT NULL DEFAULT 0,
      created_at    DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
      updated_at    DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
      PRIMARY KEY (id),
      UNIQUE KEY uq_template_id (template_id)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
  )";

  if (mysql_query(conn, sql) != 0) {
    Logger::Error(std::string("EnsureTable failed: ") + mysql_error(conn));
  }
  pool_->Release(conn);
}

bool TemplateManagerService::ListAll(std::vector<TemplateEntry>& out, std::string& error) {
  auto conn = pool_->Acquire();
  if (!conn) { error = "No DB connection"; return false; }

  const char* sql =
      "SELECT id, template_id, template_name, is_active, "
      "available_from, available_to, created_by, created_at, updated_at "
      "FROM template_listings ORDER BY updated_at DESC";

  if (mysql_query(conn, sql) != 0) {
    error = mysql_error(conn);
    pool_->Release(conn);
    return false;
  }

  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) {
    error = mysql_error(conn);
    pool_->Release(conn);
    return false;
  }

  MYSQL_FIELD*  fields     = mysql_fetch_fields(res);
  unsigned int  num_fields = mysql_num_fields(res);
  MYSQL_ROW     row;
  while ((row = mysql_fetch_row(res))) {
    out.push_back(RowToEntry(row, fields, num_fields));
  }
  mysql_free_result(res);
  pool_->Release(conn);
  return true;
}

bool TemplateManagerService::Upsert(const TemplateEntry& entry, std::string& error) {
  auto conn = pool_->Acquire();
  if (!conn) { error = "No DB connection"; return false; }

  std::string esc_id   = EscStr(conn, entry.template_id);
  std::string esc_name = EscStr(conn, entry.template_name);

  // available_from：若为空或特殊值 "NOW()"，则直接使用数据库函数 NOW()
  std::string from_expr;
  if (entry.available_from.empty() || entry.available_from == "NOW()") {
    from_expr = "NOW()";
  } else {
    from_expr = "'" + EscStr(conn, entry.available_from) + "'";
  }

  std::ostringstream ss;
  ss << "INSERT INTO template_listings "
        "(template_id, template_name, is_active, available_from, available_to, created_by) "
        "VALUES ('"
     << esc_id   << "', '"
     << esc_name << "', "
     << (entry.is_active ? 1 : 0) << ", "
     << from_expr << ", ";

  if (entry.available_to.empty()) {
    ss << "NULL";
  } else {
    ss << "'" << EscStr(conn, entry.available_to) << "'";
  }
  ss << ", " << entry.created_by << ") "
        "ON DUPLICATE KEY UPDATE "
        "template_name=VALUES(template_name), "
        "is_active=VALUES(is_active), "
        "available_from=VALUES(available_from), "
        "available_to=VALUES(available_to), "
        "updated_at=CURRENT_TIMESTAMP";

  std::string sql = ss.str();
  if (mysql_query(conn, sql.c_str()) != 0) {
    error = mysql_error(conn);
    pool_->Release(conn);
    return false;
  }
  pool_->Release(conn);
  return true;
}

bool TemplateManagerService::Deactivate(const std::string& template_id, std::string& error) {
  auto conn = pool_->Acquire();
  if (!conn) { error = "No DB connection"; return false; }

  std::string esc_id = EscStr(conn, template_id);
  std::string sql =
      "UPDATE template_listings SET is_active=0, updated_at=CURRENT_TIMESTAMP "
      "WHERE template_id='" + esc_id + "'";

  if (mysql_query(conn, sql.c_str()) != 0) {
    error = mysql_error(conn);
    pool_->Release(conn);
    return false;
  }
  pool_->Release(conn);
  return true;
}

bool TemplateManagerService::Remove(const std::string& template_id, std::string& error) {
  auto conn = pool_->Acquire();
  if (!conn) { error = "No DB connection"; return false; }

  std::string esc_id = EscStr(conn, template_id);
  std::string sql = "DELETE FROM template_listings WHERE template_id='" + esc_id + "'";

  if (mysql_query(conn, sql.c_str()) != 0) {
    error = mysql_error(conn);
    pool_->Release(conn);
    return false;
  }
  pool_->Release(conn);
  return true;
}

bool TemplateManagerService::ListActiveIds(std::vector<std::string>& out, std::string& error) {
  auto conn = pool_->Acquire();
  if (!conn) { error = "No DB connection"; return false; }

  const char* sql =
      "SELECT template_id FROM template_listings "
      "WHERE is_active=1 "
      "  AND available_from <= NOW() "
      "  AND (available_to IS NULL OR available_to >= NOW())";

  if (mysql_query(conn, sql) != 0) {
    error = mysql_error(conn);
    pool_->Release(conn);
    return false;
  }

  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) {
    error = mysql_error(conn);
    pool_->Release(conn);
    return false;
  }

  MYSQL_ROW row;
  while ((row = mysql_fetch_row(res))) {
    if (row[0]) out.emplace_back(row[0]);
  }
  mysql_free_result(res);
  pool_->Release(conn);
  return true;
}
