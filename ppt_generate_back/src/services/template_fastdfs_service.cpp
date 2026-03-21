#include "services/template_fastdfs_service.h"

#include <cstring>
#include <filesystem>
#include <sstream>

#include <mysql/mysql.h>

#include "logger.h"

TemplateFastDfsService::TemplateFastDfsService(
    std::shared_ptr<MySQLConnectionPool> pool,
    std::shared_ptr<FastDfsClient> fastdfs_client)
    : pool_(std::move(pool)),
      fastdfs_client_(std::move(fastdfs_client)) {}

void TemplateFastDfsService::EnsureTable() {
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) {
    Logger::Error("TemplateFastDfsService::EnsureTable: no DB connection");
    return;
  }

  constexpr const char* kDDL = R"(
    CREATE TABLE IF NOT EXISTS template_fastdfs_map (
      template_id        VARCHAR(64)   NOT NULL,
      pptx_file_id       VARCHAR(256)  DEFAULT NULL COMMENT 'FastDFS .pptx 文件 ID',
      pptx_url           VARCHAR(512)  DEFAULT NULL COMMENT 'FastDFS .pptx 访问 URL',
      thumbnail_file_id  VARCHAR(256)  DEFAULT NULL COMMENT 'FastDFS 缩略图文件 ID',
      thumbnail_url      VARCHAR(512)  DEFAULT NULL COMMENT 'FastDFS 缩略图访问 URL',
      analysis_file_id   VARCHAR(256)  DEFAULT NULL COMMENT 'FastDFS 分析 JSON 文件 ID',
      analysis_url       VARCHAR(512)  DEFAULT NULL COMMENT 'FastDFS 分析 JSON 访问 URL',
      uploaded_at        DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
      updated_at         DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
      PRIMARY KEY (template_id),
      INDEX idx_uploaded_at (uploaded_at)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='模板文件 FastDFS 存储映射'
  )";

  if (mysql_query(conn, kDDL) != 0) {
    Logger::Error(std::string("TemplateFastDfsService::EnsureTable failed: ") + mysql_error(conn));
  } else {
    Logger::Info("TemplateFastDfsService: template_fastdfs_map table ready");
  }
}

std::optional<TemplateFastDfsService::TemplateEntry> TemplateFastDfsService::GetEntry(
    const std::string& template_id) const {
  if (template_id.empty()) return std::nullopt;

  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) return std::nullopt;

  // template_id 安全性：在查询前清理（只保留字母数字、-、_、.）
  std::string safe_id;
  for (char c : template_id) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') {
      safe_id.push_back(c);
    }
  }

  std::ostringstream q;
  q << "SELECT template_id, "
    << "IFNULL(pptx_file_id,''), IFNULL(pptx_url,''), "
    << "IFNULL(thumbnail_file_id,''), IFNULL(thumbnail_url,''), "
    << "IFNULL(analysis_file_id,''), IFNULL(analysis_url,'') "
    << "FROM template_fastdfs_map WHERE template_id = '" << safe_id << "'";

  if (mysql_query(conn, q.str().c_str()) != 0) return std::nullopt;
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) return std::nullopt;
  MYSQL_ROW row = mysql_fetch_row(res);
  if (!row) {
    mysql_free_result(res);
    return std::nullopt;
  }
  unsigned long* lengths = mysql_fetch_lengths(res);
  TemplateEntry entry;
  if (row[0] && lengths[0]) entry.template_id        = std::string(row[0], lengths[0]);
  if (row[1] && lengths[1]) entry.pptx_file_id       = std::string(row[1], lengths[1]);
  if (row[2] && lengths[2]) entry.pptx_url           = std::string(row[2], lengths[2]);
  if (row[3] && lengths[3]) entry.thumbnail_file_id  = std::string(row[3], lengths[3]);
  if (row[4] && lengths[4]) entry.thumbnail_url      = std::string(row[4], lengths[4]);
  if (row[5] && lengths[5]) entry.analysis_file_id   = std::string(row[5], lengths[5]);
  if (row[6] && lengths[6]) entry.analysis_url       = std::string(row[6], lengths[6]);
  mysql_free_result(res);
  return entry;
}

