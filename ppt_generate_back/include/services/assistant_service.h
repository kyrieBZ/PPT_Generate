#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "database/mongo_client.h"

struct AssistantAction {
  std::string intent;      // DELETE_PPT / GENERATE_PPT / NAVIGATE / DOWNLOAD_PPT / LIST_TEMPLATES / UNKNOWN
  std::string ppt_id;
  std::string ppt_title;
  std::string topic;
  int page_count = 0;
  std::string style;
  std::string page;        // 用于 NAVIGATE intent
  std::string confirm_text;
};

struct AssistantResponse {
  std::string reply;
  bool has_action = false;
  AssistantAction action;
};

struct ChatMessage {
  std::string role;         // "user" or "assistant"
  std::string content;
  std::string timestamp;   // ISO 8601 字符串（持久化时填充）
};

struct ChatSession {
  std::string session_id;
  std::uint64_t user_id = 0;
  std::string title;
  std::string created_at;
  std::string updated_at;
};

class AssistantService {
 public:
  AssistantService(std::string api_key,
                   std::uint32_t timeout_seconds = 30,
                   std::shared_ptr<MongoClient> mongo = nullptr);

  bool IsEnabled() const { return !api_key_.empty(); }
  bool IsPersistenceEnabled() const {
    return mongo_ != nullptr && mongo_->IsConnected();
  }

  // ─── 原有：无状态对话（向后兼容）─────────────────────────────────────────
  /**
   * 处理用户消息，调用 LLM 解析意图并返回结构化响应。
   * history 由调用方传入（无状态模式）。
   */
  bool Chat(const std::string& message,
            const std::string& context_json,
            const std::vector<ChatMessage>& history,
            AssistantResponse& out_response,
            std::string& error_message) const;

  // ─── 新增：会话管理（持久化模式）────────────────────────────────────────

  /**
   * 创建新会话。
   * @param user_id       用户 ID（来自 MySQL users.id）
   * @param out_session   成功时填充会话信息
   * @return 成功返回 true
   */
  bool CreateSession(std::uint64_t user_id,
                     ChatSession& out_session,
                     std::string& error_message);

  /**
   * 查询用户的会话列表（按 updated_at 倒序）。
   * @param user_id  用户 ID
   * @param limit    最大返回数量，默认 20
   */
  std::vector<ChatSession> ListSessions(std::uint64_t user_id,
                                        int limit = 20,
                                        std::string& error_message = dummy_error_);

  /**
   * 删除会话及其所有消息。
   */
  bool DeleteSession(const std::string& session_id,
                     std::uint64_t user_id,
                     std::string& error_message);

  /**
   * 查询某会话的历史消息（按时间升序）。
   * @param limit  最大条数，默认 50
   */
  std::vector<ChatMessage> GetMessages(const std::string& session_id,
                                       std::uint64_t user_id,
                                       int limit = 50,
                                       std::string& error_message = dummy_error_);

  /**
   * 在会话内发送消息：保存用户消息 → 读取历史 → 调 LLM → 保存 AI 回复。
   * @param session_id    目标会话 ID
   * @param user_id       用户 ID（用于归属校验）
   * @param user_message  用户消息文本
   * @param context_json  前端上下文（recent_ppts 等），可为空字符串
   * @param out_response  LLM 结构化响应（含 reply 与可能的 action）
   * @return 成功返回 true
   */
  bool ChatInSession(const std::string& session_id,
                     std::uint64_t user_id,
                     const std::string& user_message,
                     const std::string& context_json,
                     AssistantResponse& out_response,
                     std::string& error_message);

 private:
  std::string BuildSystemPrompt() const;
  std::string BuildUserPrompt(const std::string& message,
                              const std::string& context_json) const;
  bool ParseLLMResponse(const std::string& llm_text,
                        AssistantResponse& out_response) const;
  std::string CallQwenAPI(const std::string& system_prompt,
                          const std::string& user_prompt,
                          const std::vector<ChatMessage>& history,
                          std::string& error_message) const;

  /** 验证 session 归属，返回 false 时 error_message 已填充 */
  bool ValidateSession(const std::string& session_id,
                       std::uint64_t user_id,
                       std::string& error_message);

  /** 保存一条消息到 MongoDB */
  bool SaveMessage(const std::string& session_id,
                   std::uint64_t user_id,
                   const std::string& role,
                   const std::string& content);

  /** 从 MongoDB 读取最近 N 条消息（时序升序） */
  std::vector<ChatMessage> LoadRecentMessages(const std::string& session_id,
                                              int n = 10);

  /** 更新会话的 updated_at 与 title（若 title 为空则用首条消息截取） */
  void TouchSession(const std::string& session_id,
                    const std::string& first_message = "");

  std::string api_key_;
  std::uint32_t timeout_seconds_;
  std::shared_ptr<MongoClient> mongo_;

  // 供默认参数使用的静态哑变量
  static std::string dummy_error_;
};
