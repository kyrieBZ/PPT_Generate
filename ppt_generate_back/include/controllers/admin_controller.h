#pragma once

#include <memory>
#include <string>

#include "http/http_types.h"
#include "services/auth_service.h"

class AdminController {
 public:
  explicit AdminController(std::shared_ptr<AuthService> auth_service);

  HttpResponse ListUsers(const HttpRequest& request);
  HttpResponse UpdateUserStatus(const HttpRequest& request);

 private:
  std::shared_ptr<User> AuthenticateAdmin(const HttpRequest& request, std::string& error) const;

  std::shared_ptr<AuthService> auth_service_;
};
