#include "controllers/assistant_controller.h"

#include <nlohmann/json.hpp>

#include "logger.h"

namespace {

// 从 URL path 中提取 session_id
// 约定路径格式：/api/assistant/sessions/{session_id}/...
// 返回 session_id 段，如果路径格式不符则返回空字符串
std::string ExtractSessionId(const std::string& path) {
  // /api/assistant/sessions/
  constexpr std::string_view kPrefix = "/api/assistant/sessions/";
  if (path.size() <= kPrefix.size()) return {};
  const std::string after = path.substr(kPrefix.size());
  // session_id 是第一个 '/' 之前的部分
  const auto slash = after.find('/');
  return slash == std::string::npos ? after : after.substr(0, slash);
}

}  // namespace

AssistantController::AssistantController(
    std::shared_ptr<AuthService> auth_service,
    std::shared_ptr<AssistantService> assistant_service)
    : auth_service_(std::move(auth_service)),
      assistant_service_(std::move(assistant_service)) {}

// ── 私有：token 提取 ──────────────────────────────────────────────────────────
std::string AssistantController::ExtractToken(const HttpRequest& request) {
  const std::string auth_header = request.Header("authorization");
  if (auth_header.size() > 7 && auth_header.substr(0, 7) == "Bearer ") {
    return auth_header.substr(7);
  }
  return {};
}

// ── 原有：无状态对话（向后兼容） ─────────────────────────────────────────────
HttpResponse AssistantController::Chat(const HttpRequest& request) {
  HttpResponse response;
  response.ApplyCors();

  const std::string token = ExtractToken(request);
  if (token.empty()) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "请先登录"));
  }
  std::string auth_error;
  const auto user_opt = auth_service_->GetUserFromToken(token, auth_error);
  if (!user_opt.has_value()) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "登录已过期，请重新登录"));
  }

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

  std::string context_json;
  if (body.contains("context") && body["context"].is_object()) {
    try { context_json = body["context"].dump(); } catch (...) { context_json = "{}"; }
  }

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

  AssistantResponse assistant_response;
  std::string service_error;
  const bool ok = assistant_service_->Chat(
      message, context_json, history, assistant_response, service_error);

  if (!ok) {
    Logger::Error("AssistantController::Chat 失败: " + service_error);
    return HttpResponse::Json(500, ErrorJson("INTERNAL_ERROR", kInternalErrorMessage));
  }

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

// ── POST /api/assistant/sessions ─────────────────────────────────────────────
HttpResponse AssistantController::CreateSession(const HttpRequest& request) {
  HttpResponse response;
  response.ApplyCors();

  const std::string token = ExtractToken(request);
  if (token.empty()) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "请先登录"));
  }
  std::string auth_error;
  const auto user_opt = auth_service_->GetUserFromToken(token, auth_error);
  if (!user_opt.has_value()) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "登录已过期，请重新登录"));
  }

  ChatSession session;
  std::string svc_error;
  if (!assistant_service_->CreateSession(user_opt->id, session, svc_error)) {
    Logger::Error("CreateSession failed: " + svc_error);
    return HttpResponse::Json(503, ErrorJson("SERVICE_UNAVAILABLE", svc_error));
  }

  nlohmann::json result = {
      {"session_id", session.session_id},
      {"title",      session.title},
      {"created_at", session.created_at},
      {"updated_at", session.updated_at}
  };
  return HttpResponse::Json(201, result);
}

// ── GET /api/assistant/sessions ───────────────────────────────────────────────
HttpResponse AssistantController::ListSessions(const HttpRequest& request) {
  HttpResponse response;
  response.ApplyCors();

  const std::string token = ExtractToken(request);
  if (token.empty()) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "请先登录"));
  }
  std::string auth_error;
  const auto user_opt = auth_service_->GetUserFromToken(token, auth_error);
  if (!user_opt.has_value()) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "登录已过期，请重新登录"));
  }

  // ?limit=20
  int limit = 20;
  if (auto it = request.query_params.find("limit"); it != request.query_params.end()) {
    try {
      limit = std::stoi(it->second);
      if (limit <= 0 || limit > 100) limit = 20;
    } catch (...) {}
  }

  std::string svc_error;
  const auto sessions = assistant_service_->ListSessions(user_opt->id, limit, svc_error);

  nlohmann::json list = nlohmann::json::array();
  for (const auto& s : sessions) {
    list.push_back({
        {"session_id", s.session_id},
        {"title",      s.title},
        {"created_at", s.created_at},
        {"updated_at", s.updated_at}
    });
  }
  return HttpResponse::Json(200, {{"sessions", list}});
}

