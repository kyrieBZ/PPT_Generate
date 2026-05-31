#include "services/material_service.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <curl/curl.h>
#include <mysql/mysql.h>
#include <nlohmann/json.hpp>

#include "logger.h"
#include "services/fastdfs_client.h"

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

// 标准列顺序（15 列）：
//   0:id  1:user_id  2:filename  3:file_type  4:file_path  5:file_size
//   6:status  7:extract_result  8:error_msg  9:review_result
//   10:UNIX_TIMESTAMP(created_at)  11:UNIX_TIMESTAMP(updated_at)
//   12:fastdfs_file_id  13:fastdfs_url  14:storage_type
Material RowToMaterial(MYSQL_ROW row, unsigned long* lengths) {
  Material m;
  if (row[0] && lengths[0])  m.id              = std::string(row[0], lengths[0]);
  if (row[1])                m.user_id         = std::stoull(row[1]);
  if (row[2] && lengths[2])  m.filename        = std::string(row[2], lengths[2]);
  if (row[3] && lengths[3])  m.file_type       = std::string(row[3], lengths[3]);
  if (row[4] && lengths[4])  m.file_path       = std::string(row[4], lengths[4]);
  if (row[5])                m.file_size       = std::stoull(row[5]);
  if (row[6] && lengths[6])  m.status          = std::string(row[6], lengths[6]);
  if (row[7] && lengths[7])  m.extract_result  = std::string(row[7], lengths[7]);
  if (row[8] && lengths[8])  m.error_msg       = std::string(row[8], lengths[8]);
  if (row[9] && lengths[9])  m.review_result   = std::string(row[9], lengths[9]);
  if (row[10])               m.created_at      = std::stoull(row[10]);
  if (row[11])               m.updated_at      = std::stoull(row[11]);
  if (row[12] && lengths[12]) m.fastdfs_file_id = std::string(row[12], lengths[12]);
  if (row[13] && lengths[13]) m.fastdfs_url     = std::string(row[13], lengths[13]);
  if (row[14] && lengths[14]) m.storage_type    = std::string(row[14], lengths[14]);
  return m;
}

}  // namespace

MaterialService::MaterialService(std::shared_ptr<MySQLConnectionPool> pool,
                                 MaterialConfig material_config,
                                 std::string qwen_api_key,
                                 std::string python_binary,
                                 std::shared_ptr<FastDfsClient> fastdfs_client,
                                 std::shared_ptr<KnowledgeRagService> knowledge_rag_service)
    : pool_(std::move(pool)),
      material_config_(std::move(material_config)),
      qwen_api_key_(std::move(qwen_api_key)),
      python_binary_(std::move(python_binary)),
      fastdfs_client_(std::move(fastdfs_client)),
      knowledge_rag_service_(std::move(knowledge_rag_service)) {}

void MaterialService::DecorateRagStatus(Material& material) const {
  material.rag_chunk_count = 0;

  if (!knowledge_rag_service_ || !knowledge_rag_service_->IsAvailable()) {
    material.rag_status = "unavailable";
    return;
  }

  if (material.status == "failed") {
    material.rag_status = "extract_failed";
    return;
  }

  if (material.status != "completed" || material.extract_result.empty()) {
    material.rag_status = "waiting_extract";
    return;
  }

  std::string rag_error;
  const int chunk_count = knowledge_rag_service_->CountMaterialChunks(
      material.id, material.user_id, rag_error);
  if (chunk_count < 0) {
    Logger::Warn("MaterialService::DecorateRagStatus: count failed for " +
                 material.id + ": " + rag_error);
    material.rag_status = "not_indexed";
    return;
  }

  material.rag_chunk_count = chunk_count;
  material.rag_status = chunk_count > 0 ? "indexed" : "not_indexed";
}

void MaterialService::DecorateRagStatus(std::vector<Material>& materials) const {
  for (auto& material : materials) {
    DecorateRagStatus(material);
  }
}

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
  out_material.rag_status   =
      (knowledge_rag_service_ && knowledge_rag_service_->IsAvailable())
          ? "waiting_extract"
          : "unavailable";
  out_material.rag_chunk_count = 0;
  return true;
}

