#pragma once

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>

#include "app_config.h"
#include "http/http_types.h"
#include "services/auth_service.h"
#include "services/model_service.h"
#include "services/ppt_service.h"
#include "services/qwen_client.h"
#include "services/template_service.h"

class PptController {
 public:
  PptController(std::shared_ptr<AuthService> auth_service,
                std::shared_ptr<PptService> ppt_service,
                std::shared_ptr<ModelService> model_service,
                std::shared_ptr<TemplateService> template_service,
                GenerationConfig generation_config,
                std::shared_ptr<QwenClient> qwen_client);

  HttpResponse Generate(const HttpRequest& request);
  HttpResponse History(const HttpRequest& request);
  HttpResponse Delete(const HttpRequest& request);
  HttpResponse Download(const HttpRequest& request);
  HttpResponse Preview(const HttpRequest& request);

 private:
  std::shared_ptr<User> Authenticate(const HttpRequest& request, std::string& error_message) const;
  std::uint64_t ParseId(const std::string& str) const;

  std::shared_ptr<AuthService> auth_service_;
  std::shared_ptr<PptService> ppt_service_;
  std::shared_ptr<ModelService> model_service_;
  std::shared_ptr<TemplateService> template_service_;
  GenerationConfig generation_config_;
  std::shared_ptr<QwenClient> qwen_client_;
};
