#include <atomic>
#include <csignal>
#include <chrono>
#include <iostream>
#include <thread>

#include <nlohmann/json.hpp>

#include "app_config.h"
#include "controllers/auth_controller.h"
#include "controllers/admin_controller.h"
#include "controllers/assistant_controller.h"
#include "controllers/ppt_controller.h"
#include "controllers/template_controller.h"
#include "controllers/material_controller.h"
#include "controllers/model_controller.h"
#include "controllers/announcement_controller.h"
#include "database/mongo_client.h"
#include "database/mysql_connection_pool.h"
#include "database/redis_client.h"
#include "http/http_server.h"
#include "http/http_types.h"
#include "logger.h"
#include "services/assistant_service.h"
#include "services/auth_service.h"
#include "services/email_service.h"
#include "services/ppt_service.h"
#include "services/qwen_client.h"
#include "services/material_service.h"
#include "services/template_service.h"
#include "services/ppt_service_interface.h"
#include "services/libreoffice_powerpoint_service.h"
#include "services/s3_client.h"
#include "services/wanxiang_image_client.h"
#include "utils/thread_pool.h"
#include "utils/ppt_metrics.h"

namespace {
std::atomic<bool> g_should_stop{false};

void SignalHandler(int) {
  g_should_stop.store(true);
}
}

int main(int argc, char* argv[]) {
  std::string config_path = "config/config.json";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
      config_path = argv[++i];
    }
  }

  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);
#ifdef SIGPIPE
  std::signal(SIGPIPE, SIG_IGN);