bool MaterialService::GetMaterial(const std::string& material_id,
                                  std::uint64_t user_id,
                                  Material& out_material,
                                  std::string& error) {
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "无法获取数据库连接"; return false; }

  // material_id is a UUID (hex + dashes only) — safe to inline.
  // user_id is a numeric — safe to inline.
  std::ostringstream q;
  q << "SELECT id, user_id, filename, file_type, file_path, file_size, status, "
    << "extract_result, error_msg, review_result, "
    << "UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at), "
    << "IFNULL(fastdfs_file_id,''), IFNULL(fastdfs_url,''), IFNULL(storage_type,'local') "
    << "FROM materials WHERE id = '" << material_id << "' AND user_id = " << user_id;

  if (mysql_query(conn, q.str().c_str()) != 0) {
    error = "查询失败: " + std::string(mysql_error(conn));
    return false;
  }
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) { error = "无结果集"; return false; }
  MYSQL_ROW row = mysql_fetch_row(res);
  if (!row) {
    mysql_free_result(res);
    error = "材料不存在";
    return false;
  }
  unsigned long* lengths = mysql_fetch_lengths(res);
  out_material = RowToMaterial(row, lengths);
  mysql_free_result(res);
  DecorateRagStatus(out_material);
  return true;
}

std::vector<Material> MaterialService::ListMaterials(std::uint64_t user_id, std::string& error) {
  std::vector<Material> result;
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "无法获取数据库连接"; return result; }

  std::ostringstream q;
  q << "SELECT id, user_id, filename, file_type, file_path, file_size, status, "
    << "extract_result, error_msg, review_result, "
    << "UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at), "
    << "IFNULL(fastdfs_file_id,''), IFNULL(fastdfs_url,''), IFNULL(storage_type,'local') "
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
  DecorateRagStatus(result);
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

  // 若已上传 FastDFS，删除远程文件
  if (!mat.fastdfs_file_id.empty() && fastdfs_client_ && fastdfs_client_->IsEnabled()) {
    std::string fdfs_err;
    Logger::Info("DeleteMaterial: deleting FastDFS file " + mat.fastdfs_file_id);
    if (!fastdfs_client_->DeleteFile(mat.fastdfs_file_id, fdfs_err)) {
      Logger::Warn("DeleteMaterial: FastDFS delete failed for " + mat.fastdfs_file_id + ": " + fdfs_err);
    } else {
      Logger::Info("DeleteMaterial: FastDFS delete success for " + mat.fastdfs_file_id);
    }
  } else if (!mat.fastdfs_file_id.empty()) {
    Logger::Warn("DeleteMaterial: FastDFS file_id exists but client not enabled, skipping remote delete: " + mat.fastdfs_file_id);
  }
  // Remove file from disk
  if (!mat.file_path.empty()) {
    std::error_code ec;
    std::filesystem::remove(mat.file_path, ec);
  }

  // 同步删除 RAG 知识库中该素材的向量块
  if (knowledge_rag_service_ && knowledge_rag_service_->IsAvailable()) {
    std::string rag_err;
    knowledge_rag_service_->RemoveMaterial(material_id, rag_err);
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
    << "extract_result, error_msg, review_result, "
    << "UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at), "
    << "IFNULL(fastdfs_file_id,''), IFNULL(fastdfs_url,''), IFNULL(storage_type,'local') "
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

  // 提取完成后异步上传到 FastDFS（若已配置）
  if (fastdfs_client_ && fastdfs_client_->IsEnabled()) {
    UploadToFastDfs(material_id, fastdfs_client_->config().delete_local_after_upload);
  }

  // 提取完成后自动写入 RAG 知识库（若已配置 KnowledgeRagService）
  if (knowledge_rag_service_ && knowledge_rag_service_->IsAvailable()) {
    const int chunks = IndexMaterialToRag(material_id);
    if (chunks >= 0) {
      Logger::Info("MaterialService: RAG indexed " + std::to_string(chunks) +
                   " chunks for material=" + material_id);
    } else {
      Logger::Warn("MaterialService: RAG indexing failed for material=" + material_id);
    }
  }
}

