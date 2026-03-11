#include "services/material_service.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <mysql/mysql.h>
#include <nlohmann/json.hpp>

#include "logger.h"

namespace {

std::string GenerateUUID() {
  const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  static std::uint64_t counter = 0;
  ++counter;
  char buf[37];
  const auto hi = static_cast<std::uint32_t>((now >> 32) & 0xFFFFFFFF);
  const auto lo = static_cast<std::uint32_t>(now & 0xFFFFFFFF);
  const auto c  = static_cast<std::uint32_t>(counter & 0xFFFFFFFF);
  std::snprintf(buf, sizeof(buf),
                "%08x-%04x-%04x-%04x-%08x%04x",
                hi,
                static_cast<std::uint16_t>(lo >> 16),
                static_cast<std::uint16_t>(lo & 0xFFFF),
                static_cast<std::uint16_t>(c >> 16),
                c,
                static_cast<std::uint16_t>(hi & 0xFFFF));
  return buf;
}

std::uint64_t NowSeconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

Material RowToMaterial(MYSQL_ROW row, unsigned long* lengths) {
  Material m;
  if (row[0] && lengths[0]) m.id            = std::string(row[0], lengths[0]);
  if (row[1]) m.user_id       = std::stoull(row[1]);
  if (row[2] && lengths[2]) m.filename      = std::string(row[2], lengths[2]);
  if (row[3] && lengths[3]) m.file_type     = std::string(row[3], lengths[3]);
  if (row[4] && lengths[4]) m.file_path     = std::string(row[4], lengths[4]);
  if (row[5]) m.file_size     = std::stoull(row[5]);
  if (row[6] && lengths[6]) m.status        = std::string(row[6], lengths[6]);
  if (row[7] && lengths[7]) m.extract_result = std::string(row[7], lengths[7]);
  if (row[8] && lengths[8]) m.error_msg     = std::string(row[8], lengths[8]);
  if (row[9])  m.created_at  = std::stoull(row[9]);
  if (row[10]) m.updated_at  = std::stoull(row[10]);
  return m;
}

}  // namespace

MaterialService::MaterialService(std::shared_ptr<MySQLConnectionPool> pool,
                                 MaterialConfig material_config,
                                 std::string qwen_api_key,
                                 std::string python_binary)
    : pool_(std::move(pool)),
      material_config_(std::move(material_config)),
      qwen_api_key_(std::move(qwen_api_key)),
      python_binary_(std::move(python_binary)) {}

bool MaterialService::CreateMaterial(std::uint64_t user_id,
                                     const std::string& filename,
                                     const std::string& file_type,
                                     const std::string& file_path,
                                     std::uint64_t file_size,
                                     Material& out_material,
                                     std::string& error) {
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) {
    error = "无法获取数据库连接";
    return false;
  }

  const std::string id = GenerateUUID();
  const std::uint64_t now = NowSeconds();
  const std::string status = "pending";

  const std::string sql = R"(
    INSERT INTO materials
      (id, user_id, filename, file_type, file_path, file_size, status, created_at, updated_at)
    VALUES (?, ?, ?, ?, ?, ?, ?, FROM_UNIXTIME(?), FROM_UNIXTIME(?))
  )";

  MYSQL_STMT* stmt = mysql_stmt_init(conn);
  if (!stmt) { error = "无法初始化SQL语句"; return false; }
  if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
    mysql_stmt_close(stmt);
    error = "SQL准备失败";
    return false;
  }

  MYSQL_BIND params[9];
  memset(params, 0, sizeof(params));

  params[0].buffer_type   = MYSQL_TYPE_STRING;
  params[0].buffer        = (void*)id.c_str();
  params[0].buffer_length = id.length();

  unsigned long long uid_val = static_cast<unsigned long long>(user_id);
  params[1].buffer_type   = MYSQL_TYPE_LONGLONG;
  params[1].buffer        = &uid_val;
  params[1].is_unsigned   = 1;

  params[2].buffer_type   = MYSQL_TYPE_STRING;
  params[2].buffer        = (void*)filename.c_str();
  params[2].buffer_length = filename.length();

  params[3].buffer_type   = MYSQL_TYPE_STRING;
  params[3].buffer        = (void*)file_type.c_str();
  params[3].buffer_length = file_type.length();

  params[4].buffer_type   = MYSQL_TYPE_STRING;
  params[4].buffer        = (void*)file_path.c_str();
  params[4].buffer_length = file_path.length();

  unsigned long long fsize_val = static_cast<unsigned long long>(file_size);
  params[5].buffer_type   = MYSQL_TYPE_LONGLONG;
  params[5].buffer        = &fsize_val;
  params[5].is_unsigned   = 1;

  params[6].buffer_type   = MYSQL_TYPE_STRING;
  params[6].buffer        = (void*)status.c_str();
  params[6].buffer_length = status.length();

  unsigned long long now_val = static_cast<unsigned long long>(now);
  params[7].buffer_type   = MYSQL_TYPE_LONGLONG;
  params[7].buffer        = &now_val;
  params[7].is_unsigned   = 1;

  params[8].buffer_type   = MYSQL_TYPE_LONGLONG;
  params[8].buffer        = &now_val;
  params[8].is_unsigned   = 1;

  if (mysql_stmt_bind_param(stmt, params) != 0 ||
      mysql_stmt_execute(stmt) != 0) {
    error = std::string("SQL执行失败: ") + mysql_stmt_error(stmt);
    mysql_stmt_close(stmt);
    return false;
  }
  mysql_stmt_close(stmt);

  out_material.id           = id;
  out_material.user_id      = user_id;
  out_material.filename     = filename;
  out_material.file_type    = file_type;
  out_material.file_path    = file_path;
  out_material.file_size    = file_size;
  out_material.status       = status;
  out_material.created_at   = now;
  out_material.updated_at   = now;
  return true;
}

