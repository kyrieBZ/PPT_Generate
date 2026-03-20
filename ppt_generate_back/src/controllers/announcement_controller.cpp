#include "controllers/announcement_controller.h"

#include <cstring>
#include <sstream>

#include <mysql/mysql.h>
#include <nlohmann/json.hpp>

#include "http/http_types.h"
#include "logger.h"
#include "models/announcement.h"

namespace {

/** 将 MySQL 行映射为 Announcement 结构体 */
Announcement RowToAnnouncement(MYSQL_ROW row, unsigned long* /*lengths*/) {
  Announcement a;
  if (row[0]) a.id         = std::stoull(row[0]);
  if (row[1]) a.title      = row[1];
  if (row[2]) a.content    = row[2];
  if (row[3]) a.is_pinned  = std::stoi(row[3]) != 0;
  if (row[4]) a.starts_at  = std::stoull(row[4]);
  if (row[5]) a.expires_at = std::stoull(row[5]);
  if (row[6]) a.created_by = std::stoull(row[6]);
  if (row[7]) a.created_at = std::stoull(row[7]);
  if (row[8]) a.updated_at = std::stoull(row[8]);
  return a;
}

nlohmann::json AnnouncementToJson(const Announcement& a) {
  nlohmann::json j;
  j["id"]         = a.id;
  j["title"]      = a.title;
  j["content"]    = a.content;
  j["is_pinned"]  = a.is_pinned;
  j["starts_at"]  = a.starts_at;
  j["expires_at"] = a.expires_at;
  j["created_by"] = a.created_by;
  j["created_at"] = a.created_at;
  j["updated_at"] = a.updated_at;
  return j;
}

/** 从 query string 取单一参数 */
std::string QsGet(const HttpRequest& req, const std::string& key) {
  auto it = req.query_params.find(key);
  return it != req.query_params.end() ? it->second : std::string{};
}

/** 对单引号做最小转义，防止 SQL 注入（列值均用此函数处理） */
std::string EscapeStr(MYSQL* conn, const std::string& s) {
  std::string out(s.size() * 2 + 1, '\0');
  unsigned long len = mysql_real_escape_string(conn, out.data(), s.c_str(),
                                               static_cast<unsigned long>(s.size()));
  out.resize(len);
  return out;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────

AnnouncementController::AnnouncementController(
    std::shared_ptr<AuthService>         auth_service,
    std::shared_ptr<MySQLConnectionPool> pool,
    std::shared_ptr<AuditService>        audit_service)
    : auth_service_(std::move(auth_service)),
      pool_(std::move(pool)),
      audit_service_(std::move(audit_service)) {}

// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<User> AnnouncementController::AuthenticateAdmin(
    const HttpRequest& request, HttpResponse& error_response) const {
  std::string token = request.Header("authorization");
  if (token.size() > 7 && token.substr(0, 7) == "Bearer ") token = token.substr(7);

  // 也支持 query 参数 token=xxx
  if (token.empty()) {
    auto it = request.query_params.find("token");
    if (it != request.query_params.end()) token = it->second;
  }

  std::string auth_error;
  auto opt_user = auth_service_->GetUserFromToken(token, auth_error);
  if (!opt_user) {
    error_response = HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "未登录或 token 无效"));
    return nullptr;
  }
  if (!opt_user->is_admin) {
    error_response = HttpResponse::Json(403, ErrorJson("FORBIDDEN", "需要管理员权限"));
    return nullptr;
  }
  return std::make_shared<User>(*opt_user);
}

// ─────────────────────────────────────────────────────────────────────────────

