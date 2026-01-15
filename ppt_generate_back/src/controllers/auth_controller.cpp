#include "controllers/auth_controller.h"

#include "logger.h"

namespace {
nlohmann::json UserJson(const User& user) {
  nlohmann::json payload = {
      {"id", user.id},
      {"username", user.username},
      {"email", user.email},
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
      return HttpResponse::Json(400, {{"message", "缺少必要字段"}});
    }

    User user;
    std::string token;
    std::string error;
    if (!service_->RegisterUser(body["username"], body["email"], body["password"], user, token, error)) {
      return HttpResponse::Json(400, {{"message", error.empty() ? "注册失败" : error}});
    }

    nlohmann::json payload{{"token", token}, {"user", UserJson(user)}};
    HttpResponse response = HttpResponse::Json(201, payload);
    response.status_message = "Created";
    return response;
  } catch (const std::exception& ex) {
    Logger::Error(std::string("注册请求解析失败: ") + ex.what());
    return HttpResponse::Json(400, {{"message", "无效的JSON"}});
  }
}

HttpResponse AuthController::Login(const HttpRequest& request) {
  try {
    auto body = nlohmann::json::parse(request.body);
    if (!body.contains("username") || !body.contains("password")) {
      return HttpResponse::Json(400, {{"message", "缺少用户名或密码"}});
    }

    User user;
    std::string token;
    std::string error;
    if (!service_->Login(body["username"], body["password"], user, token, error)) {
      return HttpResponse::Json(401, {{"message", error.empty() ? "登录失败" : error}});
    }

    return HttpResponse::Json(200, {{"token", token}, {"user", UserJson(user)}});
  } catch (const std::exception& ex) {
    Logger::Error(std::string("登录请求解析失败: ") + ex.what());
    return HttpResponse::Json(400, {{"message", "无效的JSON"}});
  }
}

HttpResponse AuthController::Logout(const HttpRequest& request) {
  const auto token = ExtractToken(request);
  if (token.empty()) {
    return HttpResponse::Json(401, {{"message", "未提供Token"}});
  }

  std::string error;
  if (!service_->Logout(token, error)) {
    return HttpResponse::Json(400, {{"message", error.empty() ? "登出失败" : error}});
  }

  return HttpResponse::Json(200, {{"message", "已退出登录"}});
}

HttpResponse AuthController::CurrentUser(const HttpRequest& request) {
  const auto token = ExtractToken(request);
  if (token.empty()) {
    return HttpResponse::Json(401, {{"message", "未提供Token"}});
  }

  std::string error;
  auto user = service_->GetUserFromToken(token, error);
  if (!user) {
    return HttpResponse::Json(401, {{"message", error.empty() ? "Token无效" : error}});
  }

  return HttpResponse::Json(200, {{"user", UserJson(*user)}});
}

std::string AuthController::ExtractToken(const HttpRequest& request) const {
  auto header = request.Header("authorization");
  if (header.rfind("Bearer ", 0) == 0 || header.rfind("bearer ", 0) == 0) {
    return header.substr(7);
  }
  return header;
}