int MaterialService::IndexMaterialToRag(const std::string& material_id) {
  if (!knowledge_rag_service_ || !knowledge_rag_service_->IsAvailable()) {
    return -1;
  }

  // 查询素材（不限 user_id，内部调用）
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) {
    Logger::Warn("MaterialService::IndexMaterialToRag: no DB connection for " + material_id);
    return -1;
  }

  std::ostringstream q;
  q << "SELECT id, user_id, filename, file_type, file_path, file_size, status, "
    << "extract_result, error_msg, review_result, "
    << "UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at), "
    << "IFNULL(fastdfs_file_id,''), IFNULL(fastdfs_url,''), IFNULL(storage_type,'local') "
    << "FROM materials WHERE id = '" << material_id << "'";

  if (mysql_query(conn, q.str().c_str()) != 0) {
    Logger::Warn("MaterialService::IndexMaterialToRag: query failed for " + material_id);
    return -1;
  }
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) return -1;
  MYSQL_ROW row = mysql_fetch_row(res);
  if (!row) {
    mysql_free_result(res);
    Logger::Warn("MaterialService::IndexMaterialToRag: material not found: " + material_id);
    return -1;
  }
  unsigned long* lengths = mysql_fetch_lengths(res);
  Material mat = RowToMaterial(row, lengths);
  mysql_free_result(res);

  if (mat.status != "completed" || mat.extract_result.empty()) {
    Logger::Warn("MaterialService::IndexMaterialToRag: material not ready: " + material_id +
                 " status=" + mat.status);
    return -1;
  }

  // 从 extract_result JSON 提取可索引文本
  const std::string indexable_text =
      KnowledgeRagService::BuildIndexableText(mat.extract_result);
  if (indexable_text.empty()) {
    Logger::Warn("MaterialService::IndexMaterialToRag: no indexable text for " + material_id);
    return -1;
  }

  std::string rag_error;
  return knowledge_rag_service_->IndexMaterial(
      material_id, mat.user_id, indexable_text, mat.filename, rag_error);
}

std::vector<Material> MaterialService::AdminListMaterials(const AdminMaterialFilter& filter,
                                                          int& out_total,
                                                          std::string& error) {
  std::vector<Material> result;
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "无法获取数据库连接"; return result; }

  std::ostringstream where;
  where << " WHERE 1=1";
  if (filter.user_id > 0) {
    where << " AND user_id = " << filter.user_id;
  }
  if (!filter.status.empty()) {
    // Escape: status is enum-like, only allow safe chars
    std::string safe_status;
    for (char c : filter.status) {
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') safe_status.push_back(c);
    }
    if (!safe_status.empty()) {
      where << " AND status = '" << safe_status << "'";
    }
  }
  if (!filter.file_type.empty()) {
    std::string safe_type;
    for (char c : filter.file_type) {
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') safe_type.push_back(c);
    }
    if (!safe_type.empty()) {
      where << " AND file_type = '" << safe_type << "'";
    }
  }
  if (!filter.review_status.empty()) {
    if (filter.review_status == "unreviewed") {
      // 未审核：review_result 为空或 null
      where << " AND (review_result IS NULL OR review_result = '')";
    } else {
      // pass / violation / unknown：从 JSON 提取 result 字段
      std::string safe_rv;
      for (char c : filter.review_status) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') safe_rv.push_back(c);
      }
      if (!safe_rv.empty()) {
        where << " AND JSON_VALUE(review_result, '$.result') = '" << safe_rv << "'";
      }
    }
  }

  // Count query
  const std::string count_sql = "SELECT COUNT(*) FROM materials" + where.str();
  if (mysql_query(conn, count_sql.c_str()) != 0) {
    error = "统计查询失败";
    return result;
  }
  {
    MYSQL_RES* res = mysql_store_result(conn);
    if (res) {
      MYSQL_ROW row = mysql_fetch_row(res);
      if (row && row[0]) out_total = std::atoi(row[0]);
      mysql_free_result(res);
    }
  }

  const int page = std::max(1, filter.page);
  const int page_size = std::max(1, std::min(100, filter.page_size));
  const int offset = (page - 1) * page_size;

  // Data query — join with users to get username
  std::ostringstream data_sql;
  // 列顺序：0..14 同 RowToMaterial，第 15 列是 username
  data_sql << "SELECT m.id, m.user_id, m.filename, m.file_type, m.file_path, m.file_size, "
           << "m.status, m.extract_result, m.error_msg, m.review_result, "
           << "UNIX_TIMESTAMP(m.created_at), UNIX_TIMESTAMP(m.updated_at), "
           << "IFNULL(m.fastdfs_file_id,''), IFNULL(m.fastdfs_url,''), IFNULL(m.storage_type,'local'), "
           << "IFNULL(u.username, '') "
           << "FROM materials m LEFT JOIN users u ON m.user_id = u.id"
           << where.str()
           << " ORDER BY m.created_at DESC"
           << " LIMIT " << page_size << " OFFSET " << offset;

  if (mysql_query(conn, data_sql.str().c_str()) != 0) {
    error = "查询失败";
    return result;
  }
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) return result;
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(res)) != nullptr) {
    unsigned long* lengths = mysql_fetch_lengths(res);
    Material m = RowToMaterial(row, lengths);
    // row[15] is username — 编码到 error_msg 字段（仅 error_msg 为空时安全）
    if (row[15] && lengths[15] && m.error_msg.empty()) {
      m.error_msg = std::string("__username__:") + std::string(row[15], lengths[15]);
    }
    result.push_back(m);
  }
  mysql_free_result(res);
  DecorateRagStatus(result);
  return result;
}

