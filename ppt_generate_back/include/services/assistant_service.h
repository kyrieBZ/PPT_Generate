#pragma once

#include <string>
#include <vector>

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
  std::string role;    // "user" or "assistant"
  std::string content;
};

class AssistantService {
 public:
  explicit AssistantService(std::string api_key,
                            std::uint32_t timeout_seconds = 30);

  bool IsEnabled() const { return !api_key_.empty(); }

  /**
   * 处理用户消息，调用 LLM 解析意图并返回结构化响应
   * @param message      用户输入的自然语言指令
   * @param context_json 前端传来的上下文 JSON 字符串（recent_ppts 等）
   * @param history      对话历史（最近 N 条）
   * @param out_response 输出的响应结构
   * @param error_message 错误信息（失败时填充）
   * @return 成功返回 true
   */
  bool Chat(const std::string& message,
            const std::string& context_json,
            const std::vector<ChatMessage>& history,
            AssistantResponse& out_response,
            std::string& error_message) const;

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

  std::string api_key_;
  std::uint32_t timeout_seconds_;
};