bool MaterialService::GetMaterial(const std::string& material_id,
                                  std::uint64_t user_id,
                                  Material& out_material,
                                  std::string& error) {
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "无法获取数据库连接"; return false; }

  const std::string sql = R"(
    SELECT id, user_id, filename, file_type, file_path, file_size, status,
           extract_result, error_msg,
           UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at)
    FROM materials WHERE id = ? AND user_id = ?
  )";

  MYSQL_STMT* stmt = mysql_stmt_init(conn);
  if (!stmt) { error = "无法初始化SQL语句"; return false; }
  if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
    mysql_stmt_close(stmt);
    error = "SQL准备失败";
    return false;
  }

  MYSQL_BIND params[2];
  memset(params, 0, sizeof(params));
  params[0].buffer_type   = MYSQL_TYPE_STRING;
  params[0].buffer        = (void*)material_id.c_str();
  params[0].buffer_length = material_id.length();
  unsigned long long uid_val = static_cast<unsigned long long>(user_id);
  params[1].buffer_type   = MYSQL_TYPE_LONGLONG;
  params[1].buffer        = &uid_val;
  params[1].is_unsigned   = 1;

  if (mysql_stmt_bind_param(stmt, params) != 0 ||
      mysql_stmt_execute(stmt) != 0) {
    error = "SQL执行失败";
    mysql_stmt_close(stmt);
    return false;
  }

  MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
  if (!meta) { mysql_stmt_close(stmt); error = "无结果元数据"; return false; }
  mysql_stmt_store_result(stmt);

  if (mysql_stmt_num_rows(stmt) == 0) {
    mysql_free_result(meta);
    mysql_stmt_close(stmt);
    error = "材料不存在";
    return false;
  }

  // Use mysql_stmt_fetch with dynamic result
  mysql_free_result(meta);
  mysql_stmt_close(stmt);

  // Re-query with mysql_query for simplicity (result set is small)
  std::ostringstream q;
  // Escape id manually (it's a UUID, safe chars only)
  q << "SELECT id, user_id, filename, file_type, file_path, file_size, status, "
    << "extract_result, error_msg, "
    << "UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at) "
    << "FROM materials WHERE id = '" << material_id << "' AND user_id = " << user_id;

  if (mysql_query(conn, q.str().c_str()) != 0) {
    error = "查询失败";
    return false;
  }
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) { error = "无结果"; return false; }
  MYSQL_ROW row = mysql_fetch_row(res);
  if (!row) { mysql_free_result(res); error = "材料不存在"; return false; }
  unsigned long* lengths = mysql_fetch_lengths(res);
  out_material = RowToMaterial(row, lengths);
  mysql_free_result(res);
  return true;
}

std::vector<Material> MaterialService::ListMaterials(std::uint64_t user_id, std::string& error) {
  std::vector<Material> result;
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "无法获取数据库连接"; return result; }

  std::ostringstream q;
  q << "SELECT id, user_id, filename, file_type, file_path, file_size, status, "
    << "extract_result, error_msg, "
    << "UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at) "
    << "FROM materials WHERE user_id = " << user_id
    << " ORDER BY created_at DESC LIMIT 50";

  if (mysql_query(conn, q.str().c_str()) != 0) {
    error = "查询失败";
    return result;
  }
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) return result;
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(res)) != nullptr) {
    unsigned long* lengths = mysql_fetch_lengths(res);
    result.push_back(RowToMaterial(row, lengths));
  }
  mysql_free_result(res);
  return result;
}

