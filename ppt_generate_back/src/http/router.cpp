#include "http/router.h"

#include <utility>

void Router::AddRoute(const std::string& method, const std::string& path, Handler handler) {
  routes_[BuildKey(method, path)] = std::move(handler);
}

HttpResponse Router::Handle(const HttpRequest& request) const {
  const auto method_lower = string_utils::ToLower(request.method);
  if (method_lower == "options") {
    HttpResponse response;
    response.status_code = 204;
    response.status_message = "No Content";
    response.body.clear();
    response.headers["content-length"] = "0";
    return response;
  }

  const auto key = BuildKey(method_lower, request.path);
  if (auto it = routes_.find(key); it != routes_.end()) {
    return it->second(request);
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