MaterialService::AdminMaterialStats MaterialService::AdminGetStats(std::string& error) {
  AdminMaterialStats stats;
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "无法获取数据库连接"; return stats; }

  const std::string sql =
      "SELECT COUNT(*), IFNULL(SUM(file_size),0), "
      "SUM(status='completed'), SUM(status='pending' OR status='extracting'), SUM(status='failed') "
      "FROM materials";
  if (mysql_query(conn, sql.c_str()) != 0) {
    error = "统计查询失败";
    return stats;
  }
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) return stats;
  MYSQL_ROW row = mysql_fetch_row(res);
  if (row) {
    if (row[0]) stats.total       = std::atoi(row[0]);
    if (row[1]) stats.total_size  = std::stoull(row[1]);
    if (row[2]) stats.completed   = std::atoi(row[2]);
    if (row[3]) stats.pending     = std::atoi(row[3]);
    if (row[4]) stats.failed      = std::atoi(row[4]);
  }
  mysql_free_result(res);
  return stats;
}

bool MaterialService::AdminDeleteMaterial(const std::string& material_id,
                                          const std::string& delete_reason,
                                          const std::string& deleted_by,
                                          std::string& error) {
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "无法获取数据库连接"; return false; }

  // Fetch metadata before deletion (user_id, filename, file_type, file_size, file_path, fastdfs_file_id)
  std::ostringstream q;
  q << "SELECT user_id, filename, file_type, file_size, file_path, IFNULL(fastdfs_file_id,'') FROM materials"
    << " WHERE id = '" << material_id << "'";
  if (mysql_query(conn, q.str().c_str()) != 0) {
    error = "查询失败";
    return false;
  }
  std::uint64_t user_id = 0;
  std::string filename, file_type, file_path, fastdfs_file_id;
  std::uint64_t file_size = 0;
  {
    MYSQL_RES* res = mysql_store_result(conn);
    if (res) {
      MYSQL_ROW row = mysql_fetch_row(res);
      if (row) {
        if (row[0]) user_id        = std::stoull(row[0]);
        if (row[1]) filename       = row[1];
        if (row[2]) file_type      = row[2];
        if (row[3]) file_size      = std::stoull(row[3]);
        if (row[4]) file_path      = row[4];
        if (row[5]) fastdfs_file_id = row[5];
      }
      mysql_free_result(res);
    }
  }

  if (user_id == 0) { error = "材料不存在"; return false; }

  // Delete the record
  std::ostringstream del;
  del << "DELETE FROM materials WHERE id = '" << material_id << "'";
  if (mysql_query(conn, del.str().c_str()) != 0) {
    error = "删除失败";
    return false;
  }
  if (mysql_affected_rows(conn) == 0) {
    error = "材料不存在";
    return false;
  }

  // 若已上传 FastDFS，删除远程文件
  if (!fastdfs_file_id.empty() && fastdfs_client_ && fastdfs_client_->IsEnabled()) {
    std::string fdfs_err;
    Logger::Info("AdminDeleteMaterial: deleting FastDFS file " + fastdfs_file_id);
    if (!fastdfs_client_->DeleteFile(fastdfs_file_id, fdfs_err)) {
      Logger::Warn("AdminDeleteMaterial: FastDFS delete failed for " + fastdfs_file_id + ": " + fdfs_err);
    } else {
      Logger::Info("AdminDeleteMaterial: FastDFS delete success for " + fastdfs_file_id);
    }
  } else if (!fastdfs_file_id.empty()) {
    Logger::Warn("AdminDeleteMaterial: FastDFS file_id exists but client not enabled, skipping remote delete: " + fastdfs_file_id);
  }
  // Remove file from disk
  if (!file_path.empty()) {
    std::error_code ec;
    std::filesystem::remove(file_path, ec);
  }

  // Write deletion notice for the user
  {
    // Escape strings
    auto escape = [&](const std::string& s) -> std::string {
      std::string out(s.size() * 2 + 1, '\0');
      const auto len = mysql_real_escape_string(conn, out.data(), s.c_str(), s.length());
      out.resize(len);
      return out;
    };
    const std::string reason_safe = escape(delete_reason.empty() ? "管理员审核删除" : delete_reason);
    const std::string by_safe     = escape(deleted_by.empty() ? "admin" : deleted_by);
    const std::string fname_safe  = escape(filename);
    const std::string ftype_safe  = escape(file_type);

    std::ostringstream ins;
    ins << "INSERT INTO material_deletion_notices"
        << " (user_id, filename, file_type, file_size, delete_reason, deleted_by)"
        << " VALUES (" << user_id
        << ", '" << fname_safe << "'"
        << ", '" << ftype_safe << "'"
        << ", " << file_size
        << ", '" << reason_safe << "'"
        << ", '" << by_safe << "')";
    if (mysql_query(conn, ins.str().c_str()) != 0) {
      Logger::Warn(std::string("AdminDeleteMaterial: 写入删除通知失败: ") + mysql_error(conn));
      // 通知写入失败不影响删除本身
    }
  }

  return true;
}

