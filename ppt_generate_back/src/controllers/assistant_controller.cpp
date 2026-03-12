#include "controllers/assistant_controller.h"

#include <nlohmann/json.hpp>

#include "logger.h"

AssistantController::AssistantController(
    std::shared_ptr<AuthService> auth_service,
    std::shared_ptr<AssistantService> assistant_service)
    : auth_service_(std::move(auth_service)),
      assistant_service_(std::move(assistant_service)) {}

HttpResponse AssistantController::Chat(const HttpRequest& request) {
  HttpResponse response;
  response.ApplyCors();

  // 鉴权
  const std::string auth_header = request.Header("authorization");
  std::string token;
  if (auth_header.size() > 7 && auth_header.substr(0, 7) == "Bearer ") {
    token = auth_header.substr(7);
  }
  if (token.empty()) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "请先登录"));
  }
  std::string auth_error;
  const auto user_opt = auth_service_->GetUserFromToken(token, auth_error);
  if (!user_opt.has_value()) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "登录已过期，请重新登录"));
  }

  // 解析请求体
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(request.body);
  } catch (...) {
    return HttpResponse::Json(400, ErrorJson("BAD_REQUEST", "请求体格式错误"));
  }

  std::string message;
  if (body.contains("message") && body["message"].is_string()) {
    message = body["message"].get<std::string>();
  }
  if (message.empty()) {
    return HttpResponse::Json(400, ErrorJson("BAD_REQUEST", "message 不能为空"));
  }

  // 提取上下文
  std::string context_json;
  if (body.contains("context") && body["context"].is_object()) {
    try {
      context_json = body["context"].dump();
    } catch (...) {
      context_json = "{}";
    }
  }

  // 提取对话历史
  std::vector<ChatMessage> history;
  if (body.contains("context") && body["context"].is_object()) {
    const auto& ctx = body["context"];
    if (ctx.contains("history") && ctx["history"].is_array()) {
      for (const auto& item : ctx["history"]) {
        if (item.is_object() &&
            item.contains("role") && item["role"].is_string() &&
            item.contains("content") && item["content"].is_string()) {
          ChatMessage msg;
          msg.role    = item["role"].get<std::string>();
          msg.content = item["content"].get<std::string>();
          history.push_back(std::move(msg));
        }
      }
    }
  }

  // 调用 AssistantService
  AssistantResponse assistant_response;
  std::string service_error;
  const bool ok = assistant_service_->Chat(
      message, context_json, history, assistant_response, service_error);

  if (!ok) {
    Logger::Error("AssistantController::Chat 失败: " + service_error);
    return HttpResponse::Json(500, ErrorJson("INTERNAL_ERROR", kInternalErrorMessage));
  }

  // 构造响应
  nlohmann::json result;
  result["reply"] = assistant_response.reply;

  if (assistant_response.has_action) {
    const auto& act = assistant_response.action;
    nlohmann::json action_json;
    action_json["intent"]       = act.intent;
    action_json["confirm_text"] = act.confirm_text;

    nlohmann::json params;
    if (!act.ppt_id.empty())    params["ppt_id"]     = act.ppt_id;
    if (!act.ppt_title.empty()) params["ppt_title"]  = act.ppt_title;
    if (!act.topic.empty())     params["topic"]      = act.topic;
    if (act.page_count > 0)     params["page_count"] = act.page_count;
    if (!act.style.empty())     params["style"]      = act.style;
    if (!act.page.empty())      params["page"]       = act.page;

    action_json["params"] = params;
    result["action"] = action_json;
  } else {
    result["action"] = nullptr;
  }

  return HttpResponse::Json(200, result);
}