#endif

  try {
    const auto config = AppConfig::Load(config_path);

    auto pool = std::make_shared<MySQLConnectionPool>(config.database());

    // ── Redis（可选）────────────────────────────────────────────────────────
    std::shared_ptr<RedisClient> redis_client;
    if (config.redis().enabled) {
      try {
        redis_client = std::make_shared<RedisClient>(
            config.redis().host,
            config.redis().port,
            config.redis().password,
            config.redis().db,
            config.redis().pool_size,
            config.redis().connect_timeout_ms,
            config.redis().socket_timeout_ms);
        if (redis_client->Ping()) {
          Logger::Info("Redis connected: " + config.redis().host + ":" +
                       std::to_string(config.redis().port));
        } else {
          Logger::Warn("Redis Ping failed — running without cache.");
          redis_client.reset();
        }
      } catch (const std::exception& e) {
        Logger::Warn(std::string("Redis init failed, running without cache: ") + e.what());
        redis_client.reset();
      }
    }

    auto email_service = std::make_shared<EmailService>(config.email());
    auto auth_service = std::make_shared<AuthService>(
        pool, config.auth(), config.admin(), email_service, redis_client);
    auto ppt_service = std::make_shared<PptService>(pool);

    std::shared_ptr<IPowerPointServiceFactory> factory;
    LibreOfficeRuntimeOptions runtime_options;
    runtime_options.python_binary = config.generation().python_binary;
    runtime_options.builder_script = config.generation().builder_script;
    runtime_options.soffice_binary = config.generation().soffice_binary;
    runtime_options.builder_mode = config.generation().builder_mode;
    runtime_options.node_binary = config.generation().node_binary;
    runtime_options.pptxgen_builder_script = config.generation().pptxgen_builder_script;
    factory = std::make_shared<LibreOfficePowerPointServiceFactory>(runtime_options);
    if (factory) {
        ppt_service->SetPowerPointServiceFactory(factory);
    }

    auto template_service = std::make_shared<TemplateService>(config.templates().catalog_path);
    auto model_service = std::make_shared<ModelService>(config.models().catalog_path);
    std::shared_ptr<S3Client> s3_client;
    if (config.s3().enabled()) {
      s3_client = std::make_shared<S3Client>(config.s3());
      Logger::Info("S3 upload enabled: bucket=" + config.s3().bucket);
    }
    std::shared_ptr<QwenClient> qwen_client;
    if (!config.providers().qwen_api_key.empty()) {
      qwen_client = std::make_shared<QwenClient>(config.providers().qwen_api_key,
                                                 config.providers().qwen_timeout_seconds);
    }
    std::shared_ptr<WanxiangImageClient> wanx_client;
    if (!config.providers().qwen_api_key.empty()) {
      wanx_client = std::make_shared<WanxiangImageClient>(config.providers());
    }

    auto thread_pool = std::make_shared<ThreadPool>(4);

    std::string qwen_key = config.providers().qwen_api_key;
    auto material_service = std::make_shared<MaterialService>(
        pool, config.material(), qwen_key, config.generation().python_binary);

    // ── MongoDB（可选）──────────────────────────────────────────────────────
    std::shared_ptr<MongoClient> mongo_client;
    if (config.mongodb().enabled) {
      try {
        mongo_client = std::make_shared<MongoClient>(
            config.mongodb().uri, config.mongodb().database);
        if (mongo_client->IsConnected()) {
          Logger::Info("MongoDB connected: " + config.mongodb().uri +
                       " / " + config.mongodb().database);
        } else {
          Logger::Warn("MongoDB connection failed — chat persistence disabled.");
          mongo_client.reset();
        }
      } catch (const std::exception& e) {
        Logger::Warn(std::string("MongoDB init failed: ") + e.what());
        mongo_client.reset();
      }
    }

    auto assistant_service = std::make_shared<AssistantService>(qwen_key, 30, mongo_client);

    // 将 Qwen 配置注入 GenerationConfig，供 AI 原生链路使用
    GenerationConfig gen_config = config.generation();
    gen_config.qwen_api_key = config.providers().qwen_api_key;
    gen_config.qwen_timeout_seconds = config.providers().qwen_timeout_seconds;

    Router router;
    AuthController auth_controller(auth_service);
    AdminController admin_controller(auth_service);
    AssistantController assistant_controller(auth_service, assistant_service);
    MaterialController material_controller(auth_service, material_service, thread_pool,
                                           config.providers().qwen_api_key,
                                           config.providers().qwen_timeout_seconds);
    PptController ppt_controller(auth_service,
                                 ppt_service,
                                 model_service,
                                 template_service,
                                 gen_config,
                                 qwen_client,
                                 s3_client,
                                 wanx_client,
                                 thread_pool,
                                 material_service,
                                 redis_client,
                                 config.redis().ttl_ppt_status,
                                 config.redis().ttl_ppt_history);
    TemplateController template_controller(template_service);
    ModelController model_controller(model_service);
    AnnouncementController announcement_controller(auth_service, pool);

    Logger::Info("PPT output directory: " + config.generation().output_dir);

    router.AddRoute("GET", "/api/health", [](const HttpRequest&) {
      return HttpResponse::Json(200, {{"status", "ok"}});
    });
    router.AddRoute("GET", "/api/metrics", [](const HttpRequest&) {
      nlohmann::json gen = {
          {"total", PptMetrics::GenerationTotal().load(std::memory_order_relaxed)},
          {"success", PptMetrics::GenerationSuccess().load(std::memory_order_relaxed)},
          {"failed", PptMetrics::GenerationFailed().load(std::memory_order_relaxed)},
          {"last_duration_ms", PptMetrics::LastGenerationDurationMs().load(std::memory_order_relaxed)}
      };
      return HttpResponse::Json(200, {{"generation", gen}});
    });

    router.AddRoute("POST", "/api/auth/register", [&auth_controller](const HttpRequest& request) {
      return auth_controller.Register(request);
    });

    router.AddRoute("POST", "/api/auth/login", [&auth_controller](const HttpRequest& request) {
      return auth_controller.Login(request);
    });

    router.AddRoute("POST", "/api/auth/logout", [&auth_controller](const HttpRequest& request) {
      return auth_controller.Logout(request);
    });

    router.AddRoute("POST", "/api/auth/password/reset/request", [&auth_controller](const HttpRequest& request) {
      return auth_controller.RequestPasswordReset(request);
    });
    router.AddRoute("POST", "/api/auth/password/reset/confirm", [&auth_controller](const HttpRequest& request) {
      return auth_controller.ConfirmPasswordReset(request);
    });

    router.AddRoute("GET", "/api/auth/user", [&auth_controller](const HttpRequest& request) {
      return auth_controller.CurrentUser(request);
    });
    router.AddRoute("POST", "/api/auth/password/change", [&auth_controller](const HttpRequest& request) {
      return auth_controller.ChangePassword(request);
    });

    router.AddRoute("POST", "/api/ppt/generate", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.Generate(request);
    });
    router.AddRoute("GET", "/api/ppt/request", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.GetRequestStatus(request);
    });

    router.AddRoute("GET", "/api/ppt/history", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.History(request);
    });

    router.AddRoute("GET", "/api/admin/ppt/history", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.AdminHistory(request);
    });
    router.AddRoute("GET", "/api/admin/ppt/metrics", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.AdminMetrics(request);
    });
    router.AddRoute("GET", "/api/admin/insights", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.AdminInsights(request);
    });
    router.AddRoute("GET", "/api/admin/users", [&admin_controller](const HttpRequest& request) {
      return admin_controller.ListUsers(request);
    });
    router.AddRoute("POST", "/api/admin/users/status", [&admin_controller](const HttpRequest& request) {
      return admin_controller.UpdateUserStatus(request);
    });

    // ── 管理员素材管理 ──────────────────────────────────────────────────────
    router.AddRoute("GET", "/api/admin/materials", [&material_controller](const HttpRequest& request) {
      return material_controller.AdminList(request);
    });
    router.AddRoute("GET", "/api/admin/materials/stats", [&material_controller](const HttpRequest& request) {
      return material_controller.AdminStats(request);
    });
    router.AddRoute("GET", "/api/admin/materials/content", [&material_controller](const HttpRequest& request) {
      return material_controller.AdminGetContent(request);
    });
    router.AddRoute("GET", "/api/admin/materials/file", [&material_controller](const HttpRequest& request) {
      return material_controller.AdminGetFile(request);
    });
    router.AddRoute("POST", "/api/admin/materials/review", [&material_controller](const HttpRequest& request) {
      return material_controller.AdminReview(request);
    });
    router.AddRoute("DELETE", "/api/admin/materials", [&material_controller](const HttpRequest& request) {
      return material_controller.AdminDelete(request);
    });
    router.AddRoute("POST", "/api/admin/materials/batch_delete", [&material_controller](const HttpRequest& request) {
      return material_controller.AdminBatchDelete(request);
    });

    router.AddRoute("DELETE", "/api/ppt/history", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.Delete(request);
    });
    router.AddRoute("POST", "/api/ppt/batch_delete", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.BatchDelete(request);
    });
    router.AddRoute("GET", "/api/ppt/file", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.Download(request);
    });
    router.AddRoute("HEAD", "/api/ppt/file", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.Download(request);
    });
    router.AddRoute("GET", "/api/ppt/preview", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.Preview(request);
    });
    router.AddRoute("GET", "/api/ppt/structure", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.GetStructure(request);
    });
    router.AddRoute("PUT", "/api/ppt/structure", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.UpdateStructure(request);
    });
    router.AddRoute("POST", "/api/ppt/structure/regenerate", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.RegenerateFromStructure(request);
    });

    router.AddRoute("GET", "/api/templates", [&template_controller](const HttpRequest& request) {
      return template_controller.List(request);
    });
    router.AddRoute("GET", "/api/templates/file", [&template_controller](const HttpRequest& request) {
      return template_controller.Download(request);
    });
    router.AddRoute("GET", "/api/templates/preview", [&template_controller](const HttpRequest& request) {
      return template_controller.Preview(request);
    });

    router.AddRoute("GET", "/api/models", [&model_controller](const HttpRequest& request) {
      return model_controller.List(request);
    });
    router.AddRoute("POST", "/api/ppt/outline", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.Outline(request);
    });
    router.AddRoute("POST", "/api/ppt/batch_download", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.BatchDownload(request);
    });
    router.AddRoute("GET", "/api/ppt/batch_zip", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.BatchDownloadFile(request);
    });

    router.AddRoute("POST", "/api/material/upload", [&material_controller](const HttpRequest& request) {
      return material_controller.Upload(request);
    });
    router.AddRoute("GET", "/api/material/status", [&material_controller](const HttpRequest& request) {
      return material_controller.GetStatus(request);
    });
    router.AddRoute("GET", "/api/material/result", [&material_controller](const HttpRequest& request) {
      return material_controller.GetResult(request);
    });
    router.AddRoute("PUT", "/api/material/result", [&material_controller](const HttpRequest& request) {
      return material_controller.SaveResult(request);
    });
    router.AddRoute("GET", "/api/material/list", [&material_controller](const HttpRequest& request) {
      return material_controller.List(request);
    });
    router.AddRoute("DELETE", "/api/material", [&material_controller](const HttpRequest& request) {
      return material_controller.Delete(request);
    });
    router.AddRoute("POST", "/api/material/batch_delete", [&material_controller](const HttpRequest& request) {
      return material_controller.BatchDelete(request);
    });
    router.AddRoute("POST", "/api/material/batch_upload", [&material_controller](const HttpRequest& request) {
      return material_controller.BatchUpload(request);
    });
    router.AddRoute("GET", "/api/material/batch_status", [&material_controller](const HttpRequest& request) {
      return material_controller.BatchStatus(request);
    });
    router.AddRoute("GET", "/api/material/notices", [&material_controller](const HttpRequest& request) {
      return material_controller.GetDeletionNotices(request);
    });
    router.AddRoute("POST", "/api/material/notices/read", [&material_controller](const HttpRequest& request) {
      return material_controller.MarkNoticesRead(request);
    });

    // ── 公告管理 ──────────────────────────────────────────────────────────────
    // 公开接口（需登录）
    router.AddRoute("GET", "/api/announcements", [&announcement_controller](const HttpRequest& request) {
      return announcement_controller.ListActive(request);
    });
    // 管理员接口
    router.AddRoute("GET", "/api/admin/announcements", [&announcement_controller](const HttpRequest& request) {
      return announcement_controller.AdminList(request);
    });
    router.AddRoute("POST", "/api/admin/announcements", [&announcement_controller](const HttpRequest& request) {
      return announcement_controller.AdminCreate(request);
    });
    router.AddRoute("PUT", "/api/admin/announcements", [&announcement_controller](const HttpRequest& request) {
      return announcement_controller.AdminUpdate(request);
    });
    router.AddRoute("DELETE", "/api/admin/announcements", [&announcement_controller](const HttpRequest& request) {
      return announcement_controller.AdminDelete(request);
    });

    router.AddRoute("POST", "/api/assistant/chat", [&assistant_controller](const HttpRequest& request) {
      return assistant_controller.Chat(request);
    });

    // ── 会话持久化端点 ──────────────────────────────────────────────────────
    // 精确路径：不含 session_id 的操作
    router.AddRoute("POST", "/api/assistant/sessions",
        [&assistant_controller](const HttpRequest& request) {
          return assistant_controller.CreateSession(request);
        });
    router.AddRoute("GET", "/api/assistant/sessions",
        [&assistant_controller](const HttpRequest& request) {
          return assistant_controller.ListSessions(request);
        });

    // 前缀路径：含 session_id 的子路径（路由器按最长前缀匹配）
    // GET  /api/assistant/sessions/{id}/messages
    router.AddPrefixRoute("GET", "/api/assistant/sessions/",
        [&assistant_controller](const HttpRequest& request) {
          // path 以 /messages 结尾
          if (request.path.size() > 9 &&
              request.path.substr(request.path.size() - 9) == "/messages") {
            return assistant_controller.GetMessages(request);
          }
          return HttpResponse::Json(404, ErrorJson("NOT_FOUND", "Route not found"));
        });

    // POST /api/assistant/sessions/{id}/chat
    router.AddPrefixRoute("POST", "/api/assistant/sessions/",
        [&assistant_controller](const HttpRequest& request) {
          // path 以 /chat 结尾
          if (request.path.size() > 5 &&
              request.path.substr(request.path.size() - 5) == "/chat") {
            return assistant_controller.ChatInSession(request);
          }
          return HttpResponse::Json(404, ErrorJson("NOT_FOUND", "Route not found"));
        });

    // DELETE /api/assistant/sessions/{id}
    router.AddPrefixRoute("DELETE", "/api/assistant/sessions/",
        [&assistant_controller](const HttpRequest& request) {
          return assistant_controller.DeleteSession(request);
        });

    HttpServer server(config.server(), router);
    server.Start();

    while (!g_should_stop.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server.Stop();
  } catch (const std::exception& ex) {
    Logger::Error(std::string("后台服务启动失败: ") + ex.what());
    return 1;
  }

  Logger::Info("Server stopped. Bye!");
  return 0;
}
