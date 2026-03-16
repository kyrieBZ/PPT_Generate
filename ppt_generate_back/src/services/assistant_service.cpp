#include "services/assistant_service.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

#include "logger.h"

namespace {

constexpr const char* kQwenEndpoint =
    "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation";

constexpr const char* kDefaultModel = "qwen-turbo";

constexpr int kContextMessages = 10;  // 每次调 LLM 带入的最大历史条数

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total = size * nmemb;
  static_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
  return total;
}

std::string ExtractText(const nlohmann::json& resp) {
  try {
    if (resp.contains("output")) {
      const auto& output = resp["output"];
      if (output.contains("text") && output["text"].is_string()) {
        return output["text"].get<std::string>();
      }
      if (output.contains("choices") && output["choices"].is_array()) {
        for (const auto& choice : output["choices"]) {
          if (choice.contains("message") && choice["message"].contains("content")) {
            return choice["message"]["content"].get<std::string>();
          }
        }
      }
    }
  } catch (...) {}
  return {};
}

std::string ExtractJsonFromText(const std::string& text) {
  auto start = text.find('{');
  auto end   = text.rfind('}');
  if (start != std::string::npos && end != std::string::npos && end > start) {
    return text.substr(start, end - start + 1);
  }
  return text;
}

// 生成 UUID v4（简单实现，无需外部库）
std::string GenerateUUID() {
  static std::mt19937 rng(
      static_cast<std::uint32_t>(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  static std::uniform_int_distribution<int> dist(0, 15);
  static const char* hex = "0123456789abcdef";

  std::string uuid = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
  for (char& c : uuid) {
    if (c == 'x') {
      c = hex[dist(rng)];
    } else if (c == 'y') {
      c = hex[(dist(rng) & 0x3) | 0x8];
    }
  }
  return uuid;
}

// 当前时间的 ISO 8601 字符串（UTC）
std::string NowISO() {
  auto now  = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

// 截取首条消息的前 30 个字符作为会话标题
std::string MakeTitle(const std::string& message) {
  if (message.size() <= 30) return message;
  // 按 UTF-8 边界截取（简单处理：取前 30 字节后截到最近 ASCII 空格）
  std::string title = message.substr(0, 30);
  title += "...";
  return title;
}

}  // namespace

// ── 静态哑变量 ────────────────────────────────────────────────────────────────
std::string AssistantService::dummy_error_;

// ── 构造函数 ──────────────────────────────────────────────────────────────────
AssistantService::AssistantService(std::string api_key,
                                   std::uint32_t timeout_seconds,
                                   std::shared_ptr<MongoClient> mongo)
    : api_key_(std::move(api_key)),
      timeout_seconds_(timeout_seconds),
      mongo_(std::move(mongo)) {}

// ── System Prompt ─────────────────────────────────────────────────────────────
std::string AssistantService::BuildSystemPrompt() const {
  return R"(你是PPT生成系统的AI助手，名字叫"四夕丽人土土"，简称"四夕"。请以"四夕"自称，不要使用"土土"。
你的职责是帮助用户操作PPT生成系统，以友好、简洁的中文回复。

用户会用自然语言描述操作意图，你需要：
1. 理解用户意图
2. 如果是可执行的系统操作，以JSON格式返回action
3. 如果是普通对话或问题，以JSON格式返回友好回复，action设为null

可识别的 intent 类型：
- VIEW_PPT: 查看/预览/打开某个PPT，需要参数 ppt_id（从context的recent_ppts中匹配title找到id）和 ppt_title。用户说"查看"、"预览"、"打开"、"看一下"某个PPT时使用此intent
- DELETE_PPT: 删除PPT，需要参数 ppt_id（从context的recent_ppts中匹配title找到id）和 ppt_title
- GENERATE_PPT: 生成PPT，需要参数 topic（主题，必填）, page_count（页数，整数，默认10）, style（风格，可选，如business/tech/creative）
- NAVIGATE: 页面跳转，需要参数 page（可选值：history/generate/materials/templates/models）
- DOWNLOAD_PPT: 下载PPT，需要参数 ppt_id 和 ppt_title（从context的recent_ppts中匹配）
- LIST_TEMPLATES: 查看模板列表，无需额外参数
- UNKNOWN: 普通对话，无需action

严格按照以下JSON格式返回，不要输出任何其他内容，不要使用markdown代码块：
有操作时：{"reply":"回复文本","action":{"intent":"意图类型","params":{参数},"confirm_text":"操作确认提示文本"}}
无操作时：{"reply":"回复文本","action":null}

注意：
- reply 字段必须是友好的中文回复，简洁明了；回复中可用第一人称「四夕」使对话更亲切
- confirm_text 要清晰描述将要执行的操作，让用户明白后果
- 如果用户描述的PPT名称在recent_ppts中找不到匹配，在reply中告知用户，action设为null
- 删除操作的confirm_text要包含"此操作不可恢复"的提示
- VIEW_PPT 和 DELETE_PPT 都需要从 recent_ppts 中通过 title 模糊匹配找到对应的 ppt_id，匹配时忽略大小写和空格)";
}

std::string AssistantService::BuildUserPrompt(const std::string& message,
                                               const std::string& context_json) const {
  std::ostringstream oss;
  oss << "用户指令：" << message;
  if (!context_json.empty() && context_json != "{}") {
    oss << "\n\n当前上下文：" << context_json;
  }
  return oss.str();
}

// ── Qwen API 调用 ─────────────────────────────────────────────────────────────
std::string AssistantService::CallQwenAPI(const std::string& system_prompt,
                                           const std::string& user_prompt,
                                           const std::vector<ChatMessage>& history,
                                           std::string& error_message) const {
  nlohmann::json messages = nlohmann::json::array();
  messages.push_back({{"role", "system"}, {"content", system_prompt}});

  const size_t max_history = kContextMessages;
  size_t start = history.size() > max_history ? history.size() - max_history : 0;
  for (size_t i = start; i < history.size(); ++i) {
    messages.push_back({{"role", history[i].role}, {"content", history[i].content}});
  }
  messages.push_back({{"role", "user"}, {"content", user_prompt}});

  nlohmann::json payload = {
      {"model", kDefaultModel},
      {"input", {{"messages", messages}}},
      {"parameters", {{"result_format", "message"}, {"temperature", 0.3}}}
  };

  const std::string body = payload.dump();
  std::string response_body;

  CURL* curl = curl_easy_init();
  if (!curl) {
    error_message = "curl_easy_init 失败";
    return {};
  }

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key_).c_str());
  headers = curl_slist_append(headers, "Content-Type: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, kQwenEndpoint);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds_));
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    error_message = std::string("curl 请求失败: ") + curl_easy_strerror(res);
    return {};
  }

  try {
    auto resp_json = nlohmann::json::parse(response_body);
    return ExtractText(resp_json);
  } catch (const std::exception& e) {
    error_message = std::string("解析 LLM 响应失败: ") + e.what();
    return {};
  }
}

