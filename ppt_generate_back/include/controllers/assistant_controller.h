#pragma once

#include <memory>

#include "http/http_types.h"
#include "services/assistant_service.h"
#include "services/auth_service.h"

class AssistantController {
 public:
  AssistantController(std::shared_ptr<AuthService> auth_service,
                      std::shared_ptr<AssistantService> assistant_service);

  // ── 原有：无状态对话（向后兼容）────────────────────────────────────────────
  HttpResponse Chat(const HttpRequest& request);

  // ── 新增：会话管理端点 ──────────────────────────────────────────────────────

  /** POST /api/assistant/sessions — 创建新会话 */
  HttpResponse CreateSession(const HttpRequest& request);

  /** GET  /api/assistant/sessions — 当前用户的会话列表 */
  HttpResponse ListSessions(const HttpRequest& request);

  /**
   * GET  /api/assistant/sessions/{session_id}/messages
   *      — 查询某会话历史消息（?limit=50）
   */
  HttpResponse GetMessages(const HttpRequest& request);

  /**
   * POST /api/assistant/sessions/{session_id}/chat
   *      — 在会话内发送消息（持久化版本）
   */
  HttpResponse ChatInSession(const HttpRequest& request);

  /** DELETE /api/assistant/sessions/{session_id} — 删除会话及消息 */
  HttpResponse DeleteSession(const HttpRequest& request);

 private:
  /** 从 Authorization: Bearer <token> 中提取 token，失败时返回空字符串 */
  static std::string ExtractToken(const HttpRequest& request);

  std::shared_ptr<AuthService> auth_service_;
  std::shared_ptr<AssistantService> assistant_service_;
};
