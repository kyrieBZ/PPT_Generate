#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "utils/string_utils.h"

namespace detail {
inline std::string ReasonPhrase(int status) {
  switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 206: return "Partial Content";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 409: return "Conflict";
    case 416: return "Range Not Satisfiable";
    case 422: return "Unprocessable Entity";
    case 500: return "Internal Server Error";
    default: return "OK";
  }
}
}

struct HttpRequest {
  std::string method;
  std::string target;
  std::string path;
  std::unordered_map<std::string, std::string> headers;
  std::unordered_map<std::string, std::string> query_params;
  std::string body;

  std::string Header(const std::string& key) const {
    auto lower_key = string_utils::ToLower(key);
    if (auto it = headers.find(lower_key); it != headers.end()) {
      return it->second;
    }
    return {};
  }
};

struct HttpResponse {
  int status_code = 200;
  std::string status_message = "OK";
  std::unordered_map<std::string, std::string> headers{
      {"content-type", "application/json"}};
  std::string body = "{}";

  static HttpResponse Json(int status, const nlohmann::json& payload) {
    HttpResponse response;
    response.status_code = status;
    response.status_message = detail::ReasonPhrase(status);
    response.body = payload.dump();
    return response;
  }

  static HttpResponse Text(int status, const std::string& message) {
    HttpResponse response;
    response.status_code = status;
    response.status_message = detail::ReasonPhrase(status);
    response.headers["content-type"] = "text/plain; charset=utf-8";
    response.body = message;
    return response;
  }

  void ApplyCors() {
    headers["access-control-allow-origin"] = "*";
    headers["access-control-allow-headers"] = "Content-Type, Authorization, ngrok-skip-browser-warning";
    headers["access-control-allow-methods"] = "GET, POST, DELETE, OPTIONS, HEAD";
  }
};
