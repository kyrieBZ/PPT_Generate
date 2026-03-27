#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/mongo_client.h"
#include "database/mysql_connection_pool.h"

// 前向声明，避免循环依赖
class PptService;
class MaterialService;
class TemplateManagerService;
class TemplateService;
class AiSearchService;

// ── 兼容旧版意图结构（保留供无状态 Chat 降级使用）────────────────────────────
struct AssistantAction {
  std::string intent;      // 客户端工具名（NAVIGATE / GENERATE_PPT 等）
  std::string ppt_id;
  std::string ppt_title;
  std::string topic;
  int page_count = 0;
  std::string style;
  std::string page;
  std::string confirm_text;
};

// ── 工具结果摘要（供前端卡片展示）────────────────────────────────────────────
struct ToolResultCard {
  std::string card_type;    // "ppt_list" / "material_list" / "template_list" / "text"
  nlohmann::json data;      // 卡片主数据（列表）
  nlohmann::json meta;      // 顶层元数据（total/page/is_admin_view 等）
};

// ── 前端需执行的客户端工具条目 ────────────────────────────────────────────────
struct ClientToolCall {
  std::string tool_name;        // navigate_to_page / trigger_generate_ppt 等
  nlohmann::json params;
  bool confirm_required = false;
  bool require_confirm_code = false;  // 最高级危险操作：需用户输入 "CONFIRM" 字符串
  std::string confirm_text;
};

// ── AI 助手响应（扩展版）─────────────────────────────────────────────────────
struct AssistantResponse {
  std::string reply;

  // ── 新版 Tool Call 架构字段 ──────────────────────────────────────
  std::vector<ClientToolCall> pending_client_tools;  // 待前端执行的工具
  std::vector<ToolResultCard> tool_results_summary;  // 已执行工具的摘要卡片
  bool requires_confirm = false;                     // 是否有需要确认的操作

  // ── 旧版兼容字段（无状态 Chat 降级时使用）──────────────────────
  bool has_action = false;
  AssistantAction action;
};

struct ChatMessage {
  std::string role;         // "user" or "assistant" or "tool"
  std::string content;
  std::string timestamp;   // ISO 8601 字符串（持久化时填充）
  std::string tool_call_id; // tool 角色时使用
  std::string name;         // tool 角色时使用（工具名）
  nlohmann::json tool_cards = nlohmann::json::array(); // assistant 消息附带的工具卡片
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
  /**
   * @param api_key          Qwen API Key
   * @param timeout_seconds  HTTP 超时
   * @param mongo            MongoDB 客户端（可选，用于会话持久化）
   * @param pool             MySQL 连接池（可选，用于工具执行查询数据库）
   * @param ppt_service      PPT 服务（可选，用于 delete_ppt 等工具）
   * @param material_service 素材服务（可选，用于 list_materials / delete_material 等工具）
   * @param tmpl_mgr_service 模板管理服务（可选，用于 list_templates 上架过滤）
   * @param template_service 模板 catalog 服务（可选，用于获取模板详情）
   * @param ai_search_service AI 向量检索服务（可选，用于 search_ppt_history 工具）
   */
  AssistantService(std::string api_key,
                   std::uint32_t timeout_seconds = 30,
                   std::shared_ptr<MongoClient> mongo = nullptr,
                   std::shared_ptr<MySQLConnectionPool> pool = nullptr,
                   std::shared_ptr<PptService> ppt_service = nullptr,
                   std::shared_ptr<MaterialService> material_service = nullptr,
                   std::shared_ptr<TemplateManagerService> tmpl_mgr_service = nullptr,
                   std::shared_ptr<TemplateService> template_service = nullptr,
                   std::shared_ptr<AiSearchService> ai_search_service = nullptr);

  bool IsEnabled() const { return !api_key_.empty(); }
  bool IsPersistenceEnabled() const {
    return mongo_ != nullptr && mongo_->IsConnected();
  }

  // ─── 原有：无状态对话（向后兼容）─────────────────────────────────────────
  bool Chat(const std::string& message,
            const std::string& context_json,
            const std::vector<ChatMessage>& history,
            AssistantResponse& out_response,
            std::string& error_message) const;

  // ─── 新增：会话管理（持久化模式）────────────────────────────────────────

  bool CreateSession(std::uint64_t user_id,
                     ChatSession& out_session,
                     std::string& error_message);

  std::vector<ChatSession> ListSessions(std::uint64_t user_id,
                                        int limit = 20,
                                        std::string& error_message = dummy_error_);

  bool DeleteSession(const std::string& session_id,
                     std::uint64_t user_id,
                     std::string& error_message);