// ── LLM 响应解析 ──────────────────────────────────────────────────────────────
bool AssistantService::ParseLLMResponse(const std::string& llm_text,
                                         AssistantResponse& out_response) const {
  if (llm_text.empty()) {
    out_response.reply = "抱歉，我暂时无法响应，请稍后再试。";
    out_response.has_action = false;
    return true;
  }

  try {
    const std::string json_str = ExtractJsonFromText(llm_text);
    auto j = nlohmann::json::parse(json_str);

    if (j.contains("reply") && j["reply"].is_string()) {
      out_response.reply = j["reply"].get<std::string>();
    } else {
      out_response.reply = llm_text;
      out_response.has_action = false;
      return true;
    }

    if (j.contains("action") && j["action"].is_object()) {
      const auto& action_j = j["action"];
      AssistantAction action;

      if (action_j.contains("intent") && action_j["intent"].is_string()) {
        action.intent = action_j["intent"].get<std::string>();
      }
      if (action_j.contains("confirm_text") && action_j["confirm_text"].is_string()) {
        action.confirm_text = action_j["confirm_text"].get<std::string>();
      }

      if (action_j.contains("params") && action_j["params"].is_object()) {
        const auto& params = action_j["params"];

        auto get_str = [&](const char* key) -> std::string {
          if (params.contains(key) && params[key].is_string()) {
            return params[key].get<std::string>();
          }
          return {};
        };

        action.ppt_id    = get_str("ppt_id");
        action.ppt_title = get_str("ppt_title");
        action.topic     = get_str("topic");
        action.style     = get_str("style");
        action.page      = get_str("page");

        if (params.contains("page_count")) {
          if (params["page_count"].is_number_integer()) {
            action.page_count = params["page_count"].get<int>();
          } else if (params["page_count"].is_string()) {
            try {
              action.page_count = std::stoi(params["page_count"].get<std::string>());
            } catch (...) {}
          }
        }
      }

      if (!action.intent.empty() && action.intent != "UNKNOWN") {
        out_response.has_action = true;
        out_response.action = std::move(action);
      } else {
        out_response.has_action = false;
      }
    } else {
      out_response.has_action = false;
    }

    return true;
  } catch (const std::exception& e) {
    Logger::Warn(std::string("AssistantService: JSON 解析失败，降级为纯文本: ") + e.what());
    out_response.reply = llm_text;
    out_response.has_action = false;
    return true;
  }
}