std::vector<MaterialService::DeletionNotice> MaterialService::GetDeletionNotices(
    std::uint64_t user_id, std::string& error) {
  std::vector<DeletionNotice> result;
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "无法获取数据库连接"; return result; }

  std::ostringstream q;
  q << "SELECT id, filename, file_type, file_size, delete_reason, deleted_by,"
    << " UNIX_TIMESTAMP(created_at)"
    << " FROM material_deletion_notices"
    << " WHERE user_id = " << user_id << " AND is_read = 0"
    << " ORDER BY created_at DESC LIMIT 20";

  if (mysql_query(conn, q.str().c_str()) != 0) {
    error = "查询通知失败";
    return result;
  }
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) return result;
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(res)) != nullptr) {
    DeletionNotice n;
    if (row[0]) n.id            = std::stoull(row[0]);
    if (row[1]) n.filename      = row[1];
    if (row[2]) n.file_type     = row[2];
    if (row[3]) n.file_size     = std::stoull(row[3]);
    if (row[4]) n.delete_reason = row[4];
    if (row[5]) n.deleted_by    = row[5];
    if (row[6]) n.created_at    = std::stoull(row[6]);
    result.push_back(n);
  }
  mysql_free_result(res);
  return result;
}

bool MaterialService::MarkNoticesRead(std::uint64_t user_id,
                                      const std::vector<std::uint64_t>& ids,
                                      std::string& error) {
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "无法获取数据库连接"; return false; }

  std::ostringstream q;
  if (ids.empty()) {
    // Mark all unread for this user
    q << "UPDATE material_deletion_notices SET is_read = 1"
      << " WHERE user_id = " << user_id << " AND is_read = 0";
  } else {
    q << "UPDATE material_deletion_notices SET is_read = 1"
      << " WHERE user_id = " << user_id << " AND id IN (";
    for (std::size_t i = 0; i < ids.size(); ++i) {
      if (i > 0) q << ",";
      q << ids[i];
    }
    q << ")";
  }

  if (mysql_query(conn, q.str().c_str()) != 0) {
    error = "标记已读失败";
    return false;
  }
  return true;
}