bool MaterialService::UpdateExtractResult(const std::string& material_id,
                                          const std::string& status,
                                          const std::string& extract_result,
                                          const std::string& error_msg,
                                          std::string& db_error) {
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { db_error = "无法获取数据库连接"; return false; }

  const std::string sql = R"(
    UPDATE materials SET status=?, extract_result=?, error_msg=?, updated_at=FROM_UNIXTIME(?)
    WHERE id=?
  )";

  MYSQL_STMT* stmt = mysql_stmt_init(conn);
  if (!stmt) { db_error = "无法初始化SQL语句"; return false; }
  if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
    mysql_stmt_close(stmt);
    db_error = "SQL准备失败";
    return false;
  }

  MYSQL_BIND params[5];
  memset(params, 0, sizeof(params));

  params[0].buffer_type   = MYSQL_TYPE_STRING;
  params[0].buffer        = (void*)status.c_str();
  params[0].buffer_length = status.length();

  params[1].buffer_type   = MYSQL_TYPE_STRING;
  params[1].buffer        = (void*)extract_result.c_str();
  params[1].buffer_length = extract_result.length();

  params[2].buffer_type   = MYSQL_TYPE_STRING;
  params[2].buffer        = (void*)error_msg.c_str();
  params[2].buffer_length = error_msg.length();

  const std::uint64_t now = NowSeconds();
  unsigned long long now_val = static_cast<unsigned long long>(now);
  params[3].buffer_type   = MYSQL_TYPE_LONGLONG;
  params[3].buffer        = &now_val;
  params[3].is_unsigned   = 1;

  params[4].buffer_type   = MYSQL_TYPE_STRING;
  params[4].buffer        = (void*)material_id.c_str();
  params[4].buffer_length = material_id.length();

  if (mysql_stmt_bind_param(stmt, params) != 0 ||
      mysql_stmt_execute(stmt) != 0) {
    db_error = std::string("SQL执行失败: ") + mysql_stmt_error(stmt);
    mysql_stmt_close(stmt);
    return false;
  }
  mysql_stmt_close(stmt);
  return true;
}

bool MaterialService::DeleteMaterial(const std::string& material_id,
                                     std::uint64_t user_id,
                                     std::string& error) {
  // First get the file path
  Material mat;
  std::string get_error;
  if (!GetMaterial(material_id, user_id, mat, get_error)) {
    error = get_error;
    return false;
  }

  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "无法获取数据库连接"; return false; }

  std::ostringstream q;
  q << "DELETE FROM materials WHERE id = '" << material_id << "' AND user_id = " << user_id;
  if (mysql_query(conn, q.str().c_str()) != 0) {
    error = "删除失败";
    return false;
  }

  // Remove file from disk
  if (!mat.file_path.empty()) {
    std::error_code ec;
    std::filesystem::remove(mat.file_path, ec);
  }
  return true;
}

bool MaterialService::SaveExtractResult(const std::string& material_id,
                                        std::uint64_t user_id,
                                        const std::string& extract_result_json,
                                        std::string& error) {
  // Verify ownership
  Material mat;
  if (!GetMaterial(material_id, user_id, mat, error)) {
    return false;
  }

  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "无法获取数据库连接"; return false; }

  const std::string sql = R"(
    UPDATE materials SET extract_result=?, updated_at=FROM_UNIXTIME(?) WHERE id=? AND user_id=?
  )";

  MYSQL_STMT* stmt = mysql_stmt_init(conn);
  if (!stmt) { error = "无法初始化SQL语句"; return false; }
  if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
    mysql_stmt_close(stmt);
    error = "SQL准备失败";
    return false;
  }

  MYSQL_BIND params[4];
  memset(params, 0, sizeof(params));

  params[0].buffer_type   = MYSQL_TYPE_STRING;
  params[0].buffer        = (void*)extract_result_json.c_str();
  params[0].buffer_length = extract_result_json.length();

  const std::uint64_t now = NowSeconds();
  unsigned long long now_val = static_cast<unsigned long long>(now);
  params[1].buffer_type   = MYSQL_TYPE_LONGLONG;
  params[1].buffer        = &now_val;
  params[1].is_unsigned   = 1;

  params[2].buffer_type   = MYSQL_TYPE_STRING;
  params[2].buffer        = (void*)material_id.c_str();
  params[2].buffer_length = material_id.length();

  unsigned long long uid_val = static_cast<unsigned long long>(user_id);
  params[3].buffer_type   = MYSQL_TYPE_LONGLONG;
  params[3].buffer        = &uid_val;
  params[3].is_unsigned   = 1;

  if (mysql_stmt_bind_param(stmt, params) != 0 ||
      mysql_stmt_execute(stmt) != 0) {
    error = std::string("SQL执行失败: ") + mysql_stmt_error(stmt);
    mysql_stmt_close(stmt);
    return false;
  }
  mysql_stmt_close(stmt);
  return true;
}