/** GET /api/announcements — 公开（需登录），返回当前有效公告 */
HttpResponse AnnouncementController::ListActive(const HttpRequest& request) {
  // 只需验证已登录
  std::string token = request.Header("authorization");
  if (token.size() > 7 && token.substr(0, 7) == "Bearer ") token = token.substr(7);
  if (token.empty()) {
    auto it = request.query_params.find("token");
    if (it != request.query_params.end()) token = it->second;
  }
  std::string auth_error;
  auto opt_user = auth_service_->GetUserFromToken(token, auth_error);
  if (!opt_user) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "未登录"));
  }

  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) {
    return HttpResponse::Json(500, ErrorJson("DB_ERROR", "数据库连接失败"));
  }

  const char* sql =
      "SELECT id, title, content, is_pinned, "
      "UNIX_TIMESTAMP(starts_at), "
      "IF(expires_at IS NULL, 0, UNIX_TIMESTAMP(expires_at)), "
      "created_by, UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at) "
      "FROM announcements "
      "WHERE starts_at <= NOW() AND (expires_at IS NULL OR expires_at > NOW()) "
      "ORDER BY is_pinned DESC, created_at DESC "
      "LIMIT 20";

  if (mysql_query(conn, sql) != 0) {
    return HttpResponse::Json(500, ErrorJson("DB_ERROR", mysql_error(conn)));
  }
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) {
    return HttpResponse::Json(500, ErrorJson("DB_ERROR", "无结果集"));
  }

  nlohmann::json items = nlohmann::json::array();
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(res))) {
    unsigned long* lens = mysql_fetch_lengths(res);
    items.push_back(AnnouncementToJson(RowToAnnouncement(row, lens)));
  }
  mysql_free_result(res);

  return HttpResponse::Json(200, {{"items", items}});
}

// ─────────────────────────────────────────────────────────────────────────────

/** GET /api/admin/announcements — 管理员全量列表（含过期） */
HttpResponse AnnouncementController::AdminList(const HttpRequest& request) {
  HttpResponse err;
  auto admin = AuthenticateAdmin(request, err);
  if (!admin) return err;

  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) {
    return HttpResponse::Json(500, ErrorJson("DB_ERROR", "数据库连接失败"));
  }

  // 简单分页
  int page = 1, page_size = 20;
  {
    auto it = request.query_params.find("page");
    if (it != request.query_params.end() && !it->second.empty()) {
      try { page = std::stoi(it->second); } catch (...) {}
    }
    it = request.query_params.find("page_size");
    if (it != request.query_params.end() && !it->second.empty()) {
      try { page_size = std::stoi(it->second); } catch (...) {}
    }
  }
  if (page < 1) page = 1;
  if (page_size < 1 || page_size > 100) page_size = 20;
  int offset = (page - 1) * page_size;

  // 总数
  std::uint64_t total = 0;
  {
    const char* cnt_sql = "SELECT COUNT(*) FROM announcements";
    if (mysql_query(conn, cnt_sql) == 0) {
      MYSQL_RES* r = mysql_store_result(conn);
      if (r) {
        MYSQL_ROW row = mysql_fetch_row(r);
        if (row && row[0]) total = std::stoull(row[0]);
        mysql_free_result(r);
      }
    }
  }

  std::ostringstream q;
  q << "SELECT id, title, content, is_pinned, "
    << "UNIX_TIMESTAMP(starts_at), "
    << "IF(expires_at IS NULL, 0, UNIX_TIMESTAMP(expires_at)), "
    << "created_by, UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at) "
    << "FROM announcements "
    << "ORDER BY is_pinned DESC, created_at DESC "
    << "LIMIT " << page_size << " OFFSET " << offset;

  if (mysql_query(conn, q.str().c_str()) != 0) {
    return HttpResponse::Json(500, ErrorJson("DB_ERROR", mysql_error(conn)));
  }
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) {
    return HttpResponse::Json(500, ErrorJson("DB_ERROR", "无结果集"));
  }

  nlohmann::json items = nlohmann::json::array();
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(res))) {
    unsigned long* lens = mysql_fetch_lengths(res);
    items.push_back(AnnouncementToJson(RowToAnnouncement(row, lens)));
  }
  mysql_free_result(res);

  return HttpResponse::Json(200, {{"items", items}, {"total", total}, {"page", page}, {"page_size", page_size}});
}

// ─────────────────────────────────────────────────────────────────────────────

