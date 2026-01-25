#include "controllers/admin_controller.h"

#include <nlohmann/json.hpp>

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

AdminController::AdminController(std::shared_ptr<AuthService> auth_service)
    : auth_service_(std::move(auth_service)) {}

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
    return HttpResponse::Json(error == "Forbidden" ? 403 : 401, {{"message", error}});
  }

  std::string query;
  if (auto it = request.query_params.find("q"); it != request.query_params.end()) {
    query = string_utils::Trim(it->second);
  }

  auto users = auth_service_->ListUsers(query, error);
  if (!error.empty()) {
    return HttpResponse::Json(500, {{"message", error}});
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
    return HttpResponse::Json(error == "Forbidden" ? 403 : 401, {{"message", error}});
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
      return HttpResponse::Json(400, {{"message", "Invalid userId"}});
    }
    if (admin->id == user_id) {
      return HttpResponse::Json(400, {{"message", "不能修改自身状态"}});
    }

    if (!auth_service_->UpdateUserStatus(user_id, disabled, error)) {
      return HttpResponse::Json(400, {{"message", error.empty() ? "更新用户状态失败" : error}});
    }

    return HttpResponse::Json(200, {{"userId", user_id}, {"disabled", disabled}});
  } catch (const std::exception& ex) {
    Logger::Error(std::string("Failed to parse admin user status request: ") + ex.what());
    return HttpResponse::Json(400, {{"message", "Invalid JSON"}});
  }
}