// ── 原有无状态对话（向后兼容）────────────────────────────────────────────────
bool AssistantService::Chat(const std::string& message,
                             const std::string& context_json,
                             const std::vector<ChatMessage>& history,
                             AssistantResponse& out_response,
                             std::string& error_message) const {
  if (!IsEnabled()) {
    out_response.reply = "AI 助手暂未配置，请联系管理员。";
    out_response.has_action = false;
    return true;
  }

  const std::string system_prompt = BuildSystemPrompt();
  const std::string user_prompt   = BuildUserPrompt(message, context_json);
  const std::string llm_text = CallQwenAPI(system_prompt, user_prompt, history, error_message);

  if (llm_text.empty() && !error_message.empty()) {
    out_response.reply = "抱歉，AI 服务暂时不可用，请稍后再试。";
    out_response.has_action = false;
    return true;
  }

  return ParseLLMResponse(llm_text, out_response);
}

// ── 私有工具方法 ──────────────────────────────────────────────────────────────
bool AssistantService::ValidateSession(const std::string& session_id,
                                        std::uint64_t user_id,
                                        std::string& error_message) {
  if (!IsPersistenceEnabled()) {
    error_message = "会话持久化未启用（MongoDB 未连接）";
    return false;
  }
  auto sessions = mongo_->Find(
      "chat_sessions",
      {{"session_id", session_id}, {"user_id", static_cast<std::int64_t>(user_id)}});
  if (sessions.empty()) {
    error_message = "会话不存在或无权访问";
    return false;
  }
  return true;
}

bool AssistantService::SaveMessage(const std::string& session_id,
                                    std::uint64_t user_id,
                                    const std::string& role,
                                    const std::string& content) {
  if (!IsPersistenceEnabled()) return false;
  nlohmann::json doc = {
      {"message_id", GenerateUUID()},
      {"session_id", session_id},
      {"user_id",    static_cast<std::int64_t>(user_id)},
      {"role",       role},
      {"content",    content},
      {"timestamp",  NowISO()}
  };
  return mongo_->InsertOne("chat_messages", doc);
}

std::vector<ChatMessage> AssistantService::LoadRecentMessages(
    const std::string& session_id, int n) {
  std::vector<ChatMessage> result;
  if (!IsPersistenceEnabled()) return result;

  // 倒序取最近 N 条，再反转为时间升序
  auto docs = mongo_->Find(
      "chat_messages",
      {{"session_id", session_id}},
      {{"timestamp", -1}},
      n);
  std::reverse(docs.begin(), docs.end());

  for (const auto& doc : docs) {
    ChatMessage msg;
    if (doc.contains("role") && doc["role"].is_string()) {
      msg.role = doc["role"].get<std::string>();
    }
    if (doc.contains("content") && doc["content"].is_string()) {
      msg.content = doc["content"].get<std::string>();
    }
    if (doc.contains("timestamp") && doc["timestamp"].is_string()) {
      msg.timestamp = doc["timestamp"].get<std::string>();
    }
    if (!msg.role.empty() && !msg.content.empty()) {
      result.push_back(std::move(msg));
    }
  }
  return result;
}

void AssistantService::TouchSession(const std::string& session_id,
                                     const std::string& first_message) {
  if (!IsPersistenceEnabled()) return;
  nlohmann::json update = {{"updated_at", NowISO()}};
  if (!first_message.empty()) {
    update["title"] = MakeTitle(first_message);
  }
  mongo_->UpdateOne(
      "chat_sessions",
      {{"session_id", session_id}},
      update);
}

// ── 会话管理公开方法 ──────────────────────────────────────────────────────────
bool AssistantService::CreateSession(std::uint64_t user_id,
                                      ChatSession& out_session,
                                      std::string& error_message) {
  if (!IsPersistenceEnabled()) {
    error_message = "会话持久化未启用（MongoDB 未连接）";
    return false;
  }

  const std::string now = NowISO();
  out_session.session_id = GenerateUUID();
  out_session.user_id    = user_id;
  out_session.title      = "新会话";
  out_session.created_at = now;
  out_session.updated_at = now;

  nlohmann::json doc = {
      {"session_id", out_session.session_id},
      {"user_id",    static_cast<std::int64_t>(user_id)},
      {"title",      out_session.title},
      {"created_at", out_session.created_at},
      {"updated_at", out_session.updated_at}
  };

  if (!mongo_->InsertOne("chat_sessions", doc)) {
    error_message = "创建会话失败，请稍后再试";
    return false;
  }
  return true;
}