/** POST /api/admin/announcements — 创建公告 */
HttpResponse AnnouncementController::AdminCreate(const HttpRequest& request) {
  HttpResponse err;
  auto admin = AuthenticateAdmin(request, err);
  if (!admin) return err;

  nlohmann::json body;
  try {
    body = nlohmann::json::parse(request.body);
  } catch (...) {
    return HttpResponse::Json(400, ErrorJson("BAD_REQUEST", "请求体必须是 JSON"));
  }

  std::string title   = body.value("title", "");
  std::string content = body.value("content", "");
  bool is_pinned      = body.value("is_pinned", false);
  std::string starts_at_str = body.value("starts_at", "");  // ISO 8601 or empty
  std::string expires_at_str = body.value("expires_at", ""); // ISO 8601 or empty

  if (title.empty()) {
    return HttpResponse::Json(400, ErrorJson("VALIDATION_ERROR", "标题不能为空"));
  }
  if (content.empty()) {
    return HttpResponse::Json(400, ErrorJson("VALIDATION_ERROR", "内容不能为空"));
  }

  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) {
    return HttpResponse::Json(500, ErrorJson("DB_ERROR", "数据库连接失败"));
  }

  std::string esc_title   = EscapeStr(conn, title);
  std::string esc_content = EscapeStr(conn, content);

  std::ostringstream q;
  q << "INSERT INTO announcements (title, content, is_pinned, starts_at, expires_at, created_by) VALUES ("
    << "'" << esc_title << "', "
    << "'" << esc_content << "', "
    << (is_pinned ? 1 : 0) << ", ";

  if (starts_at_str.empty()) {
    q << "NOW(), ";
  } else {
    std::string esc_starts = EscapeStr(conn, starts_at_str);
    q << "'" << esc_starts << "', ";
  }

  if (expires_at_str.empty()) {
    q << "NULL, ";
  } else {
    std::string esc_exp = EscapeStr(conn, expires_at_str);
    q << "'" << esc_exp << "', ";
  }

  q << admin->id << ")";

  if (mysql_query(conn, q.str().c_str()) != 0) {
    return HttpResponse::Json(500, ErrorJson("DB_ERROR", mysql_error(conn)));
  }

  std::uint64_t new_id = static_cast<std::uint64_t>(mysql_insert_id(conn));
  Logger::Info("Announcement created: id=" + std::to_string(new_id) + " by=" + admin->username);

  if (audit_service_) {
    audit_service_->Write(admin->id, admin->username,
                          "create_announcement", "announcement",
                          std::to_string(new_id),
                          "{\"title\":\"" + title + "\"}",
                          request.Header("x-forwarded-for").empty()
                              ? request.Header("x-real-ip")
                              : request.Header("x-forwarded-for"));
  }

  return HttpResponse::Json(200, {{"id", new_id}, {"message", "公告创建成功"}});
}

// ─────────────────────────────────────────────────────────────────────────────

