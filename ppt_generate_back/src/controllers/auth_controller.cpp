#include "controllers/auth_controller.h"

#include "http/http_types.h"
#include "logger.h"

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

AuthController::AuthController(std::shared_ptr<AuthService> service) : service_(std::move(service)) {}

HttpResponse AuthController::Register(const HttpRequest& request) {
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

std::string AuthController::ExtractToken(const HttpRequest& request) const {
  auto header = request.Header("authorization");
  if (header.rfind("Bearer ", 0) == 0 || header.rfind("bearer ", 0) == 0) {
    return header.substr(7);
  }
  return header;
}
