#pragma once

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "http/http_types.h"
#include "services/auth_service.h"

class AuthController {
 public:
  explicit AuthController(std::shared_ptr<AuthService> service);

  HttpResponse Register(const HttpRequest& request);
  HttpResponse Login(const HttpRequest& request);
  HttpResponse Logout(const HttpRequest& request);
  HttpResponse CurrentUser(const HttpRequest& request);
  HttpResponse RequestPasswordReset(const HttpRequest& request);
  HttpResponse ConfirmPasswordReset(const HttpRequest& request);

 private:
  std::string ExtractToken(const HttpRequest& request) const;

  std::shared_ptr<AuthService> service_;
};
