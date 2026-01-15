#include "app_config.h"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace {
ServerConfig ParseServer(const nlohmann::json& json) {
  ServerConfig cfg;
  if (auto it = json.find("host"); it != json.end() && it->is_string()) {
    cfg.host = *it;
  }
  if (auto it = json.find("port"); it != json.end() && it->is_number_unsigned()) {
    cfg.port = static_cast<std::uint16_t>(it->get<std::uint32_t>());
  }
  if (auto it = json.find("thread_count"); it != json.end() && it->is_number_unsigned()) {
    cfg.thread_count = static_cast<std::size_t>(it->get<std::uint32_t>());
  }
  if (cfg.thread_count == 0) {
    cfg.thread_count = 1;
  }
  return cfg;
}

DatabaseConfig ParseDatabase(const nlohmann::json& json) {
  DatabaseConfig cfg;
  if (auto it = json.find("host"); it != json.end() && it->is_string()) {
    cfg.host = *it;
  }
  if (auto it = json.find("port"); it != json.end() && it->is_number_unsigned()) {
    cfg.port = static_cast<std::uint16_t>(it->get<std::uint32_t>());
  }
  if (auto it = json.find("user"); it != json.end() && it->is_string()) {
    cfg.user = *it;
  }
  if (auto it = json.find("password"); it != json.end() && it->is_string()) {
    cfg.password = *it;
  }
  if (auto it = json.find("name"); it != json.end() && it->is_string()) {
    cfg.name = *it;
  }
  if (auto it = json.find("pool_size"); it != json.end() && it->is_number_unsigned()) {
    cfg.pool_size = static_cast<std::size_t>(it->get<std::uint32_t>());
  }
  if (cfg.pool_size == 0) {
    cfg.pool_size = 1;
  }
  return cfg;
}

AuthConfig ParseAuth(const nlohmann::json& json) {
  AuthConfig cfg;
  if (auto it = json.find("token_ttl_minutes"); it != json.end() && it->is_number_unsigned()) {
    cfg.token_ttl_minutes = it->get<std::uint32_t>();
  }
  if (cfg.token_ttl_minutes == 0) {
    cfg.token_ttl_minutes = 60;
  }
  return cfg;
}

TemplateConfig ParseTemplates(const nlohmann::json& json) {
  TemplateConfig cfg;
  if (auto it = json.find("catalog_path"); it != json.end() && it->is_string()) {
    cfg.catalog_path = *it;
  }
  if (cfg.catalog_path.empty()) {
    cfg.catalog_path = "config/templates.json";
  }
  return cfg;
}

ModelConfig ParseModels(const nlohmann::json& json) {
  ModelConfig cfg;
  if (auto it = json.find("catalog_path"); it != json.end() && it->is_string()) {
    cfg.catalog_path = *it;
  }
  if (cfg.catalog_path.empty()) {
    cfg.catalog_path = "config/models.json";
  }
  return cfg;
}

ProviderConfig ParseProviders(const nlohmann::json& json) {
  ProviderConfig cfg;
  if (auto it = json.find("qwen_api_key"); it != json.end() && it->is_string()) {
    cfg.qwen_api_key = *it;
  }
  return cfg;
}
}  // namespace

AppConfig AppConfig::Load(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Unable to open config file: " + path);
  }

  nlohmann::json data;
  try {
    file >> data;
  } catch (const std::exception& ex) {
    throw std::runtime_error(std::string("Invalid JSON config: ") + ex.what());
  }

  AppConfig config;
  if (auto it = data.find("server"); it != data.end()) {
    config.server_ = ParseServer(*it);
  }
  if (auto it = data.find("database"); it != data.end()) {
    config.database_ = ParseDatabase(*it);
  }
  if (auto it = data.find("auth"); it != data.end()) {
    config.auth_ = ParseAuth(*it);
  }
  if (auto it = data.find("templates"); it != data.end()) {
    config.templates_ = ParseTemplates(*it);
  }
  if (auto it = data.find("models"); it != data.end()) {
    config.models_ = ParseModels(*it);
  }
  if (auto it = data.find("providers"); it != data.end()) {
    config.providers_ = ParseProviders(*it);
  }

  if (config.database_.user.empty() || config.database_.name.empty()) {
    throw std::runtime_error("Database user/name must be configured");
  }

  return config;
}