/** PUT /api/admin/announcements?id=xxx — 更新公告 */
HttpResponse AnnouncementController::AdminUpdate(const HttpRequest& request) {
  HttpResponse err;
  auto admin = AuthenticateAdmin(request, err);
  if (!admin) return err;

  std::string id_str = QsGet(request, "id");
  if (id_str.empty()) {
    return HttpResponse::Json(400, ErrorJson("BAD_REQUEST", "缺少参数 id"));
  }
  std::uint64_t ann_id = 0;
  try { ann_id = std::stoull(id_str); } catch (...) {
    return HttpResponse::Json(400, ErrorJson("BAD_REQUEST", "id 格式错误"));
  }

  nlohmann::json body;
  try {
    body = nlohmann::json::parse(request.body);
  } catch (...) {
    return HttpResponse::Json(400, ErrorJson("BAD_REQUEST", "请求体必须是 JSON"));
  }

  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) {
    return HttpResponse::Json(500, ErrorJson("DB_ERROR", "数据库连接失败"));
  }

  std::ostringstream sets;
  bool has_set = false;
  auto appendSet = [&](const std::string& expr) {
    if (has_set) sets << ", ";
    sets << expr;
    has_set = true;
  };

  if (body.contains("title") && body["title"].is_string()) {
    appendSet("title = '" + EscapeStr(conn, body["title"].get<std::string>()) + "'");
  }
  if (body.contains("content") && body["content"].is_string()) {
    appendSet("content = '" + EscapeStr(conn, body["content"].get<std::string>()) + "'");
  }
  if (body.contains("is_pinned") && body["is_pinned"].is_boolean()) {
    appendSet(std::string("is_pinned = ") + (body["is_pinned"].get<bool>() ? "1" : "0"));
  }
  if (body.contains("starts_at")) {
    std::string v = body["starts_at"].is_string() ? body["starts_at"].get<std::string>() : "";
    if (v.empty()) {
      appendSet("starts_at = NOW()");
    } else {
      appendSet("starts_at = '" + EscapeStr(conn, v) + "'");
    }
  }
  if (body.contains("expires_at")) {
    std::string v = body["expires_at"].is_string() ? body["expires_at"].get<std::string>() : "";
    if (v.empty()) {
      appendSet("expires_at = NULL");
    } else {
      appendSet("expires_at = '" + EscapeStr(conn, v) + "'");
    }
  }

  if (!has_set) {
    return HttpResponse::Json(400, ErrorJson("BAD_REQUEST", "没有可更新的字段"));
  }

  std::ostringstream q;
  q << "UPDATE announcements SET " << sets.str() << " WHERE id = " << ann_id;

  if (mysql_query(conn, q.str().c_str()) != 0) {
    return HttpResponse::Json(500, ErrorJson("DB_ERROR", mysql_error(conn)));
  }
  if (mysql_affected_rows(conn) == 0) {
    return HttpResponse::Json(404, ErrorJson("NOT_FOUND", "公告不存在"));
  }

  Logger::Info("Announcement updated: id=" + std::to_string(ann_id) + " by=" + admin->username);

  if (audit_service_) {
    audit_service_->Write(admin->id, admin->username,
                          "update_announcement", "announcement",
                          std::to_string(ann_id), "{}",
                          request.Header("x-forwarded-for").empty()
                              ? request.Header("x-real-ip")
                              : request.Header("x-forwarded-for"));
  }

  return HttpResponse::Json(200, {{"message", "公告已更新"}});
}

// ─────────────────────────────────────────────────────────────────────────────

/** DELETE /api/admin/announcements?id=xxx — 删除公告 */
HttpResponse AnnouncementController::AdminDelete(const HttpRequest& request) {
  HttpResponse err;
  auto admin = AuthenticateAdmin(request, err);
  if (!admin) return err;

  std::string id_str = QsGet(request, "id");
  if (id_str.empty()) {
    return HttpResponse::Json(400, ErrorJson("BAD_REQUEST", "缺少参数 id"));
  }
  std::uint64_t ann_id = 0;
  try { ann_id = std::stoull(id_str); } catch (...) {
    return HttpResponse::Json(400, ErrorJson("BAD_REQUEST", "id 格式错误"));
  }

  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) {
    return HttpResponse::Json(500, ErrorJson("DB_ERROR", "数据库连接失败"));
  }

  std::ostringstream q;
  q << "DELETE FROM announcements WHERE id = " << ann_id;
  if (mysql_query(conn, q.str().c_str()) != 0) {
    return HttpResponse::Json(500, ErrorJson("DB_ERROR", mysql_error(conn)));
  }
  if (mysql_affected_rows(conn) == 0) {
    return HttpResponse::Json(404, ErrorJson("NOT_FOUND", "公告不存在"));
  }

  Logger::Info("Announcement deleted: id=" + std::to_string(ann_id) + " by=" + admin->username);

  if (audit_service_) {
    audit_service_->Write(admin->id, admin->username,
                          "delete_announcement", "announcement",
                          std::to_string(ann_id), "{}",
                          request.Header("x-forwarded-for").empty()
                              ? request.Header("x-real-ip")
                              : request.Header("x-forwarded-for"));
  }

  return HttpResponse::Json(200, {{"message", "公告已删除"}});
}
