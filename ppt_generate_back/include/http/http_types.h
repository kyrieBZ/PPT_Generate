#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "utils/string_utils.h"

/** 统一错误响应体：{ "code", "message", "data": null } */
inline nlohmann::json ErrorJson(const std::string& code, const std::string& message) {
  return nlohmann::json{{"code", code}, {"message", message}, {"data", nullptr}};
}

/** 500 错误对用户展示的固定文案，不暴露内部信息 */
constexpr const char* kInternalErrorMessage = "服务暂时不可用，请稍后重试";

namespace detail {
inline std::string ReasonPhrase(int status) {
  switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
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
    // Chrome Private Network Access: allow requests from public pages (e.g. ngrok) to localhost
    headers["access-control-allow-private-network"] = "true";
  }
};

/**
 * SSE (Server-Sent Events) 流式响应描述符。
 * HttpServer 检测到路由返回此类型时，保持 TCP 连接打开，
 * 将 auth_headers 和 CORS 头发送后调用 stream_fn，
 * stream_fn 通过 write_fn 向客户端逐块写入 SSE 事件。
 *
 * SSE 事件格式：
 *   data: <json>\n\n
 *
 * stream_fn 应在生成完毕后返回（连接随后关闭）。
 */
struct SseResponse {
  int status_code = 200;
  // stream_fn 通过此回调写入原始字节；返回 false 表示客户端已断开
  using WriteFn = std::function<bool(const std::string&)>;
  // 实际流逻辑：blocking，直到流结束
  std::function<void(WriteFn)> stream_fn;

  /** 构造辅助：将 JSON 对象格式化为单条 SSE data 行 */
  static std::string MakeEvent(const nlohmann::json& payload,
                               const std::string& event_name = "") {
    std::string out;
    if (!event_name.empty()) out += "event: " + event_name + "\n";
    out += "data: " + payload.dump() + "\n\n";
    return out;
  }
};
