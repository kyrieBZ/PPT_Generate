#pragma once

#include <cstdint>
#include <string>

struct ServerConfig {
  std::string host = "0.0.0.0";
  std::uint16_t port = 8080;
  std::size_t thread_count = 4;
};

struct DatabaseConfig {
  std::string host = "127.0.0.1";
  std::uint16_t port = 3306;
  std::string user;
  std::string password;
  std::string name;
  std::size_t pool_size = 8;
};

struct AuthConfig {
  std::uint32_t token_ttl_minutes = 120;
};

struct TemplateConfig {
  std::string catalog_path = "config/templates.json";
};

struct ModelConfig {
  std::string catalog_path = "config/models.json";
};

struct ProviderConfig {
  std::string qwen_api_key;
};

struct GenerationConfig {
  std::string output_dir = "assets/generated";
  std::string python_binary = "python3";
  std::string builder_script = "scripts/libreoffice_ppt_builder.py";
  std::string soffice_binary = "soffice";
};

class AppConfig {
 public:
  static AppConfig Load(const std::string& path);

  const ServerConfig& server() const { return server_; }
  const DatabaseConfig& database() const { return database_; }
  const AuthConfig& auth() const { return auth_; }
  const TemplateConfig& templates() const { return templates_; }
  const ModelConfig& models() const { return models_; }
  const ProviderConfig& providers() const { return providers_; }
  const GenerationConfig& generation() const { return generation_; }

 private:
  ServerConfig server_{};
  DatabaseConfig database_{};
  AuthConfig auth_{};
  TemplateConfig templates_{};
  ModelConfig models_{};
  ProviderConfig providers_{};
  GenerationConfig generation_{};
};
