#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "http/http_types.h"
#include "utils/string_utils.h"

class Router {
 public:
  using Handler = std::function<HttpResponse(const HttpRequest&)>;

  void AddRoute(const std::string& method, const std::string& path, Handler handler);
  HttpResponse Handle(const HttpRequest& request) const;

 private:
  std::string BuildKey(const std::string& method, const std::string& path) const;

  std::unordered_map<std::string, Handler> routes_;
};
