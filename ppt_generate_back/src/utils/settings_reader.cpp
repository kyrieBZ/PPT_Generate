#include "utils/settings_reader.h"

#include <sstream>

#include <mysql/mysql.h>

#include "logger.h"

namespace SettingsReader {

namespace {

/** 查询单个 key 的 value，失败返回 nullopt */
std::string QueryValue(MySQLConnectionPool& pool,
                       const std::string&   key,
                       bool&                found) {
  found = false;
  auto conn_guard = pool.GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) {
    Logger::Warn("SettingsReader: DB connection unavailable for key=" + key);
    return {};
  }

  // 安全转义 key
  std::string escaped(key.size() * 2 + 1, '\0');
  unsigned long elen = mysql_real_escape_string(
      conn, escaped.data(), key.c_str(), static_cast<unsigned long>(key.size()));
  escaped.resize(elen);

  std::string sql = "SELECT value FROM system_settings WHERE `key`='" + escaped + "' LIMIT 1";
  if (mysql_query(conn, sql.c_str()) != 0) {
    // 表可能不存在（首次启动还未调用 GetSettings），静默降级
    return {};
  }
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) return {};

  MYSQL_ROW row = mysql_fetch_row(res);
  std::string value;
  if (row && row[0]) {
    value = row[0];
    found = true;
  }
  mysql_free_result(res);
  return value;
}

}  // namespace

std::string GetString(MySQLConnectionPool& pool,
                      const std::string&   key,
                      const std::string&   default_value) {
  bool found = false;
  auto val = QueryValue(pool, key, found);
  return found ? val : default_value;
}

bool GetBool(MySQLConnectionPool& pool,
             const std::string&   key,
             bool                 default_value) {
  bool found = false;
  auto val = QueryValue(pool, key, found);
  if (!found) return default_value;
  return (val == "true" || val == "1");
}

int GetInt(MySQLConnectionPool& pool,
           const std::string&   key,
           int                  default_value) {
  bool found = false;
  auto val = QueryValue(pool, key, found);
  if (!found) return default_value;
  try {
    return std::stoi(val);
  } catch (...) {
    return default_value;
  }
}

}  // namespace SettingsReader
