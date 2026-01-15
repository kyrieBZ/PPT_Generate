#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "models/ppt_template.h"

class TemplateService {
 public:
  explicit TemplateService(const std::string& catalog_path);

  const std::vector<RemoteTemplate>& GetAll() const { return templates_; }
  std::vector<RemoteTemplate> Search(const std::string& query) const;
  std::optional<RemoteTemplate> FindById(const std::string& id) const;
  std::optional<std::string> GetLocalFile(const std::string& id) const;

 private:
  void LoadCatalog(const std::string& path);

  std::vector<RemoteTemplate> templates_;
  std::filesystem::path catalog_dir_;
};
