#pragma once

#include <memory>

#include <nlohmann/json.hpp>

#include "http/http_types.h"
#include "services/model_service.h"

class ModelController {
 public:
  explicit ModelController(std::shared_ptr<ModelService> service);

  HttpResponse List(const HttpRequest& request);

 private:
  static nlohmann::json ToJson(const GenerationModel& model);

  std::shared_ptr<ModelService> service_;
};
