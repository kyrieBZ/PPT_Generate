#include "controllers/admin_controller.h"

#include <sstream>

#include <nlohmann/json.hpp>

#include "http/http_types.h"
#include "logger.h"
#include "utils/string_utils.h"

namespace {
std::string ExtractToken(const HttpRequest& request) {
  auto header = request.Header("authorization");
  if (header.rfind("Bearer ", 0) == 0 || header.rfind("bearer ", 0) == 0) {
    return header.substr(7);
  }
  if (!header.empty()) {
    return header;
  }
  if (auto it = request.query_params.find("token"); it != request.query_params.end()) {
    return it->second;
  }
  return {};
}

nlohmann::json UserToJson(const User& user) {
  nlohmann::json payload = {
      {"id", user.id},
      {"username", user.username},
      {"email", user.email},
      {"isAdmin", user.is_admin},
      {"isDisabled", user.is_disabled},
      {"createdAt", user.created_at},
      {"updatedAt", user.updated_at}};
  if (user.last_login) {
    payload["lastLogin"] = *user.last_login;
  }
  return payload;
}
}  // namespace

AdminController::AdminController(std::shared_ptr<AuthService>  auth_service,
                                 std::shared_ptr<AuditService> audit_service)
    : auth_service_(std::move(auth_service)),
      audit_service_(std::move(audit_service)) {}

std::string AdminController::ExtractIp(const HttpRequest& request) {
  auto xff = request.Header("x-forwarded-for");
  if (!xff.empty()) {
    // 可能是逗号分隔列表，取第一个
    auto pos = xff.find(',');
    return string_utils::Trim(pos != std::string::npos ? xff.substr(0, pos) : xff);
  }
  auto real = request.Header("x-real-ip");
  if (!real.empty()) return string_utils::Trim(real);
  return "unknown";
}

std::shared_ptr<User> AdminController::AuthenticateAdmin(const HttpRequest& request, std::string& error) const {
  const auto token = ExtractToken(request);
  if (token.empty()) {
    error = "Token not provided";
    return nullptr;
  }
  auto user = auth_service_->GetUserFromToken(token, error);
  if (!user) {
    error = error.empty() ? "Invalid token" : error;
    return nullptr;
  }
  if (!user->is_admin) {
    error = "Forbidden";
    return nullptr;
  }
  return std::make_shared<User>(*user);
}

HttpResponse AdminController::ListUsers(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    if (error == "Forbidden") {
      return HttpResponse::Json(403, ErrorJson("ERR_FORBIDDEN", error));
    }
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  std::string query;
  if (auto it = request.query_params.find("q"); it != request.query_params.end()) {
    query = string_utils::Trim(it->second);
  }

  auto users = auth_service_->ListUsers(query, error);
  if (!error.empty()) {
    Logger::Error(std::string("ListUsers failed: ") + error);
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
  }

  nlohmann::json payload;
  payload["items"] = nlohmann::json::array();
  for (const auto& user : users) {
    payload["items"].push_back(UserToJson(user));
  }
  return HttpResponse::Json(200, payload);
}

HttpResponse AdminController::UpdateUserStatus(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    if (error == "Forbidden") {
      return HttpResponse::Json(403, ErrorJson("ERR_FORBIDDEN", error));
    }
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  try {
    auto body = nlohmann::json::parse(request.body);
    std::uint64_t user_id = 0;
    if (body.contains("userId")) {
      user_id = body["userId"].get<std::uint64_t>();
    } else if (body.contains("id")) {
      user_id = body["id"].get<std::uint64_t>();
    }
    bool disabled = false;
    if (body.contains("disabled")) {
      disabled = body["disabled"].get<bool>();
    } else if (body.contains("isDisabled")) {
      disabled = body["isDisabled"].get<bool>();
    }

    if (user_id == 0) {
      return HttpResponse::Json(400, ErrorJson("ERR_ADMIN_INVALID_USER_ID", "Invalid userId"));
    }
    if (admin->id == user_id) {
      return HttpResponse::Json(400, ErrorJson("ERR_ADMIN_CANNOT_CHANGE_SELF", "不能修改自身状态"));
    }

    if (!auth_service_->UpdateUserStatus(user_id, disabled, error)) {
      return HttpResponse::Json(400, ErrorJson("ERR_ADMIN_UPDATE_STATUS_FAILED", error.empty() ? "更新用户状态失败" : error));
    }

    if (audit_service_) {
      std::string detail = std::string("{\"disabled\":") + (disabled ? "true" : "false") + "}";
      audit_service_->Write(admin->id, admin->username,
                            disabled ? "disable_user" : "enable_user",
                            "user", std::to_string(user_id),
                            detail, ExtractIp(request));
    }

    return HttpResponse::Json(200, {{"userId", user_id}, {"disabled", disabled}});
  } catch (const std::exception& ex) {
    Logger::Error(std::string("Failed to parse admin user status request: ") + ex.what());
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
  }
}