bool TemplateFastDfsService::UpsertEntry(const TemplateEntry& entry, std::string& error) {
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "无法获取数据库连接"; return false; }

  const std::string sql = R"(
    INSERT INTO template_fastdfs_map
      (template_id, pptx_file_id, pptx_url, thumbnail_file_id, thumbnail_url,
       analysis_file_id, analysis_url, uploaded_at)
    VALUES (?, ?, ?, ?, ?, ?, ?, NOW())
    ON DUPLICATE KEY UPDATE
      pptx_file_id      = IF(VALUES(pptx_file_id) != '',      VALUES(pptx_file_id),      pptx_file_id),
      pptx_url          = IF(VALUES(pptx_url) != '',          VALUES(pptx_url),          pptx_url),
      thumbnail_file_id = IF(VALUES(thumbnail_file_id) != '', VALUES(thumbnail_file_id), thumbnail_file_id),
      thumbnail_url     = IF(VALUES(thumbnail_url) != '',     VALUES(thumbnail_url),     thumbnail_url),
      analysis_file_id  = IF(VALUES(analysis_file_id) != '',  VALUES(analysis_file_id),  analysis_file_id),
      analysis_url      = IF(VALUES(analysis_url) != '',      VALUES(analysis_url),      analysis_url),
      updated_at        = NOW()
  )";

  MYSQL_STMT* stmt = mysql_stmt_init(conn);
  if (!stmt) { error = "无法初始化 SQL 语句"; return false; }
  if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
    error = std::string("SQL 准备失败: ") + mysql_stmt_error(stmt);
    mysql_stmt_close(stmt);
    return false;
  }

  MYSQL_BIND params[7];
  memset(params, 0, sizeof(params));

  auto bind_str = [](MYSQL_BIND& b, const std::string& s) {
    b.buffer_type   = MYSQL_TYPE_STRING;
    b.buffer        = (void*)s.c_str();
    b.buffer_length = s.length();
  };

  bind_str(params[0], entry.template_id);
  bind_str(params[1], entry.pptx_file_id);
  bind_str(params[2], entry.pptx_url);
  bind_str(params[3], entry.thumbnail_file_id);
  bind_str(params[4], entry.thumbnail_url);
  bind_str(params[5], entry.analysis_file_id);
  bind_str(params[6], entry.analysis_url);

  if (mysql_stmt_bind_param(stmt, params) != 0 ||
      mysql_stmt_execute(stmt) != 0) {
    error = std::string("SQL 执行失败: ") + mysql_stmt_error(stmt);
    mysql_stmt_close(stmt);
    return false;
  }
  mysql_stmt_close(stmt);
  return true;
}

bool TemplateFastDfsService::RemoveEntry(const std::string& template_id,
                                         bool delete_from_fastdfs,
                                         std::string& error) {
  // 先查询要删除 FastDFS 文件的 file_id
  if (delete_from_fastdfs && fastdfs_client_ && fastdfs_client_->IsEnabled()) {
    auto entry = GetEntry(template_id);
    if (entry) {
      auto try_delete = [&](const std::string& fid) {
        if (fid.empty()) return;
        std::string fdfs_err;
        if (!fastdfs_client_->DeleteFile(fid, fdfs_err)) {
          Logger::Warn("TemplateFastDfsService::RemoveEntry: delete FastDFS file failed " +
                       fid + ": " + fdfs_err);
        }
      };
      try_delete(entry->pptx_file_id);
      try_delete(entry->thumbnail_file_id);
      try_delete(entry->analysis_file_id);
    }
  }

  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "无法获取数据库连接"; return false; }

  std::string safe_id;
  for (char c : template_id) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') {
      safe_id.push_back(c);
    }
  }

  const std::string sql = "DELETE FROM template_fastdfs_map WHERE template_id = '" + safe_id + "'";
  if (mysql_query(conn, sql.c_str()) != 0) {
    error = std::string("删除失败: ") + mysql_error(conn);
    return false;
  }
  return true;
}

bool TemplateFastDfsService::UploadTemplate(const std::string& template_id,
                                             const std::string& pptx_path,
                                             const std::string& thumbnail_path,
                                             std::string& error) {
  if (!fastdfs_client_ || !fastdfs_client_->IsEnabled()) {
    error = "FastDFS 未启用";
    return false;
  }

  TemplateEntry entry;
  entry.template_id = template_id;

  if (!pptx_path.empty() && std::filesystem::exists(pptx_path)) {
    std::string file_id, upload_err;
    if (!fastdfs_client_->UploadFile(pptx_path, "pptx", file_id, upload_err)) {
      error = "上传 pptx 失败: " + upload_err;
      return false;
    }
    entry.pptx_file_id = file_id;
    entry.pptx_url     = fastdfs_client_->BuildAccessUrl(file_id);
    Logger::Info("TemplateFastDfsService: uploaded pptx for " + template_id + " -> " + file_id);
  }

  if (!thumbnail_path.empty() && std::filesystem::exists(thumbnail_path)) {
    const std::string ext = std::filesystem::path(thumbnail_path).extension().string();
    const std::string ext_noDot = (ext.size() > 1) ? ext.substr(1) : "png";
    std::string file_id, upload_err;
    if (!fastdfs_client_->UploadFile(thumbnail_path, ext_noDot, file_id, upload_err)) {
      error = "上传缩略图失败: " + upload_err;
      // pptx 已上传，记录已上传部分
    } else {
      entry.thumbnail_file_id = file_id;
      entry.thumbnail_url     = fastdfs_client_->BuildAccessUrl(file_id);
      Logger::Info("TemplateFastDfsService: uploaded thumbnail for " + template_id + " -> " + file_id);
    }
  }

  if (entry.pptx_file_id.empty() && entry.thumbnail_file_id.empty()) {
    error = "没有任何文件成功上传到 FastDFS";
    return false;
  }

  return UpsertEntry(entry, error);
}
