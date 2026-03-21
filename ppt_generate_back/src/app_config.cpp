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
  if (auto it = json.find("query_timeout_seconds"); it != json.end() && it->is_number_unsigned()) {
    cfg.query_timeout_seconds = it->get<std::uint32_t>();
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
  if (auto it = json.find("doubao_api_key"); it != json.end() && it->is_string()) {
    cfg.doubao_api_key = *it;
  }
  if (auto it = json.find("doubao_image_endpoint"); it != json.end() && it->is_string()) {
    cfg.doubao_image_endpoint = *it;
  }
  if (auto it = json.find("doubao_image_model"); it != json.end() && it->is_string()) {
    cfg.doubao_image_model = *it;
  }
  if (auto it = json.find("doubao_image_size"); it != json.end() && it->is_string()) {
    cfg.doubao_image_size = *it;
  }
  if (auto it = json.find("doubao_image_response_format"); it != json.end() && it->is_string()) {
    cfg.doubao_image_response_format = *it;
  }
  if (auto it = json.find("doubao_image_count"); it != json.end() && it->is_number_unsigned()) {
    cfg.doubao_image_count = it->get<std::uint32_t>();
  }
  if (auto it = json.find("doubao_timeout_seconds"); it != json.end() && it->is_number_unsigned()) {
    cfg.doubao_timeout_seconds = it->get<std::uint32_t>();
  }
  if (auto it = json.find("qwen_timeout_seconds"); it != json.end() && it->is_number_unsigned()) {
    cfg.qwen_timeout_seconds = it->get<std::uint32_t>();
  }
  if (cfg.qwen_timeout_seconds == 0) {
    cfg.qwen_timeout_seconds = 60;
  }
  if (auto it = json.find("wanx_image_endpoint"); it != json.end() && it->is_string()) {
    cfg.wanx_image_endpoint = *it;
  }
  if (auto it = json.find("wanx_image_model"); it != json.end() && it->is_string()) {
    cfg.wanx_image_model = *it;
  }
  if (auto it = json.find("wanx_timeout_seconds"); it != json.end() && it->is_number_unsigned()) {
    cfg.wanx_timeout_seconds = it->get<std::uint32_t>();
  }
  if (cfg.wanx_timeout_seconds == 0) {
    cfg.wanx_timeout_seconds = 120;
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
  if (auto it = json.find("image_dir"); it != json.end() && it->is_string()) {
    cfg.image_dir = *it;
  }
  if (auto it = json.find("python_binary"); it != json.end() && it->is_string()) {
    cfg.python_binary = *it;
  }
  if (auto it = json.find("builder_script"); it != json.end() && it->is_string()) {
    cfg.builder_script = *it;
  }
  if (auto it = json.find("template_analyzer_script"); it != json.end() && it->is_string()) {
    cfg.template_analyzer_script = *it;
  } else if (auto it = json.find("templateAnalyzerScript"); it != json.end() && it->is_string()) {
    cfg.template_analyzer_script = *it;
  }
  if (auto it = json.find("template_analysis_dir"); it != json.end() && it->is_string()) {
    cfg.template_analysis_dir = *it;
  } else if (auto it = json.find("templateAnalysisDir"); it != json.end() && it->is_string()) {
    cfg.template_analysis_dir = *it;
  }
  if (auto it = json.find("soffice_binary"); it != json.end() && it->is_string()) {
    cfg.soffice_binary = *it;
  }
  if (auto it = json.find("builder_mode"); it != json.end() && it->is_string()) {
    cfg.builder_mode = *it;
  }
  if (auto it = json.find("node_binary"); it != json.end() && it->is_string()) {
    cfg.node_binary = *it;
  }
  if (auto it = json.find("pptxgen_builder_script"); it != json.end() && it->is_string()) {
    cfg.pptxgen_builder_script = *it;
  }
  if (auto it = json.find("ai_native_builder_script"); it != json.end() && it->is_string()) {
    cfg.ai_native_builder_script = *it;
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
  cfg.image_dir = make_absolute(cfg.image_dir);
  cfg.builder_script = make_absolute(cfg.builder_script);
  cfg.template_analyzer_script = make_absolute(cfg.template_analyzer_script);
  cfg.template_analysis_dir = make_absolute(cfg.template_analysis_dir);
  cfg.pptxgen_builder_script = make_absolute(cfg.pptxgen_builder_script);
  cfg.ai_native_builder_script = make_absolute(cfg.ai_native_builder_script);
  return cfg;
}

MaterialConfig ParseMaterial(const nlohmann::json& json, const std::filesystem::path& base_dir) {
  MaterialConfig cfg;
  if (auto it = json.find("upload_dir"); it != json.end() && it->is_string()) {
    cfg.upload_dir = *it;
  }
  if (auto it = json.find("max_file_size_mb"); it != json.end() && it->is_number_unsigned()) {
    cfg.max_file_size_mb = it->get<std::uint64_t>();
  }
  if (auto it = json.find("allowed_types"); it != json.end() && it->is_array()) {
    cfg.allowed_types.clear();
    for (const auto& item : *it) {
      if (item.is_string()) {
        cfg.allowed_types.push_back(item.get<std::string>());
      }
    }
  }
  if (auto it = json.find("extract_script"); it != json.end() && it->is_string()) {
    cfg.extract_script = *it;
  }
  auto make_absolute = [&](const std::string& value) {
    if (value.empty()) return value;
    std::filesystem::path path(value);
    if (path.is_relative()) {
      path = (base_dir / path).lexically_normal();
    }
    return path.lexically_normal().string();
  };
  cfg.upload_dir = make_absolute(cfg.upload_dir);
  cfg.extract_script = make_absolute(cfg.extract_script);
  return cfg;
}

MongoConfig ParseMongo(const nlohmann::json& json) {
  MongoConfig cfg;
  if (auto it = json.find("uri"); it != json.end() && it->is_string()) {
    cfg.uri = *it;
  }
  if (auto it = json.find("database"); it != json.end() && it->is_string()) {
    cfg.database = *it;
  }
  if (auto it = json.find("enabled"); it != json.end() && it->is_boolean()) {
    cfg.enabled = it->get<bool>();
  }
  if (cfg.uri.empty()) {
    cfg.uri = "mongodb://localhost:27017";
  }
  if (cfg.database.empty()) {
    cfg.database = "ppt_generate_chat";
  }
  return cfg;
}

RedisConfig ParseRedis(const nlohmann::json& json) {
  RedisConfig cfg;
  if (auto it = json.find("host"); it != json.end() && it->is_string()) {
    cfg.host = *it;
  }
  if (auto it = json.find("port"); it != json.end() && it->is_number_unsigned()) {
    cfg.port = static_cast<std::uint16_t>(it->get<std::uint32_t>());
  }
  if (auto it = json.find("password"); it != json.end() && it->is_string()) {
    cfg.password = *it;
  }
  if (auto it = json.find("db"); it != json.end() && it->is_number_integer()) {
    cfg.db = it->get<int>();
  }
  if (auto it = json.find("pool_size"); it != json.end() && it->is_number_unsigned()) {
    cfg.pool_size = it->get<int>();
  }
  if (auto it = json.find("connect_timeout_ms"); it != json.end() && it->is_number_unsigned()) {
    cfg.connect_timeout_ms = it->get<int>();
  }
  if (auto it = json.find("socket_timeout_ms"); it != json.end() && it->is_number_unsigned()) {
    cfg.socket_timeout_ms = it->get<int>();
  }
  if (auto it = json.find("enabled"); it != json.end() && it->is_boolean()) {
    cfg.enabled = it->get<bool>();
  }
  if (auto it = json.find("ttl"); it != json.end() && it->is_object()) {
    const auto& ttl = *it;
    if (auto jt = ttl.find("auth_token"); jt != ttl.end() && jt->is_number_unsigned()) {
      cfg.ttl_auth_token = jt->get<int>();
    }
    if (auto jt = ttl.find("ppt_status"); jt != ttl.end() && jt->is_number_unsigned()) {
      cfg.ttl_ppt_status = jt->get<int>();
    }
    if (auto jt = ttl.find("ppt_history"); jt != ttl.end() && jt->is_number_unsigned()) {
      cfg.ttl_ppt_history = jt->get<int>();
    }
  }
  if (cfg.pool_size <= 0) cfg.pool_size = 4;
  if (cfg.connect_timeout_ms <= 0) cfg.connect_timeout_ms = 200;
  if (cfg.socket_timeout_ms <= 0) cfg.socket_timeout_ms = 500;
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

FastDfsConfig ParseFastDfs(const nlohmann::json& json) {
  FastDfsConfig cfg;
  if (auto it = json.find("enabled"); it != json.end() && it->is_boolean()) {
    cfg.enabled = it->get<bool>();
  }
  if (auto it = json.find("client_conf"); it != json.end() && it->is_string()) {
    cfg.client_conf = *it;
  }
  if (auto it = json.find("tracker_http_url"); it != json.end() && it->is_string()) {
    cfg.tracker_http_url = *it;
  }
  if (auto it = json.find("storage_http_url"); it != json.end() && it->is_string()) {
    cfg.storage_http_url = *it;
  }
  if (auto it = json.find("group_name"); it != json.end() && it->is_string()) {
    cfg.group_name = *it;
  }
  if (auto it = json.find("upload_timeout_seconds"); it != json.end() && it->is_number_unsigned()) {
    cfg.upload_timeout_seconds = it->get<std::uint32_t>();
  }
  if (auto it = json.find("delete_local_after_upload"); it != json.end() && it->is_boolean()) {
    cfg.delete_local_after_upload = it->get<bool>();
  }
  if (auto it = json.find("use_for_materials"); it != json.end() && it->is_boolean()) {
    cfg.use_for_materials = it->get<bool>();
  }
  if (auto it = json.find("use_for_templates"); it != json.end() && it->is_boolean()) {
    cfg.use_for_templates = it->get<bool>();
  }
  if (cfg.group_name.empty()) {
    cfg.group_name = "group1";
  }
  if (cfg.upload_timeout_seconds == 0) {
    cfg.upload_timeout_seconds = 30;
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
  if (auto it = data.find("fastdfs"); it != data.end()) {
    config.fastdfs_ = ParseFastDfs(*it);
  }
  if (auto it = data.find("material"); it != data.end()) {
    config.material_ = ParseMaterial(*it, project_root);
  } else {
    config.material_ = ParseMaterial(nlohmann::json::object(), project_root);
  }

  if (auto it = data.find("mongodb"); it != data.end()) {
    config.mongodb_ = ParseMongo(*it);
  }

  if (auto it = data.find("redis"); it != data.end()) {
    config.redis_ = ParseRedis(*it);
  }

  if (auto it = data.find("ai_search"); it != data.end()) {
    const auto& j = *it;
    if (auto jt = j.find("enabled"); jt != j.end() && jt->is_boolean()) {
      config.ai_search_.enabled = jt->get<bool>();
    }
    if (auto jt = j.find("qdrant_host"); jt != j.end() && jt->is_string()) {
      config.ai_search_.qdrant_host = *jt;
    }
    if (auto jt = j.find("qdrant_port"); jt != j.end() && jt->is_number_unsigned()) {
      config.ai_search_.qdrant_port = static_cast<std::uint16_t>(jt->get<std::uint32_t>());
    }
    if (auto jt = j.find("collection_name"); jt != j.end() && jt->is_string()) {
      config.ai_search_.collection_name = *jt;
    }
    if (auto jt = j.find("embedding_model"); jt != j.end() && jt->is_string()) {
      config.ai_search_.embedding_model = *jt;
    }
    if (auto jt = j.find("embedding_dimension"); jt != j.end() && jt->is_number_integer()) {
      config.ai_search_.embedding_dimension = jt->get<int>();
    }
    if (auto jt = j.find("top_k_retrieve"); jt != j.end() && jt->is_number_integer()) {
      config.ai_search_.top_k_retrieve = jt->get<int>();
    }
    if (auto jt = j.find("top_k_return"); jt != j.end() && jt->is_number_integer()) {
      config.ai_search_.top_k_return = jt->get<int>();
    }
    if (auto jt = j.find("score_threshold"); jt != j.end() && jt->is_number()) {
      config.ai_search_.score_threshold = jt->get<double>();
    }
    if (auto jt = j.find("enable_rerank"); jt != j.end() && jt->is_boolean()) {
      config.ai_search_.enable_rerank = jt->get<bool>();
    }
    if (auto jt = j.find("rerank_model"); jt != j.end() && jt->is_string()) {
      config.ai_search_.rerank_model = *jt;
    }
  }

  // auth.token_ttl_minutes 优先于 redis.ttl.auth_token（保持一致）
  if (config.redis_.ttl_auth_token <= 0) {
    config.redis_.ttl_auth_token =
        static_cast<int>(config.auth_.token_ttl_minutes) * 60;
  }

  if (config.database_.user.empty() || config.database_.name.empty()) {
    throw std::runtime_error("Database user/name must be configured");
  }

  return config;
}
