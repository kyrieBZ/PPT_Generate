#include "services/assistant_service.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <sstream>
#include <stdexcept>

#include "logger.h"

namespace {

constexpr const char* kQwenEndpoint =
    "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation";

constexpr const char* kDefaultModel = "qwen-turbo";

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total = size * nmemb;
  static_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
  return total;
}

// 从 LLM 响应 JSON 中提取文本内容
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

// 从原始文本中提取 JSON（处理 LLM 可能输出 markdown 代码块的情况）
std::string ExtractJsonFromText(const std::string& text) {
  // 尝试找到 { 开头的 JSON
  auto start = text.find('{');
  auto end = text.rfind('}');
  if (start != std::string::npos && end != std::string::npos && end > start) {
    return text.substr(start, end - start + 1);
  }
  return text;
}

}  // namespace

AssistantService::AssistantService(std::string api_key,
                                   std::uint32_t timeout_seconds)
    : api_key_(std::move(api_key)), timeout_seconds_(timeout_seconds) {}

std::string AssistantService::BuildSystemPrompt() const {
  return R"(你是PPT生成系统的AI助手，名字叫"四夕丽人土土"（简称土土）。
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
- reply 字段必须是友好的中文回复，简洁明了
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

std::string AssistantService::CallQwenAPI(const std::string& system_prompt,
                                           const std::string& user_prompt,
                                           const std::vector<ChatMessage>& history,
                                           std::string& error_message) const {
  nlohmann::json messages = nlohmann::json::array();

  // 系统消息
  messages.push_back({{"role", "system"}, {"content", system_prompt}});

  // 历史消息（最多 10 条）
  const size_t max_history = 10;
  size_t start = history.size() > max_history ? history.size() - max_history : 0;
  for (size_t i = start; i < history.size(); ++i) {
    messages.push_back({{"role", history[i].role}, {"content", history[i].content}});
  }

  // 当前用户消息
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
    // 降级：把 LLM 原始文本作为回复
    out_response.reply = llm_text;
    out_response.has_action = false;
    return true;
  }
}

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
