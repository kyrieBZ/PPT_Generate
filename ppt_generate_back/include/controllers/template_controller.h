#pragma once

#include <memory>

#include <nlohmann/json.hpp>

#include "http/http_types.h"
#include "services/template_service.h"

class TemplateController {
 public:
  explicit TemplateController(std::shared_ptr<TemplateService> service);

  HttpResponse List(const HttpRequest& request);
  HttpResponse Download(const HttpRequest& request);

 private:
  static nlohmann::json ToJson(const RemoteTemplate& item);

  std::shared_ptr<TemplateService> service_;
};
