#include "controllers/auth_controller.h"

#include "http/http_types.h"
#include "logger.h"
#include "utils/settings_reader.h"

namespace {
nlohmann::json UserJson(const User& user) {
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

}

AuthController::AuthController(std::shared_ptr<AuthService>         service,
                               std::shared_ptr<MySQLConnectionPool> pool)
    : service_(std::move(service)), pool_(std::move(pool)) {}

HttpResponse AuthController::Register(const HttpRequest& request) {
  // 动态检查 registration_enabled 配置（热更新，无需重启）
  if (pool_) {
    const bool reg_enabled = SettingsReader::GetBool(*pool_, "registration_enabled", true);
    if (!reg_enabled) {
      Logger::Info("Registration attempt rejected: registration_enabled=false");
      return HttpResponse::Json(403, ErrorJson("ERR_REGISTRATION_DISABLED", "当前系统已关闭新用户注册，请联系管理员"));
    }
  }

  try {
    auto body = nlohmann::json::parse(request.body);
    if (!body.contains("username") || !body.contains("email") || !body.contains("password")) {
      return HttpResponse::Json(400, ErrorJson("ERR_AUTH_REGISTER_MISSING_FIELDS", "Missing required fields"));
    }

    User user;
    std::string token;
    std::string error;
    if (!service_->RegisterUser(body["username"], body["email"], body["password"], user, token, error)) {
      return HttpResponse::Json(400, ErrorJson("ERR_AUTH_REGISTER_FAILED", error.empty() ? "Registration failed" : error));
    }

    nlohmann::json payload{{"token", token}, {"user", UserJson(user)}};
    HttpResponse response = HttpResponse::Json(201, payload);
    response.status_message = "Created";
    return response;
  } catch (const std::exception& ex) {
    Logger::Error(std::string("Failed to parse registration request: ") + ex.what());
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
  }
}

HttpResponse AuthController::Login(const HttpRequest& request) {
  try {
    auto body = nlohmann::json::parse(request.body);
    if (!body.contains("username") || !body.contains("password")) {
      return HttpResponse::Json(400, ErrorJson("ERR_AUTH_LOGIN_MISSING_FIELDS", "Username or password missing"));
    }

    User user;
    std::string token;
    std::string error;
    if (!service_->Login(body["username"], body["password"], user, token, error)) {
      return HttpResponse::Json(401, ErrorJson("ERR_AUTH_LOGIN_FAILED", error.empty() ? "Login failed" : error));
    }

    // 普通用户在维护模式下禁止登录；管理员始终可以登录以便解除维护
    if (!user.is_admin && pool_) {
      const bool in_maintenance = SettingsReader::GetBool(*pool_, "maintenance_mode", false);
      if (in_maintenance) {
        return HttpResponse::Json(503, ErrorJson("ERR_MAINTENANCE",
            "系统正在维护中，请稍后再试。如有疑问请联系管理员。"));
      }
    }

    return HttpResponse::Json(200, {{"token", token}, {"user", UserJson(user)}});
  } catch (const std::exception& ex) {
    Logger::Error(std::string("Failed to parse login request: ") + ex.what());
    return HttpResponse::Json(400, ErrorJson("ERR_AUTH_LOGIN_INVALID_JSON", "Invalid JSON"));
  }
}

HttpResponse AuthController::Logout(const HttpRequest& request) {
  const auto token = ExtractToken(request);
  if (token.empty()) {
    return HttpResponse::Json(401, ErrorJson("ERR_AUTH_TOKEN_MISSING", "Token not provided"));
  }

  std::string error;
  if (!service_->Logout(token, error)) {
    return HttpResponse::Json(400, ErrorJson("ERR_AUTH_LOGOUT_FAILED", error.empty() ? "Logout failed" : error));
  }

  return HttpResponse::Json(200, {{"message", "Logged out successfully"}});
}

HttpResponse AuthController::CurrentUser(const HttpRequest& request) {
  const auto token = ExtractToken(request);
  if (token.empty()) {
    return HttpResponse::Json(401, ErrorJson("ERR_AUTH_TOKEN_MISSING", "Token not provided"));
  }

  std::string error;
  auto user = service_->GetUserFromToken(token, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_AUTH_TOKEN_INVALID", error.empty() ? "Invalid token" : error));
  }

  return HttpResponse::Json(200, {{"user", UserJson(*user)}});
}

HttpResponse AuthController::ChangePassword(const HttpRequest& request) {
  const auto token = ExtractToken(request);
  if (token.empty()) {
    return HttpResponse::Json(401, ErrorJson("ERR_AUTH_TOKEN_MISSING", "Token not provided"));
  }
  std::string error;
  auto user = service_->GetUserFromToken(token, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_AUTH_TOKEN_INVALID", error.empty() ? "Invalid token" : error));
  }
  try {
    auto body = nlohmann::json::parse(request.body);
    const std::string current_password = body.value("currentPassword", body.value("current_password", ""));
    const std::string new_password = body.value("newPassword", body.value("new_password", ""));
    if (current_password.empty() || new_password.empty()) {
      return HttpResponse::Json(400, ErrorJson("ERR_PASSWORD_MISSING", "当前密码和新密码不能为空"));
    }
    if (!service_->ChangePassword(user->id, current_password, new_password, error)) {
      return HttpResponse::Json(400, ErrorJson("ERR_PASSWORD_CHANGE_FAILED", error.empty() ? "修改失败" : error));
    }
    return HttpResponse::Json(200, {{"message", "密码已更新"}});
  } catch (const std::exception& ex) {
    Logger::Error(std::string("ChangePassword parse error: ") + ex.what());
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
  }
}

HttpResponse AuthController::RequestPasswordReset(const HttpRequest& request) {
  try {
    auto body = nlohmann::json::parse(request.body);
    if (!body.contains("email") || !body["email"].is_string()) {
      return HttpResponse::Json(400, ErrorJson("ERR_AUTH_RESET_EMAIL_REQUIRED", "Email required"));
    }
    std::string error;
    if (!service_->RequestPasswordReset(body["email"].get<std::string>(), error)) {
      Logger::Error(std::string("RequestPasswordReset failed: ") + error);
      return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
    }
    return HttpResponse::Json(200, {{"message", "验证码已发送"}});
  } catch (const std::exception& ex) {
    Logger::Error(std::string("Failed to parse reset request: ") + ex.what());
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
  }
}

HttpResponse AuthController::ConfirmPasswordReset(const HttpRequest& request) {
  try {
    auto body = nlohmann::json::parse(request.body);
    if (!body.contains("email") || !body.contains("code") || !body.contains("password")) {
      return HttpResponse::Json(400, ErrorJson("ERR_AUTH_RESET_MISSING_FIELDS", "Missing required fields"));
    }
    std::string error;
    if (!service_->ResetPassword(body["email"].get<std::string>(),
                                 body["code"].get<std::string>(),
                                 body["password"].get<std::string>(),
                                 error)) {
      return HttpResponse::Json(400, ErrorJson("ERR_AUTH_RESET_FAILED", error.empty() ? "Reset failed" : error));
    }
    return HttpResponse::Json(200, {{"message", "密码已重置"}});
  } catch (const std::exception& ex) {
    Logger::Error(std::string("Failed to parse reset confirm: ") + ex.what());
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
  }
}

HttpResponse AuthController::DeleteAccount(const HttpRequest& request) {
  const auto token = ExtractToken(request);
  if (token.empty()) {
    return HttpResponse::Json(401, ErrorJson("ERR_AUTH_TOKEN_MISSING", "Token not provided"));
  }
  std::string error;
  auto user = service_->GetUserFromToken(token, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_AUTH_TOKEN_INVALID", error.empty() ? "Invalid token" : error));
  }
  if (user->is_admin) {
    return HttpResponse::Json(403, ErrorJson("ERR_FORBIDDEN", "管理员账号无法自助注销"));
  }
  try {
    auto body = nlohmann::json::parse(request.body);
    const std::string password = body.value("password", "");
    if (password.empty()) {
      return HttpResponse::Json(400, ErrorJson("ERR_PASSWORD_MISSING", "请输入密码以确认注销"));
    }
    if (!service_->DeleteAccount(user->id, password, token, error)) {
      return HttpResponse::Json(400, ErrorJson("ERR_DELETE_ACCOUNT_FAILED", error.empty() ? "注销失败" : error));
    }
    return HttpResponse::Json(200, {{"message", "账号已成功注销"}});
  } catch (const std::exception& ex) {
    Logger::Error(std::string("DeleteAccount parse error: ") + ex.what());
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
  }
}

std::string AuthController::ExtractToken(const HttpRequest& request) const {
  auto header = request.Header("authorization");
  if (header.rfind("Bearer ", 0) == 0 || header.rfind("bearer ", 0) == 0) {
    return header.substr(7);
  }
  return header;
}
