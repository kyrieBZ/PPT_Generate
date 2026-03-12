#pragma once

#include <memory>

#include "http/http_types.h"
#include "services/assistant_service.h"
#include "services/auth_service.h"

class AssistantController {
 public:
  AssistantController(std::shared_ptr<AuthService> auth_service,
                      std::shared_ptr<AssistantService> assistant_service);

  HttpResponse Chat(const HttpRequest& request);

 private:
  std::shared_ptr<AuthService> auth_service_;
  std::shared_ptr<AssistantService> assistant_service_;
};