std::vector<ChatSession> AssistantService::ListSessions(std::uint64_t user_id,
                                                         int limit,
                                                         std::string& error_message) {
  std::vector<ChatSession> result;
  if (!IsPersistenceEnabled()) {
    error_message = "会话持久化未启用";
    return result;
  }

  auto docs = mongo_->Find(
      "chat_sessions",
      {{"user_id", static_cast<std::int64_t>(user_id)}},
      {{"updated_at", -1}},
      limit);

  for (const auto& doc : docs) {
    ChatSession s;
    auto get_str = [&](const char* key) -> std::string {
      if (doc.contains(key) && doc[key].is_string()) return doc[key].get<std::string>();
      return {};
    };
    s.session_id = get_str("session_id");
    s.user_id    = user_id;
    s.title      = get_str("title");
    s.created_at = get_str("created_at");
    s.updated_at = get_str("updated_at");
    if (!s.session_id.empty()) {
      result.push_back(std::move(s));
    }
  }
  return result;
}

bool AssistantService::DeleteSession(const std::string& session_id,
                                      std::uint64_t user_id,
                                      std::string& error_message) {
  if (!ValidateSession(session_id, user_id, error_message)) return false;

  // 先删消息，再删会话
  mongo_->DeleteMany("chat_messages", {{"session_id", session_id}});
  mongo_->DeleteMany("chat_sessions",
                     {{"session_id", session_id},
                      {"user_id", static_cast<std::int64_t>(user_id)}});
  return true;
}

std::vector<ChatMessage> AssistantService::GetMessages(const std::string& session_id,
                                                        std::uint64_t user_id,
                                                        int limit,
                                                        std::string& error_message) {
  std::vector<ChatMessage> empty;
  if (!ValidateSession(session_id, user_id, error_message)) return empty;

  auto docs = mongo_->Find(
      "chat_messages",
      {{"session_id", session_id}},
      {{"timestamp", 1}},  // 时间升序
      limit);

  std::vector<ChatMessage> result;
  for (const auto& doc : docs) {
    ChatMessage msg;
    if (doc.contains("role") && doc["role"].is_string()) {
      msg.role = doc["role"].get<std::string>();
    }
    if (doc.contains("content") && doc["content"].is_string()) {
      msg.content = doc["content"].get<std::string>();
    }
    if (doc.contains("timestamp") && doc["timestamp"].is_string()) {
      msg.timestamp = doc["timestamp"].get<std::string>();
    }
    if (!msg.role.empty()) result.push_back(std::move(msg));
  }
  return result;
}

bool AssistantService::ChatInSession(const std::string& session_id,
                                      std::uint64_t user_id,
                                      const std::string& user_message,
                                      const std::string& context_json,
                                      AssistantResponse& out_response,
                                      std::string& error_message) {
  if (!IsEnabled()) {
    out_response.reply = "AI 助手暂未配置，请联系管理员。";
    out_response.has_action = false;
    return true;
  }

  // 1. 验证会话归属（同时校验持久化是否启用）
  if (!ValidateSession(session_id, user_id, error_message)) return false;

  // 2. 保存用户消息
  SaveMessage(session_id, user_id, "user", user_message);

  // 3. 读取历史构建上下文
  auto history = LoadRecentMessages(session_id, kContextMessages);

  // 4. 调用 LLM
  const std::string system_prompt = BuildSystemPrompt();
  const std::string user_prompt   = BuildUserPrompt(user_message, context_json);

  // history 中已包含本次用户消息（SaveMessage 刚写入后 LoadRecent 能读到），
  // 但 CallQwenAPI 会再追加一条 user 消息，所以传入 history 时排除最后一条
  std::vector<ChatMessage> history_without_last = history;
  if (!history_without_last.empty() &&
      history_without_last.back().role == "user") {
    history_without_last.pop_back();
  }

  std::string llm_error;
  const std::string llm_text = CallQwenAPI(
      system_prompt, user_prompt, history_without_last, llm_error);

  if (llm_text.empty() && !llm_error.empty()) {
    Logger::Error("AssistantService::ChatInSession LLM error: " + llm_error);
    out_response.reply = "抱歉，AI 服务暂时不可用，请稍后再试。";
    out_response.has_action = false;
  } else {
    ParseLLMResponse(llm_text, out_response);
  }

  // 5. 保存 AI 回复
  if (!out_response.reply.empty()) {
    SaveMessage(session_id, user_id, "assistant", out_response.reply);
  }

  // 6. 更新会话时间，首次消息时设置 title
  bool is_first = (mongo_->Count("chat_messages", {{"session_id", session_id}}) <= 2);
  TouchSession(session_id, is_first ? user_message : "");

  return true;
}
