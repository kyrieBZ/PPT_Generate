#include "controllers/audit_controller.h"

#include <sstream>

#include <nlohmann/json.hpp>

#include "logger.h"
#include "utils/string_utils.h"

namespace {
std::string ExtractToken(const HttpRequest& request) {
  auto header = request.Header("authorization");
  if (header.rfind("Bearer ", 0) == 0 || header.rfind("bearer ", 0) == 0) {
    return header.substr(7);
  }
  if (!header.empty()) return header;
  if (auto it = request.query_params.find("token"); it != request.query_params.end()) {
    return it->second;
  }
  return {};
}

std::string QsGet(const HttpRequest& request, const std::string& key) {
  if (auto it = request.query_params.find(key); it != request.query_params.end()) {
    return string_utils::Trim(it->second);
  }
  return {};
}

int QsInt(const HttpRequest& request, const std::string& key, int def) {
  auto s = QsGet(request, key);
  if (s.empty()) return def;
  try { return std::stoi(s); } catch (...) { return def; }
}

nlohmann::json LogToJson(const AuditLog& log) {
  return {
    {"id",          log.id},
    {"operatorId",  log.operator_id},
    {"operator",    log.operator_name},
    {"action",      log.action},
    {"targetType",  log.target_type},
    {"targetId",    log.target_id},
    {"detail",      log.detail},
    {"ip",          log.ip},
    {"createdAt",   log.created_at}
  };
}

// 将一行审计日志转成 CSV 行（字段含逗号/引号时加双引号包裹）
std::string ToCsvField(const std::string& s) {
  bool need_quote = s.find(',') != std::string::npos ||
                    s.find('"') != std::string::npos ||
                    s.find('\n') != std::string::npos;
  if (!need_quote) return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '"') out += "\"\"";
    else out += c;
  }
  out += '"';
  return out;
}
}  // namespace

AuditController::AuditController(std::shared_ptr<AuthService>  auth_service,
                                 std::shared_ptr<AuditService> audit_service)
    : auth_service_(std::move(auth_service)),
      audit_service_(std::move(audit_service)) {}

std::shared_ptr<User> AuditController::AuthenticateAdmin(
    const HttpRequest& request, std::string& error) const {
  const auto token = ExtractToken(request);
  if (token.empty()) { error = "Token not provided"; return nullptr; }
  auto user = auth_service_->GetUserFromToken(token, error);
  if (!user) { error = error.empty() ? "Invalid token" : error; return nullptr; }
  if (!user->is_admin) { error = "Forbidden"; return nullptr; }
  return std::make_shared<User>(*user);
}

HttpResponse AuditController::AdminList(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  AuditFilter filter;
  filter.action     = QsGet(request, "action");
  filter.start_date = QsGet(request, "start");
  filter.end_date   = QsGet(request, "end");
  filter.keyword    = QsGet(request, "q");
  filter.page       = QsInt(request, "page",      1);
  filter.page_size  = QsInt(request, "page_size", 30);

  std::vector<AuditLog> logs;
  int total = 0;
  if (!audit_service_->List(filter, logs, total, error)) {
    Logger::Error("AuditController::AdminList failed: " + error);
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "查询审计日志失败"));
  }

  nlohmann::json items = nlohmann::json::array();
  for (const auto& log : logs) {
    items.push_back(LogToJson(log));
  }
  return HttpResponse::Json(200, {
    {"items", items},
    {"total", total},
    {"page",  filter.page},
    {"pageSize", filter.page_size}
  });
}

HttpResponse AuditController::AdminExport(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  AuditFilter filter;
  filter.action     = QsGet(request, "action");
  filter.start_date = QsGet(request, "start");
  filter.end_date   = QsGet(request, "end");
  filter.keyword    = QsGet(request, "q");

  std::vector<AuditLog> logs;
  if (!audit_service_->Export(filter, logs, error)) {
    Logger::Error("AuditController::AdminExport failed: " + error);
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "导出审计日志失败"));
  }

  // 构建 CSV
  std::ostringstream csv;
  csv << "ID,操作人ID,操作人,操作类型,对象类型,对象ID,详情,IP,时间\n";
  for (const auto& log : logs) {
    csv << ToCsvField(std::to_string(log.id))          << ","
        << ToCsvField(std::to_string(log.operator_id)) << ","
        << ToCsvField(log.operator_name)               << ","
        << ToCsvField(log.action)                      << ","
        << ToCsvField(log.target_type)                 << ","
        << ToCsvField(log.target_id)                   << ","
        << ToCsvField(log.detail)                      << ","
        << ToCsvField(log.ip)                          << ","
        << ToCsvField(log.created_at)                  << "\n";
  }

  HttpResponse resp;
  resp.status_code    = 200;
  resp.status_message = "OK";
  resp.headers["content-type"]        = "text/csv; charset=utf-8";
  resp.headers["content-disposition"] = "attachment; filename=\"audit_logs.csv\"";
  resp.body = csv.str();
  return resp;
}