  std::vector<ChatMessage> GetMessages(const std::string& session_id,
                                       std::uint64_t user_id,
                                       int limit = 50,
                                       std::string& error_message = dummy_error_);

  /**
   * 在会话内发送消息（Tool Call 架构版）：
   * 保存用户消息 → 读取历史 → Tool Call 循环（最多 3 轮）→ 保存 AI 回复。
   *
   * @param session_id    目标会话 ID
   * @param user_id       用户 ID（用于归属校验）
   * @param is_admin      是否管理员（控制可用工具集）
   * @param user_message  用户消息文本
   * @param context_json  前端上下文（recent_ppts 等），可为空字符串
   * @param out_response  LLM 结构化响应
   * @return 成功返回 true
   */
  bool ChatInSession(const std::string& session_id,
                     std::uint64_t user_id,
                     bool is_admin,
                     const std::string& user_message,
                     const std::string& context_json,
                     AssistantResponse& out_response,
                     std::string& error_message);

 private:
  // ── System Prompt ─────────────────────────────────────────────────────────
  std::string BuildSystemPrompt(bool is_admin = false) const;
  std::string BuildUserPrompt(const std::string& message,
                              const std::string& context_json) const;

  // ── Tool Call 架构核心 ────────────────────────────────────────────────────

  /** 构建工具描述列表（JSON array，符合 Qwen Function Calling 格式） */
  nlohmann::json BuildTools(bool is_admin) const;

  /**
   * 执行服务端工具（在 AssistantService 内部直接调用业务服务层）。
   * @param tool_name  工具名
   * @param params     工具参数（JSON object）
   * @param user_id    当前用户 ID（用于权限控制）
   * @param is_admin   是否管理员
   * @return           工具执行结果（JSON，包含 card_type 和 data 字段）
   */
  nlohmann::json ExecuteServerTool(const std::string& tool_name,
                                    const nlohmann::json& params,
                                    std::uint64_t user_id,
                                    bool is_admin) const;

  /** 判断工具是否为客户端工具（需透传给前端执行） */
  static bool IsClientTool(const std::string& tool_name);

  /** 判断工具是否需要二次确认 */
  static bool ToolRequiresConfirm(const std::string& tool_name);

  /** 生成工具的确认文本（危险操作提示） */
  static std::string BuildConfirmText(const std::string& tool_name,
                                       const nlohmann::json& params);

  // ── Qwen API ──────────────────────────────────────────────────────────────

  /**
   * 调用 Qwen LLM（支持 Function Calling）。
   * @param messages   完整消息列表（含 system/user/assistant/tool 角色）
   * @param tools      工具描述列表（空则不传 tools 参数）
   * @param out_text   LLM 纯文本回复（无 tool_calls 时填充）
   * @param out_tool_calls  LLM 发起的工具调用（有时填充）
   * @return true 表示成功（out_text 或 out_tool_calls 二者之一有值）
   */
  bool CallQwenAPIWithTools(const nlohmann::json& messages,
                             const nlohmann::json& tools,
                             std::string& out_text,
                             nlohmann::json& out_tool_calls,
                             std::string& error_message) const;

  /** 原有 Qwen 调用（无 tools，向后兼容） */
  std::string CallQwenAPI(const std::string& system_prompt,
                          const std::string& user_prompt,
                          const std::vector<ChatMessage>& history,
                          std::string& error_message) const;

  // ── 旧版响应解析（向后兼容）──────────────────────────────────────────────
  bool ParseLLMResponse(const std::string& llm_text,
                        AssistantResponse& out_response) const;

  // ── MongoDB 工具方法 ──────────────────────────────────────────────────────
  bool ValidateSession(const std::string& session_id,
                       std::uint64_t user_id,
                       std::string& error_message);

  bool SaveMessage(const std::string& session_id,
                   std::uint64_t user_id,
                   const std::string& role,
                   const std::string& content,
                   const nlohmann::json& tool_cards = nlohmann::json::array());

  std::vector<ChatMessage> LoadRecentMessages(const std::string& session_id,
                                              int n = 10);

  void TouchSession(const std::string& session_id,
                    const std::string& first_message = "");

  // ── 成员变量 ──────────────────────────────────────────────────────────────
  std::string api_key_;
  std::uint32_t timeout_seconds_;
  std::shared_ptr<MongoClient> mongo_;
  std::shared_ptr<MySQLConnectionPool> pool_;
  std::shared_ptr<PptService> ppt_service_;
  std::shared_ptr<MaterialService> material_service_;
  std::shared_ptr<TemplateManagerService> tmpl_mgr_service_;
  std::shared_ptr<TemplateService> template_service_;
  std::shared_ptr<AiSearchService> ai_search_service_;

  static std::string dummy_error_;
};