HttpResponse AdminController::BatchUpdateUserStatus(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    if (error == "Forbidden") {
      return HttpResponse::Json(403, ErrorJson("ERR_FORBIDDEN", error));
    }
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  try {
    auto body = nlohmann::json::parse(request.body);

    if (!body.contains("ids") || !body["ids"].is_array()) {
      return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "缺少 ids 数组"));
    }
    bool disabled = false;
    if (body.contains("disabled") && body["disabled"].is_boolean()) {
      disabled = body["disabled"].get<bool>();
    }

    int success_count = 0;
    int skip_count = 0;

    for (const auto& id_val : body["ids"]) {
      std::uint64_t uid = 0;
      if (id_val.is_number_unsigned()) {
        uid = id_val.get<std::uint64_t>();
      } else if (id_val.is_number()) {
        uid = static_cast<std::uint64_t>(id_val.get<std::int64_t>());
      }
      if (uid == 0 || uid == admin->id) {
        ++skip_count;
        continue;
      }
      std::string op_error;
      if (auth_service_->UpdateUserStatus(uid, disabled, op_error)) {
        ++success_count;
      } else {
        ++skip_count;
        Logger::Warn("BatchUpdateUserStatus: uid=" + std::to_string(uid) + " err=" + op_error);
      }
    }

    Logger::Info("BatchUpdateUserStatus by " + admin->username +
                 ": success=" + std::to_string(success_count) +
                 " skip=" + std::to_string(skip_count) +
                 " disabled=" + (disabled ? "true" : "false"));

    if (audit_service_ && success_count > 0) {
      std::string detail = std::string("{\"disabled\":") + (disabled ? "true" : "false") +
                           ",\"success\":" + std::to_string(success_count) + "}";
      audit_service_->Write(admin->id, admin->username,
                            disabled ? "batch_disable_user" : "batch_enable_user",
                            "user", "batch",
                            detail, ExtractIp(request));
    }

    return HttpResponse::Json(200, {
      {"success", success_count},
      {"skipped", skip_count},
      {"disabled", disabled}
    });
  } catch (const std::exception& ex) {
    Logger::Error(std::string("BatchUpdateUserStatus parse error: ") + ex.what());
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
  }
}

HttpResponse AdminController::ExportUsers(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  std::string query;
  if (auto it = request.query_params.find("q"); it != request.query_params.end()) {
    query = string_utils::Trim(it->second);
  }

  auto users = auth_service_->ListUsers(query, error);
  if (!error.empty()) {
    Logger::Error(std::string("ExportUsers ListUsers failed: ") + error);
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
  }

  auto csvField = [](const std::string& s) -> std::string {
    bool need = s.find(',') != std::string::npos ||
                s.find('"') != std::string::npos ||
                s.find('\n') != std::string::npos;
    if (!need) return s;
    std::string out = "\"";
    for (char c : s) { if (c == '"') out += "\"\""; else out += c; }
    return out + '"';
  };

  std::ostringstream csv;
  csv << "ID,用户名,邮箱,是否管理员,是否禁用,注册时间,上次登录\n";
  for (const auto& u : users) {
    csv << u.id                                        << ","
        << csvField(u.username)                        << ","
        << csvField(u.email)                           << ","
        << (u.is_admin    ? "是" : "否")               << ","
        << (u.is_disabled ? "是" : "否")               << ","
        << csvField(u.created_at)                      << ","
        << csvField(u.last_login.value_or(""))         << "\n";
  }

  HttpResponse resp;
  resp.status_code    = 200;
  resp.status_message = "OK";
  resp.headers["content-type"]        = "text/csv; charset=utf-8";
  resp.headers["content-disposition"] = "attachment; filename=\"users.csv\"";
  resp.body = csv.str();
  return resp;
}