// ── GET /api/assistant/sessions/{session_id}/messages ─────────────────────────
HttpResponse AssistantController::GetMessages(const HttpRequest& request) {
  HttpResponse response;
  response.ApplyCors();

  const std::string token = ExtractToken(request);
  if (token.empty()) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "请先登录"));
  }
  std::string auth_error;
  const auto user_opt = auth_service_->GetUserFromToken(token, auth_error);
  if (!user_opt.has_value()) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "登录已过期，请重新登录"));
  }

  const std::string session_id = ExtractSessionId(request.path);
  if (session_id.empty()) {
    return HttpResponse::Json(400, ErrorJson("BAD_REQUEST", "缺少 session_id"));
  }

  int limit = 50;
  if (auto it = request.query_params.find("limit"); it != request.query_params.end()) {
    try {
      limit = std::stoi(it->second);
      if (limit <= 0 || limit > 200) limit = 50;
    } catch (...) {}
  }

  std::string svc_error;
  const auto messages =
      assistant_service_->GetMessages(session_id, user_opt->id, limit, svc_error);

  if (!svc_error.empty() && messages.empty()) {
    return HttpResponse::Json(404, ErrorJson("NOT_FOUND", svc_error));
  }

  nlohmann::json list = nlohmann::json::array();
  for (const auto& m : messages) {
    nlohmann::json item = {{"role", m.role}, {"content", m.content}};
    if (!m.timestamp.empty()) item["timestamp"] = m.timestamp;
    list.push_back(std::move(item));
  }
  return HttpResponse::Json(200, {{"session_id", session_id}, {"messages", list}});
}

// ── POST /api/assistant/sessions/{session_id}/chat ───────────────────────────
HttpResponse AssistantController::ChatInSession(const HttpRequest& request) {
  HttpResponse response;
  response.ApplyCors();

  const std::string token = ExtractToken(request);
  if (token.empty()) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "请先登录"));
  }
  std::string auth_error;
  const auto user_opt = auth_service_->GetUserFromToken(token, auth_error);
  if (!user_opt.has_value()) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "登录已过期，请重新登录"));
  }

  const std::string session_id = ExtractSessionId(request.path);
  if (session_id.empty()) {
    return HttpResponse::Json(400, ErrorJson("BAD_REQUEST", "缺少 session_id"));
  }

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

  std::string context_json;
  if (body.contains("context") && body["context"].is_object()) {
    try { context_json = body["context"].dump(); } catch (...) { context_json = "{}"; }
  }

  AssistantResponse assistant_response;
  std::string svc_error;
  const bool ok = assistant_service_->ChatInSession(
      session_id, user_opt->id, message, context_json, assistant_response, svc_error);

  if (!ok) {
    Logger::Error("ChatInSession failed: " + svc_error);
    // 会话归属校验失败时返回 403，其他错误返回 500
    if (svc_error.find("不存在") != std::string::npos ||
        svc_error.find("无权") != std::string::npos) {
      return HttpResponse::Json(403, ErrorJson("FORBIDDEN", svc_error));
    }
    return HttpResponse::Json(500, ErrorJson("INTERNAL_ERROR", kInternalErrorMessage));
  }

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

// ── DELETE /api/assistant/sessions/{session_id} ──────────────────────────────
HttpResponse AssistantController::DeleteSession(const HttpRequest& request) {
  HttpResponse response;
  response.ApplyCors();

  const std::string token = ExtractToken(request);
  if (token.empty()) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "请先登录"));
  }
  std::string auth_error;
  const auto user_opt = auth_service_->GetUserFromToken(token, auth_error);
  if (!user_opt.has_value()) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "登录已过期，请重新登录"));
  }

  const std::string session_id = ExtractSessionId(request.path);
  if (session_id.empty()) {
    return HttpResponse::Json(400, ErrorJson("BAD_REQUEST", "缺少 session_id"));
  }

  std::string svc_error;
  if (!assistant_service_->DeleteSession(session_id, user_opt->id, svc_error)) {
    return HttpResponse::Json(403, ErrorJson("FORBIDDEN", svc_error));
  }

  return HttpResponse::Json(200, {{"success", true}});
}
