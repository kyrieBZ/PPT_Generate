#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "models/generation_model.h"

class ModelService {
 public:
  explicit ModelService(const std::string& catalog_path);

  const std::vector<GenerationModel>& GetAll() const { return models_; }
  std::optional<GenerationModel> FindById(const std::string& id) const;

 private:
  void LoadCatalog(const std::string& path);

  std::vector<GenerationModel> models_;
  std::unordered_map<std::string, GenerationModel> model_map_;
};
