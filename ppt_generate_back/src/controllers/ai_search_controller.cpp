#include "controllers/ai_search_controller.h"

#include <atomic>

#include <nlohmann/json.hpp>

#include "http/http_types.h"
#include "logger.h"

namespace {

std::string ExtractToken(const HttpRequest& request) {
  auto header = request.Header("authorization");
  if (header.rfind("Bearer ", 0) == 0 || header.rfind("bearer ", 0) == 0) {
    return header.substr(7);
  }
  if (!header.empty()) return header;
  if (auto it = request.query_params.find("token"); it != request.query_params.end()) {
    return it->second;
  }
  return {};
}

// Reindex job state (very lightweight – just a global counter for progress reporting)
std::atomic<int> g_reindex_total{0};
std::atomic<int> g_reindex_done{0};
std::atomic<bool> g_reindex_running{false};

}  // namespace

AiSearchController::AiSearchController(
    std::shared_ptr<AuthService> auth_service,
    std::shared_ptr<AiSearchService> ai_search_service,
    std::shared_ptr<ThreadPool> thread_pool)
    : auth_service_(std::move(auth_service)),
      ai_search_service_(std::move(ai_search_service)),
      thread_pool_(std::move(thread_pool)) {}

HttpResponse AiSearchController::Search(const HttpRequest& request) {
  const std::string token = ExtractToken(request);
  if (token.empty()) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", "未登录"));
  }
  std::string auth_err;
  const auto user_opt = auth_service_->GetUserFromToken(token, auth_err);
  if (!user_opt) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED",
                                             auth_err.empty() ? "Token 无效" : auth_err));
  }

  std::string query;
  int top_k = 10;
  bool enable_rerank = true;

  try {
    if (!request.body.empty()) {
      const auto body = nlohmann::json::parse(request.body);
      query        = body.value("query", "");
      top_k        = body.value("top_k", 10);
      enable_rerank = body.value("enable_rerank", true);
    }
  } catch (const std::exception& ex) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST",
                                             std::string("请求解析失败: ") + ex.what()));
  }

  if (query.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "搜索词不能为空"));
  }
  if (top_k < 1 || top_k > 50) top_k = 10;

  const auto response = ai_search_service_->Search(query, user_opt->id, top_k, enable_rerank);

  nlohmann::json results_arr = nlohmann::json::array();
  for (const auto& item : response.results) {
    nlohmann::json obj = {
      {"ppt_id",        item.ppt_id},
      {"title",         item.title},
      {"topic",         item.topic},
      {"template_name", item.template_name},
      {"pages",         item.pages},
      {"created_at",    item.created_at},
      {"status",        item.status},
      {"score",         item.score},
      {"reason",        item.reason},
      {"preview_url",   "/api/ppt/preview?id=" + std::to_string(item.ppt_id)},
      {"download_url",  "/api/ppt/file?id=" + std::to_string(item.ppt_id)},
    };
    results_arr.push_back(std::move(obj));
  }

  nlohmann::json out = {
    {"results",  results_arr},
    {"query",    query},
    {"total",    static_cast<int>(response.results.size())},
    {"fallback", response.fallback}
  };
  return HttpResponse::Json(200, out);
}

HttpResponse AiSearchController::AdminReindex(const HttpRequest& request) {
  const std::string token = ExtractToken(request);
  if (token.empty()) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", "未登录"));
  }
  std::string auth_err;
  const auto user_opt = auth_service_->GetUserFromToken(token, auth_err);
  if (!user_opt || !auth_service_->IsAdmin(*user_opt)) {
    return HttpResponse::Json(403, ErrorJson("ERR_FORBIDDEN", "需要管理员权限"));
  }

  if (g_reindex_running.load()) {
    return HttpResponse::Json(409, ErrorJson("ERR_CONFLICT", "重建索引任务正在运行中，请稍后再试"));
  }

  g_reindex_running.store(true);
  g_reindex_done.store(0);
  g_reindex_total.store(0);

  auto ai_svc = ai_search_service_;
  thread_pool_->Submit([ai_svc]() {
    Logger::Info("AiSearch: admin reindex started");
    std::string err;
    const int count = ai_svc->ReindexAll(err);
    g_reindex_done.store(count);
    g_reindex_running.store(false);
    if (!err.empty()) {
      Logger::Warn("AiSearch: reindex error: " + err);
    }
    Logger::Info("AiSearch: admin reindex done, indexed=" + std::to_string(count));
  });

  return HttpResponse::Json(202, {
    {"message", "重建索引任务已在后台启动"},
    {"status",  "running"}
  });
}

HttpResponse AiSearchController::AdminIndexStatus(const HttpRequest& request) {
  const std::string token = ExtractToken(request);
  if (token.empty()) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", "未登录"));
  }
  std::string auth_err;
  const auto user_opt = auth_service_->GetUserFromToken(token, auth_err);
  if (!user_opt || !auth_service_->IsAdmin(*user_opt)) {
    return HttpResponse::Json(403, ErrorJson("ERR_FORBIDDEN", "需要管理员权限"));
  }

  const bool running = g_reindex_running.load();
  const int done     = g_reindex_done.load();

  nlohmann::json out = {
    {"running",          running},
    {"indexed_count",    done},
    {"vector_available", ai_search_service_->IsVectorSearchAvailable()}
  };
  return HttpResponse::Json(200, out);
}
