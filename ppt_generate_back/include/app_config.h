#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

struct AdminConfig {
  std::vector<std::string> usernames;
  std::vector<std::string> emails;
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

struct EmailConfig {
  std::string smtp_host;
  std::uint16_t smtp_port = 587;
  std::string smtp_user;
  std::string smtp_password;
  std::string from_email;
  std::string from_name = "PPT生成系统";
  std::string smtp_security;
  bool use_tls = true;
};

struct GenerationConfig {
  std::string output_dir = "assets/generated";
  std::string python_binary = "python3";
  std::string builder_script = "scripts/libreoffice_ppt_builder.py";
  std::string soffice_binary = "soffice";
};

struct S3Config {
  std::string endpoint;
  std::string public_endpoint;
  std::string access_key;
  std::string secret_key;
  std::string region = "us-east-1";
  std::string bucket;
  std::uint32_t url_expiration_seconds = 3600;

  bool enabled() const {
    return !endpoint.empty() && !access_key.empty() && !secret_key.empty() && !bucket.empty();
  }

  std::string effective_public_endpoint() const {
    return public_endpoint.empty() ? endpoint : public_endpoint;
  }
};

class AppConfig {
 public:
  static AppConfig Load(const std::string& path);

  const ServerConfig& server() const { return server_; }
  const DatabaseConfig& database() const { return database_; }
  const AuthConfig& auth() const { return auth_; }
  const AdminConfig& admin() const { return admin_; }
  const TemplateConfig& templates() const { return templates_; }
  const ModelConfig& models() const { return models_; }
  const ProviderConfig& providers() const { return providers_; }
  const EmailConfig& email() const { return email_; }
  const GenerationConfig& generation() const { return generation_; }
  const S3Config& s3() const { return s3_; }

 private:
  ServerConfig server_{};
  DatabaseConfig database_{};
  AuthConfig auth_{};
  AdminConfig admin_{};
  TemplateConfig templates_{};
  ModelConfig models_{};
  ProviderConfig providers_{};
  EmailConfig email_{};
  GenerationConfig generation_{};
  S3Config s3_{};
};