void MaterialService::RunExtraction(const std::string& material_id) {
  // Mark as extracting
  std::string db_error;
  UpdateExtractResult(material_id, "extracting", "", "", db_error);

  // Find the material (admin query without user_id check)
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) {
    Logger::Error("MaterialService::RunExtraction: no DB connection for " + material_id);
    UpdateExtractResult(material_id, "failed", "", "无法获取数据库连接", db_error);
    return;
  }

  std::ostringstream q;
  q << "SELECT id, user_id, filename, file_type, file_path, file_size, status, "
    << "extract_result, error_msg, "
    << "UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at) "
    << "FROM materials WHERE id = '" << material_id << "'";

  if (mysql_query(conn, q.str().c_str()) != 0) {
    UpdateExtractResult(material_id, "failed", "", "查询材料失败", db_error);
    return;
  }
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) {
    UpdateExtractResult(material_id, "failed", "", "查询材料无结果", db_error);
    return;
  }
  MYSQL_ROW row = mysql_fetch_row(res);
  if (!row) {
    mysql_free_result(res);
    UpdateExtractResult(material_id, "failed", "", "材料记录不存在", db_error);
    return;
  }
  unsigned long* lengths = mysql_fetch_lengths(res);
  Material mat = RowToMaterial(row, lengths);
  mysql_free_result(res);

  if (!std::filesystem::exists(mat.file_path)) {
    UpdateExtractResult(material_id, "failed", "", "文件不存在: " + mat.file_path, db_error);
    return;
  }

  if (python_binary_.empty() || material_config_.extract_script.empty()) {
    UpdateExtractResult(material_id, "failed", "", "提取脚本未配置", db_error);
    return;
  }

  // Build command: python3 extract_material.py --file <path> --type <type> --api-key <key>
  std::ostringstream cmd;
  cmd << "\"" << python_binary_ << "\""
      << " \"" << material_config_.extract_script << "\""
      << " --file \"" << mat.file_path << "\""
      << " --type \"" << mat.file_type << "\""
      << " --api-key \"" << qwen_api_key_ << "\"";

  // Capture stdout via temp file
  const std::string tmp_output = mat.file_path + ".extract_out.json";
  cmd << " > \"" << tmp_output << "\" 2>&1";

  Logger::Info("MaterialService: running extraction for " + material_id);
  const int ret = std::system(cmd.str().c_str());

  // Read output
  std::string output;
  {
    std::ifstream f(tmp_output);
    if (f.is_open()) {
      std::ostringstream ss;
      ss << f.rdbuf();
      output = ss.str();
    }
    std::error_code ec;
    std::filesystem::remove(tmp_output, ec);
  }

  if (ret != 0 || output.empty()) {
    const std::string err_msg = output.empty() ? "提取脚本无输出" : output.substr(0, 500);
    Logger::Error("MaterialService: extraction failed for " + material_id + ": " + err_msg);
    UpdateExtractResult(material_id, "failed", "", err_msg, db_error);
    return;
  }

  // Validate JSON
  try {
    // Trim output
    const auto start = output.find_first_not_of(" \t\r\n");
    const auto end   = output.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) {
      throw std::runtime_error("空输出");
    }
    output = output.substr(start, end - start + 1);

    // Check for error field
    auto parsed = nlohmann::json::parse(output);
    if (parsed.contains("error")) {
      const std::string err_msg = parsed["error"].get<std::string>();
      Logger::Error("MaterialService: extraction script error for " + material_id + ": " + err_msg);
      UpdateExtractResult(material_id, "failed", "", err_msg, db_error);
      return;
    }
  } catch (const std::exception& ex) {
    Logger::Error("MaterialService: JSON parse error for " + material_id + ": " + std::string(ex.what()));
    UpdateExtractResult(material_id, "failed", "", std::string("JSON解析失败: ") + ex.what(), db_error);
    return;
  }

  Logger::Info("MaterialService: extraction completed for " + material_id);
  UpdateExtractResult(material_id, "completed", output, "", db_error);
}
