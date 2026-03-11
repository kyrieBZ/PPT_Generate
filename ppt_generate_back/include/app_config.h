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
  /** 单次查询读/写超时（秒），0 表示不设置 */
  std::uint32_t query_timeout_seconds = 30;
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
  std::string doubao_api_key;
  std::string doubao_image_endpoint;
  std::string doubao_image_model;
  std::string doubao_image_size = "1024x1024";
  std::string doubao_image_response_format = "url";
  std::uint32_t doubao_image_count = 1;
  std::uint32_t doubao_timeout_seconds = 30;
  /** 通义千问 HTTP 请求超时（秒），0 表示使用默认 60 */
  std::uint32_t qwen_timeout_seconds = 60;
  /** 通义万象（万相）文生图：异步任务创建接口，空则用默认 DashScope 地址 */
  std::string wanx_image_endpoint;
  /** 通义万象模型名，如 wan2.6-image */
  std::string wanx_image_model = "wan2.6-image";
  /** 通义万象轮询超时（秒），含创建+轮询总时间 */
  std::uint32_t wanx_timeout_seconds = 120;
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
  std::string image_dir = "assets/generated/images";
  std::string python_binary = "python3";
  std::string builder_script = "scripts/libreoffice_ppt_builder.py";
  std::string template_analyzer_script = "scripts/ppt_template_analyzer.py";
  std::string template_analysis_dir = "assets/template_analysis";
  std::string soffice_binary = "soffice";
  // PptxGenJS from-scratch builder (scheme B)
  std::string builder_mode = "python";
  std::string node_binary = "node";
  std::string pptxgen_builder_script = "scripts/pptxgen_builder.js";
};

struct MaterialConfig {
  std::string upload_dir = "assets/materials";
  std::uint64_t max_file_size_mb = 20;
  std::vector<std::string> allowed_types = {"pdf", "docx", "txt"};
  std::string extract_script = "scripts/extract_material.py";
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
  const MaterialConfig& material() const { return material_; }

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
  MaterialConfig material_{};
};
