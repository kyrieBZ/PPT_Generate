#include "services/model_service.h"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

ModelService::ModelService(const std::string& catalog_path) {
  LoadCatalog(catalog_path);
}

void ModelService::LoadCatalog(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Unable to open model catalog: " + path);
  }
  nlohmann::json data;
  file >> data;
  if (!data.is_array()) {
    throw std::runtime_error("Model catalog must be an array");
  }
  models_.clear();
  model_map_.clear();
  for (const auto& item : data) {
    GenerationModel model;
    model.id = item.value("id", "");
    model.name = item.value("name", "");
    model.provider = item.value("provider", "");
    model.locale = item.value("locale", "");
    model.description = item.value("description", "");
    model.latency_level = item.value("latency", "");
    model.cost_level = item.value("cost", "");
    if (item.contains("capabilities") && item["capabilities"].is_array()) {
      for (const auto& cap : item["capabilities"]) {
        model.capabilities.push_back(cap.get<std::string>());
      }
    }
    if (model.id.empty()) {
      continue;
    }
    model_map_[model.id] = model;
    models_.push_back(model);
  }
  if (models_.empty()) {
    throw std::runtime_error("Model catalog is empty");
  }
}

std::optional<GenerationModel> ModelService::FindById(const std::string& id) const {
  if (auto it = model_map_.find(id); it != model_map_.end()) {
    return it->second;
  }
  return std::nullopt;
}
