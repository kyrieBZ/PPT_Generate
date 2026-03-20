#pragma once

#include <memory>

#include "http/http_types.h"
#include "services/ai_search_service.h"
#include "services/auth_service.h"
#include "utils/thread_pool.h"

class AiSearchController {
 public:
  AiSearchController(std::shared_ptr<AuthService> auth_service,
                     std::shared_ptr<AiSearchService> ai_search_service,
                     std::shared_ptr<ThreadPool> thread_pool);

  // POST /api/ppt/ai_search
  // Body: { "query": "...", "top_k": 10, "enable_rerank": true }
  HttpResponse Search(const HttpRequest& request);

  // POST /api/admin/ppt/reindex  (admin only)
  // Triggers full reindex of all completed PPTs in background
  HttpResponse AdminReindex(const HttpRequest& request);

  // GET /api/admin/ppt/index_status  (admin only)
  HttpResponse AdminIndexStatus(const HttpRequest& request);

 private:
  std::shared_ptr<AuthService> auth_service_;
  std::shared_ptr<AiSearchService> ai_search_service_;
  std::shared_ptr<ThreadPool> thread_pool_;
};
