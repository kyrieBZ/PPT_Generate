#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "http/http_types.h"
#include "utils/string_utils.h"

class Router {
 public:
  using Handler = std::function<HttpResponse(const HttpRequest&)>;

  /**
   * 全局中间件：在所有路由处理器之前调用。
   * 返回 nullopt 表示放行，返回 HttpResponse 表示短路（直接返回该响应）。
   * 用于 maintenance_mode 等全局拦截逻辑。
   */
  using Middleware = std::function<std::optional<HttpResponse>(const HttpRequest&)>;
  void SetGlobalMiddleware(Middleware mw);

  /** 精确路径注册 */
  void AddRoute(const std::string& method, const std::string& path, Handler handler);

  /**
   * 前缀路径注册：method + path 精确匹配失败时，
   * 按注册顺序匹配前缀最长的条目。
   * 用于处理 /api/foo/{id}/bar 形式的动态路径。
   */
  void AddPrefixRoute(const std::string& method,
                      const std::string& prefix,
                      Handler handler);

  HttpResponse Handle(const HttpRequest& request) const;

 private:
  std::string BuildKey(const std::string& method, const std::string& path) const;

  std::unordered_map<std::string, Handler> routes_;

  struct PrefixRoute {
    std::string key_prefix;  // lower(method) + ":" + prefix
    Handler handler;
  };
  std::vector<PrefixRoute> prefix_routes_;

  Middleware global_middleware_;
};