bool MaterialService::AdminGetMaterial(const std::string& material_id,
                                       Material& out_material,
                                       std::string& error) {
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "无法获取数据库连接"; return false; }

  std::ostringstream q;
  q << "SELECT id, user_id, filename, file_type, file_path, file_size, status, "
    << "extract_result, error_msg, review_result, "
    << "UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at), "
    << "IFNULL(fastdfs_file_id,''), IFNULL(fastdfs_url,''), IFNULL(storage_type,'local') "
    << "FROM materials WHERE id = '" << material_id << "'";

  if (mysql_query(conn, q.str().c_str()) != 0) {
    error = "查询失败";
    return false;
  }
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) { error = "无结果"; return false; }
  MYSQL_ROW row = mysql_fetch_row(res);
  if (!row) {
    mysql_free_result(res);
    error = "材料不存在";
    return false;
  }
  unsigned long* lengths = mysql_fetch_lengths(res);
  out_material = RowToMaterial(row, lengths);
  mysql_free_result(res);
  DecorateRagStatus(out_material);
  return true;
}

namespace {
// 简单 curl 写回调（复用 qwen_client 同样的模式）
std::size_t MaterialCurlWrite(void* ptr, std::size_t size, std::size_t nmemb, void* userp) {
  const std::size_t total = size * nmemb;
  static_cast<std::string*>(userp)->append(static_cast<char*>(ptr), total);
  return total;
}

// 调用通义千问做内容审核，返回原始 AI 文本
bool CallQwenForReview(const std::string& api_key,
                       const std::string& prompt,
                       std::string& text_out,
                       std::string& error,
                       std::uint32_t timeout_sec) {
  constexpr const char* kEndpoint =
      "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation";

  CURL* curl = curl_easy_init();
  if (!curl) { error = "无法初始化 HTTP 客户端"; return false; }

  nlohmann::json body;
  body["model"] = "qwen-plus";
  body["parameters"]["result_format"] = "message";
  body["input"]["messages"] = nlohmann::json::array({
    {{"role", "user"}, {"content", prompt}}
  });

  const std::string payload = body.dump();
  std::string response_buf;

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  const std::string auth = "Authorization: Bearer " + api_key;
  headers = curl_slist_append(headers, auth.c_str());

  curl_easy_setopt(curl, CURLOPT_URL, kEndpoint);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, MaterialCurlWrite);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buf);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_sec > 0 ? timeout_sec : 60));

  const CURLcode rc = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (rc != CURLE_OK) { error = curl_easy_strerror(rc); return false; }

  try {
    auto resp = nlohmann::json::parse(response_buf);
    // 兼容两种格式：output.text 或 output.choices[0].message.content
    if (resp.contains("output")) {
      auto& output = resp["output"];
      if (output.contains("text") && output["text"].is_string()) {
        text_out = output["text"].get<std::string>();
        return true;
      }
      if (output.contains("choices") && output["choices"].is_array() && !output["choices"].empty()) {
        auto& msg = output["choices"][0];
        if (msg.contains("message") && msg["message"].contains("content")) {
          text_out = msg["message"]["content"].get<std::string>();
          return true;
        }
      }
    }
    error = resp.value("message", "通义千问返回内容为空");
    return false;
  } catch (const std::exception& ex) {
    error = ex.what();
    return false;
  }
}

// 从 AI 回复中解析审核结论（result + reason）
MaterialService::ReviewResult ParseReviewText(const std::string& text) {
  MaterialService::ReviewResult rv;
  rv.reviewed_at = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch()).count());

  // 尝试从返回文本中提取 JSON 对象
  const auto obj_start = text.find('{');
  const auto obj_end   = text.rfind('}');
  if (obj_start != std::string::npos && obj_end != std::string::npos && obj_end > obj_start) {
    try {
      auto j = nlohmann::json::parse(text.substr(obj_start, obj_end - obj_start + 1));
      rv.result = j.value("result", "unknown");
      rv.reason = j.value("reason", "");
      return rv;
    } catch (...) {}
  }

  // 回退：按关键词判断
  const std::string lower_text = [&]() {
    std::string s = text;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
  }();

  if (lower_text.find("违规") != std::string::npos ||
      lower_text.find("violation") != std::string::npos ||
      lower_text.find("不合规") != std::string::npos ||
      lower_text.find("涉及违") != std::string::npos) {
    rv.result = "violation";
  } else if (lower_text.find("合规") != std::string::npos ||
             lower_text.find("pass") != std::string::npos ||
             lower_text.find("无违规") != std::string::npos ||
             lower_text.find("正常") != std::string::npos) {
    rv.result = "pass";
  } else {
    rv.result = "unknown";
  }
  rv.reason = text.size() > 500 ? text.substr(0, 500) + "…" : text;
  return rv;
}
}  // namespace

