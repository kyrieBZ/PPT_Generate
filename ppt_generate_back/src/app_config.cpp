#include "app_config.h"

#include <filesystem>
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

AdminConfig ParseAdmin(const nlohmann::json& json) {
  AdminConfig cfg;
  if (auto it = json.find("usernames"); it != json.end() && it->is_array()) {
    for (const auto& item : *it) {
      if (item.is_string()) {
        cfg.usernames.push_back(item.get<std::string>());
      }
    }
  }
  if (auto it = json.find("emails"); it != json.end() && it->is_array()) {
    for (const auto& item : *it) {
      if (item.is_string()) {
        cfg.emails.push_back(item.get<std::string>());
      }
    }
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

EmailConfig ParseEmail(const nlohmann::json& json) {
  EmailConfig cfg;
  bool use_ssl = false;
  if (auto it = json.find("smtp_host"); it != json.end() && it->is_string()) {
    cfg.smtp_host = *it;
  }
  if (auto it = json.find("smtp_port"); it != json.end() && it->is_number_unsigned()) {
    cfg.smtp_port = static_cast<std::uint16_t>(it->get<std::uint32_t>());
  }
  if (auto it = json.find("smtp_user"); it != json.end() && it->is_string()) {
    cfg.smtp_user = *it;
  }
  if (auto it = json.find("smtp_password"); it != json.end() && it->is_string()) {
    cfg.smtp_password = *it;
  }
  if (auto it = json.find("from_email"); it != json.end() && it->is_string()) {
    cfg.from_email = *it;
  }
  if (auto it = json.find("from_name"); it != json.end() && it->is_string()) {
    cfg.from_name = *it;
  }
  if (auto it = json.find("smtp_security"); it != json.end() && it->is_string()) {
    cfg.smtp_security = *it;
  }
  if (auto it = json.find("use_ssl"); it != json.end() && it->is_boolean()) {
    use_ssl = it->get<bool>();
  }
  if (auto it = json.find("use_tls"); it != json.end() && it->is_boolean()) {
    cfg.use_tls = it->get<bool>();
  }
  if (cfg.smtp_security.empty() && use_ssl) {
    cfg.smtp_security = "smtps";
  }
  return cfg;
}

GenerationConfig ParseGeneration(const nlohmann::json& json, const std::filesystem::path& base_dir) {
  GenerationConfig cfg;
  if (auto it = json.find("output_dir"); it != json.end() && it->is_string()) {
    cfg.output_dir = *it;
  }
  if (auto it = json.find("python_binary"); it != json.end() && it->is_string()) {
    cfg.python_binary = *it;
  }
  if (auto it = json.find("builder_script"); it != json.end() && it->is_string()) {
    cfg.builder_script = *it;
  }
  if (auto it = json.find("soffice_binary"); it != json.end() && it->is_string()) {
    cfg.soffice_binary = *it;
  }

  auto make_absolute = [&](const std::string& value) {
    if (value.empty()) {
      return value;
    }
    std::filesystem::path path(value);
    if (path.is_relative()) {
      path = (base_dir / path).lexically_normal();
    }
    return path.lexically_normal().string();
  };

  cfg.output_dir = make_absolute(cfg.output_dir);
  cfg.builder_script = make_absolute(cfg.builder_script);
  return cfg;
}

S3Config ParseS3(const nlohmann::json& json) {
  S3Config cfg;
  if (auto it = json.find("endpoint"); it != json.end() && it->is_string()) {
    cfg.endpoint = *it;
  }
  if (auto it = json.find("public_endpoint"); it != json.end() && it->is_string()) {
    cfg.public_endpoint = *it;
  }
  if (auto it = json.find("access_key"); it != json.end() && it->is_string()) {
    cfg.access_key = *it;
  }
  if (auto it = json.find("secret_key"); it != json.end() && it->is_string()) {
    cfg.secret_key = *it;
  }
  if (auto it = json.find("region"); it != json.end() && it->is_string()) {
    cfg.region = *it;
  }
  if (auto it = json.find("bucket"); it != json.end() && it->is_string()) {
    cfg.bucket = *it;
  }
  if (auto it = json.find("url_expiration_seconds"); it != json.end() && it->is_number_unsigned()) {
    cfg.url_expiration_seconds = it->get<std::uint32_t>();
  }
  if (cfg.region.empty()) {
    cfg.region = "us-east-1";
  }
  if (cfg.url_expiration_seconds == 0) {
    cfg.url_expiration_seconds = 3600;
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
  const auto config_path = std::filesystem::absolute(std::filesystem::path(path));
  const auto config_dir = config_path.parent_path();
  const auto project_root = config_dir.parent_path();

  if (auto it = data.find("server"); it != data.end()) {
    config.server_ = ParseServer(*it);
  }
  if (auto it = data.find("database"); it != data.end()) {
    config.database_ = ParseDatabase(*it);
  }
  if (auto it = data.find("auth"); it != data.end()) {
    config.auth_ = ParseAuth(*it);
  }
  if (auto it = data.find("admin"); it != data.end()) {
    config.admin_ = ParseAdmin(*it);
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
  if (auto it = data.find("email"); it != data.end()) {
    config.email_ = ParseEmail(*it);
  }
  if (auto it = data.find("generation"); it != data.end()) {
    config.generation_ = ParseGeneration(*it, project_root);
  } else {
    config.generation_ = ParseGeneration(nlohmann::json::object(), project_root);
  }
  if (auto it = data.find("s3"); it != data.end()) {
    config.s3_ = ParseS3(*it);
  }

  if (config.database_.user.empty() || config.database_.name.empty()) {
    throw std::runtime_error("Database user/name must be configured");
  }

  return config;
}
