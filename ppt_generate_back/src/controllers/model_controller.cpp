#include "controllers/model_controller.h"

ModelController::ModelController(std::shared_ptr<ModelService> service)
    : service_(std::move(service)) {}

HttpResponse ModelController::List(const HttpRequest& /*request*/) {
  auto list = service_->GetAll();
  nlohmann::json payload;
  payload["items"] = nlohmann::json::array();
  for (const auto& item : list) {
    nlohmann::json itemJson;
    itemJson["id"] = item.id;
    itemJson["name"] = item.name;
    itemJson["provider"] = item.provider;
    itemJson["description"] = item.description;
    itemJson["enabled"] = true;  // 默认启用所有模型
    itemJson["default"] = false; // 默认没有预设模型
    payload["items"].push_back(itemJson);
  }
  return HttpResponse::Json(200, payload);
}