bool MaterialService::AdminReviewMaterial(const std::string& material_id,
                                          const std::string& api_key,
                                          std::uint32_t timeout_sec,
                                          ReviewResult& out_review,
                                          std::string& error) {
  if (api_key.empty()) {
    error = "未配置通义千问 API Key，无法执行 AI 审核";
    return false;
  }

  Material mat;
  if (!AdminGetMaterial(material_id, mat, error)) {
    return false;
  }

  if (mat.status != "completed" || mat.extract_result.empty()) {
    error = "素材尚未完成文本提取，无法进行 AI 审核";
    return false;
  }

  // 从 extract_result JSON 中提取文本内容
  std::string content_text;
  try {
    auto j = nlohmann::json::parse(mat.extract_result);
    if (j.contains("text") && j["text"].is_string()) {
      content_text = j["text"].get<std::string>();
    } else if (j.contains("content") && j["content"].is_string()) {
      content_text = j["content"].get<std::string>();
    } else if (j.contains("sections") && j["sections"].is_array()) {
      for (const auto& sec : j["sections"]) {
        if (sec.contains("content") && sec["content"].is_string()) {
          content_text += sec["content"].get<std::string>() + "\n";
        }
      }
    } else {
      content_text = mat.extract_result;
    }
  } catch (...) {
    content_text = mat.extract_result;
  }

  // 截断至 3000 字，避免超出模型上下文
  constexpr std::size_t kMaxContentLen = 3000;
  if (content_text.size() > kMaxContentLen) {
    content_text = content_text.substr(0, kMaxContentLen) + "…（内容已截断）";
  }

  const std::string prompt =
      "你是一名内容安全审核专家。请审核以下文档内容，判断是否存在以下任一违规情况：\n"
      "1. 色情、淫秽内容\n"
      "2. 政治敏感、反社会或违法内容\n"
      "3. 侵权、抄袭或虚假信息\n"
      "4. 人身攻击、仇恨言论\n"
      "5. 其他明显违规内容\n\n"
      "文档内容如下：\n---\n" + content_text + "\n---\n\n"
      "请严格按照以下 JSON 格式输出审核结论，不要输出任何其他内容：\n"
      "{\"result\": \"pass 或 violation\", \"reason\": \"简明说明（50字以内）\"}\n"
      "result 字段只能是 pass（合规）或 violation（违规）。";

  std::string ai_response;
  if (!CallQwenForReview(api_key, prompt, ai_response, error, timeout_sec)) {
    return false;
  }

  out_review = ParseReviewText(ai_response);

  // 持久化审核结论到数据库
  nlohmann::json review_json = {
    {"result",      out_review.result},
    {"reason",      out_review.reason},
    {"reviewed_at", out_review.reviewed_at}
  };
  const std::string review_str = review_json.dump();

  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (conn) {
    std::string safe_id;
    for (char c : material_id) {
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') safe_id.push_back(c);
    }
    // Escape review_str via mysql_real_escape_string
    std::string escaped_review(review_str.size() * 2 + 1, '\0');
    const auto elen = mysql_real_escape_string(
        conn, escaped_review.data(), review_str.c_str(), review_str.length());
    escaped_review.resize(elen);

    std::ostringstream upd;
    upd << "UPDATE materials SET review_result = '" << escaped_review
        << "', updated_at = NOW() WHERE id = '" << safe_id << "'";
    if (mysql_query(conn, upd.str().c_str()) != 0) {
      Logger::Warn(std::string("AdminReviewMaterial: 写入审核结论失败: ") + mysql_error(conn));
    }
  }

  return true;
}

