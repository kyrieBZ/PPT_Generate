#include "http/router.h"

#include <algorithm>
#include <utility>

void Router::SetGlobalMiddleware(Middleware mw) {
  global_middleware_ = std::move(mw);
}

void Router::AddRoute(const std::string& method, const std::string& path, Handler handler) {
  routes_[BuildKey(method, path)] = std::move(handler);
}

void Router::AddSseRoute(const std::string& method, const std::string& path, SseHandler handler) {
  sse_routes_[BuildKey(method, path)] = std::move(handler);
}

void Router::AddPrefixRoute(const std::string& method,
                             const std::string& prefix,
                             Handler handler) {
  PrefixRoute pr;
  pr.key_prefix = BuildKey(method, prefix);
  pr.handler    = std::move(handler);
  prefix_routes_.push_back(std::move(pr));
}

RouteResult Router::Handle(const HttpRequest& request) const {
  const auto method_lower = string_utils::ToLower(request.method);
  if (method_lower == "options") {
    HttpResponse response;
    response.status_code = 204;
    response.status_message = "No Content";
    response.body.clear();
    response.headers["content-length"] = "0";
    return response;
  }

  // 全局中间件（如 maintenance_mode 拦截）
  if (global_middleware_) {
    auto intercept = global_middleware_(request);
    if (intercept.has_value()) {
      return std::move(*intercept);
    }
  }

  const auto key = BuildKey(method_lower, request.path);

  // 1. SSE 精确匹配（优先于普通路由）
  if (auto it = sse_routes_.find(key); it != sse_routes_.end()) {
    return it->second(request);
  }

  // 2. 普通精确匹配
  if (auto it = routes_.find(key); it != routes_.end()) {
    return it->second(request);
  }

  // 3. 前缀匹配（选最长前缀）
  const PrefixRoute* best = nullptr;
  for (const auto& pr : prefix_routes_) {
    if (key.size() >= pr.key_prefix.size() &&
        key.compare(0, pr.key_prefix.size(), pr.key_prefix) == 0) {
      if (best == nullptr || pr.key_prefix.size() > best->key_prefix.size()) {
        best = &pr;
      }
    }
  }
  if (best != nullptr) {
    return best->handler(request);
  }

  nlohmann::json payload{{"message", "Route not found"}};
  HttpResponse response;
  response.status_code = 404;
  response.status_message = "Not Found";
  response.body = payload.dump();
  return response;
}

std::string Router::BuildKey(const std::string& method, const std::string& path) const {
  return string_utils::ToLower(method) + ":" + path;
}
