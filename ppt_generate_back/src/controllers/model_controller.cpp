#include "controllers/model_controller.h"

ModelController::ModelController(std::shared_ptr<ModelService> service)
    : service_(std::move(service)) {}

HttpResponse ModelController::List(const HttpRequest& request) {
  nlohmann::json payload;
  payload["items"] = nlohmann::json::array();
  for (const auto& model : service_->GetAll()) {
    payload["items"].push_back(ToJson(model));
  }
  payload["total"] = payload["items"].size();
  return HttpResponse::Json(200, payload);
}

nlohmann::json ModelController::ToJson(const GenerationModel& model) {
  return {
      {"id", model.id},
      {"name", model.name},
      {"provider", model.provider},
      {"locale", model.locale},
      {"description", model.description},
      {"latency", model.latency_level},
      {"cost", model.cost_level},
      {"capabilities", model.capabilities}};
}