bool MaterialService::UpdateFastDfsInfo(const std::string& material_id,
                                        const std::string& fastdfs_file_id,
                                        const std::string& fastdfs_url,
                                        const std::string& storage_type) {
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) return false;

  const std::string sql = R"(
    UPDATE materials
    SET fastdfs_file_id=?, fastdfs_url=?, storage_type=?, updated_at=FROM_UNIXTIME(?)
    WHERE id=?
  )";
  MYSQL_STMT* stmt = mysql_stmt_init(conn);
  if (!stmt) return false;
  if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
    mysql_stmt_close(stmt);
    return false;
  }

  MYSQL_BIND params[5];
  memset(params, 0, sizeof(params));

  params[0].buffer_type   = MYSQL_TYPE_STRING;
  params[0].buffer        = (void*)fastdfs_file_id.c_str();
  params[0].buffer_length = fastdfs_file_id.length();

  params[1].buffer_type   = MYSQL_TYPE_STRING;
  params[1].buffer        = (void*)fastdfs_url.c_str();
  params[1].buffer_length = fastdfs_url.length();

  params[2].buffer_type   = MYSQL_TYPE_STRING;
  params[2].buffer        = (void*)storage_type.c_str();
  params[2].buffer_length = storage_type.length();

  const std::uint64_t now = NowSeconds();
  unsigned long long now_val = static_cast<unsigned long long>(now);
  params[3].buffer_type   = MYSQL_TYPE_LONGLONG;
  params[3].buffer        = &now_val;
  params[3].is_unsigned   = 1;

  params[4].buffer_type   = MYSQL_TYPE_STRING;
  params[4].buffer        = (void*)material_id.c_str();
  params[4].buffer_length = material_id.length();

  const bool ok = (mysql_stmt_bind_param(stmt, params) == 0 &&
                   mysql_stmt_execute(stmt) == 0);
  if (!ok) {
    Logger::Warn(std::string("UpdateFastDfsInfo failed: ") + mysql_stmt_error(stmt));
  }
  mysql_stmt_close(stmt);
  return ok;
}

void MaterialService::UploadToFastDfs(const std::string& material_id, bool delete_local) {
  if (!fastdfs_client_ || !fastdfs_client_->IsEnabled()) return;

  // 查询素材信息
  Material mat;
  {
    auto conn_guard = pool_->GetConnection();
    MYSQL* conn = conn_guard.Get();
    if (!conn) {
      Logger::Warn("UploadToFastDfs: no DB connection for " + material_id);
      return;
    }

    std::ostringstream q;
    q << "SELECT id, user_id, filename, file_type, file_path, file_size, status, "
      << "extract_result, error_msg, review_result, "
      << "UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at), "
      << "IFNULL(fastdfs_file_id,''), IFNULL(fastdfs_url,''), IFNULL(storage_type,'local') "
      << "FROM materials WHERE id = '" << material_id << "'";

    if (mysql_query(conn, q.str().c_str()) != 0) {
      Logger::Warn("UploadToFastDfs: query failed for " + material_id);
      return;
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
      mysql_free_result(res);
      Logger::Warn("UploadToFastDfs: material not found: " + material_id);
      return;
    }
    unsigned long* lengths = mysql_fetch_lengths(res);
    mat = RowToMaterial(row, lengths);
    mysql_free_result(res);
  }

  if (mat.storage_type == "fastdfs" && !mat.fastdfs_file_id.empty()) {
    Logger::Info("UploadToFastDfs: already uploaded for " + material_id);
    return;
  }
  if (mat.file_path.empty() || !std::filesystem::exists(mat.file_path)) {
    Logger::Warn("UploadToFastDfs: local file missing for " + material_id + ": " + mat.file_path);
    return;
  }

  std::string file_id, upload_error;
  if (!fastdfs_client_->UploadFile(mat.file_path, mat.file_type, file_id, upload_error)) {
    Logger::Error("UploadToFastDfs: upload failed for " + material_id + ": " + upload_error);
    return;
  }

  const std::string access_url = fastdfs_client_->BuildAccessUrl(file_id);
  if (!UpdateFastDfsInfo(material_id, file_id, access_url, "fastdfs")) {
    Logger::Error("UploadToFastDfs: DB update failed for " + material_id);
    return;
  }

  Logger::Info("UploadToFastDfs: success for " + material_id + " -> " + file_id);

  if (delete_local && !mat.file_path.empty()) {
    std::error_code ec;
    std::filesystem::remove(mat.file_path, ec);
    if (!ec) {
      Logger::Info("UploadToFastDfs: deleted local file " + mat.file_path);
    }
  }
}
