#include <atomic>
#include <csignal>
#include <chrono>
#include <iostream>
#include <thread>

#include "app_config.h"
#include "controllers/auth_controller.h"
#include "controllers/ppt_controller.h"
#include "controllers/template_controller.h"
#include "controllers/model_controller.h"
#include "database/mysql_connection_pool.h"
#include "http/http_server.h"
#include "logger.h"
#include "services/auth_service.h"
#include "services/ppt_service.h"
#include "services/qwen_client.h"
#include "services/template_service.h"
#include "services/ppt_service_interface.h"
// 不再需要单独包含SlideContent定义，因为它在qwen_client.h中

// Uncomment the desired implementation
// #include "services/aspose_powerpoint_service.h"
// #include "services/libreoffice_powerpoint_service.h"

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
    auto auth_service = std::make_shared<AuthService>(pool, config.auth());
    auto ppt_service = std::make_shared<PptService>(pool);
    
    // Conditionally set PowerPoint service factory based on availability
    // For now, we'll leave this commented until the actual implementation is ready
    /*
    std::shared_ptr<IPowerPointServiceFactory> factory;
    
    // Try to use Aspose first, fallback to LibreOffice, or none if neither available
    #ifdef USE_ASPOSE_SLIDES
    factory = std::make_shared<AsposePowerPointServiceFactory>();
    #elif USE_LIBREOFFICE_SDK
    factory = std::make_shared<LibreOfficePowerPointServiceFactory>();
    #endif
    
    if (factory) {
        ppt_service->SetPowerPointServiceFactory(factory);
    }
    */

    auto template_service = std::make_shared<TemplateService>(config.templates().catalog_path);
    auto model_service = std::make_shared<ModelService>(config.models().catalog_path);
    std::shared_ptr<QwenClient> qwen_client;
    if (!config.providers().qwen_api_key.empty()) {
      qwen_client = std::make_shared<QwenClient>(config.providers().qwen_api_key);
    }

    Router router;
    AuthController auth_controller(auth_service);
    PptController ppt_controller(auth_service, ppt_service, model_service, template_service, qwen_client);
    TemplateController template_controller(template_service);
    ModelController model_controller(model_service);

    router.AddRoute("GET", "/api/health", [](const HttpRequest&) {
      return HttpResponse::Json(200, {{"status", "ok"}});
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

    router.AddRoute("GET", "/api/auth/user", [&auth_controller](const HttpRequest& request) {
      return auth_controller.CurrentUser(request);
    });

    router.AddRoute("POST", "/api/ppt/generate", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.Generate(request);
    });

    router.AddRoute("GET", "/api/ppt/history", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.History(request);
    });

    router.AddRoute("DELETE", "/api/ppt/history", [&ppt_controller](const HttpRequest& request) {
      return ppt_controller.Delete(request);
    });

    router.AddRoute("GET", "/api/templates", [&template_controller](const HttpRequest& request) {
      return template_controller.List(request);
    });
    router.AddRoute("GET", "/api/templates/file", [&template_controller](const HttpRequest& request) {
      return template_controller.Download(request);
    });

    router.AddRoute("GET", "/api/models", [&model_controller](const HttpRequest& request) {
      return model_controller.List(request);
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